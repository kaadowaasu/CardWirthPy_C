#include "event_content_dialog_eligibility.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/contentinitializer.hpp"
#include <stdexcept>
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/eventview.hpp"

#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/messageutils.hpp"

#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/menu.hpp"
#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/script.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_layout.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/ui_passive_input.hpp"
#include "cwxeditor_src/lib/ui_text_layout.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::eventview {

namespace {

// DはUndoContentsがtree.startsを置換しても、表示中のTreeItem
// のインデックスパスを保持する。C++の親・ツリーリンクは弱参照ですぐ
// 無効になるため、表示の再構築前に公開済み行から同じパスを復元する。
[[nodiscard]] std::vector<std::size_t> displayedContentPath(
    const ContentPtr& content,
    const std::unordered_map<TreeItemId, NodeInfo>& nodes,
    const std::unordered_map<const event::Content*, TreeItemId>& contentIds) {
    if (!content) return {};
    if (content->tree()) return content->ctPath();
    std::vector<std::size_t> path;
    auto current = content;
    while (current && path.size() <= nodes.size()) {
        const auto id = contentIds.find(current.get());
        if (id == contentIds.end()) break;
        const auto row = nodes.find(id->second);
        if (row == nodes.end() || row->second.kind != NodeInfo::Kind::Content ||
            row->second.content != current) break;
        path.push_back(row->second.contentIndex);
        current = row->second.parentContent;
        if (!current) {
            std::reverse(path.begin(), path.end());
            return path;
        }
    }
    // 公開済みのUIパスがなければ、任意の切り離されたContent
    // には厳密なモデル契約を維持し、不正な参照関係を隠さない。
    return content->ctPath();
}

// Dの履歴用コピーは入力元オブジェクトの下位カウンターへ登録
// する。後の資源名変更を保持中の取り消し状態へ反映しつつ、そ
// のコピーを追加の使用中シナリオの参照として数えないため。
template <typename Model>
[[nodiscard]] auto duplicateHistorySnapshot(const std::shared_ptr<Model>& source) {
    auto saved = source->dup();
    if (const auto counter = source->useCounter())
        saved->setUseCounter(counter->sub(),
            ::cwxeditor::cwx::usecounter::renameInfo(source));
    return saved;
}

[[nodiscard]] String templateMenuText(const structs::EvTemplate& value) {
    // DはMenuPropsでアクセスキーのエスケープ・挿入を行う。共通
    // MenuItemはホットキーをshortcut項目へ別に保存する。
    return ::cwxeditor::cwx::menu::MenuProps::buildMenu(
        value.name.empty() ? String{"（無題）"} : value.name, value.mnemonic, {}, false);
}

constexpr std::string_view kClipboardRoot = "CardWirthClipboard";
constexpr std::string_view kClipboardMime = "application/x-cardwirth-event+xml";
constexpr float kEventGraphRowHeight = 19.0F;
constexpr float kEventGraphLineNumberWidth = 24.0F;
constexpr float kWarningIconSize = 16.0F;
[[nodiscard]] float graphScale() noexcept { return lib::editor_ui_text_scale(); }
[[nodiscard]] float graphPx(const float value) noexcept { return value * graphScale(); }
[[nodiscard]] float graphRowHeight() noexcept { return graphPx(kEventGraphRowHeight); }

[[nodiscard]] lib::Rect warningBounds(const float row_y,
                                      const float detail_x) noexcept {
    const float size = graphPx(kWarningIconSize);
    return {detail_x - size - graphPx(5.0F),
            row_y + (graphRowHeight() - size) * 0.5F, size, size};
}

[[nodiscard]] bool pointInRect(const lib::Rect bounds,
                               const lib::Point point) noexcept {
    return point.x >= bounds.x && point.y >= bounds.y &&
           point.x < bounds.x + bounds.width &&
           point.y < bounds.y + bounds.height;
}

[[nodiscard]] String warningText(lib::Array<String> warnings) {
    std::ranges::sort(warnings);
    const auto unique = std::ranges::unique(warnings);
    warnings.erase(unique.begin(), unique.end());
    String result;
    for (const auto& warning : warnings) {
        if (warning.empty()) continue;
        if (!result.empty()) result.push_back('\n');
        result += warning;
    }
    return result;
}

using ContentTextResolver = std::function<String(const event::Content&)>;
using ContentNameResolver = std::function<String(types::CType)>;

[[nodiscard]] std::optional<String> resolveContentName(
    const types::CType type, const ContentNameResolver& resolver,
    const std::weak_ptr<const int>& life) {
    if (life.expired()) return std::nullopt;
    if (resolver) {
        auto value = resolver(type);
        if (life.expired()) return std::nullopt;
        if (!value.empty()) return value;
    }
    if (life.expired()) return std::nullopt;
    const auto detail = event::contentDetail(type);
    if (!detail.names.empty() && !detail.names.front().empty())
        return detail.names.front();
    return detail.type;
}

[[nodiscard]] std::optional<String> formatContentText(
    const ContentPtr& content, const ContentTextResolver& textResolver,
    const ContentNameResolver& nameResolver,
    const std::weak_ptr<const int>& life, const bool useTextResultVerbatim,
    const bool startNameFirst) {
    if (life.expired()) return std::nullopt;
    if (!content) return String{};
    const auto type = content->type();
    const auto name = content->name();
    const auto detail = content->detail();

    // EventTreeViewのDの状態表示経路は、空の結果も含めcomm.contentText
    // をそのまま公開する。他の行文字列の呼び出し側は、
    // 空ならローカライズしたコンテント名へ切り替える。
    if (useTextResultVerbatim && textResolver) {
        auto value = textResolver(*content);
        if (life.expired()) return std::nullopt;
        return value;
    }
    if (startNameFirst && type == types::CType::Start && !name.empty())
        return name;
    if (textResolver) {
        auto value = textResolver(*content);
        if (life.expired()) return std::nullopt;
        if (!value.empty()) return value;
    }

    const auto displayName = resolveContentName(type, nameResolver, life);
    if (!displayName) return std::nullopt;
    if (!name.empty() && displayName->empty()) return name;
    if (!name.empty()) return *displayName + ": " + name;
    if (!displayName->empty()) return *displayName;
    if (!detail.names.empty()) return detail.names.front();
    return detail.type;
}

[[nodiscard]] String makeEventViewId() {
    static std::atomic_uint64_t next{1};
    return "event-view-" + std::to_string(next.fetch_add(1, std::memory_order_relaxed));
}

[[nodiscard]] String lower(StringView value) {
    String result;
    result.reserve(value.size());
    for (const unsigned char character : value)
        result.push_back(static_cast<char>(std::tolower(character)));
    return result;
}

[[nodiscard]] bool containsText(StringView haystack, StringView needle) {
    if (needle.empty()) return true;
    return lower(haystack).find(lower(needle)) != String::npos;
}

[[nodiscard]] bool sameFire(const FireCondition& left, const FireCondition& right) {
    if (left.kind != right.kind) return false;
    if (left.kind == FireKind::KeyCode) {
        return left.keyCode.keyCode == right.keyCode.keyCode &&
               left.keyCode.kind == right.keyCode.kind;
    }
    return left.kind != FireKind::Round || left.round == right.round;
}

// DのSystem.convFireKeyCodeに合わせ、キーコード発火
// にはタイミングの印を付ける。成功は末尾に○、失敗は×、未所持は先頭に！。
[[nodiscard]] String keyCodeWithTiming(const ::cwxeditor::cwx::event::FKeyCode& value) {
    switch (value.kind) {
    case types::FKCKind::Success: return value.keyCode + "○";
    case types::FKCKind::Failure: return value.keyCode + "×";
    case types::FKCKind::HasNot: return "！" + value.keyCode;
    default: return value.keyCode;
    }
}

// keyCodeWithTimingの逆変換。DのSystem.toFKeyCodeに対応する。
[[nodiscard]] ::cwxeditor::cwx::event::FKeyCode keyCodeFromText(String text) {
    ::cwxeditor::cwx::event::FKeyCode result;
    const auto ends = [&text](const StringView suffix) {
        return text.size() >= suffix.size() &&
               text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    if (ends("○")) {
        text.erase(text.size() - StringView{"○"}.size());
        result.kind = types::FKCKind::Success;
    } else if (ends("×")) {
        text.erase(text.size() - StringView{"×"}.size());
        result.kind = types::FKCKind::Failure;
    } else if (text.rfind("！", 0) == 0) {
        text.erase(0, StringView{"！"}.size());
        result.kind = types::FKCKind::HasNot;
    } else {
        result.kind = types::FKCKind::Use;
    }
    result.keyCode = std::move(text);
    return result;
}

[[nodiscard]] String numberText(const std::size_t value) {
    return std::to_string(static_cast<unsigned long long>(value));
}

[[nodiscard]] String fireKindText(const FireKind kind) {
    switch (kind) {
    case FireKind::Enter: return "enter";
    case FireKind::Escape: return "escape";
    case FireKind::Lose: return "lose";
    case FireKind::EveryRound: return "everyRound";
    case FireKind::RoundEnd: return "roundEnd";
    case FireKind::Round0: return "round0";
    case FireKind::KeyCode: return "keyCode";
    case FireKind::Round: return "round";
    }
    return {};
}

[[nodiscard]] std::optional<FireKind> fireKindFromText(StringView value) {
    const auto text = lower(value);
    if (text == "enter") return FireKind::Enter;
    if (text == "escape") return FireKind::Escape;
    if (text == "lose") return FireKind::Lose;
    if (text == "everyround") return FireKind::EveryRound;
    if (text == "roundend") return FireKind::RoundEnd;
    if (text == "round0") return FireKind::Round0;
    if (text == "keycode") return FireKind::KeyCode;
    if (text == "round") return FireKind::Round;
    return std::nullopt;
}

[[nodiscard]] bool isMove(const lib::DropOperation value) {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(lib::DropOperation::Move)) != 0U;
}

[[nodiscard]] bool isCopy(const lib::DropOperation value) {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(lib::DropOperation::Copy)) != 0U;
}

[[nodiscard]] bool hasShift(const lib::CommandGesture gesture) {
    const auto shift = static_cast<std::uint32_t>(lib::Modifiers::Shift);
    return (static_cast<std::uint32_t>(gesture.modifiers) & shift) != 0U &&
           (static_cast<std::uint32_t>(gesture.accelerator_modifiers) & shift) == 0U;
}

[[nodiscard]] bool hasControl(const lib::Modifiers modifiers,
                              const std::optional<bool> physical_control = std::nullopt) {
    const bool semantic = (static_cast<std::uint32_t>(modifiers) &
            static_cast<std::uint32_t>(lib::Modifiers::Control)) != 0U;
    // Dの配置操作は、AppleのCommandの別名ではなく物理Ctrlを
    // 読む。ALLとネイティブ由来の情報がない呼び出し側の動作は維持する。
    return lib::editor_window_host_options().all_mode ? semantic
                                                     : physical_control.value_or(semantic);
}

[[nodiscard]] bool hasControl(const lib::CommandGesture gesture) {
    const auto control = static_cast<std::uint32_t>(lib::Modifiers::Control);
    if ((static_cast<std::uint32_t>(gesture.accelerator_modifiers) & control) != 0U)
        return false;
    return hasControl(gesture.modifiers, gesture.physical_control);
}

[[nodiscard]] bool hasAlt(const lib::Modifiers modifiers) {
    return (static_cast<std::uint32_t>(modifiers) &
            static_cast<std::uint32_t>(lib::Modifiers::Alt)) != 0U;
}

[[nodiscard]] bool hasAlt(const lib::CommandGesture gesture) {
    const auto alt = static_cast<std::uint32_t>(lib::Modifiers::Alt);
    return (static_cast<std::uint32_t>(gesture.modifiers) & alt) != 0U &&
           (static_cast<std::uint32_t>(gesture.accelerator_modifiers) & alt) == 0U;
}

[[nodiscard]] EventTreePtr findTreeById(EventTreeOwner* owner,
                                        const StringView id) {
    if (!owner || id.empty()) return {};
    for (const auto& tree : owner->trees())
        if (tree && tree->objectId() == id) return tree;
    return {};
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

[[nodiscard]] String groupLabel(const types::CTypeGroup group) {
    using types::CTypeGroup;
    switch (group) {
    case CTypeGroup::Terminal: return "開始/終端";
    case CTypeGroup::Visual: return "外観操作";
    case CTypeGroup::Standard: return "基本";
    case CTypeGroup::Data: return "変数操作/分岐";
    case CTypeGroup::Utility: return "状況分岐";
    case CTypeGroup::Branch: return "保有分岐";
    case CTypeGroup::Get: return "取得";
    case CTypeGroup::Lost: return "喪失";
    case CTypeGroup::Variant: return "演算";
    }
    return {};
}

[[nodiscard]] String contentCommand(const types::CType type) {
    return "content:" + std::to_string(static_cast<std::int32_t>(type));
}

[[nodiscard]] std::optional<types::CType> contentCommandType(
    const StringView command) {
    constexpr StringView prefix = "content:";
    if (!command.starts_with(prefix)) return std::nullopt;
    try {
        const auto raw = std::stoi(String(command.substr(prefix.size())));
        if (raw < static_cast<std::int32_t>(types::CType::Start) ||
            raw > static_cast<std::int32_t>(types::CType::CustomXmlContent))
            return std::nullopt;
        return static_cast<types::CType>(raw);
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace

EventView::EventView(Commons comm, Props prop, EventTreeOwner& area,
                     UndoManager& undo, const bool readOnly)
    : comm_(std::move(comm)), prop_(std::move(prop)), area_(&area), undo_(&undo),
      readOnly_(readOnly) {
    id_ = makeEventViewId();
    contentAutoEdit_ = prop_.etc.contentsAutoOpen;
    contentInsertFirst_ = prop_.etc.contentsInsertFirst;
    contentPutMode_ = prop_.etc.contentsPutMode == 0 ? PutMode::Quick
                    : prop_.etc.contentsPutMode == 2 ? PutMode::Continue
                                                     : PutMode::Select;
    contentToolArrow_ = contentPutMode_ != PutMode::Quick;
}

EventView::EventView(Commons comm, Props prop, EventTreeOwner* area,
                     UndoManager& undo, const bool readOnly)
    : comm_(std::move(comm)), prop_(std::move(prop)), area_(area), undo_(&undo),
      readOnly_(readOnly) {
    id_ = makeEventViewId();
    contentAutoEdit_ = prop_.etc.contentsAutoOpen;
    contentInsertFirst_ = prop_.etc.contentsInsertFirst;
    contentPutMode_ = prop_.etc.contentsPutMode == 0 ? PutMode::Quick
                    : prop_.etc.contentsPutMode == 2 ? PutMode::Continue
                                                     : PutMode::Select;
    contentToolArrow_ = contentPutMode_ != PutMode::Quick;
}

EventView::EventView(Commons comm, Props prop, EventTreeOwnerPtr area,
                     UndoManager& undo, const bool readOnly)
    : comm_(std::move(comm)), prop_(std::move(prop)), area_(area.get()),
      areaHolder_(std::move(area)), undo_(&undo), readOnly_(readOnly) {
    id_ = makeEventViewId();
    contentAutoEdit_ = prop_.etc.contentsAutoOpen;
    contentInsertFirst_ = prop_.etc.contentsInsertFirst;
    contentPutMode_ = prop_.etc.contentsPutMode == 0 ? PutMode::Quick
                    : prop_.etc.contentsPutMode == 2 ? PutMode::Continue
                                                     : PutMode::Select;
    contentToolArrow_ = contentPutMode_ != PutMode::Quick;
}

EventView::~EventView() {
    // 履歴がこのツリーを保持していても、進行中のスクロールバー操作を無効にする。
    if (cards_) { ++cards_->scroll_update_revision; cards_->on_scroll_changed = {}; }
    paletteLifetime_.token.reset();
    unbindAutoHidePointerObserver();
    closeCardPreview();
    if (paletteTarget_) paletteTarget_->owner = nullptr;
    floatingPalette_.reset();
    cancelPendingNameEdit();
    unbindDragDrop();
}


void EventView::closeCardPreview() noexcept {
    ++cardPreviewGeneration_;
    auto preview = std::move(cardPreview_);
    cardPreviewOwner_.reset();
    // バックエンドのコールバックがこの表示を終了し得るため、その後にメンバーへアクセスしない。
    if (preview) preview->dispose();
}

void EventView::triggerCardPreview(const lib::PointerEvent& pointer) {
    namespace area = ::cwxeditor::cwx::area;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto tree = cards_;
    if (!tree) return;
    const auto height = lib::item_row_height(*tree,
        lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height));
    const auto hit = lib::hit_test_tree_row(*tree, tree->bounds(), pointer.position, height);
    if (!hit || *hit >= tree->nodes.size()) { closeCardPreview(); return; }
    const auto id = tree->nodes[*hit].id;
    const auto* info = nodeInfo(id);
    // DでAbstractSpCardを持つのはカード所有元の行だけ。イベント・発火の行には含めない。
    if (!info || info->kind != NodeInfo::Kind::Owner) { closeCardPreview(); return; }
    std::shared_ptr<areaviewutils::AbstractSpCard> card;
    const auto model = areaHolder_;
    if (const auto ownerArea = std::dynamic_pointer_cast<area::Area>(model)) {
        for (const auto& value : ownerArea->cards())
            if (value && value.get() == info->owner) { card = value; break; }
    } else if (const auto battle = std::dynamic_pointer_cast<area::Battle>(model)) {
        for (const auto& value : battle->cards())
            if (value && value.get() == info->owner) { card = value; break; }
    }
    if (!card) { closeCardPreview(); return; }
    if (cardPreviewOwner_ == card && cardPreview_ && cardPreview_->visible()) return;
    const auto position = lib::tree_visible_position(*tree, id);
    if (!position || *position < tree->top_index) { closeCardPreview(); return; }
    const auto bounds = tree->bounds();
    const auto row = lib::tree_row_geometry(*tree, bounds, *hit,
        bounds.y + static_cast<float>(*position - tree->top_index) * height, height);
    if (!row) { closeCardPreview(); return; }
    const auto screenX = row->row.x + pointer.screen_position.x - pointer.position.x;
    const auto screenY = row->row.y + row->row.height + pointer.screen_position.y - pointer.position.y;
    const auto propsProvider = comm_.editorProps;
    const auto skinProvider = cardPreviewSkin_;
    const auto debugProvider = cardPreviewEnemyDebug_;
    const auto summary = comm_.summary.lock();
    const auto services = comm_.services;
    // 旧ポップアップの破棄前に、この要求を現在のものとして確定する。バック
    // エンドのコールバックが、より新しいホバー要求を同期公開し得るため。
    const auto generation = cardPreviewGeneration_ + 1U;
    closeCardPreview();
    if (life.expired() || cardPreviewGeneration_ != generation) return;
    const auto current = [this, life, generation, model, card, id] {
        if (life.expired() || cardPreviewGeneration_ != generation || areaHolder_ != model)
            return false;
        const auto* rowInfo = nodeInfo(id);
        if (!rowInfo || rowInfo->kind != NodeInfo::Kind::Owner || rowInfo->owner != card.get())
            return false;
        if (const auto a = std::dynamic_pointer_cast<area::Area>(model))
            return std::any_of(a->cards().begin(), a->cards().end(),
                [&](const auto& value) { return value == card; });
        if (const auto b = std::dynamic_pointer_cast<area::Battle>(model))
            return std::any_of(b->cards().begin(), b->cards().end(),
                [&](const auto& value) { return value == card; });
        return false;
    };
    auto* props = propsProvider ? propsProvider() : nullptr;
    if (!current() || !props || !skinProvider) return;
    const auto skin = skinProvider();
    if (!current() || !skin) return;
    const bool smoothing = props->var().smoothingCard.value;
    bool debug = props->var().viewEnemyCardDebug.value;
    if (debugProvider) debug = debugProvider() || debug;
    if (!current()) return;
    std::shared_ptr<areaviewutils::PileImage> image;
    if (const auto menu = std::dynamic_pointer_cast<area::MenuCard>(card)) {
        String title = menu->name();
        if (menu->expandSPChars())
            title = messageutils::createSPCharPreview(*props, summary.get(), skin,
                menu->useCounter().get(), title, false);
        if (!current()) return;
        image = areaviewutils::createMenuCardImage<areaviewutils::PileImage>(
            *props, *skin, summary.get(), title, menu->paths(), 0, 0, 100,
            smoothing, menu->layer());
    } else if (const auto enemy = std::dynamic_pointer_cast<area::EnemyCard>(card)) {
        areaviewutils::Commons common(*props, *skin);
        common.summary = summary.get();
        common.services = services;
        const auto cast = summary ? summary->cwCast(enemy->id()) : nullptr;
        image = areaviewutils::createCastCardImage<areaviewutils::PileImage>(
            common, *skin, summary.get(), enemy.get(), cast.get(), 0, 0, 100,
            smoothing, debug, enemy->layer());
    }
    if (!current() || !image) return;
    const auto preview = std::make_shared<areaviewutils::Preview>(*props, tree, services);
    if (!current()) return;
    cardPreview_ = preview;
    cardPreviewOwner_ = card;
    preview->image(std::move(image), static_cast<std::int32_t>(screenX),
        static_cast<std::int32_t>(screenY), static_cast<std::int32_t>(height));
    if (!current()) { preview->dispose(); return; }
    preview->show();
}

void EventView::applyDisplayPreferences(const bool detail, const bool lineNumber,
                                        const int slope, const int slopeMaximum) {
    showEventTreeDetail_ = detail;
    eventGraphDetailWidth_ = std::max(120.0F, prop_.etc.eventGraphDetailWidth);
    showLineNumber_ = lineNumber;
    const bool allMode = lib::editor_window_host_options().all_mode;
    const auto defaultMaximum = static_cast<int>(std::clamp<std::int64_t>(
        std::int64_t{16} * std::max(1, dutils::dpiMuls()), 0,
        std::numeric_limits<int>::max()));
    eventTreeSlopeMaximum_ = allMode ? 32 : (slopeMaximum < 0 ? defaultMaximum : slopeMaximum);
    eventTreeSlopeMaximumConfigured_ = true;
    if (slope_) slope_->maximum = static_cast<double>(eventTreeSlopeMaximum_);
    applyEventTreeSlope(slope);
    rebuildContentTable();
    refreshToolbarState();
}

void EventView::eventEditorStyleChanged() {
    // DのrefEventEditorStyleはupdatePosAllと
    // 再描画を呼ぶ。同じ軽量な通知境界を維持し、paintEventGraph
    // が設定を確認して、インライン編集中の内容を取り消さずに図を更新する。
    invalidateEventGraph();
}

void EventView::applyEventTreeSlope(const int value) {
    eventTreeSlope_ = std::clamp(value, 0, eventTreeSlopeMaximum_);
    if (slope_) {
        slope_->maximum = static_cast<double>(eventTreeSlopeMaximum_);
        slope_->value = eventTreeSlope_;
    }
    if (panel_) {
        rebuildGraphRows();
        invalidateEventGraph();
    }
}

void EventView::editEventTreeSlope(const int value) {
    applyEventTreeSlope(value);
    // Dはプレビューや取消でも共通設定を書き、refEventTreeSlope
    // を公開する。永続化と表示間の通知はホストが担当する。
    slopeSettingChanged.call(eventTreeSlope_);
}

void EventView::cancelPendingNameEdit() noexcept {
    ++nameEditRequest_;
    if (nameEditTimer_ && comm_.services.dispatcher)
        comm_.services.dispatcher->cancel_timer(*nameEditTimer_);
    nameEditTimer_.reset();
}

ContentToolState EventView::contentToolState() const noexcept {
    const auto mode = contentPutMode_ == PutMode::Quick ? 0
                    : contentPutMode_ == PutMode::Continue ? 2 : 1;
    return {contentToolArrow_, contentToolType_, mode, contentAutoEdit_,
            contentInsertFirst_};
}

void EventView::applyContentToolState(const ContentToolState& value) {
    contentToolArrow_ = value.arrow;
    contentToolType_ = value.type;
    contentPutMode_ = value.put_mode == 0 ? PutMode::Quick
                    : value.put_mode == 2 ? PutMode::Continue : PutMode::Select;
    contentAutoEdit_ = value.auto_open;
    contentInsertFirst_ = value.insert_first;
    prop_.etc.contentsPutMode = value.put_mode == 0 ? 0 : value.put_mode == 2 ? 2 : 1;
    prop_.etc.contentsAutoOpen = value.auto_open;
    prop_.etc.contentsInsertFirst = value.insert_first;
    refreshToolbarState();
}

void EventView::publishContentToolState() {
    const auto state = contentToolState();
    prop_.etc.contentsPutMode = state.put_mode;
    prop_.etc.contentsAutoOpen = state.auto_open;
    prop_.etc.contentsInsertFirst = state.insert_first;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    contentToolStateChanged.call(state);
    if (life.expired()) return;
    updateContentToolCursor();
}

void EventView::setReadOnly(const bool value) noexcept {
    readOnly_ = value;
    if (cards_) cards_->set_enabled(true);
    if (contents_) contents_->set_enabled(true);
    if (contentTable_) contentTable_->set_enabled(true);
    refreshToolbarState();
}

void EventView::ownerTitle(String value) {
    ownerTitle_ = std::move(value);
    if (!initialized_ || !cards_ || rootId_ == 0U) return;

    // DのrefreshTitleは所有元のラベルだけを変える。全行を
    // 再構築すると、折りたたんだ所有元の枝を開き、無関係な選択を乱す。
    const auto root = std::find_if(cards_->nodes.begin(), cards_->nodes.end(),
        [this](const lib::TreeNode& node) { return node.id == rootId_; });
    if (root == cards_->nodes.end()) return;
    root->text = ownerDisplayName();
    if (comm_.services.gui) comm_.services.gui->request_redraw(cards_);
}

void EventView::localVariables(flagmodel::FlagDirPtr root,
                               flagmodel::UseCounterPtr useCounter) {
    localVariableRoot_ = std::move(root);
    localVariableUseCounter_ = std::move(useCounter);
    if (!localVariables_) return;
    localVariables_->useCounter(localVariableUseCounter_);
    localVariables_->setDir(localVariableRoot_, true);
}

PanelPtr EventView::createControl(const Parent& parent) {
    ensureWidgets(parent);
    bindDragDrop();
    const auto control = panel_;
    refresh();
    return control;
}

void EventView::createTopBar() {
    const bool all_mode = lib::editor_window_host_options().all_mode;
    // DのSpinnerは設定済みINIT.ppisの上限を維持する
    // 。ウィジェット構築で読み込み済みの上限を16*dpiへ戻さな
    // い。直接構築した表示には既定値が必要で、ALLは32を保つ。
    if (all_mode || !eventTreeSlopeMaximumConfigured_) {
        eventTreeSlopeMaximum_ = all_mode ? 32 : static_cast<int>(std::clamp<std::int64_t>(
            std::int64_t{16} * std::max(1, dutils::dpiMuls()), 0,
            std::numeric_limits<int>::max()));
    }
    // 参照シナリオのペインを狭くしても全操作部を保持する。固
    // 定の横Panelでは帯幅の合計が分割の下限になるため、
    // コンテントパレットと同じ共通の折り返す帯配置を使う。
    topBarPanel_ = std::make_shared<lib::CoolBar>();
    topBarPanel_->locked = true;
    auto layout = std::make_shared<lib::LinearLayout>(
        all_mode ? lib::Orientation::Vertical : lib::Orientation::Horizontal);
    layout->spacing = 1.0F;
    layout->padding = {};
    topBarPanel_->set_layout(layout);
    topBarPanel_->set_layout_item({.row = 0U, .column = 0U,
                                   .horizontal = lib::Align::Stretch,
                                   .minimum = {240.0F, 28.0F},
                                   .preferred = {0.0F, 0.0F}});
    topBarPanel_->set_visible(prop_.etc.showEventToolBar);

    const auto tool = [this](String text, String command, const types::CType iconType) {
        lib::ToolItem item;
        item.text = std::move(text);
        item.icon = viewImage(command, iconType);
        item.command = std::move(command);
        return item;
    };
    const auto separator = [] {
        lib::ToolItem item;
        item.separator = true;
        return item;
    };

    toolbar_ = std::make_shared<lib::ToolBar>();
    toolbar_->tooltip_width_provider=[this, life=std::weak_ptr<const int>(paletteLifetime_.token)] { return life.expired() ? 0.0F : float(prop_.etc.toolTipWidth); };
    toolbar_->set_id(1001);
    // アイコン列全体を計測する。固定幅では「場面へ戻る」の追
    // 加後や大きいUI文字倍率で、下移動ボタンだけが別れる。
    toolbar_->set_layout_item({.minimum = {0.0F, 26.0F},
                               .preferred = {0.0F, 26.0F}});
    toolbar_->items = {
        tool("元に戻す", "undo", types::CType::LinkStart),
        tool("やり直す", "redo", types::CType::CallStart),
        separator(),
        tool("上へ", "up", types::CType::SetStepUp),
        tool("下へ", "down", types::CType::SetStepDown),
    };
    // 単体版のArea/Battleのイベント表示は、Dと同様に所有場面への操作
    // を提供する。ALLも同じコンパクトな戻る操作を維持し、他の所有元では省く。
    if (comm_.returnToScene) {
        toolbar_->items.insert(toolbar_->items.begin(),
            tool(all_mode ? "シーンビューに戻る" : "シーンを開く",
                 "return_scene", types::CType::LinkStart));
        if (!all_mode)
            toolbar_->items.insert(toolbar_->items.begin() + 1, separator());
    }
    // Dは保存設定が有効な場合だけ、構築時にこのツールを追加する。
    if (!all_mode && comm_.openDuplicate) {
        const auto offset = comm_.returnToScene ? 2U : 0U;
        toolbar_->items.insert(toolbar_->items.begin() + offset,
            tool(comm_.duplicateViewText, "duplicate_event", types::CType::LinkStart));
        toolbar_->items.insert(toolbar_->items.begin() + offset + 1, separator());
    }
    if (all_mode) {
        // ALLの操作列には場面へ戻る操作と上下移動だけを残す。取り消し・やり直し
        // はEngine/Dataの下のMainWindowの履歴欄が担当する。
        std::vector<lib::ToolItem> compact;
        const auto move = [&compact, this](const StringView command) {
            const auto found = std::find_if(toolbar_->items.begin(), toolbar_->items.end(),
                [command](const lib::ToolItem& item) { return item.command == command; });
            if (found != toolbar_->items.end()) compact.push_back(*found);
        };
        move("return_scene");
        move("up");
        move("down");
        toolbar_->items = std::move(compact);
    }
    toolbar_->on_command = [this](const StringView command) {
        if (command == "return_scene") {
            // 別のドックを開くと、現在の表示が解放される場合がある。
            const auto action = comm_.returnToScene;
            if (action) action();
        }
        else if (command == "duplicate_event") {
            const auto action = comm_.openDuplicate;
            if (action) action();
        }
        else if (command == "undo") undo();
        else if (command == "redo") redo();
        else if (command == "up") (void)up();
        else if (command == "down") (void)down();
    };
    topBarPanel_->add(toolbar_);

    treeKind_ = std::make_shared<lib::ComboBox>();
    treeKind_->editable = false;
    // スキル・アイテム・召喚獣の使用時イベントにはシ
    // ステム発火がなく、キーコードだけを設定できる。
    const bool has_system_fire = !area_ || area_->canHasFireEnter() ||
        area_->canHasFireLose() || area_->canHasFireEscape() ||
        area_->canHasFireEveryRound() || area_->canHasFireRoundEnd() ||
        area_->canHasFireRound0() ||
        dynamic_cast<::cwxeditor::cwx::area::Package*>(area_) != nullptr;
    if (has_system_fire) treeKind_->items.push_back({"システム", "system"});
    if (area_ && area_->canHasFireKeyCode())
        treeKind_->items.push_back({"キーコード", "keycode"});
    if (area_ && area_->canHasFireRound())
        treeKind_->items.push_back({"ラウンド", "round"});
    treeKind_->selected_index = 0U;
    const float tree_kind_width = all_mode ? 72.0F : 172.0F;
    treeKind_->set_layout_item({.minimum = {tree_kind_width, 24.0F},
                                .preferred = {tree_kind_width, 24.0F}});
    treeKind_->on_selection_changed = [this](const std::optional<std::size_t>) {
        refreshIgnitionChoices();
        refreshToolbarState();
    };
    topBarPanel_->add(treeKind_);

    ignition_ = std::make_shared<lib::ComboBox>();
    ignition_->editable = false;
    const float fire_width = std::min(105.0F,
                                      std::max(1.0F, prop_.etc.firesWidth));
    ignition_->set_layout_item({.minimum = {fire_width, 24.0F},
                                .preferred = {fire_width, 24.0F}});
    // 炎のツールの利用可否は、選択した発火条件に従う。
    ignition_->on_selection_changed = [this](const std::optional<std::size_t>) {
        refreshToolbarState();
    };
    ignition_->on_text_changed = [this](std::string_view) { refreshToolbarState(); };
    topBarPanel_->add(ignition_);

    // ラウンド入力は、共通数値編集経路でDの範囲付きSpinnerに合
    // わせる。先頭のマイナスを、折り返した符号なし値として解析しない。
    roundIgnition_ = std::make_shared<lib::NumberEdit>();
    roundIgnition_->value = 1.0;
    roundIgnition_->minimum = 1.0;
    roundIgnition_->maximum = 9999.0;
    roundIgnition_->step = 1.0;
    roundIgnition_->decimals = 0U;
    roundIgnition_->input_max_length = 4U;
    roundIgnition_->set_layout_item({.minimum = {fire_width, 24.0F},
                                     .preferred = {fire_width, 24.0F}});
    roundIgnition_->on_changed = [this](double) { refreshToolbarState(); };
    roundIgnition_->set_visible(false);
    topBarPanel_->add(roundIgnition_);

    keyCodeTiming_ = std::make_shared<lib::ComboBox>();
    keyCodeTiming_->editable = false;
    keyCodeTiming_->items = {
        {"使用", "use"},
        {"成功", "success"},
        {"失敗", "failure"},
        {"所持していない", "has_not"},
    };
    keyCodeTiming_->selected_index = 0U;
    keyCodeTiming_->set_layout_item({.minimum = {64.0F, 24.0F},
                                     .preferred = {64.0F, 24.0F}});
    topBarPanel_->add(keyCodeTiming_);
    refreshIgnitionChoices();

    viewToolbar_ = std::make_shared<lib::ToolBar>();
    viewToolbar_->tooltip_width_provider=[this, life=std::weak_ptr<const int>(paletteLifetime_.token)] { return life.expired() ? 0.0F : float(prop_.etc.toolTipWidth); };
    viewToolbar_->set_layout_item({.minimum = {260.0F, 26.0F},
                                   .preferred = {320.0F, 26.0F}, .grow = 1.0F});
    viewToolbar_->items = {
        tool("新規イベント", "create", types::CType::Start),
        tool("発火条件を追加", "add_fire", types::CType::BranchKeyCode),
        separator(),
        tool("編集", "edit", types::CType::TalkMessage),
        tool("選択肢を編集", "edit_selection", types::CType::BranchFlag),
        separator(),
        tool("全て展開", "expand", types::CType::BranchSelect),
        tool("全て折りたたむ", "collapse", types::CType::End),
        separator(),
        tool("詳細表示", "detail", types::CType::CustomXmlContent),
        tool("行番号", "line", types::CType::CheckStep),
        separator(),
        tool("現在のイベント", "select_current", types::CType::CallStart),
    };
    if (all_mode) {
        // ALL左下の細い操作列では、発火操作の横にある二
        // つのコンパクトな操作だけを残す。他の幅広い表示コ
        // マンドはメニュー・コンテキスト操作から使える。細
        // い列に、切れた単体版ツールバーを再導入しない。
        std::erase_if(viewToolbar_->items, [](const lib::ToolItem& item) {
            return item.command != "create" && item.command != "add_fire";
        });
        viewToolbar_->set_layout_item({.minimum = {105.0F, 24.0F},
                                       .preferred = {105.0F, 24.0F}});
    }
    viewToolbar_->on_command = [this](const StringView command) {
        if (command == "create") {
            // Dの青いNewEventツールは、三つの操作
            // 部で選択中の発火条件を使い、EventTree
            // を直ちに作る。イベントのプロパティ画面を開
            // くコンテキストメニューのコマンドとは別。
            createEventWithIgnition();
        } else if (command == "add_fire") {
            const auto fire = ignitionFromControls();
            if (!fire) return;
            // 右ペインが以前のツリーを表示中でも、Dは左の
            // EventTreeの現在の選択を必要とする。
            const auto tree = selectionEventTree();
            if (!tree || !canAddFire()) return;
            (void)addFire(*fire, tree);
        } else if (command == "edit") {
            edit();
        } else if (command == "edit_selection") {
            (void)editSelection();
        } else if (command == "expand") {
            if (contents_) for (auto& node : contents_->nodes) node.expanded = true;
            rebuildContentTable();
        } else if (command == "collapse") {
            // DはStart行だけを折りたたみ、子孫の展開状態を記憶する。合成した
            // EventTreeルート自体はStartやコンテントの行ではない。
            if (contents_) for (auto& node : contents_->nodes)
                if (const auto* info = nodeInfo(node.id); info && info->content &&
                    info->content->type() == types::CType::Start)
                    node.expanded = false;
            rebuildContentTable();
        } else if (command == "detail") {
            showEventTreeDetail_ = !showEventTreeDetail_;
            rebuildContentTable();
            const std::weak_ptr<const int> life = paletteLifetime_.token;
            detailSettingChanged.call(showEventTreeDetail_);
            if (life.expired()) return;
        } else if (command == "line") {
            showLineNumber_ = !showLineNumber_;
            rebuildContentTable();
            const std::weak_ptr<const int> life = paletteLifetime_.token;
            lineNumberSettingChanged.call(showLineNumber_);
            if (life.expired()) return;
        } else if (command == "select_current") {
            selectCurrentEvent();
        }
        refreshToolbarState();
    };
    topBarPanel_->add(viewToolbar_);

    slope_ = std::make_shared<lib::NumberEdit>();
    slope_->minimum = 0.0;
    slope_->maximum = static_cast<double>(eventTreeSlopeMaximum_);
    slope_->value = eventTreeSlope_;
    slope_->set_tooltip("イベントツリーの傾斜");
    const auto editSlope = [this](const double value) {
        if (!std::isfinite(value)) return;
        const double bounded = std::clamp(value, 0.0,
            static_cast<double>(eventTreeSlopeMaximum_));
        editEventTreeSlope(static_cast<int>(bounded));
    };
    slope_->on_changed = editSlope;
    slope_->on_preview = editSlope;
    slope_->on_committed = editSlope;
    slope_->on_edit_cancelled = [this] {
        // NumberEditSessionは、このコールバックの前に編集前の値を復元する。
        const double value = std::isfinite(slope_->value)
            ? std::clamp(slope_->value, 0.0,
                static_cast<double>(eventTreeSlopeMaximum_)) : 0.0;
        editEventTreeSlope(static_cast<int>(value));
    };
    slope_->set_layout_item({.minimum = {46.0F, 24.0F},
                             .preferred = {46.0F, 24.0F}});
    if (all_mode) slope_->set_visible(false);
    topBarPanel_->add(slope_);

    // ALLではEventView本体からこの幅広い上部帯を外す。選択中のEventView
    // は、MainWindowの操作列へコンパクトなコマンド列だけを公開する。
    if (!all_mode) panel_->add(topBarPanel_);
}

std::shared_ptr<lib::Panel> EventView::allCompactToolbar() {
    if (!lib::editor_window_host_options().all_mode || !toolbar_) return {};
    if (all_compact_toolbar_) return all_compact_toolbar_;
    all_compact_toolbar_ = std::make_shared<lib::Panel>();
    all_compact_toolbar_->accessibility_name = "ALL EventView compact toolbar";
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    layout->spacing = 2.0F;
    layout->padding = {};
    all_compact_toolbar_->set_layout(layout);
    all_compact_toolbar_->set_layout_item({
        .horizontal = lib::Align::Stretch,
        .minimum = {0.0F, 0.0F},
        .preferred = {0.0F, 0.0F}});
    // EventViewの戻る・上・下の移動列
    // を、下記でALL左下の最初の行へ置く。
    if (topBarPanel_) topBarPanel_->remove(toolbar_);

    // 単体版はツリー上の広い帯に発火・
    // 新規イベントの操作部を置く。ALL
    // の主画面にはその帯を置かず、同
    // じ有効なウィジェットを左下へ移
    // す。幅に合わせ、上から場面へ戻
    // る・上・下、システム/キーコー
    // ド/ラウンド、その内容の発火条
    // 件、キーコードのタイミング、イ
    // ベント作成・発火条件追加を縦に
    // 並べる。中央の三行は全幅を使う
    // 。同じ操作部なのでコールバック
    // と有効状態の更新は変えない。横
    // に帯を詰めるCoolBarで
    // は一行へ潰れるため使わない。
    if (topBarPanel_) {
        for (const auto& widget : std::initializer_list<lib::WidgetPtr>{
                 treeKind_, ignition_, roundIgnition_, keyCodeTiming_, viewToolbar_})
            if (widget) topBarPanel_->remove(widget);
    }
    const auto full_width = [](const lib::WidgetPtr& widget) {
        if (!widget) return;
        widget->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Center,
                                 .minimum = {0.0F, 24.0F},
                                 .preferred = {0.0F, 24.0F}});
    };
    full_width(treeKind_);
    full_width(ignition_);
    full_width(roundIgnition_);
    // ALL左下のイベントツールは、押しやすくするため縦横
    // を1.3倍（24pxから約32px）にする。場面へ戻
    // る・上・下の列と、下の作成・発火追加の行に適用する。
    constexpr float kAllEventToolScale = 1.3F;
    const float event_tool_height = std::ceil(24.0F * kAllEventToolScale);
    if (toolbar_) {
        // ALLの移動列にはすでに場面へ戻る・上・下だけがある（createTopBar
        // 参照）。上でtopBarPanel_から切り離し済み。
        toolbar_->icon_scale = kAllEventToolScale;
        toolbar_->set_layout_item({.horizontal = lib::Align::Start,
                                   .vertical = lib::Align::Center,
                                   .minimum = {0.0F, event_tool_height + 2.0F},
                                   .preferred = {0.0F, event_tool_height + 2.0F}});
        all_compact_toolbar_->add(toolbar_);
    }
    if (treeKind_) all_compact_toolbar_->add(treeKind_);
    if (ignition_) all_compact_toolbar_->add(ignition_);
    if (roundIgnition_) all_compact_toolbar_->add(roundIgnition_);
    // タイミングコンボの計測最小幅（「所持していない」と矢
    // 印）が操作列より広いため、同じ行にすると作成・発火
    // 追加ツールが幅0になる。独立した全幅の行を与える。
    if (keyCodeTiming_) {
        full_width(keyCodeTiming_);
        all_compact_toolbar_->add(keyCodeTiming_);
    }
    auto action_row = std::make_shared<lib::Panel>();
    action_row->accessibility_name = "ALL EventView ignition actions";
    auto action_layout = std::make_shared<lib::LinearLayout>(
        lib::Orientation::Horizontal);
    action_layout->spacing = 1.0F;
    action_layout->padding = {};
    action_layout->cross_axis = lib::Align::Center;
    action_row->set_layout(action_layout);
    action_row->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .minimum = {0.0F, event_tool_height + 2.0F},
                                 .preferred = {0.0F, event_tool_height + 2.0F}});
    if (viewToolbar_) {
        // 作成・発火追加の二つのアイコンの自然幅。
        viewToolbar_->icon_scale = kAllEventToolScale;
        viewToolbar_->set_layout_item({.vertical = lib::Align::Center,
                                       .minimum = {0.0F, event_tool_height},
                                       .preferred = {0.0F, event_tool_height}});
        action_row->add(viewToolbar_);
    }
    all_compact_toolbar_->add(action_row);

    // Content-placement icons are intentionally not part of this rail.
    // コンテント配置アイコンはこの操作列には置かず、図の上のEventViewパレット帯に置く。
    all_compact_toolbar_->set_visible(prop_.etc.showEventToolBar);
    return all_compact_toolbar_;
}

std::shared_ptr<void> EventView::releaseContentsPalette() {
    if (!paletteHost_) return {};
    unbindAutoHidePointerObserver();
    auto payload = std::make_shared<PaletteTransfer>();
    payload->state = contentToolState();
    if (rightPanel_) rightPanel_->remove(paletteHost_);
    payload->host = std::move(paletteHost_);
    payload->floating = std::move(floatingPalette_);
    payload->panel = std::move(palettePanel_);
    payload->trigger = std::move(paletteTrigger_);
    payload->bars = std::move(paletteBars_);
    payload->selectors = std::move(paletteGroupSelectors_);
    payload->target = std::move(paletteTarget_);
    if (payload->target) payload->target->owner = nullptr;
    return payload;
}

PanelPtr EventView::adoptContentsPalette(std::shared_ptr<void> value) {
    if (!value || !rightPanel_) return {};
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto payload = std::static_pointer_cast<PaletteTransfer>(std::move(value));
    if (!payload->host || !payload->target) return {};
    // Dのbox.ownerの移動に合わせ、同じ保持ボタン、メニューモデル、設定を引
    // き継ぐ。Dの追跡GCによる所有元参照を、弱く変更可能な対象参照へ置き換える。
    (void)releaseContentsPalette();
    if (life.expired()) return {};
    paletteHost_ = std::move(payload->host);
    floatingPalette_ = std::move(payload->floating);
    palettePanel_ = std::move(payload->panel);
    paletteTrigger_ = std::move(payload->trigger);
    paletteBars_ = std::move(payload->bars);
    paletteGroupSelectors_ = std::move(payload->selectors);
    paletteTarget_ = std::move(payload->target);
    paletteTarget_->owner = this;
    paletteTarget_->lifetime = paletteLifetime_.token;
    attachPaletteHost(rightPanel_);
    if (life.expired()) return {};
    bindAutoHidePointerObserver();
    if (life.expired()) return {};
    const auto host = paletteHost_;
    applyContentToolState(payload->state);
    return life.expired() ? PanelPtr{} : host;
}

void EventView::rebuildContentsPalette() {
    if (!rightPanel_) return;
    (void)releaseContentsPalette();
    createContentsPalette(rightPanel_);
    refreshToolbarState();
}

EventView::FloatingPalette::~FloatingPalette() {
    if (disconnectOwner) disconnectOwner();
    if (!window) return;
    window->on_bounds_changed = {};
    window->on_close_requested = {};
    window->set_visible(false);
    if (gui) gui->close(window);
}

std::shared_ptr<lib::Window> EventView::contentsPaletteWindow() const noexcept {
    return floatingPalette_ ? floatingPalette_->window : std::shared_ptr<lib::Window>{};
}

void EventView::contentsPaletteOwner(const std::shared_ptr<lib::Window>& owner) {
    if (comm_.dialogOwner.lock() == owner) return;
    comm_.dialogOwner = owner;
    if (paletteHost_ && rightPanel_) attachPaletteHost(rightPanel_);
}

lib::Point EventView::floatingPaletteOrigin() const {
    const auto owner = comm_.dialogOwner.lock();
    const auto bounds = owner ? (comm_.services.gui
        ? comm_.services.gui->window_bounds(owner) : owner->bounds()) : lib::Rect{};
    const auto frame = panel_ ? panel_->bounds() : lib::Rect{};
    return {bounds.x + frame.x, bounds.y + frame.y};
}

void EventView::saveFloatingPalettePosition() {
    if (!floatingPalette_ || !floatingPalette_->window) return;
    auto* props = comm_.editorProps ? comm_.editorProps() : nullptr;
    if (!props) return;
    const auto origin = floatingPaletteOrigin();
    const auto bounds = floatingPalette_->window->bounds();
    props->flex().contentsWin.x(static_cast<int>(std::lround(bounds.x - origin.x)));
    props->flex().contentsWin.y(static_cast<int>(std::lround(bounds.y - origin.y)));
}

lib::Rect EventView::clampFloatingPaletteBounds(lib::Rect bounds) const {
    if (comm_.services.gui) {
        const auto screens = comm_.services.gui->screens();
        if (!screens.empty()) {
            const auto screen = std::find_if(screens.begin(), screens.end(), [&bounds](const auto& value) {
                return bounds.x >= value.work_area.x && bounds.x < value.work_area.x + value.work_area.width &&
                       bounds.y >= value.work_area.y && bounds.y < value.work_area.y + value.work_area.height;
            });
            const auto display = (screen == screens.end() ? screens.front() : *screen).work_area;
            int x = static_cast<int>(bounds.x), y = static_cast<int>(bounds.y);
            dutils::intoDisplay(x, y, static_cast<int>(bounds.width), static_cast<int>(bounds.height), display);
            bounds.x = static_cast<float>(x); bounds.y = static_cast<float>(y);
        }
    }
    return bounds;
}

void EventView::openToolWindow() {
    if (readOnly_ || !selectedTree_) return;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    toolWindowOpen_ = true;
    (void)ensureContentsPalette();
    if (life.expired()) return;
    // DのEventTreeView.openToolWindowは、自動で隠れたパレットを表示する。
    if (lib::editor_window_host_options().all_mode || !prop_.etc.contentsAutoHide ||
        prop_.etc.contentsFloat || !palettePanel_) return;
    const auto panel = palettePanel_;
    const auto trigger = paletteTrigger_;
    panel->set_visible(true);
    if (life.expired()) return;
    if (trigger) trigger->set_visible(false);
}

void EventView::closeToolWindow() {
    toolWindowOpen_ = false;
    // Dが隠すのは自動非表示の帯だけ。最後のEventTree
    // がなくなっても、浮動ツールボックスは開いたままにする。
    if (!lib::editor_window_host_options().all_mode && prop_.etc.contentsAutoHide &&
        !prop_.etc.contentsFloat && palettePanel_) {
        const std::weak_ptr<const int> life = paletteLifetime_.token;
        const auto panel = palettePanel_;
        const auto trigger = paletteTrigger_;
        if (life.expired()) return;
        // Dは自動非表示のShellを隠している間も、MouseTrackフィルターを保持する。
        panel->set_visible(false);
        if (life.expired()) return;
        if (trigger) trigger->set_visible(true);
    }
}

void EventView::bindAutoHidePointerObserver() {
    if (palettePointerObserver_ || !paletteTarget_ || !paletteTrigger_ ||
        !palettePanel_ || !topBarPanel_ || !panel_ || !prop_.etc.contentsAutoHide ||
        prop_.etc.contentsFloat || lib::editor_window_host_options().all_mode)
        return;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto target = paletteTarget_;
    palettePointerObserver_ = lib::add_window_pointer_observer(
        [target, life](const lib::WidgetPtr& scope, const lib::PointerEvent& event,
                       const lib::WidgetPtr& hit) {
            if (life.expired() || !target) return;
            auto* self = target->live();
            if (!self || !self->prop_.etc.contentsAutoHide ||
                self->prop_.etc.contentsFloat ||
                lib::editor_window_host_options().all_mode ||
                !self->paletteHost_ || !self->paletteTrigger_ || !self->palettePanel_ ||
                !self->topBarPanel_ || !self->panel_) return;
            if (event.pointer_type != lib::PointerType::Mouse ||
                (event.action != lib::PointerAction::Enter &&
                 event.action != lib::PointerAction::Leave &&
                 event.action != lib::PointerAction::Up)) return;

            const auto host = self->paletteHost_;
            const auto panel = self->palettePanel_;
            const auto trigger = self->paletteTrigger_;
            const auto toolbar = self->topBarPanel_;
            const auto eventViewPanel = self->panel_;
            const bool visible = panel->visible();
            const auto in_scope = [&](const lib::WidgetPtr& root) {
                return root && hit && lib::widget_in_input_scope(root, hit);
            };
            const auto hide = [&] {
                if (life.expired()) return;
                panel->set_visible(false);
                if (life.expired()) return;
                trigger->set_visible(true);
            };
            const auto show = [&] {
                if (life.expired()) return;
                trigger->set_visible(false);
                if (life.expired()) return;
                panel->set_visible(true);
            };

            // Dは全体のフィルターを適用する前に、アクティブなEventTreeを確認する。
            if (!self->displayedEventTree()) {
                hide();
                return;
            }

            // SWTのDisplayフィルターは、自動非表示Shell
            // が隠れている間も設定済み。非表示からの再表示は、このEventView
            // の所有入力範囲内に限定する。表示中のパレットは、D
            // と同様にポインターが別ウィンドウへ入ると閉じる。
            if (!lib::widget_in_input_scope(scope, eventViewPanel)) {
                if (visible) hide();
                return;
            }

            auto toolbarBounds = toolbar->bounds();
            const bool hasToolbarBounds = toolbar->visible() && toolbarBounds.height > 0.0F;
            if (!hasToolbarBounds) {
                // Dは非表示ツールバーの高さ0の矩形を、この代替値で置
                // き換える。C++の上部バーはEventWindow
                // 全幅の領域なので、分割の片側だけの右ペインの起動領
                // 域ではなく、最初の行のホストから非表示帯を求める。
                const auto viewBounds = eventViewPanel->bounds();
                toolbarBounds = {viewBounds.x, viewBounds.y, viewBounds.width,
                    static_cast<float>(std::max(0,
                        self->prop_.etc.showContentsBoxHeightWhenNoToolBar))};
            }
            const bool inToolbar = lib::point_inside(toolbarBounds, event.position) ||
                (hasToolbarBounds && in_scope(toolbar));
            const bool inPalette = visible &&
                (in_scope(host) || lib::point_inside(host->bounds(), event.position));

            if (event.action == lib::PointerAction::Up) {
                if (visible) {
                    // C++の起動領域は、Dのサイズ0の非表示Shell
                    // に加えた局所的な操作部。その解放でパレットを表示し
                    // た直後に、同じ解放を非表示操作として消費しない。
                    if (!(hit == trigger && panel->visible())) hide();
                } else if (inToolbar) {
                    // Dはツリーのフォーカスに関係なく、ツールバーのボタン解放で開く。
                    show();
                }
                return;
            }

            if (visible) {
                // Dはツールバーとパレットの全矩形に対して
                // 判定する。同じEventWindow内で
                // も図の中央は両方の外。ネイティブのウィン
                // ドウ離脱は最後の位置を持ち、その位置が
                // 起動範囲内のままでも、ヒットなしならウ
                // ィンドウ外なので自動非表示を維持する。
                if ((event.action == lib::PointerAction::Leave && !hit) ||
                    (!inToolbar && !inPalette)) hide();
            } else if (inToolbar &&
                       (event.action == lib::PointerAction::Enter ||
                        (event.action == lib::PointerAction::Leave && hit)) &&
                       self->comm_.services.gui &&
                       self->comm_.services.gui->focused_widget() == self->eventGraphCanvas_) {
                // Dは起動範囲へMouseExit/MouseEnterのフィルターを
                // 適用し、EventTreeにフォーカスがあれば再表示する。ネイティブ
                // のウィンドウ離脱はヒット先がないため、古い最終位置で再表示しない。
                show();
            }
        });
}

void EventView::unbindAutoHidePointerObserver() noexcept {
    if (!palettePointerObserver_) return;
    lib::remove_window_pointer_observer(palettePointerObserver_);
    palettePointerObserver_ = 0U;
}

void EventView::showFloatingPalette() {
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto state = floatingPalette_;
    if (!state || !state->window || !selectedTree_) return;
    if (!state->opened) {
        // パレット構築後に配置が終わる場合がある。初回表示時に、実際
        // のEventViewの枠を基準として保存位置を復元する。
        if (auto* props = comm_.editorProps ? comm_.editorProps() : nullptr) {
            const auto origin = floatingPaletteOrigin();
            auto bounds = state->window->bounds();
            const auto& saved = props->flex().contentsWin;
            if (saved.x() != -1) bounds.x = origin.x + saved.x();
            if (saved.y() != -1) bounds.y = origin.y + saved.y();
            bounds = clampFloatingPaletteBounds(bounds);
            state->window->set_bounds(bounds);
            if (life.expired()) return;
            if (state->gui) state->gui->set_window_bounds(state->window, bounds);
            if (life.expired()) return;
        }
        state->opened = true;
    }
    state->window->set_visible(true);
    if (state->gui) state->gui->show(state->window);
}

void EventView::attachPaletteHost(const std::shared_ptr<lib::Container>& parent) {
    if (!paletteHost_ || !parent) return;
    const bool floating = prop_.etc.contentsFloat &&
                          !lib::editor_window_host_options().all_mode;
    if (!floating) {
        if (floatingPalette_ && floatingPalette_->window)
            floatingPalette_->window->remove(paletteHost_);
        floatingPalette_.reset();
        parent->insert(0U, paletteHost_);
        return;
    }
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto target = paletteTarget_;
    auto state = floatingPalette_;
    const bool created = !state;
    if (!state) {
        state = std::make_shared<FloatingPalette>();
        state->gui = comm_.services.gui;
        state->window = state->gui ? state->gui->create_window()
                                  : std::make_shared<lib::Window>();
        if (!state->window) return;
        state->window->set_visible(false);
        floatingPalette_ = state;
    }
    if (state->disconnectOwner) { state->disconnectOwner(); state->disconnectOwner = {}; }
    const auto window = state->window;
    const auto owner = comm_.dialogOwner.lock();
    window->owner = owner;
    window->resizable = true;
    window->minimizable = false;
    window->maximizable = false;
    window->activate_on_show = false;
    window->utility_window = true; // D eventtreeview.d:5459 SWT.TOOL.
    auto* props = comm_.editorProps ? comm_.editorProps() : nullptr;
    window->title = props ? props->requiredMsgs().tools.value : String{"イベントコンテント"};
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    layout->padding = {}; layout->spacing = 0.0F;
    window->set_layout(layout);
    if (created) window->add(paletteHost_);
    const std::weak_ptr<FloatingPalette> weak = state;
    window->on_close_requested = [weak] {
        if (const auto current = weak.lock()) {
            current->window->set_visible(false);
            if (current->gui) current->gui->hide(current->window);
        }
    };
    window->on_bounds_changed = [target](lib::Rect) {
        if (auto* self = target->live()) self->saveFloatingPalettePosition();
    };
    if (created) {
        const auto origin = floatingPaletteOrigin();
        const auto size = lib::preferred_ui_size(paletteHost_);
        lib::Rect bounds{origin.x, origin.y, std::max(1.0F, size.width), std::max(1.0F, size.height)};
        if (props) {
            const auto& saved = props->flex().contentsWin;
            if (saved.x() != -1) bounds.x += saved.x();
            if (saved.y() != -1) bounds.y += saved.y();
        }
        bounds = clampFloatingPaletteBounds(bounds);
        window->set_bounds(bounds);
        if (life.expired()) return;
        if (state->gui) state->gui->set_window_bounds(window, bounds);
        if (life.expired()) return;
    }
    if (owner) {
        const auto active = std::make_shared<bool>(true);
        const auto prior = owner->on_bounds_changed;
        struct OwnerMove {
            std::shared_ptr<bool> active;
            std::function<void(lib::Rect)> prior;
            std::weak_ptr<FloatingPalette> state;
            std::shared_ptr<lib::Rect> before;
            void operator()(lib::Rect next) const {
                const auto dx = next.x - before->x, dy = next.y - before->y;
                *before = next;
                if (prior) prior(next);
                if (!*active || (dx == 0.0F && dy == 0.0F)) return;
                if (const auto current = state.lock()) {
                    auto bounds = current->window->bounds();
                    bounds.x += dx; bounds.y += dy;
                    if (current->gui) current->gui->set_window_bounds(current->window, bounds);
                    else current->window->set_bounds(bounds);
                }
            }
        };
        owner->on_bounds_changed = OwnerMove{active, prior, weak, std::make_shared<lib::Rect>(owner->bounds())};
        const std::weak_ptr<lib::Window> weakOwner = owner;
        state->disconnectOwner = [weakOwner, active, prior] {
            *active = false;
            if (const auto owner = weakOwner.lock())
                if (const auto* current = owner->on_bounds_changed.target<OwnerMove>(); current && current->active == active)
                    owner->on_bounds_changed = prior;
        };
    }
    showFloatingPalette();
}

void EventView::createContentsPalette(const std::shared_ptr<lib::Container>& parent) {
    auto* self = this;
    self->paletteTarget_ = std::make_shared<PaletteTarget>();
    self->paletteTarget_->owner = this;
    self->paletteTarget_->lifetime = self->paletteLifetime_.token;
    const auto target = self->paletteTarget_;
    self->paletteHost_ = std::make_shared<lib::Panel>();
    auto hostLayout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    hostLayout->spacing = 0.0F;
    hostLayout->padding = {};
    self->paletteHost_->set_layout(hostLayout);
    // DはCoolBarを使い、各モード・グループのツールバーを実際の
    // EventView幅に応じて折り返す帯とする。四行固定で詰める
    // と、細い分割ペインでCustom XMLなど最後の帯が切れる。
    self->palettePanel_ = std::make_shared<lib::CoolBar>();
    const bool all_mode = lib::editor_window_host_options().all_mode;
    // ALLのコンテントパレット（開始・終端から演算まで）は横一
    // 列に固定する。CoolBar::lockedで移動用グリ
    // ップと帯のドラッグを両方無効にし、グリップも表示しない。
    self->palettePanel_->locked = all_mode || self->prop_.etc.contentsLock;
    self->palettePanel_->single_row = all_mode;
    // パレットのコマンドは所属するEventViewに従う。ALLは上部の
    // 埋め込み帯を維持し、単体版は同じ操作部をDの浮動ウィンドウに置ける。
    auto paletteLayout = std::make_shared<lib::LinearLayout>(
        lib::Orientation::Horizontal);
    paletteLayout->spacing = 0.0F;
    paletteLayout->padding = {};
    self->palettePanel_->set_layout(paletteLayout);
    // Dの既定ContentsToolBoxは通常幅で四行を維
    // 持する。操作・終端・表示、標準・データ、ユーティリティ・
    // 分岐、取得・喪失・コモンの順。幅だけで詰めると標準が一行
    // 目へ入り、残りが一行分のネイティブ表示領域の下に隠れる。
    self->palettePanel_->wrap_indices.clear();
    if (!all_mode) {
        for (const auto index : self->prop_.etc.contentsWrapIndices)
            if (index > 0) self->palettePanel_->wrap_indices.push_back(
                static_cast<std::size_t>(index));
    }
    // CoolBarの高さは現在幅でできた帯に合わ
    // せる。最悪時の固定高では広い表示に空行が残り
    // 、四行固定では細いペインの追加折り返しが切れ
    // る。LinearLayoutが最終幅で計測す
    // るため、高さを固定せず、その配置結果を使う。
    self->palettePanel_->set_layout_item({.row = 0U, .column = 0U,
                                    .horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Start,
                                    .minimum = {all_mode ? 105.0F : 180.0F, 0.0F},
                                    .preferred = {0.0F, 0.0F}});

    std::vector<std::pair<std::weak_ptr<lib::Widget>, std::int32_t>> paletteBandIds;
    std::vector<types::CTypeGroup> orderedGroups;
    const auto& canonicalGroups = event::CTYPE_GROUPS_LIST();
    orderedGroups.reserve(canonicalGroups.size());
    for (const auto raw : self->prop_.etc.contentsOrder) {
        const auto groupIndex = raw - 2;
        if (groupIndex < 0 || static_cast<std::size_t>(groupIndex) >= canonicalGroups.size())
            continue;
        const auto group = canonicalGroups[static_cast<std::size_t>(groupIndex)];
        if (std::find(orderedGroups.begin(), orderedGroups.end(), group) == orderedGroups.end())
            orderedGroups.push_back(group);
    }
    for (const auto group : canonicalGroups)
        if (std::find(orderedGroups.begin(), orderedGroups.end(), group) == orderedGroups.end())
            orderedGroups.push_back(group);

    if (!all_mode) {
            // Dは選択矢印を独立したCoolBar帯（順序0
            // ）に置き、配置モードの帯（順序1）を続ける。
            auto arrow = std::make_shared<lib::ToolBar>();
    arrow->tooltip_width_provider=[this, life=std::weak_ptr<const int>(paletteLifetime_.token)] { return life.expired() ? 0.0F : float(prop_.etc.toolTipWidth); };
            arrow->items.push_back(
                {"選択", "arrow", self->viewImage("arrow", types::CType::Start), true, true});
            arrow->set_layout_item({.minimum = {all_mode ? 105.0F : 0.0F, 24.0F},
                                    .preferred = {all_mode ? 105.0F : 0.0F, 24.0F}});
            arrow->on_command = [target](const StringView command) {
                auto* self = target->live();
                if (!self) return;
                if (command != "arrow") return;
                // Dのarrow()は配置モードを保ったまま矢印を選ぶ。
                self->contentToolArrow_ = true;
                self->publishContentToolState();
                if (target->live() != self) return;
                self->refreshToolbarState();
            };
            arrow->on_pointer = [target](lib::PointerEvent& event) {
                auto* self = target->live();
                if (!self) return;
                if (event.button != lib::PointerButton::Secondary ||
                    event.action != lib::PointerAction::Up)
                    return;
                // DのTMListenerは、副クリックをarrow()へ渡してモードを保持する。
                self->contentToolArrow_ = true;
                self->publishContentToolState();
                if (target->live() != self) return;
                self->refreshToolbarState();
                event.handled = true;
            };
            arrow->single_row = true; // DはSWT.WRAPなしのSWT.FLATツールバーを作る。
            self->paletteBars_.push_back(arrow);
            self->palettePanel_->add(arrow);
            paletteBandIds.emplace_back(arrow, 0);

            auto mode = std::make_shared<lib::ToolBar>();
    mode->tooltip_width_provider=[this, life=std::weak_ptr<const int>(paletteLifetime_.token)] { return life.expired() ? 0.0F : float(prop_.etc.toolTipWidth); };

            auto putModeMenu = std::make_shared<lib::Menu>(true);
            const auto rebuildPutModeMenu = [target, weak = std::weak_ptr<lib::Menu>(putModeMenu)] {
                auto* self = target->live();
                if (!self) return;
                const auto menu = weak.lock();
                if (!menu) return;
                std::vector<lib::MenuItem> items;
                const auto append = [target, &items](String text, String command,
                                                   const PutMode value) {
                auto* self = target->live();
                if (!self) return;
                    lib::MenuItem item;
                    item.text = std::move(text);
                    item.command = std::move(command);
                    item.radio = true;
                    item.checked = self->contentPutMode_ == value;
                    item.action = [target, value] {
                auto* self = target->live();
                if (!self) return;
                        const auto previousMode = self->contentPutMode_;
                        self->contentPutMode_ = value;
                        // DのupdatePutMode()はQuickへ入る・出る際に矢印を選ぶ。Select
                        // とContinueの間では、選択中のコンテントツールを保持する。
                        if (value == PutMode::Quick || previousMode == PutMode::Quick)
                            self->contentToolArrow_ = true;
                        self->publishContentToolState();
                if (target->live() != self) return;
                        self->refreshToolbarState();
                    };
                    items.push_back(std::move(item));
                };
                append("即時配置", "put-quick", PutMode::Quick);
                append("選択して配置", "put-select", PutMode::Select);
                append("連続配置", "put-continue", PutMode::Continue);
                (void)lib::replace_menu_items(menu, std::move(items));
            };
            putModeMenu->on_open = rebuildPutModeMenu;
            rebuildPutModeMenu();
            lib::ToolItem putMode;
            putMode.text = "配置方法";
            putMode.command = "put_mode";
            putMode.icon = self->viewImage(self->contentPutMode_ == PutMode::Quick ? "quick" :
                                     self->contentPutMode_ == PutMode::Continue ? "continue" :
                                                                           "select",
                                     types::CType::CallStart);
            putMode.drop_down = true;
            putMode.drop_down_menu = std::move(putModeMenu);
            putMode.tooltip = "コンテントの配置方法";
            mode->items.push_back(std::move(putMode));
            mode->items.push_back(
                {"配置後に編集", "auto_edit",
                 self->viewImage("auto_edit", types::CType::TalkMessage)});
            mode->items.push_back(
                {"先頭に配置", "insert_first",
                 self->viewImage("insert_first", types::CType::SetStepUp)});

            auto templateMenu = std::make_shared<lib::Menu>(true);
            const auto rebuildTemplateMenu = [target, weak = std::weak_ptr<lib::Menu>(templateMenu)] {
                auto* self = target->live();
                if (!self) return;
                const auto menu = weak.lock();
                if (!menu) return;
                std::vector<lib::MenuItem> items;
                const auto append = [target, &items](
                                        const lib::Array<structs::EvTemplate>& values) {
                auto* self = target->live();
                if (!self) return;
                    for (const auto& value : values) {
                        lib::MenuItem item;
                        item.text = templateMenuText(value);
                        item.icon = self->templateContentImage(value.script);
                        item.shortcut = value.hotkey;
                        item.enabled = !self->readOnly_ &&
                                       static_cast<bool>(self->comm_.compileEventTemplate);
                        item.action = [target, script = value.script] {
                auto* self = target->live();
                if (!self) return;
                            (void)self->pasteScript(script, false);
                        };
                        item.action_with_gesture = [target, script = value.script](
                                                       const lib::CommandGesture gesture) {
                auto* self = target->live();
                if (!self) return;
                            // DのテンプレートメニューはShiftで実行
                            // した場合だけtryInsertを使う。通
                            // 常操作は上記の従来の代替処理を維持する。
                            (void)self->pasteScript(script, hasShift(gesture));
                        };
                        items.push_back(std::move(item));
                    }
                };
                const auto scenario = self->comm_.scenarioEventTemplates
                                          ? self->comm_.scenarioEventTemplates()
                                          : lib::Array<structs::EvTemplate>{};
                if (target->live() != self) return;
                const auto global = self->comm_.globalEventTemplates
                                        ? self->comm_.globalEventTemplates()
                                        : lib::Array<structs::EvTemplate>{};
                if (target->live() != self) return;
                append(scenario);
                if (!scenario.empty() && !global.empty()) {
                    lib::MenuItem separator;
                    separator.separator = true;
                    items.push_back(std::move(separator));
                }
                append(global);
                if (self->comm_.editScenarioEventTemplates) {
                    if (!items.empty()) {
                        lib::MenuItem separator;
                        separator.separator = true;
                        items.push_back(std::move(separator));
                    }
                    lib::MenuItem edit;
                    edit.text = self->comm_.editScenarioEventTemplatesText;
                    edit.action = self->comm_.editScenarioEventTemplates;
                    items.push_back(std::move(edit));
                }
                (void)lib::replace_menu_items(menu, std::move(items));
            };
            templateMenu->on_open = rebuildTemplateMenu;
            rebuildTemplateMenu();
            lib::ToolItem templates;
            templates.text = self->comm_.eventTemplatesText;
            templates.command = "templates";
            templates.icon = self->viewImage("templates", types::CType::CustomXmlContent);
            templates.drop_down = true;
            templates.drop_down_menu = std::move(templateMenu);
            templates.tooltip = self->comm_.eventTemplatesText;
            mode->items.push_back(std::move(templates));
            // SWTのToolBarに対応するこの帯は一行に
            // 保つが、幅は固定しない。自然な計測で分割ドロッ
            // プダウン、UI倍率、アイコンなしスキンの代替文
            // 字を考慮する。132px固定では最後の操作部が
            // 帯内で折り返され、後続のパレット行が切れる。
            mode->set_layout_item({.minimum = {all_mode ? 105.0F : 0.0F, 24.0F},
                                   .preferred = {all_mode ? 105.0F : 0.0F, 24.0F}});
            mode->on_command = [target](const StringView command) {
                auto* self = target->live();
                if (!self) return;
                if (command == "auto_edit") {
                    self->contentAutoEdit_ = !self->contentAutoEdit_;
                } else if (command == "insert_first") {
                    self->contentInsertFirst_ = !self->contentInsertFirst_;
                }
                self->publishContentToolState();
                if (target->live() != self) return;
                self->refreshToolbarState();
            };
            mode->on_pointer = [target](lib::PointerEvent& event) {
                auto* self = target->live();
                if (!self) return;
                if (event.button != lib::PointerButton::Secondary ||
                    event.action != lib::PointerAction::Up)
                    return;
                // Dのコンテントツールボックスはボタン3で、
                // 現在の配置モードを保ったまま矢印を選ぶ。
                self->contentToolArrow_ = true;
                self->publishContentToolState();
                if (target->live() != self) return;
                self->refreshToolbarState();
                event.handled = true;
            };
            mode->single_row = true; // DはSWT.WRAPなしのSWT.FLATツールバーを作る。
            self->paletteBars_.push_back(mode);
            self->palettePanel_->add(mode);
            paletteBandIds.emplace_back(mode, 1);
    }

    for (const auto group : orderedGroups) {
            const auto found = event::CTYPE_GROUP().find(group);
            if (found == event::CTYPE_GROUP().end()) continue;

            if (all_mode) {
                // ALLは幅広いアイコン列の代わりに、Dのコンテント群ごとに
                // コンパクトな選択部を置く。閉じているときは群の名前を残し
                // 、ポップアップの各行には実際のアイコンと名前を表示する。
                auto selector = std::make_shared<lib::ComboBox>();
                selector->editable = false;
                selector->edit_text = groupLabel(group);
                selector->visible_item_count = 20U;
                for (const auto type : found->second) {
                    lib::ChoiceItem choice;
                    choice.text = self->contentName(type);
                    choice.value = contentCommand(type);
                    choice.icon = self->contentImage(type);
                    choice.enabled = !self->readOnly_ && self->selectedTree_ != nullptr;
                    choice.tooltip = self->contentDescription(type);
                    if (choice.tooltip.empty()) choice.tooltip = choice.text;
                    selector->items.push_back(std::move(choice));
                }
                // ALLでは文字と▼の間を空けない。幅はラベルの左余白5px
                // 、文字幅、コンボの矢印セル25pxの合計とする。
                const float selector_width = all_mode
                    ? lib::ui_text_width(selector->edit_text,
                                         lib::scaled_ui_metric(31.0F))
                    : std::max(72.0F, lib::ui_text_width(
                        selector->edit_text, lib::scaled_ui_metric(37.0F)));
                selector->set_layout_item({.minimum = {selector_width, 24.0F},
                                           .preferred = {selector_width, 24.0F}});
                selector->on_selection_changed = [target, weak =
                    std::weak_ptr<lib::ComboBox>(selector)](
                    const std::optional<std::size_t> index) {
                auto* self = target->live();
                if (!self) return;
                    const auto control = weak.lock();
                    if (!control || !index || *index >= control->items.size()) return;
                    const auto command = control->items[*index].value;
                    control->selected_index.reset();
                    if (const auto type = contentCommandType(command)) {
                        self->selectContentTool(*type);
                if (target->live() != self) return;
                        if (self->contentPutMode_ == PutMode::Quick)
                            (void)self->placeSelectedContentTool(false);
                    }
                };
                selector->on_pointer = [target](lib::PointerEvent& event) {
                auto* self = target->live();
                if (!self) return;
                    if (event.button != lib::PointerButton::Secondary ||
                        event.action != lib::PointerAction::Up) return;
                    // Dのarrow()は配置モードを保ったまま矢印を選ぶ。
                    self->contentToolArrow_ = true;
                    self->publishContentToolState();
                if (target->live() != self) return;
                    self->refreshToolbarState();
                    event.handled = true;
                };
                self->paletteGroupSelectors_.push_back(selector);
                self->palettePanel_->add(selector);
                continue;
            }

            // SWTは群の見出しを、フラットToolBarの前のLabel
            // として置く。文字ToolItemとして扱うと、ネ
            // イティブの自動サイズ調整で想定23pxを超え、後
            // 続のCTypeアイコンが重なったり切れたりする。
            auto groupPanel = std::make_shared<lib::Panel>();
            auto groupLayout =
                std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
            groupLayout->spacing = 1.0F;
            groupLayout->padding = {};
            groupPanel->set_layout(groupLayout);

            float captionWidth{};
            if (self->prop_.etc.showContentsGroupName) {
                const auto captionText = self->comm_.modelProps
                    ? self->comm_.modelProps->requiredMsgs().menuText(menu::cTypeGroupToMenuID(group))
                    : groupLabel(group);
                // std::string::size()はUTF-8のバイト数な
                // ので、日本語の見出しがSWTの計測幅の約三倍になってしまう。
                captionWidth = std::max(28.0F,
                    lib::ui_text_width(captionText, 5.0F));
                auto caption = std::make_shared<lib::Label>(captionText);
                caption->alignment = lib::TextAlignment::Center;
                caption->set_layout_item({.minimum = {captionWidth, 23.0F},
                                          .preferred = {captionWidth, 23.0F}});
                groupPanel->add(caption);
            }

            auto bar = std::make_shared<lib::ToolBar>();
            bar->tooltip_width_provider=[self, life=std::weak_ptr<const int>(self->paletteLifetime_.token)] { return life.expired() ? 0.0F : float(self->prop_.etc.toolTipWidth); };
            for (const auto type : found->second) {
                lib::ToolItem item;
                item.command = contentCommand(type);
                item.icon = self->contentImage(type);
                item.tooltip = self->contentName(type);
                if (self->prop_.etc.showEventContentDescription) {
                    if (const auto description = self->contentDescription(type);
                        !description.empty())
                        item.tooltip += "\n" + description;
                }
                // スキンがアイコンを持たない場合は、小さい見える
                // 代替表示を保つ。コンテント名は固定幅アイコン列
                // ではなく、ツールチップとコマンドモデルに置く。
                if (item.icon == 0) item.text = "□";
                item.checked = !self->contentToolArrow_ && type == self->contentToolType_;
                bar->items.push_back(std::move(item));
            }
            // DはCoolBarの帯全体を折り返し、個々
            // のコンテント群内のアイコンは折り返さない。
            // 幅はツールバーの自然な計測に任せ、最後のア
            // イコンや代替表示が見えない二行目へ落ちない
            // ようにする。帯高も実際の操作部から求め、24px
            // は推奨値の上限ではなく最小値とする。
            bar->set_layout_item({.minimum = {0.0F, 24.0F},
                                  .preferred = {0.0F, 0.0F}});
            bar->on_command = [target](const StringView command) {
                auto* self = target->live();
                if (!self) return;
                if (const auto type = contentCommandType(command)) {
                    if (!self->contentTypeAllowed(*type)) return;
                        self->selectContentTool(*type);
                    if (target->live() != self) return;
                    if (self->contentPutMode_ == PutMode::Quick) {
                        (void)self->placeSelectedContentTool(false);
                        if (target->live() != self || self->contentToolArrow_) return;
                        // DのputQuickは、createが選択した挿
                        // 入先を拒否しても、一回限りのツールを解除する。
                        self->contentToolArrow_ = true;
                        self->publishContentToolState();
                        if (target->live() != self) return;
                        self->refreshToolbarState();
                    }
                }
            };
            bar->on_command_with_gesture = [target](const StringView command,
                                                  const lib::CommandGesture gesture) {
                auto* self = target->live();
                if (!self) return;
                const auto type = contentCommandType(command);
                if (!type || !self->contentTypeAllowed(*type)) return;

                const bool middle = gesture.button == lib::PointerButton::Middle;
                // DのmiddleClickはputQuick(true)
                // へ委ねる。Quick以外では、Select/Continue
                // のツールを選択中のまま残さず、配置ツールを取り消す。
                if (middle && self->contentPutMode_ != PutMode::Quick) {
                    self->contentToolArrow_ = true;
                    // DのputQuick(true)は、モードを変えずにツールを取り消す。
                    self->publishContentToolState();
                if (target->live() != self) return;
                    self->refreshToolbarState();
                    return;
                }

                self->selectContentTool(*type);
                if (target->live() != self) return;
                if (self->contentPutMode_ == PutMode::Quick) {
                    // Dのツールボックスは、今回の実行だけにControl/Alt
                    // を読む。どちらの修飾キーも、保存済みのツール選択を変えない。
                    (void)self->placeSelectedContentToolWithOptions(
                        hasShift(gesture) || middle,
                        self->contentInsertFirst_ != hasControl(gesture),
                        self->contentAutoEdit_ != hasAlt(gesture));
                    if (target->live() != self || self->contentToolArrow_) return;
                    // StartとShiftなどで拒否しても、Quickの一回の実
                    // 行は完了する。Quickモードは維持し、ツールを解除する。
                    self->contentToolArrow_ = true;
                    self->publishContentToolState();
                    if (target->live() != self) return;
                    self->refreshToolbarState();
                }
            };
            const std::weak_ptr<lib::ToolBar> weak_bar = bar;
            bar->on_pointer = [target, weak_bar](lib::PointerEvent& event) {
                auto* self = target->live();
                if (!self) return;
                if (event.button != lib::PointerButton::Secondary ||
                    event.action != lib::PointerAction::Up)
                    return;
                // Dは各コンテントToolItemにTMListenerを付
                // ける。共通UIは包含するToolBarでイベントを報告す
                // るため、同じヒット境界を維持し、未使用の空白は無視する。
                const auto toolbar = weak_bar.lock();
                if (!toolbar) return;
                const auto item_bounds = lib::tool_bar_item_geometry(*toolbar);
                bool hit_content_item = false;
                for (std::size_t index = 0;
                     index < toolbar->items.size() && index < item_bounds.size();
                     ++index) {
                    const auto& item = toolbar->items[index];
                    if (!item.enabled || item.separator ||
                        !contentCommandType(item.command))
                        continue;
                    if (lib::point_inside(item_bounds[index].bounds,
                                          event.position)) {
                        hit_content_item = true;
                        break;
                    }
                }
                if (!hit_content_item) return;
                // EventTreeView.TMListener.mouseUpに合
                // わせ、Dの配置モードを変えずに選択中のコンテントツールを取り消す。
                self->contentToolArrow_ = true;
                self->publishContentToolState();
                if (target->live() != self) return;
                self->refreshToolbarState();
                event.handled = true;
            };
            bar->single_row = true; // DはCoolBarの帯を折り返し、群内のアイコンは折り返さない。
            self->paletteBars_.push_back(bar);
            groupPanel->add(bar);
            // 群の見出しとToolBarから、Dと同じ自然な帯高を決める。
            groupPanel->set_layout_item({
                .minimum = {0.0F, 24.0F},
                .preferred = {0.0F, 0.0F}});
            self->palettePanel_->add(groupPanel);
            const auto canonical = std::find(canonicalGroups.begin(), canonicalGroups.end(), group);
            paletteBandIds.emplace_back(groupPanel, static_cast<std::int32_t>(
                std::distance(canonicalGroups.begin(), canonical)) + 2);
    }
    if (!all_mode) {
        // Dは矢印・モード帯も含む全帯の同一性を復元し、
        // ツールボックス破棄時に実際の順を記録する。共通
        // CoolBarは完了した各移動を直接報告する。
        std::vector<std::int32_t> order;
        for (const auto id : self->prop_.etc.contentsOrder) {
            const auto known = std::find_if(paletteBandIds.begin(), paletteBandIds.end(),
                [id](const auto& item) { return item.second == id; });
            if (known != paletteBandIds.end() &&
                std::find(order.begin(), order.end(), id) == order.end()) order.push_back(id);
        }
        for (const auto& item : paletteBandIds)
            if (std::find(order.begin(), order.end(), item.second) == order.end())
                order.push_back(item.second);
        for (std::size_t to = 0; to < order.size(); ++to) {
            const auto identity = std::find_if(paletteBandIds.begin(), paletteBandIds.end(),
                [id = order[to]](const auto& item) { return item.second == id; });
            const auto widget = identity->first.lock();
            const auto& children = self->palettePanel_->children();
            const auto found = std::find(children.begin(), children.end(), widget);
            if (found != children.end())
                self->palettePanel_->move_child(static_cast<std::size_t>(
                    std::distance(children.begin(), found)), to);
        }
        self->prop_.etc.contentsOrder = order;
        self->palettePanel_->on_item_moved = [target, paletteBandIds](std::size_t, std::size_t) {
            auto* self = target->live();
            if (!self || !self->palettePanel_) return;
            std::vector<std::int32_t> current;
            for (const auto& child : self->palettePanel_->children()) {
                const auto found = std::find_if(paletteBandIds.begin(), paletteBandIds.end(),
                    [&child](const auto& item) { return item.first.lock() == child; });
                if (found != paletteBandIds.end()) current.push_back(found->second);
            }
            self->prop_.etc.contentsOrder = current;
            self->contentsOrderChanged.call(current);
        };
    }
    if (self->prop_.etc.contentsAutoHide &&
        !(self->prop_.etc.contentsFloat && !all_mode)) {
        self->paletteTrigger_ = std::make_shared<lib::Panel>();
        const auto triggerHeight = static_cast<float>(std::max(
            1, self->prop_.etc.showContentsBoxHeightWhenNoToolBar));
        self->paletteTrigger_->set_layout_item({.horizontal = lib::Align::Stretch,
                                          .minimum = {180.0F, triggerHeight},
                                          .preferred = {0.0F, triggerHeight}});
        self->paletteTrigger_->set_tooltip("コンテントパレットを表示");
        self->palettePanel_->set_visible(false);
        // C++の意図的な追加として、可視の局所的な起動領域を
        // 残す。D互換の表示全体のMouseTrackに加え
        // 、その上のホバーや解放でもパレットを再表示できる。
        const auto reveal = [target](lib::PointerEvent& event) {
            auto* self = target->live();
            if (!self) return;
            if (event.action != lib::PointerAction::Move &&
                event.action != lib::PointerAction::Up) return;
            // Dの自動非表示ツールは、所有するイベントツリーがなければ再表示できない。
            if (!lib::editor_window_host_options().all_mode) {
                if (!self->displayedEventTree()) return;
                if (event.action == lib::PointerAction::Move &&
                    (!self->comm_.services.gui ||
                     self->comm_.services.gui->focused_widget() != self->eventGraphCanvas_))
                    return;
            }
            self->paletteTrigger_->set_visible(false);
            self->palettePanel_->set_visible(true);
        };
        self->paletteTrigger_->on_pointer = reveal;
        const std::weak_ptr<const int> life = self->paletteLifetime_.token;
        self->palettePanel_->on_blur = [target, life] {
            auto* self = target->live();
            if (life.expired() || !self) return;
            if (!self->prop_.etc.contentsAutoHide || !self->paletteTrigger_) return;
            const auto panel = self->palettePanel_;
            const auto trigger = self->paletteTrigger_;
            if (life.expired()) return;
            panel->set_visible(false);
            if (life.expired()) return;
            trigger->set_visible(true);
        };
        self->paletteHost_->add(self->paletteTrigger_);
    }
    self->paletteHost_->add(self->palettePanel_);
    // 単体版はDの浮動設定に従い、ALLは埋め込みホストを維持する。
    self->attachPaletteHost(parent);
    // Dは自動非表示Shellの寿命中、非表示の間も含め、Display
    // のMouseTrackフィルターを接続したままにする。
    self->bindAutoHidePointerObserver();
}

void EventView::ensureWidgets(const Parent& parent) {
    if (panel_) return;
    panel_ = std::make_shared<lib::Panel>();
    panel_->set_id(1000);
    auto panelLayout =
        std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    // Editorの描画面は意図的に端まで使う。Dの残余の隙間を持ち込
    // むと、入れ子のEventViewごとに余白が付いて見えるため。
    panelLayout->spacing = 0.0F;
    panelLayout->padding = {};
    panel_->set_layout(panelLayout);
    lib::LayoutItem panelItem;
    panelItem.minimum = {240.0F, 180.0F};
    panelItem.grow = 1.0F;
    panelItem.horizontal = lib::Align::Stretch;
    panelItem.vertical = lib::Align::Stretch;
    panel_->set_layout_item(panelItem);
    if (parent) parent->add(panel_);

    createTopBar();

    cards_ = std::make_shared<lib::TreeView>();
    cards_->set_id(1003);
    // 構造上の所有元だけを常時展開する。イベント行はDのEventView
    // で到着・キーコード・ラウンドなどの発火条件を子
    // に持つため、ネイティブの＋/－操作を維持する。
    cards_->show_expanders = true;
    cards_->flush_root_left = true;
    cards_->accessibility_name = "イベントと発火条件";
    cards_->inline_edit_admission = [this](const lib::TreeNode& node) {
        const auto* info = nodeInfo(node.id);
        return info && info->kind != NodeInfo::Kind::Owner;
    };
    cards_->on_selection_changed = [this](const std::vector<TreeItemId>& values) {
        if (!values.empty()) select(values.front());
    };
    // Dの左のイベント・発火ツリーには、既定選択による編集部がな
    // い。ダブルクリックはネイティブの展開操作に使い、編集はツ
    // ールバー・コンテキストメニューの明示操作にする。右のコン
    // テントツリーは下記の独自のダブルクリック編集を維持する。
    cards_->on_activate = {};
    cards_->on_item_edited = [this](const TreeItemId value, String text) {
        // 発火行も親ツリーを持つ。Dはデータ種別で処理を分けるため、editEnd
        // を通し、キーコードの確定でツリー名を変えないようにする。
        (void)editEnd(value, std::move(text));
    };
    cards_->on_expansion_changed = [this](const TreeItemId value, const bool expanded) {
        if (value != rootId_ || expanded || !cards_) return;
        const auto found = std::find_if(cards_->nodes.begin(), cards_->nodes.end(),
                                        [value](const auto& node) {
                                            return node.id == value;
                                        });
        if (found == cards_->nodes.end()) return;
        found->expanded = true;
        if (comm_.services.gui) comm_.services.gui->request_redraw(cards_);
    };
    const std::weak_ptr<const int> previewLife = paletteLifetime_.token;
    cards_->on_scroll_changed = [this, previewLife] {
        if (!previewLife.expired()) closeCardPreview();
    };
    cards_->on_pointer = [this, previewLife](lib::PointerEvent& event) {
        if (previewLife.expired()) return;
        if (event.action == lib::PointerAction::Enter) {
            // DのPreviewTrigger.mouseEnterは、後のMove
            // がポインター下のカードを公開する前に、選択コンテントの状態表示を戻す。
            const auto propsProvider = comm_.editorProps;
            auto* props = propsProvider ? propsProvider() : nullptr;
            if (previewLife.expired()) return;
            if (props && props->var().showCardStatusUnderPointer.value) {
                refreshStatusLine();
                if (!previewLife.expired()) statusChanged.call();
            }
            return;
        }
        if (event.action == lib::PointerAction::Leave ||
            event.action == lib::PointerAction::Cancel) {
            closeCardPreview();
            if (!previewLife.expired()) refreshHoverStatus(event);
            return;
        }
        if (event.action == lib::PointerAction::Move) {
            triggerCardPreview(event);
            // プレビューの構築・終了処理が、この表示を同期的に終了させる場合がある。
            if (!previewLife.expired()) refreshHoverStatus(event);
            return;
        }
        if (event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Down)
            return;
        // Win32のTreeViewは副ボタンを保持
        // ・キャプチャし、対応する解放コールバックを共
        // 通ウィジェットへ送らない場合がある。Dのメニ
        // ューは項目単位なので、押した項目を解決した時
        // 点で開く。item_indexはモデルのTreeItemId
        // を表す。メニュー構築前にポインター下の
        // 項目を正とし、右のコンテント選択や以前
        // のイベントへコマンドを誤適用しない。
        if (event.item_hit_test_available && event.item_index >= 0) {
            const auto hit = static_cast<TreeItemId>(event.item_index);
            if (nodeInfo(hit)) (void)select(hit);
        }
        showEventContextMenu(cards_, event.position);
        event.handled = true;
    };

    // DのEventTreeViewは階層を持つが、表に似た列
    // を描く。パス・検索・クリップボードの互換性のため階層モデ
    // ルを維持し、下記のTableViewへ表示用に投影する。
    contents_ = std::make_shared<lib::TreeView>();
    contents_->set_id(1004);
    contents_->on_selection_changed = [this](const std::vector<TreeItemId>& values) {
        if (!values.empty()) selectContent(values.front());
    };
    contents_->on_activate = [this](const TreeItemId) { edit(); };

    body_ = std::make_shared<lib::SplitView>();
    body_->orientation = lib::Orientation::Horizontal;
    body_->weights = {22.0F, 78.0F};
    // ALLの仕様差として、パッケージのイベント表示は
    // 左側を100px狭く開く。一度限りの初期ヒントで
    // 、分割部のドラッグで解除する。エリア・バトルも同
    // じ比率と位置補正で、パッケージと同じ左幅にする。
    if (lib::editor_window_host_options().all_mode &&
        (dynamic_cast<::cwxeditor::cwx::area::Package*>(area_) ||
         dynamic_cast<::cwxeditor::cwx::area::Area*>(area_) ||
         dynamic_cast<::cwxeditor::cwx::area::Battle*>(area_)))
        body_->initial_first_extent_offset = -100.0F;
    body_->set_layout_item({.row = 1U, .column = 0U,
                            .horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .minimum = {360.0F, 180.0F}, .grow = 1.0F});
    cards_->set_layout_item({.minimum = {150.0F, 80.0F}, .grow = 1.0F});

    // DのEventViewは、所有元にシナリオ内のローカル変数
    // があれば左側を上下に分割する。イベント・発火ツリーだけを組
    // み込むと、Package/Skill/Item/Beast
    // の左下領域とローカル変数編集が消えるため、両方を用意する。
    if (auto* localOwner = dynamic_cast<flagmodel::LocalVariableOwner*>(area_)) {
        localVariableRoot_ = localOwner->flagDirRoot();
        localVariableUseCounter_ = localOwner->useCounter();
    }
    if (localVariableRoot_) {
        leftSplit_ = std::make_shared<lib::SplitView>();
        leftSplit_->orientation = lib::Orientation::Vertical;
        leftSplit_->weights = {
            static_cast<float>(std::max(0, prop_.etc.localVariablesSashL)),
            static_cast<float>(std::max(0, prop_.etc.localVariablesSashR))};
        leftSplit_->on_weights_changed = [this](const std::vector<float>& weights) {
            if (weights.size() < 2U || !prop_.etc.storeLocalVariablesSash) return;
            prop_.etc.storeLocalVariablesSash(
                static_cast<std::int32_t>(std::lround(weights[0])),
                static_cast<std::int32_t>(std::lround(weights[1])));
        };
        leftSplit_->set_layout_item({.minimum = {150.0F, 140.0F}, .grow = 1.0F});
        leftSplit_->add(cards_);

        localVariablesPanel_ = std::make_shared<lib::Panel>();
        auto localLayout = std::make_shared<lib::GridLayout>();
        localLayout->columns = 1U;
        localLayout->spacing = 1.0F;
        localLayout->padding = {};
        localVariablesPanel_->set_layout(localLayout);
        localVariablesPanel_->set_layout_item({.minimum = {150.0F, 92.0F}, .grow = 1.0F});

        auto localTitle = std::make_shared<lib::Label>("ローカル変数");
        localTitle->set_layout_item({.row = 0U, .column = 0U,
                                     .horizontal = lib::Align::Stretch,
                                     .minimum = {100.0F, 20.0F},
                                     .preferred = {0.0F, 20.0F}});
        localVariablesPanel_->add(localTitle);

        localVariables_ = std::make_unique<variables::FlagTable>(
            comm_, prop_, *undo_, true, readOnly_);
        localVariables_->useCounter(localVariableUseCounter_);
        auto localControl = localVariables_->createControl(
            localVariablesPanel_, localVariablesPanel_);
        // ローカル使用数、翻訳済み見出し、保存されたローカル列幅
        // も含め、FlagTableのD互換の四列を維持する。
        if (localControl) {
            localControl->set_layout_item({.row = 1U, .column = 0U,
                                           .horizontal = lib::Align::Stretch,
                                           .vertical = lib::Align::Stretch,
                                           .minimum = {140.0F, 70.0F}, .grow = 1.0F});
        }
        localVariables_->setDir(localVariableRoot_, true);
        // Dはローカル変数の挿入・削除で所有カードの印を更新する。組み込
        // みEventWindowの正確な所有元への橋渡しを再利用する。
        if (!lib::editor_window_host_options().all_mode) {
            const std::weak_ptr<const int> life = paletteLifetime_.token;
            const auto refreshLocalOwner = [this, life] {
                if (!life.expired()) modelChanged.call();
            };
            localVariables_->createEvent.add(refreshLocalOwner);
            localVariables_->deleteEvent.add(refreshLocalOwner);
        }
        leftSplit_->add(localVariablesPanel_);
        body_->add(leftSplit_);
    } else {
        body_->add(cards_);
    }

    rightPanel_ = std::make_shared<lib::Panel>();
    // パレットは自然高の最上行とし、イベント本体だけを伸
    // ばす。グリッドの余った行高をパレット上へ配らない。
    auto rightLayout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    rightLayout->spacing = 0.0F;
    rightLayout->padding = {};
    rightPanel_->set_layout(rightLayout);
    // 図は自然高のパレット行の下にあるスクロール文書
    // 。EventViewが低い場合も、内容の最小サ
    // イズがパレットの計測高を奪わないようにする。
    rightPanel_->set_layout_item({.minimum = {190.0F, 140.0F}, .grow = 1.0F,
                                  .constrain_minimum_to_preferred = true});
    createContentsPalette(rightPanel_);

    contentTable_ = std::make_shared<lib::TableView>();
    contentTable_->set_id(1004);
    contentTable_->accessibility_name = "EventTreeView";
    // DのEventTreeViewはネイティブヘッダーなしで列状の項目を描く。
    // 空タイトルでWindows側のLVS_NOCOLUMNHEADERを選ぶ。
    contentTable_->columns = {
        {"", 0.04F, lib::TextAlignment::End, false, false},
        {"", 0.70F, lib::TextAlignment::Start, true, false},
        {"", 0.07F, lib::TextAlignment::End, true, false},
        {"", 0.19F, lib::TextAlignment::Start, true, false},
    };
    contentTable_->highlighted_column = 3U;
    contentTable_->inline_edit_column = 1U;
    contentTable_->multi_select = false;
    contentTable_->edit_on_double_click = false;
    contentTable_->set_layout_item({.row = 1U, .column = 0U,
                                    .horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Stretch,
                                    .minimum = {180.0F, 100.0F}, .grow = 1.0F});
    contentTable_->on_selection_changed = [this](const std::vector<std::size_t>& rows) {
        if (rows.empty() || rows.front() >= tableItems_.size()) return;
        if (!selectContent(tableItems_[rows.front()])) return;
        if (!contentToolArrow_ && contentPutMode_ != PutMode::Quick)
            (void)placeSelectedContentTool(false);
    };
    contentTable_->on_cell_activate = [this](const std::size_t row,
                                              const std::size_t) {
        if (row < tableItems_.size() && selectContent(tableItems_[row])) edit();
    };
    contentTable_->on_cell_edited = [this](const std::size_t row,
                                            const std::size_t,
                                            String text) {
        if (row < tableItems_.size())
            (void)editEnd(tableItems_[row], std::move(text));
    };
    // Dの既定EventTreeViewは表・一覧コントロールで
    // はなく、横に傾いた独自描画ツリーと、右の同期した詳細欄から
    // なる。contentTable_はテスト・アクセシビリティ
    // 用の共通行モデルとして残し、実際の編集面はこの図とする。
    eventGraphScroll_ = std::make_shared<lib::ScrollView>();
    eventGraphScroll_->horizontal_scroll = true;
    eventGraphScroll_->vertical_scroll = true;
    // DのEventEditor::onMouseWheelは、0以外
    // のホイールイベントごとに縦ツリーを一行進める。この規則を共通ScrollView
    // の処理で扱い、他の表示の既定36画素は維持する。
    eventGraphScroll_->wheel_step = kEventGraphRowHeight;
    eventGraphScroll_->wheel_step_per_event = true;
    // スクロールCanvasの覆われていない部分では、表示領域自身がネイティブ
    // のヒット対象になり得る。そのダブルクリックを同じコンテントのヒット判定へ
    // 渡し、見えているEventTreeViewから設定を開けるようにする。
    eventGraphScroll_->on_pointer = [this](lib::PointerEvent& event) {
        if (event.button == lib::PointerButton::Primary &&
            event.action == lib::PointerAction::DoubleClick &&
            (contentToolArrow_ || contentPutMode_ == PutMode::Quick) &&
            rightPaneActive() && selectedContent_) {
            event.handled = true;
            edit();
        }
    };
    // DはEventTreeへ固定の表示高を付けず、縦横に埋める配置セ
    // ルを与える。スクロール表示領域の最小高は0にし、下のCanvas
    // が図全体の文書サイズを保持して、残りの領域内をスクロールする。
    eventGraphScroll_->set_layout_item({.row = 1U, .column = 0U,
                                        .horizontal = lib::Align::Stretch,
                                        .vertical = lib::Align::Stretch,
                                        .minimum = {180.0F, 0.0F}, .grow = 1.0F});
    eventGraphCanvas_ = std::make_shared<lib::Canvas>();
    eventGraphCanvas_->set_id(1005);
    eventGraphCanvas_->accessibility_name = "EventTreeView";
    eventGraphCanvas_->border = false;
    // Dの独自描画EventTreeViewは、フォーカスを得ても
    // 表示領域を維持する。このCanvasはスクロール文書全体を表
    // すため、クリックごとに全体を表示させると左上へ戻ってしまう。
    eventGraphCanvas_->set_reveal_on_focus(false);
    eventGraphCanvas_->set_background(lib::Color{255, 255, 255, 255});
    eventGraphCanvas_->on_paint_context =
        [this](lib::DrawingContext& context, const lib::Rect dirty) {
            paintEventGraph(context, dirty);
        };
    eventGraphCanvas_->on_pointer =
        [this](lib::PointerEvent& event) {
            const bool hover_event = !lib::editor_window_host_options().all_mode &&
                event.pointer_type == lib::PointerType::Mouse &&
                (event.action == lib::PointerAction::Move ||
                 event.action == lib::PointerAction::Enter ||
                 event.action == lib::PointerAction::Cancel ||
                 event.action == lib::PointerAction::Leave);
            if (hover_event) {
                if (event.action == lib::PointerAction::Leave ||
                    event.action == lib::PointerAction::Cancel) graphHoverPointerPosition_.reset();
                else graphHoverPointerPosition_ = event.position;
            }
            const std::weak_ptr<bool> lifetime = nameEditLifetime_;
            handleEventGraphPointer(event);
            const auto alive = lifetime.lock();
            if (hover_event && alive && *alive) invalidateEventGraph();
        };
    eventGraphCanvas_->on_key_down =
        [this](lib::KeyEvent& event) { handleEventGraphKey(event); };
    // DのTextEditMFListener.focusLost
    // は、表示から離れると保留中のクリックによる名前変更を取り消
    // す。EventEditorは図にフォーカスがある間だけ枠
    // を描く。論理選択を変えず、取得・喪失の両方を再描画する。
    eventGraphCanvas_->on_focus = [this] {
        if (comm_.services.gui) comm_.services.gui->request_redraw(eventGraphCanvas_);
    };
    eventGraphCanvas_->on_blur = [this] {
        cancelPendingNameEdit();
        if (comm_.services.gui) comm_.services.gui->request_redraw(eventGraphCanvas_);
    };
    eventGraphCanvas_->set_layout_item({.minimum = {520.0F, 240.0F},
                                        .preferred = {760.0F, 420.0F},
                                        .grow = 1.0F});
    eventGraphScroll_->add(eventGraphCanvas_);
    rightPanel_->add(eventGraphScroll_);

    body_->add(rightPanel_);
    panel_->add(body_);
}

lib::ResourceId EventView::contentImage(const types::CType type) const {
    return comm_.contentImage ? comm_.contentImage(type) : 0;
}

lib::ResourceId EventView::templateContentImage(const StringView source) const {
    const auto summary = comm_.summary.lock();
    if (!summary) return 0;
    const auto type = ::cwxeditor::cwx::script::firstContentType(
        comm_.modelProps, summary.get(), source);
    return static_cast<std::int32_t>(type) < 0 ? 0 : contentImage(type);
}

lib::ResourceId EventView::viewImage(const StringView name,
                                     const types::CType fallback) const {
    if (comm_.eventViewImage) {
        const auto image = comm_.eventViewImage(name);
        if (image != 0) return image;
    }
    return contentImage(fallback);
}

String EventView::contentName(const types::CType type) const {
    const auto resolver = comm_.contentName;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto value = resolveContentName(type, resolver, life);
    return value.value_or(String{});
}

String EventView::contentDescription(const types::CType type) const {
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto resolver = comm_.contentDesc;
    const auto editorProps = comm_.editorProps;
    const auto modelProps = comm_.modelProps;
    const auto summary = comm_.summary.lock();
    String description = resolver ? resolver(type) : String{};
    if (life.expired()) return {};
    if (!summary || !summary->legacy()) return description;

    String version;
    switch (type) {
    case types::CType::MoveBgImage:
    case types::CType::ReplaceBgImage:
    case types::CType::LoseBgImage: version = "1"; break;
    case types::CType::BranchMultiCoupon:
    case types::CType::BranchMultiRandom: version = "2"; break;
    case types::CType::MoveCard: version = "3"; break;
    case types::CType::ChangeEnvironment: version = "4"; break;
    case types::CType::CustomXmlContent: version = "6"; break;
    default: return description;
    }
    const auto format = [&description, &version](const auto& messages) {
        return utils::tryFormat(messages.contentDescWsnN.value, description, version);
    };
    const auto* editor_props = editorProps ? editorProps() : nullptr;
    if (life.expired()) return {};
    if (editor_props)
        return format(editor_props->requiredMsgs());
    if (modelProps) return format(modelProps->requiredMsgs());
    static const ::cwxeditor::cwx::msgs::Msgs defaults{};
    return format(defaults);
}

namespace {
[[nodiscard]] const ::cwxeditor::cwx::msgs::Msgs& fallbackEventViewMessages() {
    // 有効なモデルがメッセージを提供する間は、大きな代替メッセージ一覧を読み込まない。
    static const ::cwxeditor::cwx::msgs::Msgs fallback_messages{};
    return fallback_messages;
}
}
String EventView::eventText(const ContentPtr& parent,
                            const ContentPtr& content) const {
    if (!content) return {};
    // DのEventTreeView.eventText()
    // は最上位Startの名前を表示する。コンテントアイコン
    // の直後のラベルであり、右の別枠の詳細文字列ではない。
    if (!parent) return content->name();

    const auto preview = [&](const String& text) {
        return comm_.branchPreviewText ? comm_.branchPreviewText(content, text) : text;
    };
    // Dは汎用の次種別の検査より先に、XMLの派生種別へ振り分ける。
    if (parent->type() == types::CType::CustomXmlContent) {
        const auto& xml = parent->xmlType();
        const bool success = content->name() != "×";
        if (xml == "TalkMessage" || xml == "TalkDialog") {
            // C++の意図的な拡張。DのeventText分岐は全体設
            // 定だけを見るが、他のイベントラベルと同様に、所有するXML
            // コンテントのexpandSPChars設定も反映する。
            return parent->expandSPChars() ? preview(content->name())
                                           : content->name();
        }
        struct Label { const char* type; const char* yes; const char* no; };
        static constexpr Label labels[] = {
            {"BranchFlag", "対象フラグ = TRUE", "対象フラグ = FALSE"},
            {"BranchFlagValue", "対象のフラグが同値", "対象のフラグの値が異なる"},
            {"BranchStep", "対象ステップ >= 指定値", "対象ステップ < 指定値"},
            {"BranchSelect", "メンバ選択: 成功", "メンバ選択: 失敗"},
            {"BranchAbility", "能力判定: 成功", "能力判定: 失敗"},
            {"BranchRandom", "ランダム: 成功", "ランダム: 失敗"},
            {"BranchLevel", "レベル判定: 以上", "レベル判定: 未満"},
            {"BranchStatus", "状態判定: 合致", "状態判定: 不合致"},
            {"BranchPartyNumber", "人数判定: 以上", "人数判定: 未満"},
            {"BranchRound", "ラウンド判定: 合致", "ラウンド判定: 不合致"},
            {"BranchCast", "キャスト存在: あり", "キャスト存在: なし"},
            {"BranchItem", "アイテム所持: あり", "アイテム所持: なし"},
            {"BranchSkill", "スキル所持: あり", "スキル所持: なし"},
            {"BranchBeast", "召喚獣存在: あり", "召喚獣存在: なし"},
            {"BranchInfo", "情報所持: あり", "情報所持: なし"},
            {"BranchMoney", "所持金: 以上", "所持金: 未満"},
            {"BranchCoupon", "クーポン分岐: 合致", "クーポン分岐: 不合致"},
            {"BranchCompleteStamp", "終了シナリオ分岐: 済み", "終了シナリオ分岐: 未完了"},
            {"BranchGossip", "ゴシップ分岐: あり", "ゴシップ分岐: なし"},
            {"BranchKeyCode", "キーコード分岐: 合致", "キーコード分岐: 不合致"}
        };
        for (const auto& label : labels) if (xml == label.type) return success ? label.yes : label.no;
        if (xml == "BranchMultiCoupon" && comm_.modelProps) {
            const auto& m=comm_.modelProps->requiredMsgs();
            return content->name().empty() ? utils::tryFormat(m.xmlBranchMultiCouponFailure.value,types::fromRange(parent->range()))
                : utils::tryFormat(m.xmlBranchMultiCouponSuccess.value,types::fromRange(parent->range()),parent->expandSPChars() ? preview(content->name()) : content->name());
        }
        if (xml != "BranchStepValue") return {};
        if (!comm_.modelProps) return {};
        const auto& m=comm_.modelProps->requiredMsgs();
        const auto summary=comm_.summary.lock();
        const auto name=[&](const String& path) {
            if (path.empty() || !summary) return m.noSelectStep.value;
            return flagmodel::findVar<flagmodel::Step>(summary->flagDirRoot(),parent->useCounter(),path)
                ? path : utils::tryFormat(m.noStep.value,path);
        };
        const auto& key=content->name();
        return utils::tryFormat((key=="=" ? m.xmlBranchStepValueEq : key=="<" ? m.xmlBranchStepValueLesser : m.xmlBranchStepValueGreater).value,
            name(parent->step()),name(parent->step2()));
    }
    // CardWirth XMLの子のContent::nameは分岐
    // キーを保持する。そのまま表示すると○/×だけの行になるため、Dの
    // EventTreeViewと同様に上側の所有コンテントから表示文
    // 字列を求める。テキスト分岐は例外で、子の名前自体が選択肢となる。
    if (parent->detail().nextType == types::CNextType::Text) {
        if (!content->name().empty()) return preview(content->name());
        return prop_.etc.showInputGuide ? prop_.etc.inputGuideText : String{" "};
    }

    // Startや順次コンテントなど通常の所有元は、アイコン横
    // に子のcontentTextを置かない。Dはこの位置を空
    // にし、contentTextは右の詳細枠だけに表示する。
    if (parent->detail().nextType == types::CNextType::None)
        return {};

    const auto& branchName = content->name();
    const bool success = branchName != "×";
    const auto summary = comm_.summary.lock();
    const auto variable_root = localVariableRoot_
        ? localVariableRoot_
        : (summary ? summary->flagDirRoot() : flagmodel::FlagDirPtr{});
    const auto counter = parent->useCounter()
        ? parent->useCounter()
        : localVariableUseCounter_;
    const auto variable = [&](const String& path, const StringView fallback) {
        return path.empty() ? String(fallback) : path;
    };
    if (comm_.modelProps) {
        const auto& m = comm_.modelProps->requiredMsgs();
        const auto fmt = [](const auto& message, const auto&... args) {
            return utils::tryFormat(message.value, args...);
        };
        const bool matched = parent->invertResult() ? !success : success;
        const auto join = [](const auto& values, const String& separator) {
            String text;
            for (const auto& value : values) { if (!text.empty()) text += separator; text += value; }
            return text;
        };
        switch (parent->type()) {
        case types::CType::BranchLevel:
            return fmt(success ? m.branchLevelSuccess : m.branchLevelFailure,
                parent->average() ? m.levelAverage.value : m.levelSelected.value,
                parent->unsignedLevel());
        case types::CType::BranchStatus: {
            const auto range=comm_.branchRangeText ? comm_.branchRangeText(parent->range(),parent->holdingCoupon()) : m.rangeName(parent->range());
            return fmt(matched ? m.branchStatusSuccess : m.branchStatusFailure,range,m.statusName(parent->status()));
        }
        case types::CType::BranchMultiCoupon: {
            const auto range=comm_.branchRangeText ? comm_.branchRangeText(parent->range(),parent->holdingCoupon()) : m.rangeName(parent->range());
            return branchName.empty() ? fmt(m.branchMultiCouponFailure,range)
                : fmt(m.branchMultiCouponSuccess,range,parent->expandSPChars() ? preview(branchName) : branchName);
        }
        case types::CType::BranchGossip: {
            const auto gossip=parent->expandSPChars() ? preview(parent->gossip()) : parent->gossip();
            return fmt(success ? m.branchGossipSuccess : m.branchGossipFailure,gossip.empty() ? m.noSelectGossip.value : gossip);
        }

        case types::CType::BranchSelect: {
            const auto& target = parent->targetAll() ? m.partyAll : m.partyActive;
            const auto method = parent->selectionMethod();
            const auto& selection = method == types::SelectionMethod::Manual ? m.manualSelect
                : method == types::SelectionMethod::Random ? m.autoSelect : m.valuedSelect;
            return fmt(success ? m.selectMemberSuccess : method == types::SelectionMethod::Manual
                ? m.selectMemberCancel : m.selectMemberFailure, target.value, selection.value);
        }
        case types::CType::BranchAbility:
            return fmt(success ? m.branchAbilitySuccess : m.branchAbilityFailure,
                m.targetName(parent->targetS().m), parent->signedLevel(),
                m.physicalName(parent->physical()), m.mentalName(parent->mental()),
                (parent->invertResult() ? m.abilityLowest : m.abilityHighest).value);
        case types::CType::BranchCast: {
            String name=m.noSelectCast.value;
            if (parent->casts() && summary) {
                const auto card=summary->cwCast(parent->casts());
                name=card ? fmt(m.nameWithID,card->id(),card->name()) : fmt(m.noCast,parent->casts());
            }
            return fmt(success ? m.branchCastSuccess : m.branchCastFailure,name);
        }
        case types::CType::BranchInfo: {
            String name=m.noSelectInfo.value;
            if (parent->info() && summary) {
                const auto card=summary->info(parent->info());
                name=card ? fmt(m.nameWithID,card->id(),card->name()) : fmt(m.noInfo,parent->info());
            }
            return fmt(success ? m.branchInfoSuccess : m.branchInfoFailure,name);
        }
        case types::CType::BranchItem:
        case types::CType::BranchSkill:
        case types::CType::BranchBeast: {
            String name;
            const auto resolve = [&](const auto& card, const auto id, const auto& missing) {
                return card ? fmt(m.nameWithID,card->id(),card->name()) : fmt(missing,id);
            };
            if (parent->type()==types::CType::BranchItem) {
                name=m.noSelectItem.value;
                if (parent->item() && summary) name=resolve(summary->item(parent->item()),parent->item(),m.noItem);
            } else if (parent->type()==types::CType::BranchSkill) {
                name=m.noSelectSkill.value;
                if (parent->skill() && summary) name=resolve(summary->skill(parent->skill()),parent->skill(),m.noSkill);
            } else {
                name=m.noSelectBeast.value;
                if (parent->beast() && summary) name=resolve(summary->beast(parent->beast()),parent->beast(),m.noBeast);
            }
            if (parent->range()==types::Range::SelectedCard)
                return fmt(matched ? m.branchEffectCardForSelectedCardSuccess : m.branchEffectCardForSelectedCardFailure,name);
            return fmt(matched ? m.branchEffectCardSuccess : m.branchEffectCardFailure,m.rangeName(parent->range()),name);
        }
        case types::CType::BranchCoupon: {
            auto names=parent->couponNames();
            if (parent->expandSPChars()) for (auto& name:names) name=preview(name);
            const auto range=m.rangeName(parent->range());
            if (names.size()<2U) return fmt(matched ? m.branchCouponSuccess : m.branchCouponFailure,
                range,names.empty() || names.front().empty() ? m.noSelectCoupon.value : names.front());
            for (auto& name:names) name=fmt(m.couponNames,name);
            return fmt(matched ? m.branchCouponMultiSuccess : m.branchCouponMultiFailure,range,
                join(names,m.couponNamesSeparator.value),
                (parent->matchingType()==types::MatchingType::Or ? m.matchingTypeOr : m.matchingTypeAnd).value);
        }
        case types::CType::BranchRandomSelect: {
            const auto range=comm_.branchCastRangesText ? comm_.branchCastRangesText(parent->castRange()) : dutils::castRangesName(nullptr,parent->castRange());
            const bool level=parent->levelMax()>0, status=parent->status()!=types::Status::None;
            if (!level && !status) return fmt(success ? m.branchRandomSelectSuccessN : m.branchRandomSelectFailureN,range);
            const auto condition=level && status ? fmt(m.randomSelectCondition3,parent->levelMin(),parent->levelMax(),m.statusName(parent->status()))
                : level ? fmt(m.randomSelectCondition1,parent->levelMin(),parent->levelMax())
                : fmt(m.randomSelectCondition2,m.statusName(parent->status()));
            if (parent->invertResult()) return fmt(success ? m.branchRandomSelectSuccessInvert : m.branchRandomSelectFailureInvert,range,condition);
            return fmt(success ? m.branchRandomSelectSuccess : m.branchRandomSelectFailure,range,condition);
        }
        case types::CType::BranchKeyCode: {
            const auto name=parent->keyCode().empty() ? m.noKeyCode.value : parent->keyCode();
            const auto condition=(parent->matchingCondition()==types::MatchingCondition::Has ? m.contains : m.notContains).value;
            const bool card=parent->keyCodeRange()==types::Range::SelectedCard;
            if (parent->targetIsSkill() && parent->targetIsItem() && parent->targetIsBeast() && parent->targetIsHand()) {
                if (card) return fmt(matched ? m.branchKeyCodeWithConditionAllTypeForSelectedCardSuccess : m.branchKeyCodeWithConditionAllTypeForSelectedCardFailure,name,condition);
                return fmt(matched ? m.branchKeyCodeWithConditionAllTypeSuccess : m.branchKeyCodeWithConditionAllTypeFailure,name,condition,m.rangeName(parent->keyCodeRange()));
            }
            std::vector<String> targets;
            if(parent->targetIsSkill())targets.push_back(m.targetIsSkill.value);
            if(parent->targetIsItem())targets.push_back(m.targetIsItem.value);
            if(parent->targetIsBeast())targets.push_back(m.targetIsBeast.value);
            if(parent->targetIsHand())targets.push_back(m.targetIsHand.value);
            const auto target=targets.empty() ? m.noSelectTarget.value : join(targets,m.targetSeparator.value);
            if (card) return fmt(matched ? m.branchKeyCodeWithConditionForSelectedCardSuccess : m.branchKeyCodeWithConditionForSelectedCardFailure,name,condition,target);
            return fmt(matched ? m.branchKeyCodeWithConditionSuccess : m.branchKeyCodeWithConditionFailure,name,condition,target,m.rangeName(parent->keyCodeRange()));
        }
        default:break;
        }
    }
    const auto& m = comm_.modelProps
        ? comm_.modelProps->requiredMsgs() : fallbackEventViewMessages();
    const auto flagDisplayName = [&](const String& path) {
        if (path.empty() || !summary) return m.noSelectFlag.value;
        return flagmodel::findVar<flagmodel::Flag>(variable_root, counter, path)
            ? path : utils::tryFormat(m.noFlag.value, path);
    };
    const auto stepDisplayName = [&](const String& path) {
        if (path.empty() || !summary) return m.noSelectStep.value;
        return flagmodel::findVar<flagmodel::Step>(variable_root, counter, path)
            ? path : utils::tryFormat(m.noStep.value, path);
    };
    // DのEventTreeView.eventText()
    // に対応する。左の文字は子が選ぶ分岐を説明し、右
    // 枠の詳しいコンテント説明とは意図的に分ける。
    switch (parent->type()) {
    case types::CType::BranchFlag: {
        String on = m.flagOn.value, off = m.flagOff.value;
        if (!parent->flag().empty() && summary) {
            if (const auto found = flagmodel::findVar<flagmodel::Flag>(
                    variable_root, counter, parent->flag())) {
                on = found->on();
                off = found->off();
            }
        }
        const bool value = comm_.modelProps
            ? branchName != comm_.modelProps->sys().evtChildFalse() : success;
        return utils::tryFormat(m.evtChildBrVar.value,
                                flagDisplayName(parent->flag()), value ? on : off);
    }
    case types::CType::BranchMultiStep: {
        const auto defaultChild = comm_.modelProps
            ? comm_.modelProps->sys().evtChildDefault() : String("Default");
        int valueIndex = -1;
        if (branchName != defaultChild && !branchName.empty() &&
            std::ranges::all_of(branchName, [](const unsigned char value) {
                return value >= '0' && value <= '9';
            })) {
            try {
                valueIndex = std::stoi(branchName);
            } catch (const std::exception&) {
                valueIndex = -1;
            }
        }
        auto value = valueIndex >= 0
            ? structs::parseDollarParams(prop_.etc.stepValueName,
                  {{'N', std::to_string(valueIndex)}})
            : m.etc.value;
        const auto name = stepDisplayName(parent->step());
        if (valueIndex >= 0 && !parent->step().empty() && summary) {
            if (const auto found = flagmodel::findVar<flagmodel::Step>(
                    variable_root, counter, parent->step())) {
                value = static_cast<std::uint32_t>(valueIndex) < found->count()
                    ? found->getValue(static_cast<std::uint32_t>(valueIndex))
                    : utils::tryFormat(m.unknownStepValue.value, valueIndex);
            }
        }
        return utils::tryFormat(m.evtChildBrVar.value, name, value);
    }
    case types::CType::BranchStep: {
        auto value = structs::parseDollarParams(prop_.etc.stepValueName,
            {{'N', std::to_string(parent->stepValue())}});
        const auto name = stepDisplayName(parent->step());
        if (!parent->step().empty() && summary) {
            if (const auto found = flagmodel::findVar<flagmodel::Step>(
                    variable_root, counter, parent->step())) {
                value = parent->stepValue() >= 0 &&
                                static_cast<std::uint32_t>(parent->stepValue()) < found->count()
                    ? found->getValue(static_cast<std::uint32_t>(parent->stepValue()))
                    : utils::tryFormat(m.unknownStepValue.value, parent->stepValue());
            }
        }
        const bool valueIsTrue = comm_.modelProps
            ? branchName != comm_.modelProps->sys().evtChildFalse() : success;
        return utils::tryFormat(valueIsTrue ? m.stepMoreThan.value : m.stepLessThan.value,
                                name, value);
    }
    case types::CType::BranchRandom: {
        const auto percent = !success && prop_.etc.branchRandomFailureFormat
            ? 100 - parent->percent() : parent->percent();
        return std::to_string(percent) + (success ? "%成功" : "%失敗");
    }
    case types::CType::BranchLevel: {
        const auto target = parent->average() ? "パーティ全員の平均値" : "選択中のメンバ";
        return String(target) + "がレベル" + std::to_string(parent->unsignedLevel()) +
               (success ? "以上" : "未満");
    }
    case types::CType::BranchStatus: {
        auto range = dutils::rangeName(nullptr, parent->range(), parent->holdingCoupon());
        const auto state = std::to_string(static_cast<std::int32_t>(parent->status()));
        const bool matched = parent->invertResult() ? !success : success;
        return range + "が状態[" + state + "]で" + (matched ? "ある" : "ない");
    }
    case types::CType::BranchPartyNumber:
        return success
            ? "パーティに" + std::to_string(parent->partyNumber()) + "人以上いる"
            : "パーティは" + std::to_string(parent->partyNumber()) + "人未満";
    case types::CType::BranchArea:
    case types::CType::BranchBattle: {
        const bool battle = parent->type() == types::CType::BranchBattle;
        std::optional<std::uint64_t> id;
        if (!branchName.empty() && std::ranges::all_of(branchName, [](unsigned char c) { return c >= '0' && c <= '9'; })) {
            try { id = std::stoull(branchName); } catch (const std::exception&) {}
        }
        std::optional<String> name;
        if (id && summary) {
            if (battle) { if (const auto value = summary->battle(*id)) name = value->name(); }
            else { if (const auto value = summary->area(*id)) name = value->name(); }
        }
        if (comm_.branchLocationText) return comm_.branchLocationText(battle, id, name);
        const String kind = battle ? "バトル" : "エリア";
        if (!id) return kind + " = その他";
        if (!name) return "存在しない" + kind + "(ID:" + std::to_string(*id) + ")";
        return kind + " = " + std::to_string(*id) + "." + *name;
    }
    case types::CType::BranchIsBattle:
        return success ? "イベント発生時の状況が戦闘中"
                       : "イベント発生時の状況が戦闘中以外";
    case types::CType::BranchMoney:
        return success
            ? std::to_string(parent->money()) + "sp以上所持している"
            : std::to_string(parent->money()) + "sp以上所持していない";
    case types::CType::BranchCompleteStamp: {
        const auto name = variable(parent->completeStamp(), "未選択の終了印");
        return "シナリオ「" + name + "」が終了済みで" + (success ? "ある" : "ない");
    }
    case types::CType::BranchGossip: {
        const auto name = variable(parent->expandSPChars() ? preview(parent->gossip()) : parent->gossip(), "未選択のゴシップ");
        return "ゴシップ「" + name + "」が宿屋に" + (success ? "ある" : "無い");
    }
    case types::CType::BranchStepCmp: {
        // DのevtChildBrStepCmpは各パスを解決し、参照切れの
        // StepにnoStepを付ける。壊れた参照でも行の意味を示すため。
        const auto name = [&](const String& path) {
            if (path.empty() || !summary) return m.noSelectStep.value;
            return flagmodel::findVar<flagmodel::Step>(variable_root, counter,
                                                       path)
                ? path : utils::tryFormat(m.noStep.value, path);
        };
        const auto left = name(parent->step());
        const auto right = name(parent->step2());
        if (branchName == "=") return "ステップ「" + left + "」が「" + right + "」と同値";
        if (branchName == "<") return "ステップ「" + left + "」が「" + right + "」より小さい";
        return "ステップ「" + left + "」が「" + right + "」より大きい";
    }
    case types::CType::BranchFlagCmp: {
        // DのevtChildBrFlagCmpは比
        // 較の両側に同じパス欠落の診断を使い、未解
        // 決パスを有効な変数名のように表示しない。
        const auto name = [&](const String& path) {
            if (path.empty() || !summary) return m.noSelectFlag.value;
            return flagmodel::findVar<flagmodel::Flag>(variable_root, counter,
                                                       path)
                ? path : utils::tryFormat(m.noFlag.value, path);
        };
        const auto left = name(parent->flag());
        const auto right = name(parent->flag2());
        return "フラグ「" + left + "」が「" + right + "」と" +
               (success ? "同値" : "値が異なる");
    }
    case types::CType::BranchRound: {
        const auto comparison = success
            ? (parent->comparison3() == types::Comparison3::Eq ? "である" :
               parent->comparison3() == types::Comparison3::Lt ? "より大きい" : "より小さい")
            : (parent->comparison3() == types::Comparison3::Eq ? "ではない" :
               parent->comparison3() == types::Comparison3::Lt ? "以下" : "以上");
        return "バトルが" + std::to_string(parent->round()) + "ラウンド" + comparison;
    }
    case types::CType::BranchMultiCoupon:
        return branchName.empty()
            ? "対象が全ての称号を所有していない"
            : "対象が称号「" + (parent->expandSPChars() ? preview(branchName) : branchName) + "」を所有している";
    case types::CType::BranchVariant: {
        const auto expression = parent->expression().empty()
            ? String("式未入力") : dutils::exprStr(parent->expression());
        return String(success ? "TRUE =〔 " : "FALSE =〔 ") + expression + " 〕";
    }
    default:
        break;
    }

    // 対象・カード・キーコードの分岐も、Dのコンテント名
    // と分岐結果を使う。右枠の説明文を左列へ流用しない。
    auto result = contentName(parent->type());
    if (branchName == "○") return result + "：成立";
    if (branchName == "×") return result + "：不成立";
    if (branchName == "Default") return result + "：その他";
    if (!branchName.empty()) return result + "：" + branchName;
    return result;
}

String EventView::ownerDisplayName() const {
    if (!ownerTitle_.empty()) return ownerTitle_;
    if (area_) return ownerDisplayName(*area_);
    return messages_.events;
}

String EventView::ownerDisplayName(const EventTreeOwner& owner) const {
    namespace area = ::cwxeditor::cwx::area;
    if (&owner == area_ && !ownerTitle_.empty()) return ownerTitle_;
    if (const auto* value = dynamic_cast<const area::AbstractArea*>(&owner);
        value && !value->name().empty())
        return value->name();
    if (dynamic_cast<const area::PlayerCardEvents*>(&owner))
        return messages_.playerCard;
    if (const auto* card = dynamic_cast<const area::MenuCard*>(&owner))
        return card->name();
    if (const auto* card = dynamic_cast<const area::EnemyCard*>(&owner)) {
        // DのcardNameは、明示値が空でも有効な上書き値を返す。
        if (card->isOverrideName())
            return card->overrideName();
        if (const auto summary = comm_.summary.lock()) {
            if (const auto cast = summary->cwCast(card->id())) return cast->name();
        }
        // DのcardNameはリンク先Castを解決できなければ空
        // ラベルを返す。無効な保存済み上書き値を表示の代替にしない。
        return {};
    }
    return messages_.events;
}

EventTreePtr EventView::findTreeByObjectId(const StringView id) const {
    if (!area_ || id.empty()) return {};
    for (auto* owner : etos(*area_))
        if (const auto tree = findTreeById(owner, id)) return tree;
    return {};
}

String EventView::selectedContentId() const {
    if (rightPaneActive() && selectedContent_ &&
        !selectedContent_->eventId().empty())
        return selectedContent_->eventId();
    if (selectedTree_) return selectedTree_->objectId();
    return {};
}

std::uint32_t EventView::startUseCount(const ContentPtr& content) const {
    if (!content || content->type() != types::CType::Start || !selectedTree_)
        return 0U;

    // Start参照はEventTree内に限定される。汎用
    // のシナリオUseCounterにはStartIdの入れ
    // 物がないため、未対応IDを問い合わせず、このツリーのLink/Call
    // Startを巡回してDのstartUseCounter
    // に合わせる。最初のStartは、明示参照がな
    // くても通常のイベント入口として一回数える。
    std::uint32_t result = !selectedTree_->starts().empty() &&
                                   selectedTree_->starts().front() == content
                               ? 1U
                               : 0U;
    std::vector<ContentPtr> pending(selectedTree_->starts().begin(),
                                    selectedTree_->starts().end());
    std::unordered_set<const event::Content*> visited;
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        if (!current || !visited.insert(current.get()).second) continue;
        if ((current->type() == types::CType::LinkStart ||
             current->type() == types::CType::CallStart) &&
            current->start() == content->name())
            ++result;
        pending.insert(pending.end(), current->next().begin(), current->next().end());
    }
    return result;
}

void EventView::rebuildContentTable() {
    if (!contentTable_) return;
    std::vector<TreeItemId> preparedItems;
    std::vector<lib::TableRow> preparedRows;
    std::unordered_map<const event::Content*, std::size_t> preparedLogicalLines;
    std::size_t logical_line = 0;
    if (contents_) {
        const auto visible = [this](const lib::TreeNode& node) {
            auto parent_id = node.parent;
            while (parent_id != 0) {
                const auto parent = std::find_if(
                    contents_->nodes.begin(), contents_->nodes.end(),
                    [parent_id](const lib::TreeNode& candidate) {
                        return candidate.id == parent_id;
                    });
                if (parent == contents_->nodes.end()) break;
                // 合成したEventTreeルートは表示したままにする。
                // 各実コンテントの展開状態が制御するのは、その子孫だけ。
                if (parent->parent != 0 && !parent->expanded) return false;
                parent_id = parent->parent;
            }
            return true;
        };
        for (const auto& node : contents_->nodes) {
            const auto* info = nodeInfo(node.id);
            if (!info || info->kind != NodeInfo::Kind::Content || !info->content)
                continue;
            const auto number = ++logical_line;
            preparedLogicalLines[info->content.get()] = number;
            if (!visible(node)) continue;
            const auto& content = info->content;
            lib::TableRow row;
            lib::ListItem line;
            if (showLineNumber_)
                line.text = std::to_string(number);
            lib::ListItem description;
            // 名前のないコンテントを斜線だけで表示
            // すると、多くのコンテントが無名なの
            // で、詳細列に値があってもツリーが空
            // に見える。Dと同様にツリー列へ概要
            // を描き、連続した階層線を表示する。
            std::vector<ContentPtr> ancestors;
            for (auto parent = content->parent(); parent; parent = parent->parent())
                ancestors.push_back(parent);
            std::reverse(ancestors.begin(), ancestors.end());
            for (std::size_t level = 1U; level < ancestors.size(); ++level) {
                const auto& ancestor = ancestors[level];
                const auto& grandParent = ancestors[level - 1U];
                const auto& siblings = grandParent->next();
                const bool last = !siblings.empty() && siblings.back() == ancestor;
                description.text += last ? "   " : "│  ";
            }
            if (const auto parent = content->parent()) {
                const auto& siblings = parent->next();
                const bool last = !siblings.empty() && siblings.back() == content;
                description.text += last ? "└─ " : "├─ ";
            }
            description.text += eventText(content->parent(), content);
            description.icon = contentImage(content->type());
            if (const auto spec = graphEditSpec(content)) {
                description.editor_value = spec->value;
                if (spec->choices.empty()) {
                    description.editor = lib::CellEditorKind::Text;
                } else {
                    description.editor = spec->editable_choice
                        ? lib::CellEditorKind::EditableChoice
                        : lib::CellEditorKind::Choice;
                    for (const auto& choice : spec->choices) {
                        description.editor_choices.push_back(choice.text);
                        description.editor_choice_values.push_back(choice.value);
                    }
                }
            } else {
                description.enabled = false;
            }
            lib::ListItem uses;
            if (prop_.etc.drawCountOfUseOfStart && content->type() == types::CType::Start)
                uses.text = "利用数 " + std::to_string(startUseCount(content));
            lib::ListItem detail;
            if (showEventTreeDetail_) {
                if (prop_.etc.drawContentIconOnDetail)
                    detail.icon = contentImage(content->type());
                if (content->type() == types::CType::Start)
                    detail.text = "スタートコンテント[" + content->name() + "]";
                else
                    detail.text = contentText(content);
            }
            row.cells = {std::move(line), std::move(description), std::move(uses),
                         std::move(detail)};
            row.warnings = node.warnings;
            row.warning_icon = node.warning_icon;
            preparedRows.push_back(std::move(row));
            preparedItems.push_back(node.id);
        }
    }
    std::vector<std::size_t> preparedSelection;
    if (selectedContentId_) {
        const auto found = std::find(preparedItems.begin(), preparedItems.end(),
                                     selectedContentId_);
        if (found != preparedItems.end())
            preparedSelection = {static_cast<std::size_t>(
                std::distance(preparedItems.begin(), found))};
    }
    auto publication = lib::prepare_table_rows(contentTable_, std::move(preparedRows),
                                                std::move(preparedSelection));
    if (!publication.commit()) return;
    tableItems_.swap(preparedItems);
    graphLogicalLines_.swap(preparedLogicalLines);
    rebuildGraphRows();
    invalidateEventGraph();
}

bool EventView::refreshGraphPreferences() {
    // Dは配置・描画時に現在のEditor設定を読む。新しく開いたホス
    // トの取得状態によって、既存の図のウィンドウに古い規則を残さない。
    auto* props = comm_.editorProps ? comm_.editorProps() : nullptr;
    if (!props) return false;
    bool changed = false;
    const auto assign = [&changed](bool& target, bool value) {
        if (target != value) { target = value; changed = true; }
    };
    assign(prop_.etc.drawContentTreeLine, props->var().drawContentTreeLine.value);
    assign(prop_.etc.drawCountOfUseOfStart, props->var().drawCountOfUseOfStart.value);
    assign(prop_.etc.showTerminalMark, props->var().showTerminalMark.value);
    assign(prop_.etc.forceIndentBranchContent, props->var().forceIndentBranchContent.value);
    assign(prop_.etc.showTargetStartLineNumber, props->var().showTargetStartLineNumber.value);
    return changed;
}

void EventView::rebuildGraphRows() {
    (void)refreshGraphPreferences();
    graphRowsScale_ = graphScale();
    graphRows_.clear();
    if (eventGraphCanvas_) eventGraphCanvas_->set_tooltip({});
    graphRows_.reserve(tableItems_.size());
    std::unordered_map<const event::Content*, std::size_t> row_by_content;
    // Dの独自描画EventEditorは、クライアント原点
    // から最初の行を始める。人工的な上余白を置くと、周囲の配
    // 置に関係なく全EventViewに同じ空帯ができる。
    float y = 0.0F;
    for (std::size_t row = 0; row < tableItems_.size(); ++row) {
        const auto id = tableItems_[row];
        const auto* info = nodeInfo(id);
        if (!info || !info->content) continue;
        GraphRow value;
        value.id = id;
        value.content = info->content;
        if (comm_.warnings && prop_.etc.drawContentWarnings)
            value.warnings = comm_.warnings(*info->content);
        value.warning_icon = value.warnings.empty() ? 0U : comm_.warningImage;
        value.row = row;
        value.logical_line = graphLogicalLines_.at(info->content.get());
        value.y = y;
        if (const auto parent = info->content->parent()) {
            const auto found = row_by_content.find(parent.get());
            if (found != row_by_content.end()) {
                value.parent_row = found->second;
                const auto& parent_row = graphRows_[found->second];
                value.depth1 = parent_row.depth1;
                value.depth2 = parent_row.depth2;
                // DはStart・分岐コンテントの後にアイコン全幅を
                // 使い、通常の単一子の連鎖には設定された傾きを使う。
                if (parent->type() == types::CType::Start ||
                    parent->next().size() != 1U ||
                    (prop_.etc.forceIndentBranchContent && event::isBranchContent(parent->type())))
                    ++value.depth1;
                else
                    ++value.depth2;
            }
        }
        value.hit = {0.0F, y, 1000.0F, graphRowHeight()};
        row_by_content[info->content.get()] = graphRows_.size();
        graphRows_.push_back(std::move(value));
        y += graphRowHeight();
    }
    if (eventGraphCanvas_) {
        const float height = std::max(graphPx(240.0F), y + graphPx(4.0F));
        // Dは独立した詳細欄を現在の表示領域内に保つ。
        // 実際の階層が深くなった場合だけ横へ広げる。
        float graphRight = 0.0F;
        const float slope = graphPx(slope_ ? static_cast<float>(slope_->value) : 16.0F);
        for (const auto& row : graphRows_) {
            // eventeditor.dは、字下げした分岐を_imageWidth
            // （16）だけ進める。追加の文字間隔4画素は加えない。
            const float x = (showLineNumber_ ? graphPx(kEventGraphLineNumberWidth) : 0.0F) +
                            static_cast<float>(row.depth1) * graphPx(16.0F) +
                            static_cast<float>(row.depth2) * slope;
            graphRight = std::max(graphRight, x + graphPx(160.0F));
        }
        // 描画で新しい文字列を計測するまで、最後の計測寸法を
        // 保持する。途中の行更新で横の表示領域を制限しない。
        graphRight = std::max(graphRight, graphPx(graphMeasuredRight_));
        const float minimumWidth = std::max(graphPx(520.0F), graphRight + graphPx(eventGraphDetailWidth_ + 12.0F));
        const float preferredWidth = std::max(graphPx(760.0F), minimumWidth);
        // LayoutItemの寸法は指定単位で、共通配置
        // 側がUI文字倍率を適用する。上記の図の座標はすで
        // に実画素なので、この配置の境界でだけ変換する。
        eventGraphCanvas_->set_layout_item({.minimum = {minimumWidth / graphScale(),
                                                       height / graphScale()},
                                            .preferred = {preferredWidth / graphScale(),
                                                          height / graphScale()},
                                            .grow = 1.0F});
    }
}

void EventView::paintEventGraph(lib::DrawingContext& context, const lib::Rect) {
    if (refreshGraphPreferences() || graphRowsScale_ != graphScale()) rebuildGraphRows();
    const auto canvas_bounds = eventGraphCanvas_ ? eventGraphCanvas_->bounds() : lib::Rect{};
    const float width = std::max(graphPx(520.0F), canvas_bounds.width);
    const float height = std::max(
        canvas_bounds.height,
        graphRows_.empty() ? graphPx(240.0F) : graphRows_.back().y + graphPx(42.0F));
    const float detail_x = eventGraphDetailX(width);
    const float content_origin =
        showLineNumber_ ? graphPx(kEventGraphLineNumberWidth) : 0.0F;
    // ポインターが動かなくても、描画ごとに現在の配置済みCanvas矩形に対して
    // 位置を解決する。ホイール・スクロールバー移動や詳細欄の分割ドラッグも含む。
    TreeItemId pointer_hover = 0;
    if (!lib::editor_window_host_options().all_mode && graphHoverPointerPosition_) {
        auto viewport = canvas_bounds;
        if (eventGraphScroll_ && eventGraphScroll_->bounds().width > 0.0F &&
            eventGraphScroll_->bounds().height > 0.0F) {
            const auto extent = eventGraphScroll_->content_extent.value_or(
                lib::Size{canvas_bounds.width, canvas_bounds.height});
            viewport = lib::pixel_scroll_geometry(eventGraphScroll_->bounds(), extent,
                eventGraphScroll_->scroll_offset, eventGraphScroll_->horizontal_scroll,
                eventGraphScroll_->vertical_scroll).viewport;
        }
        const auto point = *graphHoverPointerPosition_;
        if (pointInRect(viewport, point) && pointInRect(canvas_bounds, point)) {
            const float y = point.y - canvas_bounds.y;
            const auto found = std::find_if(graphRows_.begin(), graphRows_.end(),
                [this, y](const GraphRow& row) {
                    return y >= row.y && y < row.y + graphRowHeight();
                });
            if (found != graphRows_.end()) pointer_hover = found->id;
        }
    }
    constexpr lib::Color white{255, 255, 255, 255};
    constexpr lib::Color black{0, 0, 0, 255};
    constexpr lib::Color grid{210, 216, 221, 255};
    constexpr lib::Color selected{171, 211, 247, 255};
    constexpr lib::Color drag_hover{224, 240, 255, 255};
    constexpr lib::Color branch{176, 184, 190, 255};
    const bool graph_has_focus = eventGraphCanvas_ && comm_.services.gui &&
        comm_.services.gui->focused_widget() == eventGraphCanvas_;
    // DWTはイベント名と独立した説明列の両方に、継承したEditor
    // ウィジェットのフォントを使う。固定のMS UI GothicのCanvas
    // フォントでは共通UI倍率を通らず、小さく見えてしまう。
    const lib::FontSpec font = lib::effective_editor_widget_font();
    context.clear(white);
    if (showLineNumber_)
        context.draw_line({graphPx(kEventGraphLineNumberWidth), 0.0F},
                          {graphPx(kEventGraphLineNumberWidth), height}, grid, 1.0F,
                          lib::StrokeStyle::Solid);
    context.draw_line({detail_x, 0.0F}, {detail_x, height}, grid, 1.0F,
                      lib::StrokeStyle::Solid);

    // コンテントアイコンより先に、すべての接続線を描く
    // 。行ごとに子の線を描くと、始点が描画済みの親ア
    // イコンに重なり、終端線も自身のアイコンを横切る
    // 。Dと同様に接続図をコンテント層の背面に保つ。
    for (auto& row : graphRows_) {
        const float y = row.y;
        const float slope = graphPx(slope_ ? static_cast<float>(slope_->value) : 16.0F);
        const float x = content_origin + static_cast<float>(row.depth1) * graphPx(16.0F) +
                        static_cast<float>(row.depth2) * slope;
        row.hit = {0.0F, y, width, graphRowHeight()};
        if (row.id == selectedContentId_) {
            context.fill_rect({0.0F, y, width, graphRowHeight()}, selected);
            // Dのeventeditor.d:1545-1552は非アクティブ
            // でも塗りを維持し、フォーカス枠はフォーカス中の選択行だけに描く。
            if (graph_has_focus) {
                const float inset = graphPx(2.0F);
                context.draw_rect({inset, y + inset,
                                   std::max(0.0F, width - 2.0F * inset),
                                   std::max(0.0F, graphRowHeight() - 2.0F * inset)},
                                  black, 1.0F, lib::StrokeStyle::Dot);
            }
        } else if (row.id == dragHoverContentId_)
            context.fill_rect({0.0F, y, width, graphRowHeight()}, drag_hover);
        else if (!lib::editor_window_host_options().all_mode && row.id == pointer_hover)
            context.fill_rect({0.0F, y, width, graphRowHeight()}, lib::Color{191, 224, 255, 255});

        if (prop_.etc.drawContentWarnings && !row.warnings.empty()) {
            // eventtreeview.dは、設定幅から独立し
            // た詳細枠まで、生成した一画素の警告階調を重ねる。
            const float band_width = std::min(
                graphPx(static_cast<float>(std::max(1, prop_.etc.warningImageWidth))),
                std::max(0.0F, detail_x - content_origin));
            constexpr int bands = 24;
            const float strip_width = band_width / static_cast<float>(bands);
            for (int index = 0; index < bands; ++index) {
                auto color = prop_.etc.warningImageColor;
                color.alpha = static_cast<std::uint8_t>(
                    static_cast<unsigned>(color.alpha) *
                    static_cast<unsigned>(index + 1) /
                    static_cast<unsigned>(bands));
                context.fill_rect(
                    {detail_x - band_width + strip_width * static_cast<float>(index),
                     y, strip_width + 0.5F, graphRowHeight()}, color);
            }
        }

        if (row.parent_row && *row.parent_row < graphRows_.size()) {
            const auto& parent = graphRows_[*row.parent_row];
            const float parent_x = content_origin +
                                   static_cast<float>(parent.depth1) * graphPx(16.0F) +
                                   static_cast<float>(parent.depth2) * slope;
            const float parent_y = parent.y;
            const auto from = lib::Point{parent_x + graphPx(8.0F), parent_y + graphPx(9.0F)};
            const auto to = lib::Point{x + graphPx(8.0F), y + graphPx(9.0F)};
            if (std::abs(to.x - from.x) < 0.5F) {
                context.draw_line(from, to, branch, graphPx(3.0F),
                                  lib::StrokeStyle::Solid);
            } else {
                const float bend_y = std::max(from.y, y - graphPx(9.0F));
                context.draw_line(from, {from.x, bend_y}, branch, graphPx(3.0F),
                                  lib::StrokeStyle::Solid);
                context.draw_line({from.x, bend_y}, to, branch, graphPx(3.0F),
                                  lib::StrokeStyle::Solid);
                // 直後の行でない子には直径12指定画素の分
                // 岐点を描く。中心Xは親アイコンの中心線、
                // 中心Yは子行上端より5指定画素上とする。
                if (y > parent_y + graphPx(19.5F)) {
                    const float branch_y = y - graphPx(5.0F);
                    context.fill_ellipse({from.x - graphPx(6.0F), branch_y - graphPx(6.0F),
                                          graphPx(12.0F), graphPx(12.0F)},
                                         white);
                    context.draw_ellipse({from.x - graphPx(6.0F), branch_y - graphPx(6.0F),
                                          graphPx(12.0F), graphPx(12.0F)},
                                         branch, graphPx(3.0F), lib::StrokeStyle::Solid);
                }
            }
        }

        if (prop_.etc.drawContentTreeLine && row.row > 0U &&
            row.content->type() == types::CType::Start)
            context.draw_line({0.0F, y}, {width, y}, branch, 1.0F, lib::StrokeStyle::Solid);

        if (prop_.etc.showTerminalMark && row.content->type() != types::CType::Start &&
            row.content->next().empty()) {
            // Dは次のコンテントを所有可能な葉に短い継続記
            // 号、所有不可の葉に塗りつぶした終端を置く。
            const float terminal_y = y + graphRowHeight();
            const lib::Point center{x + graphPx(8.0F), terminal_y};
            if (row.content->detail().owner) {
                context.draw_line({x + graphPx(8.0F), y + graphPx(9.0F)},
                                  {center.x, terminal_y - graphPx(8.0F)},
                                  branch, graphPx(3.0F), lib::StrokeStyle::Solid);
                context.draw_line({center.x, terminal_y - graphPx(6.0F)},
                                  {center.x, terminal_y - graphPx(2.0F)},
                                  branch, graphPx(3.0F), lib::StrokeStyle::Solid);
                context.draw_line({center.x, terminal_y},
                                  {center.x, terminal_y + graphPx(3.0F)},
                                  branch, graphPx(3.0F), lib::StrokeStyle::Solid);
            } else {
                context.draw_line({x + graphPx(8.0F), y + graphPx(9.0F)},
                                  {center.x, terminal_y - graphPx(5.0F)},
                                  branch, graphPx(3.0F), lib::StrokeStyle::Solid);
                context.fill_rect({center.x - graphPx(6.0F), terminal_y - graphPx(5.0F),
                                   graphPx(12.0F), graphPx(4.0F)}, branch);
            }
        }
    }

    // Dはコメントの重なり回避に、コメント行だけでなく全
    // 可視ツリー項目の矩形を使う。長い分岐式も含め実際に
    // 描く文字を計測し、アイコンと行高全体も確保する。
    std::vector<lib::Rect> occupied_boxes;
    occupied_boxes.reserve(graphRows_.size() * 2U);
    for (const auto& row : graphRows_) {
        const float y = row.y;
        const float slope = graphPx(slope_ ? static_cast<float>(slope_->value) : 16.0F);
        const float x = content_origin + static_cast<float>(row.depth1) * graphPx(16.0F) +
                        static_cast<float>(row.depth2) * slope;

        if (showLineNumber_) {
            const auto line = std::to_string(row.logical_line);
            const auto extent = context.measure_text(line, font, 0.0F);
            context.draw_text(
                line,
                {std::max(0.0F, graphPx(kEventGraphLineNumberWidth - 5.0F) - extent.width),
                 y + (graphRowHeight() - extent.height) * 0.5F},
                font, black);
        }

        const auto icon = contentImage(row.content->type());
        if (icon != 0) {
            context.draw_image(icon, {0.0F, 0.0F, 16.0F, 16.0F},
                               {x, y + graphPx(1.0F), graphPx(16.0F), graphPx(16.0F)},
                               lib::Interpolation::Nearest);
        } else {
            context.draw_rect({x + graphPx(2.0F), y + graphPx(3.0F),
                               graphPx(12.0F), graphPx(12.0F)}, black, 1.0F,
                              lib::StrokeStyle::Solid);
        }
        const auto item_text = eventText(row.content->parent(), row.content);
        const auto item_extent = context.measure_text(item_text, font, 0.0F);
        // Webを含むSDLの代替フォントは行寸法が異なる。文字の字面の中
        // 心を、y+1から16pxのコンテントアイコンの中心へ合わせる。
        const float icon_center_y = y + graphPx(9.0F);
        const float text_y = icon_center_y - item_extent.height * 0.5F -
                             lib::measured_effective_ui_text_ink_offset(font);
        // Dはネイティブ項目の全矩形を使う。独自描画の文字
        // はフォント・字面の調整で指定行より上へ伸び得る
        // ため、共通描画処理へ実際に渡す矩形を確保する。
        const float occupied_top = std::min(y, text_y);
        const float occupied_bottom = std::max(
            y + graphRowHeight(), text_y + item_extent.height);
        occupied_boxes.push_back(
            {x, occupied_top, graphPx(19.0F) + item_extent.width,
             occupied_bottom - occupied_top});
        // DのEventEditはインライン編集中に下の名前を隠す
        // 。短い名前を入力しても、旧名の末尾が見えないようにする。
        lib::Color item_color = black;
        const auto parent = row.content->parent();
        if (!lib::editor_window_host_options().all_mode &&
            row.content->name().empty() && parent &&
            parent->detail().nextType == types::CNextType::Text) {
            // Dの独自描画EventEditorは不透明な灰色のガイドを
            // 使う。選択行は濃くし、ホバー・通常行は標準の灰色にする。
            const auto gray = static_cast<std::uint8_t>(
                row.id == selectedContentId_ ? 96 : 128);
            item_color = {gray, gray, gray, 255};
        }
        if (!(row.id == selectedContentId_ && inlineNameEditActive()))
            context.draw_text(item_text, {x + graphPx(19.0F), text_y}, font, item_color);
        if (prop_.etc.showTargetStartLineNumber) {
            std::vector<std::size_t> linked_lines;
            const bool incoming = row.content->type() == types::CType::Start;
            if (incoming) {
                for (const auto& [candidate, number] : graphLogicalLines_)
                    if (candidate->detail().use(types::CArg::Start) &&
                        candidate->tree() == row.content->tree() &&
                        candidate->start() == row.content->name())
                        linked_lines.push_back(number);
            } else if (row.content->detail().use(types::CArg::Start)) {
                const auto tree = row.content->tree();
                const auto target = tree ? tree->start(row.content->start()) : ContentPtr{};
                const auto found = graphLogicalLines_.find(target.get());
                if (target && found != graphLogicalLines_.end()) linked_lines.push_back(found->second);
            }
            if (!linked_lines.empty()) {
                std::sort(linked_lines.begin(), linked_lines.end());
                const float unit = graphRowHeight() / 4.0F;
                float cursor = x + graphPx(19.0F) + item_extent.width +
                               (incoming ? unit : (item_text.empty() ? 0.0F : 2.0F * unit));
                const float center = y + graphRowHeight() / 2.0F;
                for (int arrow = 0; arrow < 2; ++arrow) {
                    const float start = cursor + static_cast<float>(arrow) * unit;
                    const float tip = incoming ? start : start + unit;
                    const float back = incoming ? start + unit : start;
                    const std::array points{lib::Point{back, center-unit},
                        lib::Point{tip, center}, lib::Point{back, center+unit}};
                    context.draw_line(points[0], points[1], black, graphPx(2.0F), lib::StrokeStyle::Solid);
                    context.draw_line(points[1], points[2], black, graphPx(2.0F), lib::StrokeStyle::Solid);
                }
                cursor += 3.0F * unit;
                for (std::size_t index = 0; index < linked_lines.size(); ++index) {
                    if (index) {
                        cursor += 2.0F * unit;
                        context.fill_ellipse({cursor-unit-graphPx(1.0F), center-graphPx(1.0F),
                                              graphPx(3.0F), graphPx(3.0F)}, black);
                    }
                    const auto line = std::to_string(linked_lines[index]);
                    context.draw_text(line, {cursor, text_y}, font, black);
                    cursor += context.measure_text(line, font, 0.0F).width;
                }
                // 注記も横スクロールとコメントの重なり回避の対象に含める。
                occupied_boxes.back().width = std::max(occupied_boxes.back().width, cursor-x);
            }
        }

        if (prop_.etc.drawContentWarnings && !row.warnings.empty()) {
            const auto bounds = warningBounds(y, detail_x);
            std::uint8_t warning_alpha = 255U;
            if (!lib::editor_window_host_options().all_mode) {
                const bool icon_hover = row.id == pointer_hover && graphHoverPointerPosition_ &&
                    pointInRect(bounds, {graphHoverPointerPosition_->x - canvas_bounds.x,
                                         graphHoverPointerPosition_->y - canvas_bounds.y});
                warning_alpha = icon_hover ? 255U : 128U;
            }
            if (row.warning_icon != 0U) {
                context.draw_image_with_alpha(row.warning_icon,
                                   {0.0F, 0.0F, kWarningIconSize, kWarningIconSize},
                                   bounds, lib::Interpolation::Nearest, warning_alpha);
            } else {
                const std::array points{
                    lib::Point{bounds.x + bounds.width * 0.5F, bounds.y},
                    lib::Point{bounds.x + bounds.width, bounds.y + bounds.height},
                    lib::Point{bounds.x, bounds.y + bounds.height}};
                const lib::Color warning_ink{0, 0, 0, warning_alpha};
                context.fill_polygon(points, lib::Color{255, 224, 0, warning_alpha});
                context.draw_polygon(points, warning_ink, 1.0F,
                                     lib::StrokeStyle::Solid);
                context.draw_text("!", {bounds.x + graphPx(6.0F), bounds.y + graphPx(2.0F)},
                                  font, warning_ink);
            }
        }

        if (prop_.etc.drawCountOfUseOfStart && row.content->type() == types::CType::Start) {
            if (lib::editor_window_host_options().all_mode) {
                const auto uses = "利用数 " + std::to_string(startUseCount(row.content));
                const auto extent = context.measure_text(uses, font, 0.0F);
                context.draw_text(uses, {detail_x - graphPx(65.0F),
                    y + (graphRowHeight() - extent.height) * 0.5F}, font, black);
            } else {
                static const msgs::Msgs fallbackMessages;
                const auto& messages = comm_.modelProps ? comm_.modelProps->requiredMsgs() : fallbackMessages;
                const auto label = messages.startUseCount.value;
                const auto count = std::to_string(startUseCount(row.content));
                const auto labelExtent = context.measure_text(label, font, 0.0F);
                const auto countExtent = context.measure_text(count, font, 0.0F);
                const float countX = detail_x - graphPx(4.0F) - countExtent.width;
                // DのEventEditorの前景色はRGB(96,96,96)
                // 。開始使用数のラベルはその不透明色、件数は黒を使う。DのAlpha192
                // は背景の塗りに適用し、ラベルには適用しない。
                context.draw_text(label, {countX - graphPx(4.0F) - labelExtent.width,
                    y + (graphRowHeight() - labelExtent.height) * 0.5F},
                    font, lib::Color{96, 96, 96, 255});
                context.draw_text(count, {countX,
                    y + (graphRowHeight() - countExtent.height) * 0.5F}, font, black);
            }
        }
        if (showEventTreeDetail_) {
            // DのEventEditorは、右の独立した詳細枠
            // に行自身のcontentText(c)を描く。
            const auto detail_icon = prop_.etc.drawContentIconOnDetail
                ? contentImage(row.content->type()) : 0U;
            if (detail_icon != 0)
                context.draw_image(detail_icon, {0.0F, 0.0F, 16.0F, 16.0F},
                                   {detail_x + graphPx(2.0F), y + graphPx(1.0F),
                                    graphPx(16.0F), graphPx(16.0F)},
                                   lib::Interpolation::Nearest);
            const auto detail = row.content->type() == types::CType::Start
                ? String{"スタートコンテント["} + row.content->name() + "]"
                : contentText(row.content);
            const auto detail_extent = context.measure_text(detail, font, 0.0F);
            context.draw_text(detail,
                              {detail_x + graphPx(detail_icon != 0U ? 20.0F : 4.0F),
                               y + (graphRowHeight() - detail_extent.height) * 0.5F},
                              font, black);
        }
    }

    float markerRight = 0.0F;
    // DのdrawCommentは、非表示の子を持つ折りたたみ行に印
    // を付ける。可視の図の行だけでなく、保持したノードから判定する。
    if (contents_) for (const auto& row : graphRows_) {
        const auto node = std::find_if(contents_->nodes.begin(), contents_->nodes.end(),
            [&](const auto& value) { return value.id == row.id; });
        if (node == contents_->nodes.end() || node->expanded ||
            !std::any_of(contents_->nodes.begin(), contents_->nodes.end(),
                [&](const auto& value) { return value.parent == row.id; })) continue;
        const float slope = graphPx(slope_ ? static_cast<float>(slope_->value) : 16.0F);
        const float x = content_origin + static_cast<float>(row.depth1) * graphPx(16.0F) +
                        static_cast<float>(row.depth2) * slope;
        const auto label = context.measure_text(eventText(row.content->parent(), row.content), font, 0.0F);
        const auto dots = context.measure_text("...", font, 0.0F);
        const float start = x + graphPx(21.0F) + label.width;
        const lib::Rect bounds{start + graphPx(10.0F), row.y,
                               dots.width + graphPx(10.0F), graphRowHeight()};
        auto marker = black;
        marker.alpha = 128;
        const float cy = bounds.y + bounds.height * 0.5F;
        context.draw_line({start, cy}, {bounds.x, cy}, marker,
                          graphPx(1.0F), lib::StrokeStyle::Solid);
        context.draw_text("...", {bounds.x + graphPx(5.0F),
            bounds.y + (bounds.height - dots.height) * 0.5F}, font, marker);
        // ネイティブの10pxの丸い輪郭を、角を切った形で近似する。
        const float radius = std::min(graphPx(5.0F), bounds.height * 0.5F);
        const float right = bounds.x + bounds.width, bottom = bounds.y + bounds.height;
        markerRight = std::max(markerRight, right);
        const std::array outline{lib::Point{bounds.x + radius, bounds.y},
            lib::Point{right - radius, bounds.y}, lib::Point{right, bounds.y + radius},
            lib::Point{right, bottom - radius}, lib::Point{right - radius, bottom},
            lib::Point{bounds.x + radius, bottom}, lib::Point{bounds.x, bottom - radius},
            lib::Point{bounds.x, bounds.y + radius}};
        context.draw_polygon(outline, marker, graphPx(1.0F), lib::StrokeStyle::Solid);
    }

    // DのEventTreeViewは、各コンテントの右
    // に独自描画のコメント吹き出しを置く。指定距離は項目
    // 文字の末尾から吹き出し文字までで、枠は独自の余白を
    // 加える。障害物を左から右へ調べ、右移動で確認済み項
    // 目へ重ならないようにする。字下げが減る場合や先行コ
    // メントが大きく右へ移った場合、行順だけでは不十分。
    std::ranges::sort(occupied_boxes, {}, &lib::Rect::x);
    struct CommentBox {
        lib::Rect bounds;
        lib::GcTextLayout text;
    };
    std::vector<CommentBox> comments;
    comments.reserve(graphRows_.size());
    // Dはネイティブ項目をツリー順に訪問するが、独自描画の字下げは途
    // 中で左へ戻り得る。X順に障害物を調べ、右移動で確認済みの深い行
    // へ入らないようにする。lower_boundは整列済み一覧を必
    // 要とするため、下記で先行吹き出しを加える際もこの順を維持する。
    std::stable_sort(occupied_boxes.begin(), occupied_boxes.end(),
        [](const lib::Rect& a, const lib::Rect& b) { return a.x < b.x; });
    const float margin_x = graphPx(5.0F);
    const float margin_y = graphPx(4.0F);
    const float first_y = graphRows_.empty() ? 0.0F : graphRows_.front().y;
    const float last_y = graphRows_.empty()
        ? height
        : graphRows_.back().y + graphRowHeight();
    for (const auto& row : graphRows_) {
        String comment = row.content ? row.content->comment() : String{};
        const bool raw_comment_present = !comment.empty();
        while (!comment.empty() &&
               (comment.back() == '\r' || comment.back() == '\n'))
            comment.pop_back();
        if (!raw_comment_present) continue;
        auto comment_text = lib::layout_gc_text(context, comment, font);

        // DはlastRet/chompの前に生コメントを検査す
        // るため、改行だけの取り込みコメントにも空の吹き出しを
        // 残す。SDLの空文字寸法は0なので、共通GCの文字配
        // 置で空文字を計測せず、一行分のフォント高を用意する。

        const float slope = graphPx(slope_ ? static_cast<float>(slope_->value) : 16.0F);
        const float x = content_origin + static_cast<float>(row.depth1) * graphPx(16.0F) +
                        static_cast<float>(row.depth2) * slope;
        const auto item_text = eventText(row.content->parent(), row.content);
        const auto item_extent = context.measure_text(item_text, font, 0.0F);
        const float item_right = x + graphPx(19.0F) + item_extent.width;
        const float text_x = item_right +
                             graphPx(static_cast<float>(prop_.etc.commentBoxDistance));
        float box_y = row.y +
                      (graphRowHeight() -
                       comment_text.extent.height) * 0.5F -
                      margin_y;
        const float box_height = comment_text.extent.height +
                                 margin_y * 2.0F;
        if (box_y < first_y) box_y = first_y;
        if (box_y + box_height > last_y)
            box_y = std::max(first_y, last_y - box_height);
        lib::Rect box{text_x - margin_x, box_y,
                      comment_text.extent.width + margin_x * 2.0F, box_height};
        for (const auto& occupied : occupied_boxes) {
            const bool intersects = box.x < occupied.x + occupied.width &&
                                    occupied.x < box.x + box.width &&
                                    box.y < occupied.y + occupied.height &&
                                    occupied.y < box.y + box.height;
            if (intersects) {
                box.x = occupied.x + occupied.width + margin_x;
            }
        }
        const auto position = std::lower_bound(
            occupied_boxes.begin(), occupied_boxes.end(), box.x,
            [](const lib::Rect& occupied, const float x) { return occupied.x < x; });
        occupied_boxes.insert(position, box);
        const float connector_start = item_right + graphPx(2.0F);
        const float connector_end = box.x;
        if (connector_end > connector_start)
            context.draw_line({connector_start, row.y + graphPx(8.0F)},
                              {connector_end, row.y + graphPx(8.0F)}, branch, 1.0F,
                              lib::StrokeStyle::Solid);
        comments.push_back({box, std::move(comment_text)});
    }

    // Dの独立したdrawComment処理と同様に、全接続線を終えてか
    // ら吹き出しを描く。後の線が先のコメント文字を横切らないようにする。
    for (const auto& comment : comments) {
        const auto box = comment.bounds;
        // DのdrawCommentは、コントロールから継承した背景色をアルファ128で使う。
        auto callout_background = eventGraphCanvas_
            ? eventGraphCanvas_->background().value_or(
                  lib::Color{255, 255, 255, 255})
            : lib::Color{255, 255, 255, 255};
        callout_background.alpha = 128U;
        context.fill_rect(box, callout_background);
        context.draw_rect(box, branch, 1.0F, lib::StrokeStyle::Solid);
        lib::draw_gc_text(context, comment.text, {box.x + margin_x, box.y + margin_y},
                          font, black);
    }

    // 仕様追加 (Web ALL のみ): コンテント配置ツールを選択中、マウスが
    // イベントビュー上にある間は、選択したコンテントのアイコンをカーソルの
    // 左に描く。D/スタンドアロンにはない表示で、ブラウザではカスタム
    // カーソルが使えないため、配置するコンテントをここで示す。
    if (toolCursorIconEnabled() && !contentToolArrow_ && toolCursorPosition_) {
        const auto icon = contentImage(contentToolType_);
        if (icon != 0) {
            const float iconSize = graphPx(16.0F);
            const float cursorGap = graphPx(4.0F);
            context.draw_image(icon, {0.0F, 0.0F, 16.0F, 16.0F},
                               {toolCursorPosition_->x - iconSize - cursorGap,
                                toolCursorPosition_->y - iconSize * 0.5F,
                                iconSize, iconSize},
                               lib::Interpolation::Nearest);
        }
    }
    // Dは実際の項目とコメントの矩形を横スクロール範囲に含め
    // る。階層の深さだけでは、最大までスクロールしても長いラ
    // ベル・コメントが切れる。独立した詳細枠は含めず、計測
    // がCanvas幅を自身へ戻して増幅しないようにする。
    float measuredRight = 0.0F;
    for (const auto& box : occupied_boxes)
        measuredRight = std::max(measuredRight, box.x + box.width);
    measuredRight = std::max(measuredRight, markerRight);
    graphMeasuredRight_ = measuredRight / graphScale();
    const auto canvas = eventGraphCanvas_;
    const auto scroll = eventGraphScroll_;
    const auto gui = comm_.services.gui;
    // 分割部の操作中はCanvasの座標を安定させる。移動
    // 中に幅を変えると、次のポインター差分へ新幅が影響する
    // 。解放時の再描画で、最終的な詳細幅の範囲を公開する。
    if (canvas && !eventGraphDetailResizeActive_) {
        auto layout = canvas->layout_item();
        const float minimum = std::max(520.0F, graphMeasuredRight_ + eventGraphDetailWidth_ + 12.0F);
        const float preferred = std::max(760.0F, minimum);
        if (std::abs(layout.minimum.width - minimum) > 0.5F ||
            std::abs(layout.preferred.width - preferred) > 0.5F) {
            layout.minimum.width = minimum;
            layout.preferred.width = preferred;
            canvas->set_layout_item(layout);
            // 外部への配置公開は元のEventWindowを終了させ
            // 得るため、その後は保持したローカルハンドルだけを使う。
            if (scroll) (void)lib::arrange_container(scroll, scroll->bounds());
            if (gui) gui->request_redraw(canvas);
        }
    }
}

bool EventView::toolCursorIconEnabled() noexcept {
    // DにはないALL専用の追加仕様。選択したコンテントツ
    // ールのアイコンをポインターの横に描く。Web ALL
    // とAndroid ALLで一つの動作を共有する。
    return lib::editor_window_host_options().all_mode;
}

float EventView::eventGraphDetailX(const float canvasWidth) const noexcept {
    const float minimumFrameWidth = graphPx(120.0F);
    const float width = std::max(graphPx(520.0F), canvasWidth);
    return std::clamp(width - graphPx(eventGraphDetailWidth_), minimumFrameWidth,
                      width - minimumFrameWidth);
}

void EventView::handleEventGraphPointer(lib::PointerEvent& event) {
    // DはMouseUp/DoubleClickの配送中、直前の
    // ホバーが設定したカーソルを維持する。共通経路が今回の操作用
    // にCanvasカーソルを更新する前に、その状態を取得する。
    const auto cursor_at_event_entry = eventGraphCanvas_
        ? std::optional<lib::CursorKind>{eventGraphCanvas_->cursor()}
        : std::nullopt;
    if (refreshGraphPreferences() || graphRowsScale_ != graphScale()) rebuildGraphRows();
    // 開いている名前・選択肢の編集部内の押下は、キャレット移動や選択ポップアッ
    // プの操作として編集部へ渡す。行を再選択したり編集を再開始したりしない。
    if (inlineNameEditActive() && event.action != lib::PointerAction::Move &&
        editBounds_) {
        if (const auto bounds = editBounds_();
            bounds && pointInRect(*bounds, event.position))
            return;
    }
    // PointerEventはウィンドウ相対座標、GraphRow::hit
    // はCanvasの内容座標。ScrollViewは現在の位置だ
    // けCanvas矩形を動かすため、配置済みCanvasの原点
    // による一度の変換で、通常・スクロール後のクリックを扱える。
    const auto canvasBounds = eventGraphCanvas_ ? eventGraphCanvas_->bounds()
                                                : lib::Rect{};
    const auto graphPosition = lib::Point{event.position.x - canvasBounds.x,
                                          event.position.y - canvasBounds.y};
    const float canvasWidth = std::max(graphPx(520.0F), canvasBounds.width);
    const float detailX = eventGraphDetailX(canvasWidth);
    const float sashHitWidth = graphPx(10.0F);

    // 仕様追加 (Web ALL のみ): カーソル左のコンテントアイコン用に、
    // イベントビュー上のポインタ位置を記録する。離れたら消す。
    if (toolCursorIconEnabled()) {
        const bool inside = event.action != lib::PointerAction::Leave &&
            event.position.x >= canvasBounds.x &&
            event.position.y >= canvasBounds.y &&
            event.position.x < canvasBounds.x + canvasBounds.width &&
            event.position.y < canvasBounds.y + canvasBounds.height;
        const auto next = inside ? std::optional<lib::Point>{graphPosition}
                                 : std::nullopt;
        const bool changed = next.has_value() != toolCursorPosition_.has_value() ||
            (next && (next->x != toolCursorPosition_->x ||
                      next->y != toolCursorPosition_->y));
        toolCursorPosition_ = next;
        if (changed && !contentToolArrow_) invalidateEventGraph();
    }

    const bool pointerInsideCanvas = graphPosition.x >= 0.0F &&
        graphPosition.y >= 0.0F && graphPosition.x < canvasBounds.width &&
        graphPosition.y < canvasBounds.height;
    const bool onDetailSash = graphPosition.x >= detailX - sashHitWidth &&
        graphPosition.x < detailX + sashHitWidth;
    if (eventGraphCanvas_ && event.action == lib::PointerAction::Move) {
        String tooltip;
        // DのupdateToolTipは、分割カーソルが有効、詳細幅をド
        // ラッグ中、クライアント領域外のいずれかならホバー文字列を消す。
        if (pointerInsideCanvas && !onDetailSash &&
            !eventGraphDetailResizeActive_ && prop_.etc.drawContentWarnings) {
            for (const auto& row : graphRows_) {
                if (!row.warnings.empty() &&
                    pointInRect(warningBounds(row.y, detailX), graphPosition)) {
                    tooltip = warningText(row.warnings);
                    break;
                }
            }
        }
        if (tooltip.empty() && pointerInsideCanvas && !onDetailSash &&
            !eventGraphDetailResizeActive_ && showEventTreeDetail_ &&
            graphPosition.x >= detailX && comm_.services.graphics) {
            const auto row = std::find_if(graphRows_.begin(), graphRows_.end(),
                [graphPosition](const GraphRow& value) {
                    return graphPosition.y >= value.hit.y &&
                           graphPosition.y < value.hit.y + value.hit.height;
                });
            if (row != graphRows_.end() && row->content) {
                const auto detail = row->content->type() == types::CType::Start
                    ? String{"スタートコンテント["} + row->content->name() + "]"
                    : contentText(row->content);
                const auto width = std::max(0.0F,
                    graphPx(eventGraphDetailWidth_ - 20.0F));
                if (comm_.services.graphics->measure_text(
                        detail, lib::effective_editor_widget_font(), 0.0F).width > width)
                    tooltip = detail;
            }
        }
        eventGraphCanvas_->set_tooltip(std::move(tooltip));
        eventGraphCanvas_->set_tooltip_balloon(true);
        eventGraphCanvas_->set_tooltip_immediate(true);
    }

    if (!eventGraphDetailResizeActive_ && eventGraphCanvas_) {
        const bool onSash = event.action != lib::PointerAction::Leave &&
                            graphPosition.x >= detailX - sashHitWidth &&
                            graphPosition.x < detailX + sashHitWidth;
        bool onEditableContentIcon = false;
        const bool icon_edit_cursor_mode = contentToolArrow_ ||
            (!lib::editor_window_host_options().all_mode &&
             contentPutMode_ == PutMode::Quick);
        if (event.action == lib::PointerAction::Move && !readOnly_ &&
            icon_edit_cursor_mode) {
            const std::weak_ptr<const int> life = paletteLifetime_.token;
            const auto* editor_props = comm_.editorProps ? comm_.editorProps() : nullptr;
            if (life.expired()) return;
            if (editor_props && editor_props->var().clickIconIsStartEdit.value) {
                const auto row = std::find_if(graphRows_.begin(), graphRows_.end(),
                    [graphPosition](const GraphRow& value) {
                        return graphPosition.y >= value.hit.y &&
                               graphPosition.y < value.hit.y + value.hit.height;
                    });
                if (row != graphRows_.end() && row->content) {
                    const float originX = showLineNumber_
                        ? graphPx(kEventGraphLineNumberWidth) : 0.0F;
                    const float slopeValue = graphPx(
                        slope_ ? static_cast<float>(slope_->value) : 16.0F);
                    const float iconX = originX +
                        static_cast<float>(row->depth1) * graphPx(16.0F) +
                        static_cast<float>(row->depth2) * slopeValue;
                    const bool iconHit = graphPosition.x >= iconX &&
                        graphPosition.x < iconX + graphPx(16.0F) &&
                        graphPosition.y >= row->y + graphPx(1.0F) &&
                        graphPosition.y < row->y + graphPx(17.0F);
                    const auto summary = comm_.summary.lock();
                    onEditableContentIcon = iconHit &&
                        contentinitializer::hasDialog(row->content->type(),
                                                      bool(summary)) &&
                        contentDialogResourcesAvailable(row->content->type(),
                            summary, row->content->tree());
                }
            }
        }
        // eventeditor.dに合わせ、詳細欄の境界線上ではSIZEWE、それ以外は保存カーソルを使う。
        if (onSash) eventGraphCanvas_->set_cursor(lib::CursorKind::SizeHorizontal);
        else if (onEditableContentIcon)
            eventGraphCanvas_->set_cursor(lib::CursorKind::Hand);
        else applyContentToolCursor(*eventGraphCanvas_);
    }

    // Dのイベントツリーと独立した説明枠の間は分割境
    // 界。行選択・コンテント配置より先にこの操作を扱
    // い、境界のドラッグで下のツリーを誤編集しない。
    if (eventGraphDetailResizeActive_ &&
        event.pointer_id == eventGraphDetailResizePointer_) {
        if (event.action == lib::PointerAction::Move ||
            event.action == lib::PointerAction::Up) {
            const float minimumFrameWidth = graphPx(120.0F);
            const float movedX = std::clamp(graphPosition.x, minimumFrameWidth,
                                            canvasWidth - minimumFrameWidth);
            eventGraphDetailWidth_ = (canvasWidth - movedX) / graphScale();
            if (prop_.etc.storeEventGraphDetailWidth)
                prop_.etc.storeEventGraphDetailWidth(
                    static_cast<std::int32_t>(std::lround(eventGraphDetailWidth_)));
            invalidateEventGraph();
        }
        if (event.action == lib::PointerAction::Up ||
            event.action == lib::PointerAction::Cancel) {
            eventGraphDetailResizeActive_ = false;
            if (comm_.services.gui)
                comm_.services.gui->capture_pointer(eventGraphCanvas_, false);
        }
        event.handled = true;
        return;
    }
    if (event.button == lib::PointerButton::Primary &&
        event.action == lib::PointerAction::Down &&
        graphPosition.x >= detailX - sashHitWidth &&
        graphPosition.x < detailX + sashHitWidth) {
        pendingClickPointer_ = 0;
        pendingClickContentId_ = 0;
        pendingClickMoved_ = false;
        pendingClickSelectionCommitted_ = false;
        eventGraphDetailResizeActive_ = true;
        eventGraphDetailResizePointer_ = event.pointer_id;
        if (comm_.services.gui)
            comm_.services.gui->capture_pointer(eventGraphCanvas_, true);
        event.handled = true;
        return;
    }
    const auto hitRow = [this, graphPosition] {
        return std::find_if(graphRows_.begin(), graphRows_.end(),
            [graphPosition](const GraphRow& row) {
                return graphPosition.y >= row.hit.y &&
                       graphPosition.y < row.hit.y + row.hit.height;
            });
    };
    const auto selectFromPointer = [this](const TreeItemId id) {
        // SelectionChangedは包含フレームを同期
        // 更新し得る。選択コンテントの再公開中もユーザーの表
        // 示位置を維持する。Dはマウス選択で先頭へ戻らない。
        const auto offset = eventGraphScroll_
                                ? std::optional<lib::Point>{eventGraphScroll_->scroll_offset}
                                : std::nullopt;
        const auto canvas_bounds = eventGraphCanvas_
                                       ? std::optional<lib::Rect>{eventGraphCanvas_->bounds()}
                                       : std::nullopt;
        const bool selected = selectContent(id);
        if (selected && offset && eventGraphScroll_) {
            (void)lib::scroll_view_content(eventGraphScroll_, *offset);
            // ネイティブの表示領域ができる前に、同期の選択監視
            // 通知が再配置する場合がある。モデルのスクロール量
            // も維持し、次の配置で実際の寸法に対して制限する。
            eventGraphScroll_->scroll_offset = *offset;
        }
        if (selected && canvas_bounds && eventGraphCanvas_)
            eventGraphCanvas_->set_bounds(*canvas_bounds);
        return selected;
    };
    if (event.button == lib::PointerButton::Secondary) {
        if (event.action == lib::PointerAction::Down) {
            // DのEventEditor.onMouseDownは、全ホストで主・副ボタンのマ
            // ウス・ペン押下時に選択する。タッチは移動でスクロールできるよう、選択を遅らせる。
            const bool mouse_or_pen_down =
                event.pointer_type != lib::PointerType::Touch;
            if (mouse_or_pen_down) {
                const auto found = hitRow();
                if (comm_.services.gui && eventGraphCanvas_)
                    comm_.services.gui->focus(eventGraphCanvas_);
                if (found != graphRows_.end() && selectFromPointer(found->id))
                    invalidateEventGraph();
                event.handled = true;
            }
            return;
        }
        pendingClickPointer_ = 0;
        pendingClickContentId_ = 0;
        pendingClickMoved_ = false;
        pendingClickSelectionCommitted_ = false;
        if (event.action != lib::PointerAction::Up) return;

        const auto found = hitRow();
        // 右側のメニューはコンテント用。背景クリックで選択イベン
        // トへ切り替えず、最後の実際のコンテント選択を維持する。
        // 今回の操作で行に当たった場合は、必ずその行を選び直す。
        if (found != graphRows_.end() && selectFromPointer(found->id))
            invalidateEventGraph();

        // Dと同様に右クリックは、先に有効な配置
        // ツールを取り消し、その後コンテキストメ
        // ニューを開く。ここで戻ると、ツール選択
        // 中の最初の右クリックが無効に見える。
        if (!contentToolArrow_) {
            contentToolArrow_ = true;
            // DのCreateL.mouseUpは、モードを変えずに矢印へ切り替える。
            publishContentToolState();
            refreshToolbarState();
        }
        showEventContextMenu(eventGraphCanvas_, event.position);
        event.handled = true;
        return;
    }
    const bool middle = event.button == lib::PointerButton::Middle;
    if (event.button != lib::PointerButton::Primary && !middle) return;
    if (middle && event.action != lib::PointerAction::Up) return;
    if (!middle) {
        // DのEventEditor.onMouseDownは両ホストでマウス・ペン
        // の選択を確定する。タッチは未処理のMoveでスクロールできるよう遅らせる。
        if (event.action == lib::PointerAction::Down) {
            const auto found = hitRow();
            if (comm_.services.gui && eventGraphCanvas_)
                comm_.services.gui->focus(eventGraphCanvas_);
            pendingClickPointer_ = event.pointer_id;
            pendingClickContentId_ = found == graphRows_.end() ? 0 : found->id;
            pendingClickWasSelected_ = found != graphRows_.end() &&
                                       found->id == selectedContentId_ &&
                                       selectedContent_ != nullptr;
            pendingClickSelectionCommitted_ = false;
            cancelPendingNameEdit();
            pendingClickStart_ = event.position;
            pendingClickMoved_ = false;

            const bool mouse_or_pen_down =
                event.pointer_type != lib::PointerType::Touch;
            if (mouse_or_pen_down && found != graphRows_.end()) {
                const auto id = found->id;
                (void)selectFromPointer(id);
                pendingClickSelectionCommitted_ =
                    selectedContentId_ == id && selectedContent_ != nullptr;
            }
            // 選択は上で確定済み。共通のクリック分類にも押下を渡し、二回目
            // のクリックでDのmouseDoubleClickを配送できる
            // ようにする。DではonMouseDownがdoit=false
            // でもSWTはMouseDoubleClickを通知する。
            return;
        }
        if (event.action == lib::PointerAction::DoubleClick) {
            // DのmouseDoubleClickに合わせ、ダブルクリックで名前編集を始めない。
            cancelPendingNameEdit();
            // 完了した解放処理で行は確定済み。合成したダブル
            // クリックは、下記のDの編集・配置処理へ渡す。
            pendingClickPointer_ = 0;
            pendingClickContentId_ = 0;
            pendingClickMoved_ = false;
            pendingClickSelectionCommitted_ = false;
        } else if (event.pointer_id != pendingClickPointer_) {
            return;
        }
        if (event.action == lib::PointerAction::Move) {
            if (event.pointer_type == lib::PointerType::Touch) {
                const auto dx = event.position.x - pendingClickStart_.x;
                const auto dy = event.position.y - pendingClickStart_.y;
                const auto threshold = lib::scaled_ui_metric(8.0F);
                pendingClickMoved_ = pendingClickMoved_ ||
                    dx * dx + dy * dy >= threshold * threshold;
            }
            return;
        }
        if (event.action == lib::PointerAction::Cancel) {
            pendingClickPointer_ = 0;
            pendingClickContentId_ = 0;
            pendingClickMoved_ = false;
            pendingClickSelectionCommitted_ = false;
            return;
        }
        if (event.action == lib::PointerAction::Up) {
            const auto id = pendingClickContentId_;
            const bool moved = pendingClickMoved_;
            const bool selectionCommitted = pendingClickSelectionCommitted_;
            pendingClickPointer_ = 0;
            pendingClickContentId_ = 0;
            pendingClickMoved_ = false;
            pendingClickSelectionCommitted_ = false;
            const bool wasSelected = pendingClickWasSelected_;
            pendingClickWasSelected_ = false;
            const auto found = hitRow();
            // 実際の押下・解放位置からクリックを解決し、アイコンクリッ
            // クの認識に事前のホバーを必須としない。Dの別の_clickStart
            // キャッシュはMoveまで空の場合がある。またDはホバ
            // ーがBへ移った後も選択中のAを開き得るため、同じ行
            // であることを要求して誤った行のダイアログを避ける。
            if (moved || id == 0 || found == graphRows_.end() || found->id != id)
                return;
            if (!selectionCommitted && !selectFromPointer(id)) return;
            if (!selectionCommitted) invalidateEventGraph();
            // DのTextEditMFListener.mouseUpと
            // EventEdit.selectionMに合わせ、選択済み
            // 行の名前（20pxアイコンの右、詳細枠の左）をクリックする
            // とその場編集を始める。Slowはダブルクリック時間だけ待
            // ち、ダブルクリックではコンテント画面を開けるようにする。
            const float originX = showLineNumber_ ? graphPx(kEventGraphLineNumberWidth) : 0.0F;
            const float slopeValue = graphPx(slope_ ? static_cast<float>(slope_->value) : 16.0F);
            const float nameX = originX + static_cast<float>(found->depth1) * graphPx(16.0F) +
                                static_cast<float>(found->depth2) * slopeValue + graphPx(20.0F);
            const std::weak_ptr<const int> click_life = paletteLifetime_.token;
            const auto* editor_props = comm_.editorProps ? comm_.editorProps() : nullptr;
            if (click_life.expired()) return;
            const auto summary = comm_.summary.lock();
            // DはQuickモードでもアイコン編集を許可する。16pxの画像範囲に4pxの余白は含めない。
            if ((contentToolArrow_ || (!lib::editor_window_host_options().all_mode && contentPutMode_ == PutMode::Quick)) && !readOnly_ && editor_props &&
                editor_props->var().clickIconIsStartEdit.value &&
                graphPosition.x >= nameX - graphPx(20.0F) &&
                graphPosition.x < nameX - graphPx(4.0F) &&
                graphPosition.y >= found->y + graphPx(1.0F) &&
                graphPosition.y < found->y + graphPx(17.0F) &&
                selectedContent_ && contentinitializer::hasDialog(selectedContent_->type(), bool(summary)) &&
                contentDialogResourcesAvailable(selectedContent_->type(), summary, selectedContent_->tree())) {
                event.handled = true;
                edit();
                return;
            }
            if (wasSelected && contentToolArrow_ && !readOnly_ &&
                graphPosition.x >= nameX && graphPosition.x < detailX &&
                canEditSelection()) {
                if (prop_.etc.editTriggerQuick || !comm_.services.dispatcher) {
                    (void)editSelection();
                } else {
                    const auto request = ++nameEditRequest_;
                    const std::weak_ptr<bool> alive = nameEditLifetime_;
                    nameEditTimer_ = comm_.services.dispatcher->post_delayed(
                        std::chrono::milliseconds(500),
                        [this, alive, request, id] {
                            if (alive.expired() || request != nameEditRequest_) return;
                            nameEditTimer_.reset();
                            if (selectedContentId_ != id) return;
                            (void)editSelection();
                        });
                }
            }
            if (!contentToolArrow_ && contentPutMode_ != PutMode::Quick) {
                const bool insertBefore =
                    (static_cast<std::uint32_t>(event.modifiers) &
                     static_cast<std::uint32_t>(lib::Modifiers::Shift)) != 0U;
                event.handled = placeSelectedContentToolWithOptions(
                    insertBefore, contentInsertFirst_ != hasControl(event.modifiers, event.physical_control),
                    contentAutoEdit_ != hasAlt(event.modifiers));
            }
            return;
        }
    }
    // 主ボタンのDown/Move/Up/Cancelは、上記の一つの
    // 解放時確定経路で処理済み。その合成ダブルクリックだけを続行する。
    if (!middle && event.action != lib::PointerAction::DoubleClick) return;
    const auto found = hitRow();
    if (found == graphRows_.end()) return;
    // DのEditL.mouseDoubleClickは_tree.control.getCursor()
    // で判定する。直線表示のEventEditor自身のホバー上
    // 書きは、編集可能アイコンのHandと詳細分割のSIZEWE
    // 。選択コンテント画像のカーソルは親EventTreeView
    // に設定するため、C側のCustomツールカーソルを子の
    // 判定へ含めない。上記の共通更新は合成したMove以外の
    // イベントでもカーソルを戻すので、入口の保存状態を使う。
    if (event.action == lib::PointerAction::DoubleClick &&
        event.button == lib::PointerButton::Primary && cursor_at_event_entry &&
        (*cursor_at_event_entry == lib::CursorKind::Hand ||
         *cursor_at_event_entry == lib::CursorKind::SizeHorizontal))
        return;
    // CreateLは、実際に所有元として包む操作を行う中央クリッ
    // クだけでSWTのツリー選択を変える。selectContent()
    // 前に判定し、無効・所有不可のツールで選択を変えない。
    if (middle) {
        if (contentToolArrow_ || contentPutMode_ == PutMode::Quick ||
            !found->content || !found->content->parent() ||
            !event::contentDetail(contentToolType_).owner)
            return;
    }
    if (!selectFromPointer(found->id)) return;
    invalidateEventGraph();
    // DのCreateLは、ルート以外の既存行への中央クリック
    // を挿入とし、所有可能なツールでその行を包む。Quick
    // には同等操作を設けず、Startも包む側には使えない。
    if (middle) {
        event.handled = placeSelectedContentToolWithOptions(
            true, contentInsertFirst_ != hasControl(event.modifiers, event.physical_control),
            contentAutoEdit_ != hasAlt(event.modifiers));
        return;
    }
    // DはダブルクリックでStartの展開を切り替え、他のコンテントはEditL
    // を使う。挿入ツールを優先すると、ネイティブのダブルクリック配送
    // 中にツリーを変え、古い行のダイアログを開こうとして停止し得る。
    if (event.action == lib::PointerAction::DoubleClick) {
        // DのEditLは通常の矢印カーソル時だけ開く。Select/Continue
        // のカーソル選択中は配置操作であり、ここで画面
        // を開くと後のmouseUpの動作と食い違う。
        event.handled = true;
        // Quick配置はパレット操作で直ちに実行し、共通の
        // 矢印フラグが既定のfalseでも、図上の配置カーソ
        // ルは準備しない。既存コンテントは編集可能のまま。
        if (selectedContent_ && selectedContent_->type() == types::CType::Start &&
            !selectedContent_->next().empty() && contents_) {
            const auto found = std::find_if(contents_->nodes.begin(), contents_->nodes.end(),
                [this](const lib::TreeNode& node) { return node.id == selectedContentId_; });
            if (found != contents_->nodes.end()) {
                const std::weak_ptr<const int> life = paletteLifetime_.token;
                found->expanded = !found->expanded;
                rebuildContentTable();
                if (life.expired()) return;
                invalidateEventGraph();
                if (life.expired()) return;
                // DのEventTreeViewは展開・折りたたみの通知ごとに
                // ホストのツールバーを更新する。組み込んだコマンドを同期する。
                refreshToolbarState();
                if (life.expired()) return;
                const auto refresh = comm_.refreshToolBar;
                if (refresh) refresh();
            }
            return;
        }
        if (contentToolArrow_ || contentPutMode_ == PutMode::Quick) edit();
    }
}

void EventView::showEventContextMenu(const lib::WidgetPtr& owner,
                                     const lib::Point position) {
    if (!comm_.services.gui || !owner) return;
    auto menu = std::make_shared<lib::Menu>(true);
    const auto add = [&menu](String text, String command, const bool enabled,
                             lib::Action action) {
            lib::MenuItem item;
            item.text = std::move(text);
            item.command = std::move(command);
            item.enabled = enabled;
            item.action = std::move(action);
            menu->items.push_back(std::move(item));
        };
        const auto separator = [&menu] {
            lib::MenuItem item;
            item.separator = true;
            menu->items.push_back(std::move(item));
        };

        const bool left_tree = owner == cards_;
        if (left_tree) {
            add("新規イベント(&N)", "create-event", canCreateEvent(),
                [this] { createEvent(); });
            separator();
        }
        add("編集(&E)", "edit", canEdit(), [this] { edit(); });
        add("コメント(&M)", "comment", canWriteComment(), [this] {
            if (writeCommentAction_) writeCommentAction_();
        });
        separator();
        add("元に戻す(&U)", "undo", !readOnly_ && canUndo(), [this] { undo(); });
        add("やり直し(&R)", "redo", !readOnly_ && canRedo(), [this] { redo(); });
        separator();
        add("切り取り(&T)", "cut", canDoT(), [this] { (void)cut(); });
        add("コピー(&C)", "copy", canDoC(), [this] { (void)copy(); });
        add("貼り付け(&P)", "paste", canDoP(), [this] { (void)paste(); });
        if (const auto paste_item = std::find_if(menu->items.begin(), menu->items.end(),
                [](const lib::MenuItem& item) { return item.command == "paste"; });
            paste_item != menu->items.end()) {
            const std::weak_ptr<const int> paste_life = paletteLifetime_.token;
            paste_item->action_with_gesture = [this, paste_life](
                                                    const lib::CommandGesture gesture) {
                if (paste_life.expired()) return;
                // DのisInsertFirstは、ポインターで開いたメニューでは物理
                // Ctrlを読むが、メニューコマンドの実行に使ったCtrlは無視する。
                const bool menu_control = gesture.button != lib::PointerButton::None &&
                    hasControl(gesture);
                (void)pasteWithInsertFirst(contentInsertFirst_ != menu_control);
            };
        }
        add("削除(&D)", "delete", canDoD(), [this] {
            if (rightPaneActive() && selectedContent_) {
                (void)deleteContent();
            } else if (const auto fire = selectionFire()) {
                (void)removeFire(*fire);
            } else {
                (void)deleteEvent();
            }
        });
        add("複製", "clone", canDoClone(), [this] { (void)clone(); });
        if (left_tree) {
            if (const auto fire = selectionFire();
                fire && fire->kind == FireKind::KeyCode) {
                separator();
                lib::MenuItem timing;
                timing.text = "キーコード発火タイミング";
                timing.command = "keycode-timing";
                const auto append_timing = [this, &timing](String text,
                                                            String command,
                                                            const types::FKCKind kind) {
                    lib::MenuItem item;
                    item.text = std::move(text);
                    item.command = std::move(command);
                    item.enabled = canSetKeyCodeTiming(kind);
                    item.checked = selectionFire() &&
                                   selectionFire()->keyCode.kind == kind;
                    item.radio = true;
                    item.action = [this, kind] { (void)setKeyCodeTiming(kind); };
                    timing.children.push_back(std::move(item));
                };
                append_timing("使用", "keycode-use", types::FKCKind::Use);
                append_timing("成功", "keycode-success", types::FKCKind::Success);
                append_timing("失敗", "keycode-failure", types::FKCKind::Failure);
                append_timing("未所持", "keycode-hasnot", types::FKCKind::HasNot);
                timing.enabled = std::any_of(timing.children.begin(), timing.children.end(),
                    [](const auto& item) { return item.enabled; });
                menu->items.push_back(std::move(timing));

                lib::MenuItem copy_timing;
                copy_timing.text = "変換したキーコードをコピー";
                copy_timing.command = "keycode-copy-converted";
                const auto append_copy_timing = [this, &copy_timing](
                                                         String text,
                                                         String command,
                                                         const types::FKCKind kind) {
                    lib::MenuItem item;
                    item.text = std::move(text);
                    item.command = std::move(command);
                    item.enabled = static_cast<bool>(comm_.convertFireKeyCode);
                    item.action = [this, kind] {
                        (void)copyConvertedKeyCode(kind);
                    };
                    copy_timing.children.push_back(std::move(item));
                };
                append_copy_timing("使用", "keycode-copy-use", types::FKCKind::Use);
                append_copy_timing("成功", "keycode-copy-success", types::FKCKind::Success);
                append_copy_timing("失敗", "keycode-copy-failure", types::FKCKind::Failure);
                append_copy_timing("未所持", "keycode-copy-hasnot", types::FKCKind::HasNot);
                copy_timing.enabled = static_cast<bool>(comm_.convertFireKeyCode);
                menu->items.push_back(std::move(copy_timing));
            }
            if (selectionEventTree()) {
                lib::MenuItem condition;
                condition.text = "キーコード発火条件";
                condition.command = "keycode-condition";
                const auto append_condition = [this, &condition](String text,
                                                                  String command,
                                                                  const types::MatchingType type) {
                    lib::MenuItem item;
                    item.text = std::move(text);
                    item.command = std::move(command);
                    const auto tree = selectionEventTree();
                    item.enabled = canSetKeyCodeMatchingType(type);
                    item.checked = tree && tree->keyCodeMatchingType() == type;
                    item.radio = true;
                    item.action = [this, type] {
                        (void)setKeyCodeMatchingType(type);
                    };
                    condition.children.push_back(std::move(item));
                };
                append_condition("いずれかに一致 (OR)", "keycode-or",
                                 types::MatchingType::Or);
                append_condition("すべてに一致 (AND)", "keycode-and",
                                 types::MatchingType::And);
                menu->items.push_back(std::move(condition));
            }
        }
        separator();
        add("スクリプトとしてコピー", "to-script",
            static_cast<bool>(comm_.toScript) &&
                static_cast<bool>(canonicalSelectedContent()),
            [this] {
                if (comm_.toScript) comm_.toScript();
            });
        if (!left_tree) {
            add("1コンテントをスクリプトとしてコピー", "to-script-one",
                static_cast<bool>(comm_.toScriptOne) &&
                    static_cast<bool>(canonicalSelectedContent()),
                [this] {
                    if (comm_.toScriptOne) comm_.toScriptOne();
                });
        }
        add("イベント全体をスクリプトとしてコピー", "to-script-all",
            static_cast<bool>(comm_.toScriptAll) &&
                static_cast<bool>(selectedTree_),
            [this] {
                if (comm_.toScriptAll) comm_.toScriptAll();
            });
        if (left_tree) {
            add("イベントをパッケージへ変換", "event-to-package",
                comm_.canEventToPackage && comm_.canEventToPackage(),
                [this] {
                    if (comm_.eventToPackage) comm_.eventToPackage();
                });
            add("ラウンドを範囲追加", "add-many-rounds",
                comm_.canAddManyRounds && comm_.canAddManyRounds(),
                [this] {
                    if (comm_.addManyRounds) comm_.addManyRounds();
                });
        } else {
            add("スタートコンテントをパッケージへ変換", "start-to-package",
                comm_.canStartToPackage && comm_.canStartToPackage(),
                [this] {
                    if (comm_.startToPackage) comm_.startToPackage();
                });
        }
        separator();
        add("1コンテントを切り取り", "cut-one", canCut1Content(),
            [this] { (void)cut1Content(); });
        add("1コンテントをコピー", "copy-one", canCopy1Content(),
            [this] { (void)copy1Content(); });
        add("挿入して貼り付け", "paste-insert", canPasteInsert(),
            [this] { (void)pasteInsert(); });
        if (const auto paste_insert_item = std::find_if(
                menu->items.begin(), menu->items.end(),
                [](const lib::MenuItem& item) {
                    return item.command == "paste-insert";
                });
            paste_insert_item != menu->items.end()) {
            const std::weak_ptr<const int> paste_insert_life = paletteLifetime_.token;
            paste_insert_item->action_with_gesture = [this, paste_insert_life](
                    const lib::CommandGesture gesture) {
                if (paste_insert_life.expired()) return;
                // DはisInsertFirstでCtrlを読み、設定されたキーだけを除外する。
                (void)pasteInsert(gesture);
            };
        }
        add("1コンテントを削除", "delete-one", canDel1Content(),
            [this] { (void)del1Content(); });
        separator();
        add("親コンテントと入れ替え", "swap-parent", canSwapToParent(),
            [this] { (void)swapToParent(); });
        add("子コンテントと入れ替え", "swap-child", canSwapToChild(),
            [this] { (void)swapToChild(); });
        if (!left_tree) {
            add("選択肢を編集(&S)", "edit-selection", canEditSelection(),
                [this] { (void)editSelection(); });
            separator();
            const auto selected_node = [this]() -> lib::TreeNode* {
                if (!contents_ || selectedContentId_ == 0) return nullptr;
                const auto found = std::find_if(
                    contents_->nodes.begin(), contents_->nodes.end(),
                    [this](const lib::TreeNode& node) {
                        return node.id == selectedContentId_;
                    });
                return found == contents_->nodes.end() ? nullptr : &*found;
            };
            const auto* node = selected_node();
            // DのeditEnterを先に行ってから切り替える順序に合わせる。表示側の確定
            // がこの表示を終了させ得るため、C++のコールバックはIDから行を再解決する。
            add("展開", "expand", node && node->has_lazy_children && !node->expanded,
                [this] {
                    if (!contents_) return;
                    const auto selected = std::find_if(
                        contents_->nodes.begin(), contents_->nodes.end(),
                        [this](const lib::TreeNode& value) {
                            return value.id == selectedContentId_;
                        });
                    if (selected == contents_->nodes.end() ||
                        !selected->has_lazy_children || selected->expanded) return;
                    const std::weak_ptr<const int> life = paletteLifetime_.token;
                    const TreeItemId id = selectedContentId_;
                    editEnter();
                    if (life.expired() || !contents_) return;
                    const auto found = std::find_if(
                        contents_->nodes.begin(), contents_->nodes.end(),
                        [id](const lib::TreeNode& value) { return value.id == id; });
                    if (found == contents_->nodes.end() ||
                        !found->has_lazy_children || found->expanded) return;
                    found->expanded = true;
                    rebuildContentTable();
                    if (life.expired()) return;
                    invalidateEventGraph();
                    if (life.expired()) return;
                    refreshToolbarState();
                    if (life.expired()) return;
                    if (comm_.refreshToolBar) comm_.refreshToolBar();
                });
            add("折りたたむ", "collapse", node && node->has_lazy_children && node->expanded,
                [this] {
                    if (!contents_) return;
                    const auto selected = std::find_if(
                        contents_->nodes.begin(), contents_->nodes.end(),
                        [this](const lib::TreeNode& value) {
                            return value.id == selectedContentId_;
                        });
                    if (selected == contents_->nodes.end() ||
                        !selected->has_lazy_children || !selected->expanded) return;
                    const std::weak_ptr<const int> life = paletteLifetime_.token;
                    const TreeItemId id = selectedContentId_;
                    editEnter();
                    if (life.expired() || !contents_) return;
                    const auto found = std::find_if(
                        contents_->nodes.begin(), contents_->nodes.end(),
                        [id](const lib::TreeNode& value) { return value.id == id; });
                    if (found == contents_->nodes.end() ||
                        !found->has_lazy_children || !found->expanded) return;
                    found->expanded = false;
                    rebuildContentTable();
                    if (life.expired()) return;
                    invalidateEventGraph();
                    if (life.expired()) return;
                    refreshToolbarState();
                    if (life.expired()) return;
                    if (comm_.refreshToolBar) comm_.refreshToolBar();
                });

            lib::MenuItem other_item;
            other_item.text = "その他";
            other_item.command = "other";
            other_item.enabled = canCopy1Content();
            lib::MenuItem copy_xml;
            copy_xml.text = "単体コンテントをXMLコピー";
            copy_xml.command = "copy-xml-one";
            copy_xml.enabled = canCopy1Content();
            copy_xml.action = [this] { (void)copySelectionXML(true); };
            other_item.children.push_back(std::move(copy_xml));
            lib::MenuItem copy_xml_children;
            copy_xml_children.text = "子コンテントを含めてXMLコピー";
            copy_xml_children.command = "copy-xml-tree";
            copy_xml_children.enabled = canCopy1Content();
            copy_xml_children.action = [this] { (void)copySelectionXML(false); };
            other_item.children.push_back(std::move(copy_xml_children));
            menu->items.push_back(std::move(other_item));

            separator();
            lib::MenuItem create_item;
            create_item.text = "コンテントの作成";
            create_item.command = "create-content";
            create_item.enabled = !readOnly_ && static_cast<bool>(selectedTree_);
            for (const auto& [group, types] : event::CTYPE_GROUP()) {
                lib::MenuItem group_item;
                group_item.text = groupLabel(group);
                group_item.command = "create-group:" +
                                     std::to_string(static_cast<std::int32_t>(group));
                for (const auto type : types) {
                    lib::MenuItem type_item;
                    type_item.text = contentName(type);
                    type_item.command = contentCommand(type);
                    type_item.icon = contentImage(type);
                    type_item.enabled = create_item.enabled && contentTypeAllowed(type);
                    type_item.action = [this, type] {
                        if (!contentTypeAllowed(type)) return;
                        const auto savedMode = contentPutMode_;
                        const std::weak_ptr<const int> life = paletteLifetime_.token;
                        contentPutMode_ = PutMode::Quick;
                        selectContentTool(type);
                        if (life.expired()) return;
                        (void)placeSelectedContentTool(false);
                        if (life.expired()) return;
                        contentToolArrow_ = true;
                        contentPutMode_ = savedMode;
                        publishContentToolState();
                        refreshToolbarState();
                    };
                    type_item.action_with_gesture = [this, type](
                                                        const lib::CommandGesture gesture) {
                        if (!contentTypeAllowed(type)) return;
                        const auto savedMode = contentPutMode_;
                        const std::weak_ptr<const int> life = paletteLifetime_.token;
                        contentPutMode_ = PutMode::Quick;
                        selectContentTool(type);
                        if (life.expired()) return;
                        (void)placeSelectedContentToolWithOptions(
                            hasShift(gesture),
                            contentInsertFirst_ != hasControl(gesture),
                            contentAutoEdit_ != hasAlt(gesture));
                        if (life.expired()) return;
                        contentToolArrow_ = true;
                        contentPutMode_ = savedMode;
                        publishContentToolState();
                        refreshToolbarState();
                    };
                    group_item.children.push_back(std::move(type_item));
                }
                if (!group_item.children.empty())
                    create_item.children.push_back(std::move(group_item));
            }
            menu->items.push_back(std::move(create_item));

            lib::MenuItem convert_item;
            convert_item.text = "コンテントの変換";
            convert_item.command = "convert-content";
            convert_item.enabled = !readOnly_ && contentSelectionActive_ &&
                                   static_cast<bool>(selectedContent_);
            for (const auto& [group, types] : event::CTYPE_GROUP()) {
                lib::MenuItem group_item;
                group_item.text = groupLabel(group);
                group_item.command = "convert-group:" +
                                     std::to_string(static_cast<std::int32_t>(group));
                for (const auto type : types) {
                    if (!selectedContent_ || !selectedContent_->canConvert(type)) continue;
                    lib::MenuItem type_item;
                    type_item.text = contentName(type);
                    type_item.enabled = canConvert(type);
                    type_item.command = "convert:" +
                                        std::to_string(static_cast<std::int32_t>(type));
                    type_item.icon = contentImage(type);
                    type_item.action = [this, type] { (void)convert(type); };
                    group_item.children.push_back(std::move(type_item));
                }
                if (!group_item.children.empty())
                    convert_item.children.push_back(std::move(group_item));
            }
            convert_item.enabled = convert_item.enabled &&
                                   !convert_item.children.empty();
            menu->items.push_back(std::move(convert_item));

            lib::MenuItem templates_item;
            templates_item.text = comm_.eventTemplatesText;
            templates_item.command = "event-templates";
            const auto append_templates = [this, &templates_item](
                                              const lib::Array<structs::EvTemplate>& values) {
                for (const auto& value : values) {
                    lib::MenuItem item;
                    item.text = templateMenuText(value);
                    item.icon = templateContentImage(value.script);
                    item.shortcut = value.hotkey;
                    item.enabled = !readOnly_ &&
                                   static_cast<bool>(comm_.compileEventTemplate);
                    item.action = [this, script = value.script] {
                        (void)pasteScript(script, false);
                    };
                    item.action_with_gesture = [this, script = value.script](
                                                   const lib::CommandGesture gesture) {
                        (void)pasteScript(script, hasShift(gesture));
                    };
                    templates_item.children.push_back(std::move(item));
                }
            };
            const auto scenario_templates = comm_.scenarioEventTemplates
                                                ? comm_.scenarioEventTemplates()
                                                : lib::Array<structs::EvTemplate>{};
            const auto global_templates = comm_.globalEventTemplates
                                              ? comm_.globalEventTemplates()
                                              : lib::Array<structs::EvTemplate>{};
            append_templates(scenario_templates);
            if (!scenario_templates.empty() && !global_templates.empty()) {
                lib::MenuItem item;
                item.separator = true;
                templates_item.children.push_back(std::move(item));
            }
            append_templates(global_templates);
            if (comm_.editScenarioEventTemplates) {
                if (!templates_item.children.empty()) {
                    lib::MenuItem item;
                    item.separator = true;
                    templates_item.children.push_back(std::move(item));
                }
                lib::MenuItem edit_templates;
                edit_templates.text = comm_.editScenarioEventTemplatesText;
                edit_templates.action = comm_.editScenarioEventTemplates;
                templates_item.children.push_back(std::move(edit_templates));
            }
            templates_item.enabled = !templates_item.children.empty();
            menu->items.push_back(std::move(templates_item));

            separator();
            add("ツリーをスタートコンテントへ分離", "wrap-tree",
                canWrapTree(), [this] { (void)wrapTree(); });
            add("スタートコンテントの参照元を検索", "find-start-users",
                canFindStartUsers(), [this] { (void)findStartUsers(); });
        }
        const bool canSelectConnected = left_tree
            ? canSelectConnectedResource()
            : (comm_.canSelectConnectedContentResource &&
               comm_.canSelectConnectedContentResource());
        // ポップアップを開いた側を正とする。メニュー表示中も
        // キーのフォーカスは他方のペインにある場合がある。
        add("接続先を選択", "select-connected", canSelectConnected,
            [this, left_tree] {
                if (left_tree) {
                    selectConnectedResource();
                } else if (comm_.selectConnectedContentResource) {
                    comm_.selectConnectedContentResource();
                }
            });

    if (!left_tree) {
        // 右側のコンテントメニューは、共通MenuIDの表示情報か
        // らDのEventTreeViewの順序、ラベル、キー、
        // アイコンを使う。ALLもDと同じく群間に区切りを置く。
        struct Entry { const char* command; std::optional<types::MenuID> id; };
        const std::vector<std::vector<Entry>> groups{
            {{"edit", types::MenuID::EditProp}, {"comment", types::MenuID::Comment}},
            {{"undo", types::MenuID::Undo}, {"redo", types::MenuID::Redo}},
            {{"cut", types::MenuID::Cut}, {"copy", types::MenuID::Copy},
             {"paste", types::MenuID::Paste}, {"delete", types::MenuID::Delete}},
            {{"clone", types::MenuID::Clone}},
            {{"cut-one", types::MenuID::Cut1Content}, {"copy-one", types::MenuID::Copy1Content},
             {"delete-one", types::MenuID::Delete1Content}},
            {{"paste-insert", types::MenuID::PasteInsert}},
            {{"swap-parent", types::MenuID::SwapToParent},
             {"swap-child", types::MenuID::SwapToChild}},
            {{"expand", types::MenuID::Expand}, {"collapse", types::MenuID::Collapse}},
            {{"to-script", types::MenuID::ToScript},
             {"to-script-one", types::MenuID::ToScript1Content},
             {"to-script-all", types::MenuID::ToScriptAll}},
            {{"other", std::nullopt}},
            {{"start-to-package", types::MenuID::StartToPackage},
             {"wrap-tree", types::MenuID::WrapTree}},
            {{"select-connected", types::MenuID::SelectConnectedResource}},
            {{"find-start-users", types::MenuID::FindID}},
            {{"create-content", types::MenuID::CreateContent},
             {"convert-content", types::MenuID::ConvertContent}},
            {{"event-templates", types::MenuID::EvTemplates}},
        };
        const bool no_separators = false;
        std::vector<lib::MenuItem> ordered;
        for (const auto& group : groups) {
            bool added = false;
            for (const auto& entry : group) {
                const auto found = std::find_if(menu->items.begin(), menu->items.end(),
                    [&entry](const lib::MenuItem& item) {
                        return !item.separator && item.command == entry.command;
                    });
                if (found == menu->items.end()) continue;
                auto item = std::move(*found);
                if (entry.id) {
                    if (comm_.menuText)
                        if (auto text = comm_.menuText(*entry.id); !text.empty())
                            item.text = std::move(text);
                    if (comm_.menuImage)
                        if (const auto icon = comm_.menuImage(*entry.id); icon)
                            item.icon = icon;
                }
                if (!added && !ordered.empty() && !no_separators) {
                    lib::MenuItem separator_item;
                    separator_item.separator = true;
                    ordered.push_back(std::move(separator_item));
                }
                ordered.push_back(std::move(item));
                added = true;
            }
        }
        menu->items = std::move(ordered);
    }

    comm_.services.gui->show_context_menu(menu, owner, position);
}

void EventView::handleEventGraphKey(lib::KeyEvent& event) {
    // Canvas上で名前・選択肢の編集部が開いている間は、移
    // 動、Enter、Escapeを含む全キーを編集部へ渡す。
    // ここでカーソルキーを扱うと行選択が動き、編集が壊れる。
    if (inlineNameEditActive()) return;
    // Dの独自描画EventEditorは、修飾なしのカーソ
    // ルキーを行移動に使う。Ctrl+上下など修飾付きはメ
    // ニューのアクセラレーターへ残し、ここでは消費しない。
    if (graphRows_.empty()) return;
    const bool cursor_key = event.key == lib::Key::Left ||
                            event.key == lib::Key::Right ||
                            event.key == lib::Key::Up ||
                            event.key == lib::Key::Down;
    const auto* editor_props = comm_.editorProps ? comm_.editorProps() : nullptr;
    const bool classic_left = event.key == lib::Key::Left &&
        !lib::editor_window_host_options().all_mode &&
        comm_.services.gui && comm_.services.gui->platform() == lib::Platform::Windows &&
        editor_props && !editor_props->var().straightEventTreeView.value &&
        editor_props->var().classicStyleTree.value;
    if (cursor_key && event.modifiers != lib::Modifiers::None && !classic_left) return;
    auto found = std::find_if(graphRows_.begin(), graphRows_.end(),
        [this](const GraphRow& row) { return row.id == selectedContentId_; });
    if (cursor_key && found == graphRows_.end()) {
        // DのonKeyDownは、未選択なら右・下
        // も含め、修飾なしのどの矢印でも行0を選ぶ。
        (void)selectContent(graphRows_.front().id);
        event.handled = true;
        return;
    }
    std::size_t index = found == graphRows_.end()
        ? 0U : static_cast<std::size_t>(std::distance(graphRows_.begin(), found));

    if (event.key == lib::Key::Left || event.key == lib::Key::Right) {
        const auto selected_id = graphRows_[index].id;
        const auto node = contents_ ? std::find_if(
            contents_->nodes.begin(), contents_->nodes.end(),
            [selected_id](const lib::TreeNode& candidate) {
                return candidate.id == selected_id;
            }) : std::vector<lib::TreeNode>::iterator{};
        if (contents_ && found != graphRows_.end() &&
            node != contents_->nodes.end()) {
            // DのWindowsの従来型Treeリスナーは、修飾付き
            // 左矢印も含め実際のコンテント親を選ぶ。合成したEventTree
            // ルートは、DのTreeItemの親ではない。
            const auto* parent_info = nodeInfo(node->parent);
            if (classic_left && parent_info && parent_info->kind == NodeInfo::Kind::Content) {
                (void)selectContent(node->parent);
                invalidateEventGraph();
                event.handled = true;
                return;
            }
        }
        // DのEventEditorはexpandedOperationの既定
        // がfalse。左右は折りたたみ・展開ではなく、前後の行へ移動する。
        if (event.key == lib::Key::Left) {
            if (index > 0U) --index;
        } else if (index + 1U < graphRows_.size()) {
            ++index;
        }
    } else if (event.key == lib::Key::Up && index > 0U) {
        --index;
    } else if (event.key == lib::Key::Down && index + 1U < graphRows_.size()) {
        ++index;
    } else if (event.key == lib::Key::F2) {
        (void)editSelection();
        event.handled = true;
        return;
    } else if (event.key == lib::Key::Enter) {
        // DはStartのEnterを、ダイアログなしの代替処理を呼ばずに消費する。F2
        // やメニューからの明示的な名前変更はeditSelection()で扱う。
        if (lib::editor_window_host_options().all_mode || !selectedContent_ ||
            selectedContent_->type() != types::CType::Start) edit();
        event.handled = true;
        return;
    } else if (event.key == lib::Key::Delete && !readOnly_) {
        (void)deleteContent();
        event.handled = true;
        return;
    } else {
        return;
    }
    (void)selectContent(graphRows_[index].id);
    invalidateEventGraph();
    event.handled = true;
}

void EventView::revealSelectedEventContent() {
    const auto scroll = eventGraphScroll_;
    if (!scroll) return;
    const auto row = std::find_if(graphRows_.begin(), graphRows_.end(),
        [this](const GraphRow& value) { return value.id == selectedContentId_; });
    if (row == graphRows_.end()) return;
    // DのEventEditor::showSelectionは、表示中のスクロールバーを除くgetClientArea()
    // を使う。ここでも共通の表示領域を使い、直交方向のスクロールバーが選択行を覆わないようにする。
    const auto viewport = lib::item_scroll_geometry(*scroll).viewport;
    if (viewport.width <= 0.0F || viewport.height <= 0.0F) return;
    auto offset = scroll->scroll_offset;
    const float top = row->y, bottom = top + graphRowHeight();
    if (top < offset.y) offset.y = top;
    else if (bottom > offset.y + viewport.height) offset.y = bottom - viewport.height;
    const float slope = graphPx(slope_ ? static_cast<float>(slope_->value) : 16.0F);
    const float left = (showLineNumber_ ? graphPx(kEventGraphLineNumberWidth) : 0.0F) +
        static_cast<float>(row->depth1) * graphPx(16.0F) + static_cast<float>(row->depth2) * slope;
    const float right = left + graphPx(19.0F);
    if (left < offset.x) offset.x = left;
    else if (right > offset.x + viewport.width) offset.x = right - viewport.width;
    // 文書の寸法とネイティブのスクロールバー同期は、共通の配置処理が管理する。
    (void)lib::scroll_view_content(scroll, offset);
}

void EventView::invalidateEventGraph() {
    if (eventGraphCanvas_ && eventGraphCanvas_->invalidate)
        eventGraphCanvas_->invalidate();
    else if (eventGraphCanvas_ && comm_.services.gui)
        comm_.services.gui->request_redraw(eventGraphCanvas_);
}

void EventView::refreshIgnitionChoices() {
    if (!ignition_ || !roundIgnition_) return;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    std::vector<lib::ChoiceItem> choices;
    std::optional<std::size_t> selected;
    std::optional<std::string> editText;
    String kind = "system";
    if (treeKind_ && treeKind_->selected_index &&
        *treeKind_->selected_index < treeKind_->items.size())
        kind = treeKind_->items[*treeKind_->selected_index].value;
    const bool round_kind = kind == "round";
    roundIgnition_->set_visible(round_kind);
    ignition_->set_visible(!round_kind);
    if (round_kind) roundIgnition_->value = 1.0;
    if (kind == "keycode") {
        ignition_->editable = true;
        if (comm_.fireKeyCodes) {
            // Dは標準名と使用済み名の間の空の区切りを保持する。
            const auto key_codes = comm_.fireKeyCodes();
            if (life.expired()) return;
            for (const auto& key_code : key_codes)
                choices.push_back({key_code, key_code});
        }
        if (!choices.empty()) {
            selected = 0U;
            editText = choices.front().text;
        } else {
            editText = std::string{};
        }
    } else if (kind == "round") {
        ignition_->editable = false;
        editText = std::string{};
    } else if (area_) {
        ignition_->editable = false;
        editText = std::string{};
        // DのEventView.startDefValsのシステム発火は、左ツ
        // リーの選択所有元に従う。メニューカードはクリック、敵・プレイヤーカー
        // ドは死亡。それ以外は表示自身の種別（areaDefVals）を使う。
        namespace area = ::cwxeditor::cwx::area;
        auto* const owner = selectionParent();
        if (dynamic_cast<area::MenuCard*>(owner)) {
            choices.push_back({String{"クリック"}, "enter"});
        } else if (dynamic_cast<area::EnemyCard*>(owner) ||
                   dynamic_cast<area::PlayerCardEvents*>(owner)) {
            choices.push_back({String{"死亡"}, "enter"});
        } else if (dynamic_cast<area::Area*>(area_)) {
            choices.push_back({String{"到着"}, "enter"});
        } else if (dynamic_cast<area::Battle*>(area_)) {
            choices.push_back({String{"勝利"}, "enter"});
            choices.push_back({String{"逃走"}, "escape"});
            choices.push_back({String{"敗北"}, "lose"});
            choices.push_back({String{"毎ラウンド"}, "every_round"});
            choices.push_back({String{"ラウンド終了"}, "round_end"});
            choices.push_back({String{"バトル開始"}, "round_0"});
        } else if (dynamic_cast<area::Package*>(area_)) {
            choices.push_back({String{"パッケージ"}, "package"});
        } else {
            choices.push_back({String{"使用時"}, "enter"});
        }
        // Dは以前のシステム選択が引き続き候補にあれば維持する。
        if (ignition_->selected_index &&
            *ignition_->selected_index < ignition_->items.size()) {
            const auto& old = ignition_->items[*ignition_->selected_index];
            for (std::size_t index = 0; index < choices.size(); ++index)
                if (choices[index].text == old.text &&
                    choices[index].value == old.value) selected = index;
        }
    }
    if (!ignition_->editable) {
        if (choices.empty()) choices.push_back({String{"パッケージ"}, "package"});
        if (!selected) selected = 0U;
    }
    if (!lib::replace_combo_items(ignition_, std::move(choices), selected,
                                  std::move(editText))) return;
    if (life.expired()) return;
    if (keyCodeTiming_) {
        // DのkindSelectedでは、キーコードのタイミングを敵・プレイヤー・効果系
        // カードに適用する。Area/Battleの所有元自身にはタイミングを付けない。
        namespace area = ::cwxeditor::cwx::area;
        auto* const owner = selectionParent();
        const bool card_view = area_ && !dynamic_cast<area::Area*>(area_) &&
            !dynamic_cast<area::Battle*>(area_) && !dynamic_cast<area::Package*>(area_);
        const bool timing_owner = card_view ||
            dynamic_cast<area::EnemyCard*>(owner) ||
            dynamic_cast<area::PlayerCardEvents*>(owner);
        keyCodeTiming_->set_enabled(!readOnly_ && kind == "keycode" && timing_owner);
    }
    if (life.expired()) return;
    // editableはWindowsのネイティブコンボのクラスを変える。
    // ここで整合させ、「キーコード」の選択直後に、D/SWTのkindSelected()
    // と同じく発火一覧を文字入力可能なコンボへ置き換える。
    const auto gui = comm_.services.gui;
    const auto panel = panel_;
    if (gui && panel) gui->request_redraw(panel);
}

std::optional<FireCondition> EventView::ignitionFromControls() const {
    if (!treeKind_ || !treeKind_->selected_index ||
        *treeKind_->selected_index >= treeKind_->items.size() ||
        !ignition_ || !roundIgnition_)
        return std::nullopt;
    const auto& kind = treeKind_->items[*treeKind_->selected_index].value;
    if (kind == "round") {
        const auto round = lib::validated_number_value(
            *roundIgnition_, roundIgnition_->value);
        if (!round || std::floor(*round) != *round) return std::nullopt;
        return FireCondition::withRound(static_cast<std::uint32_t>(*round));
    }
    String value;
    if (ignition_->editable) value = ignition_->edit_text;
    else if (ignition_->selected_index &&
             *ignition_->selected_index < ignition_->items.size())
        value = ignition_->items[*ignition_->selected_index].value;

    if (kind == "keycode") {
        if (value.empty()) return std::nullopt;
        types::FKCKind timing = types::FKCKind::Use;
        if (keyCodeTiming_ && keyCodeTiming_->enabled() &&
            keyCodeTiming_->selected_index) {
            switch (*keyCodeTiming_->selected_index) {
            case 1U: timing = types::FKCKind::Success; break;
            case 2U: timing = types::FKCKind::Failure; break;
            case 3U: timing = types::FKCKind::HasNot; break;
            default: break;
            }
        }
        return FireCondition::withKeyCode({value, timing});
    }

    if (value == "enter") return FireCondition::enter();
    if (value == "escape") return FireCondition::escape();
    if (value == "lose") return FireCondition::lose();
    if (value == "every_round") return FireCondition::everyRound();
    if (value == "round_end") return FireCondition::roundEnd();
    if (value == "round_0") return FireCondition::round0();
    if (value == "package") return FireCondition::enter();
    return std::nullopt;
}

bool EventView::contentTypeAllowed(const types::CType type) const {
    const auto summary = comm_.summary.lock();
    return !summary || !summary->legacy() || !types::isWsnContent(type);
}

void EventView::selectContentTool(const types::CType type) {
    if (!contentTypeAllowed(type)) return;
    contentToolType_ = type;
    contentToolArrow_ = false;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    publishContentToolState();
    if (life.expired()) return;
    refreshToolbarState();
}

bool EventView::placeSelectedContentTool(const bool insertBefore) {
    return placeSelectedContentToolWithOptions(insertBefore, contentInsertFirst_,
                                               contentAutoEdit_);
}

bool EventView::placeSelectedContentToolWithOptions(const bool insertBefore,
                                                    const bool insertFirst,
                                                    const bool autoEdit) {
    if (readOnly_ || !selectedTree_ || contentToolArrow_ ||
        !contentTypeAllowed(contentToolType_)) return false;
    // DのEventTreeView.create()は表示EventTree
    // に項目がなければ戻る。読み込んだ空ツリーでも同じ判定を使う。テンプレー
    // トのPutContentsは別のD経路でStartルートを追加できる。
    if (selectedTree_->starts().empty()) return false;

    (void)canonicalSelectedContent();

    // ホストのバージョン、Basic、組み込みの順のファクトリーを、変換処理と共有する。
    auto content = comm_.initialContent ? comm_.initialContent(contentToolType_)
                                       : std::make_shared<event::Content>(contentToolType_);
    if (!content) return false;
    const auto tree = selectedTree_;
    ContentPtr dialogParent;
    std::function<bool()> structuralAction;
    if (contentToolType_ == types::CType::Start) {
        // EventTreeView.create(insertTo)
        // は、既存行へのStart挿入を拒否する。選択した最上
        // 位行の隣に置く、通常のStart配置とは区別する。
        if (insertBefore) return false;
        const auto* editor_props = comm_.editorProps ? comm_.editorProps() : nullptr;
        String start_name = editor_props
            ? editor_props->requiredMsgs().defaultStartName.value
            : comm_.modelProps
                ? comm_.modelProps->requiredMsgs().defaultStartName.value
                : ::cwxeditor::cwx::msgs::Msgs{}.defaultStartName.value;
        if (prop_.etc.useCurrentStartName && selectedContent_) {
            if (const auto current = selectedContent_->parentStart(); current)
                start_name = current->name();
        }
        content->setName(::cwxeditor::cwx::utils::createNewName(
            start_name, [tree = selectedTree_](const String candidate) {
                return tree && !tree->hasStart(candidate);
            }, true, eventTreeNames(prop_, *selectedTree_)));
        std::size_t index = selectedTree_->starts().size();
        // DのEventTreeView.create()は、常に末尾で
        // はなく、選択行の最上位Startの隣へStartを挿入する。
        if (selectedContent_) {
            const auto selectedStart = selectedContent_->parentStart();
            const auto found = std::find(selectedTree_->starts().begin(),
                                          selectedTree_->starts().end(),
                                          selectedStart);
            if (found != selectedTree_->starts().end()) {
                index = static_cast<std::size_t>(
                    std::distance(selectedTree_->starts().begin(), found));
                if (!insertBefore) ++index;
            }
        }
        structuralAction = [this, tree, content, index] {
                if (!tree) return false;
                tree->insert(index, content);
                selectedTree_ = tree;
                selectedContent_ = content;
                contentSelectionActive_ = true;
                return true;
            };
    } else {
        // DのinsertTo形式は所有可能な種別だけで
        // 有効。親内の選択行を新しい所有元に置き換え、現
        // 在の先頭挿入設定に従って、その行を最初・最後
        // の子にする。兄弟挿入ではなく構造を包む操作。
        if (insertBefore) {
            if (!content->detail().owner || !selectedContent_) return false;
            const auto selected = selectedContent_;
            const auto parent = selected->parent();
            if (!parent) return false;
            const auto found = std::find(parent->next().begin(), parent->next().end(),
                                         selected);
            if (found == parent->next().end()) return false;
            const auto index = static_cast<std::size_t>(
                std::distance(parent->next().begin(), found));
            content->setName(selected->name());
            dialogParent = parent;
            structuralAction = [this, tree, parent, selected, content, index,
                                insertFirst] {
                    const auto current = std::find(parent->next().begin(),
                                                   parent->next().end(), selected);
                    if (current == parent->next().end()) return false;
                    parent->remove(selected);
                    // DのinsertBeforeは、移動した子の旧分岐文字列を消し
                    // てから、新しい親に対してvalidText()で正規化する。
                    if (prop_.etc.adjustContentName) {
                        selected->setName("");
                    }
                    // DのContent.add/insertは常にvalidText()
                    // を実行する。子の分岐名（○/×、ステップなど）を表
                    // 示内容へ正規化し、空条件の警告が出ないようにする。
                    const auto* model = comm_.modelProps;
                    if (insertFirst) {
                        content->insert(model, 0U, selected);
                    } else {
                        content->add(model, selected);
                    }
                    parent->insert(model, index, content);
                    selectedTree_ = tree;
                    selectedContent_ = content;
                    contentSelectionActive_ = true;
                    return true;
                };
        } else {
            auto parent = selectedContent_;
            std::size_t index = std::numeric_limits<std::size_t>::max();
            if (parent && !parent->detail().owner) {
                const auto child = parent;
                parent = child->parent();
                if (parent && insertFirst) index = 0U;
            } else if (parent && insertFirst) {
                index = 0U;
            }
            if (!parent || !parent->detail().owner) return false;
            dialogParent = parent;
            structuralAction = [this, tree, parent, content, index] {
                    // DのContent.add/insertでは、validText()
                    // が分岐の新しい子に名前を付ける（上記参照）。
                    const auto* model = comm_.modelProps;
                    if (index == std::numeric_limits<std::size_t>::max())
                        parent->add(model, content);
                    else
                        parent->insert(model, index, content);
                    selectedTree_ = tree;
                    selectedContent_ = content;
                    contentSelectionActive_ = true;
                    return true;
                };
        }
    }

    if (!structuralAction) return false;

    bool dialogOpened = false;
    if (autoEdit && autoOpenContentAction_) {
        // Dは一時Contentで型付きダイアログを作る。最初の
        // 適用成功までは、EventTreeへ何も挿入しない。
        pendingContentCreates_.insert_or_assign(
            content.get(), PendingContentCreate{
                content, dialogParent, std::move(structuralAction)});
        try {
            dialogOpened = autoOpenContentAction_(content, dialogParent);
        } catch (...) {
            (void)cancelPendingContentCreate(content);
            throw;
        }
        if (!dialogOpened && !commitPendingContentCreate(content)) return false;
    } else {
        const bool applied = mutate(structuralAction);
        if (!applied) return false;
    }

    // 公開時は挿入コンテントを選択したまま対応表を再構築す
    // る。通常の選択経路をもう一度通し、図と共通Tree/Table
    // の双方に、DのsetSelection/showSelection
    // と同じ選択IDを渡す。
    if (!dialogOpened) {
        if (const auto found = contentIds_.find(content.get());
            found != contentIds_.end()) (void)selectContent(found->second);
        if (autoEdit && !autoOpenContentAction_) edit();
    }
    // DのPutQuickとPutSelectは、パレッ
    // トコマンドの適用後に矢印へ戻る。次の行のためにツー
    // ルを選択中に保つのはPutContinueだけ。
    if (contentPutMode_ != PutMode::Continue) contentToolArrow_ = true;
    publishContentToolState();
    refreshToolbarState();
    return true;
}

void EventView::applyContentToolCursor(lib::Widget& widget) const {
    const auto image = contentToolArrow_ ? lib::ResourceId{} : contentImage(contentToolType_);
    widget.set_custom_cursor(image);
}

void EventView::updateContentToolCursor() {
    if (contents_) applyContentToolCursor(*contents_);
    if (contentTable_) applyContentToolCursor(*contentTable_);
    if (eventGraphCanvas_ && !eventGraphDetailResizeActive_)
        applyContentToolCursor(*eventGraphCanvas_);
    for (const auto& bar : paletteBars_)
        if (bar) applyContentToolCursor(*bar);
}

void EventView::refreshToolbarState() {
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    updateContentToolCursor();
    if (life.expired()) return;
    const auto* editor_props = comm_.editorProps ? comm_.editorProps() : nullptr;
    if (life.expired()) return;
    const bool straight_tree = !editor_props ||
        editor_props->var().straightEventTreeView.value;
    const auto summary = comm_.summary.lock();
    const bool legacy_player_events = summary && summary->legacy() &&
        dynamic_cast<area::PlayerCardEvents*>(selectionParent()) != nullptr;
    const bool edit_tree_controls = !readOnly_ && !legacy_player_events;
    const auto update = [](std::vector<lib::ToolItem>& items,
                           const StringView command, const bool enabled,
                           const std::optional<bool> checked = std::nullopt) {
        for (auto& item : items) {
            if (item.command != command) continue;
            item.enabled = enabled;
            if (checked) item.checked = *checked;
        }
    };
    if (toolbar_) {
        auto items = toolbar_->items;
        update(items, "undo", !readOnly_ && canUndo());
        update(items, "redo", !readOnly_ && canRedo());
        update(items, "up", canUp());
        update(items, "down", canDown());
        (void)lib::replace_tool_bar_items(toolbar_, std::move(items));
    }
    if (viewToolbar_) {
        auto items = viewToolbar_->items;
        update(items, "create", canCreateEventWithIgnition());
        update(items, "add_fire", canAddFire());
        update(items, "edit", canEdit());
        update(items, "edit_selection", canEditSelection());
        // DのcanAllExpandは保持した全コンテントの展開状態を調べる。canAllFoldStart
        // は子孫の展開ではなくStart行だけを調べる。
        bool can_expand = false;
        bool can_fold_start = false;
        if (selectedTree_ && contents_) for (const auto& node : contents_->nodes) {
            const auto* info = nodeInfo(node.id);
            if (!info || !info->content) continue;
            can_expand = can_expand || !node.expanded;
            if (info->content->type() == types::CType::Start && node.expanded)
                can_fold_start = true;
        }
        update(items, "expand", can_expand);
        update(items, "collapse", can_fold_start);
        update(items, "detail", straight_tree, showEventTreeDetail_);
        update(items, "line", straight_tree, showLineNumber_);
        update(items, "select_current", canSelectCurrentEvent());
        (void)lib::replace_tool_bar_items(viewToolbar_, std::move(items));
    }
    if (slope_) slope_->set_enabled(straight_tree);
    if (treeKind_) treeKind_->set_enabled(edit_tree_controls);
    if (ignition_) ignition_->set_enabled(edit_tree_controls);
    if (roundIgnition_) roundIgnition_->set_enabled(edit_tree_controls);
    for (const auto& bar : paletteBars_) {
        if (!bar) continue;
        auto items = bar->items;
        for (auto& item : items) {
            if (const auto type = contentCommandType(item.command)) {
                item.enabled = !readOnly_ && selectedTree_ != nullptr &&
                               contentTypeAllowed(*type);
                item.checked = !contentToolArrow_ && *type == contentToolType_;
                item.tooltip = contentName(*type);
                if (prop_.etc.showEventContentDescription) {
                    if (const auto description = contentDescription(*type);
                        !description.empty())
                        item.tooltip += "\n" + description;
                }
            } else if (item.command == "arrow") {
                item.checked = contentToolArrow_;
            } else if (item.command == "put_mode") {
                item.icon = viewImage(contentPutMode_ == PutMode::Quick ? "quick" :
                                      contentPutMode_ == PutMode::Continue ? "continue" :
                                                                            "select",
                                      types::CType::CallStart);
            } else if (item.command == "auto_edit") {
                item.checked = contentAutoEdit_;
            } else if (item.command == "insert_first") {
                item.checked = contentInsertFirst_;
            }
        }
        (void)lib::replace_tool_bar_items(bar, std::move(items));
    }
    for (const auto& selector : paletteGroupSelectors_) {
        if (!selector) continue;
        auto items = selector->items;
        for (auto& item : items) {
            const auto type = contentCommandType(item.value);
            item.enabled = !readOnly_ && selectedTree_ != nullptr &&
                           (!type || contentTypeAllowed(*type));
            if (type) {
                item.tooltip = contentDescription(*type);
                if (item.tooltip.empty()) item.tooltip = item.text;
            }
        }
        (void)lib::replace_combo_items(selector, std::move(items),
                                       std::nullopt, selector->edit_text);
    }

    // ツール項目は値で保存する。チェック・有効状
    // 態を変えても、明示的に同期するまでは作成済
    // みのネイティブツールバーに反映されない。
    if (comm_.services.gui) {
        if (toolbar_) comm_.services.gui->request_redraw(toolbar_);
        if (viewToolbar_) comm_.services.gui->request_redraw(viewToolbar_);
        if (palettePanel_) comm_.services.gui->request_redraw(palettePanel_);
    }
}

void EventView::edit() {
    // DのEditLは所有元への通知前に読み取り専用を検査する。キー・
    // ポインターのどちらからも、読み取り専用EventViewのコー
    // ルバックが外部の書き込み可能なダイアログを開かないようにする。
    if (readOnly_) return;
    if (editAction_) editAction_();
    else editing_ = true;
}

void EventView::editEnter() {
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    // 左のTreeEditの確定をまたいで、元の右編集部の同一性を保持する。
    const auto gui = comm_.services.gui;
    const auto graph_canvas = eventGraphCanvas_;
    const auto graph_binding = graphEditBinding_;
    const bool graph_edit_was_active = gui && graph_canvas &&
        gui->programmatic_inline_edit_active(graph_canvas);
    // DのEventView.editEnterは、左のTreeEditと右の図の編集の両方を確定する。
    if (gui && cards_)
        (void)gui->finish_programmatic_inline_edit(cards_, true);
    if (life.expired()) return;
    // 左の確定で右の代替編集が同期的に開く場合がある。取得したCanvas
    // ・サービス・接続が一致する場合だけ、右セッションを終了する。
    if (graph_edit_was_active && gui == comm_.services.gui &&
        graph_canvas == eventGraphCanvas_ && graph_binding == graphEditBinding_)
        (void)gui->finish_programmatic_inline_edit(graph_canvas, true);
    // 確定コールバックがこの表示を終了させたり、新しい編集を開いたりし
    // 得る。終了済みの表示に触れず、代替セッションの接続も解除しない。
    if (life.expired() || inlineNameEditActive()) return;
    editing_ = false;
    graphEditBinding_.reset();
}

void EventView::editCancel() {
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    // 左のTreeEditの取消をまたいで、元の右編集部の同一性を保持する。
    const auto gui = comm_.services.gui;
    const auto graph_canvas = eventGraphCanvas_;
    const auto graph_binding = graphEditBinding_;
    const bool graph_edit_was_active = gui && graph_canvas &&
        gui->programmatic_inline_edit_active(graph_canvas);
    // DのEventView.editCancelは、左のTreeEditと右の図の編集の両方を取り消す。
    if (gui && cards_)
        (void)gui->finish_programmatic_inline_edit(cards_, false);
    if (life.expired()) return;
    // コールバックが開いた代替の図の編集は、そのまま残す。
    if (graph_edit_was_active && gui == comm_.services.gui &&
        graph_canvas == eventGraphCanvas_ && graph_binding == graphEditBinding_)
        (void)gui->finish_programmatic_inline_edit(graph_canvas, false);
    // 確定コールバックがこの表示を終了させたり、新しい編集を開いたりし
    // 得る。終了済みの表示に触れず、代替セッションの接続も解除しない。
    if (life.expired() || inlineNameEditActive()) return;
    editing_ = false;
    graphEditBinding_.reset();
}

bool EventView::initial() {
    if (initialized_) return false;
    refresh();
    return true;
}

void EventView::refresh(EventTreeOwner* excludedOwner) {
    refreshImpl(excludedOwner, true);
}

void EventView::refreshExternalState() {
    refreshImpl(nullptr, false);
}

void EventView::refreshImpl(EventTreeOwner* excludedOwner,
                            const bool cancelInlineEditors) {
    if (historyPresentationRetired_) return;
    const std::weak_ptr<const int> statusLife = paletteLifetime_.token;
    if (cancelInlineEditors) {
        const auto cancelSameOwnerEditors = cancelSameOwnerEditors_;
        if (cancelSameOwnerEditors) {
            const bool canceled = cancelSameOwnerEditors();
            if (!canceled) return;
        } else {
            editCancel();
        }
    }
    if (statusLife.expired()) return;
    const auto previousTree = selectedTree_;
    initialized_ = true;
    rebuildCards(excludedOwner);
    // rebuildCards()は取り消し・やり直しの復元後に共通ノー
    // ドの対応表を置換する。ダイアログやテンプレートへ選択を公開する前に
    // 正規化し、破棄済みツリーのコピーに属するコンテントを保持させない。
    (void)canonicalSelectedContent();
    refreshStatusLine();
    if (statusLife.expired()) return;
    refreshToolbarState();
    // 上記の配列はその場で置換する。既存のネイテ
    // ィブツリー・表にも反映し、古い行が残って成
    // 功した追加が失敗したように見えるのを防ぐ。
    if (comm_.services.gui) {
        if (cards_) comm_.services.gui->request_redraw(cards_);
        if (contents_) comm_.services.gui->request_redraw(contents_);
        if (contentTable_) comm_.services.gui->request_redraw(contentTable_);
    }
    invalidateEventGraph();
    if (comm_.refreshToolBar) comm_.refreshToolBar();
    if (statusLife.expired()) return;
    // 削除や履歴再適用後の再構築は、select()を経ずに表
    // 示ツリーを置換・解除し得る。同じホストの調整役へ通知し、
    // 現在のパレットを取得するか、所有ツリーのないものを隠す。
    if (previousTree != selectedTree_) {
        selectionChanged.call();
    }
    if (statusLife.expired()) return;
    if (selectedTree_) {
        showFloatingPalette();
    } else {
        closeToolWindow();
    }
    if (!statusLife.expired()) {
        statusChanged.call();
    }
}

void EventView::refreshTopStart() { refresh(); }

void EventView::refreshTrees() { refresh(); }

TreeItemId EventView::appendCardNode(NodeMap& nodes, TreeIdMap& treeIds,
                                     std::vector<lib::TreeNode>& treeNodes,
                                     TreeItemId& nextId, const NodeInfo& info,
                                     const TreeItemId parent, String text,
                                     const bool expanded) {
    TreeItemId id{};
    if (info.kind == NodeInfo::Kind::Owner && rootId_ != 0) {
        const auto found = nodes_.find(rootId_);
        if (found != nodes_.end() && found->second.kind == NodeInfo::Kind::Owner &&
            found->second.owner == info.owner) {
            id = rootId_;
        }
    } else if (info.kind == NodeInfo::Kind::EventTree && info.tree) {
        const auto found = treeIds_.find(info.tree.get());
        if (found != treeIds_.end()) {
            id = found->second;
        } else {
            // DのUndoTreeDataは、履歴でEventTreeポイン
            // ターが保存コピーへ置換されても、論理行の展開状態を維持する。
            const auto snapshotRow = std::find_if(
                nodes_.begin(), nodes_.end(), [&info](const auto& entry) {
                    const auto& previous = entry.second;
                    return previous.kind == NodeInfo::Kind::EventTree &&
                           previous.owner == info.owner && previous.tree &&
                           previous.tree->objectId() == info.tree->objectId() &&
                           previous.ownerIndex == info.ownerIndex &&
                           previous.treeIndex == info.treeIndex;
                });
            if (snapshotRow != nodes_.end()) id = snapshotRow->first;
        }
    } else if (info.kind == NodeInfo::Kind::Fire && info.tree) {
        const auto found = std::find_if(
            nodes_.begin(), nodes_.end(), [&info](const auto& entry) {
                const auto& previous = entry.second;
                return previous.kind == NodeInfo::Kind::Fire &&
                       previous.tree.get() == info.tree.get() &&
                       previous.fire.kind == info.fire.kind &&
                       previous.fire.round == info.fire.round &&
                       previous.fire.keyCode.kind == info.fire.keyCode.kind &&
                       previous.fire.keyCode.keyCode == info.fire.keyCode.keyCode;
            });
        if (found != nodes_.end()) id = found->first;
    }
    if (id == 0) id = nextId++;
    bool effectiveExpanded = expanded;
    if (cards_) {
        const auto previous = std::find_if(
            cards_->nodes.begin(), cards_->nodes.end(),
            [id](const lib::TreeNode& value) { return value.id == id; });
        if (previous != cards_->nodes.end()) effectiveExpanded = previous->expanded;
    }
    nodes.emplace(id, info);
    lib::TreeNode node{id, parent, std::move(text), {}, 0,
                       effectiveExpanded, false, false};
    if (info.kind == NodeInfo::Kind::Owner) {
        if (dynamic_cast<::cwxeditor::cwx::area::PlayerCardEvents*>(info.owner))
            node.icon = viewImage("player_card", types::CType::Start);
        else if (const auto* card = dynamic_cast<::cwxeditor::cwx::area::MenuCard*>(info.owner))
            node.icon = viewImage(card->flag().empty() ? "menu_card" : "card_flag",
                                  types::CType::Start);
        else if (const auto* card = dynamic_cast<::cwxeditor::cwx::area::EnemyCard*>(info.owner)) {
            // DのcardIconは、カード自身のフラグとRunAway
            // 操作を使う。所属バトルの逃走許可や現在のフラグ値ではない。
            const bool flagged = !card->flag().empty();
            const bool escapes = card->action(types::ActionCardType::RunAway);
            node.icon = viewImage(escapes
                ? (flagged ? "enemy_escape_flag" : "enemy_escape")
                : (flagged ? "card_flag" : "enemy_card"), types::CType::Start);
        }
        else
            node.icon = viewImage("owner", types::CType::Start);
        node.expanded = true;
        // ＋/－の印を隠すのは構造上のArea/Battleルー
        // トだけ。Dはプレイヤー・メニュー・敵の所有元を独立した
        // 最上位行として公開し、イベントの子を展開可能に保つ。
        node.suppress_expander = info.ownerIndex == 0U;
        // DのsetupComment(Tree)はEventTreeOwner.commentForEvents()
        // を読む。共通TreeViewの表示でも、同じ所有元データとアイコンを使う。
        node.comment_text = info.owner->commentForEvents();
        if (!node.comment_text.empty() && comm_.menuImage)
            node.comment_icon = comm_.menuImage(types::MenuID::Comment);
    } else if (info.kind == NodeInfo::Kind::EventTree) {
        node.icon = viewImage("event_tree", types::CType::Start);
        if (info.tree) {
            node.has_lazy_children = info.tree->fireEnter() || info.tree->fireEscape() ||
                                     info.tree->fireLose() || info.tree->fireEveryRound() ||
                                     info.tree->fireRoundEnd() || info.tree->fireRound0() ||
                                     !info.tree->keyCodes().empty() ||
                                     !info.tree->rounds().empty();
        }
    } else if (info.kind == NodeInfo::Kind::Fire) {
        const auto round = info.fire.kind == FireKind::Round;
        const auto key_code = info.fire.kind == FireKind::KeyCode;
        // DのEventView.keyCodeImageは、敵・プレイヤーカー
        // ドの発火にタイミングアイコンを示す。他の所有元は未所持だけを区別する。
        const char* key_code_icon = "fire_keycode";
        if (key_code) {
            namespace area = ::cwxeditor::cwx::area;
            const bool card_owner =
                dynamic_cast<area::EnemyCard*>(info.owner) ||
                dynamic_cast<area::PlayerCardEvents*>(info.owner);
            switch (info.fire.keyCode.kind) {
            case types::FKCKind::Success:
                if (card_owner) key_code_icon = "fire_keycode_success";
                break;
            case types::FKCKind::Failure:
                if (card_owner) key_code_icon = "fire_keycode_failure";
                break;
            case types::FKCKind::HasNot:
                key_code_icon = "fire_keycode_hasnot";
                break;
            default: break;
            }
        }
        node.icon = viewImage(round ? "fire_round" : key_code ? key_code_icon
                                                            : "fire_default",
                              round ? types::CType::BranchRound
                                    : key_code ? types::CType::BranchKeyCode
                                               : types::CType::Start);
    }
    // C++の意図的な追加として、場面に加えPackageとカード使用
    // 時イベントにも、汎用の所有元・ツリーの診断を公開する。DのgetWarnings
    // は場面だけだが、ここでは広い表示範囲を維持する。
    if (comm_.warnings) {
        if (info.kind == NodeInfo::Kind::Owner && info.owner)
            node.warnings = comm_.warnings(*info.owner);
        else if (info.kind == NodeInfo::Kind::EventTree && info.tree)
            node.warnings = comm_.warnings(*info.tree);
    }
    // DのEventView.getWarningsは、コアのEventTreeの警
    // 告群と独立に、表示中イベント名の文字コード警告を場面イベントの行へ追加する。
    if (info.kind == NodeInfo::Kind::EventTree && info.tree &&
        comm_.modelProps && (dynamic_cast<area::Area*>(area_) ||
                             dynamic_cast<area::Battle*>(area_)) &&
        !info.tree->starts().empty() && info.tree->starts().front()) {
        const auto summary = comm_.summary.lock();
        auto names = ::cwxeditor::cwx::warning::sjisWarnings(
            *comm_.modelProps, summary.get(), info.tree->starts().front()->name(),
            comm_.modelProps->requiredMsgs().eventName.value);
        node.warnings.insert(node.warnings.begin(), names.begin(), names.end());
    }
    if (info.kind == NodeInfo::Kind::Fire && info.tree && comm_.ignitionWarnings) {
        node.warnings = comm_.ignitionWarnings(*info.tree,info.fire);
    }
    if (!node.warnings.empty()) node.warning_icon = comm_.warningImage;
    treeNodes.push_back(std::move(node));
    if (info.kind == NodeInfo::Kind::EventTree && info.tree)
        treeIds[info.tree.get()] = id;
    return id;
}

void EventView::rebuildCards(EventTreeOwner* excludedOwner) {
    const auto oldTree = selectedTree_;
    const auto oldContent = selectedContent_;
    const auto* oldSelectedInfo = nodeInfo(cards_ && !cards_->selection.empty() ? cards_->selection.front() : selectedId_);
    auto* oldOwner = oldSelectedInfo ? oldSelectedInfo->owner : area_;
    // 新規イベント作成では、新ツリーを設定すると同時に左の論理選択を解
    // 除する。DのappendTreeは作成イベントを選ぶため、再構築
    // で以前の公開所有元行がこの明示的な解除を上書きしないようにする。
    const bool oldOwnerSelection = selectedId_ != 0 && oldSelectedInfo &&
        oldSelectedInfo->kind == NodeInfo::Kind::Owner;
    // 選択Fireの所有EventTreeを、右ペインの表示
    // ツリーと分けて保持する。selectedTree_を
    // 変えず別イベントのFireを選べるうえ、共通履歴の再
    // 適用はFireの復元後に複数回この表示を更新し得る。
    std::optional<FireCondition> oldFireSelection;
    String oldFireTreeId;
    std::optional<FireKind> oldFireKind;
    std::size_t oldFireIndex{};
    if ((selectedId_ != 0 || pendingFireSelection_) && oldSelectedInfo &&
        oldSelectedInfo->kind == NodeInfo::Kind::Fire &&
        oldSelectedInfo->tree && cards_) {
        oldFireSelection = oldSelectedInfo->fire;
        oldFireTreeId = oldSelectedInfo->tree->objectId();
        oldFireKind = oldSelectedInfo->fire.kind;
        for (const auto& row : cards_->nodes) {
            if (row.id == (!cards_->selection.empty() ? cards_->selection.front() : selectedId_)) break;
            const auto* info = nodeInfo(row.id);
            if (info && info->kind == NodeInfo::Kind::Fire &&
                info->tree == oldSelectedInfo->tree && info->fire.kind == *oldFireKind)
                ++oldFireIndex;
        }
    }
    const bool oldContentSelectionActive = contentSelectionActive_;
    const auto oldTreeId = oldTree ? oldTree->objectId() : String{};
    const auto oldContentPath = oldContent ? displayedContentPath(oldContent, nodes_, contentIds_)
                                           : lib::Array<std::size_t>{};
    if (!cards_) return;
    NodeMap nextNodes;
    TreeIdMap nextTreeIds;
    std::vector<lib::TreeNode> nextTreeNodes;
    auto nextId = nextId_;
    TreeItemId nextRootId{};
    TreeItemId nextSelectedId{};
    EventTreePtr nextSelectedTree;
    ContentPtr nextSelectedContent;
    if (!area_) {
        auto publication = lib::prepare_tree_nodes(cards_, {}, {});
        if (!publication.commit()) return;
        nodes_.clear();
        treeIds_.clear();
        contentIds_.clear();
        rootId_ = 0;
        selectedId_ = 0;
        selectedContentId_ = 0;
        contentSelectionActive_ = oldContentSelectionActive;
        selectedTree_.reset();
        selectedContent_.reset();
        rebuildContents();
        return;
    }

    auto owners = etos(*area_);
    // Dはモデルにまだカードが残る削除前通知で、公開す
    // る所有元行を外す。今回の公開だけから除外する。
    if (excludedOwner) std::erase(owners, excludedOwner);
    std::unordered_map<EventTreeOwner*, TreeItemId> ownerIds;
    for (std::size_t ownerIndex = 0; ownerIndex < owners.size(); ++ownerIndex) {
        auto* owner = owners[ownerIndex];
        if (!owner) continue;
        NodeInfo ownerInfo;
        ownerInfo.kind = NodeInfo::Kind::Owner;
        ownerInfo.owner = owner;
        ownerInfo.ownerIndex = ownerIndex;
        const auto ownerId = appendCardNode(
            nextNodes, nextTreeIds, nextTreeNodes, nextId, ownerInfo, 0,
            ownerDisplayName(*owner), true);
        ownerIds.emplace(owner, ownerId);
        if (ownerIndex == 0U) nextRootId = ownerId;

        for (std::size_t index = 0; index < owner->trees().size(); ++index) {
            const auto tree = owner->trees()[index];
            if (!tree) continue;
            NodeInfo info;
            info.kind = NodeInfo::Kind::EventTree;
            info.owner = owner;
            info.tree = tree;
            info.ownerIndex = ownerIndex;
            info.treeIndex = index;
            const auto treeId = appendCardNode(
                nextNodes, nextTreeIds, nextTreeNodes, nextId, info, ownerId,
                eventTreeDisplayLabel(*owner, *tree),
                false);
            appendFireNodes(nextNodes, nextTreeIds, nextTreeNodes, nextId,
                            tree, treeId);
        }
    }


    if (oldTree && nextTreeIds.contains(oldTree.get())) {
        nextSelectedTree = oldTree;
        nextSelectedContent = oldContent;
    } else if (!oldTreeId.empty()) {
        for (auto* owner : owners) {
            if (!owner) continue;
            const auto replacement = std::find_if(
                owner->trees().begin(), owner->trees().end(),
                [&oldTreeId](const auto& tree) {
                    return tree && tree->objectId() == oldTreeId;
                });
            if (replacement == owner->trees().end()) continue;
            nextSelectedTree = *replacement;
            if (oldContent && !oldContentPath.empty())
                nextSelectedContent = nextSelectedTree->fromPath(oldContentPath);
            break;
        }
    }
    nextSelectedId = oldOwnerSelection && oldOwner && ownerIds.contains(oldOwner)
                         ? ownerIds.at(oldOwner)
                         : nextSelectedTree && nextTreeIds.contains(nextSelectedTree.get())
                               ? nextTreeIds.at(nextSelectedTree.get()) : nextRootId;
    if (!nextSelectedTree && !area_->trees().empty()) {
        for (const auto& tree : area_->trees()) {
            if (tree) {
                nextSelectedTree = tree;
                nextSelectedId = nextTreeIds.at(tree.get());
                break;
            }
        }
    }
    const auto fireTreeId = !pendingFireTreeId_.empty()
                                ? pendingFireTreeId_ : oldFireTreeId;
    const auto fireSelection = pendingFireSelection_ ? pendingFireSelection_
                                                       : oldFireSelection;
    bool restoredFire{};
    if (fireSelection && !fireTreeId.empty()) {
        for (const auto& row : nextTreeNodes) {
            const auto& info = nextNodes.at(row.id);
            if (info.kind == NodeInfo::Kind::Fire && info.tree &&
                info.tree->objectId() == fireTreeId &&
                sameFire(info.fire, *fireSelection)) {
                nextSelectedId = row.id;
                restoredFire = true;
                if (pendingFireSelection_) {
                    const auto parent = std::find_if(
                        nextTreeNodes.begin(), nextTreeNodes.end(),
                        [&row](const auto& candidate) {
                            return candidate.id == row.parent;
                        });
                    if (parent != nextTreeNodes.end()) parent->expanded = true;
                }
                break;
            }
        }
    }
    // キーコード編集は選択行の値を変え得る。Dの位置に基づく動作
    // を維持しつつ、検索はそのFire自身のツリー内に限定する。
    if (!restoredFire && oldFireKind) {
        const auto fallbackTreeId = !oldFireTreeId.empty()
                                        ? oldFireTreeId
                                        : nextSelectedTree ? nextSelectedTree->objectId()
                                                           : String{};
        std::size_t index{};
        for (const auto& row : nextTreeNodes) {
            const auto& info = nextNodes.at(row.id);
            if (info.kind != NodeInfo::Kind::Fire || !info.tree ||
                info.tree->objectId() != fallbackTreeId ||
                info.fire.kind != *oldFireKind) continue;
            if (index++ == oldFireIndex) {
                nextSelectedId = row.id;
                break;
            }
        }
    }
    const auto sourceArea = area_;
    auto publication = lib::prepare_tree_nodes(
        cards_, std::move(nextTreeNodes),
        nextSelectedId ? std::vector<TreeItemId>{nextSelectedId}
                       : std::vector<TreeItemId>{},
        nextSelectedId ? std::optional<TreeItemId>{nextSelectedId} : std::nullopt,
        [this, sourceArea] { return area_ == sourceArea; });
    if (!publication.commit()) return;
    pendingFireSelection_.reset();
    pendingFireTreeId_.clear();
    nodes_.swap(nextNodes);
    treeIds_.swap(nextTreeIds);
    nextId_ = nextId;
    rootId_ = nextRootId;
    selectedId_ = nextSelectedId;
    selectedContentId_ = 0;
    contentSelectionActive_ = oldContentSelectionActive;
    selectedTree_ = std::move(nextSelectedTree);
    selectedContent_ = std::move(nextSelectedContent);
    refreshEventTreeImages();
    rebuildContents();
}

void EventView::appendFireNodes(NodeMap& nodes, TreeIdMap& treeIds,
                                std::vector<lib::TreeNode>& treeNodes,
                                TreeItemId& nextId, const EventTreePtr& tree,
                                const TreeItemId treeId) {
    if (!tree) return;
    const auto append = [this, &nodes, &treeIds, &treeNodes, &nextId,
                         &tree, treeId](const FireCondition& fire) {
        NodeInfo info;
        info.kind = NodeInfo::Kind::Fire;
        info.owner = tree->owner() ? tree->owner() : area_;
        info.tree = tree;
        info.fire = fire;
        info.treeIndex = treeIds.contains(tree.get()) ?
            nodes.at(treeIds.at(tree.get())).treeIndex : 0;
        appendCardNode(nodes, treeIds, treeNodes, nextId, info, treeId,
                       fireText(fire, tree));
    };
    if (tree->fireEnter()) append(FireCondition::enter());
    if (tree->fireEscape()) append(FireCondition::escape());
    if (tree->fireLose()) append(FireCondition::lose());
    if (tree->fireEveryRound()) append(FireCondition::everyRound());
    if (tree->fireRoundEnd()) append(FireCondition::roundEnd());
    if (tree->fireRound0()) append(FireCondition::round0());
    for (const auto& keyCode : tree->keyCodes()) append(FireCondition::withKeyCode(keyCode));
    for (const auto round : tree->rounds()) append(FireCondition::withRound(round));
}

void EventView::rebuildContents() {
    if (!contents_) {
        return;
    }
    NodeMap nextNodes = nodes_;
    for (auto it = nextNodes.begin(); it != nextNodes.end();) {
        if (it->second.kind == NodeInfo::Kind::Content) it = nextNodes.erase(it);
        else ++it;
    }
    ContentIdMap nextContentIds;
    std::vector<lib::TreeNode> nextTreeNodes;
    auto nextId = nextId_;
    if (!selectedTree_) {
        auto publication = lib::prepare_tree_nodes(contents_, {}, {});
        const bool committed = publication.commit();
        if (!committed) return;
        nodes_.swap(nextNodes);
        contentIds_.clear();
        selectedContent_.reset();
        selectedContentId_ = 0;
        rebuildContentTable();
        return;
    }
    const auto treeId = findItem(selectedTree_);
    if (!treeId) {
        return;
    }
    auto* selectedOwner = selectedTree_->owner();
    if (!selectedOwner) selectedOwner = area_;
    const auto treeLabel = selectedOwner
        ? eventTreeDisplayLabel(*selectedOwner, *selectedTree_) : messages_.event;
    nextTreeNodes.push_back({treeId, 0, treeLabel, {},
                             contentImage(types::CType::Start), true, false, false});
    std::unordered_set<const event::Content*> visiting;
    for (std::size_t index = 0; index < selectedTree_->starts().size(); ++index) {
        const auto& start = selectedTree_->starts()[index];
        if (start) appendContentNode(nextNodes, nextContentIds, nextTreeNodes,
                                     nextId, start, {}, treeId, index, visiting);
    }
    auto nextSelectedContent = selectedContent_;
    TreeItemId nextSelectedContentId{};
    if (nextSelectedContent && nextContentIds.contains(nextSelectedContent.get())) {
        nextSelectedContentId = nextContentIds.at(nextSelectedContent.get());
    } else if (!selectedTree_->starts().empty() && selectedTree_->starts().front()) {
        nextSelectedContent = selectedTree_->starts().front();
        nextSelectedContentId = nextContentIds.at(nextSelectedContent.get());
    } else {
        nextSelectedContent.reset();
    }
    // コンテントがアクティブな間は、その安定したIDをコマ
    // ンド・クリップボードの選択にも維持する。選択とコピ
    // ーの間の再描画で、右ではコンテントが選ばれているの
    // にEventTree全体を直列化しないようにする。
    const auto nextSelectedId = contentSelectionActive_ && nextSelectedContentId != 0
                                    ? nextSelectedContentId : selectedId_;
    auto publication = lib::prepare_tree_nodes(
        contents_, std::move(nextTreeNodes),
        nextSelectedContentId ? std::vector<TreeItemId>{nextSelectedContentId}
                              : std::vector<TreeItemId>{},
        nextSelectedContentId ? std::optional<TreeItemId>{nextSelectedContentId}
                              : std::nullopt);
    const bool committed = publication.commit();
    if (!committed) return;
    nodes_.swap(nextNodes);
    contentIds_.swap(nextContentIds);
    nextId_ = nextId;
    selectedContent_ = std::move(nextSelectedContent);
    selectedContentId_ = nextSelectedContentId;
    selectedId_ = nextSelectedId;
    rebuildContentTable();
}

TreeItemId EventView::appendContentNode(
    NodeMap& nodes, ContentIdMap& contentIds,
    std::vector<lib::TreeNode>& treeNodes, TreeItemId& nextId,
    const ContentPtr& content, const ContentPtr& parent,
    const TreeItemId parentId, const std::size_t index,
    std::unordered_set<const event::Content*>& visiting) {
    if (!content || visiting.contains(content.get())) return 0;
    visiting.insert(content.get());
    NodeInfo info;
    info.kind = NodeInfo::Kind::Content;
    info.owner = selectedTree_ && selectedTree_->owner()
                     ? selectedTree_->owner() : area_;
    info.tree = selectedTree_;
    info.content = content;
    info.parentContent = parent;
    info.contentIndex = index;
    TreeItemId id{};
    const auto previous = contentIds_.find(content.get());
    if (previous != contentIds_.end()) id = previous->second;
    if (id == 0) id = nextId++;
    nodes.emplace(id, info);
    contentIds[content.get()] = id;
    auto row_text = contentText(content);
    lib::TreeNode node{id, parentId, std::move(row_text), content->detail().type,
                       contentImage(content->type()), true, false,
                       !content->next().empty()};
    // 再描画・改名で、無関係な折りたたみ枝を開かない。正確な有
    // 効な同一性を優先し、履歴復元でオブジェクトが置換された場
    // 合は、ツリーの同一性と構造上のコンテントパスで照合する。
    const lib::TreeNode* oldNode = nullptr;
    if (contents_) {
        const auto found = std::find_if(contents_->nodes.begin(), contents_->nodes.end(),
            [id](const auto& value) { return value.id == id; });
        if (found != contents_->nodes.end()) oldNode = &*found;
        if (!oldNode && selectedTree_) {
            for (const auto& value : contents_->nodes) {
                const auto* old = nodeInfo(value.id);
                if (old && old->content && old->tree &&
                    (old->tree == selectedTree_ || (!selectedTree_->objectId().empty() &&
                     old->tree->objectId() == selectedTree_->objectId())) &&
                    displayedContentPath(old->content, nodes_, contentIds_) == content->ctPath()) {
                    oldNode = &value;
                    break;
                }
            }
        }
    }
    if (oldNode) node.expanded = oldNode->expanded;
    if (comm_.warnings) node.warnings = comm_.warnings(*content);
    if (!node.warnings.empty()) node.warning_icon = comm_.warningImage;
    treeNodes.push_back(std::move(node));
    for (std::size_t childIndex = 0; childIndex < content->next().size(); ++childIndex)
        appendContentNode(nodes, contentIds, treeNodes, nextId,
                          content->next()[childIndex], content, id,
                          childIndex, visiting);
    visiting.erase(content.get());
    return id;
}

void EventView::selectFallback() {
    if (selectedTree_ && treeIds_.contains(selectedTree_.get())) {
        selectedId_ = treeIds_.at(selectedTree_.get());
    } else {
        selectedTree_.reset();
        selectedId_ = rootId_;
    }
    rebuildContents();
}

bool EventView::select(const TreeItemId id) {
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    if (life.expired()) return false;
    const auto* info = nodeInfo(id);
    if (!info || info->kind == NodeInfo::Kind::Content) return false;
    selectedId_ = id;
    contentSelectionActive_ = false;
    // DのselectImplが_etreeを変えるのはEventTree項目だけ。
    // 所有元やFireの選択は左のコマンドを変えるが、右の表示ツリーを終了しない。
    if (info->kind == NodeInfo::Kind::EventTree && info->tree &&
        info->tree != selectedTree_) {
        if (selectedTree_) {
            const auto found = std::find_if(graphRows_.begin(), graphRows_.end(),
                [this](const GraphRow& row) { return row.id == selectedContentId_; });
            const auto offset = eventGraphScroll_ ? eventGraphScroll_->scroll_offset : lib::Point{};
            const auto key = selectedTree_->objectId();
            if (found == graphRows_.end() ||
                (offset.y == 0.0F && found == graphRows_.begin()))
                storedGraphPositions_.erase(key);
            else
                storedGraphPositions_[key] = {offset, static_cast<std::size_t>(found-graphRows_.begin())};
        }
        selectedTree_ = info->tree;
        selectedContent_.reset();
        rebuildContents();
        if (life.expired()) return true;
        const auto editorProps = comm_.editorProps;
        const auto* props = editorProps ? editorProps() : nullptr;
        if (life.expired()) return true;
        const bool restore = props && props->var().restorePositionOfEventTreeView.value;
        const auto saved = storedGraphPositions_.find(selectedTree_->objectId());
        lib::Point offset{};
        if (restore && saved != storedGraphPositions_.end() && !graphRows_.empty()) {
            const auto index = std::min(saved->second.selected_row, graphRows_.size()-1U);
            selectedContent_ = graphRows_[index].content;
            selectedContentId_ = graphRows_[index].id;
            if (contents_) {
                const auto contents = contents_;
                (void)lib::select_tree_items(*contents, {selectedContentId_}, false);
                if (life.expired()) return true;
            }
            if (contentTable_) {
                const auto contentTable = contentTable_;
                (void)lib::select_table_rows(*contentTable, {index}, false, false);
                if (life.expired()) return true;
            }
            offset = saved->second.offset;
        }
        if (eventGraphScroll_) {
            (void)lib::scroll_view_content(eventGraphScroll_, offset);
            if (life.expired()) return true;
            // ネイティブホストが新しいCanvasを配置するまで、要求されたスクロール量を保持する。
            eventGraphScroll_->scroll_offset = offset;
        }
        invalidateEventGraph();
        if (life.expired()) return true;
    }
    refreshEventTreeImages();
    if (life.expired()) return true;
    if (cards_) {
        const auto cards = cards_;
        (void)lib::select_tree_items(*cards, {id}, true);
        if (life.expired()) return true;
    }
    refreshStatusLine();
    if (life.expired()) return true;
    refreshToolbarState();
    if (life.expired()) return true;
    // DのselectImplからkindSelectedへの経路に合わせ、発火の選択肢は所有元に従う。
    refreshIgnitionChoices();
    if (life.expired()) return true;
    statusChanged.call();
    if (!life.expired()) selectionChanged.call();
    return true;
}

bool EventView::selectContent(const TreeItemId id) {
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    if (life.expired()) return false;
    const auto* info = nodeInfo(id);
    if (!info || info->kind != NodeInfo::Kind::Content || !info->content) return false;
    selectedContentId_ = id;
    contentSelectionActive_ = true;
    // クリップボード・コマンドの配送はselectedId_を
    // 使う。イベントツリーのIDを残すと、コンテント行が選択表
    // 示されていてもコピーがイベント全体を直列化してしまう。
    selectedId_ = id;
    selectedContent_ = info->content;
    selectedTree_ = info->tree;
    if (contents_) {
        const auto contents = contents_;
        (void)lib::select_tree_items(*contents, {id}, true, lib::editor_window_host_options().all_mode);
        if (life.expired()) return true;
    }
    if (contentTable_) {
        const auto contentTable = contentTable_;
        const auto found = std::find(tableItems_.begin(), tableItems_.end(), id);
        (void)lib::select_table_rows(*contentTable, found == tableItems_.end()
            ? std::vector<std::size_t>{}
            : std::vector<std::size_t>{static_cast<std::size_t>(
                  std::distance(tableItems_.begin(), found))}, true, lib::editor_window_host_options().all_mode);
        if (life.expired()) return true;
    }
    refreshStatusLine();
    if (life.expired()) return true;
    refreshToolbarState();
    if (life.expired()) return true;
    invalidateEventGraph();
    if (life.expired()) return true;
    revealSelectedEventContent();
    if (life.expired()) return true;
    statusChanged.call();
    if (!life.expired()) selectionChanged.call();
    return true;
}

bool EventView::selection(const std::size_t index) {
    if (!cards_) return false;
    // DのTree.getItem(index)は最上位行を指し、平坦化した子の番号ではない。
    std::size_t rootIndex{};
    for (const auto& row : cards_->nodes) {
        if (row.parent != 0) continue;
        if (rootIndex++ == index) return select(row.id);
    }
    return false;
}

bool EventView::selectVisibleContent(const std::size_t index) {
    if (index >= tableItems_.size()) return false;
    return selectContent(tableItems_[index]);
}

bool EventView::rightPaneActive() const noexcept {
    // SWTのコマンドは、選択が変わっていなくても実際のフォー
    // カスで分岐する。最後のアクティブ選択は、フォーカスを観測
    // できないAPI呼び出し側・バックエンド用の代替とする。
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto gui = comm_.services.gui;
    if (gui) {
        const auto focused = gui->focused_widget();
        // ホストのコールバックはこの表示を終了させ得る。戻った後に
        // ペインのメンバーを読む前に、ローカルの寿命を確認する。
        if (life.expired()) return false;
        if (focused) {
            if (cards_ && !lib::input_scope_path(cards_, focused).empty()) return false;
            if ((eventGraphCanvas_ && !lib::input_scope_path(eventGraphCanvas_, focused).empty()) ||
                (contents_ && !lib::input_scope_path(contents_, focused).empty()) ||
                (contentTable_ && !lib::input_scope_path(contentTable_, focused).empty())) return true;
        }
    }
    return !life.expired() && contentSelectionActive_;
}

ContentPtr EventView::selectionContent() const noexcept {
    if (navigationContent_) return navigationContent_;
    return selectedContent_;
}

ContentPtr EventView::activeSelectionContent() const noexcept {
    if (navigationContent_) return navigationContent_;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const bool active = rightPaneActive();
    return !life.expired() && active ? selectedContent_ : ContentPtr{};
}

TreeItemId EventView::leftOrActiveSelectionId() const noexcept {
    // 共通selection()のIDは、Dの左専用のEventView.selectionと異
    // なり、アクティブペインに従う。DのCardsを使う側は、Contentと独立に解決する。
    return cards_ && !cards_->selection.empty() ? cards_->selection.front() : selectedId_;
}

EventTreeOwner* EventView::selectionParent() const noexcept {
    if (const auto* info = nodeInfo(leftOrActiveSelectionId())) return info->owner;
    return area_;
}

EventTreePtr EventView::selectionEventTree() const noexcept {
    if (const auto* info = nodeInfo(leftOrActiveSelectionId())) return info->tree;
    return {};
}

std::optional<FireCondition> EventView::selectionFire() const {
    // DのEventView.selectionは、右のEventTreeView
    // が別のコンテント選択を保持していても、選択されたCards行を読む。
    if (const auto* info = nodeInfo(leftOrActiveSelectionId());
        info && info->kind == NodeInfo::Kind::Fire)
        return info->fire;
    return std::nullopt;
}

std::vector<std::size_t> EventView::selectionPath() const {
    const auto tree = selectionEventTree();
    if (!tree || !area_) return {};
    const auto owners = etos(*area_);
    for (std::size_t ownerIndex = 0; ownerIndex < owners.size(); ++ownerIndex) {
        const auto* owner = owners[ownerIndex];
        if (!owner) continue;
        const auto it = std::find(owner->trees().begin(), owner->trees().end(), tree);
        if (it == owner->trees().end()) continue;
        const auto treeIndex = static_cast<std::size_t>(
            std::distance(owner->trees().begin(), it));
        return owners.size() == 1U
                   ? std::vector<std::size_t>{treeIndex}
                   : std::vector<std::size_t>{ownerIndex, treeIndex};
    }
    return {};
}

std::vector<std::size_t> EventView::contentPath() const {
    return rightPaneActive() && selectedContent_
               ? selectedContent_->ctPath()
               : std::vector<std::size_t>{};
}

TreeItemId EventView::findItem(const EventTreePtr& tree) const noexcept {
    if (!tree) return 0;
    const auto it = treeIds_.find(tree.get());
    return it == treeIds_.end() ? 0 : it->second;
}

const NodeInfo* EventView::nodeInfo(const TreeItemId id) const noexcept {
    const auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

std::vector<TreeItemId> EventView::search(const StringView text) {
    searchText_ = String(text);
    searchResults_.clear();
    searchCursor_ = 0;
    if (searchText_.empty()) return searchResults_;
    const auto matchesNode = [this](const TreeItemId id, const String& label) {
        const auto it = nodes_.find(id);
        if (it == nodes_.end()) return false;
        const auto& info = it->second;
        if (containsText(label, searchText_)) return true;
        if (info.tree && containsText(info.tree->comment(), searchText_)) return true;
        if (info.owner && containsText(info.owner->commentForEvents(), searchText_)) return true;
        if (info.content && containsText(info.content->comment(), searchText_)) return true;
        return false;
    };
    if (cards_) for (const auto& node : cards_->nodes)
        if (matchesNode(node.id, node.text)) searchResults_.push_back(node.id);
    if (contents_) for (const auto& node : contents_->nodes)
        if (matchesNode(node.id, node.text)) searchResults_.push_back(node.id);
    if (!searchResults_.empty()) {
        const auto id = searchResults_.front();
        if (const auto* info = nodeInfo(id); info && info->kind == NodeInfo::Kind::Content)
            selectContent(id);
        else select(id);
    }
    return searchResults_;
}

bool EventView::searchNext() {
    if (searchResults_.empty()) return false;
    searchCursor_ = (searchCursor_ + 1) % searchResults_.size();
    const auto id = searchResults_[searchCursor_];
    const auto* info = nodeInfo(id);
    if (!info) return false;
    return info->kind == NodeInfo::Kind::Content ? selectContent(id) : select(id);
}

void EventView::clearSearch() noexcept {
    searchText_.clear();
    searchResults_.clear();
    searchCursor_ = 0;
}

String EventView::statusLine() const { return statusText_; }

void EventView::refreshStatusLine() {
    hoveredStatusId_ = 0;
    const auto generation = ++hoverStatusGeneration_;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    if (life.expired()) return;
    const auto selectedId = selectedContentId_;
    const auto* const info = nodeInfo(selectedId);
    const ContentPtr content = info ? info->content : ContentPtr{};
    const auto contentText = comm_.contentText;
    const auto contentName = comm_.contentName;
    const auto status = status_;
    // Dの状態表示は、空も含めcomm.contentTextを直接
    // 使う。共通整形はコールバックがない場合の代替だけを提供する。
    const auto value = formatContentText(content, contentText, contentName, life,
                                         true, true);
    if (!value || life.expired() || selectedContentId_ != selectedId ||
        hoverStatusGeneration_ != generation) {
        return;
    }
    statusText_ = *value;
    if (status) status->text = statusText_;
}

void EventView::refreshHoverStatus(const lib::PointerEvent& pointer) {
    namespace area = ::cwxeditor::cwx::area;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto generation = ++hoverStatusGeneration_;
    const auto clear = [this, life, generation] {
        if (life.expired() || hoverStatusGeneration_ != generation ||
            hoveredStatusId_ == 0U) return;
        // MenuCard・EnemyCardのない行は、選択コンテント
        // の整形処理を使う。更新で世代を進め、入れ子のポインターイベン
        // トやホストのコールバックを、古い状態表示の復元より優先する。
        refreshStatusLine();
        if (life.expired() || hoverStatusGeneration_ != generation + 1U) return;
        statusChanged.call();
    };
    if (life.expired()) return;
    if (pointer.action == lib::PointerAction::Leave ||
        pointer.action == lib::PointerAction::Cancel) { clear(); return; }
    const auto propsProvider = comm_.editorProps;
    if (!propsProvider) { clear(); return; }
    auto* props = propsProvider();
    if (life.expired() || hoverStatusGeneration_ != generation) return;
    if (!props || !props->var().showCardStatusUnderPointer.value) { clear(); return; }

    const auto tree = cards_;
    if (!tree) { clear(); return; }
    TreeItemId id{};
    if (pointer.item_hit_test_available) {
        if (pointer.item_index >= 0) {
            const auto candidate = static_cast<TreeItemId>(pointer.item_index);
            if (std::any_of(tree->nodes.begin(), tree->nodes.end(),
                    [candidate](const auto& node) { return node.id == candidate; }))
                id = candidate;
        }
    } else {
        const auto height = lib::item_row_height(*tree,
            lib::scaled_ui_metric(lib::kEditorUiMetrics.row_height));
        const auto hit = lib::hit_test_tree_row(*tree, tree->bounds(),
                                                pointer.position, height);
        if (hit && *hit < tree->nodes.size()) id = tree->nodes[*hit].id;
    }
    const auto* info = id == 0U ? nullptr : nodeInfo(id);
    if (!info || info->kind != NodeInfo::Kind::Owner) { clear(); return; }

    const auto owner = info->owner;
    const auto model = areaHolder_;
    std::shared_ptr<areaviewutils::AbstractSpCard> card;
    if (const auto ownerArea = std::dynamic_pointer_cast<area::Area>(model)) {
        for (const auto& value : ownerArea->cards())
            if (value && value.get() == owner) { card = value; break; }
    } else if (const auto battle = std::dynamic_pointer_cast<area::Battle>(model)) {
        for (const auto& value : battle->cards())
            if (value && value.get() == owner) { card = value; break; }
    }
    if (!card ||
        (!std::dynamic_pointer_cast<area::MenuCard>(card) &&
         !std::dynamic_pointer_cast<area::EnemyCard>(card))) {
        clear();
        return;
    }

    const auto skinProvider = cardPreviewSkin_;
    const auto summary = comm_.summary.lock();
    const auto services = comm_.services;
    if (!skinProvider) { clear(); return; }
    const auto skin = skinProvider();
    if (life.expired() || hoverStatusGeneration_ != generation) return;
    if (!skin) { clear(); return; }

    areaviewutils::Commons common(*props, *skin);
    common.summary = summary.get();
    common.services = services;
    String value;
    if (const auto menu = std::dynamic_pointer_cast<area::MenuCard>(card))
        value = areaviewutils::createAreaViewStatusLine(
            common, summary.get(), *skin, *menu);
    else if (const auto enemy = std::dynamic_pointer_cast<area::EnemyCard>(card))
        value = areaviewutils::createAreaViewStatusLine(
            common, summary.get(), *skin, *enemy);
    if (life.expired() || hoverStatusGeneration_ != generation) return;
    const auto* current = nodeInfo(id);
    if (areaHolder_ != model || !current ||
        current->kind != NodeInfo::Kind::Owner || current->owner != owner) return;
    if (hoveredStatusId_ == id && statusText_ == value) return;
    hoveredStatusId_ = id;
    statusText_ = std::move(value);
    if (status_) status_->text = statusText_;
    // DのEventViewは、ホバー中カードの状態を選択コンテントと同じ
    // ホスト通知で公開する。このコールバックは現在の表示を終了させ得る。
    statusChanged.call();
}

EventTreePtr EventView::selectedTreeOr(const EventTreePtr& tree) const noexcept {
    return tree ? tree : selectionEventTree();
}

ContentPtr EventView::selectedContentOr(const ContentPtr& content) const noexcept {
    return content ? content : selectedContent_;
}

ContentPtr EventView::canonicalSelectedContent() {
    if (!selectedContent_ || !selectedTree_) return selectedContent_;
    const auto path = displayedContentPath(selectedContent_, nodes_, contentIds_);
    // 取り消し・やり直しや一部のダイアログは、EventTree全体を置換
    // する。selectedTree_内のContentだけを解決し直して
    // も、古いツリーを検索することになる。先に安定したオブジェクトIDで選
    // 択ツリーを有効な所有元へ再接続し、その後コンテントパスを解決する。
    auto* owner = selectedTree_->owner();
    if (!owner) owner = area_;
    if (owner) {
        const auto live = std::find(owner->trees().begin(), owner->trees().end(),
                                    selectedTree_);
        if (live == owner->trees().end()) {
            const auto object_id = selectedTree_->objectId();
            const auto replacement = std::find_if(
                owner->trees().begin(), owner->trees().end(),
                [&object_id](const EventTreePtr& tree) {
                    return tree && tree->objectId() == object_id;
                });
            if (replacement != owner->trees().end()) {
                selectedTree_ = *replacement;
            } else {
                // 旧形式・一時ツリーには安定したオブジェクトIDがない場合があ
                // る。公開済みの所有元行に記録した現在の順番を、SelectionSnapshot::restore
                // と同じ代替として使う。
                const auto published = std::find_if(
                    nodes_.begin(), nodes_.end(),
                    [this, owner](const auto& entry) {
                        const auto& info = entry.second;
                        return info.kind == NodeInfo::Kind::EventTree &&
                               info.owner == owner && info.tree == selectedTree_;
                    });
                if (published != nodes_.end() &&
                    published->second.treeIndex < owner->trees().size())
                    selectedTree_ = owner->trees()[published->second.treeIndex];
                else if (owner->trees().size() == 1U)
                    selectedTree_ = owner->trees().front();
            }
        }
    }
    const auto current = selectedTree_->fromPath(path);
    if (!current) {
        selectedContent_.reset();
        selectedContentId_ = 0;
        if (contentSelectionActive_) selectedId_ = 0;
        contentSelectionActive_ = false;
        return {};
    }
    // contentIds_は最後に描画したツリーを表す。ダイアロ
    // グや取り消し・やり直しがモデルノードを置換しても、古いポイン
    // ターが対応表に残り得る。所属だけで選択が正規だとは判断せず
    // 、変更コマンドごとに有効なEventTreeから解決する。
    selectedContent_ = current;
    if (const auto found = contentIds_.find(current.get()); found != contentIds_.end()) {
        selectedContentId_ = found->second;
        if (contentSelectionActive_) selectedId_ = found->second;
    }
    return current;
}

EventTreeOwner* EventView::selectedOwnerOr(EventTreeOwner* owner) const noexcept {
    return owner ? owner : selectionParent();
}

std::vector<EventView::OwnerSnapshot> EventView::snapshot() const {
    std::vector<OwnerSnapshot> result;
    if (!area_) return result;
    for (auto* owner : etos(*area_)) {
        if (!owner) continue;
        OwnerSnapshot item;
        item.owner = owner;
        item.trees.reserve(owner->trees().size());
        for (const auto& tree : owner->trees()) {
            if (!tree) continue;
            auto saved = tree->dup();
            // Dの取り消し用コピーは下位カウンターへ登録する。外部の改名
            // を保持履歴へ反映し、使用中シナリオの参照としては数えない。
            if (const auto counter = tree->useCounter())
                saved->setUseCounter(counter->sub(),
                    ::cwxeditor::cwx::usecounter::renameInfo(tree));
            item.trees.push_back(std::move(saved));
        }
        result.push_back(std::move(item));
    }
    return result;
}

EventView::SelectionSnapshot EventView::selectionSnapshot(
    const bool useHistorySource) const {
    if (useHistorySource && historySelectionSource_) {
        if (const auto selected = historySelectionSource_()) return *selected;
    }
    SelectionSnapshot result;
    result.contentActive = contentSelectionActive_;
    result.fire = pendingFireSelection_ ? pendingFireSelection_ : selectionFire();
    if (!pendingFireTreeId_.empty()) result.fireTreeId = pendingFireTreeId_;
    else if (result.fire) {
        if (const auto tree = selectionEventTree()) result.fireTreeId = tree->objectId();
    }
    if (!selectedTree_) return result;
    result.treeId = selectedTree_->objectId();
    if (area_) {
        const auto owners = etos(*area_);
        for (std::size_t ownerIndex = 0; ownerIndex < owners.size(); ++ownerIndex) {
            const auto* owner = owners[ownerIndex];
            if (!owner) continue;
            const auto found = std::find(owner->trees().begin(), owner->trees().end(),
                                         selectedTree_);
            if (found == owner->trees().end()) continue;
            result.ownerIndex = ownerIndex;
            result.treeIndex = static_cast<std::size_t>(
                std::distance(owner->trees().begin(), found));
            break;
        }
    }
    if (selectedContent_) result.contentPath = selectedContent_->ctPath();
    return result;
}

void EventView::restore(const std::vector<OwnerSnapshot>& values,
                        const SelectionSnapshot& selection) {
    if (!area_) return;
    // モデルオブジェクトを破棄する前に編集部を閉じる。終了コールバックが
    // 表示を更新して外部編集記録を消し得るため、先にルートをコピーする。
    std::vector<ContentPtr> retired;
    for (const auto& item : values) {
        if (!item.owner) continue;
        for (const auto& tree : item.owner->trees())
            if (tree) for (const auto& start : tree->starts())
                if (start) retired.push_back(start);
    }
    for (const auto& start : retired)
        if (comm_.deletingContent) comm_.deletingContent(start);
    externalContentEdits_.clear();
    for (const auto& item : values) {
        if (!item.owner) continue;
        while (!item.owner->trees().empty())
            item.owner->removeEvent(item.owner->trees().size() - 1);
        for (const auto& value : item.trees)
            if (value) item.owner->add(value->dup());
    }

    restoreSelection(selection);
}

void EventView::restoreHistorySelectionForReplay(const SelectionSnapshot& selection) {
    if (!historyPresentationRetired_) restoreSelection(selection);
}

void EventView::restoreSelection(const SelectionSnapshot& selection) {
    pendingFireSelection_ = selection.fire;
    pendingFireTreeId_ = selection.fireTreeId;
    if (!area_) return;
    EventTreePtr restoredTree;
    const auto owners = etos(*area_);
    auto* selectedOwner = selection.ownerIndex < owners.size()
                              ? owners[selection.ownerIndex] : area_;
    if (!selection.treeId.empty()) {
        restoredTree = findTreeById(selectedOwner, selection.treeId);
        if (!restoredTree) restoredTree = findTreeByObjectId(selection.treeId);
    }
    if (!restoredTree && selectedOwner &&
        selection.treeIndex < selectedOwner->trees().size())
        restoredTree = selectedOwner->trees()[selection.treeIndex];
    selectedTree_ = std::move(restoredTree);
    selectedContent_.reset();
    // Dは左ペインにフォーカスがあってもEventTreeView.selection
    // を保持する。フォーカス範囲を変えずにコンテントパスを復元する。
    if (selectedTree_ && !selection.contentPath.empty())
        selectedContent_ = selectedTree_->fromPath(selection.contentPath);
    contentSelectionActive_ = selection.contentActive && static_cast<bool>(selectedContent_);
    selectedId_ = 0;
    selectedContentId_ = 0;
}

void EventView::pushHistory(lib::Action undo, lib::Action redo, const bool retainPresentation) {
    if (!undo_) return;
    const auto origin = retainPresentation && historyOrigin_ ? historyOrigin_() : std::shared_ptr<void>{};
    const auto replayed = historyReplayed_;
    const auto manager = undo_;
    manager->push([origin, action = std::move(undo), replayed] {
                    if (action) action();
                    if (replayed) replayed();
                },
                [origin, action = std::move(redo), replayed] {
                    if (action) action();
                    if (replayed) replayed();
                });
}

void EventView::retireHistoryPresentation() {
    if (historyPresentationRetired_) return;
    historyPresentationRetired_ = true;
    // 履歴がこのツリーを保持していても、進行中のスクロールバー操作を無効にする。
    if (cards_) { ++cards_->scroll_update_revision; cards_->on_scroll_changed = {}; }
    paletteLifetime_.token.reset();
    if (paletteTarget_) paletteTarget_->owner = nullptr;
    floatingPalette_.reset();
    cancelPendingNameEdit();
    unbindDragDrop();
    editAction_ = {};
    autoOpenContentAction_ = {};
    createEventAction_ = {};
    writeCommentAction_ = {};
    if (localVariables_) localVariables_->dispose();
    localVariables_.reset();
    if (panel_) { panel_->set_enabled(false); panel_->set_visible(false); }
    closeCardPreview(); // バックエンドの終了コールバックが保持ホストを解放し得るため、最後に行う。
}

std::function<void(const std::vector<EventView::OwnerSnapshot>&, const EventView::SelectionSnapshot&)>
EventView::snapshotModelReplay(
    std::function<void(const SelectionSnapshot&)> restorePeers) {
    const auto root = areaHolder_;
    const auto summary = comm_.summary.lock();
    if (!root || !summary) return {};
    const auto scene = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Area>(root);
    const auto battle = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Battle>(root);
    const auto attached = [root, summary, scene, battle] {
        return (scene && std::find(summary->areas().begin(), summary->areas().end(), scene) != summary->areas().end()) ||
               (battle && std::find(summary->battles().begin(), summary->battles().end(), battle) != summary->battles().end());
    };
    if (!attached()) return {};
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto deleting = historyModelDeleting_;
    const auto notify_model = historyModelNotify_;
    return [this, root, attached, life, deleting, notify_model, restorePeers](
            const std::vector<OwnerSnapshot>& values, const SelectionSnapshot& selection) {
        const auto validate = [&] {
            if (!attached()) throw std::logic_error("Event snapshot history owner is no longer in its Summary");
            const auto owners = etos(*root);
            for (const auto& item : values)
                if (!item.owner || std::find(owners.begin(), owners.end(), item.owner) == owners.end())
                    throw std::logic_error("Event snapshot history child owner no longer exists");
        };
        validate();
        std::vector<ContentPtr> retired;
        for (const auto& item : values)
            for (const auto& tree : item.owner->trees())
                if (tree) for (const auto& start : tree->starts())
                    if (start) retired.push_back(start);
        // このディスパッチャーは、モデルの寿命に対して安全なものとしてホストが明
        // 示提供する。編集元Editorの通常のUIコールバックを保持しない。
        if (deleting) for (const auto& start : retired) deleting(start);
        validate(); // ダイアログ終了の監視通知が、所有元を削除している場合がある。
        for (const auto& item : values) {
            while (!item.owner->trees().empty()) item.owner->removeEvent(item.owner->trees().size() - 1);
            for (const auto& tree : item.trees) if (tree) item.owner->add(tree->dup());
        }
        // DのEVUndo::udaは表示更新前に有効な全関連画面を復元する。コピ
        // ーしたコールバックは弱い履歴状態だけを持ち、終了したホストは保持しない。
        if (restorePeers) restorePeers(selection);
        if (!life.expired()) {
            externalContentEdits_.clear();
            restoreSelection(selection);
            notify({}, {}, true);
        } else if (notify_model) notify_model();
    };
}

bool EventView::mutate(const std::function<bool()>& action, const bool useCount,
                       const bool recordUndo) {
    if (readOnly_ || !area_ || !action) return false;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    if (life.expired()) return false;
    const auto beforeSelection = selectionSnapshot();
    const auto before = snapshot();
    const bool action_applied = action();
    if (!action_applied) return false;
    // Editor・ダイアログの監視通知が、ネイティブホストを同期
    // 終了させ得る。終了済み表示を通じて履歴を取得・公開しない。
    if (life.expired()) return false;
    const auto after = snapshot();
    const auto afterSelection = selectionSnapshot();
    if (recordUndo && undo_) {
        const bool dynamicSelection = !lib::editor_window_host_options().all_mode &&
            historyReplaySelectionSource_ && historySelectionRestorer_;
        const auto model_replay = snapshotModelReplay(
            dynamicSelection ? historySelectionRestorer_
                             : std::function<void(const SelectionSnapshot&)>{});
        if (dynamicSelection && model_replay) {
            // 変更時点の前後の選択を固定せず、DのEventView.EVUndo::udb/uda
            // と同様に、取り消し・やり直しごとにフォーカス中の有効
            // な関連画面を取得し、保存パスを復元して交換する。モデ
            // ルの再適用は編集元ウィンドウの寿命から独立させる。
            const auto saved = std::make_shared<SelectionSnapshot>(beforeSelection);
            const auto source = historyReplaySelectionSource_;
            const auto replay = [saved, source, model_replay](
                const std::vector<OwnerSnapshot>& values) {
                const auto next = source();
                const auto target = *saved;
                model_replay(values, target);
                if (next) *saved = *next;
            };
            pushHistory([replay, before] { replay(before); },
                        [replay, after] { replay(after); }, false);
        } else {
            pushHistory(
                [this, before, beforeSelection, model_replay] {
                    if (model_replay) model_replay(before, beforeSelection);
                    else { restore(before, beforeSelection); notify({}, {}, true); }
                },
                [this, after, afterSelection, model_replay] {
                    if (model_replay) model_replay(after, afterSelection);
                    else { restore(after, afterSelection); notify({}, {}, true); }
                }, !model_replay);
        }
    }
    if (life.expired()) return true;
    notify({}, {}, useCount);
    return true;
}

bool EventView::editEventTree(const std::function<bool()>& action) {
    return mutate(action, true, true);
}

std::function<void()> EventView::historyOwnerGuard(EventTreeOwner* owner) const {
    const auto model = areaHolder_;
    const auto summary = comm_.summary.lock();
    const auto scene = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Area>(model);
    const auto battle = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Battle>(model);
    if (!model || !summary || (!scene && !battle)) return {};
    const bool initially_attached = scene
        ? std::find(summary->areas().begin(), summary->areas().end(), scene) != summary->areas().end()
        : std::find(summary->battles().begin(), summary->battles().end(), battle) != summary->battles().end();
    if (!initially_attached) return {};
    const auto validate = [model, summary, scene, battle, owner] {
        const bool attached = scene
            ? std::find(summary->areas().begin(), summary->areas().end(), scene) != summary->areas().end()
            : std::find(summary->battles().begin(), summary->battles().end(), battle) != summary->battles().end();
        if (!attached) throw std::logic_error("Event history root no longer exists");
        const auto owners = etos(*model);
        if (!owner || std::find(owners.begin(), owners.end(), owner) == owners.end())
            throw std::logic_error("Event history child owner no longer exists");
    };
    validate();
    return validate;
}

bool EventView::mutateSubtree(const ContentPtr& content,
                               const std::function<bool()>& action) {
    if (readOnly_ || !area_ || !content || !action) return false;
    const std::weak_ptr<const int> author_life = paletteLifetime_.token;
    if (author_life.expired()) return false;
    const auto tree = content->tree();
    auto* const owner = tree ? tree->owner() : nullptr;
    if (!owner) return false;
    const auto found = std::find(owner->trees().begin(), owner->trees().end(), tree);
    if (found == owner->trees().end()) return false;
    const auto treeIndex = static_cast<std::size_t>(found - owner->trees().begin());
    const auto treeId = tree->objectId();
    const auto path = content->ctPath();
    const auto model_guard = historyOwnerGuard(owner);
    const auto deleting = model_guard ? historyModelDeleting_ : comm_.deletingContent;
    const auto notify_model = historyModelNotify_;
    const auto beforeSelection = selectionSnapshot();
    const auto before = content->dup();
    // Dの親付きの並べ替えは、兄弟へ通知する前に親だけを保存する。
    if (!action()) return false;
    if (author_life.expired()) return false;
    const auto after = content->dup();
    const auto afterSelection = selectionSnapshot();
    const auto restoreContent = [this, owner, treeId, treeIndex, path, model_guard, author_life, deleting, notify_model](
                                    const ContentPtr& saved,
                                    const SelectionSnapshot& selection) {
        if (model_guard) model_guard();
        auto liveTree = findTreeById(owner, treeId);
        if (!liveTree && treeId.empty() && treeIndex < owner->trees().size())
            liveTree = owner->trees()[treeIndex];
        const auto current = liveTree ? liveTree->fromPath(path) : ContentPtr{};
        if (!current || !saved || path.empty()) {
            if (model_guard) throw std::logic_error("Event subtree history target no longer exists");
            return;
        }
        if (deleting) deleting(current);
        if (model_guard) model_guard();
        if (std::find(owner->trees().begin(), owner->trees().end(), liveTree) == owner->trees().end() ||
            liveTree->fromPath(path) != current) {
            if (model_guard) throw std::logic_error("Event subtree history changed during deletion notification");
            return;
        }
        const auto copy = saved->dup();
        if (const auto parent = current->parent()) {
            // 正確な履歴復元なので、正規化しないオーバーロードを使う。
            parent->insert(path.back(), copy);
            parent->remove(current);
        } else {
            // コアの最後のStartを残す不変条件を守るため、先に挿入する。
            liveTree->insert(path.front(), copy);
            liveTree->remove(current);
        }
        if (!author_life.expired()) {
            restoreSelection(selection);
            notify(liveTree, copy, true);
        } else if (notify_model) notify_model();
    };
    if (undo_) pushHistory(
        [restoreContent, before, beforeSelection] { restoreContent(before, beforeSelection); },
        [restoreContent, after, afterSelection] { restoreContent(after, afterSelection); }, !model_guard);
    notify(tree, content, true);
    return true;
}

bool EventView::replaceEventContents(const EventTreePtr& tree,
                                      const std::vector<ContentPtr>& starts) {
    if (readOnly_ || !area_ || !tree || starts.empty()) return false;
    const std::weak_ptr<const int> author_life = paletteLifetime_.token;
    if (author_life.expired()) return false;
    auto* const owner = tree->owner();
    if (!owner) return false;
    const auto owners = etos(*area_);
    if (std::find(owners.begin(), owners.end(), owner) == owners.end()) return false;
    const auto found = std::find(owner->trees().begin(), owner->trees().end(), tree);
    if (found == owner->trees().end()) return false;
    for (const auto& start : starts)
        if (!start || start->type() != types::CType::Start) return false;
    const auto copyStarts = [](const auto& values) {
        std::vector<ContentPtr> copies;
        for (const auto& value : values)
            copies.push_back(duplicateHistorySnapshot(value));
        return copies;
    };
    const auto treeIndex = static_cast<std::size_t>(found - owner->trees().begin());
    const auto treeId = tree->objectId();
    const auto model_guard = historyOwnerGuard(owner);
    const auto deleting = model_guard ? historyModelDeleting_ : comm_.deletingContent;
    const auto notify_model = historyModelNotify_;
    const auto beforeSelection = selectionSnapshot();
    const auto before = copyStarts(tree->starts());
    const auto retired = tree->starts();
    for (const auto& start : retired) {
        if (deleting) deleting(start);
        if (author_life.expired()) return false;
        if (model_guard) model_guard();
    }
    if (tree->owner() != owner || tree->starts() != retired) return false;
    tree->starts(starts);
    selectedTree_ = tree;
    selectedContent_.reset();
    contentSelectionActive_ = false;
    const auto afterSelection = selectionSnapshot();
    const auto after = copyStarts(tree->starts());
    const auto restoreContents = [this, owner, treeId, treeIndex, copyStarts, model_guard, author_life, deleting, notify_model](
                                      const std::vector<ContentPtr>& saved,
                                      const SelectionSnapshot& selection) {
        if (model_guard) model_guard();
        auto liveTree = findTreeById(owner, treeId);
        if (!liveTree && treeId.empty() && treeIndex < owner->trees().size())
            liveTree = owner->trees()[treeIndex];
        if (!liveTree) {
            if (model_guard) throw std::logic_error("Event contents history tree no longer exists");
            return;
        }
        const auto retired = liveTree->starts();
        for (const auto& start : retired) {
            if (deleting) deleting(start);
            if (model_guard) model_guard();
        }
        if (std::find(owner->trees().begin(), owner->trees().end(), liveTree) == owner->trees().end() ||
            liveTree->starts() != retired) {
            if (model_guard) throw std::logic_error("Event contents history changed during deletion notification");
            return;
        }
        liveTree->starts(copyStarts(saved));
        if (!author_life.expired()) {
            restoreSelection(selection);
            notify(liveTree, {}, true);
        } else if (notify_model) notify_model();
    };
    if (undo_) pushHistory(
        [restoreContents, before, beforeSelection] { restoreContents(before, beforeSelection); },
        [restoreContents, after, afterSelection] { restoreContents(after, afterSelection); }, !model_guard);
    notify(tree, {}, true);
    return true;
}

bool EventView::convertStartToPackage(const ContentPtr& start,
                                      const std::function<std::uint64_t()>& createPackage) {
    if (readOnly_ || !area_ || !start || !createPackage ||
        start->type() != types::CType::Start) return false;
    const auto tree = start->tree();
    auto* const owner = tree ? tree->owner() : nullptr;
    if (!owner) return false;
    const auto owners = etos(*area_);
    if (std::find(owners.begin(), owners.end(), owner) == owners.end()) return false;
    const auto treeAt = std::find(owner->trees().begin(), owner->trees().end(), tree);
    const auto at = std::find(tree->starts().begin(), tree->starts().end(), start);
    if (treeAt == owner->trees().end() || at == tree->starts().end()) return false;
    const auto treeIndex = static_cast<std::size_t>(treeAt - owner->trees().begin());
    const auto index = static_cast<std::size_t>(at - tree->starts().begin());
    const auto treeId = tree->objectId();
    const bool top = index == 0U;
    std::vector<ContentPtr> affected{start}, pending;
    for (const auto& root : tree->starts()) if (root != start) pending.push_back(root);
    while (!pending.empty()) {
        const auto c = pending.back(); pending.pop_back();
        if (!c) continue;
        if ((c->type() == types::CType::LinkStart || c->type() == types::CType::CallStart) &&
            c->start() == start->name()) affected.push_back(c);
        pending.insert(pending.end(), c->next().begin(), c->next().end());
    }
    std::vector<std::vector<std::size_t>> paths;
    std::vector<ContentPtr> before;
    for (const auto& c : affected) { paths.push_back(c->ctPath()); before.push_back(c->dup()); }
    const auto beforeSelection = selectionSnapshot();
    const auto model_guard = historyOwnerGuard(owner);
    const auto deleting = model_guard ? historyModelDeleting_ : comm_.deletingContent;
    const auto notify_model = historyModelNotify_;
    // Dは作成処理がドック・行選択へ再入する前にUndoCPを構築する。
    const auto lifetime = std::weak_ptr<const int>(paletteLifetime_.token);
    const auto packageId = createPackage();
    if (lifetime.expired() || packageId == 0U) return false;
    if (model_guard) model_guard(); // 作成処理がSceneを切り離している場合がある。
    if (tree->owner() != owner || index >= tree->starts().size() ||
        tree->starts()[index] != start) return false;
    for (std::size_t i = 0; i < affected.size(); ++i)
        if (tree->fromPath(paths[i]) != affected[i]) return false;
    const auto children = start->next();
    const auto original_roots = tree->starts();
    const auto retiring = top ? std::vector<ContentPtr>(children.rbegin(), children.rend())
                              : std::vector<ContentPtr>{start};
    for (const auto& value : retiring) {
        if (deleting) deleting(value);
        if (lifetime.expired()) return false;
        if (model_guard) model_guard();
    }
    if (tree->owner() != owner || tree->starts() != original_roots || start->next() != children)
        return false;
    for (std::size_t i = 0; i < affected.size(); ++i)
        if (tree->fromPath(paths[i]) != affected[i]) return false;
    ContentPtr link;
    if (top) {
        for (auto it = children.rbegin(); it != children.rend(); ++it) start->remove(*it);
        link = std::make_shared<event::Content>(types::CType::LinkPackage);
        link->packages(packageId);
        start->add(link);
    } else {
        tree->remove(start);
    }
    for (std::size_t i = 1; i < affected.size(); ++i) {
        const auto& c = affected[i];
        const auto type = c->type() == types::CType::LinkStart
            ? types::CType::LinkPackage : types::CType::CallPackage;
        if (comm_.deletingContent) comm_.deletingContent(c);
        c->convertType(type, {}, comm_.modelProps);
        c->packages(packageId);
    }
    selectedTree_ = tree;
    selectedContent_ = link;
    contentSelectionActive_ = static_cast<bool>(link);
    const auto afterSelection = selectionSnapshot();
    std::vector<ContentPtr> after;
    for (const auto& c : affected) after.push_back(c->dup());
    const auto restoreConversion = [this, owner, treeId, treeIndex, index, top, paths,
                                    model_guard, lifetime, deleting, notify_model](
                                        const std::vector<ContentPtr>& saved,
                                        const SelectionSnapshot& selection, bool redo) {
        if (model_guard) model_guard();
        auto liveTree = findTreeById(owner, treeId);
        if (!liveTree && treeId.empty() && treeIndex < owner->trees().size())
            liveTree = owner->trees()[treeIndex];
        const auto invalid = [&] {
            if (model_guard) throw std::logic_error("Event conversion history target changed");
        };
        if (!liveTree || saved.empty() || saved.size() != paths.size()) { invalid(); return; }
        const auto roots = liveTree->starts();
        if ((!top && !redo && index > roots.size()) ||
            (!top && redo && index >= roots.size())) { invalid(); return; }
        // 一部でも置換を公開する前に、全入力項目を検証する。UndoDelete
        // が削除Startを戻してからUndoContentがパスを
        // 解釈するため、元の項目を現在のルートベクターへ対応付ける。
        std::vector<ContentPtr> targets;
        targets.reserve(paths.size());
        for (std::size_t i = 0; i < paths.size(); ++i) {
            auto path = paths[i];
            if (!saved[i] || path.empty()) { invalid(); return; }
            if (!top && !redo && path.front() == index) {
                if (i != 0U || path.size() != 1U) { invalid(); return; }
                targets.push_back({}); // Startは下記で挿入する。
                continue;
            }
            if (!top && !redo && path.front() > index) --path.front();
            const auto current = liveTree->fromPath(path);
            if (!current) { invalid(); return; }
            targets.push_back(current);
        }
        for (const auto& current : targets) {
            if (current && deleting) deleting(current);
            if (model_guard) model_guard();
        }
        if (findTreeById(owner, treeId) != liveTree || liveTree->starts() != roots) {
            invalid(); return;
        }
        for (std::size_t i = 0; i < paths.size(); ++i) {
            if (!targets[i]) continue;
            auto path = paths[i];
            if (!top && !redo && path.front() > index) --path.front();
            if (liveTree->fromPath(path) != targets[i]) { invalid(); return; }
        }
        if (!top && !redo) liveTree->insert(index, saved.front()->dup());
        for (std::size_t i = 0; i < saved.size(); ++i) {
            if (!top && !redo && i == 0U) continue;
            const auto current = liveTree->fromPath(paths[i]);
            const auto copy = saved[i]->dup();
            if (const auto parent = current->parent()) {
                parent->insert(paths[i].back(), copy);
                parent->remove(current);
            } else {
                liveTree->insert(paths[i].front(), copy);
                liveTree->remove(current);
            }
        }
        if (!top && redo) liveTree->remove(liveTree->starts()[index]);
        if (!lifetime.expired()) {
            restoreSelection(selection);
            notify(liveTree, {}, true);
        } else if (notify_model) notify_model();
    };
    if (undo_) pushHistory(
        [restoreConversion, before, beforeSelection] { restoreConversion(before, beforeSelection, false); },
        [restoreConversion, after, afterSelection] { restoreConversion(after, afterSelection, true); }, !model_guard);
    notify(tree, link, true);
    return true;
}

bool EventView::beginExternalContentEdit(const ContentPtr& content) {
    if (readOnly_ || !area_) {
        endExternalContentEdit(content);
        return false;
    }
    if (!content || externalContentEdits_.contains(content.get())) return false;
    auto savedFields = content->dup();
    if (const auto counter = content->useCounter())
        savedFields->setUseCounter(counter->sub(),
            ::cwxeditor::cwx::usecounter::renameInfo(content));
    externalContentEdits_.emplace(
        content.get(), ExternalContentEdit{
            content, pendingContentCreates_.contains(content.get()) ? snapshot()
                : std::vector<OwnerSnapshot>{}, selectionSnapshot(), std::move(savedFields),
            pendingContentCreates_.contains(content.get())});
    return true;
}

void EventView::refreshEventTreeImages() {
    if (!cards_) return;
    for (auto& node : cards_->nodes) {
        const auto found = nodes_.find(node.id);
        if (found == nodes_.end() ||
            found->second.kind != NodeInfo::Kind::EventTree ||
            !found->second.tree)
            continue;
        const bool is_and = found->second.tree->keyCodeMatchingType() ==
                            types::MatchingType::And;
        const bool selected = prop_.etc.specifySelectedEventTree && selectedTree_ &&
                              selectedTree_.get() == found->second.tree.get();
        node.icon = viewImage(selected
                                  ? (is_and ? "event_tree_and_selected"
                                            : "event_tree_selected")
                                  : (is_and ? "event_tree_and" : "event_tree"),
                              types::CType::Start);
    }
    if (comm_.services.gui) comm_.services.gui->request_redraw(cards_);
}

void EventView::recordExternalChildNames(const ContentPtr& content,
    const std::vector<std::pair<ContentPtr, String>>& changes) {
    const auto edit = content ? externalContentEdits_.find(content.get())
                             : externalContentEdits_.end();
    if (edit == externalContentEdits_.end()) return;
    // 変更前の保存状態には、今回の適用が実際に変える部分だけを含める。
    edit->second.child_names_before.clear();
    for (const auto& [child, old_name] : changes)
        edit->second.child_names_before.emplace_back(child, old_name);
}

bool EventView::commitExternalContentEdit(const ContentPtr& content) {
    const auto edit = content ? externalContentEdits_.find(content.get())
                              : externalContentEdits_.end();
    if (readOnly_ || !area_ || edit == externalContentEdits_.end()) return false;

    auto before = edit->second.before;
    auto beforeSelection = edit->second.before_selection;
    if (!edit->second.creating) {
        // モードレス編集部は他の編集をまたいで開いたままの場合がある
        // 。現在の参照構造を基準に、このコンテントの編集項目だけを
        // 置き換える。ダイアログを開いた時点の構造全体へ戻さない。
        const auto tree = content->tree();
        if (!tree || tree->fromPath(content->ctPath()) != content) return false;
        before = snapshot();
        beforeSelection = selectionSnapshot();
        bool found = false;
        for (auto& owner : before) for (auto& saved_tree : owner.trees) {
            if (!saved_tree || saved_tree->objectId() != tree->objectId()) continue;
            if (auto saved = saved_tree->fromPath(content->ctPath())) {
                saved->shallowCopy(*edit->second.fields_before);
                // 編集可能なオブジェクトIDや古い編集用インデックスではなく、有
                // 効な同一性と現在のパスに従う。途中の無関係な編集を維持する。
                for (const auto& [weak_child, old_name] : edit->second.child_names_before) {
                    const auto child = weak_child.lock();
                    if (!child || child->parent() != content || child->tree() != tree ||
                        tree->fromPath(child->ctPath()) != child) continue;
                    if (const auto saved_child = saved_tree->fromPath(child->ctPath()))
                        saved_child->setName(old_name); // 変更前の値が空であることも正確に保持する。
                }
                found = true;
            }
        }
        if (!found) return false;
    }
    const auto after = snapshot();
    const auto afterSelection = selectionSnapshot();
    if (undo_) {
        // モードレスの適用も、通常変更と同じモデル所有の共有履
        // 歴に参加する。閉じた編集元を経由して再適用しない。
        const auto model_replay = snapshotModelReplay();
        pushHistory(
            [this, before, beforeSelection, model_replay] {
                if (model_replay) model_replay(before, beforeSelection);
                else { restore(before, beforeSelection); notify({}, {}, true); }
            },
            [this, after, afterSelection, model_replay] {
                if (model_replay) model_replay(after, afterSelection);
                else { restore(after, afterSelection); notify({}, {}, true); }
            }, !model_replay);
    }
    // 次の適用は、今回公開した直後の状態を基準にする。
    edit->second.child_names_before.clear();
    edit->second.before.clear();
    edit->second.before_selection = afterSelection;
    edit->second.fields_before = content->dup();
    if (const auto counter = content->useCounter())
        edit->second.fields_before->setUseCounter(counter->sub(),
            ::cwxeditor::cwx::usecounter::renameInfo(content));
    edit->second.creating = false;
    notify({}, {}, true);
    return true;
}

bool EventView::cancelExternalContentEdit(const ContentPtr& content) {
    const auto edit = content ? externalContentEdits_.find(content.get())
                              : externalContentEdits_.end();
    if (!area_ || edit == externalContentEdits_.end()) return false;
    const auto before = edit->second.before;
    const auto beforeSelection = edit->second.before_selection;
    const auto fields = edit->second.fields_before;
    const bool creating = edit->second.creating;
    externalContentEdits_.erase(edit);
    if (creating) restore(before, beforeSelection);
    else if (const auto tree = content->tree();
             tree && tree->fromPath(content->ctPath()) == content && fields)
        content->shallowCopy(*fields);
    notify({}, {}, true);
    return true;
}

bool EventView::hasPendingContentCreate(const ContentPtr& content) const noexcept {
    if (!content) return !pendingContentCreates_.empty();
    return pendingContentCreates_.contains(content.get());
}

bool EventView::commitPendingContentCreate(const ContentPtr& content) {
    const auto found = content ? pendingContentCreates_.find(content.get())
                               : pendingContentCreates_.end();
    if (found == pendingContentCreates_.end() ||
        !beginExternalContentEdit(content)) return false;
    auto pending = std::move(found->second);
    pendingContentCreates_.erase(found);
    if (!pending.apply || !pending.apply()) {
        endExternalContentEdit(content);
        return false;
    }
    if (commitExternalContentEdit(content)) return true;
    // 確定失敗時に、取り消しの基準状態を持たない挿入を残さない。
    (void)cancelExternalContentEdit(content);
    return false;
}

bool EventView::cancelPendingContentCreate(const ContentPtr& content) noexcept {
    if (!content) {
        if (pendingContentCreates_.empty()) return false;
        pendingContentCreates_.clear();
        return true;
    }
    if (pendingContentCreates_.erase(content.get()) == 0U) return false;
    endExternalContentEdit(content);
    return true;
}

void EventView::endExternalContentEdit(const ContentPtr& content) noexcept {
    if (content) externalContentEdits_.erase(content.get());
}

void EventView::notify(EventTreePtr tree, ContentPtr content, const bool useCount) {
    // 同期した関連画面の更新が、このネイティブホストを閉じ得る。
    // 各公開後にメンバーへ触る前に、共通の表示寿命を確認する。
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    if (life.expired()) return;
    refresh();
    if (life.expired()) return;
    if (!tree) tree = selectedTree_;
    if (!content) content = selectedContent_;
    modelChanged.call();
    if (life.expired()) return;
    if (tree) {
        eventTreeChanged.call(tree);
    }
    if (life.expired()) return;
    if (content) {
        contentChanged.call(content);
    }
    if (life.expired()) return;
    if (useCount) {
        comm_.refUseCount.call();
    }
    if (life.expired()) return;
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

bool EventView::canCreateEvent() const noexcept {
    if (readOnly_ || !area_ || !selectionParent()) return false;
    // DはClassicシナリオへのプレイヤーカードイベント追加を許可しない。
    if (!lib::editor_window_host_options().all_mode &&
        dynamic_cast<::cwxeditor::cwx::area::PlayerCardEvents*>(selectionParent()))
        if (const auto summary = comm_.summary.lock(); summary && summary->legacy())
            return false;
    return true;
}

bool EventView::canAddFire() const {
    // DのAddFireはNewEventではない。所有元の選択を変えない。
    if (readOnly_ || !area_ || !selectionEventTree()) return false;
    const auto fire = ignitionFromControls();
    if (!fire) return false;
    // Dでは、バトルの敵カードにラウンド発火を設定できない。
    namespace area = ::cwxeditor::cwx::area;
    if (dynamic_cast<area::Battle*>(area_) &&
        dynamic_cast<area::EnemyCard*>(selectionParent()) &&
        fire->kind == FireKind::Round)
        return false;
    return true;
}

void EventView::createEvent() {
    if (!lib::editor_window_host_options().all_mode && !canCreateEvent()) return;
    if (createEventAction_) {
        createEventAction_();
        return;
    }
    createEventWithIgnition();
}

bool EventView::canCreateEventWithIgnition() const {
    return canCreateEvent() && ignitionFromControls().has_value();
}

void EventView::createEventWithIgnition() {
    // 通常作成もテンプレート作成も、GUIで選択された発火条件を必要とする。
    const auto ignition = ignitionFromControls();
    if (!ignition) return;
    (void)createEventWithIgnition({}, ignition);
}

EventTreePtr EventView::createEventWithIgnition(const std::vector<ContentPtr>& starts) {
    // DはテンプレートStartにもaddingFireを呼ぶ。存在する
    // が不正なキー・ラウンドを黙ってEnterへ変えない。操作部がなけれ
    // ば指定発火条件もない。ヘッドレスホストも同じ構築済み操作部を使う。
    const auto ignition = ignitionFromControls();
    if (!ignition) return {};
    return createEventWithIgnition(starts, ignition);
}

EventTreePtr EventView::createEventWithIgnition(
    const std::vector<ContentPtr>& starts,
    const std::optional<FireCondition>& ignition) {
    if (!canCreateEvent()) return {};
    EventTreeOwner* owner = selectionParent();
    if (!owner) owner = area_;
    for (const auto& start : starts)
        if (!start || start->type() != types::CType::Start) return {};
    if (!owner) return {};
    const auto fire = ignition.value_or(FireCondition::enter());
    // Dのeventview.dのcreateEventWithIgnition
    // は、テンプレートStartがなければnew EventTree(treeName)
    // を作る。Stringコンストラクターが発火名（メニューカードなら「
    // クリック」など）のStartを追加し、新イベントを表示可能にする。
    auto tree = starts.empty()
        ? std::make_shared<event::EventTree>(eventTreeNameForIgnition(*owner, fire))
        : std::make_shared<event::EventTree>(starts);
    // Dは名前付きとテンプレートStartの両ツリーへ、選択した発火条件を適用する。
    switch (fire.kind) {
    case FireKind::Enter:
        if (owner->canHasFireEnter()) tree->enter(true);
        break;
    case FireKind::Escape:
        if (owner->canHasFireEscape()) tree->escape(true);
        break;
    case FireKind::Lose:
        if (owner->canHasFireLose()) tree->lose(true);
        break;
    case FireKind::EveryRound:
        if (owner->canHasFireEveryRound()) tree->everyRound(true);
        break;
    case FireKind::RoundEnd:
        if (owner->canHasFireRoundEnd()) tree->roundEnd(true);
        break;
    case FireKind::Round0:
        if (owner->canHasFireRound0()) tree->round0(true);
        break;
    case FireKind::KeyCode:
        if (owner->canHasFireKeyCode()) tree->addKeyCode(fire.keyCode);
        break;
    case FireKind::Round:
        if (owner->canHasFireRound()) tree->addRound(fire.round);
        break;
    }
    return appendTree(std::move(tree), static_cast<std::size_t>(-1), owner);
}

namespace {
enum class FireNamePurpose { ReservedStartName, IgnitionTitle };

[[nodiscard]] String dFKeyCodeAggregate(const event::FKeyCode& value) {
    // DのEventView.eventTreeNamesはFKeyCode自身を%sへ渡し、
    // 固定したDMDは既定の集成体をFKeyCode("keyCode", FKCKind)
    // として描く。新規ツリーの発火タイトルはキー文字列だけを渡すため、規則は分けて維持する。
    String result{"FKeyCode("};
    result += ::cwxeditor::cwx::utils::detail::quoteDFormatString(value.keyCode);
    result += ", ";
    result += ::cwxeditor::cwx::enumutils::enumToStringExact(value.kind);
    result.push_back(')');
    return result;
}

String eventTreeFireName(const Props& prop, EventTreeOwner* owner,
                         const FireCondition& fire,
                         const FireNamePurpose purpose) {
    const pane::EventTreeMessages messages = prop.eventTreeMessagesProvider
        ? prop.eventTreeMessagesProvider()
        : pane::EventTreeMessages{prop.msgs.enterTree, prop.msgs.selectTree,
                            prop.msgs.deadTree, prop.msgs.victoryTree,
                            prop.msgs.packageTree, prop.msgs.playerCard,
                            prop.msgs.useTree, prop.msgs.escapeTree,
                            prop.msgs.loseTree, prop.msgs.everyRoundTree,
                            prop.msgs.roundEndTree, prop.msgs.round0Tree,
                            prop.msgs.keyCodeTree, prop.msgs.roundTree};
    switch (fire.kind) {
    case FireKind::Enter:
        if (owner) {
            if (dynamic_cast<::cwxeditor::cwx::area::MenuCard*>(owner))
                return messages.selectTree;
            if (dynamic_cast<::cwxeditor::cwx::area::EnemyCard*>(owner))
                return messages.deadTree;
            if (dynamic_cast<::cwxeditor::cwx::area::Area*>(owner))
                return messages.enterTree;
            if (dynamic_cast<::cwxeditor::cwx::area::Battle*>(owner))
                return messages.victoryTree;
            if (dynamic_cast<::cwxeditor::cwx::area::Package*>(owner))
                return messages.packageTree;
            if (dynamic_cast<::cwxeditor::cwx::area::PlayerCardEvents*>(owner))
                return purpose == FireNamePurpose::ReservedStartName
                    ? messages.playerCard : messages.deadTree;
        }
        return messages.useTree;
    case FireKind::Escape: return messages.escapeTree;
    case FireKind::Lose: return messages.loseTree;
    case FireKind::EveryRound: return messages.everyRoundTree;
    case FireKind::RoundEnd: return messages.roundEndTree;
    case FireKind::Round0: return messages.round0Tree;
    case FireKind::KeyCode: {
        const auto value = purpose == FireNamePurpose::ReservedStartName
            ? dFKeyCodeAggregate(fire.keyCode) : fire.keyCode.keyCode;
        return ::cwxeditor::cwx::utils::tryFormat(messages.keyCodeTree, value);
    }
    case FireKind::Round:
        return ::cwxeditor::cwx::utils::tryFormat(
            messages.roundTree, fire.round);
    }
    return {};
}
} // namespace

String EventView::eventTreeNameForIgnition(EventTreeOwner& owner,
                                            const FireCondition& fire) const {
    return eventTreeFireName(prop_, &owner, fire, FireNamePurpose::IgnitionTitle);
}

String EventView::eventTreeDisplayName(EventTreeOwner& owner,
                                       const event::EventTree& tree) const {
    if (tree.fireEnter())
        return eventTreeNameForIgnition(owner, FireCondition::enter());
    if (tree.fireEscape())
        return eventTreeNameForIgnition(owner, FireCondition::escape());
    if (tree.fireLose())
        return eventTreeNameForIgnition(owner, FireCondition::lose());
    if (tree.fireEveryRound())
        return eventTreeNameForIgnition(owner, FireCondition::everyRound());
    if (tree.fireRoundEnd())
        return eventTreeNameForIgnition(owner, FireCondition::roundEnd());
    if (tree.fireRound0())
        return eventTreeNameForIgnition(owner, FireCondition::round0());
    if (!tree.keyCodes().empty())
        return eventTreeNameForIgnition(
            owner, FireCondition::withKeyCode(tree.keyCodes().front()));
    if (!tree.rounds().empty())
        return eventTreeNameForIgnition(
            owner, FireCondition::withRound(tree.rounds().front()));
    return messages_.event;
}

String EventView::eventTreeDisplayLabel(EventTreeOwner& owner,
                                        const event::EventTree& tree) const {
    // EventTree::nameはDと同じく最初のStartを直接参照する。
    // ただし読み込んだ空ツリーも表示可能にし、更新中に落ちず、EventTreeView.create
    // 自身の空ツリー判定で編集を拒否できるようにする。
    if (tree.starts().empty() || !tree.starts().front())
        return eventTreeDisplayName(owner, tree);
    const auto& name = tree.name();
    return name.empty() ? eventTreeDisplayName(owner, tree) : name;
}

EventTreePtr EventView::appendTree(EventTreePtr tree, const std::size_t index,
                                   EventTreeOwner* explicit_owner) {
    if (!tree || !canCreateEvent()) return {};
    auto* owner = explicit_owner ? explicit_owner : selectionParent();
    if (!owner) owner = area_;
    if (!owner) return {};
    // モーダルのコールバックが元の選択子を終了・削除し得る。まだ
    // 所有されている親だけに挿入し、新しい選択へ振り替えない。
    if (explicit_owner) {
        const auto owners = etos(*area_);
        if (std::find(owners.begin(), owners.end(), owner) == owners.end()) return {};
    }
    const auto at = index == static_cast<std::size_t>(-1)
                        ? owner->trees().size() : std::min(index, owner->trees().size());
    const auto beforeSelection = selectionSnapshot();
    owner->insert(at, tree);
    selectedTree_ = tree;
    selectedContent_.reset();
    contentSelectionActive_ = false;
    selectedId_ = 0;
    selectedContentId_ = 0;
    // Dの挿入履歴は作成したイベントを選び、取り消しではそのイベ
    // ントだけを除く。既存の所有ツリーと同一性をすべて維持する。
    const auto afterSelection =
        historySelectionSource_ && !lib::editor_window_host_options().all_mode
            ? selectionSnapshot(false)
            : selectionSnapshot();
    const auto treeId = tree->objectId();
    struct InsertHistory { EventTreePtr removed; };
    const auto history = std::make_shared<InsertHistory>();
    const auto model = areaHolder_;
    const auto summary = comm_.summary.lock();
    const auto scene = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Area>(model);
    const auto battle = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Battle>(model);
    std::function<void()> model_guard;
    if (model && summary &&
        ((scene && std::find(summary->areas().begin(), summary->areas().end(), scene) != summary->areas().end()) ||
         (battle && std::find(summary->battles().begin(), summary->battles().end(), battle) != summary->battles().end()))) {
        const auto attached = [model, summary, scene, battle, owner] {
            const bool root_attached = scene
                ? std::find(summary->areas().begin(), summary->areas().end(), scene) != summary->areas().end()
                : std::find(summary->battles().begin(), summary->battles().end(), battle) != summary->battles().end();
            if (!root_attached) throw std::logic_error("Event insert history root no longer exists");
            const auto owners = etos(*model);
            if (std::find(owners.begin(), owners.end(), owner) == owners.end())
                throw std::logic_error("Event insert history child owner no longer exists");
        };
        attached();
        model_guard = attached;
    }
    const std::weak_ptr<const int> author_life = paletteLifetime_.token;
    const auto deleting = model_guard ? historyModelDeleting_ : comm_.deletingContent;
    const auto notify_model = historyModelNotify_;
    const auto restore_peer_selection = historySelectionRestorer_;
    if (undo_) pushHistory(
        [this, owner, treeId, history, beforeSelection, model_guard,
         restore_peer_selection, author_life, deleting, notify_model] {
            if (model_guard) model_guard();
            const auto live = findTreeById(owner, treeId);
            if (!live) {
                if (model_guard) throw std::logic_error("Event insert history tree no longer exists");
                return;
            }
            const auto saved = duplicateHistorySnapshot(live);
            const auto roots = live->starts();
            for (const auto& root : roots) if (deleting) deleting(root);
            if (model_guard) model_guard();
            if (findTreeById(owner, treeId) != live || live->starts() != roots) {
                if (model_guard) throw std::logic_error("Event insert history tree changed during deletion notification");
                return;
            }
            const auto found = std::find(owner->trees().begin(), owner->trees().end(), live);
            if (found == owner->trees().end()) return;
            history->removed = saved;
            owner->removeEvent(static_cast<std::size_t>(found - owner->trees().begin()));
            // DのEVUndo::udaは、残存する全関連画面で記録したパスを復元する。
            if (restore_peer_selection) restore_peer_selection(beforeSelection);
            if (!author_life.expired()) {
                if (!restore_peer_selection) restoreSelection(beforeSelection);
                notify({}, {}, true);
            } else if (notify_model) {
                notify_model();
            }
        },
        [this, owner, treeId, at, history, afterSelection, model_guard,
         restore_peer_selection, author_life, notify_model] {
            if (model_guard) model_guard();
            if (!history->removed || findTreeById(owner, treeId)) {
                if (model_guard) throw std::logic_error("Event insert history cannot restore into changed owner");
                return;
            }
            const auto restored = history->removed->dup();
            owner->insert(std::min(at, owner->trees().size()), restored);
            if (restore_peer_selection) restore_peer_selection(afterSelection);
            if (!author_life.expired()) {
                if (!restore_peer_selection) restoreSelection(afterSelection);
                notify(restored, {}, true);
            } else if (notify_model) {
                notify_model();
            }
        }, !model_guard);
    notify(tree, {}, true);
    return tree;
}

bool EventView::deleteEvent(const EventTreePtr& value) {
    const auto tree = selectedTreeOr(value);
    if (!tree || !area_) return false;
    auto* owner = tree->owner();
    if (!owner) owner = area_;
    if (!owner) return false;
    const auto it = std::find(owner->trees().begin(), owner->trees().end(), tree);
    if (it == owner->trees().end()) return false;
    return mutate([owner, tree] {
        owner->remove(tree);
        return true;
    });
}

bool EventView::cloneEvent(const EventTreePtr& value) {
    const auto source = selectedTreeOr(value);
    if (!source || !area_) return false;
    auto* owner = source->owner();
    if (!owner) owner = area_;
    if (!owner) return false;
    // Dの複製はクリップボードXMLを経由し、新しいモデルの同一性を
    // 作る。dup()は履歴保存用にIDを維持するため、ここで使うと
    // 、やり直しの同一性検索が複製でなく元イベントを選んでしまう。
    const auto* model_props = comm_.modelProps;
    const card::XMLOption option(model_props ? &model_props->sys() : nullptr,
                                versioninfo::LATEST_VERSION);
    std::optional<system::XMLInfo> version;
    if (model_props) version.emplace(model_props->sys(), versioninfo::LATEST_VERSION);
    auto copy = event::EventTree::createFromNode(source->toNode(&option),
                                                version ? &*version : nullptr);
    if (!copy) return false;
    // Systemの解決処理がないAPIホストでも、型付きの発火タイミングを維持する。
    copy->copyIgnitions(*source);
    copy->name(source->name().empty() ? messages_.event : source->name() + " copy");
    const auto it = std::find(owner->trees().begin(), owner->trees().end(), source);
    const auto index = it == owner->trees().end() ? owner->trees().size()
                                                   : static_cast<std::size_t>(std::distance(owner->trees().begin(), it)) + 1;
    // 共通の挿入履歴を使い、取り消しでこの複製だけを削除す
    // る。元イベントと無関係な全モデルの同一性を保持する。
    return static_cast<bool>(appendTree(std::move(copy), index, owner));
}

std::function<void(const String&)> EventView::treeScalarReplay(
        const EventTreePtr& tree, const bool comment) {
    // 場面・イベントの共通履歴は、編集元のウィジェットではなくモデルを所有する。
    const auto root=areaHolder_;
    const auto summary=comm_.summary.lock();
    if (!root || !summary || !tree || tree->objectId().empty()) return {};
    const auto area=std::dynamic_pointer_cast<::cwxeditor::cwx::area::Area>(root);
    const auto battle=std::dynamic_pointer_cast<::cwxeditor::cwx::area::Battle>(root);
    if ((!area || std::find(summary->areas().begin(),summary->areas().end(),area)==summary->areas().end()) &&
        (!battle || std::find(summary->battles().begin(),summary->battles().end(),battle)==summary->battles().end()))
        return {}; // 借用・Scene以外の編集部は、既存の所有経路を維持する。
    const auto tree_id=tree->objectId();
    const auto notify_model=historyModelNotify_;
    const std::weak_ptr<const int> life=paletteLifetime_.token;
    return [this,life,root,summary,area,battle,tree_id,comment,notify_model](const String& value) {
        const bool attached=area
            ? std::find(summary->areas().begin(),summary->areas().end(),area)!=summary->areas().end()
            : std::find(summary->battles().begin(),summary->battles().end(),battle)!=summary->battles().end();
        if (!attached) throw std::logic_error("Event scalar history owner is no longer in its Summary");
        EventTreePtr current;
        for (auto* owner:etos(*root)) {
            current=findTreeById(owner,tree_id);
            if (current) break;
        }
        if (!current) throw std::logic_error("Event scalar history tree no longer exists");
        if (comment) current->comment(value);
        else current->name(value);
        if (!life.expired()) notify(current,{},true);
        else if (notify_model) notify_model();
    };
}

bool EventView::setTreeName(String value, const EventTreePtr& valueTree) {
    const auto tree = selectedTreeOr(valueTree);
    if (!tree) return false;
    if (tree->name() == value) return false;
    // Dの左のインライン編集は、イベント名自身である最初のStartを除
    // 外し、参照名の変更前に他の全Startに対して一意な名前にする。
    value = ::cwxeditor::cwx::utils::createNewName(
        value, [tree](const String candidate) {
            const auto& starts = tree->starts();
            for (std::size_t index = 1; index < starts.size(); ++index)
                if (starts[index] && starts[index]->name() == candidate) return false;
            return true;
        }, true);
    const auto tree_id = tree->objectId();
    const auto before = tree->name();
    const auto after = std::move(value);
    const auto model_replay=treeScalarReplay(tree,false);
    const std::function<void(const String&)> apply = model_replay ? model_replay :
        std::function<void(const String&)>{[this, tree_id](const String& name) {
            const auto current = findTreeByObjectId(tree_id);
            if (!current) return;
            current->name(name);
            notify(current, {}, true);
        }};
    const std::weak_ptr<const int> edit_life=paletteLifetime_.token;
    apply(after);
    if (edit_life.expired()) return true;
    if (undo_) {
        pushHistory([apply, before] { apply(before); },
                    [apply, after] { apply(after); },!model_replay);
    }
    return true;
}

bool EventView::setComment(String value, EventTreeOwner* valueOwner,
                           const EventTreePtr& valueTree) {
    const auto tree = selectedTreeOr(valueTree);
    auto* owner = selectedOwnerOr(valueOwner);
    if (valueTree && tree) {
        if (tree->comment() == value) return false;
        const auto tree_id = tree->objectId();
        const auto before = tree->comment();
        const auto after = std::move(value);
        const auto model_replay=treeScalarReplay(tree,true);
        const std::function<void(const String&)> apply = model_replay ? model_replay :
            std::function<void(const String&)>{[this, tree_id](const String& comment) {
                const auto current = findTreeByObjectId(tree_id);
                if (!current) return;
                current->comment(comment);
                notify(current, {}, true);
            }};
        const std::weak_ptr<const int> edit_life=paletteLifetime_.token;
        apply(after);
        if (edit_life.expired()) return true;
        if (undo_) {
            pushHistory([apply, before] { apply(before); },
                        [apply, after] { apply(after); },!model_replay);
        }
        return true;
    }
    if (!owner || owner->commentForEvents() == value) return false;
    const auto before = owner->commentForEvents();
    const auto after = std::move(value);
    const auto root = areaHolder_;
    const auto summary = comm_.summary.lock();
    const auto scene = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Area>(root);
    const auto battle = std::dynamic_pointer_cast<::cwxeditor::cwx::area::Battle>(root);
    const bool model_replay = root && owner == root.get() && summary &&
        ((scene && std::find(summary->areas().begin(), summary->areas().end(), scene) != summary->areas().end()) ||
         (battle && std::find(summary->battles().begin(), summary->battles().end(), battle) != summary->battles().end()));
    const std::weak_ptr<const int> edit_life = paletteLifetime_.token;
    const auto notify_model = historyModelNotify_;
    const auto apply = [this, owner, root, summary, scene, battle,
                        model_replay, edit_life, notify_model](const String& comment) {
        if (model_replay) {
            const bool attached = scene
                ? std::find(summary->areas().begin(), summary->areas().end(), scene) != summary->areas().end()
                : std::find(summary->battles().begin(), summary->battles().end(), battle) != summary->battles().end();
            if (!attached) throw std::logic_error("Event owner-comment history owner is no longer in its Summary");
        }
        owner->commentForEvents(comment);
        if (!edit_life.expired()) notify({}, {}, true);
        else if (model_replay && notify_model) notify_model();
    };
    apply(after);
    if (edit_life.expired()) return true;
    if (undo_) {
        pushHistory([apply, before] { apply(before); },
                    [apply, after] { apply(after); }, !model_replay);
    }
    return true;
}

String EventView::comment(EventTreeOwner* valueOwner, const EventTreePtr& valueTree) const {
    if (valueTree) return valueTree->comment();
    if (valueOwner) return valueOwner->commentForEvents();
    if (const auto tree = rightPaneActive() ? selectedTree_ : selectionEventTree()) return tree->comment();
    return selectionParent() ? selectionParent()->commentForEvents() : String{};
}

std::vector<FireCondition> EventView::fireConditions(const EventTreePtr& value) const {
    const auto tree = selectedTreeOr(value);
    std::vector<FireCondition> result;
    if (!tree) return result;
    if (tree->fireEnter()) result.push_back(FireCondition::enter());
    if (tree->fireEscape()) result.push_back(FireCondition::escape());
    if (tree->fireLose()) result.push_back(FireCondition::lose());
    if (tree->fireEveryRound()) result.push_back(FireCondition::everyRound());
    if (tree->fireRoundEnd()) result.push_back(FireCondition::roundEnd());
    if (tree->fireRound0()) result.push_back(FireCondition::round0());
    for (const auto& valueCode : tree->keyCodes()) result.push_back(FireCondition::withKeyCode(valueCode));
    for (const auto roundValue : tree->rounds()) result.push_back(FireCondition::withRound(roundValue));
    return result;
}

namespace {
bool insertFireValue(const EventTreePtr& tree, const FireCondition& fire, std::ptrdiff_t key_index = -1) {
        switch (fire.kind) {
        case FireKind::Enter:
            if (tree->fireEnter()) return false;
            tree->enter(true); return true;
        case FireKind::Escape:
            if (tree->fireEscape()) return false;
            tree->escape(true); return true;
        case FireKind::Lose:
            if (tree->fireLose()) return false;
            tree->lose(true); return true;
        case FireKind::EveryRound:
            if (tree->fireEveryRound()) return false;
            tree->everyRound(true); return true;
        case FireKind::RoundEnd:
            if (tree->fireRoundEnd()) return false;
            tree->roundEnd(true); return true;
        case FireKind::Round0:
            if (tree->fireRound0()) return false;
            tree->round0(true); return true;
        case FireKind::KeyCode:
            return tree->addKeyCode(fire.keyCode, key_index);
        case FireKind::Round:
            if (!tree->addRound(fire.round)) return false;
            // DはコアのaddRoundではなく、編集部の境界で正規化する。
            tree->sortRounds();
            return true;
        }
        return false;
}
bool eraseFireValue(const EventTreePtr& tree, const FireCondition& fire) {
        switch (fire.kind) {
        case FireKind::Enter:
            if (!tree->fireEnter()) return false;
            tree->enter(false); return true;
        case FireKind::Escape:
            if (!tree->fireEscape()) return false;
            tree->escape(false); return true;
        case FireKind::Lose:
            if (!tree->fireLose()) return false;
            tree->lose(false); return true;
        case FireKind::EveryRound:
            if (!tree->fireEveryRound()) return false;
            tree->everyRound(false); return true;
        case FireKind::RoundEnd:
            if (!tree->fireRoundEnd()) return false;
            tree->roundEnd(false); return true;
        case FireKind::Round0:
            if (!tree->fireRound0()) return false;
            tree->round0(false); return true;
        case FireKind::KeyCode:
            if (!tree->fireKeyCode(fire.keyCode)) return false;
            tree->removeKeyCode(fire.keyCode); return true;
        case FireKind::Round:
            if (!tree->fireRound(fire.round)) return false;
            tree->removeRound(fire.round); return true;
        }
        return false;
}
bool insertFireNode(const EventTreePtr& tree, EventTreeOwner* owner, const xml::XNode& node, std::ptrdiff_t key_index = -1) {
    if (!tree || !owner) return false;
    const auto name = node.name();
    // 旧Dのクリップボードは、これらの発火ノードを直接書く。変更前に
    // 全値を復号し、複数値の貼り付けを一つの取り消し記録にまとめる。
    if (name == event::ROUNDS_XML_NAME) {
        return !tree->roundsFromNode(*owner, node).empty();
    }
    if (name == card::KEY_CODES_XML_NAME) {
        if (!owner->canHasFireKeyCode()) return false;
        std::vector<event::FKeyCode> keys;
        for (const auto& text : utils::decodeLf(node.value(), true))
            keys.push_back(keyCodeFromText(text));
        bool changed = false;
        for (const auto& key : keys) { if (tree->addKeyCode(key, key_index)) { changed = true; if (key_index >= 0) ++key_index; } }
        return changed;
    }
    auto kind = fireKindFromText(node.attr("kind", false, ""));
    if (name == "IgniteWithEnter") kind = FireKind::Enter;
    else if (name == "IgniteWithRunAway") kind = FireKind::Escape;
    else if (name == "IgniteWithLose") kind = FireKind::Lose;
    else if (name == "IgniteWithEveryRound") kind = FireKind::EveryRound;
    else if (name == "IgniteWithRoundEnd") kind = FireKind::RoundEnd;
    else if (name == "IgniteWithRound0") kind = FireKind::Round0;
    if (!kind) return false;
    // 旧形式XMLと包まれたXMLの両方へ、同じ出力先の利用可否判定を適用する。
    bool allowed = false;
    switch (*kind) {
    case FireKind::Enter: allowed = owner->canHasFireEnter(); break;
    case FireKind::Escape: allowed = owner->canHasFireEscape(); break;
    case FireKind::Lose: allowed = owner->canHasFireLose(); break;
    case FireKind::EveryRound: allowed = owner->canHasFireEveryRound(); break;
    case FireKind::RoundEnd: allowed = owner->canHasFireRoundEnd(); break;
    case FireKind::Round0: allowed = owner->canHasFireRound0(); break;
    case FireKind::KeyCode: allowed = owner->canHasFireKeyCode(); break;
    case FireKind::Round: allowed = owner->canHasFireRound(); break;
    }
    if (!allowed) return false;
    FireCondition fire;
    fire.kind = *kind;
    if (*kind == FireKind::KeyCode) {
        fire.keyCode.keyCode = node.attr("value", false, "");
        try {
            fire.keyCode.kind = static_cast<types::FKCKind>(node.attr<std::uint32_t>("timing", false, 0));
        } catch (...) {
            fire.keyCode.kind = types::FKCKind::Use;
        }
    } else if (*kind == FireKind::Round) {
        try { fire.round = node.attr<std::uint32_t>("value", true); }
        catch (...) { return false; }
    }
    return insertFireValue(tree, fire, key_index);
}
} // namespace

bool EventView::addFire(const FireCondition& fire, const EventTreePtr& value) {
    const auto tree = selectedTreeOr(value);
    if (!tree) return false;
    // DのcreateEventFireは、取り消し状態の保存前に同じ所有元の全表示の編集を確定
    // する。ホストのコールバックがこのペインを閉じ得るため、その後に借用コールバックを残さない。
    if (!lib::editor_window_host_options().all_mode) {
        if (readOnly_) return false;
        const auto* targetOwner = tree->owner();
        if (!targetOwner) return false;
        const std::weak_ptr<const int> life = paletteLifetime_.token;
        const auto finish = beforeFireEdit_;
        if (finish) {
            if (!finish() || life.expired()) return false;
        } else {
            editEnter();
            if (life.expired()) return false;
        }
        // 編集コールバックが対象を削除・置換し得る。切り離されたツリーへ書き込まない。
        if (tree->owner() != targetOwner ||
            std::find(targetOwner->trees().begin(), targetOwner->trees().end(), tree) ==
                targetOwner->trees().end()) return false;
    }
    if (!tree) return false;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    return mutate([this, tree, fire, life] {
        if (!insertFireValue(tree, fire)) return false;
        // モデルのコールバックが編集元の表示を終了させ得る。古
        // いペインから保留選択を公開せず、Cardsの選択が別
        // のEventTreeを指すだけで右の図を置換しない。
        if (life.expired()) return true;
        pendingFireSelection_ = fire;
        pendingFireTreeId_ = tree->objectId();
        return true;
    });
}

bool EventView::removeFire(const FireCondition& fire, const EventTreePtr& value) {
    const auto tree = selectedTreeOr(value);
    if (!tree) return false;
    return mutate([tree, fire] { return eraseFireValue(tree, fire); });
}

bool EventView::createEventFire(const FireCondition& fire) { return addFire(fire); }

bool EventView::addManyRounds(const std::vector<std::uint32_t>& values) {
    const auto tree = selectionEventTree();
    if (!tree || values.empty() || !dynamic_cast<area::Battle*>(area_) ||
        selectionParent() != area_ || tree->owner() != area_) return false;
    return mutate([tree, values] {
        // 既存のaddRoundsは、重複だけなら何もしない契約を維持する。
        return !tree->addRounds(values).empty();
    });
}

bool EventView::setKeyCodeMatchingType(const types::MatchingType value) {
    const auto tree = selectionEventTree();
    if (!tree || tree->keyCodeMatchingType() == value) return false;
    return mutate([tree, value] {
        tree->keyCodeMatchingType(value);
        return true;
    });
}

bool EventView::canSetKeyCodeMatchingType(const types::MatchingType value) const noexcept {
    const auto tree = selectionEventTree();
    return !readOnly_ && tree && tree->keyCodeMatchingType() != value;
}

bool EventView::canSetKeyCodeTiming(const types::FKCKind value) const noexcept {
    const auto tree = selectionEventTree();
    const auto fire = selectionFire();
    if (readOnly_ || !tree || !fire || fire->kind != FireKind::KeyCode ||
        fire->keyCode.kind == value) return false;
    // DのcanConvKeyCodeに合わせ、成功・失敗を設定できるのは敵・プレイヤーのイベントだけ。
    const auto* owner = selectionParent();
    if (value != types::FKCKind::Use && value != types::FKCKind::HasNot &&
        !dynamic_cast<const area::EnemyCard*>(owner) &&
        !dynamic_cast<const area::PlayerCardEvents*>(owner)) return false;
    auto replacement = fire->keyCode;
    replacement.kind = value;
    return !tree->fireKeyCode(replacement);
}

bool EventView::setKeyCodeTiming(const types::FKCKind value) {
    if (!canSetKeyCodeTiming(value)) return false;
    const auto tree = selectionEventTree();
    const auto fire = selectionFire();
    const auto old = fire->keyCode;
    event::FKeyCode replacement = old;
    replacement.kind = value;
    if (tree->fireKeyCode(replacement)) return false;
    const auto found = std::find_if(tree->keyCodes().begin(), tree->keyCodes().end(),
        [&old](const auto& key) { return key.keyCode == old.keyCode && key.kind == old.kind; });
    if (found == tree->keyCodes().end()) return false;
    const auto index = static_cast<std::size_t>(std::distance(tree->keyCodes().begin(), found));
    return mutate([this, tree, index, replacement] {
        // mutateが選択状態を保存する前に、変更したFireの同
        // 一性を取得する。公開行は通知まで古いタイミングを持つため。
        tree->setKeyCode(index, replacement);
        pendingFireSelection_ = FireCondition::withKeyCode(replacement);
        return true;
    });
}

bool EventView::copyConvertedKeyCode(const types::FKCKind value) {
    const auto fire = selectionFire();
    if (!fire || fire->kind != FireKind::KeyCode ||
        !comm_.convertFireKeyCode)
        return false;
    const auto converted = comm_.convertFireKeyCode(fire->keyCode.keyCode, value);
    // DのcopyKeyCodeTimは、印の変換で基底キーが空になって
    // もKeyCodes XMLを書く。例えばAreaのキーが○の場合。
    const auto xml = card::keyCodesToXML({converted});
    const auto* props = comm_.editorProps ? comm_.editorProps() : nullptr;
    const bool plain_xml = comm_.xmlCopyProvider ? comm_.xmlCopyProvider()
                                               : props && props->var().xmlCopy.value;
    writeClipboard(plain_xml ? xmlbytestransfer::makeTextPayload(xml)
                             : xmlbytestransfer::makeXMLPayload(xml));
    if (comm_.refreshToolBar) comm_.refreshToolBar();
    return true;
}

bool EventView::copySelectionXML(const bool shallow) {
    const auto selected = canonicalSelectedContent();
    if (!contentSelectionActive_ || !selected) return false;
    auto copy = selected->dup();
    if (shallow) copy->next().clear();
    // Dの生XMLコピーはContentsLineを子へ展開し、その子に
    // ルート専用の直列化を行って「Node is not Root」で失
    // 敗する。存在するContentsLineも含め独立したルートを保
    // ち、有効なコンテントのコピーを成功させる。Dの失敗は再現しない。
    auto node = copy->toNode().clone(true);
    node.removeAttrRecursive("contentId");
    xml::XmlWriterOptions options;
    options.declaration = false;
    const auto xml = node.serialize(options);
    if (xml.empty()) return false;
    writeClipboard(lib::ClipboardData{xml, {}, {}});
    if (comm_.refreshToolBar) comm_.refreshToolBar();
    return true;
}

std::size_t EventView::keyCodesIndex(const EventTreePtr& value) const {
    const auto tree = selectedTreeOr(value);
    if (!tree) return 0;
    return static_cast<std::size_t>(tree->fireEnter()) +
           static_cast<std::size_t>(tree->fireEscape()) +
           static_cast<std::size_t>(tree->fireLose()) +
           static_cast<std::size_t>(tree->fireEveryRound()) +
           static_cast<std::size_t>(tree->fireRoundEnd()) +
           static_cast<std::size_t>(tree->fireRound0());
}

String EventView::fireText(const FireCondition& fire, const EventTreePtr& tree) const {
    switch (fire.kind) {
    case FireKind::Enter:
        if (const auto context = tree ? tree : selectedTree_) {
            if (dynamic_cast<const ::cwxeditor::cwx::area::MenuCard*>(context->owner()))
                return "クリック";
            if (dynamic_cast<const ::cwxeditor::cwx::area::EnemyCard*>(context->owner()) ||
                dynamic_cast<const ::cwxeditor::cwx::area::PlayerCardEvents*>(context->owner()))
                return "死亡";
            if (dynamic_cast<const ::cwxeditor::cwx::area::Battle*>(context->owner()))
                return "勝利";
        }
        return messages_.enter;
    case FireKind::Escape: return messages_.escape;
    case FireKind::Lose: return messages_.lose;
    case FireKind::EveryRound: return messages_.everyRound;
    case FireKind::RoundEnd: return messages_.roundEnd;
    case FireKind::Round0: return messages_.round0;
    case FireKind::KeyCode: return keyCodeWithTiming(fire.keyCode);
    case FireKind::Round: return messages_.round + std::to_string(fire.round);
    }
    return {};
}

std::vector<String> EventView::eventTreeNames(const Props& prop, const event::EventTree& tree) {
    std::vector<String> result;
    auto* const owner = tree.owner();
    if (tree.fireEnter())
        result.push_back(eventTreeFireName(prop, owner, FireCondition::enter(),
                                           FireNamePurpose::ReservedStartName));
    if (tree.fireEscape())
        result.push_back(eventTreeFireName(prop, owner, FireCondition::escape(),
                                           FireNamePurpose::ReservedStartName));
    if (tree.fireLose())
        result.push_back(eventTreeFireName(prop, owner, FireCondition::lose(),
                                           FireNamePurpose::ReservedStartName));
    if (tree.fireEveryRound())
        result.push_back(eventTreeFireName(prop, owner, FireCondition::everyRound(),
                                           FireNamePurpose::ReservedStartName));
    if (tree.fireRoundEnd())
        result.push_back(eventTreeFireName(prop, owner, FireCondition::roundEnd(),
                                           FireNamePurpose::ReservedStartName));
    if (tree.fireRound0())
        result.push_back(eventTreeFireName(prop, owner, FireCondition::round0(),
                                           FireNamePurpose::ReservedStartName));
    for (const auto& code : tree.keyCodes())
        result.push_back(eventTreeFireName(prop, owner, FireCondition::withKeyCode(code),
                                           FireNamePurpose::ReservedStartName));
    // Dはラウンドがちょうど一つ設定されている場合だけ、ラウンドの見出しを確保する。
    if (tree.rounds().size() == 1U)
        result.push_back(eventTreeFireName(
            prop, owner, FireCondition::withRound(tree.rounds().front()),
            FireNamePurpose::ReservedStartName));
    return result;
}

bool EventView::canMoveContentSibling(const ContentPtr& content, const int delta) const noexcept {
    if (readOnly_ || !content || !selectedTree_ || content->tree() != selectedTree_) return false;
    const auto parent = content->parent();
    const auto canMoveWithin = [&](const auto& siblings) {
        const auto it = std::find(siblings.begin(), siblings.end(), content);
        if (it == siblings.end()) return false;
        const auto destination = std::distance(siblings.begin(), it) + delta;
        return destination >= 0 && static_cast<std::size_t>(destination) < siblings.size();
    };
    if (parent) return canMoveWithin(parent->next());
    return canMoveWithin(selectedTree_->starts());
}

bool EventView::canUp() const noexcept {
    if (readOnly_) return false;
    if (rightPaneActive()) {
        const auto* editor_props = comm_.editorProps ? comm_.editorProps() : nullptr;
        const bool vertical_tree = !editor_props || editor_props->var().straightEventTreeView.value;
        return selectedContent_ &&
               (canMoveContentSibling(selectedContent_, -1) ||
                (vertical_tree && canSwapToParent()));
    }
    // Dは保持されたコンテント選択ではなく、現在フォーカスを持つペインから配送する。
    if (const auto* info = nodeInfo(leftOrActiveSelectionId())) {
        if (info->kind == NodeInfo::Kind::EventTree && info->owner)
            return info->treeIndex > 0;
        if (info->kind == NodeInfo::Kind::Fire && info->tree && info->fire.kind == FireKind::KeyCode) {
            std::size_t index = 0;
            for (const auto& code : info->tree->keyCodes()) {
                if (code.keyCode == info->fire.keyCode.keyCode && code.kind == info->fire.keyCode.kind)
                    return index > 0;
                ++index;
            }
        }
    }
    return false;
}

bool EventView::canDown() const noexcept {
    if (readOnly_) return false;
    if (rightPaneActive()) {
        const auto* editor_props = comm_.editorProps ? comm_.editorProps() : nullptr;
        const bool vertical_tree = !editor_props || editor_props->var().straightEventTreeView.value;
        return selectedContent_ &&
               (canMoveContentSibling(selectedContent_, 1) ||
                (vertical_tree && canSwapToChild()));
    }
    // 保持されているコンテントの選択ではなく、現在フォーカスを持つペインから処理を振り分ける（D版と同じ）。
    if (const auto* info = nodeInfo(leftOrActiveSelectionId())) {
        if (info->kind == NodeInfo::Kind::EventTree && info->owner)
            return info->treeIndex + 1 < info->owner->trees().size();
        if (info->kind == NodeInfo::Kind::Fire && info->tree && info->fire.kind == FireKind::KeyCode) {
            std::size_t index = 0;
            for (const auto& code : info->tree->keyCodes()) {
                if (code.keyCode == info->fire.keyCode.keyCode && code.kind == info->fire.keyCode.kind)
                    return index + 1 < info->tree->keyCodes().size();
                ++index;
            }
        }
    }
    return false;
}

bool EventView::up() {
    if (readOnly_) return false;
    // 保持されている左側の選択ではなく、現在フォーカスを持つペインから処理を振り分ける（D版と同じ）。
    if (rightPaneActive()) {
        if (!selectedContent_) return false;
        return canMoveContentSibling(selectedContent_, -1)
                   ? moveContent(selectedContent_, -1) : swapToParent();
    }
    const auto selected = leftOrActiveSelectionId();
    if (const auto* info = nodeInfo(selected); info && info->kind == NodeInfo::Kind::EventTree && info->owner && canUp()) {
        const auto owner = info->owner;
        const auto from = info->treeIndex;
        return mutate([owner, from] {
            owner->swapEventTree(from, from - 1);
            return true;
        });
    }
    if (const auto* info = nodeInfo(selected); info && info->kind == NodeInfo::Kind::Fire &&
        info->tree && info->fire.kind == FireKind::KeyCode && canUp()) {
        std::size_t index = 0;
        for (const auto& code : info->tree->keyCodes()) {
            if (code.keyCode == info->fire.keyCode.keyCode && code.kind == info->fire.keyCode.kind) {
                const auto fire = info->fire;
                const auto tree = info->tree;
                return mutate([this, tree, index, fire,
                               fireTreeId = tree->objectId(),
                               life = std::weak_ptr<const int>(paletteLifetime_.token)] {
                    tree->swapKeyCode(index, index - 1);
                    // EventTree::swapKeyCode は交換前に通知する。先に設定し
                    // た保留中の Fire は、 古い並びに対する同期更新で消費されることがある。
                    // D版と同様、別のイベントを表示中でも同じ KeyCode 行を移動する。
                    if (!life.expired()) {
                        pendingFireSelection_ = fire;
                        pendingFireTreeId_ = fireTreeId;
                    }
                    return true;
                });
            }
            ++index;
        }
    }
    return false;
}

bool EventView::down() {
    if (readOnly_) return false;
    // 保持されている左側の選択ではなく、現在フォーカスを持つペインから処理を振り分ける（D版と同じ）。
    if (rightPaneActive()) {
        if (!selectedContent_) return false;
        return canMoveContentSibling(selectedContent_, 1)
                   ? moveContent(selectedContent_, 1) : swapToChild();
    }
    const auto selected = leftOrActiveSelectionId();
    if (const auto* info = nodeInfo(selected); info && info->kind == NodeInfo::Kind::EventTree && info->owner && canDown()) {
        const auto owner = info->owner;
        const auto from = info->treeIndex;
        return mutate([owner, from] {
            owner->swapEventTree(from, from + 1);
            return true;
        });
    }
    if (const auto* info = nodeInfo(selected); info && info->kind == NodeInfo::Kind::Fire &&
        info->tree && info->fire.kind == FireKind::KeyCode && canDown()) {
        std::size_t index = 0;
        for (const auto& code : info->tree->keyCodes()) {
            if (code.keyCode == info->fire.keyCode.keyCode && code.kind == info->fire.keyCode.kind) {
                const auto fire = info->fire;
                const auto tree = info->tree;
                return mutate([this, tree, index, fire,
                               fireTreeId = tree->objectId(),
                               life = std::weak_ptr<const int>(paletteLifetime_.token)] {
                    tree->swapKeyCode(index, index + 1);
                    // EventTree::swapKeyCode は交換前に通知する。先に設定し
                    // た保留中の Fire は、 古い並びに対する同期更新で消費されることがある。
                    // D版と同様、別のイベントを表示中でも同じ KeyCode 行を移動する。
                    if (!life.expired()) {
                        pendingFireSelection_ = fire;
                        pendingFireTreeId_ = fireTreeId;
                    }
                    return true;
                });
            }
            ++index;
        }
    }
    return false;
}

bool EventView::canEdit() const noexcept {
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    if (readOnly_ || selectionEventTree()) return !readOnly_;
    const bool active = rightPaneActive();
    return !life.expired() && active && static_cast<bool>(selectedContent_);
}

std::optional<EventView::GraphEditSpec> EventView::graphEditSpec(
    const ContentPtr& content) const {
    if (readOnly_ || !content) return std::nullopt;
    GraphEditSpec result;
    result.value = content->name();
    const auto parent = content->parent();
    if (!parent) return result; // スタート名。

    const auto makeChoice = [this, &parent, &content](String stored) {
        auto display = content->dup();
        display->setName(stored);
        return lib::ChoiceItem{eventText(parent, display), std::move(stored), {}, true};
    };
    const auto append = [&result, &makeChoice](String stored) {
        result.choices.push_back(makeChoice(std::move(stored)));
    };
    // D版の createSelectionCombo / createCouponCombo と同
    // 様、編集可能なコンボには 加工前の名前を並べ、入力された文字列をそのまま名前として保存する。
    const auto appendRaw = [&result](String stored) {
        auto text = stored;
        result.choices.push_back(lib::ChoiceItem{std::move(text), std::move(stored), {}, true});
    };
    switch (parent->detail().nextType) {
    case types::CNextType::None:
        return std::nullopt;
    case types::CNextType::Text:
        // D版では選択履歴のコンボを使用できる。移植版で
        // は編集可能なまま、 スキンに設定された標準の選
        // 択肢を初期表示し、既存の独自の値も保持する。
        if (prop_.etc.editSelectionWithCombo) {
            result.minimum_width = prop_.etc.selectionWidth;
            const std::weak_ptr<const int> life=paletteLifetime_.token;
            const auto provider=comm_.standardSelectionsProvider;
            const auto standards=provider ? provider() : comm_.standardSelections;
            if (life.expired()) return std::nullopt;
            for (const auto& value : standards) appendRaw(value);
            if (std::ranges::find(standards, content->name()) == standards.end())
                appendRaw(content->name());
            result.editable_choice = true;
            const auto fallback = comm_.standardSelections;
            const auto current = content->name();
            result.refresh_choices = [provider, fallback, current] {
                const auto refreshed = provider ? provider() : fallback;
                std::vector<lib::ChoiceItem> choices;
                choices.reserve(refreshed.size() + 1U);
                for (const auto& value : refreshed)
                    choices.push_back({value, value, {}, true});
                if (std::ranges::find(refreshed, current) == refreshed.end())
                    choices.push_back({current, current, {}, true});
                return choices;
            };
            result.refresh_binding = comm_.selectionChangesBinding;
        }
        break;
    case types::CNextType::Bool:
        append("○");
        append("×");
        // D版の createBoolEditor と同様、TRUE のときだけインデックス 0 とし、それ以外は FALSE にする。
        if (result.value != "○") result.value = "×";
        break;
    case types::CNextType::Step:
        {
        std::size_t count = prop_.looks.stepMaxCount;
        const auto summary = comm_.summary.lock();
        const auto root = localVariableRoot_
            ? localVariableRoot_
            : (summary ? summary->flagDirRoot() : flagmodel::FlagDirPtr{});
        const auto counter = parent->useCounter()
            ? parent->useCounter() : localVariableUseCounter_;
        if (const auto step = flagmodel::findVar<flagmodel::Step>(
                root, counter, parent->step()))
            count = step->count();
        for (std::size_t index = 0; index < count; ++index)
            append(std::to_string(index));
        append("Default");
        // D版の createNumEditor と同様、一覧にない値では Default を選択する。
        if (std::ranges::none_of(result.choices, [&result](const lib::ChoiceItem& item) {
                return item.value == result.value; }))
            result.value = "Default";
        break;
        }
    case types::CNextType::IdArea:
    case types::CNextType::IdBattle:
        if (const auto summary = comm_.summary.lock()) {
            if (parent->detail().nextType == types::CNextType::IdArea) {
                for (const auto& value : summary->areas())
                    if (value) append(std::to_string(value->id()));
            } else {
                for (const auto& value : summary->battles())
                    if (value) append(std::to_string(value->id()));
            }
        }
        append("Default");
        // D版の createNumEditor と同様、一覧にない値では Default を選択する。
        if (std::ranges::none_of(result.choices, [&result](const lib::ChoiceItem& item) {
                return item.value == result.value; }))
            result.value = "Default";
        break;
    case types::CNextType::Trio:
        append(">");
        append("<");
        append("=");
        // D版の createTrioEditor と同様、< と = 以外では > を選択する。
        if (result.value != "<" && result.value != "=") result.value = ">";
        break;
    case types::CNextType::Coupon:
        result.minimum_width = prop_.etc.couponWidth;
        if (comm_.couponChoicesProvider) {
            const std::weak_ptr<const int> life = paletteLifetime_.token;
            const auto provider = comm_.couponChoicesProvider;
            const auto values = provider(content);
            if (life.expired()) return std::nullopt;
            for (const auto& value : values) appendRaw(value);
            result.refresh_choices = [provider, content] {
                std::vector<lib::ChoiceItem> choices;
                const auto refreshed = provider(content);
                choices.reserve(refreshed.size());
                for (const auto& value : refreshed)
                    choices.push_back({value, value, {}, true});
                return choices;
            };
            result.refresh_binding = comm_.couponChangesBinding;
        }
        if (std::ranges::none_of(result.choices, [&result](const lib::ChoiceItem& item) {
                return item.value == result.value;
            }))
            appendRaw(content->name());
        result.editable_choice = true;
        break;
    }
    return result;
}

bool EventView::inlineNameEditActive() const {
    return eventGraphCanvas_ && comm_.services.gui &&
           comm_.services.gui->programmatic_inline_edit_active(eventGraphCanvas_);
}

bool EventView::canEditSelection() const noexcept {
    return !readOnly_ && selectedContent_ &&
           static_cast<bool>(graphEditSpec(selectedContent_));
}

bool EventView::editSelection() {
    if (!canEditSelection() || !eventGraphCanvas_ || !comm_.services.gui)
        return false;
    const auto content = selectedContent_;
    const auto id = selectedContentId_;
    auto spec = graphEditSpec(content);
    if (!spec) return false;
    if (!lib::editor_window_host_options().all_mode && spec->minimum_width)
        graphEditorMinimumWidth_ = spec->minimum_width;
    lib::ProgrammaticInlineEditRequest request;
    request.generic_owner = eventGraphCanvas_;
    request.generic_text = spec->value;
    request.generic_choices = std::move(spec->choices);
    request.generic_editable_choice = spec->editable_choice;
    const auto refresh_choices = spec->refresh_choices;
    const auto refresh_binding = spec->refresh_binding;
    request.generic_editor_created = [this, refresh_choices, refresh_binding](const auto& editor,
                                                              const auto& choice) {
        // インライン編集には、置き換えるグラフ内の文字と同じフォントを
        // 使う。 グラフの effective_editor_widget_font()
        // は、標準のエディタフォントに UI文字倍率を適用したも
        // の。ウィジェット自身が倍率を適用するため、 ここでは拡
        // 大前の標準フォントを渡す。拡大済みのものを渡すと 二重
        // に拡大され、UI倍率に合わせたイベント行からはみ出す。
        const lib::FontSpec font{};
        if (editor) editor->set_font(font);
        if (choice) choice->set_font(font);
        const std::weak_ptr<const int> life = paletteLifetime_.token;
        auto selection_binding = comm_.setupSelectionEditor
            ? comm_.setupSelectionEditor(editor, choice) : std::shared_ptr<void>{};
        if (life.expired()) return;
        std::shared_ptr<void> choices_binding;
        if (choice && refresh_choices && refresh_binding) {
            const std::weak_ptr<lib::ComboBox> weak_choice = choice;
            const auto refresh = [life, weak_choice, refresh_choices] {
                if (life.expired()) return;
                const auto target = weak_choice.lock();
                if (!target) return;
                const auto edit_text = target->edit_text;
                const auto selected_before = target->selected_index;
                const auto order_before = target->item_order_revision;
                auto items = refresh_choices();
                if (life.expired() || target->edit_text != edit_text ||
                    target->selected_index != selected_before ||
                    target->item_order_revision != order_before) return;
                std::optional<std::size_t> selected;
                for (std::size_t index = 0; index < items.size(); ++index) {
                    if (items[index].value == edit_text) {
                        selected = index;
                        break;
                    }
                }
                (void)lib::replace_combo_items(target, std::move(items), selected,
                    edit_text, [life, weak_choice, order_before] {
                        const auto current = weak_choice.lock();
                        return !life.expired() && current &&
                               current->item_order_revision == order_before;
                    });
            };
            choices_binding = refresh_binding(refresh);
            if (life.expired()) return;
        }
        if (selection_binding && choices_binding) {
            graphEditBinding_ = std::make_shared<std::pair<std::shared_ptr<void>,
                std::shared_ptr<void>>>(std::move(selection_binding),
                                        std::move(choices_binding));
        } else {
            graphEditBinding_ = selection_binding ? std::move(selection_binding)
                                                  : std::move(choices_binding);
        }
    };
    request.focus_scope = panel_;
    request.row_height = graphRowHeight();
    editBounds_ = [this, id]() -> std::optional<lib::Rect> {
        if (!eventGraphCanvas_) return std::nullopt;
        const auto row = std::ranges::find(graphRows_, id, &GraphRow::id);
        if (row == graphRows_.end()) return std::nullopt;
        const auto canvas = eventGraphCanvas_->bounds();
        const float origin = showLineNumber_ ? graphPx(kEventGraphLineNumberWidth) : 0.0F;
        const float slope = graphPx(slope_ ? static_cast<float>(slope_->value) : 16.0F);
        // 戦闘一覧などの表内セル編集と同じ配置にする。16px
        // のコンテントアイコンの すぐ右から、行と同じ
        // 高さで編集欄を配置する。高さや内側へのずれを
        // 余分に設けると、隣の行やアイコンに重なる。
        const float x = origin + static_cast<float>(row->depth1) * graphPx(16.0F) +
                        static_cast<float>(row->depth2) * slope + graphPx(17.0F);
        const float right = eventGraphDetailX(std::max(graphPx(520.0F), canvas.width));
        const float configured_width = lib::editor_window_host_options().all_mode
            ? prop_.etc.selectionWidth : graphEditorMinimumWidth_.value_or(prop_.etc.selectionWidth);
        const float width = std::max(24.0F, std::min(configured_width,
                                                     right - x - 2.0F));
        return lib::Rect{canvas.x + x, canvas.y + row->y,
                         width, graphRowHeight()};
    };
    request.generic_bounds = editBounds_;
    request.generic_commit = [this, id](String text) {
        editing_ = false;
        graphEditBinding_.reset();
        return editEnd(id, std::move(text));
    };
    request.generic_cancelled = [this] {
        editing_ = false;
        graphEditBinding_.reset();
    };
    const bool started = comm_.services.gui->request_programmatic_inline_edit(request);
    if (!started) graphEditBinding_.reset();
    editing_ = started;
    return started;
}

bool EventView::editEnd(const TreeItemId id, String text) {
    if (readOnly_) return false;
    if (!lib::editor_window_host_options().all_mode) graphEditorMinimumWidth_.reset();
    const auto* info = nodeInfo(id);
    if (!info) return false;
    if (info->kind == NodeInfo::Kind::EventTree && info->tree)
        return setTreeName(std::move(text), info->tree);
    if (info->kind == NodeInfo::Kind::Content && info->content) {
        const auto tree = info->tree;
        auto* const owner = tree ? tree->owner() : nullptr;
        if (!owner) return false;
        const auto model_guard = historyOwnerGuard(owner);
        const auto tree_id = tree->objectId();
        const auto content_id = info->content->eventId();
        const std::weak_ptr<const int> author_life = paletteLifetime_.token;
        const auto notify_model = historyModelNotify_;
        const auto* model = comm_.modelProps;
        const auto applyName = [this, owner, tree_id, content_id, model_guard,
                                author_life, notify_model, model](const String& name) {
            if (model_guard) model_guard();
            const auto current_tree = findTreeById(owner, tree_id);
            const auto target = findContentById(current_tree, content_id);
            if (!target) {
                if (model_guard) throw std::logic_error("Event name history content no longer exists");
                return;
            }
            if (target->type() == types::CType::Start) target->setName(name);
            else target->setName(model, name); // D版と同様に親のテキストを再検証する。
            if (!author_life.expired()) notify(current_tree, target, true);
            else if (notify_model) notify_model();
        };
        if (info->content->type() == types::CType::Start) {
            if (!info->tree || info->content->name() == text) return false;
            const auto start = info->content;
            const auto unique = ::cwxeditor::cwx::utils::createNewName(
                text, [tree = info->tree, start](const String candidate) {
                    const auto other = tree->start(candidate);
                    return !other || other == start;
                }, true, eventTreeNames(prop_, *info->tree));
            const auto before = start->name();
            start->setName(unique);
            if (undo_) {
                pushHistory([applyName, before] { applyName(before); },
                            [applyName, unique] { applyName(unique); }, !model_guard);
            }
            notify(tree, start, true);
            return true;
        }
        if (info->content->name() == text) return false;
        const auto content = info->content;
        const auto before = content->name();
        const auto after = std::move(text);
        content->setName(model, after);
        if (undo_) {
            pushHistory([applyName, before] { applyName(before); },
                        [applyName, after] { applyName(after); }, !model_guard);
        }
        notify(tree, content, true);
        return true;
    }
    if (info->kind == NodeInfo::Kind::Fire && info->tree &&
        info->fire.kind == FireKind::KeyCode && !text.empty()) {
        const auto old = info->fire.keyCode;
        // 行に表示した発火タイミングの印を解析して戻す（D版の toFKeyCode に相当）。
        event::FKeyCode replacement = keyCodeFromText(std::move(text));
        if (replacement.keyCode.empty()) return false;
        if (info->tree->fireKeyCode(replacement)) {
            // D版と同様、重複するキーコードへの編集は既存の兄弟行に振り替える。
            const auto duplicate = std::find_if(nodes_.begin(), nodes_.end(),
                [&](const auto& entry) {
                    const auto& row = entry.second;
                    return row.kind == NodeInfo::Kind::Fire && row.tree == info->tree &&
                           row.fire.kind == FireKind::KeyCode &&
                           row.fire.keyCode.keyCode == replacement.keyCode &&
                           row.fire.keyCode.kind == replacement.kind;
                });
            if (duplicate != nodes_.end()) (void)select(duplicate->first);
            return false;
        }
        return mutate([this, tree = info->tree, old, replacement] {
            std::size_t index = 0;
            while (index < tree->keyCodes().size() &&
                   (tree->keyCodes()[index].keyCode != old.keyCode ||
                    tree->keyCodes()[index].kind != old.kind)) ++index;
            if (index == tree->keyCodes().size()) return false;
            tree->setKeyCode(index, replacement);
            pendingFireSelection_ = FireCondition::withKeyCode(replacement);
            return true;
        });
    }
    return false;
}

bool EventView::commentUsesRightPane() const noexcept {
    // D版ではコメント操作だけが、最後に操作したペインではなく _cards.isFocusControl()
    // を調べる。 右側でフォーカスを持てる編集欄（傾きなど）はグラフの
    // 入力範囲外だが、 表示中のコンテントを対象にする必要がある。フォー
    // カスを取得できない環境に限り、 従来の選択状態による判定を残す。
    if (const auto gui = comm_.services.gui)
        if (const auto focused = gui->focused_widget())
            return !cards_ || lib::input_scope_path(cards_,focused).empty();
    // D版のコメント操作と同様、Cards にフォーカスがなければ右側の EventTreeView を選ぶ。
    return true;
}

bool EventView::canWriteComment() const noexcept {
    if (readOnly_) return false;
    if (commentUsesRightPane()) return selectedTree_ && selectedContent_;
    const auto id = cards_ && !cards_->selection.empty() ? cards_->selection.front() : selectedId_;
    const auto* info = nodeInfo(id);
    // D版の canWriteCommentL は Classic のプレイヤーカード所有者の
    // 行を除外するが、 配下の各 EventTree 行では通常どおりコメントを編集できる。
    if (info && info->kind == NodeInfo::Kind::Owner &&
        dynamic_cast<area::PlayerCardEvents*>(info->owner)) {
        const auto summary = comm_.summary.lock();
        if (summary && summary->legacy()) return false;
    }
    return info && (info->kind == NodeInfo::Kind::Owner || info->kind == NodeInfo::Kind::EventTree);
}

bool EventView::writeComment(String text) {
    if (!canWriteComment()) return false;
    if (commentUsesRightPane() && selectedContent_) {
        return writeContentComment(selectedContent_, std::move(text));
    }
    if (const auto tree = commentUsesRightPane() ? selectedTree_ : selectionEventTree()) return setComment(std::move(text), nullptr, tree);
    return setComment(std::move(text), selectionParent());
}

bool EventView::writeContentComment(const ContentPtr& content, String text) {
    if (readOnly_ || !area_ || !content || content->comment() == text) return false;
    const auto tree = content->tree();
    if (!tree || tree->fromPath(content->ctPath()) != content) return false;
    const auto owners = etos(*area_);
    if (std::find(owners.begin(), owners.end(), tree->owner()) == owners.end()) return false;
    return mutate([content, text = std::move(text)]() mutable {
        content->comment(std::move(text));
        return true;
    });
}

bool EventView::canWrapTree() const noexcept {
    return !readOnly_ && contentSelectionActive_ && selectedTree_ &&
           selectedContent_ &&
           static_cast<bool>(selectedContent_->parent()) &&
           static_cast<bool>(selectedContent_->parentStart());
}

bool EventView::wrapTree() {
    if (!canWrapTree()) return false;
    const auto content = selectedContent_;
    const auto parent = content->parent();
    const auto parent_start = content->parentStart();
    const auto tree = selectedTree_;
    const auto start_index = std::find(tree->starts().begin(), tree->starts().end(),
                                       parent_start);
    if (!parent || start_index == tree->starts().end()) return false;
    const auto insert_index = static_cast<std::size_t>(
                                  std::distance(tree->starts().begin(), start_index)) +
                              1U;
    const auto name = ::cwxeditor::cwx::utils::createNewName(
        parent_start->name(), [tree](const String candidate) {
            return !tree->hasStart(candidate);
        }, true, eventTreeNames(prop_, *tree));
    auto start = std::make_shared<event::Content>(types::CType::Start, name);
    auto link = std::make_shared<event::Content>(types::CType::LinkStart,
                                                 content->name());
    link->start(name);

    const bool changed = mutate([this, tree, parent, content, start, link,
                                  insert_index] {
        const auto content_position =
            std::find(parent->next().begin(), parent->next().end(), content);
        if (content_position == parent->next().end()) return false;
        const auto child_index = static_cast<std::size_t>(
            std::distance(parent->next().begin(), content_position));
        // D版の Content.insert/add と同様、移動した子に validText() を適用する。
        parent->insert(comm_.modelProps, child_index, link);
        parent->remove(content);
        start->add(comm_.modelProps, content);
        tree->insert(insert_index, start);
        // mutate() のやり直し用スナップショットにも、下で
        // 画面に反映する 操作後の選択状態と同じものを記録する。
        selectedTree_ = tree;
        selectedContent_ = start;
        contentSelectionActive_ = true;
        selectedId_ = 0;
        selectedContentId_ = 0;
        // D版と同様、新しいスタートへの接続後に移動元の行を無効化し、 モードレ
        // スのプロパティ・コメントダイアログが古い行を編集し続けないようにする。
        if (comm_.deletingContent) comm_.deletingContent(content);
        return true;
    });
    if (!changed) return false;
    return true;
}

bool EventView::canFindStartUsers() const noexcept {
    return !readOnly_ && contentSelectionActive_ && selectedTree_ && selectedContent_ &&
           selectedContent_->type() == types::CType::Start;
}

bool EventView::findStartUsers() {
    if (!canFindStartUsers()) return false;
    // D版と同様、現在のイベント選択を最初の呼び出し元に変更せ
    // ず、 すべての呼び出し元を参照結果ウィンドウに表示する。
    const auto callback = comm_.findStartUsers;
    const auto start = selectedContent_;
    return callback ? callback(start) : false;
}

bool EventView::canConvert(const types::CType type,
                           const ContentPtr& value) const {
    if (!value && !contentSelectionActive_) return false;
    const auto content = selectedContentOr(value);
    return !readOnly_ && content && contentTypeAllowed(type) &&
           content->canConvert(type);
}

bool EventView::convert(const types::CType type, const ContentPtr& value) {
    const auto content = selectedContentOr(value);
    if (!canConvert(type, content)) return false;
    const auto convert = comm_.convertContent;
    const auto model = comm_.modelProps;
    return mutate([content, type, convert, model] {
        if (convert) convert(content, type);
        // D版の convertType と同様、子に validText() を適用する。
        else content->convertType(type, {}, model);
        return true;
    });
}

std::vector<types::CType> EventView::conversionTypes() const {
    std::vector<types::CType> result;
    if (!contentSelectionActive_ || !selectedContent_) return result;
    if (!prop_.etc.contentConversionGroups.empty()) {
        std::unordered_set<types::CType> emitted;
        for (const auto& group : prop_.etc.contentConversionGroups) {
            if (std::find(group.group.begin(), group.group.end(),
                          selectedContent_->type()) == group.group.end())
                continue;
            for (const auto type : group.group) {
                if (type == types::CType::Start || type == selectedContent_->type() ||
                    emitted.contains(type) || !selectedContent_->canConvert(type))
                    continue;
                emitted.insert(type);
                result.push_back(type);
            }
        }
        return result;
    }
    for (const auto& [type, detail] : event::CONTENT_DETAILS()) {
        static_cast<void>(detail);
        if (selectedContent_->canConvert(type)) result.push_back(type);
    }
    return result;
}

bool EventView::pasteScript(const StringView script, const bool tryInsert,
                            const bool createEvent) {
    if (readOnly_ || (!createEvent && !selectedTree_) ||
        (createEvent && !canCreateEvent()) ||
        !comm_.compileEventTemplate || script.empty()) return false;
    if (createEvent && dynamic_cast<area::PlayerCardEvents*>(selectionParent()))
        if (const auto summary = comm_.summary.lock(); summary && summary->legacy())
            return false;
    if (scriptTemplateResolver_ &&
        scriptTemplateResolver_(script, tryInsert, createEvent)) return true;
    const auto contents = comm_.compileEventTemplate(script);
    return pasteCompiledTemplateContents(contents, tryInsert, createEvent);
}

bool EventView::pasteCompiledTemplateContents(std::vector<ContentPtr> contents,
                                               const bool tryInsert,
                                               const bool createEvent) {
    // D版の左側一覧への貼り付けと同様、スタートをルートに持つデータから新しいイベントを
    // 作る。 テンプレートの明示指定とグラフへの貼り付けでは、従来の挿入動作を維持する。
    if (!createEvent) return pasteContents(contents, tryInsert);
    if (contents.empty() || !contents.front() ||
        contents.front()->type() != types::CType::Start) return false;
    if (dynamic_cast<area::PlayerCardEvents*>(selectionParent()))
        if (const auto summary = comm_.summary.lock(); summary && summary->legacy())
            return false;
    return static_cast<bool>(createEventWithIgnition(contents));
}

bool EventView::addContent(const ContentPtr& content, const ContentPtr& parent) {
    return insertContent(std::numeric_limits<std::size_t>::max(), content, parent);
}

bool EventView::insertContent(const std::size_t index, const ContentPtr& content,
                              const ContentPtr& parent) {
    if (readOnly_ || !content || !selectedTree_) return false;
    const auto target = parent ? parent : selectedContent_;
    if (!target) {
        if (content->type() != types::CType::Start) return false;
        return mutate([this, content] {
            selectedTree_->insert(selectedTree_->starts().size(), content);
            return true;
        });
    }
    if (content.get() == target.get() || content->isDescendant(target)) return false;
    // D版の Content.add/insert と同様、validText() で分岐先の子の名前を正規化する。
    return mutate([target, content, index, model = comm_.modelProps] {
        if (index == std::numeric_limits<std::size_t>::max()) target->add(model, content);
        else target->insert(model, index, content);
        return true;
    });
}

bool EventView::canDeleteContent(const ContentPtr& value) const noexcept {
    const auto content = selectedContentOr(value);
    if (readOnly_ || !area_ || !content || !selectedTree_ ||
        content->tree() != selectedTree_) return false;
    if (const auto parent = content->parent())
        return std::find(parent->next().begin(), parent->next().end(), content) !=
               parent->next().end();
    const auto& starts = selectedTree_->starts();
    return starts.size() > 1U && starts.front() != content &&
           std::find(starts.begin(), starts.end(), content) != starts.end();
}

bool EventView::deleteContent(const ContentPtr& value) {
    const auto content = selectedContentOr(value);
    if (!canDeleteContent(content)) return false;
    const auto tree = content->tree();
    const auto parent = content->parent();
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto deleting = comm_.deletingContent;
    // D版の delImpl と同様、元の親子関係が残っているうちに、履歴保存より先に通知する。
    // ダイアログのキャンセルで選択が同期更新される場合があるため、先に対象の識別情報を保存する。
    if (deleting) deleting(content);
    if (life.expired()) return false;
    if (content->tree() != tree || content->parent() != parent) return false;
    std::vector<ContentPtr> old_rows;
    std::size_t old_index{};
    bool keep_row_index = false;
    if (!lib::editor_window_host_options().all_mode && selectedTree_ == tree) {
        old_rows.reserve(graphRows_.size());
        for (const auto& row : graphRows_) {
            if (row.content == selectedContent_) {
                old_index = old_rows.size();
                keep_row_index = true;
            }
            old_rows.push_back(row.content);
        }
    }
    const auto select_after_removal = [this, tree, life, keep_row_index,
                                       old_index, old_selected = selectedContent_, old_rows = std::move(old_rows)] {
        if (life.expired() || !keep_row_index || selectedTree_ != tree ||
            (old_selected && old_selected->tree() == tree)) return;
        // D版の EventEditor と同様、消えた行の平坦化インデックスを新しい末尾までの範囲に収
        // めて保持する。 mutate がやり直し用の選択状態を記録する前に、対象の識別情報を設定する。
        std::vector<ContentPtr> survivors;
        survivors.reserve(old_rows.size());
        for (const auto& row : old_rows)
            if (row && row->tree() == tree) survivors.push_back(row);
        if (!survivors.empty())
            selectedContent_ = survivors[std::min(old_index, survivors.size() - 1U)];
    };
    if (parent) {
        if (std::find(parent->next().begin(), parent->next().end(), content) ==
            parent->next().end()) return false;
        return mutate([parent, content, select_after_removal] {
            parent->remove(content);
            select_after_removal();
            return true;
        });
    }
    const auto& starts = tree->starts();
    const auto it = std::find(starts.begin(), starts.end(), content);
    if (it == starts.end() || it == starts.begin()) return false;
    return mutate([tree, content, select_after_removal] {
        tree->remove(content);
        select_after_removal();
        return true;
    });
}

bool EventView::copy1Content() {
    const auto content = selectionContent();
    if (!canCopy1Content() || !content) return false;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    auto copy = content->dup();
    copy->next().clear();
    xml::XNode node;
    if (copy->type() == types::CType::Start) {
        // D版の単一コンテントのコピーと同様、浅く複製したスタートをルートの Content として出力する。
        node = copy->toNode();
    } else {
        auto wrapper = std::make_shared<event::EventTree>("Clipboard");
        wrapper->starts().front()->add(copy);
        node = wrapper->toNode();
    }
    auto xml = makeClipboard("content", node);
    if (xml.empty()) return false;
    try {
        auto root = xml::XNode::parse(xml);
        root.newAttr("contentId", content->eventId());
        const auto nextType = content->parent()
                                  ? content->parent()->detail().nextType
                                  : types::CNextType::Text;
        root.newAttr("lastNextType", types::fromCNextType(nextType));
        writeClipboard(lib::ClipboardData{root.text(), {}, {}});
    } catch (...) {
        return false;
    }
    if (life.expired()) return false;
    const auto refresh = comm_.refreshToolBar;
    if (refresh) refresh();
    return !life.expired();
}

bool EventView::del1Content() {
    const auto content = selectionContent();
    if (!canDel1Content() || !content) return false;
    const auto parent = content->parent();
    if (!parent || content->type() == types::CType::Start) return false;
    const auto found = std::find(parent->next().begin(), parent->next().end(), content);
    if (found == parent->next().end()) return false;
    const auto index = static_cast<std::size_t>(
        std::distance(parent->next().begin(), found));
    const auto tree = content->tree();
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto deleting = comm_.deletingContent;
    if (deleting) deleting(content);
    if (life.expired()) return false;
    if (content->tree() != tree || content->parent() != parent ||
        index >= parent->next().size() || parent->next()[index] != content) return false;
    return mutate([this, parent, content, index] {
        std::vector<ContentPtr> children = content->next();
        const auto contentName = content->name();
        const auto lastNextType = types::fromCNextType(content->detail().nextType);
        parent->remove(content);
        std::size_t childIndex = index;
        bool first = true;
        for (const auto& child : children) {
            if (!child) continue;
            content->remove(child);
            // D版の del1Content と同様、次の型が変わるときは最初の子が削除したコン
            // テントの テキストを引き継ぎ、それ以外は adjustText() で調整する。
            if (prop_.etc.adjustContentName && first &&
                lastNextType != types::fromCNextType(parent->detail().nextType))
                child->setName(contentName);
            else
                adjustText(parent, child, lastNextType);
            first = false;
            parent->insert(comm_.modelProps, childIndex++, child);
        }
        selectedContent_ = children.empty() ? parent : children.front();
        contentSelectionActive_ = static_cast<bool>(selectedContent_);
        return true;
    });
}

bool EventView::cut1Content() {
    if (!canCut1Content()) return false;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    if (!copy1Content() || life.expired()) return false;
    return del1Content();
}

bool EventView::moveContent(const ContentPtr& content, const int delta) {
    if (readOnly_ || !content || delta == 0) return false;
    const auto parent = content->parent();
    if (parent) {
        const auto& values = parent->next();
        const auto it = std::find(values.begin(), values.end(), content);
        if (it == values.end()) return false;
        const auto from = static_cast<std::ptrdiff_t>(std::distance(values.begin(), it));
        const auto to = from + delta;
        if (to < 0 || static_cast<std::size_t>(to) >= values.size()) return false;
        const auto other = values[static_cast<std::size_t>(to)];
        return mutateSubtree(parent, [this, parent, content, other, from, to] {
            if (comm_.deletingContent) comm_.deletingContent(content);
            if (comm_.deletingContent) comm_.deletingContent(other);
            // 監視側の処理が表示更新や再選択を行うことがあるため、ビューの選択状態を読み直さない。
            if (static_cast<std::size_t>(from) >= parent->next().size() ||
                static_cast<std::size_t>(to) >= parent->next().size() ||
                parent->next()[static_cast<std::size_t>(from)] != content ||
                parent->next()[static_cast<std::size_t>(to)] != other) return false;
            parent->swapContent(static_cast<std::size_t>(from), static_cast<std::size_t>(to));
            return true;
        });
    }
    if (!selectedTree_ || content->tree() != selectedTree_) return false;
    const auto& values = selectedTree_->starts();
    const auto it = std::find(values.begin(), values.end(), content);
    if (it == values.end()) return false;
    const auto from = static_cast<std::ptrdiff_t>(std::distance(values.begin(), it));
    const auto to = from + delta;
    if (to < 0 || static_cast<std::size_t>(to) >= values.size()) return false;
    const auto tree = selectedTree_;
    auto* const owner = tree->owner();
    if (!owner || std::find(owner->trees().begin(), owner->trees().end(), tree) == owner->trees().end()) return false;
    const auto treeId = tree->objectId();
    const auto model_guard = historyOwnerGuard(owner);
    const std::weak_ptr<const int> author_life = paletteLifetime_.token;
    const auto deleting = model_guard ? historyModelDeleting_ : comm_.deletingContent;
    const auto notify_model = historyModelNotify_;
    const auto first = static_cast<std::size_t>(from);
    const auto second = static_cast<std::size_t>(to);
    const auto beforeSelection = selectionSnapshot();
    // D版の storeSwap/UndoSwap と同様、既存のスタートを再び交換する。 所有者全体のスナ
    // ップショットを使うと、無関係なツリーまで無効化し、すべての contentId を再生成してしまう。
    const auto swapLive = [owner, treeId, first, second, model_guard, deleting]() -> EventTreePtr {
        if (model_guard) model_guard();
        const auto live = findTreeById(owner, treeId);
        if (!live || first >= live->starts().size() || second >= live->starts().size()) {
            if (model_guard) throw std::logic_error("Event swap history target no longer exists");
            return {};
        }
        const auto left = live->starts()[first], right = live->starts()[second];
        if (deleting) deleting(left);
        if (model_guard) model_guard();
        if (deleting) deleting(right);
        if (model_guard) model_guard();
        // 行の無効化通知によって、表示更新・再選択・編集が同期的に実行される場合がある。
        if (findTreeById(owner, treeId) != live || first >= live->starts().size() ||
            second >= live->starts().size() || live->starts()[first] != left ||
            live->starts()[second] != right) {
            if (model_guard) throw std::logic_error("Event swap history changed during deletion notification");
            return {};
        }
        live->swapStart(first, second);
        return live;
    };
    if (!swapLive()) return false;
    if (author_life.expired()) return false;
    const auto afterSelection = selectionSnapshot();
    if (undo_) pushHistory(
        [this, swapLive, beforeSelection, author_life, notify_model] {
            if (const auto live = swapLive()) {
                if (!author_life.expired()) {
                    restoreSelection(beforeSelection);
                    notify(live, {}, true);
                } else if (notify_model) notify_model();
            }
        },
        [this, swapLive, afterSelection, author_life, notify_model] {
            if (const auto live = swapLive()) {
                if (!author_life.expired()) {
                    restoreSelection(afterSelection);
                    notify(live, {}, true);
                } else if (notify_model) notify_model();
            }
        }, !model_guard);
    notify(tree, {}, true);
    return true;
}

bool EventView::canCut1Content() const noexcept {
    const auto content = selectionContent();
    return !readOnly_ && content &&
           content->type() != types::CType::Start &&
           static_cast<bool>(content->parent());
}
bool EventView::canDel1Content() const noexcept { return canCut1Content(); }
bool EventView::canCopy1Content() const noexcept {
    // D版の単一コンテントのコピーと同様、左側の所有者ツリーにフォーカスがあっても EventTreeView.selection
    // を参照する。通常のコピーはフォーカスのある側を対象にする。
    return static_cast<bool>(selectionContent());
}
bool EventView::canPasteInsert() const noexcept {
    // D版の EventTreeView::canPasteInsert と同様、挿入可否はペインのフォー
    // カスではなく、 有効で書き込み可能なイベントグラフと、対応するクリップボードデータの有無で判定する。
    return !readOnly_ && selectedTree_ && clipboardOffersPaste();
}
bool EventView::canSwapToParent() const noexcept {
    // D版の canSwapToParent と同様、ペインのフォーカスにかかわらず EventTreeView.selection を参照する。
    const auto content = selectionContent();
    if (readOnly_ || !content) return false;
    const auto parent = content->parent();
    if (!parent || parent->type() == types::CType::Start || !parent->parent())
        return false;
    if (parent->next().size() != 1U) return false;
    return content->detail().owner;
}
bool EventView::canSwapToChild() const noexcept {
    // D版の canSwapToChild と同様、フォーカスとは独立して保持されている右側ツリーの選択を使う。
    const auto content = selectionContent();
    if (readOnly_ || !content) return false;
    if (content->type() == types::CType::Start) return false;
    if (content->next().size() != 1U) return false;
    const auto& child = content->next().front();
    return child && child->detail().owner && content->parent();
}

void EventView::swapParentChildContent(const ContentPtr& par, const ContentPtr& next) {
    const auto parPar = par ? par->parent() : ContentPtr{};
    if (!par || !next || !parPar) return;
    const auto* model = comm_.modelProps;
    const auto parName = par->name();
    const auto nextName = next->name();
    const auto parNType = types::fromCNextType(par->detail().nextType);
    const auto nextNType = types::fromCNextType(next->detail().nextType);
    const auto parParNType = types::fromCNextType(parPar->detail().nextType);
    const auto found = std::find(parPar->next().begin(), parPar->next().end(), par);
    if (found == parPar->next().end()) return;
    const auto parIndex = static_cast<std::size_t>(
        std::distance(parPar->next().begin(), found));
    // 子が持つ子要素が一つ以下なら、そのテキストも交換する。
    const bool replaceName = next->next().size() <= 1U;

    parPar->remove(par);
    par->remove(next);
    if (replaceName) {
        String cName;
        if (next->next().size() == 1U) {
            const auto c = next->next().front();
            cName = c->name();
            next->remove(c);
            c->setName(model, nextName);
            par->add(model, c);
        }
        next->setName(model, parName);
        par->setName(model, cName);
        adjustText(next, par, nextNType);
        parPar->insert(model, parIndex, next);
        next->add(model, par);
    } else {
        const std::vector<ContentPtr> children = next->next();
        for (const auto& c : children) {
            if (!c) continue;
            next->remove(c);
            adjustText(par, c, nextNType);
            par->add(model, c);
        }
        par->setName(model, nextName);
        next->setName(model, parName);
        // 名前は交換済みなので、lastNextType も交換する。
        adjustText(parPar, next, parParNType);
        parPar->insert(model, parIndex, next);
        adjustText(next, par, parNType);
        next->add(model, par);
    }
}

bool EventView::swapToParent() {
    if (!canSwapToParent()) return false;
    const auto content = selectionContent();
    const auto parent = content->parent();
    if (!parent || !parent->parent()) return false;
    // D版の swapToParent() と同様、親とこのコンテントを
    // 交換し、選択を親の元の位置へ移す。 swapToPCImpl と
    // 同様、所有者内の全ツリーではなく parPar だけを保存する。
    return mutateSubtree(parent->parent(), [this, parent, content] {
        swapParentChildContent(parent, content);
        selectedContent_ = content;
        contentSelectionActive_ = true;
        return true;
    });
}

bool EventView::swapToChild() {
    if (!canSwapToChild()) return false;
    const auto content = selectionContent();
    const auto child = content->next().front();
    if (!child) return false;
    // D版の swapToChild() と同様、このコン
    // テントと唯一の子を交換し、選択を子の位置へ移す。 交
    // 換前に、このコンテントを含む部分ツリーを保存する。
    return mutateSubtree(content->parent(), [this, content, child] {
        swapParentChildContent(content, child);
        selectedContent_ = content;
        contentSelectionActive_ = true;
        return true;
    });
}

bool EventView::canCopySelection() const noexcept {
    if (rightPaneActive()) return selectedContent_ != nullptr;
    // D版の Cards 操作と同様、EventTreeView のコンテント
    // 選択が保持されていても、 Cards コントロール自身の選択状態を使う。
    if (!cards_ || cards_->selection.empty()) return false;
    const auto id = cards_->selection.front();
    const auto* info = nodeInfo(id);
    if (!info || (info->kind != NodeInfo::Kind::EventTree &&
                  info->kind != NodeInfo::Kind::Fire)) return false;
    const auto row = std::find_if(cards_->nodes.begin(), cards_->nodes.end(),
        [id](const lib::TreeNode& value) { return value.id == id; });
    return row != cards_->nodes.end() && row->parent != 0;
}

bool EventView::clipboardAvailable() const noexcept {
    try {
        if (!rightPaneActive() && dynamic_cast<area::PlayerCardEvents*>(selectionParent()))
            if (const auto summary = comm_.summary.lock(); summary && summary->legacy())
                return false;
        return clipboardOffersPaste() &&
               (!rightPaneActive() || static_cast<bool>(selectedTree_));
    } catch (...) {
        return false;
    }
}

bool EventView::clipboardOffersPaste() const noexcept {
    try {
        const auto data = readClipboard();
        // D版の CBisText/CBisXML と同様、イベントとして
        // 解析できるかではなく転送形式への対応を調べる。 空白だけのテキス
        // トも貼り付け可能とし、解析とエラーの通知は貼り付け処理に任せる。
        return !data.text.empty() ||
            data.formats.contains("text/plain") ||
            data.formats.contains("text/plain;charset=utf-8") ||
            data.formats.contains("text/xml") ||
            data.formats.contains(std::string(kClipboardMime)) ||
            data.formats.contains(std::string(xmlbytestransfer::XML_BYTES_FORMAT)) ||
            data.formats.contains(std::string(xmlbytestransfer::XML_BYTES_MIME));
    } catch (...) {
        return false;
    }
}

String EventView::makeClipboard(const StringView kind, const xml::XNode& node) const {
    auto root = xml::XNode::create(kClipboardRoot);
    root.newAttr("kind", kind);
    root.newAttr("paneId", id_);
    root.append(node.clone());
    return root.text();
}

String EventView::itemToXML(const TreeItemId id, const bool canFire, const bool paneId) const {
    auto itemId = id;
    if (!itemId) {
        if (rightPaneActive()) {
            if (selectedContent_) itemId = selectedContentId_;
        } else if (cards_ && !cards_->selection.empty()) {
            itemId = cards_->selection.front();
        }
    }
    const auto* info = nodeInfo(itemId);
    if (!info) return {};
    String kind;
    xml::XNode node;
    // D版と同様、クリップボードでは実際の実行環境の System と最新の交換形式バージョンを使う。
    const ::cwxeditor::cwx::card::XMLOption option(
        comm_.modelProps ? &comm_.modelProps->sys() : nullptr,
        ::cwxeditor::cwx::versioninfo::LATEST_VERSION);
    if (info->kind == NodeInfo::Kind::EventTree && info->tree) {
        kind = "event";
        node = info->tree->toNode(&option);
    } else if (info->kind == NodeInfo::Kind::Content && info->content) {
        kind = "content";
        if (info->content->type() == types::CType::Start) {
            // D版と同様、選択したスタートをルートの Content として直列化し、モデルの制約を維持する。
            node = info->content->toNode(&option);
        } else {
            auto wrapper = std::make_shared<event::EventTree>("Clipboard");
            wrapper->starts().front()->add(info->content->dup());
            node = wrapper->toNode();
        }
    } else if (info->kind == NodeInfo::Kind::Fire && canFire && info->tree) {
        kind = "fire";
        node = xml::XNode::create("Fire");
        node.newAttr("kind", fireKindText(info->fire.kind));
        if (info->fire.kind == FireKind::KeyCode) {
            node.newAttr("value", info->fire.keyCode.keyCode);
            node.newAttr("timing", static_cast<std::uint32_t>(info->fire.keyCode.kind));
        } else if (info->fire.kind == FireKind::Round) {
            node.newAttr("value", info->fire.round);
        }
    } else if (info->kind == NodeInfo::Kind::Fire && info->tree) {
        kind = "event";
        node = info->tree->toNode(&option);
    } else {
        return {};
    }
    auto result = makeClipboard(kind, node);
    if (kind == "content" && info->content) {
        try {
            auto root = xml::XNode::parse(result);
            root.newAttr("contentId", info->content->eventId());
            const auto nextType = info->content->parent()
                                      ? info->content->parent()->detail().nextType
                                      : types::CNextType::Text;
            root.newAttr("lastNextType", types::fromCNextType(nextType));
            result = root.text();
        } catch (...) {
            return {};
        }
    }
    if (!paneId) {
        try {
            auto root = xml::XNode::parse(result);
            root.remove_attr("paneId");
            result = root.text();
        } catch (...) {
            return {};
        }
    }
    return result;
}

lib::ClipboardData EventView::clipboardData() const {
    lib::ClipboardData result;
    result.text = itemToXML(0, true, true);
    if (!result.text.empty()) {
        std::vector<std::byte> bytes;
        bytes.reserve(result.text.size());
        for (const auto character : result.text) bytes.push_back(static_cast<std::byte>(character));
        result.formats.emplace(String(kClipboardMime), std::move(bytes));
    }
    return result;
}

void EventView::writeClipboard(const lib::ClipboardData& data) {
    clipboardMemory_ = data;
    hasClipboardMemory_ = !data.text.empty();
    if (const auto clipboard = comm_.clipboard()) clipboard->write(data);
}

lib::ClipboardData EventView::readClipboard() const {
    if (const auto clipboard = comm_.clipboard()) {
        // D版の eventview.d と同様、テキストに加えてネイティブの XMLBytes
        // 転送を受け付ける。 形式を選んで読み取る動作を維持し、既存の共通コーデックで復号
        // する。 明示的なXMLコピーでバイナリ形式だけを使った場合、テキストは存在しない。
        const std::string_view requested[]{kClipboardMime, xmlbytestransfer::XML_BYTES_FORMAT,
            xmlbytestransfer::XML_BYTES_MIME, "text/xml", "text/plain;charset=utf-8", "text/plain"};
        auto value = clipboard->read_formats(requested);
        if (value.text.empty()) value.text = xmlbytestransfer::CBtoXML(value);
        if (!value.text.empty() || !value.formats.empty()) return value;
    }
    return clipboardMemory_;
}

bool EventView::copy() {
    if (!canCopySelection()) return false;
    const auto data = clipboardData();
    if (data.text.empty()) return false;
    const auto* props = comm_.editorProps ? comm_.editorProps() : nullptr;
    const bool xml_copy = comm_.xmlCopyProvider ? comm_.xmlCopyProvider()
                                                : props && props->var().xmlCopy.value;
    // D版の EventView.copy は XMLtoCB に委譲し、現在の XMLCopy
    // 設定に従って 通常は XMLBytes、有効時はテキストのどちらか一形式だけを転送する。
    xmlbytestransfer::ClipData transfer;
    xmlbytestransfer::XMLtoCB(xml_copy, transfer, data.text);
    writeClipboard(transfer.getContents());
    if (comm_.refreshToolBar) comm_.refreshToolBar();
    return true;
}

bool EventView::cut() {
    if (!canDoT() || !copy()) return false;
    if (rightPaneActive() && selectedContent_) return deleteContent();
    if (const auto fire = selectionFire()) return removeFire(*fire);
    return deleteEvent();
}

std::optional<EventView::ClipboardPayload> EventView::parseClipboard(const StringView text) const {
    if (text.empty()) return std::nullopt;
    try {
        auto root = xml::XNode::parse(text);
        if (root.name() == "Event") return ClipboardPayload{"event", root};
        const auto name = root.name();
        if (name == "IgniteWithEnter" || name == "IgniteWithRunAway" ||
            name == "IgniteWithLose" || name == "IgniteWithEveryRound" ||
            name == "IgniteWithRoundEnd" || name == "IgniteWithRound0" ||
            name == event::ROUNDS_XML_NAME || name == card::KEY_CODES_XML_NAME)
            return ClipboardPayload{"fire", root};
        // 明示的なXMLコピーは Content（または ContentsLine
        // ）を直接出力し、 通常のコピーは Event で包む。任意のXML
        // ルートを受け付ける前に、 D版と同じコアのデコーダーを通す。
        if (root.name() != kClipboardRoot) {
            if (event::Content::createFromNode(root))
                return ClipboardPayload{"single-content", root};
            return std::nullopt;
        }
        const auto kind = root.attr("kind", false, "event");
        auto node = root.child("Event", false);
        if (!node) node = root.child("Fire", false);
        if (!node && kind == "content") {
            // Content::toNode() は、子が一つのルートのスタートを ContentsLine として
            // 出力することがある。 単独の Content のどちらの形式も、XMLコピーと共通のデコーダーに渡す。
            auto contentNode = root.child("Start", false);
            if (!contentNode) contentNode = root.child("ContentsLine", false);
            if (contentNode)
                return ClipboardPayload{"single-content", contentNode,
                                        root.attr("lastNextType", false, "")};
        }
        if (!node) return std::nullopt;
        return ClipboardPayload{kind, node, root.attr("lastNextType", false, "")};
    } catch (...) {
        return std::nullopt;
    }
}

bool EventView::paste() {
    return pasteData(readClipboard());
}

bool EventView::pasteWithInsertFirst(const bool value, const bool tryInsert) {
    // paste() の同期通知によって、この EventView が破棄さ
    // れることがある。 呼び出しをまたいで保持するのは弱参照のトークンだけと
    // し、 この表示が存続している場合に限り、入れ子の上書き設定を復元する。
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto previous = pasteInsertFirstOverride_;
    pasteInsertFirstOverride_ = value;
    try {
        const bool result = tryInsert ? pasteText(readClipboard().text, true) : paste();
        if (!life.expired()) pasteInsertFirstOverride_ = previous;
        return result;
    } catch (...) {
        if (!life.expired()) pasteInsertFirstOverride_ = previous;
        throw;
    }
}

bool EventView::pasteData(const lib::ClipboardData& data) {
    return pasteText(data.text, false);
}

bool EventView::pasteInsert() {
    return pasteText(readClipboard().text, true);
}

bool EventView::pasteInsert(const lib::CommandGesture gesture) {
    return pasteWithInsertFirst(contentInsertFirst_ != hasControl(gesture), true);
}

bool EventView::pasteText(const StringView text, const bool tryInsert) {
    if (readOnly_ || text.empty()) return false;
    if (!rightPaneActive() && dynamic_cast<area::PlayerCardEvents*>(selectionParent()))
        if (const auto summary = comm_.summary.lock(); summary && summary->legacy())
            return false;
    if (const auto payload = parseClipboard(text)) {
        // D版の EventView と同様、左側にフォーカスがあるXML貼り付けは Cards ツリーへ渡
        // す。 対象は Event/Fire のデータだけで、グラフの Content XML は無視する。
        if (!tryInsert && !rightPaneActive() && payload->kind != "event" &&
            payload->kind != "fire") return false;
        return pastePayload(*payload, tryInsert);
    }
    const auto first = text.find_first_not_of(" \t\r\n");
    // 未対応または不正なXMLデータをスクリプト入力として扱わない。
    if (first == StringView::npos || text[first] == '<') return false;
    return pasteScript(text, tryInsert, !rightPaneActive());
}

bool EventView::pastePayload(const ClipboardPayload& payload, const bool tryInsert) {
    if (payload.kind == "event") {
        std::optional<::cwxeditor::cwx::system::XMLInfo> version;
        if (comm_.modelProps)
            version.emplace(comm_.modelProps->sys(), ::cwxeditor::cwx::versioninfo::LATEST_VERSION);
        auto tree = event::EventTree::createFromNode(payload.node, version ? &*version : nullptr);
        return tree && pasteEvent(tree);
    }
    if (payload.kind == "content") {
        auto tree = event::EventTree::createFromNode(payload.node);
        return tree && pasteContent(tree, tryInsert, payload.lastNextType);
    }
    if (payload.kind == "single-content") {
        std::optional<::cwxeditor::cwx::system::XMLInfo> version;
        if (comm_.modelProps)
            version.emplace(comm_.modelProps->sys(), ::cwxeditor::cwx::versioninfo::LATEST_VERSION);
        const auto content = event::Content::createFromNode(
            payload.node, version ? &*version : nullptr);
        return content && pasteContents({content}, tryInsert, payload.lastNextType);
    }
    if (payload.kind == "fire") return pasteFire(payload);
    return false;
}

bool EventView::pasteEvent(const EventTreePtr& value) {
    if (!value || !area_ || readOnly_) return false;
    auto* owner = selectionParent();
    if (!owner) owner = area_;
    std::size_t index = owner->trees().size();
    // D版の通常の左側イベント貼り付けと同様、所有者のイベント一覧の末尾に追加する。
    auto copy = value->dup();
    if (!mutate([this, owner, copy, index] {
            owner->insert(index, copy);
            selectedTree_ = copy;
            selectedContent_.reset();
            contentSelectionActive_ = false;
            selectedId_ = 0;
            selectedContentId_ = 0;
            return true;
        })) return false;
    notify(copy, {}, true);
    return true;
}

bool EventView::pasteContent(const EventTreePtr& value, const bool tryInsert,
                             const StringView lastNextType) {
    if (!value || value->starts().empty() || !selectedTree_ || readOnly_) return false;
    return pasteContents(value->starts().front()->next(), tryInsert, lastNextType);
}

void EventView::adjustText(const ContentPtr& owner, const ContentPtr& evt,
                           const StringView lastNextType) const {
    if (readOnly_ || !owner || !evt || !prop_.etc.adjustContentName) return;
    const auto textLike = [](const types::CNextType value) {
        return value == types::CNextType::Text || value == types::CNextType::Coupon;
    };
    const auto ownerType = owner->detail().nextType;
    if (!lastNextType.empty()) {
        const auto lastType = types::toCNextType(lastNextType);
        if (ownerType != lastType && !(textLike(ownerType) && textLike(lastType))) {
            // 次の型が異なるため、先に分岐テキストを消す。
            evt->setName({});
            return;
        }
    }
    if (textLike(ownerType)) return;
    // 兄弟が同じテキストを持つ場合は消去し、所有者に残りの値を割り当て
    // させる （○なら×、Default なら次のステップ値など）。
    for (const auto& child : owner->next()) {
        if (child && child != evt && child->name() == evt->name()) {
            evt->setName({});
            return;
        }
    }
}

bool EventView::pasteContents(const std::vector<ContentPtr>& values,
                              const bool tryInsert, const StringView lastNextType) {
    if (readOnly_ || !selectedTree_) return false;

    std::vector<ContentPtr> starts;
    std::vector<ContentPtr> contents;
    starts.reserve(values.size());
    contents.reserve(values.size());
    for (const auto& value : values) {
        if (!value) continue;
        auto copy = value->dup();
        if (copy->type() == types::CType::Start) starts.push_back(std::move(copy));
        else contents.push_back(std::move(copy));
    }
    if (starts.empty() && contents.empty()) return false;

    // D版の putContents() と同様、テンプレートがス
    // タートも生成する場合は挿入置換を無効にする。 スタートは最上
    // 位に挿入し、通常のコンテントは本来の所有者の位置に配置する。
    const bool insertContents = tryInsert && starts.empty();

    const auto selected = canonicalSelectedContent();
    auto target = selected;
    if (!contents.empty()) {
        if (target && !insertContents && !target->detail().owner)
            target = target->parent();
        // D版の putContents() と同様、通常の挿入先が未選択でもス
        // タートは取り込む。 通常のコンテントだけを貼り付ける場合のみ拒否する。
        if (!target && starts.empty()) return false;
    }

    const auto lastDescendant = [](ContentPtr content) {
        while (content && !content->next().empty()) {
            const auto& child = content->next().back();
            if (!child) break;
            content = child;
        }
        return content;
    };
    // D版の通常の addContents と同様、挿入先の所有者だけを保
    // 存する。 スタートを含む追加や挿入・接ぎ木は、既存の処理経路を使う。
    const bool scopedContentPaste = starts.empty() && !insertContents;
    const bool insertFirst = pasteInsertFirstOverride_.value_or(contentInsertFirst_);
    auto change = [this, starts = std::move(starts), contents = std::move(contents),
                   target, selected, insertContents, insertFirst, lastDescendant,
                   lastNextType = String(lastNextType)]() mutable {
        ContentPtr selectedResult;
        std::size_t startIndex = selectedTree_->starts().size();
        if (selected) {
            const auto selectedStart = selected->parentStart();
            const auto found = std::find(selectedTree_->starts().begin(),
                                         selectedTree_->starts().end(), selectedStart);
            if (found != selectedTree_->starts().end())
                startIndex = static_cast<std::size_t>(
                    std::distance(selectedTree_->starts().begin(), found)) + 1U;
        }

        // D版の addStarts() と同様、取り込んだスタートの名前を重複し
        // ないようにし、 併せて取り込むコンテント内の参照を書き換えてから、一つの
        // 取り消し履歴に保存する。 ここでも一回の変更処理内でこの順序を維持する。
        for (auto& start : starts) {
            const auto oldName = start->name();
            const auto uniqueName = ::cwxeditor::cwx::utils::createNewName(
                oldName, [this, &starts, &start](const String candidate) {
                    if (selectedTree_->hasStart(candidate)) return false;
                    return std::none_of(starts.begin(), starts.end(),
                        [&start, &candidate](const ContentPtr& other) {
                            return other && other != start && other->name() == candidate;
                        });
                }, true, eventTreeNames(prop_, *selectedTree_));
            if (uniqueName != oldName) {
                start->setName(uniqueName);
                std::vector<ContentPtr> pending;
                // D版と同様、通常の挿入先が有効な混在貼り付けでのみ refCS
                // を渡す。 スタートだけの場合は参照を書き換えずに改名する。
                // 混在時の refCS には、改名したスタート自身も含む。
                if (!contents.empty() && target) {
                    pending = contents;
                    for (const auto& imported : starts)
                        if (imported) pending.push_back(imported);
                }
                while (!pending.empty()) {
                    auto current = std::move(pending.back());
                    pending.pop_back();
                    if (!current) continue;
                    if ((current->type() == types::CType::LinkStart ||
                         current->type() == types::CType::CallStart) &&
                        current->start() == oldName)
                        current->start(uniqueName);
                    pending.insert(pending.end(), current->next().begin(),
                                   current->next().end());
                }
            }
            selectedTree_->insert(startIndex++, start);
            selectedResult = lastDescendant(start);
        }

        if (!contents.empty() && target) {
            ContentPtr insertionOwner = target;
            ContentPtr graftOwner;
            if (insertContents && selected && selected->parent()) {
                // D版と同様、ルートと子を先行順でたどり、最後の所有者を保
                // 持する。 後入れ先出しの作業リストには逆順で追加する。
                std::vector<ContentPtr> pending(contents.rbegin(), contents.rend());
                while (!pending.empty()) {
                    auto current = std::move(pending.back());
                    pending.pop_back();
                    if (!current) continue;
                    if (current->detail().owner) graftOwner = current;
                    pending.insert(pending.end(), current->next().rbegin(),
                                   current->next().rend());
                }
            }
            if (graftOwner) {
                // D版の addContents と同様、取り込んだツリーの走査中に
                // allLast を記録し、 その後で元の選択を取り込み先の配下に
                // 接ぎ木する。 接ぎ木後に選択すると、元の部分ツリーへ移ってしまう。
                const auto importedSelection = lastDescendant(contents.back());
                const auto parent = selected->parent();
                const auto found = std::find(parent->next().begin(), parent->next().end(),
                                             selected);
                if (found == parent->next().end()) return false;
                const auto index = static_cast<std::size_t>(
                    std::distance(parent->next().begin(), found));
                contents.front()->setName(selected->name());
                parent->remove(selected);
                if (prop_.etc.adjustContentName) selected->setName("");
                graftOwner->add(comm_.modelProps, selected);
                parent->insert(comm_.modelProps, index, contents.front());
                for (std::size_t index2 = 1; index2 < contents.size(); ++index2)
                    parent->add(comm_.modelProps, contents[index2]);
                selectedResult = importedSelection;
            } else {
                if (!insertionOwner || !insertionOwner->detail().owner) return false;
                std::size_t insertIndex = 0;
                for (const auto& content : contents) {
                    // D版の addContents() と同様、追加のたびに先に adjustText()
                    // を呼ぶ。 取り込んだ兄弟の順序を保つため、昇順の位置に挿入する。
                    adjustText(insertionOwner, content, lastNextType);
                    // D版の pasteContents と同様、BranchFlag の直下に子
                    // を追加するときだけ、 XMLの TRUE/FALSE ラベルを正規化する。
                    if (insertionOwner->type() == types::CType::BranchFlag &&
                        comm_.modelProps) {
                        const auto& messages = comm_.modelProps->requiredMsgs();
                        if (content->name() == messages.flagOn.value)
                            content->setName(comm_.modelProps->sys().evtChildTrue());
                        else if (content->name() == messages.flagOff.value)
                            content->setName(comm_.modelProps->sys().evtChildFalse());
                    }
                    if (insertFirst) insertionOwner->insert(comm_.modelProps, insertIndex++, content);
                    else insertionOwner->add(comm_.modelProps, content);
                    selectedResult = lastDescendant(content);
                }
            }
        }
        // D版の putContents は addContents の後で addStarts
        // を呼ぶため、 混在貼り付けの最終選択は最後のスタートの部分ツリーに属する。
        if (!starts.empty()) selectedResult = lastDescendant(starts.back());
        if (!selectedResult) return false;
        selectedContent_ = selectedResult;
        contentSelectionActive_ = true;
        return true;
    };
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const bool pasted = scopedContentPaste ? mutateSubtree(target, change)
                                           : mutate(change);
    // D版の eventtreeview.d の addContents()/addStarts()
    // と同様、最後の挿入行を選び、 showSelection() で表示する。共通
    // の変更通知による更新でグラフが再構築された後、 その有効な行を表示範囲に入れる。
    if (pasted && !life.expired()) revealSelectedEventContent();
    return pasted;
}

bool EventView::pasteFire(const ClipboardPayload& payload) {
    const auto tree = selectionEventTree();
    auto* owner = selectionParent();
    if (readOnly_ || !tree || !owner) return false;
    return mutate([tree, owner, &payload] { return insertFireNode(tree, owner, payload.node); });
}

bool EventView::clone() {
    if (readOnly_ || !canCopySelection()) return false;
    if (!rightPaneActive()) {
        // D版の複製と同様に canFire=false とする。通常のコピーでは Fire
        // 単独もコピーする。 フォーカスの切り替えだけでは、保持中のグラフ項目IDを上書きしな
        // い。 左側にフォーカスがあるときは、そのツリーで実際に選択している項目を複製する。
        const auto leftId = cards_ && !cards_->selection.empty()
            ? cards_->selection.front() : selectedId_;
        const auto payload = parseClipboard(itemToXML(leftId, false, false));
        return payload && pastePayload(*payload, false);
    }
    // D版の memoryMode と同様、複製にはOSクリップボードのどの形式も使わ
    // ない。 挿入先の選択を変更する間、選択コンテントのデータをローカルに保持する。
    const auto payload = parseClipboard(itemToXML(selectedContentId_, true, false));
    if (!payload) return false;
    // D版の clone() と同様、通常の貼り付け処理に先立って親を選択す
    // る。 これにより、スタート以外の複製が誤って子にならず、兄弟になる。
    if (rightPaneActive() && selectedContent_) {
        if (const auto parent = selectedContent_->parent()) {
            const auto found = contentIds_.find(parent.get());
            if (found == contentIds_.end() || !selectContent(found->second)) return false;
        }
    }
    return pastePayload(*payload, false);
}

void EventView::beginDrag() {
    dragSourceId_ = 0;
    if (readOnly_) return;
    // SWT の EventDragSource と同様、自身のエディタコントロールに
    // フォーカスがあるときだけ開始する。 フォーカスを取得できない環境では従来どおり許
    // 可するが、 別の場所にあると分かっている場合は、残っている選択行を移動しない。
    if (const auto gui = comm_.services.gui) {
        if (const auto focused = gui->focused_widget(); focused &&
            focused != eventGraphCanvas_) return;
    }
    (void)canonicalSelectedContent();
    const auto candidate = rightPaneActive() && selectedContent_ &&
                                   selectedContentId_ != 0
                               ? selectedContentId_
                               : selectedId_;
    if (const auto* info = nodeInfo(candidate);
        info && info->kind == NodeInfo::Kind::Content && info->content &&
        info->content->type() != types::CType::Start) {
        dragSourceId_ = candidate;
    }
    dragMoveHandled_ = false;
}

lib::ClipboardData EventView::dragData() const {
    lib::ClipboardData result;
    if (!dragSourceId_) return result;
    result.text = itemToXML(dragSourceId_, false, true);
    if (!result.text.empty()) {
        std::vector<std::byte> bytes;
        bytes.reserve(result.text.size());
        for (const auto character : result.text)
            bytes.push_back(static_cast<std::byte>(character));
        result.formats.emplace(String(kClipboardMime), std::move(bytes));
    }
    return result;
}

bool EventView::drop(lib::DragEvent& event) {
    if (readOnly_ || event.data.text.empty()) return false;
    const auto payload = parseClipboard(event.data.text);
    if (!payload || payload->kind != "content") return false;

    auto target = canonicalSelectedContent();
    if (target && !target->detail().owner) target = target->parent();
    if (!target || !target->detail().owner) return false;

    const bool insert_first = contentInsertFirst_ !=
        (!lib::editor_window_host_options().all_mode && hasControl(event.modifiers, event.physical_control));

    String sourcePane;
    String sourceContentId;
    try {
        const auto root = xml::XNode::parse(event.data.text);
        if (root.name() == kClipboardRoot) {
            sourcePane = root.attr("paneId", false, "");
            sourceContentId = root.attr("contentId", false, "");
        }
    } catch (...) {
        return false;
    }
    // ネイティブの転送では DragOrigin を正とする。 クリップボードや
    // 旧バックエンドのデータには、XMLの paneId による代替判定を残す。
    const bool samePane = event.same_pane() ||
                          (!sourcePane.empty() && sourcePane == id_);

    // D版の selParent 判定と同様、転送元のウィジェットではなく、直列
    // 化された contentId を使う。 同じイベントモデルを別ペインに表示
    // している場合も、 コンテントを自身の配下へドロップする操作を拒否できる。
    if (!sourceContentId.empty()) {
        for (auto current = target; current; current = current->parent())
            if (current->eventId() == sourceContentId) return false;
    }

    NodeInfo source;
    bool haveSource = false;
    if (dragSourceId_) {
        if (const auto* info = nodeInfo(dragSourceId_)) {
            source = *info;
            haveSource = true;
        }
    }
    if (haveSource) {
        if (source.kind != NodeInfo::Kind::Content || !source.content ||
            source.content->type() == types::CType::Start)
            return false;
        if (target == source.content || target->isDescendant(source.content))
            return false;
    }

    if (samePane) {
        if (!haveSource) return false;
        const auto moving = source.content;
        const auto oldParent = moving->parent();
        if (!oldParent) return false;
        // D版と同様、新しい部分ツリーを再構築して転送元を無効化する。 両方を
        // 一つの履歴にまとめ、ネイティブの移動処理による二重削除を抑止する。
        auto parsed = event::EventTree::createFromNode(payload->node);
        if (!parsed || parsed->starts().empty() ||
            parsed->starts().front()->next().size() != 1U) return false;
        const auto copy = parsed->starts().front()->next().front()->dup();
        const auto sourceTree = moving->tree();
        const auto targetTree = target->tree();
        const bool moved = mutate([this, target, moving, oldParent, copy,
                                    sourceTree, targetTree, insert_first] {
            if (oldParent != target)
                adjustText(target, copy,
                           types::fromCNextType(oldParent->detail().nextType));
            if (insert_first) target->insert(comm_.modelProps, 0, copy);
            else target->add(comm_.modelProps, copy);
            // キャンセル中は転送元を接続したままにする。 子
            // 孫に未確定のモードレス編集がある場合も同様。
            if (comm_.deletingContent) comm_.deletingContent(moving);
            if (moving->parent() != oldParent || moving->tree() != sourceTree ||
                target->tree() != targetTree) return false;
            oldParent->remove(moving);
            selectedTree_ = targetTree;
            selectedContent_ = copy;
            contentSelectionActive_ = true;
            return true;
        });
        if (!moved) return false;
        dragMoveHandled_ = true;
        event.selected = lib::DropOperation::Move;
    } else {
        auto tree = event::EventTree::createFromNode(payload->node);
        if (!tree || tree->starts().empty()) return false;
        const auto children = tree->starts().front()->next();
        if (children.empty()) return false;
        const bool copied = mutate([this, target, children, insert_first] {
            std::size_t index = 0;
            ContentPtr selectedCopy;
            for (const auto& child : children) {
                if (!child) continue;
                const auto copy = child->dup();
                if (insert_first) target->insert(comm_.modelProps, index++, copy);
                else target->add(comm_.modelProps, copy);
                if (!selectedCopy) selectedCopy = copy;
            }
            if (!selectedCopy) return false;
            selectedContent_ = selectedCopy;
            contentSelectionActive_ = true;
            return true;
        });
        if (!copied) return false;
        // D版と同様、別ペインからのドロップは、転送元が MOVE を提示
        // していてもコピーとして扱う。 転送元ペインのコンテントは残す。
        event.selected = lib::DropOperation::Copy;
    }
    event.handled = true;
    return true;
}

lib::DragSourceBinding EventView::dragSourceBinding() {
    lib::DragSourceBinding binding;
    binding.source = cards_;
    // 別ペインへの転送は部分ツリーのコピー、同じペイン内の並べ替えは移動とす
    // る。 コピー成功時に転送元が削除されないよう、両方の操作を提示する。
    binding.allowed = lib::DropOperation::Copy | lib::DropOperation::Move;
    binding.provide_data = [this] { beginDrag(); return dragData(); };
    // ネイティブのバックエンドはXMLデータではなく DragOrigin
    // を使い、 このビューへのドロップが並べ替え（移動）か、ビュー間コ
    // ピーかを判定する。 ここを空にするとすべて外部からのドロップと見な
    // され、 ドラッグはできてもD版と同じツリー内移動を完了できない。
    binding.provide_origin = [this] {
        lib::DragOrigin origin;
        origin.pane_id = id_;
        if (const auto content = canonicalSelectedContent()) {
            origin.selected_keys.push_back(content->eventId());
            origin.cursor_index = static_cast<std::size_t>(selectedContentId_);
        }
        return origin;
    };
    binding.completed = [this](const lib::DropOperation operation) {
        if (isMove(operation) && dragSourceId_ && !dragMoveHandled_) {
            if (const auto* info = nodeInfo(dragSourceId_)) {
                if (info->kind == NodeInfo::Kind::EventTree) deleteEvent(info->tree);
                else if (info->kind == NodeInfo::Kind::Fire) (void)removeFire(info->fire, info->tree);
                else if (info->kind == NodeInfo::Kind::Content) (void)deleteContent(info->content);
            }
        }
        dragSourceId_ = 0;
        dragMoveHandled_ = false;
    };
    return binding;
}

lib::DropTargetBinding EventView::dropTargetBinding() {
    lib::DropTargetBinding binding;
    binding.target = cards_;
    binding.pane_id = id_;
    binding.accepted_formats = {String(kClipboardMime), "text/xml"};
    binding.on_enter = [this](lib::DragEvent& event) {
        if (readOnly_) {
            event.selected = lib::DropOperation::None;
            return;
        }
        event.selected = event.same_pane() ? lib::DropOperation::Move
                                           : lib::DropOperation::Copy;
    };
    binding.on_over = [this](lib::DragEvent& event) {
        if (readOnly_ || !event.target_index) {
            event.selected = lib::DropOperation::None;
            return;
        }
        const auto* info = nodeInfo(static_cast<TreeItemId>(*event.target_index));
        auto target = info && info->kind == NodeInfo::Kind::Content
                          ? info->content
                          : ContentPtr{};
        if (target && !target->detail().owner) target = target->parent();
        if (!target || !target->detail().owner) {
            event.selected = lib::DropOperation::None;
            return;
        }
        if (dragSourceId_) {
            const auto* source = nodeInfo(dragSourceId_);
            if (!source || source->kind != NodeInfo::Kind::Content ||
                !source->content || target == source->content ||
                target->isDescendant(source->content)) {
                event.selected = lib::DropOperation::None;
                return;
            }
        }
        event.selected = event.same_pane() ? lib::DropOperation::Move
                                           : lib::DropOperation::Copy;
    };
    binding.on_drop = [this](lib::DragEvent& event) {
        if (event.target_index) {
            const auto id = static_cast<TreeItemId>(*event.target_index);
            if (nodeInfo(id)) (void)select(id);
        }
        drop(event);
    };
    return binding;
}

bool EventView::dropEventList(lib::DragEvent& event) {
    const std::weak_ptr<const int> lifetime = paletteLifetime_.token;
    event.selected = lib::DropOperation::None;
    if (readOnly_ || !cards_ || !event.target_index ||
        *event.target_index >= cards_->nodes.size()) return false;
    const auto* target_info = nodeInfo(cards_->nodes[*event.target_index].id);
    if (!target_info || !target_info->owner || target_info->kind == NodeInfo::Kind::Content) return false;
    const auto target = *target_info;
    const auto summ = comm_.summary.lock();
    if (summ && summ->legacy() && dynamic_cast<::cwxeditor::cwx::area::PlayerCardEvents*>(target.owner)) return false;
    auto text = event.data.text;
    if (text.empty()) text = xmlbytestransfer::CBtoXML(event.data);
    const auto payload = parseClipboard(text);
    // D版の左側 EventView と同様、Event・発火条件のXMLだけを受け付け、右側の Content は受け付けない。
    if (!payload || (payload->kind != "event" && payload->kind != "fire")) return false;
    bool same_pane = event.same_pane();
    try { same_pane = same_pane || xml::XNode::parse(text).attr("paneId", false, "") == id_; }
    catch (...) { return false; }
    const auto source = same_pane ? eventListDragSource_ : std::optional<NodeInfo>{};
    if (source && source->kind == NodeInfo::Kind::EventTree &&
        source->owner == target.owner && source->tree) {
        const auto& trees = target.owner->trees();
        const auto from_it = std::find(trees.begin(), trees.end(), source->tree);
        if (from_it == trees.end()) return false;
        const auto from = static_cast<std::size_t>(from_it - trees.begin());
        std::size_t to = target.kind == NodeInfo::Kind::Owner ? trees.size() : target.treeIndex;
        if (to > trees.size() || from == to || from + 1U == to) return false;
        if (from < to) --to;
        if (!mutate([this, owner = target.owner, from, to, tree = source->tree] {
            auto at = from;
            while (at < to) { owner->swapEventTree(at, at + 1U); ++at; }
            while (at > to) { owner->swapEventTree(at, at - 1U); --at; }
            selectedTree_ = tree; contentSelectionActive_ = false;
            return true;
        })) return false;
    } else if (source && source->kind == NodeInfo::Kind::Fire &&
               source->fire.kind == FireKind::KeyCode && source->tree == target.tree) {
        const auto& keys = source->tree->keyCodes();
        const auto match = [](const auto& a, const auto& b) { return a.keyCode == b.keyCode && a.kind == b.kind; };
        const auto from_it = std::find_if(keys.begin(), keys.end(), [&](const auto& k) { return match(k, source->fire.keyCode); });
        if (from_it == keys.end()) return false;
        const auto from = static_cast<std::size_t>(from_it - keys.begin());
        std::size_t to = target.kind == NodeInfo::Kind::EventTree ? keys.size() : 0U;
        if (target.kind == NodeInfo::Kind::Fire && target.fire.kind == FireKind::KeyCode) {
            const auto at = std::find_if(keys.begin(), keys.end(), [&](const auto& k) { return match(k, target.fire.keyCode); });
            if (at == keys.end()) return false;
            to = static_cast<std::size_t>(at - keys.begin());
        }
        if (from == to || from + 1U == to) return false;
        if (from < to) --to;
        if (!mutate([this, tree = source->tree, fire = source->fire, from, to] {
            auto at = from;
            while (at < to) { tree->swapKeyCode(at, at + 1U); ++at; }
            while (at > to) { tree->swapKeyCode(at, at - 1U); --at; }
            selectedTree_ = tree; contentSelectionActive_ = false;
            pendingFireSelection_ = fire;
            return true;
        })) return false;
    } else if (payload->kind == "event") {
        std::optional<::cwxeditor::cwx::system::XMLInfo> version;
        if (comm_.modelProps) version.emplace(comm_.modelProps->sys(), ::cwxeditor::cwx::versioninfo::LATEST_VERSION);
        auto tree = event::EventTree::createFromNode(payload->node, version ? &*version : nullptr);
        if (!tree) return false;
        const auto at = target.kind == NodeInfo::Kind::Owner ? target.owner->trees().size() : target.treeIndex;
        if (!mutate([this, target, at, tree, source] {
            target.owner->insert(at, tree);
            if (source && source->kind == NodeInfo::Kind::EventTree && source->owner)
                source->owner->remove(source->tree);
            selectedTree_ = tree; selectedContent_.reset();
            selectedId_ = selectedContentId_ = 0; contentSelectionActive_ = false;
            return true;
        })) return false;
    } else {
        if (target.kind == NodeInfo::Kind::Owner || !target.tree) return false;
        if (!dynamic_cast<::cwxeditor::cwx::area::Area*>(area_) &&
            !dynamic_cast<::cwxeditor::cwx::area::Battle*>(area_)) return false;
        std::ptrdiff_t key_index = -1;
        if (target.kind == NodeInfo::Kind::Fire) {
            key_index = 0;
            if (target.fire.kind == FireKind::KeyCode) {
                const auto& keys = target.tree->keyCodes();
                const auto at = std::find_if(keys.begin(), keys.end(), [&](const auto& key) {
                    return key.keyCode == target.fire.keyCode.keyCode && key.kind == target.fire.keyCode.kind;
                });
                if (at == keys.end()) return false;
                key_index = at - keys.begin();
            }
        }
        if (!mutate([this, target, source, &payload, key_index] {
            const auto previous = fireConditions(target.tree);
            if (!insertFireNode(target.tree, target.owner, payload->node, key_index)) return false;
            for (const auto& fire : fireConditions(target.tree))
                if (std::none_of(previous.begin(), previous.end(), [&](const auto& old) { return sameFire(old, fire); })) {
                    pendingFireSelection_ = fire;
                    // Fire の識別情報は所属ツリー内でのみ有効。 転送時は表
                    // 示更新や履歴保存の前に、転送元のツリーIDを置き換える。
                    pendingFireTreeId_ = target.tree->objectId();
                }
            if (source && source->kind == NodeInfo::Kind::Fire && source->tree != target.tree)
                (void)eraseFireValue(source->tree, source->fire);
            selectedTree_ = target.tree; contentSelectionActive_ = false;
            return true;
        })) return false;
    }
    if (!lifetime.expired()) eventListMoveHandled_ = bool(source);
    event.selected = source ? lib::DropOperation::Move : lib::DropOperation::Copy;
    event.handled = true;
    return true;
}

void EventView::bindEventListDragDrop() {
    const auto backend = comm_.services.drag_drop;
    if (!backend || !cards_) return;
    lib::DragSourceBinding source;
    source.source = cards_;
    source.allowed = lib::DropOperation::Copy | lib::DropOperation::Move;
    source.provide_data = [this] {
        eventListDragSource_.reset(); eventListMoveHandled_ = false;
        lib::ClipboardData data;
        if (readOnly_) return data;
        if (const auto gui = comm_.services.gui; gui && gui->focused_widget() != cards_) return data;
        const auto selected = leftOrActiveSelectionId();
        const auto* info = nodeInfo(selected);
        if (!info || (info->kind != NodeInfo::Kind::EventTree && info->kind != NodeInfo::Kind::Fire)) return data;
        eventListDragSource_ = *info;
        data.text = itemToXML(selected, true, true);
        if (!data.text.empty()) {
            std::vector<std::byte> bytes;
            for (const auto ch : data.text) bytes.push_back(static_cast<std::byte>(ch));
            data.formats.emplace(String(kClipboardMime), std::move(bytes));
        }
        return data;
    };
    source.provide_origin = [this] { lib::DragOrigin origin; origin.pane_id = id_; return origin; };
    source.completed = [this](lib::DropOperation operation) {
        const auto prior = eventListDragSource_;
        const bool handled = eventListMoveHandled_;
        eventListDragSource_.reset(); eventListMoveHandled_ = false;
        if (isMove(operation) && !handled && prior) {
            if (prior->kind == NodeInfo::Kind::EventTree) (void)deleteEvent(prior->tree);
            else if (prior->kind == NodeInfo::Kind::Fire) (void)removeFire(prior->fire, prior->tree);
        }
    };
    backend->bind_source(std::move(source));
    lib::DropTargetBinding target;
    target.target = cards_; target.pane_id = id_;
    target.accepted_formats = {String(kClipboardMime), "text/xml", String(xmlbytestransfer::XML_BYTES_FORMAT), String(xmlbytestransfer::XML_BYTES_MIME)};
    target.on_enter = target.on_over = [this](lib::DragEvent& event) {
        event.selected = lib::DropOperation::None;
        if (readOnly_ || !cards_ || !event.target_index || *event.target_index >= cards_->nodes.size()) return;
        const auto* info = nodeInfo(cards_->nodes[*event.target_index].id);
        if (!info || !info->owner) return;
        if (const auto summ = comm_.summary.lock(); summ && summ->legacy() &&
            dynamic_cast<::cwxeditor::cwx::area::PlayerCardEvents*>(info->owner)) return;
        event.selected = event.same_pane() ? lib::DropOperation::Move : lib::DropOperation::Copy;
    };
    target.on_drop = [this](lib::DragEvent& event) { (void)dropEventList(event); };
    backend->bind_target(std::move(target));
}

void EventView::bindDragDrop() {
    const auto backend = comm_.services.drag_drop;
    if (!backend) return;
    // 左側のイベント・発火条件と右側のコンテントの操作には、それぞれ対応するD版の仕様を適用する。
    bindEventListDragDrop();
    if (eventGraphCanvas_) {
        const auto graphRowAt = [this](const lib::Point position) {
            const auto bounds = eventGraphCanvas_ ? eventGraphCanvas_->bounds()
                                                  : lib::Rect{};
            const auto local = lib::Point{position.x - bounds.x,
                                          position.y - bounds.y};
            return std::find_if(graphRows_.begin(), graphRows_.end(),
                [local](const GraphRow& row) {
                    return local.y >= row.hit.y &&
                           local.y < row.hit.y + row.hit.height;
                });
        };
        auto source = dragSourceBinding();
        source.source = eventGraphCanvas_;
        source.provide_data = [this] {
            beginDrag();
            return dragData();
        };
        source.provide_visual = [this]() -> std::optional<lib::DragVisual> {
            if (!eventGraphCanvas_ || dragSourceId_ == 0) return std::nullopt;
            const auto row = std::find_if(graphRows_.begin(), graphRows_.end(),
                [this](const GraphRow& value) { return value.id == dragSourceId_; });
            if (row == graphRows_.end()) return std::nullopt;
            const auto bounds = eventGraphCanvas_->bounds();
            return lib::DragVisual{{bounds.x + row->hit.x, bounds.y + row->hit.y,
                                    row->hit.width, row->hit.height}};
        };
        backend->bind_source(std::move(source));
        auto target = dropTargetBinding();
        target.target = eventGraphCanvas_;
        target.on_over = [this, graphRowAt](lib::DragEvent& event) {
            if (readOnly_) {
                if (dragHoverContentId_ != 0) {
                    dragHoverContentId_ = 0;
                    invalidateEventGraph();
                }
                event.selected = lib::DropOperation::None;
                return;
            }
            const auto found = graphRowAt(event.position);
            if (found == graphRows_.end()) {
                if (dragHoverContentId_ != 0) {
                    dragHoverContentId_ = 0;
                    invalidateEventGraph();
                }
                event.selected = lib::DropOperation::None;
                return;
            }
            const auto* info = nodeInfo(found->id);
            auto destination = info ? info->content : ContentPtr{};
            if (destination && !destination->detail().owner)
                destination = destination->parent();
            const auto* sourceInfo = dragSourceId_ ? nodeInfo(dragSourceId_) : nullptr;
            if (!destination || !destination->detail().owner ||
                (sourceInfo && sourceInfo->content &&
                  (destination == sourceInfo->content ||
                   destination->isDescendant(sourceInfo->content)))) {
                if (dragHoverContentId_ != 0) {
                    dragHoverContentId_ = 0;
                    invalidateEventGraph();
                }
                event.selected = lib::DropOperation::None;
            } else {
                if (dragHoverContentId_ != found->id) {
                    dragHoverContentId_ = found->id;
                    invalidateEventGraph();
                }
                event.selected = event.same_pane() ? lib::DropOperation::Move
                                                   : lib::DropOperation::Copy;
            }
        };
        target.on_leave = [this] {
            if (dragHoverContentId_ == 0) return;
            dragHoverContentId_ = 0;
            invalidateEventGraph();
        };
        target.on_drop = [this, graphRowAt](lib::DragEvent& event) {
            dragHoverContentId_ = 0;
            invalidateEventGraph();
            const auto found = graphRowAt(event.position);
            if (found == graphRows_.end() || !selectContent(found->id)) {
                event.selected = lib::DropOperation::None;
                event.handled = false;
                return;
            }
            drop(event);
        };
        backend->bind_target(std::move(target));
    }
}

void EventView::unbindDragDrop() {
    if (const auto backend = comm_.services.drag_drop; backend) {
        if (eventGraphCanvas_) backend->unbind(eventGraphCanvas_);
        if (cards_) backend->unbind(cards_);
    }
}

bool EventView::canUndo() const noexcept { return undo_ && undo_->canUndo(); }
bool EventView::canRedo() const noexcept { return undo_ && undo_->canRedo(); }

void EventView::undo() {
    if (readOnly_ || !undo_ || !undo_->canUndo()) return;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto replay = [this, life] {
        if (life.expired() || !undo_ || !undo_->canUndo()) return;
        undo_->undo();
        if (life.expired()) return;
        refresh();
        if (life.expired()) return;
        modelChanged.call();
        if (life.expired()) return;
        if (comm_.refreshToolBar) comm_.refreshToolBar();
    };
    const auto coordinator = historyReplayCoordinator_;
    if (coordinator) {
        coordinator(replay);
        return;
    }
    // 直接使用する詳細ビューでは、所有者がなくても履歴再適用前のキャンセルを行う。
    const auto cancel = cancelSameOwnerEditors_;
    if (cancel) {
        if (!cancel()) return;
    } else {
        editCancel();
    }
    replay();
}

void EventView::redo() {
    if (readOnly_ || !undo_ || !undo_->canRedo()) return;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto replay = [this, life] {
        if (life.expired() || !undo_ || !undo_->canRedo()) return;
        undo_->redo();
        if (life.expired()) return;
        refresh();
        if (life.expired()) return;
        modelChanged.call();
        if (life.expired()) return;
        if (comm_.refreshToolBar) comm_.refreshToolBar();
    };
    const auto coordinator = historyReplayCoordinator_;
    if (coordinator) {
        coordinator(replay);
        return;
    }
    // 直接使用する詳細ビューでは、所有者がなくても履歴再適用前のキャンセルを行う。
    const auto cancel = cancelSameOwnerEditors_;
    if (cancel) {
        if (!cancel()) return;
    } else {
        editCancel();
    }
    replay();
}

bool EventView::forceSel(const std::vector<std::size_t>& eventPath) {
    if (!area_ || eventPath.empty()) return false;
    const auto owners = etos(*area_);
    const bool ownerQualified = owners.size() > 1U && eventPath.size() > 1U;
    const auto ownerIndex = ownerQualified ? eventPath[0] : 0U;
    const auto treePathIndex = ownerQualified ? 1U : 0U;
    if (ownerIndex >= owners.size() || !owners[ownerIndex] ||
        eventPath[treePathIndex] >= owners[ownerIndex]->trees().size())
        return false;
    const auto& tree = owners[ownerIndex]->trees()[eventPath[treePathIndex]];
    if (!tree) return false;
    selectedTree_ = tree;
    if (eventPath.size() == treePathIndex + 1U) return select(findItem(tree));
    auto content = tree->fromPath({eventPath.begin() +
                                       static_cast<std::ptrdiff_t>(treePathIndex + 1U),
                                   eventPath.end()});
    if (!content || !contentIds_.contains(content.get())) {
        rebuildContents();
    }
    return content && selectContent(contentIds_.at(content.get()));
}

bool EventView::openCWXPath(const StringView value, const bool shellActivate) {
    if (!area_) return false;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    if (path::cpcategory(value) == "variable") {
        return localVariables_ &&
            localVariables_->openCWXPath(path::cpbottom(value), shellActivate);
    }
    String target(value);
    auto resolved = area_->findCWXPath(target);
    if (!resolved) {
        // この移植版で以前保存した /N;Content-ID 形式の接尾辞への互
        // 換対応は、 確認できるイベント配下の要素だけに限定する。event:N
        // のインデックスや 無関係な数値カテゴリ、検索に失敗した値は読み替えない。
        String canonical;
        String attributes;
        bool event = false;
        bool converted = false;
        bool valid = true;
        std::size_t begin = 0;
        while (begin < target.size()) {
            const auto end = target.find('/', begin);
            const auto step = StringView(target).substr(begin,
                end == String::npos ? String::npos : end - begin);
            const auto body = path::cpbody(step);
            if (body.empty()) { valid = false; break; }
            String normalized(body);
            if (event) {
                auto digits = StringView(body);
                if (digits.starts_with(':')) digits.remove_prefix(1);
                else converted = true;
                if (digits.empty() || !std::ranges::all_of(digits,
                    [](unsigned char c) { return c >= '0' && c <= '9'; })) {
                    valid = false; break;
                }
                normalized = ":" + String(digits);
            } else if (path::cpcategory(step) == "event") {
                event = true;
            }
            for (const auto& attr : path::cpattr(step)) {
                if (event && attr.starts_with("Content-")) continue;
                if (end != String::npos ||
                    (attr != "only" && attr != "nofocus" && attr != "opendialog" &&
                     attr != "eventview")) { valid = false; break; }
                attributes = path::cpaddattr(attributes, attr);
            }
            if (!valid) break;
            if (!canonical.empty()) canonical += '/';
            canonical += normalized;
            if (end == String::npos) break;
            begin = end + 1;
        }
        if (valid && event && converted) {
            target = canonical + attributes;
            resolved = area_->findCWXPath(target);
        }
    }
    if (!resolved) return false;
    const auto focus = [&](const lib::WidgetPtr& control) {
        if (!path::cphasattr(target, "nofocus") && comm_.services.gui && control)
            comm_.services.gui->focus(control);
    };
    if (const auto owner = std::dynamic_pointer_cast<EventTreeOwner>(resolved)) {
        const auto found = std::find_if(nodes_.begin(), nodes_.end(),
            [raw = owner.get()](const auto& entry) {
                return entry.second.kind == NodeInfo::Kind::Owner && entry.second.owner == raw;
            });
        if (found == nodes_.end() || !select(found->first) || life.expired()) return false;
        focus(cards_);
        return true;
    }
    if (const auto tree = std::dynamic_pointer_cast<event::EventTree>(resolved)) {
        if (!select(findItem(tree)) || life.expired()) return false;
        focus(cards_);
        return true;
    }
    // ダイアログの結果パスはデータ自体を指し、イベントグラフではその所
    // 有コンテントを選択する。 まず完全なパスを解決し、存在しないダイ
    // アログへの移動を 所有者への移動成功として扱わないようにする。
    auto content = std::dynamic_pointer_cast<event::Content>(resolved);
    if (const auto dialog = std::dynamic_pointer_cast<event::SDialog>(resolved))
        content = dialog->parent();
    if (content) {
        const auto tree = content->tree();
        if (!tree || !select(findItem(tree)) || life.expired()) return false;
        rebuildContents();
        const auto found = contentIds_.find(content.get());
        if (found == contentIds_.end() || !selectContent(found->second) || life.expired())
            return false;
        focus(eventGraphCanvas_ ? std::static_pointer_cast<lib::Widget>(eventGraphCanvas_)
                               : std::static_pointer_cast<lib::Widget>(contents_));
        if (life.expired()) return false;
        if (path::cphasattr(target, "opendialog") && !readOnly_) {
            // 明示的な移動はこの Content を対象とする。 nofocus
            // でキーボードフォーカスを左側のイベントツリーに残す場合も同様。
            const auto previous = std::exchange(navigationContent_, content);
            edit();
            if (!life.expired()) navigationContent_ = previous;
        }
        return true;
    }
    return false;
}

std::vector<String> EventView::openedCWXPath() const {
    std::vector<String> result;
    const auto complete_path = [this](String value) {
        if (!area_ || value.empty()) return value;
        const auto owner_path = area_->cwxPath(true);
        if (owner_path.empty()) return value;
        const auto body = path::cpbody(value);
        if (body == owner_path ||
            (body.size() > owner_path.size() &&
             body.starts_with(owner_path) && body[owner_path.size()] == '/'))
            return value;
        // D版の EventTreeView と同様、シナリオ内の絶対 CWXPath
        // を記録する。 切り離されたカードやイベントの複製は、一時的に menucard:N/event:N
        // のような 相対パスしか返せない場合がある。再起動後にエリア・
        // 戦闘を特定できないため、 この相対的なUIパスは永続化しない。
        return owner_path + "/" + value;
    };
    // D版と同様、左側の所有者の選択状態は右側の表示とは別に保存する。
    if (!selectionEventTree()) {
        auto* owner = selectionParent();
        if (owner) result.push_back(path::cpaddattr(complete_path(owner->cwxPath(true)), "eventview"));
    }
    if (selectedContent_) {
        auto value = complete_path(selectedContent_->cwxPath(true));
        if (!value.empty()) result.push_back(std::move(value));
    } else if (selectedTree_) {
        auto value = complete_path(selectedTree_->cwxPath(true));
        if (!value.empty()) result.push_back(std::move(value));
    }
    return result;
}

bool EventView::canSelectCurrentEvent() const noexcept {
    return selectedTree_ && selectionEventTree() != selectedTree_;
}
void EventView::selectCurrentEvent() {
    if (selectedTree_) select(findItem(selectedTree_));
}

bool EventView::canSelectConnectedResource() const noexcept {
    if (comm_.canSelectConnectedResource)
        return comm_.canSelectConnectedResource();
    return false;
}

void EventView::selectConnectedResource() {
    if (!canSelectConnectedResource()) return;
    if (comm_.selectConnectedResource) comm_.selectConnectedResource();
}

std::vector<EventTreeOwner*> EventView::etos(EventTreeOwner& owner) {
    std::vector<EventTreeOwner*> result{&owner};
    const auto appendAreaOwners = [&result](auto& area) {
        if (auto* playerEvents = area.playerEvents()) result.push_back(playerEvents);
        for (const auto& card : area.cards())
            if (card) result.push_back(card.get());
    };
    if (auto* areaOwner = dynamic_cast<::cwxeditor::cwx::area::Area*>(&owner)) {
        appendAreaOwners(*areaOwner);
    } else if (auto* battleOwner = dynamic_cast<::cwxeditor::cwx::area::Battle*>(&owner)) {
        appendAreaOwners(*battleOwner);
    }
    return result;
}

EventView* EventView::mainEventView(const std::vector<EventView*>& views) noexcept {
    for (auto* view : views) if (view) return view;
    return nullptr;
}

String EventView::contentText(const ContentPtr& content) const {
    const auto textResolver = comm_.contentText;
    const auto nameResolver = comm_.contentName;
    const std::weak_ptr<const int> life = paletteLifetime_.token;
    const auto value = formatContentText(content, textResolver, nameResolver, life,
                                         false, true);
    return value.value_or(String{});
}

bool EventView::contains(const ContentPtr& root, const ContentPtr& value) const {
    if (!root || !value) return false;
    if (root.get() == value.get()) return true;
    for (const auto& child : root->next()) if (contains(child, value)) return true;
    return false;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::eventview
