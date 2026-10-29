#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "motionview.hpp"
#include "cardpropertytransaction.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include <array>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace cwxeditor::cwx::editor::gui::dwt::motionview {
namespace {

constexpr StringView kClipboardRoot = "CWXEditorClipboard";

[[nodiscard]] String lowerCopy(const StringView value) {
    String result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](const unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return result;
}

[[nodiscard]] bool hasOperation(const lib::DropOperation value,
                                const lib::DropOperation bit) noexcept {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(bit)) != 0U;
}

[[nodiscard]] lib::DropOperation copyOperation(
    const lib::DropOperation allowed) noexcept {
    if (hasOperation(allowed, lib::DropOperation::Copy))
        return lib::DropOperation::Copy;
    if (hasOperation(allowed, lib::DropOperation::Link))
        return lib::DropOperation::Link;
    return lib::DropOperation::None;
}

[[nodiscard]] String elementName(const types::Element value) {
    switch (value) {
    case types::Element::All: return "全";
    case types::Element::Health: return "肉体";
    case types::Element::Mind: return "精神";
    case types::Element::Miracle: return "神聖";
    case types::Element::Magic: return "魔力";
    case types::Element::Fire: return "炎";
    case types::Element::Ice: return "冷気";
    }
    return "Unknown";
}

[[nodiscard]] String damageTypeName(const types::DamageType value) {
    switch (value) {
    case types::DamageType::LevelRatio: return "レベルに対応する値";
    case types::DamageType::Normal: return "値の直接入力";
    case types::DamageType::Max: return "最大値処理";
    case types::DamageType::Fixed: return "固定値";
    }
    return "Unknown";
}

[[nodiscard]] String paneName(const ParameterPane value) {
    switch (value) {
    case ParameterPane::None: return "";
    case ParameterPane::Value: return "value";
    case ParameterPane::Round: return "round";
    case ParameterPane::Ability: return "ability";
    case ParameterPane::Beast: return "beast";
    case ParameterPane::SkillPower: return "skill power";
    }
    return {};
}

[[nodiscard]] String motionGroupName(const MotionMessages& messages,
                                     const types::MType type) {
    switch (type) {
    case types::MType::Heal:
    case types::MType::Damage:
    case types::MType::Absorb: return messages.groupVitality;
    case types::MType::Paralyze:
    case types::MType::DisParalyze:
    case types::MType::Poison:
    case types::MType::DisPoison: return messages.groupPhysical;
    case types::MType::GetSkillPower:
    case types::MType::LoseSkillPower: return messages.groupSkill;
    case types::MType::Sleep:
    case types::MType::Confuse:
    case types::MType::Overheat:
    case types::MType::Brave:
    case types::MType::Panic:
    case types::MType::Normal: return messages.groupMental;
    case types::MType::Bind:
    case types::MType::DisBind:
    case types::MType::Silence:
    case types::MType::DisSilence:
    case types::MType::FaceUp:
    case types::MType::FaceDown:
    case types::MType::AntiMagic:
    case types::MType::DisAntiMagic: return messages.groupMagic;
    case types::MType::EnhanceAction:
    case types::MType::EnhanceAvoid:
    case types::MType::EnhanceDefense:
    case types::MType::EnhanceResist: return messages.groupEnhance;
    case types::MType::VanishTarget:
    case types::MType::VanishCard:
    case types::MType::VanishBeast: return messages.groupVanish;
    case types::MType::DealAttackCard:
    case types::MType::DealPowerfulAttackCard:
    case types::MType::DealCriticalAttackCard:
    case types::MType::DealFeintCard:
    case types::MType::DealDefenseCard:
    case types::MType::DealDistanceCard:
    case types::MType::DealConfuseCard:
    case types::MType::DealSkillCard:
    case types::MType::CancelAction: return messages.groupCard;
    case types::MType::SummonBeast: return messages.groupBeast;
    case types::MType::NoEffect: return messages.groupEtc;
    }
    return {};
}

[[nodiscard]] String motionRowName(const MotionMessages& messages,
                                   const types::MType type) {
    return ::cwxeditor::cwx::utils::tryFormat(
        messages.motionRowFormat, motionGroupName(messages, type),
        messages.motionName(type));
}

[[nodiscard]] String replaceFirst(String value, const StringView token,
                                   const StringView replacement) {
    const auto position = value.find(token);
    if (position != String::npos)
        value.replace(position, token.size(), replacement);
    return value;
}

[[nodiscard]] String formatTwo(const StringView pattern, const StringView first,
                               const StringView second) {
    if (pattern.find("%1$s") != StringView::npos || pattern.find("%s") != StringView::npos)
        return ::cwxeditor::cwx::utils::tryFormat(pattern, first, second);
    return replaceFirst(replaceFirst(String(pattern), "{0}", first), "{1}", second);
}

[[nodiscard]] String formatOne(const StringView pattern, const StringView value) {
    if (pattern.find("%1$s") != StringView::npos || pattern.find("%s") != StringView::npos)
        return ::cwxeditor::cwx::utils::tryFormat(pattern, value);
    return replaceFirst(String(pattern), "{0}", value);
}

[[nodiscard]] String formatNumber(const StringView pattern, const std::uint64_t value) {
    return formatOne(pattern, std::to_string(value));
}

}  // namespace

String MotionMessages::motionName(const types::MType type) const {
    if (const auto found = names.find(type); found != names.end() && !found->second.empty())
        return found->second;
    const auto value = motion::mTypeToName(type);
    return value.empty() ? "Motion" : value;
}

String MotionMessages::motionDescription(const types::MType type) const {
    if (const auto found = descriptions.find(type);
        found != descriptions.end() && !found->second.empty()) {
        return found->second;
    }
    return motionName(type);
}

String MotionMessages::elementName(const types::Element element) const {
    return ::cwxeditor::cwx::editor::gui::dwt::motionview::elementName(element);
}

String MotionMessages::damageTypeName(const types::DamageType type) const {
    return ::cwxeditor::cwx::editor::gui::dwt::motionview::damageTypeName(type);
}

bool Props::isTargetVersion(const Summary* summ, const StringView version) const {
    return !summ || summ->isTargetVersion(version);
}

bool Props::targetVersion(const Summary* summ, const StringView version) const {
    // CancelAction はWSNの対応機能ではなく、Classicエンジンの設定で判定する。
    if (editorProps) return editorProps->targetVersion(summ, version);
    return !summ || !summ->legacy();
}

MotionView::MVUndo::MVUndo(MotionView* view, Commons comm, Summary* summary)
    : view_(view), comm_(std::move(comm)), summary_(summary) {
    save(view);
}

void MotionView::MVUndo::save(MotionView* view) {
    if (view) selected_ = view->selectionIndex();
}

void MotionView::MVUndo::udb(MotionView* view) {
    selectedBefore_ = selected_;
    save(view);
}

void MotionView::MVUndo::uda(MotionView* view) {
    if (!view) return;
    if (selectedBefore_ >= 0) view->select(selectedBefore_);
    (void)view->refreshSels(true);
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void MotionView::MVUndo::dispose() {}

MotionView::UndoEdit::UndoEdit(MotionView* view, Commons comm, Summary* summary,
                               const std::ptrdiff_t index)
    : MVUndo(view, std::move(comm), summary), index_(index) {
    if (view && index >= 0) {
        if (const auto value = view->motion(static_cast<std::size_t>(index))) {
            old_ = value->dup();
            if (old_ && view->useCounter_)
                old_->setUseCounter(view->useCounter_->sub(), view->ucOwner_);
        }
    }
}

void MotionView::UndoEdit::impl() {
    auto* value = view();
    if (!value || index_ < 0) return;
    udb(value);
    const auto current = value->motion(static_cast<std::size_t>(index_));
    if (!current || !old_) return;
    auto replacement = old_->dup();
    old_->removeUseCounter();
    old_ = current->dup();
    if (old_ && value->useCounter_)
        old_->setUseCounter(value->useCounter_->sub(), value->ucOwner_);
    value->motion(static_cast<std::size_t>(index_), std::move(replacement), false);
    uda(value);
}

void MotionView::UndoEdit::undo() { impl(); }
void MotionView::UndoEdit::redo() { impl(); }
void MotionView::UndoEdit::dispose() {
    if (old_) old_->removeUseCounter();
    old_.reset();
}

MotionView::UndoSwap::UndoSwap(MotionView* view, Commons comm, Summary* summary,
                               const std::ptrdiff_t first,
                               const std::ptrdiff_t second)
    : MVUndo(view, std::move(comm), summary), first_(first), second_(second) {}

void MotionView::UndoSwap::impl() {
    auto* value = view();
    if (!value) return;
    udb(value);
    value->swap(first_, second_, false);
    uda(value);
}

void MotionView::UndoSwap::undo() { impl(); }
void MotionView::UndoSwap::redo() { impl(); }
void MotionView::UndoSwap::dispose() {}

MotionView::UndoMove::UndoMove(MotionView* view, Commons comm, Summary* summary,
                               const std::ptrdiff_t from, const std::ptrdiff_t to)
    : MVUndo(view, std::move(comm), summary), from_(from), to_(to) {}

void MotionView::UndoMove::impl() {
    auto* value = view();
    if (!value || from_ < 0 || to_ < 0) return;
    udb(value);
    if (to_ < static_cast<std::ptrdiff_t>(value->motions().size())) {
        const auto selected = value->motion(static_cast<std::size_t>(to_));
        if (selected) {
            value->removeMotion(to_, false, false, false);
            value->appendMotion(selected, from_, false, false, false);
        }
    }
    std::swap(from_, to_);
    uda(value);
}

void MotionView::UndoMove::undo() { impl(); }
void MotionView::UndoMove::redo() { impl(); }
void MotionView::UndoMove::dispose() {}

MotionView::UndoInsertDelete::UndoInsertDelete(MotionView* view, Commons comm,
                                               Summary* summary,
                                               const std::ptrdiff_t index,
                                               const bool insert)
    : MVUndo(view, std::move(comm), summary), index_(index), insert_(insert) {
    if (!insert_) initUndoDelete(view);
}

void MotionView::UndoInsertDelete::initUndoDelete(MotionView* view) {
    if (!view || index_ < 0) return;
    if (motion_) motion_->removeUseCounter();
    if (const auto value = view->motion(static_cast<std::size_t>(index_))) {
        motion_ = value->dup();
        if (motion_ && view->useCounter_)
            motion_->setUseCounter(view->useCounter_->sub(), view->ucOwner_);
    }
}

void MotionView::UndoInsertDelete::undoInsert() {
    auto* value = view();
    if (!value) return;
    udb(value);
    // 挿入した値は削除前に保存する。 削除後に取得
    // すると次の行か空を参照し、やり直しが壊れる。
    initUndoDelete(value);
    value->removeMotion(index_, false, false, false);
    insert_ = false;
    uda(value);
}

void MotionView::UndoInsertDelete::undoDelete() {
    auto* value = view();
    if (!value || !motion_) return;
    udb(value);
    value->appendMotion(motion_->dup(), index_, false, false, false);
    insert_ = true;
    uda(value);
}

void MotionView::UndoInsertDelete::undo() {
    if (insert_) undoInsert();
    else undoDelete();
}

void MotionView::UndoInsertDelete::redo() { undo(); }
void MotionView::UndoInsertDelete::dispose() {
    if (motion_) motion_->removeUseCounter();
    motion_.reset();
}

MotionView::MotionView(Commons comm, Props prop, Summary* summary,
                       CWXPath* ucOwner, UseCounterPtr useCounter,
                       const Parent& parent, const int style, UndoManager* undo)
    : comm_(std::move(comm)), prop_(std::move(prop)), summary_(summary),
      useCounter_(std::move(useCounter)), undo_(undo),
      readOnly_((style & ReadOnlyStyle) != 0) {
    motion::static_this();
    id_ = "motionview-" + std::to_string(
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)));
    if (ucOwner) ucOwner_ = lib::Owner<CWXPath>(ucOwner, [](CWXPath*) {});
    if (!undo_) {
        ownedUndo_ = std::make_unique<UndoManager>(prop_.etc.undoMaxEtc);
        undo_ = ownedUndo_.get();
    }
    subscribeDataVersion();
    if (parent) (void)createControl(parent);
}

MotionView::MotionView(Commons comm, Props prop, SummaryPtr summary,
                       lib::Owner<CWXPath> ucOwner, UseCounterPtr useCounter,
                       const Parent& parent, const int style, UndoManager* undo)
    : comm_(std::move(comm)), prop_(std::move(prop)), summary_(summary.get()),
      summaryHolder_(std::move(summary)), ucOwner_(std::move(ucOwner)),
      useCounter_(std::move(useCounter)), undo_(undo),
      readOnly_((style & ReadOnlyStyle) != 0) {
    motion::static_this();
    id_ = "motionview-" + std::to_string(
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)));
    if (!undo_) {
        ownedUndo_ = std::make_unique<UndoManager>(prop_.etc.undoMaxEtc);
        undo_ = ownedUndo_.get();
    }
    subscribeDataVersion();
    if (parent) (void)createControl(parent);
}

void MotionView::subscribeDataVersion() {
    const std::weak_ptr<void> lifetime = lifetimeToken_;
    refDataVersionToken_ = comm_.refDataVersion.connect([this, lifetime] {
        if (!lifetime.expired()) refEnabled();
    });
}

MotionView::~MotionView() {
    disposed_ = true;
    comm_.refDataVersion.remove(refDataVersionToken_);
    refDataVersionToken_ = 0;
    // D版の破棄処理と同様、一時的な Motion の親を解放する前に、 所有する非リンクの召喚
    // 獣のイベントウィンドウを閉じる。 リンク先 Summary のウィンドウは独立して扱う。
    lifetimeToken_.reset();
    const auto closeEvents = prop_.closeUseEvents;
    const auto openedEvents = std::move(openedBeastEvents_);
    const auto comments = std::move(commentDialogs_);
    if (closeEvents)
        for (const auto& [owner, beast] : openedEvents) closeEvents(beast);
    for (const auto& [id, session] : comments) {
        session.dialog->applied = {};
        session.dialog->forceCancel();
    }
    unbindDragDrop();
    if (categoryTabs_) {
        categoryTabs_->on_selection_changed = {};
        for (auto& page : categoryTabs_->pages) {
            if (const auto toolbar =
                    std::dynamic_pointer_cast<lib::ToolBar>(page.content))
                toolbar->on_command = {};
        }
    }
    if (motionsTable_) {
        motionsTable_->on_selection_changed = {};
        motionsTable_->on_cell_activate = {};
        motionsTable_->on_pointer = {};
    }
    if (motionElementTable_) {
        motionElementTable_->on_selection_changed = {};
        motionElementTable_->on_pointer = {};
    }
    if (beastsControl_) { beastsControl_->on_selection_changed = {}; beastsControl_->on_pointer = {}; }
    if (beastSearchControl_) beastSearchControl_->on_changed = {};
    if (setBeastControl_) setBeastControl_->on_click = {};
    if (beastImage_) {
        beastImage_->on_paint = {};
        beastImage_->on_paint_context = {};
        beastImage_->on_pointer = {};
        beastImage_->on_key_down = {};
    }
    beastPreviewResource_.reset();
    if (motionContextMenu_) {
        motionContextMenu_->on_open = {};
        motionContextMenu_->on_command = {};
    }
    if (beastContextMenu_) {
        beastContextMenu_->on_open = {};
        beastContextMenu_->on_command = {};
    }
    if (maxNestControl_) maxNestControl_->on_changed = {};
    if (roundControl_) roundControl_->on_changed = {};
    if (abilityRoundControl_) abilityRoundControl_->on_changed = {};
    if (valueControl_) valueControl_->on_changed = {};
    if (skillPowerControl_) skillPowerControl_->on_changed = {};
    if (abilityValueControl_) abilityValueControl_->on_changed = {};
    for (auto& [type, control] : damageTypeControls_) {
        (void)type;
        if (control) control->on_changed = {};
    }
    for (auto& [type, control] : skillPowerTypeControls_) {
        (void)type;
        if (control) control->on_changed = {};
    }
    for (const auto& value : motions_) detachMotion(value);
}

void MotionView::setReadOnly(const bool value) noexcept {
    const bool changed = readOnly_ != value;
    const std::weak_ptr<void> lifetime = lifetimeToken_;
    readOnly_ = value;
    // 読み取り専用は変更を防ぐためのもの。 D版でも確認は可能
    // なので、効果モーション・属性の選択や閲覧まで禁止しない。
    if (motionsTable_) motionsTable_->set_enabled(true);
    if (motionElementTable_) motionElementTable_->set_enabled(true);
    if (toolbar_) toolbar_->set_enabled(!value);
    if (newMotionTypeControl_) newMotionTypeControl_->set_enabled(!value);
    if (beastsControl_) beastsControl_->set_enabled(!value);
    if (beastSearchControl_) beastSearchControl_->read_only = value;
    refEnabled();
    if (lifetime.expired() || !changed || !panel_) return;
    // 組み込み先のダイアログが createControl() 後に読み取り専用状態を
    // 変える場合がある。 読み取り専用のドラッグ元は維持しつつ、転送可否を組み直す。
    try {
        unbindDragDrop();
        if (!lifetime.expired()) bindDragDrop();
    } catch (...) {
        // 転送機能を使えなくても、この noexcept のUI設定関数から例外を
        // 出さない。 リスナー側の読み取り専用検査でも、引き続き変更を拒否する。
    }
}

PanelPtr MotionView::createControl(const Parent& parent) {
    ensureWidgets(parent);
    refreshWidgets();
    bindDragDrop();
    return panel_;
}

void MotionView::ensureWidgets(const Parent& parent) {
    if (panel_) return;
    panel_ = std::make_shared<lib::Panel>();
    panel_->set_id(0x5000U);
    auto root_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    root_layout->spacing = 3.0F;
    root_layout->padding = {3.0F, 3.0F, 3.0F, 3.0F};
    panel_->set_layout(root_layout);
    panel_->set_layout_item({.horizontal = lib::Align::Stretch,
                             .vertical = lib::Align::Stretch,
                             .minimum = {460.0F, 300.0F}, .grow = 1.0F});
    if (parent) parent->add(panel_);

    const auto command_handler = [this](const StringView command) {
        if (command == "undo") undo();
        else if (command == "redo") redo();
        else if (command == "up") up();
        else if (command == "down") down();
        else if (command == "comment") writeComment();
        else if (command == "delete") removeMotion();
        else if (command.starts_with("new:")) {
            try {
                (void)createMT(static_cast<types::MType>(
                    std::stoi(String(command.substr(4U)))));
            } catch (...) {
                // プラットフォームのバックエンドから渡された不正なコマンドは無視する。
            }
        }
        else if (command == "copy") (void)copy();
        else if (command == "paste") (void)paste();
    };

    using Types = std::vector<types::MType>;
    const std::vector<std::pair<String, Types>> categories{
        {prop_.msgs.groupVitality,
         {types::MType::Heal, types::MType::Damage, types::MType::Absorb}},
        {prop_.msgs.groupPhysical,
         {types::MType::Paralyze, types::MType::DisParalyze,
          types::MType::Poison, types::MType::DisPoison}},
        {prop_.msgs.groupSkill,
         {types::MType::GetSkillPower, types::MType::LoseSkillPower}},
        {prop_.msgs.groupMental,
         {types::MType::Sleep, types::MType::Confuse, types::MType::Overheat,
          types::MType::Brave, types::MType::Panic, types::MType::Normal}},
        {prop_.msgs.groupMagic,
         {types::MType::Bind, types::MType::DisBind, types::MType::Silence,
          types::MType::DisSilence, types::MType::FaceUp,
          types::MType::FaceDown, types::MType::AntiMagic,
          types::MType::DisAntiMagic}},
        {prop_.msgs.groupEnhance,
         {types::MType::EnhanceAction, types::MType::EnhanceAvoid,
          types::MType::EnhanceDefense, types::MType::EnhanceResist}},
        {prop_.msgs.groupVanish,
         {types::MType::VanishTarget, types::MType::VanishCard,
          types::MType::VanishBeast}},
        {prop_.msgs.groupCard,
         {types::MType::DealAttackCard, types::MType::DealPowerfulAttackCard,
          types::MType::DealCriticalAttackCard, types::MType::DealFeintCard,
          types::MType::DealDefenseCard, types::MType::DealDistanceCard,
          types::MType::DealConfuseCard, types::MType::DealSkillCard,
          types::MType::CancelAction}},
        {prop_.msgs.groupBeast, {types::MType::SummonBeast}},
        {prop_.msgs.groupEtc, {types::MType::NoEffect}},
    };
    // DWTの CTabFolder と同様、選択ページ
    // 内に効果モーション作成のツールバーを置く。 分類
    // の選択を、無関係な全体ツールバーの前に並ぶボタン
    // ではなく、 見た目も動作もタブの選択として扱う。
    const auto category_tabs = std::make_shared<lib::TabView>();
    const bool all_mode = lib::editor_window_host_options().all_mode;
    // ALL の効果分類タブは、ラベル左右の余
    // 白をそれぞれ2px増やす（4から6）。
    category_tabs->header_horizontal_padding = all_mode ? 6.0F : 2.0F;
    // ALL の見出し行は、上下を各1px高くして操作領域を広げる。
    if (all_mode) category_tabs->header_vertical_extra = 1.0F;
    categoryTabs_ = category_tabs;
    // ALL のツール行は、1.44倍のアイコンと4pxの間隔で約10px高く
    // なる。 その分を下の一覧から差し引き、ダイアログ全体の高さは変えない。
    const float kContentMinimum = all_mode ? 180.0F : 190.0F;
    category_tabs->set_id(0x5001U);
    category_tabs->selected_index = 0U;
    // 高さはタブ見出しとツールバーの実測値の合計にする。 従来の58px
    // 固定では、特に ALL のフォントでアイコン下に空白ができる。
    // TabView は推奨高さがないと汎用の大きな文書表示領域を
    // 使うため、1pxを指定する。 共通計測が指定値と「見出し＋選択
    // 中のツールバー」の大きい方を採り、内容ぴったりの高さになる。
    category_tabs->set_layout_item({.horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Start,
                                    .preferred = {0.0F, 1.0F}});
    for (std::size_t index = 0; index < categories.size(); ++index) {
        const auto& [title, types] = categories[index];
        auto toolbar = std::make_shared<lib::ToolBar>();
        toolbar->tooltip_width_provider=[this, life=std::weak_ptr<void>(lifetimeToken_)] {
            return life.expired() || !prop_.editorProps ? 0.0F : lib::scaled_ui_metric(float(prop_.editorProps->var().toolTipWidth.value));
        };
        toolbar->set_id(0x5010U + static_cast<std::uint32_t>(index));
        toolbar->set_enabled(!readOnly_);
        toolbar->on_command = command_handler;
        // ALL の効果アイコン（効果削除など）は、
        // 通常の 1.44 倍でボタン中央に描く。
        if (all_mode) toolbar->icon_scale = 1.44F;
        lib::ToolItem remove{"", "delete", prop_.deleteIcon};
        remove.tooltip = prop_.msgs.deleteMotion;
        toolbar->items.push_back(std::move(remove));
        lib::ToolItem separator;
        separator.separator = true;
        toolbar->items.push_back(separator);
        lib::ToolItem up{"", "up", prop_.upIcon};
        up.tooltip = prop_.msgs.up;
        toolbar->items.push_back(std::move(up));
        lib::ToolItem down{"", "down", prop_.downIcon};
        down.tooltip = prop_.msgs.down;
        toolbar->items.push_back(std::move(down));
        toolbar->items.push_back(separator);
        for (const auto type : types) {
            const auto name = prop_.msgs.motionName(type);
            lib::ToolItem item{"",
                               "new:" + std::to_string(static_cast<int>(type)),
                               prop_.motionIcon ? prop_.motionIcon(type)
                                                : lib::ResourceId{}};
            item.enabled = !readOnly_ &&
                (type != types::MType::NoEffect || !summary_ || !summary_->legacy());
            item.tooltip = name;
            if (prop_.etc.showMotionDescription) {
                const auto description = prop_.msgs.motionDescription(type);
                if (!description.empty()) item.tooltip += "\n" + description;
            }
            toolbar->items.push_back(std::move(item));
        }
        // ALL のツールバーは操作部品の範囲だけを占める。
        // 分類ページ全体に伸ばすと、右側に幅広い空の灰色帯がで
        // きる。 分類タブとボタンの間には縦の間隔を設ける。
        toolbar->set_layout_item({.horizontal = all_mode ? lib::Align::Start
                                                         : lib::Align::Stretch,
                                  .vertical = lib::Align::Start,
                                  .margin = {all_mode ? 4.0F : 0.0F, 0.0F, 0.0F, 0.0F},
                                  .grow = all_mode ? 0.0F : 1.0F});
        category_tabs->pages.push_back({title, {}, toolbar, false});
    }
    toolbar_ = std::dynamic_pointer_cast<lib::ToolBar>(category_tabs->pages.front().content);
    category_tabs->on_selection_changed = [this, category_tabs](
                                              const std::optional<std::size_t> index) {
        if (!index || *index >= category_tabs->pages.size()) return;
        toolbar_ = std::dynamic_pointer_cast<lib::ToolBar>(
            category_tabs->pages[*index].content);
        if (toolbar_) toolbar_->set_enabled(!readOnly_);
    };
    panel_->add(category_tabs);

    auto content = std::make_shared<lib::SplitView>();
    content->orientation = lib::Orientation::Horizontal;
    // SplitView は二分割なので、DWTの
    // 見た目の三列を、 外側の左右分割と、狭い属性
    // 選択欄・パラメーター欄の内側の横分割で表す。
    content->weights = {1.0F, 1.0F};
    content->set_layout_item({.horizontal = lib::Align::Stretch,
                              .vertical = lib::Align::Stretch,
                              .minimum = {0.0F, kContentMinimum + 4.0F}, .grow = 1.0F});
    panel_->add(content);

    motionsTable_ = std::make_shared<lib::TableView>();
    motionsTable_->set_id(0x5002U);
    // D版の MotionView と同様、効果モーションの一覧は画像・名前
    // の一列にまとめる。 属性と値の編集は、独立したパラメーター欄で行う。
    motionsTable_->columns = {{prop_.msgs.motionKind, 220.0F,
                               lib::TextAlignment::Start, true, false}};
    motionsTable_->on_selection_changed = [this](const std::vector<std::size_t>& values) {
        if (!values.empty()) select(static_cast<std::ptrdiff_t>(values.front()));
        else select(-1);
    };
    motionsTable_->on_cell_activate = [this](const std::size_t row, const std::size_t) {
        select(static_cast<std::ptrdiff_t>(row));
    };
    motionsTable_->on_pointer = [this](lib::PointerEvent& event) {
        if (event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up) return;
        showMotionContextMenu(motionsTable_, event.position);
        event.handled = true;
    };
    motionsTable_->set_layout_item({.horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Stretch,
                                    .minimum = {210.0F, kContentMinimum}, .grow = 1.0F});
    content->add(motionsTable_);

    auto details = std::make_shared<lib::SplitView>();
    details->orientation = lib::Orientation::Horizontal;
    details->weights = {1.0F, 2.35F};
    details->set_layout_item({.horizontal = lib::Align::Stretch,
                              .vertical = lib::Align::Stretch,
                              .minimum = {236.0F, kContentMinimum}, .grow = 1.0F});
    content->add(details);

    motionElementTable_ = std::make_shared<lib::TableView>();
    motionElementTable_->set_id(0x5003U);
    motionElementTable_->columns = {{prop_.msgs.motionElement, 72.0F}};
    if (all_mode) {
        // ALL は属性行を高くして操作領域を広
        // げる。見出しは共通の高さを維持する。
        motionElementTable_->row_height = 24.0F;
        motionElementTable_->header_height = lib::kEditorUiMetrics.row_height;
    }
    motionElementTable_->on_selection_changed = [this](const std::vector<std::size_t>& values) {
        if (!values.empty()) SelElement(*this).widgetSelected(values.front());
    };
    motionElementTable_->on_pointer = [this](lib::PointerEvent& event) {
        if (event.action != lib::PointerAction::Move) return;
        const auto row = event.item_hit_test_available
            ? (event.item_index >= 0 ? std::optional<std::size_t>(event.item_index) : std::nullopt)
            : lib::hit_test_table_row(*motionElementTable_, event.position,
                                     lib::table_row_height(*motionElementTable_, 24.0F));
        static constexpr std::array elements{types::Element::All, types::Element::Health,
            types::Element::Mind, types::Element::Miracle, types::Element::Magic,
            types::Element::Fire, types::Element::Ice};
        motionElementTable_->set_tooltip(row && *row < elements.size() && prop_.elementTooltip
            ? prop_.elementTooltip(elements[*row]) : String{});
    };
    motionElementTable_->set_layout_item({.horizontal = lib::Align::Stretch,
                                          .vertical = lib::Align::Stretch,
                                          .minimum = {72.0F, kContentMinimum}, .grow = 1.0F});
    details->add(motionElementTable_);

    editPanel_ = std::make_shared<lib::Panel>();
    auto edit_layout = std::make_shared<lib::GridLayout>();
    edit_layout->columns = 2U;
    edit_layout->spacing = 3.0F;
    editPanel_->set_layout(edit_layout);
    editPanel_->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Stretch,
                                 .minimum = {120.0F, kContentMinimum}, .grow = 1.0F});
    details->add(editPanel_);

    beastsControl_ = std::make_shared<lib::ComboBox>();
    beastsControl_->set_id(0x5004U);
    const std::weak_ptr<void> candidate_lifetime = lifetimeToken_;
    beastsControl_->on_selection_changed = [this, candidate_lifetime](const std::optional<std::size_t> value) {
        if (!candidate_lifetime.expired()) (void)setSelectedBeast(value);
    };
    // D版と同様、候補コンボは選択した元カードをカード一覧で開く。 プレビュー
    // のプロパティ操作は、引き続き結び付いた効果モーションの召喚獣を編集する。
    beastsControl_->on_pointer = [this, candidate_lifetime](lib::PointerEvent& event) {
        if (candidate_lifetime.expired() || event.action != lib::PointerAction::Up ||
            event.button != lib::PointerButton::Secondary || !prop_.services.gui) return;
        const auto eligible = [this, candidate_lifetime] {
            return !candidate_lifetime.expired() && !readOnly_ && selectedBeast_ &&
                beastsControl_ && beastsControl_->selected_index &&
                *beastsControl_->selected_index > 0U;
        };
        auto menu = std::make_shared<lib::Menu>(true);
        lib::MenuItem item;
        item.text = prop_.editorProps ? prop_.editorProps->buildMenu(types::MenuID::OpenAtCardView)
                                      : String{"カード一覧で開く"};
        item.command = "open-at-card-view";
        item.enabled = eligible();
        (void)lib::replace_menu_items(menu, {std::move(item)});
        menu->on_command = [this, eligible](const StringView command) {
            if (command != "open-at-card-view" || !eligible()) return;
            const auto value = selectedBeast_;
            const auto open = prop_.openCWXPath;
            if (open) (void)open(path::cpaddattr(value->cwxPath(true), "shallow"), false);
        };
        const auto gui = prop_.services.gui;
        const auto owner = beastsControl_;
        event.handled = true;
        gui->show_context_menu(menu, owner, event.position);
    };
    editPanel_->add(beastsControl_);

    beastSearchControl_ = std::make_shared<lib::TextEdit>();
    beastSearchControl_->set_id(0x5005U);
    beastSearchControl_->on_changed = [this](const StringView value) { beastIncSearch(value); };
    editPanel_->add(beastSearchControl_);

    setBeastControl_ = std::make_shared<lib::Button>(prop_.msgs.setBeast);
    if (!all_mode && prop_.editorProps) {
        // ALL では既存の文字ボタンを維持し、ボタン寸法とダイアログ配置を変えない。
        setBeastControl_->text.clear();
        setBeastControl_->icon = prop_.editorProps->images().setBeast();
        setBeastControl_->set_tooltip(prop_.msgs.setBeast);
    }
    const std::weak_ptr<void> beast_callback_lifetime = lifetimeToken_;
    setBeastControl_->on_click = [this, beast_callback_lifetime] {
        if (!beast_callback_lifetime.expired()) SetBeast(*this).widgetSelected();
    };
    editPanel_->add(setBeastControl_);

    beastImage_ = std::make_shared<lib::Canvas>();
    beastImage_->set_id(0x5006U);
    beastImage_->on_paint = [this](const lib::Rect) { updateBeastToolTip(); };
    beastImage_->on_paint_context = [this](lib::DrawingContext& context,
                                           const lib::Rect bounds) {
        PaintBeast(*this).paintControl(context, bounds);
    };
    beastImage_->on_key_down = [this, beast_callback_lifetime](lib::KeyEvent& event) {
        if (!beast_callback_lifetime.expired()) EditBeast(*this).keyPressed(event);
    };
    beastImage_->on_pointer = [this](lib::PointerEvent& event) {
        if (event.button == lib::PointerButton::Secondary &&
            event.action == lib::PointerAction::Up) {
            showBeastContextMenu(beastImage_, event.position);
            event.handled = true;
            return;
        }
        EditBeast editor(*this);
        if (event.action == lib::PointerAction::DoubleClick) editor.mouseDoubleClick(event);
        else if (event.action == lib::PointerAction::Down) editor.mouseDown(event);
        else if (event.action == lib::PointerAction::Up) editor.mouseUp(event);
        else if (event.action == lib::PointerAction::Move) editor.mouseMove(event);
    };
    editPanel_->add(beastImage_);

    motionContextMenu_ = std::make_shared<lib::Menu>(true);
    motionContextMenu_->on_open = [this] { rebuildMotionContextMenu(); };
    motionContextMenu_->on_command = [this](const StringView command) {
        MotionTCPD tcpd(*this);
        SelectionEvent event;
        if (command == "cut") tcpd.cut(event);
        else if (command == "copy") tcpd.copy(event);
        else if (command == "paste") tcpd.paste(event);
        else if (command == "delete") tcpd.del(event);
        else if (command == "clone") tcpd.clone(event);
        else if (command == "undo") undo();
        else if (command == "redo") redo();
        else if (command == "up") up();
        else if (command == "down") down();
        else if (command == "comment") writeComment();
    };
    beastContextMenu_ = std::make_shared<lib::Menu>(true);
    beastContextMenu_->on_open = [this, beast_callback_lifetime] {
        if (!beast_callback_lifetime.expired()) rebuildBeastContextMenu();
    };
    beastContextMenu_->on_command = [this, beast_callback_lifetime](const StringView command) {
        if (beast_callback_lifetime.expired()) return;
        BeastTCPD tcpd(*this);
        SelectionEvent event;
        if (command == "cut") tcpd.cut(event);
        else if (command == "copy") tcpd.copy(event);
        else if (command == "paste") tcpd.paste(event);
        else if (command == "delete") tcpd.del(event);
        else if (command == "undo") undo();
        else if (command == "redo") redo();
        else if (command == "properties") editBeastM();
        else if (command == "events") editBeastUseEvent();
        else if (command == "unlink") (void)removeRef();
        else if (command == "material") selectConnectedResource();
    };

    maxNestControl_ = std::make_shared<lib::NumberEdit>();
    maxNestControl_->set_id(0x5007U);
    maxNestControl_->minimum = 1.0;
    maxNestControl_->maximum = static_cast<double>(std::max(1U, prop_.etc.beastMaxNest));
    maxNestControl_->on_changed = [this, lifetime = std::weak_ptr<void>{lifetimeToken_}](const double value) {
        if (lifetime.expired()) return;
        if (!readOnly_) if (const auto selected = selection()) {
            beginPendingUndo();
            selected->maxNest(static_cast<std::uint32_t>(std::clamp(value, 1.0, maxNestControl_->maximum)));
            if (lifetime.expired()) return;
            commitPendingUndo();
            if (!lifetime.expired()) notify();
        }
    };
    editPanel_->add(maxNestControl_);

    roundControl_ = std::make_shared<lib::NumberEdit>();
    roundControl_->set_id(0x5008U);
    roundControl_->minimum = Motion::round_min;
    roundControl_->maximum = static_cast<double>(prop_.etc.roundMax);
    roundControl_->on_changed = [this, life = std::weak_ptr<void>{lifetimeToken_}](const double value) {
        if (!life.expired()) roundEnter(static_cast<std::int32_t>(value));
    };
    // D版のSpinnerEditは変更をプレビューし、Escで入力開始時の値へ戻す。
    roundControl_->on_preview = roundControl_->on_changed;
    roundControl_->on_edit_cancelled = [this, life = std::weak_ptr<void>{lifetimeToken_}] {
        if (!life.expired()) (void)roundCancel(static_cast<std::int32_t>(roundControl_->value));
    };
    editPanel_->add(roundControl_);

    abilityRoundControl_ = std::make_shared<lib::NumberEdit>();
    abilityRoundControl_->set_id(0x5009U);
    abilityRoundControl_->minimum = Motion::round_min;
    abilityRoundControl_->maximum = static_cast<double>(prop_.etc.roundMax);
    abilityRoundControl_->on_changed = [this, life = std::weak_ptr<void>{lifetimeToken_}](const double value) {
        if (!life.expired()) roundEnter(static_cast<std::int32_t>(value));
    };
    // D版のSpinnerEditは変更をプレビューし、Escで入力開始時の値へ戻す。
    abilityRoundControl_->on_preview = abilityRoundControl_->on_changed;
    abilityRoundControl_->on_edit_cancelled = [this, life = std::weak_ptr<void>{lifetimeToken_}] {
        if (!life.expired()) (void)roundCancel(static_cast<std::int32_t>(abilityRoundControl_->value));
    };
    editPanel_->add(abilityRoundControl_);

    valueControl_ = std::make_shared<lib::NumberEdit>();
    valueControl_->set_id(0x500AU);
    valueControl_->minimum = Motion::uValue_min;
    valueControl_->maximum = static_cast<double>(prop_.etc.uValueMax);
    valueControl_->on_changed = [this, life = std::weak_ptr<void>{lifetimeToken_}](const double value) {
        if (!life.expired()) valEnter(static_cast<std::int32_t>(value));
    };
    // D版のSpinnerEditは変更をプレビューし、Escで入力開始時の値へ戻す。
    valueControl_->on_preview = valueControl_->on_changed;
    valueControl_->on_edit_cancelled = [this, life = std::weak_ptr<void>{lifetimeToken_}] {
        if (!life.expired()) (void)valCancel(static_cast<std::int32_t>(valueControl_->value));
    };
    editPanel_->add(valueControl_);

    skillPowerControl_ = std::make_shared<lib::NumberEdit>();
    skillPowerControl_->set_id(0x500BU);
    skillPowerControl_->minimum = Motion::uValue_min;
    skillPowerControl_->maximum = static_cast<double>(prop_.etc.skillPowerMax);
    skillPowerControl_->on_changed = [this, life = std::weak_ptr<void>{lifetimeToken_}](const double value) {
        if (!life.expired()) valEnter(static_cast<std::int32_t>(value));
    };
    // D版のSpinnerEditは変更をプレビューし、Escで入力開始時の値へ戻す。
    skillPowerControl_->on_preview = skillPowerControl_->on_changed;
    skillPowerControl_->on_edit_cancelled = [this, life = std::weak_ptr<void>{lifetimeToken_}] {
        if (!life.expired()) (void)valCancel(static_cast<std::int32_t>(skillPowerControl_->value));
    };
    editPanel_->add(skillPowerControl_);

    abilityValueControl_ = std::make_shared<lib::Slider>();
    abilityValueControl_->set_id(0x500CU);
    abilityValueControl_->minimum = Motion::aValue_min;
    abilityValueControl_->maximum = Motion::aValue_max;
    abilityValueControl_->on_changed = [this](const double value) {
        AbiValListener(*this).widgetSelected(static_cast<std::int32_t>(value));
    };
    editPanel_->add(abilityValueControl_);

    for (const auto type : {types::DamageType::LevelRatio, types::DamageType::Normal,
                            types::DamageType::Max}) {
        auto radio = std::make_shared<lib::RadioButton>(prop_.msgs.damageTypeName(type));
        radio->group = "motion-value";
        radio->on_changed = [this, type](const bool checked) {
            if (checked) DamageTypeListener(*this).widgetSelected(type);
        };
        damageTypeControls_[type] = radio;
        editPanel_->add(radio);
    }
    for (const auto type : {types::DamageType::Fixed, types::DamageType::Max}) {
        auto radio = std::make_shared<lib::RadioButton>(prop_.msgs.damageTypeName(type));
        radio->group = "skill-power";
        radio->on_changed = [this, type](const bool checked) {
            if (checked) DamageTypeListener(*this).widgetSelected(type);
        };
        skillPowerTypeControls_[type] = radio;
        editPanel_->add(radio);
    }

    // D版のパラメーターのグループを再現する。 見出しや範囲の案内
    // も通常の文字計測に含め、ラベルのない入力欄だけを置かない。
    editPanel_->clear();
    auto parameter_layout = std::make_shared<lib::GridLayout>();
    parameter_layout->columns = 1U;
    parameter_layout->spacing = 5.0F;
    editPanel_->set_layout(parameter_layout);
    const auto make_panel = [&](const ParameterPane pane) {
        auto panel = std::make_shared<lib::Panel>();
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 1U;
        layout->spacing = 5.0F;
        panel->set_layout(layout);
        panel->set_visible(false);
        editPanel_->add(panel);
        parameterPanels_[pane] = panel;
        return panel;
    };
    const auto group = [](const std::shared_ptr<lib::Panel>& panel, String title) {
        auto result = std::make_shared<lib::GroupBox>();
        result->title = std::move(title);
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 1U;
        layout->spacing = 5.0F;
        layout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
        result->set_layout(layout);
        panel->add(result);
        return result;
    };
    const auto* messages = prop_.editorProps ? &prop_.editorProps->requiredMsgs() : nullptr;
    const String round_title = messages ? messages->motionRound.value : "持続時間";
    const String value_title = messages ? messages->motionValue.value : "値";
    const String type_title = messages ? messages->motionDamageType.value : "処理方式";
    const auto number_group = [&](const std::shared_ptr<lib::Panel>& panel,
                                  const String& title,
                                  const std::shared_ptr<lib::NumberEdit>& number) {
        auto container = group(panel, title);
        container->add(number);
        auto hint = std::make_shared<lib::Label>();
        hint->text = messages ? ::cwxeditor::cwx::utils::tryFormat(messages->rangeHint.value,
                static_cast<int>(number->minimum), static_cast<int>(number->maximum))
            : "(" + std::to_string(static_cast<int>(number->minimum)) + "～" +
                std::to_string(static_cast<int>(number->maximum)) + ")";
        container->add(hint);
    };
    auto summon = make_panel(ParameterPane::Beast);
    summon->add(beastsControl_);
    summon->add(beastSearchControl_);
    summon->add(setBeastControl_);
    summon->add(beastImage_);
    auto link = group(summon, messages ? messages->linkOption.value : "リンクオプション");
    auto nest_label = std::make_shared<lib::Label>();
    nest_label->text = messages ? messages->beastMaxNest.value : "ネスト可能回数";
    link->add(nest_label);
    link->add(maxNestControl_);
    auto round = make_panel(ParameterPane::Round);
    number_group(round, round_title, roundControl_);
    auto ability = make_panel(ParameterPane::Ability);
    auto enhancement = group(ability, messages ? messages->motionEnhValue.value : "能力増減値");
    enhancement->add(abilityValueControl_);
    auto endpoints = std::make_shared<lib::Panel>();
    auto endpoint_layout = std::make_shared<lib::GridLayout>();
    endpoint_layout->columns = 2U;
    endpoints->set_layout(endpoint_layout);
    auto minimum = std::make_shared<lib::Label>();
    minimum->text = std::to_string(Motion::aValue_min);
    auto maximum = std::make_shared<lib::Label>();
    maximum->text = "+" + std::to_string(Motion::aValue_max);
    maximum->set_layout_item({.column=1U, .horizontal=lib::Align::End});
    endpoints->add(minimum);
    endpoints->add(maximum);
    enhancement->add(endpoints);
    number_group(ability, round_title, abilityRoundControl_);
    auto numeric = make_panel(ParameterPane::Value);
    auto damage_type = group(numeric, type_title);
    for (const auto type : {types::DamageType::LevelRatio, types::DamageType::Normal, types::DamageType::Max})
        damage_type->add(damageTypeControls_.at(type));
    number_group(numeric, value_title, valueControl_);
    auto skill = make_panel(ParameterPane::SkillPower);
    auto skill_type = group(skill, type_title);
    for (const auto type : {types::DamageType::Fixed, types::DamageType::Max})
        skill_type->add(skillPowerTypeControls_.at(type));
    number_group(skill, value_title, skillPowerControl_);
    refBeasts();

    // D版は表示全体に KeyDownFilter を登録し、MotionView
    // 内のどの子にフォーカスがあっても取り消し・やり直しを可能にする。
    // 共通の通知処理はフォーカスのあるウィジェットを直接対象にするた
    // め、 既存の全子に処理を追加し、各自の移動・編集処理も維持する。
    const std::weak_ptr<void> lifetime = lifetimeToken_;
    const std::function<void(const lib::WidgetPtr&)> bind_key_filter =
        [this, lifetime, &bind_key_filter](const lib::WidgetPtr& widget) {
            if (!widget) return;
            const auto previous = widget->on_key_down;
            const std::weak_ptr<lib::Widget> target = widget;
            widget->on_key_down = [this, lifetime, previous, target](lib::KeyEvent& event) {
                if (previous) previous(event);
                if (event.handled || lifetime.expired()) return;
                KeyDownFilter(*this).handleEvent(event);
                if (event.handled) return;
                // Ctrl+X/C/V はポップアップメニューと同様、召喚獣画像
                // には召喚獣のTCPD、効果一覧には効果のTCPDを使う。 テキ
                // スト欄は一覧ではないため、自身のクリップボード処理を維持する。
                const auto widget_now = target.lock();
                if (!widget_now || std::dynamic_pointer_cast<lib::TextEdit>(widget_now) ||
                    std::dynamic_pointer_cast<lib::ComboBox>(widget_now) ||
                    std::dynamic_pointer_cast<lib::NumberEdit>(widget_now)) return;
                if (widget_now == beastImage_) {
                    BeastTCPD tcpd(*this);
                    (void)dmenu::handleClipboardShortcut(event, {
                        [&tcpd] { return tcpd.canDoT(); }, [&tcpd] { return tcpd.canDoC(); },
                        [&tcpd] { return tcpd.canDoP(); },
                        [&tcpd] { SelectionEvent e; tcpd.cut(e); },
                        [&tcpd] { SelectionEvent e; tcpd.copy(e); },
                        [&tcpd] { SelectionEvent e; tcpd.paste(e); }});
                } else {
                    MotionTCPD tcpd(*this);
                    (void)dmenu::handleClipboardShortcut(event, {
                        [&tcpd] { return tcpd.canDoT(); }, [&tcpd] { return tcpd.canDoC(); },
                        [&tcpd] { return tcpd.canDoP(); },
                        [&tcpd] { SelectionEvent e; tcpd.cut(e); },
                        [&tcpd] { SelectionEvent e; tcpd.copy(e); },
                        [&tcpd] { SelectionEvent e; tcpd.paste(e); }});
                }
            };
            if (const auto container = std::dynamic_pointer_cast<lib::Container>(widget))
                for (const auto& child : container->children()) bind_key_filter(child);
        };
    bind_key_filter(panel_);
}

void MotionView::refreshWidgets() {
    const std::weak_ptr<void> lifetime = lifetimeToken_;
    retireDetachedBeastEvents();
    if (lifetime.expired()) return;
    std::vector<std::shared_ptr<comment::CommentDialog>> closingComments;
    for (auto& [id, session] : commentDialogs_) {
        const auto target = std::find_if(motions_.begin(), motions_.end(),
            [&](const MotionPtr& value) {
                return value && value->objectId() == id;
            });
        if (target == motions_.end()) {
            closingComments.push_back(session.dialog);
        } else {
            // D版と同様、モードレスのコメントは Motion の objectId をキーとする。
            // 履歴が Motion オブジェクトを置き換えても、開いているダイアログを無効化しない。
            session.target = *target;
        }
    }
    for (const auto& dialog : closingComments) {
        dialog->forceCancel();
        if (lifetime.expired()) return;
    }
    refreshRows();
    refreshParameterPane();
    refreshBeastImage();
    if (status_) {
        if (const auto value = selection()) {
            status_->text = motionText(*value);
            if (!warnings(static_cast<std::size_t>(selected_)).empty())
                status_->text += " [warning]";
        } else {
            status_->text = prop_.msgs.noSelection;
        }
    }
    refEnabled();
}

void MotionView::refreshRows() {
    std::vector<MotionRow> rows;
    std::vector<lib::TableRow> motion_rows;
    rows.reserve(motions_.size());
    motion_rows.reserve(motions_.size());
    for (const auto& value : motions_) {
        if (!value) continue;
        MotionRow row;
        row.value = value;
        row.name = motionRowName(prop_.msgs, value->type());
        row.element = prop_.msgs.elementName(value->element());
        row.parameters = parameterText(*value);
        row.warnings = warnings(rows.size());
        rows.push_back(row);
        lib::TableRow table_row;
        lib::ListItem motion_cell{row.name, value->objectId()};
        if (prop_.motionIcon) motion_cell.icon = prop_.motionIcon(value->type());
        if (prop_.elementIcon) motion_cell.trailing_icon = prop_.elementIcon(value->element());
        table_row.cells = {std::move(motion_cell)};
        motion_rows.push_back(std::move(table_row));
    }
    auto motion_selection = selected_ >= 0 &&
                            selected_ < static_cast<std::ptrdiff_t>(motions_.size())
        ? std::vector<std::size_t>{static_cast<std::size_t>(selected_)}
        : std::vector<std::size_t>{};

    const std::array element_values{
        types::Element::All, types::Element::Health, types::Element::Mind,
        types::Element::Miracle, types::Element::Magic, types::Element::Fire,
        types::Element::Ice};
    std::vector<lib::TableRow> element_rows;
    element_rows.reserve(element_values.size());
    for (const auto value : element_values) {
        lib::ListItem element_cell{prop_.elementText ? prop_.elementText(value)
            : prop_.msgs.elementName(value), elementName(value)};
        if (prop_.elementIcon) element_cell.icon = prop_.elementIcon(value);
        element_rows.push_back({{std::move(element_cell)}});
    }
    std::vector<std::size_t> element_selection;
    if (selected_ >= 0 && selectedElement_ == types::Element::All) {
        element_selection = {0U};
    } else if (const auto found = std::find(element_values.begin(),
                                            element_values.end(), selectedElement_);
               found != element_values.end()) {
        element_selection = {static_cast<std::size_t>(
            std::distance(element_values.begin(), found))};
    }

    auto motion_update = motionsTable_
        ? lib::prepare_table_rows(motionsTable_, std::move(motion_rows),
                                  std::move(motion_selection))
        : lib::PreparedTableUpdate{};
    auto element_update = motionElementTable_
        ? lib::prepare_table_rows(motionElementTable_, std::move(element_rows),
                                  std::move(element_selection))
        : lib::PreparedTableUpdate{};
    if ((motionsTable_ && !motion_update.current()) ||
        (motionElementTable_ && !element_update.current())) return;
    if (motionsTable_ && !motion_update.commit()) return;
    if (motionElementTable_ && !element_update.commit()) return;
    rows_.swap(rows);
}

void MotionView::refreshParameterPane() {
    for (const auto& [pane, panel] : parameterPanels_) panel->set_visible(false);
    const auto value = selection();
    pane_ = ParameterPane::None;
    selectedDamageType_ = types::DamageType::LevelRatio;
    selectedElement_ = types::Element::All;
    abilityValue_ = 0;
    value_ = Motion::uValue_min;
    roundValue_ = Motion::round_min;
    maxNestValue_ = Motion::maxNest_init;
    const auto show = [](const lib::WidgetPtr& control, const bool visible) {
        if (control) control->set_visible(visible);
    };
    // DWT と同様、効果モーションの行が未選択でも属性選
    // 択欄を表示する。 隠すと「属性」列全体が消え、初期の
    // 効果ダイアログが大きな空の一覧だけに見えてしまう。
    show(motionElementTable_, true);
    show(beastsControl_, false);
    show(beastSearchControl_, false);
    show(setBeastControl_, false);
    show(beastImage_, false);
    show(maxNestControl_, false);
    show(roundControl_, false);
    show(abilityRoundControl_, false);
    show(valueControl_, false);
    show(skillPowerControl_, false);
    show(abilityValueControl_, false);
    for (const auto& [type, control] : damageTypeControls_) {
        (void)type;
        show(control, false);
    }
    for (const auto& [type, control] : skillPowerTypeControls_) {
        (void)type;
        show(control, false);
    }
    // 未選択でもDWTの三列目を維持し、編集部品
    // だけを隠す。 パネルを畳むと属性一覧が広が
    // り、ダイアログ全体の配置も変わってしまう。
    if (editPanel_) editPanel_->set_visible(true);
    if (!value) return;

    selectedElement_ = value->element();
    selectedDamageType_ = value->damageType();
    value_ = static_cast<std::int32_t>(value->uValue());
    roundValue_ = value->round();
    abilityValue_ = value->aValue();
    maxNestValue_ = static_cast<std::int32_t>(std::clamp(value->maxNest(), 1U,
                                                     std::max(1U, prop_.etc.beastMaxNest)));
    const auto detail = value->detail();
    if (value->type() == types::MType::GetSkillPower ||
        value->type() == types::MType::LoseSkillPower) {
        pane_ = ParameterPane::SkillPower;
    } else if (detail.use(types::MArg::Beast)) {
        pane_ = ParameterPane::Beast;
    } else if (detail.use(types::MArg::Round) && detail.use(types::MArg::AValue)) {
        pane_ = ParameterPane::Ability;
    } else if (detail.use(types::MArg::Round)) {
        pane_ = ParameterPane::Round;
    } else if (detail.use(types::MArg::UValue)) {
        pane_ = ParameterPane::Value;
    }
    const bool beast = pane_ == ParameterPane::Beast;
    const bool ability = pane_ == ParameterPane::Ability;
    const bool round = pane_ == ParameterPane::Round;
    const bool numeric = pane_ == ParameterPane::Value;
    const bool skill_power = pane_ == ParameterPane::SkillPower;
    if (const auto panel = parameterPanels_.find(pane_); panel != parameterPanels_.end())
        panel->second->set_visible(true);
    show(beastsControl_, beast);
    show(beastSearchControl_, beast);
    show(setBeastControl_, beast);
    show(beastImage_, beast);
    show(maxNestControl_, beast);
    show(roundControl_, round);
    show(abilityRoundControl_, ability);
    show(abilityValueControl_, ability);
    show(valueControl_, numeric);
    show(skillPowerControl_, skill_power);
    for (const auto& [type, control] : damageTypeControls_) {
        (void)type;
        show(control, numeric);
    }
    for (const auto& [type, control] : skillPowerTypeControls_) {
        (void)type;
        show(control, skill_power);
    }
    if (maxNestControl_) maxNestControl_->value = maxNestValue_;
    if (roundControl_) roundControl_->value = roundValue_;
    if (abilityRoundControl_) abilityRoundControl_->value = roundValue_;
    if (valueControl_) valueControl_->value = value_;
    if (skillPowerControl_) skillPowerControl_->value = value_;
    if (abilityValueControl_) abilityValueControl_->value = abilityValue_;
    for (auto& [type, control] : damageTypeControls_)
        if (control) control->checked = type == selectedDamageType_;
    for (auto& [type, control] : skillPowerTypeControls_)
        if (control) control->checked = type == selectedDamageType_;
}

void MotionView::refreshBeastImage() {
    if (!beastImage_) return;
    lib::ImageData next_image;
    lib::Size next_size{};
    if (const auto beast = editBeast(); beast && prop_.editorProps && prop_.skin &&
        prop_.services.graphics) {
        auto rendered = dutils::beastCardImage(prop_.editorProps, prop_.skin,
                                               summary_, *beast, prop_.services);
        const auto percent = prop_.etc.imageScale <= 16
            ? static_cast<std::uint32_t>(std::max(1, prop_.etc.imageScale) * 100)
            : static_cast<std::uint32_t>(prop_.etc.imageScale);
        auto image = rendered.scaledPercent(percent);
        if (!image.frames.empty()) {
            const auto& frame = image.frames.front();
            next_size = {static_cast<float>(frame.pixel_width),
                         static_cast<float>(frame.pixel_height)};
            next_image = std::move(image);
        }
    }
    const auto resource = beastPreviewResource_.publish(
        prop_.services.graphics, next_image);
    if (next_image.frames.empty()) beastPreviewSize_ = {};
    else if (resource != 0U &&
             beastPreviewResource_.graphics() == prop_.services.graphics)
        beastPreviewSize_ = next_size;
    beastImage_->set_tooltip(beastToolTip_);
    beastImage_->set_enabled(!readOnly_ || static_cast<bool>(selection()));
    if (beastImage_->invalidate) beastImage_->invalidate();
    else if (prop_.services.gui) prop_.services.gui->request_redraw(beastImage_);
}

std::optional<lib::Rect> MotionView::beastDrawBounds() const {
    if (!beastImage_ || beastPreviewResource_.id() == 0U ||
        beastPreviewSize_.width <= 0.0F || beastPreviewSize_.height <= 0.0F)
        return std::nullopt;
    const auto bounds = beastImage_->bounds();
    if (bounds.width <= 0.0F || bounds.height <= 0.0F) return std::nullopt;
    const float ratio = std::min(bounds.width / beastPreviewSize_.width,
                                 bounds.height / beastPreviewSize_.height);
    const lib::Size size{beastPreviewSize_.width * ratio,
                         beastPreviewSize_.height * ratio};
    return lib::Rect{bounds.x + (bounds.width - size.width) / 2.0F,
                     bounds.y + (bounds.height - size.height) / 2.0F,
                     size.width, size.height};
}

void MotionView::rebuildMotionContextMenu() {
    if (!motionContextMenu_) return;
    MotionTCPD tcpd(*this);
    auto item = [](String text, String command, const bool enabled) {
        lib::MenuItem value;
        value.text = std::move(text);
        value.command = std::move(command);
        value.enabled = enabled;
        return value;
    };
    lib::MenuItem separator;
    separator.separator = true;
    std::vector<lib::MenuItem> items{
        item(prop_.msgs.undo, "undo", !readOnly_ && undo_ && undo_->canUndo()),
        item(prop_.msgs.redo, "redo", !readOnly_ && undo_ && undo_->canRedo()),
        separator,
        item(prop_.msgs.up, "up", !readOnly_ && selected_ > 0),
        item(prop_.msgs.down, "down", !readOnly_ && selected_ >= 0 &&
             static_cast<std::size_t>(selected_ + 1) < motions_.size()),
        separator,
        item(prop_.msgs.cut, "cut", !readOnly_ && tcpd.canDoT()),
        item(prop_.msgs.copy, "copy", tcpd.canDoC()),
        item(prop_.msgs.paste, "paste", !readOnly_ && tcpd.canDoP()),
        item(prop_.msgs.deleteMotion, "delete", !readOnly_ && tcpd.canDoD()),
        item(prop_.msgs.clone, "clone", !readOnly_ && tcpd.canDoClone()),
        separator, item(prop_.msgs.comment, "comment", canWriteComment())};
    (void)lib::replace_menu_items(motionContextMenu_, std::move(items));
}

void MotionView::rebuildBeastContextMenu() {
    if (!beastContextMenu_) return;
    BeastTCPD tcpd(*this);
    auto item = [](String text, String command, const bool enabled) {
        lib::MenuItem value;
        value.text = std::move(text);
        value.command = std::move(command);
        value.enabled = enabled;
        return value;
    };
    lib::MenuItem separator;
    separator.separator = true;
    const auto menu_name = [this](const types::MenuID id, const StringView fallback) {
        return prop_.editorProps ? prop_.editorProps->buildMenu(id) : String{fallback};
    };
    const auto selected = selection();
    std::vector<lib::MenuItem> items{
        item(menu_name(readOnly_ ? types::MenuID::ShowProp : types::MenuID::EditProp, "プロパティ"),
             "properties", canEditBeast()), separator,
        item(menu_name(types::MenuID::EditEventAtTimeOfUsing, "使用時イベント"), "events", canEditBeast()),
        separator,
        item(menu_name(types::MenuID::RemoveRef, "参照解除"), "unlink", !readOnly_ && selected &&
             selected->beast() && selected->beast()->linkId() && summary_ &&
             static_cast<bool>(summary_->beast(selected->beast()->linkId()))), separator,
        item(prop_.msgs.undo, "undo", !readOnly_ && undo_ && undo_->canUndo()),
        item(prop_.msgs.redo, "redo", !readOnly_ && undo_ && undo_->canRedo()),
        separator,
        item(prop_.msgs.cut, "cut", !readOnly_ && tcpd.canDoT()),
        item(prop_.msgs.copy, "copy", tcpd.canDoC()),
        item(prop_.msgs.paste, "paste", !readOnly_ && tcpd.canDoP()),
        item(prop_.msgs.deleteMotion, "delete", !readOnly_ && tcpd.canDoD()), separator,
        item(menu_name(types::MenuID::SelectConnectedResource, "関連素材を選択"), "material", canSelectConnectedResource())};
    (void)lib::replace_menu_items(beastContextMenu_, std::move(items));
}

void MotionView::showMotionContextMenu(const lib::WidgetPtr& owner,
                                       const lib::Point position) {
    if (!motionContextMenu_ || !prop_.services.gui) return;
    if (motionContextMenu_->on_open) motionContextMenu_->on_open();
    prop_.services.gui->show_context_menu(motionContextMenu_, owner, position);
}

void MotionView::showBeastContextMenu(const lib::WidgetPtr& owner,
                                      const lib::Point position) {
    if (!beastContextMenu_ || !prop_.services.gui) return;
    if (beastContextMenu_->on_open) beastContextMenu_->on_open();
    prop_.services.gui->show_context_menu(beastContextMenu_, owner, position);
}

void MotionView::notify(const bool warning, const bool useCount) {
    const std::weak_ptr<void> lifetime = lifetimeToken_;
    const auto cancel = std::exchange(pendingReorderCommentCancel_, {});
    if (cancel) cancel->forceCancel();
    if (lifetime.expired()) return;
    refreshWidgets();
    if (lifetime.expired()) return;
    const auto value = selection();
    const auto modelChangedCopy = modelChanged;
    const auto motionChangedCopy = motionChanged;
    const auto warningEventCopy = warningEvent;
    const auto modEventCopy = modEvent;
    const auto useCountCopy = comm_.refUseCount;
    const auto toolBarCopy = comm_.refreshToolBar;
    modelChangedCopy.call();
    if (lifetime.expired()) return;
    if (value) motionChangedCopy.call(value);
    if (lifetime.expired()) return;
    if (warning) warningEventCopy.call();
    if (lifetime.expired()) return;
    modEventCopy.call();
    if (lifetime.expired()) return;
    if (useCount) useCountCopy.call();
    if (lifetime.expired()) return;
    if (toolBarCopy) toolBarCopy();
}

String MotionView::motionText(const Motion& value) const {
    auto result = prop_.msgs.motionName(value.type());
    if (prop_.etc.showMotionDescription) {
        result += prop_.msgs.descriptionSeparator;
        result += prop_.msgs.motionDescription(value.type());
    }
    return result;
}

String MotionView::parameterText(const Motion& value) const {
    const auto detail = value.detail();
    std::ostringstream stream;
    if (detail.use(types::MArg::UValue)) stream << "value=" << value.uValue() << ' ';
    if (detail.use(types::MArg::AValue)) stream << "ability=" << value.aValue() << ' ';
    if (detail.use(types::MArg::Round)) stream << "round=" << value.round() << ' ';
    if (detail.use(types::MArg::Beast)) {
        if (const auto beast = const_cast<Motion&>(value).beast()) stream << "beast=" << beast->id();
        else stream << prop_.msgs.noBeast;
    }
    return stream.str();
}

MotionPtr MotionView::motion(const std::size_t index) const noexcept {
    return index < motions_.size() ? motions_[index] : MotionPtr{};
}

void MotionView::motion(const std::size_t index, MotionPtr value, const bool store) {
    if (readOnly_ || index >= motions_.size() || !value) return;
    const auto action = [this, index, value = std::move(value)]() {
        detachMotion(motions_[index]);
        motions_[index] = std::move(value);
        attachMotion(motions_[index]);
        return true;
    };
    if (store) (void)mutate(action);
    else if (action()) notify();
}

MotionPtr MotionView::selection() const noexcept {
    return selected_ >= 0 ? motion(static_cast<std::size_t>(selected_)) : MotionPtr{};
}

void MotionView::setSelectedIndex(const std::ptrdiff_t index, const bool notifySelection) {
    const auto clamped = index < 0 || index >= static_cast<std::ptrdiff_t>(motions_.size())
        ? -1 : index;
    if (selected_ == clamped) {
        (void)refreshSels();
        return;
    }
    selected_ = clamped;
    (void)refreshSels(true);
    if (notifySelection) selectionChanged.call();
}

bool MotionView::select(const std::ptrdiff_t index) {
    if (index < -1 || index >= static_cast<std::ptrdiff_t>(motions_.size())) return false;
    setSelectedIndex(index, true);
    return true;
}

bool MotionView::select(const MotionPtr& value) {
    return select(indexOf(value));
}

std::vector<MotionPtr> MotionView::motions() const { return motions_; }

void MotionView::motions(std::vector<MotionPtr> values) {
    for (const auto& value : motions_) detachMotion(value);
    motions_.clear();
    for (const auto& value : values) {
        if (!value) continue;
        auto copy = value->dup();
        attachMotion(copy);
        motions_.push_back(std::move(copy));
    }
    selected_ = motions_.empty() ? -1 : 0;
    oldIndex_ = -1;
    if (undo_) undo_->reset();
    notify();
}

const MotionRow* MotionView::row(const std::size_t index) const noexcept {
    return index < rows_.size() ? &rows_[index] : nullptr;
}

MotionPtr MotionView::createMT(const types::MType type) {
    if (readOnly_ || (type == types::MType::NoEffect &&
                      summary_ && summary_->legacy())) return {};
    auto value = std::make_shared<Motion>(type, types::Element::All);
    value->damageType(type == types::MType::GetSkillPower ||
                      type == types::MType::LoseSkillPower
                          ? types::DamageType::Max
                          : types::DamageType::LevelRatio);
    value->uValue(Motion::uValue_min);
    value->aValue(0);
    value->round(std::clamp(prop_.looks.motionRoundDefault,
                            Motion::round_min,
                            static_cast<std::int32_t>(prop_.etc.roundMax)));
    value->newBeast({});
    if (!appendMotion(value)) return {};
    (void)select(static_cast<std::ptrdiff_t>(motions_.size() - 1));
    return value;
}

std::ptrdiff_t MotionView::indexOf(const MotionPtr& value) const noexcept {
    if (!value) return -1;
    const auto found = std::find_if(motions_.begin(), motions_.end(),
                                    [&value](const MotionPtr& item) {
                                        return item.get() == value.get();
                                    });
    return found == motions_.end() ? -1 : static_cast<std::ptrdiff_t>(
        std::distance(motions_.begin(), found));
}

bool MotionView::setElement(const types::Element value) {
    const auto selected = selection();
    if (!selected || selected->element() == value || readOnly_) return false;
    const bool oldVan = hasVan();
    const bool result = mutate([selected, value] {
        selected->element(value);
        return true;
    });
    if (result && oldVan != hasVan()) warningEvent.call();
    return result;
}

bool MotionView::setDamageType(const types::DamageType value) {
    const auto selected = selection();
    if (!selected || selected->damageType() == value || readOnly_) return false;
    const bool result = mutate([selected, value] {
        selected->damageType(value);
        return true;
    });
    if (result) warningEvent.call();
    return result;
}

bool MotionView::setAbilityValue(const std::int32_t value) {
    const auto selected = selection();
    if (!selected || selected->aValue() == value || readOnly_) return false;
    return mutate([selected, value] {
        selected->aValue(value);
        return true;
    });
}

void MotionView::roundEnter(const std::int32_t value) {
    const auto selected = selection();
    if (!selected || selected->round() == value || readOnly_) return;
    (void)mutate([selected, value] {
        selected->round(value);
        return true;
    });
}

std::int32_t MotionView::roundCancel(const std::int32_t oldValue) {
    roundEnter(oldValue);
    return oldValue;
}

void MotionView::valEnter(const std::int32_t value) {
    const auto selected = selection();
    if (!selected || static_cast<std::int32_t>(selected->uValue()) == value || readOnly_) return;
    (void)mutate([selected, value] {
        selected->uValue(value);
        return true;
    });
}

std::int32_t MotionView::valCancel(const std::int32_t oldValue) {
    valEnter(oldValue);
    return oldValue;
}

bool MotionView::swap(const std::ptrdiff_t first, const std::ptrdiff_t second,
                      const bool store) {
    if (readOnly_ || first < 0 || second < 0 ||
        first >= static_cast<std::ptrdiff_t>(motions_.size()) ||
        second >= static_cast<std::ptrdiff_t>(motions_.size()) || first == second)
        return false;
    const auto action = [this, first, second] {
        std::swap(motions_[static_cast<std::size_t>(first)],
                  motions_[static_cast<std::size_t>(second)]);
        if (selected_ == first) selected_ = second;
        else if (selected_ == second) selected_ = first;
        return true;
    };
    if (store) {
        const auto firstMotion = motions_[static_cast<std::size_t>(first)];
        const auto secondMotion = motions_[static_cast<std::size_t>(second)];
        const auto savedSelection = std::make_shared<std::ptrdiff_t>(selected_);
        const std::weak_ptr<void> lifetime = lifetimeToken_;
        const auto exchange = [this, lifetime, firstMotion, secondMotion, savedSelection] {
            if (lifetime.expired() || readOnly_) return;
            const auto firstIndex = indexOf(firstMotion);
            const auto secondIndex = indexOf(secondMotion);
            if (firstIndex < 0 || secondIndex < 0 || firstIndex == secondIndex) return;
            const auto previousSelection = std::exchange(*savedSelection, selected_);
            // D版の UndoSwap と同様、有効な行を交換してから MVUndo で選択を
            // 交換する。 一覧全体を復元すると、モードレスのコメントの未確定入力が失われる。
            std::swap(motions_[static_cast<std::size_t>(firstIndex)],
                      motions_[static_cast<std::size_t>(secondIndex)]);
            selected_ = previousSelection;
            oldIndex_ = -1;
        };
        if (!action()) return false;
        if (undo_) undo_->push(exchange, exchange);
        if (!lifetime.expired()) notify();
        return true;
    }
    const bool result = action();
    if (result) notify(false);
    return result;
}

bool MotionView::up() {
    return selected_ > 0 && swap(selected_, selected_ - 1);
}

bool MotionView::down() {
    return selected_ >= 0 && selected_ + 1 < static_cast<std::ptrdiff_t>(motions_.size()) &&
           swap(selected_, selected_ + 1);
}

bool MotionView::removeMotion() { return removeMotion(-1, true, true, true); }

bool MotionView::removeMotion(std::ptrdiff_t index, const bool callEvent,
                              const bool callMod, const bool store) {
    if (readOnly_) return false;
    if (index < 0) index = selected_;
    if (index < 0 || index >= static_cast<std::ptrdiff_t>(motions_.size())) return false;
    const auto action = [this, index] {
        const auto position = static_cast<std::size_t>(index);
        detachMotion(motions_[position]);
        motions_.erase(motions_.begin() + index);
        if (motions_.empty()) selected_ = -1;
        else if (selected_ >= static_cast<std::ptrdiff_t>(motions_.size()))
            selected_ = static_cast<std::ptrdiff_t>(motions_.size()) - 1;
        return true;
    };
    const std::weak_ptr<void> lifetime = lifetimeToken_;
    const bool oldVan = hasVan();
    bool result = false;
    if (store) result = mutate(action, true, callEvent);
    else {
        result = action();
        if (result) {
            if (callMod) notify(callEvent);
            else refreshWidgets();
        }
    }
    if (!lifetime.expired() && result && callEvent && oldVan != hasVan()) warningEvent.call();
    return result;
}

bool MotionView::appendMotion(MotionPtr value, std::ptrdiff_t index,
                              const bool callEvent, const bool callMod,
                              const bool store) {
    if (readOnly_ || !value) return false;
    if (index < 0 || index > static_cast<std::ptrdiff_t>(motions_.size()))
        index = static_cast<std::ptrdiff_t>(motions_.size());
    const bool oldVan = hasVan();
    const auto action = [this, index, value = std::move(value)]() {
        attachMotion(value);
        motions_.insert(motions_.begin() + index, std::move(value));
        if (selected_ < 0) selected_ = index;
        else if (selected_ >= index) ++selected_;
        return true;
    };
    bool result = false;
    if (store) result = mutate(action, true, callEvent);
    else {
        result = action();
        if (result) {
            if (callMod) notify(callEvent);
            else refreshWidgets();
        }
    }
    if (result && callEvent && oldVan != hasVan()) warningEvent.call();
    return result;
}

bool MotionView::refreshSels(const bool force) {
    if (!force && oldIndex_ == selected_) {
        refEnabled();
        return false;
    }
    oldIndex_ = selected_;
    refreshParameterPane();
    refreshRows();
    refEnabled();
    return true;
}

void MotionView::refEnabled() {
    const auto selected = selection();
    const bool writable = !readOnly_ && static_cast<bool>(selected);
    if (categoryTabs_) {
        const auto count = motions_.size();
        const auto index = selected_ < 0 ? std::size_t{0}
                                         : static_cast<std::size_t>(selected_);
        for (auto& page : categoryTabs_->pages) {
            const auto toolbar =
                std::dynamic_pointer_cast<lib::ToolBar>(page.content);
            if (!toolbar) continue;
            toolbar->set_enabled(!readOnly_);
            auto items = toolbar->items;
            for (auto& item : items) {
                if (item.command == "delete") item.enabled = writable;
                else if (item.command == "up")
                    item.enabled = writable && index > 0U;
                else if (item.command == "down")
                    item.enabled = writable && index + 1U < count;
                else if (item.command.starts_with("new:"))
                    item.enabled = !readOnly_ &&
                        (item.command != "new:" + std::to_string(static_cast<int>(types::MType::NoEffect)) ||
                         !summary_ || !summary_->legacy());
            }
            (void)lib::replace_tool_bar_items(toolbar, std::move(items));
        }
    }
    if (motionElementTable_) motionElementTable_->set_enabled(writable);
    if (maxNestControl_) maxNestControl_->set_enabled(
        writable && selected->beast() && selected->beast()->linkId() != 0 &&
        !isMotionTargetVersion("1"));
    if (valueControl_) valueControl_->set_enabled(
        writable && selected->damageType() != types::DamageType::Max);
    if (skillPowerControl_) skillPowerControl_->set_enabled(
        writable && selected->damageType() != types::DamageType::Max);
    for (auto& [type, control] : damageTypeControls_)
        if (control) control->set_enabled(writable);
    for (auto& [type, control] : skillPowerTypeControls_)
        if (control) control->set_enabled(
            writable && (!summary_ || !summary_->legacy() ||
                         selected->damageType() != types::DamageType::Max));
    if (beastsControl_) beastsControl_->set_enabled(!readOnly_ && summary_ != nullptr);
    if (setBeastControl_) setBeastControl_->set_enabled(canSetBeast());
}

bool MotionView::hasVan() const noexcept {
    return std::any_of(motions_.begin(), motions_.end(), [](const MotionPtr& value) {
        return value && value->type() == types::MType::VanishTarget &&
               value->element() != types::Element::Miracle;
    });
}

void MotionView::beastIncSearch(const StringView text) {
    beastSearch_ = String(text);
    if (beastSearchControl_ && beastSearchControl_->text != beastSearch_) {
        beastSearchControl_->text = beastSearch_;
    }
    refBeasts();
}

bool MotionView::canSetBeast() const noexcept {
    if (readOnly_) return false;
    if (selectedBeast_) return true;
    const auto value = selection();
    return value && static_cast<bool>(value->beast());
}

bool MotionView::setSelectedBeast(const std::optional<std::size_t> index) {
    const auto selected = !index || *index >= beastChoices_.size()
        ? BeastPtr{} : beastChoices_[*index].value;
    if (beastsControl_) {
        auto items = beastsControl_->items;
        if (!lib::replace_combo_items(beastsControl_, std::move(items), index,
                                      beastsControl_->edit_text)) return false;
    }
    selectedBeast_ = selected;
    refEnabled();
    return true;
}

bool MotionView::setBeast(const BeastPtr& value) {
    const auto selected = selection();
    if (!selected || !selected->detail().use(types::MArg::Beast) || readOnly_) return false;
    if (!value && !selected->beast()) return false;
    const std::weak_ptr<void> lifetime = lifetimeToken_;
    const bool result = mutate([this, selected, value] {
        if (!value) selected->newBeast({});
        else if (prop_.linkCard() || !summary_ || !summary_->legacy()) {
            auto reference = std::make_shared<BeastCard>(prop_.sys ? prop_.sys : &xmlSystem_, 1U, "",
                                                         lib::Array<lib::Owner<card::CardImage>>{}, "");
            reference->linkId(value->id());
            selected->newBeast(reference);
        } else {
            selected->beast(value);
        }
        resetMaxNest(selected);
        return true;
    });
    if (result && !lifetime.expired()) {
        const auto beastChangedCopy = beastChanged;
        beastChangedCopy.call(selected->beast());
        if (!lifetime.expired()) refEnabled();
    }
    return result;
}

bool MotionView::removeRef() {
    const auto selected = selection();
    if (!summary_ || !selected || !selected->beast() ||
        selected->beast()->linkId() == 0 || readOnly_)
        return false;
    const auto target = summary_->beast(selected->beast()->linkId());
    if (!target) return false;
    const auto id = selected->beast()->id();
    const bool result = mutate([this, selected, target, id] {
        selected->beast()->deepCopy(*target);
        selected->beast()->id(id);
        selected->beast()->linkId(0);
        // D版の removeRef と同様、最終的なリセット後の値を UndoEdit 用に記録する。
        resetMaxNest(selected);
        return true;
    });
    return result;
}

BeastPtr MotionView::linkedBeast(const BeastPtr& value) const {
    if (!value) return {};
    if (value->linkId() && summary_) return summary_->beast(value->linkId());
    return value;
}

bool MotionView::canEditBeast() const noexcept {
    const auto value = selection();
    if (!summary_ || !value || !value->beast()) return false;
    return !value->beast()->linkId() ||
           static_cast<bool>(summary_->beast(value->beast()->linkId()));
}

BeastPtr MotionView::editBeast() const { return linkedBeast(selection() ? selection()->beast() : BeastPtr{}); }

void MotionView::editBeastM() { (void)openBeastCardView(); }

void MotionView::editBeastUseEvent() {
    const auto value = selection();
    if (value && value->detail().use(types::MArg::Beast))
        (void)openBeastEventWin(value->beast());
}

void MotionView::retireDetachedBeastEvents() {
    const std::weak_ptr<void> lifetime = lifetimeToken_;
    const auto closeEvents = prop_.closeUseEvents;
    std::vector<std::pair<MotionPtr, BeastPtr>> closing;
    for (auto it = openedBeastEvents_.begin(); it != openedBeastEvents_.end();) {
        const auto& beast = it->second;
        const bool current = beast && !beast->linkId() &&
            std::any_of(motions_.begin(), motions_.end(), [&](const MotionPtr& motion) {
                return motion && motion->beast() == beast;
            });
        if (current) ++it;
        else { closing.push_back(*it); it = openedBeastEvents_.erase(it); }
    }
    // D版の delBeast と同様、削除・置換時は次のGUI表示への反映前に、元の内側カード
    // のエディタを正確に無効化する。 リンク先 Summary のホストは独立したままにする。
    for (const auto& [owner, beast] : closing) {
        if (lifetime.expired()) return;
        if (closeEvents) closeEvents(beast);
    }
}

void MotionView::trackBeastEvent(const BeastPtr& beast) {
    if (!beast || beast->linkId() != 0U) return;
    for (const auto& opened : openedBeastEvents_)
        if (opened.second == beast) return;
    MotionPtr owner;
    if (const auto rawOwner = std::dynamic_pointer_cast<Motion>(beast->cwxParent().lock()))
        owner = rawOwner->weak_from_this().lock();
    openedBeastEvents_.emplace_back(std::move(owner), beast);
}

bool MotionView::openBeastEventWin(const BeastPtr& value) {
    if (!value || !summary_ || !prop_.openUseEvents) return false;
    const auto target = value->linkId() ? summary_->beast(value->linkId()) : value;
    if (!target) return false;
    trackBeastEvent(value);
    return prop_.openUseEvents(summary_, target, true);
}

bool MotionView::openBeastCardView(const StringView nestedPath,
                                     const bool shellActivate) {
    const auto value = editBeast();
    if (!value) return false;
    const auto open = prop_.openBeastCard;
    if (open) {
        const auto target = selection();
        const std::weak_ptr<void> lifetime = lifetimeToken_;
        const auto pending = std::make_shared<Snapshot>();
        const auto binding = target ? target->beast() : BeastPtr{};
        const auto attached = [this, lifetime, target, binding] {
            return !lifetime.expired() && target && indexOf(target) >= 0 &&
                target->beast() == binding;
        };
        const auto current = [this, attached, binding, value] {
            return attached() && !readOnly_ && binding == value && !value->linkId();
        };
        const bool opened = open(summary_, ucOwner_, value, readOnly_,
            nestedPath, shellActivate,
            [this, current, target, pending] {
                *pending = {};
                // D版と同様、対象の UndoEdit は applyEvent 時に記録する
                // 。 モードレスの子は、親の同期用 pendingUndo_ を共有しない。
                if (current()) *pending = snapshot({target}, indexOf(target));
            },
            [this, current, target, value, pending] {
                auto before = std::exchange(*pending, Snapshot{});
                if (!current() || before.values.empty() ||
                    !before.values.front()->beast()) return;
                if (before.values.front()->beast()->opEquals(*value)) return;
                if (undo_) {
                    const auto state = std::make_shared<Snapshot>(std::move(before));
                    const auto exchange = [this, current, target, value, state] {
                        if (!current() || state->values.empty() ||
                            !state->values.front()->beast()) return;
                        auto latest = snapshot({target}, indexOf(target));
                        // 共通のプロパティ書き込み対象を再利用し、開いているネイティ
                        // ブの子とカードの対応を維持する。 他の Motion 行
                        // や、別途編集中のイベント・コメント欄は有効なままにする。
                        cardpropertytransaction::copyProperties(
                            *value, *state->values.front()->beast());
                        *state = std::move(latest);
                        if (current()) notify();
                    };
                    undo_->push(exchange, exchange);
                }
                if (current()) notify();
            },
            [pending] { *pending = {}; });
        if (lifetime.expired() || !attached()) return opened;
        if (opened) return true;
    }
    // D版と同様、内側の CardDialog の移動結果を返す。 所
    // 有カードへの浅い経路に切り替えて、要求された子パスを捨てない。
    if (!nestedPath.empty()) return false;
    const auto pathValue = value->cwxPath(true);
    if (pathValue.empty()) return false;
    return prop_.openCWXPath &&
           prop_.openCWXPath(path::cpaddattr(pathValue, "shallow"), false);
}

void MotionView::updateBeastToolTip() {
    const auto value = selection();
    const auto beast = value ? value->beast() : BeastPtr{};
    if (!beast) beastToolTip_ = {};
    else if (beast->linkId() && summary_) {
        const auto target = summary_->beast(beast->linkId());
        beastToolTip_ = target
            ? formatTwo(prop_.msgs.cardIsReference, std::to_string(beast->linkId()), target->name())
            : formatNumber(prop_.msgs.referencedCardIsNotFound, beast->linkId());
    } else {
        beastToolTip_ = beast->name();
    }
    if (beastImage_ && beastImage_->tooltip() != beastToolTip_)
        beastImage_->set_tooltip(beastToolTip_);
}

void MotionView::refBeast(const BeastPtr&) { refBeasts(); }

void MotionView::delBeast(CWXPath*, const BeastPtr&) { refBeasts(); }

void MotionView::refSkin(const skin::Skin* value) {
    prop_.skin = value;
    refreshRows();
    if (motionElementTable_) motionElementTable_->set_tooltip({});
    refreshBeastImage();
}

void MotionView::refImageScale(const std::int32_t value) {
    prop_.etc.imageScale = std::max(1, value);
    refreshBeastImage();
}

void MotionView::refCardImageStatus() { refreshBeastImage(); }

void MotionView::refBeasts() {
    std::vector<BeastChoice> choices;
    choices.push_back({{}, prop_.msgs.beastNone});
    if (summary_) {
        for (const auto& value : summary_->beasts()) {
            if (!value || (!beastSearch_.empty() &&
                           lowerCopy(value->name()).find(lowerCopy(beastSearch_)) == String::npos))
                continue;
            choices.push_back({value, std::to_string(value->id()) + "." + value->name()});
        }
    }
    auto selected_beast = selectedBeast_;
    std::optional<std::size_t> selected_index{0U};
    if (selected_beast) {
        const auto found = std::find_if(choices.begin(), choices.end(),
                                        [&selected_beast](const BeastChoice& choice) {
                                            return choice.value &&
                                                   choice.value->id() == selected_beast->id();
                                        });
        if (found != choices.end())
            selected_index = static_cast<std::size_t>(
                std::distance(choices.begin(), found));
        else
            selected_beast.reset();
    }
    if (beastsControl_) {
        std::vector<lib::ChoiceItem> items;
        items.reserve(choices.size());
        for (const auto& choice : choices)
            items.push_back({choice.text,
                             choice.value ? choice.value->objectId() : String{}});
        auto publication = lib::prepare_combo_items(
            beastsControl_, std::move(items), selected_index,
            beastsControl_->edit_text);
        if (!publication.commit()) return;
    }
    beastChoices_.swap(choices);
    selectedBeast_ = std::move(selected_beast);
    refEnabled();
    refreshBeastImage();
}

bool MotionView::canSelectConnectedResource() const noexcept {
    const auto value = linkedBeast(selection() ? selection()->beast() : BeastPtr{});
    // D版と同様、存在し、除外されていないシナリオ素材だけに、この操作を許可する。
    return summary_ && value && summary_->hasMaterial(value->connectedFile(),
        prop_.editorProps ? prop_.editorProps->var().ignorePaths.value : lib::Array<String>{});
}

void MotionView::selectConnectedResource() {
    if (!canSelectConnectedResource()) return;
    const auto value = linkedBeast(selection()->beast());
    if (prop_.openFilePath)
        (void)prop_.openFilePath(value->connectedFile(), false, true);
}

bool MotionView::canWriteComment() const noexcept { return !readOnly_ && selection() != nullptr; }

bool MotionView::writeComment(String value) {
    if (!canWriteComment()) return false;
    const auto selected = selection();
    if (selected->comment() == value) return false;
    return mutate([selected, value = std::move(value)] {
        selected->comment(value);
        return true;
    });
}

void MotionView::closeAuxiliaryEditors() {
    const std::weak_ptr<void> lifetime = lifetimeToken_;
    const auto closeEvents = prop_.closeUseEvents;
    const auto comments = std::move(commentDialogs_);
    const auto events = std::move(openedBeastEvents_);
    // D版の MotionView の破棄と同様、補助のモードレス画面を閉じる。 C++
    // 版では、ホストの終了後もネイティブの親 Window が保持される場合がある。
    for (const auto& [id, session] : comments) {
        session.dialog->applied = {};
        session.dialog->forceCancel();
        if (lifetime.expired()) return;
    }
    if (closeEvents) for (const auto& [owner, beast] : events) {
        closeEvents(beast);
        if (lifetime.expired()) return;
    }
}

void MotionView::writeComment() {
    if (!canWriteComment()) return;
    const auto target = selection();
    const auto id = target->objectId();
    const std::weak_ptr<void> alive = lifetimeToken_;
    if (const auto found = commentDialogs_.find(id); found != commentDialogs_.end()) {
        const auto dialog = found->second.dialog;
        if (dialog->result() == lib::DialogResult::None) {
            found->second.target = target;
            dialog->active();
            return;
        }
        dialog->forceCancel();
        if (alive.expired()) return;
    }
    auto common = std::make_shared<comment::Commons>();
    common->services = prop_.services;
    common->prop.services = prop_.services;
    common->prop.bindDialogSize = prop_.commentSizeBinder;
    if (prop_.editorProps) {
        const auto& messages = prop_.editorProps->requiredMsgs();
        common->prop.msgs.dlgTitComment = messages.dlgTitComment.value;
        common->prop.msgs.dlgTextOK = messages.dlgTextOK.value;
        common->prop.msgs.dlgTextCancel = messages.dlgTextCancel.value;
        common->prop.var.etc.textTabs = prop_.editorProps->var().textTabs.value;
    }
    const auto dialog = std::make_shared<comment::CommentDialog>(*common,
        prop_.dialogOwner.lock(), target->comment());
    if (prop_.editorProps) {
        const auto& messages = prop_.editorProps->requiredMsgs();
        // D版と同様、Motion の表の行と同じグループ・名前のラベル
        // を使う。 長い機能説明は、タイトルではなくツールチップに置く。
        dialog->title = ::cwxeditor::cwx::utils::tryFormat(
            messages.dlgTitCommentWith.value,
            motionRowName(prop_.msgs, target->type()));
    }
    const std::weak_ptr<comment::CommentDialog> weak_dialog = dialog;
    commentDialogs_[id] = {common, dialog, target};
    dialog->applied = [this, alive, id, weak_dialog](const StringView text) {
        if (alive.expired()) return;
        const auto current_dialog = weak_dialog.lock();
        if (!current_dialog || current_dialog->result() != lib::DialogResult::None) return;
        const auto current = std::find_if(motions_.begin(), motions_.end(),
            [&](const MotionPtr& value) {
                return value && value->objectId() == id;
            });
        if (current == motions_.end()) return;
        const auto edited = *current;
        if (edited->comment() == text) return;
        (void)mutate([edited, text = String{text}] { edited->comment(text); return true; });
    };
    // 所有する MotionView や編集状態が無効になった後も、ネイティブの表示側が閉
    // じたダイアログを保持する場合がある。 借用される Commons の寿命を維持する。
    dialog->closed = [this, alive, id, weak_dialog, common] {
        if (alive.expired()) return;
        const auto found = commentDialogs_.find(id);
        if (found != commentDialogs_.end() && found->second.dialog == weak_dialog.lock())
            commentDialogs_.erase(found);
    };
    (void)dialog->open();
}

bool MotionView::isMotionTargetVersion(const StringView version) const {
    return prop_.isTargetVersion(summary_, version);
}

std::vector<String> MotionView::warnings(const std::size_t index) const {
    std::vector<String> result;
    const auto value = motion(index);
    if (!value) return result;
    const auto name = prop_.msgs.motionName(value->type());
    if (value->type() == types::MType::CancelAction && !prop_.targetVersion(summary_, "1.50"))
        result.push_back(formatTwo(prop_.msgs.warningUnknownMotion, name, "1.50"));
    if (value->type() == types::MType::NoEffect && !isMotionTargetVersion("2"))
        result.push_back(formatTwo(prop_.msgs.warningUnknownMotionWsn, name, "2"));
    if (value->type() == types::MType::SummonBeast && value->beast() &&
        value->beast()->linkId() &&
        (!summary_ || !summary_->beast(value->beast()->linkId()))) {
        result.push_back(formatNumber(prop_.msgs.searchErrorLinkIdBeastNotFound,
                                       value->beast()->linkId()));
    }
    if ((value->type() == types::MType::GetSkillPower ||
         value->type() == types::MType::LoseSkillPower) &&
        value->damageType() != types::DamageType::Max && !isMotionTargetVersion("1"))
        result.push_back(prop_.msgs.warningSkillPowerWithFixedValue);
    return result;
}

std::vector<String> MotionView::warnings() const {
    std::vector<String> result;
    for (std::size_t index = 0; index < motions_.size(); ++index) {
        for (const auto& warning : warnings(index)) {
            if (std::find(result.begin(), result.end(), warning) == result.end())
                result.push_back(warning);
        }
    }
    return result;
}

String MotionView::itemToXML(const std::ptrdiff_t index, const bool paneId) const {
    const auto selectedIndex = index < 0 ? selected_ : index;
    if (selectedIndex < 0 || selectedIndex >= static_cast<std::ptrdiff_t>(motions_.size())) return {};
    const auto value = motion(static_cast<std::size_t>(selectedIndex));
    if (!value) return {};
    auto node = value->toNode(xmlOption());
    if (summary_) {
        node.newAttr("summId", summary_->id());
        node.newAttr("scenarioPath", summary_->scenarioPath());
        node.newAttr("scenarioName", summary_->scenarioName());
        node.newAttr("scenarioAuthor", summary_->author());
    }
    if (paneId) node.newAttr("paneId", id_);
    return node.text();
}

String MotionView::makeClipboard(const StringView kind, const xml::XNode& node,
                                 const bool paneId) const {
    auto root = xml::XNode::create(kClipboardRoot);
    root.newAttr("kind", kind);
    if (paneId) root.newAttr("paneId", id_);
    root.append(node.clone());
    return root.text();
}

lib::ClipboardData MotionView::clipboardData() const {
    const auto xmlValue = itemToXML(-1, false);
    if (xmlValue.empty()) return {};
    return prop_.xmlCopy()
        ? xmlbytestransfer::makeTextPayload(xmlValue)
        : xmlbytestransfer::makeXMLPayload(xmlValue);
}

void MotionView::writeClipboard(const lib::ClipboardData& value) {
    clipboardMemory_ = value;
    hasClipboardMemory_ = !value.text.empty() || !value.formats.empty();
    if (const auto backend = comm_.clipboard()) backend->write(value);
}

lib::ClipboardData MotionView::readClipboard() const {
    if (const auto backend = comm_.clipboard()) {
        const auto result = backend->read();
        if (!result.text.empty() || !result.formats.empty()) return result;
    }
    return clipboardMemory_;
}

bool MotionView::clipboardAvailable() const noexcept {
    try {
        return parseClipboard(xmlbytestransfer::CBtoXML(readClipboard())).has_value();
    } catch (...) {
        return false;
    }
}

bool MotionView::copy() {
    if (!canDoC()) return false;
    const auto data = clipboardData();
    if (data.text.empty() && data.formats.empty()) return false;
    writeClipboard(data);
    if (status_) status_->text = prop_.msgs.copied;
    if (comm_.refreshToolBar) comm_.refreshToolBar();
    return true;
}

bool MotionView::cut() {
    if (readOnly_ || !copy()) return false;
    return removeMotion();
}

std::optional<MotionView::ClipboardPayload> MotionView::parseClipboard(
    const StringView text) const {
    if (text.empty()) return std::nullopt;
    try {
        auto root = xml::XNode::parse(text);
        if (root.name() == Motion::XML_NAME) return ClipboardPayload{"motion", root};
        if (root.name() == BeastCard::XML_NAME || root.name() == BeastCard::XML_NAME_M)
            return ClipboardPayload{"beast", root};
        if (root.name() != kClipboardRoot) return std::nullopt;
        auto node = root.child(Motion::XML_NAME, false);
        if (!node) node = root.child(BeastCard::XML_NAME_M, false);
        if (!node) node = root.child(BeastCard::XML_NAME, false);
        if (!node) return std::nullopt;
        const String actual_kind = node.name() == Motion::XML_NAME ? "motion" : "beast";
        if (root.attr("kind", false, actual_kind) != actual_kind) return std::nullopt;
        return ClipboardPayload{actual_kind, node};
    } catch (...) {
        return std::nullopt;
    }
}

bool MotionView::paste() {
    if (readOnly_ || !clipboardAvailable()) return false;
    return pasteData(readClipboard());
}

bool MotionView::pasteData(const lib::ClipboardData& data) {
    if (readOnly_) return false;
    auto xml = xmlbytestransfer::CBtoXML(data);
    if (xml.empty()) xml = data.text; // 従来の非公開形式のデータも受け入れる。
    const auto payload = parseClipboard(xml);
    if (!payload) return false;
    if (payload->kind == "beast") return pasteBeast(*payload);
    return pasteMotion(*payload);
}

bool MotionView::pasteMotion(const ClipboardPayload& payload) {
    if (payload.kind != "motion") return false;
    auto node = payload.node.clone();
    try {
        const String source_path = payload.node.attr("scenarioPath", false);
        const String source_name = payload.node.attr("scenarioName", false);
        const String source_author = payload.node.attr("scenarioAuthor", false);
        auto value = Motion::createFromNode(node, xmlInfo());
        if (!qCardMaterialCopy(value->beast(), source_path, source_name,
                               source_author))
            return false;
        if (!appendMotion(value)) return false;
        select(static_cast<std::ptrdiff_t>(motions_.size() - 1));
        if (status_) status_->text = prop_.msgs.pasted;
        return true;
    } catch (...) {
        return false;
    }
}

bool MotionView::setBeastNode(xml::XNode& node) {
    auto selected = selection();
    if (!selected || !selected->detail().use(types::MArg::Beast) || readOnly_) return false;
    try {
        const auto wrapper = node;
        const bool wrapped = node.name() == BeastCard::XML_NAME_M;
        const String source_summary = wrapped ? wrapper.attr("summId", false) : String{};
        const String source_path = wrapped ? wrapper.attr("scenarioPath", false) : String{};
        const String source_name = wrapped ? wrapper.attr("scenarioName", false) : String{};
        const String source_author = wrapped ? wrapper.attr("scenarioAuthor", false) : String{};
        const bool top_level = wrapped && wrapper.attr<bool>("topLevel", false, false);
        if (wrapped) node = node.child(BeastCard::XML_NAME, true);
        auto value = BeastCard::createFromNode(node, xmlInfo());
        if (!value) return false;
        if (!qCardMaterialCopy(value, source_path, source_name, source_author))
            return false;
        const bool same_scenario = summary_ &&
            ((!source_summary.empty() && source_summary == summary_->id()) ||
             (!source_path.empty() &&
              lowerCopy(source_path) == lowerCopy(summary_->scenarioPath())));
        const auto id = value->id();
        const bool link = id != 0U && same_scenario && top_level &&
            (prop_.linkCard() || !summary_ || !summary_->legacy());
        const bool result = mutate([this, selected, value, id, link] {
            if (link) {
                auto reference = std::make_shared<BeastCard>(
                    prop_.sys ? prop_.sys : &xmlSystem_, 1U, "",
                    lib::Array<lib::Owner<card::CardImage>>{}, "");
                reference->linkId(id);
                selected->newBeast(reference);
            } else {
                selected->newBeast(value);
            }
            resetMaxNest(selected);
            return true;
        });
        if (result) {
            beastChanged.call(selected->beast());
            refEnabled();
        }
        return result;
    } catch (...) {
        return false;
    }
}

bool MotionView::pasteBeast(const ClipboardPayload& payload) {
    if (payload.kind != "beast") return false;
    auto node = payload.node.clone();
    return setBeastNode(node);
}

bool MotionView::pasteBeast(xml::XNode& node) {
    return setBeastNode(node);
}

MotionView::MaterialCopyPlan::~MaterialCopyPlan() { detach(); }

void MotionView::MaterialCopyPlan::detach() noexcept {
    if (!counter) return;
    const auto current = std::exchange(counter, {});
    try {
        if (value && value->useCounter() == current)
            value->removeUseCounter();
    } catch (...) {
        // 後処理の失敗で、受け付け済みの外部ドロップを失敗扱いにしない。
    }
}

std::shared_ptr<MotionView::MaterialCopyPlan>
MotionView::prepareMaterialCopyPlan(const BeastPtr& value,
                                    const StringView fromScenarioPath,
                                    const StringView sourceName,
                                    const StringView sourceAuthor) const {
    if (!value || !summary_) return {};
    const String target_path = summary_->scenarioPath();
    if (fromScenarioPath.empty() || target_path.empty()) return {};
    if (lowerCopy(String(fromScenarioPath)) == lowerCopy(target_path)) return {};

    const auto scenario_root = [](const String& path) {
        auto result = utils::nabs(structs::pathFromUtf8(path)).lexically_normal();
        std::error_code error;
        if (std::filesystem::is_regular_file(result, error))
            result = result.parent_path();
        return result;
    };
    const auto source_root = scenario_root(String(fromScenarioPath));
    const auto target_root = scenario_root(target_path);
    utils::debugln("[CARD_COPY_META] sourcePath=", fromScenarioPath,
                   " sourceName=", sourceName,
                   " sourceAuthor=", sourceAuthor,
                   " targetPath=", target_path,
                   " cardCount=1 motionBeast=1");
    utils::debugln("[CARD_COPY_ROOT] source=", structs::pathToUtf8(source_root),
                   " target=", structs::pathToUtf8(target_root));
    if (prop_.services.filesystem
            ? prop_.services.filesystem->filenames_equal(source_root, target_root)
            : source_root == target_root)
        return {};

    auto plan = std::make_shared<MaterialCopyPlan>();
    plan->value = value;
    plan->counter = std::make_shared<MaterialCopyPlan::Counter>(
        static_cast<CWXPath*>(nullptr));
    plan->source_name = sourceName;
    plan->source_author = sourceAuthor;
    plan->target_root = target_root;
    try {
        value->setUseCounter(plan->counter, nullptr);
    } catch (const std::exception& error) {
        utils::debugln("[CARD_COPY_ERROR] counter attach: ", error.what());
        return {};
    } catch (...) {
        utils::debugln("[CARD_COPY_ERROR] counter attach: unknown exception");
        return {};
    }

    for (const auto& id : plan->counter->keys<MaterialCopyPlan::PathId>()) {
        const String old_path = static_cast<String>(id);
        if (old_path.empty()) continue;
        if (id.isBinData()) {
            if (utils::isBinImg(old_path)) {
                plan->candidates.push_back({id, old_path, {}, true});
                utils::debugln("[CARD_COPY_PATH] binary-image candidate bytes=",
                               old_path.size());
            } else {
                utils::debugln("[CARD_COPY_PATH] binary non-image skipped");
            }
            continue;
        }
        auto relative = structs::pathFromUtf8(old_path).lexically_normal();
        bool invalid_relative = relative.empty() || relative.is_absolute();
        for (const auto& part : relative) {
            if (part == "..") {
                invalid_relative = true;
                break;
            }
        }
        if (invalid_relative) {
            utils::debugln("[CARD_COPY_PATH] invalid relative path=", old_path);
            continue;
        }
        const auto source = (source_root / relative).lexically_normal();
        std::error_code error;
        if (!std::filesystem::is_regular_file(source, error)) {
            utils::debugln("[CARD_COPY_PATH] missing or non-file path=",
                           structs::pathToUtf8(source));
            continue;
        }
        plan->candidates.push_back({id, old_path, source, false});
        plan->normal_source_names.push_back(structs::pathToUtf8(source));
    }
    for (const auto& candidate : plan->candidates)
        if (candidate.binary_image) ++plan->binary_count;
    utils::debugln("[CARD_COPY_PATH_COUNT] candidates=", plan->candidates.size(),
                   " normal=", plan->normal_source_names.size(),
                   " binaryImage=", plan->binary_count);
    if (plan->candidates.empty()) return {};
    return plan;
}

MotionView::MaterialCopyResult MotionView::applyMaterialCopy(
    const std::shared_ptr<MaterialCopyPlan>& plan, const bool externalizeBinary,
    const bool copyReferences, const bool showWarning) const {
    if (!plan || !summary_ || !plan->counter)
        return MaterialCopyResult::ContinuedWithoutCopy;
    const bool target_legacy = summary_->legacy();
    const String material_path = prop_.skin
        ? prop_.skin->materialPath()
        : (target_legacy ? String{} : String("Material"));
    const auto material = structs::pathFromUtf8(material_path);
    utils::debugln("[CARD_COPY_DEST] root=", structs::pathToUtf8(plan->target_root),
                   " material=", material_path,
                   " externalizeBinary=", externalizeBinary,
                   " copyReferences=", copyReferences);

    bool failed = false;
    bool copied_any = false;
    for (const auto& candidate : plan->candidates) {
        if (candidate.binary_image && !externalizeBinary) continue;
        if (!candidate.binary_image && !copyReferences) continue;
        try {
            String exported_name;
            if (candidate.binary_image && externalizeBinary && prop_.editorProps) {
                auto users = plan->counter->values(candidate.id);
                std::sort(users.begin(), users.end(), [](const auto& left,
                                                         const auto& right) {
                    if (!left) return false;
                    if (!right) return true;
                    return left->cwxPath(true) < right->cwxPath(true);
                });
                if (!users.empty() && users.front()) {
                    exported_name = summary::pathUserToExportedImageName(
                        prop_.editorProps->parent(), plan->source_name,
                        plan->source_author, *users.front());
                }
            }
            const auto source = candidate.binary_image
                ? utils::Path(candidate.value) : candidate.source;
            const auto copied = utils::copyTo(
                plan->target_root, source, material, candidate.binary_image,
                exported_name);
            if (copied.empty()) {
                failed = true;
                utils::debugln("[CARD_COPY_ERROR] copy failed path=", candidate.value);
                continue;
            }
            const auto new_path = structs::pathToUtf8(copied);
            if (candidate.value != new_path)
                plan->counter->change(candidate.id, usecounter::toPathId(new_path));
            copied_any = true;
            utils::debugln("[CARD_COPY_REWRITE] ", candidate.value, " -> ", new_path);
        } catch (const std::exception& error) {
            failed = true;
            utils::debugln("[CARD_COPY_ERROR] path=", candidate.value,
                           " message=", error.what());
        } catch (...) {
            failed = true;
            utils::debugln("[CARD_COPY_ERROR] path=", candidate.value,
                           " unknown exception");
        }
    }

    if (!target_legacy) {
        for (const auto& id : plan->counter->keys<MaterialCopyPlan::PathId>()) {
            if (!id.isBinData() || !utils::isBinImg(static_cast<String>(id))) continue;
            try {
                plan->counter->change(id, usecounter::toPathId(""), true);
            } catch (const std::exception& error) {
                failed = true;
                utils::debugln("[CARD_COPY_ERROR] binary cleanup: ", error.what());
            } catch (...) {
                failed = true;
                utils::debugln("[CARD_COPY_ERROR] binary cleanup: unknown exception");
            }
        }
    }

    plan->detach();
    if (failed && showWarning && prop_.editorProps && prop_.services.dialogs) {
        lib::MessageDialogRequest warning;
        warning.title = prop_.editorProps->requiredMsgs().dlgTitWarning.value;
        warning.message = prop_.editorProps->requiredMsgs().dlgMsgCopyError.value;
        warning.severity = lib::MessageSeverity::Warning;
        warning.buttons = lib::MessageButtons::OK;
        try {
            (void)prop_.services.dialogs->message(warning);
        } catch (...) {
            // 警告の表示失敗で、受け付け済みの召喚獣を拒否しない。
        }
    }
    const auto result = failed ? MaterialCopyResult::CopiedWithErrors
                               : (copied_any ? MaterialCopyResult::Copied
                                             : MaterialCopyResult::ContinuedWithoutCopy);
    utils::debugln("[CARD_COPY_OUTCOME] result=", static_cast<int>(result),
                   " copied=", copied_any, " failed=", failed);
    return result;
}

void MotionView::showMaterialCopyWarningAsync() const {
    if (!prop_.editorProps || !prop_.services.dialogs) return;
    lib::MessageDialogRequest warning;
    warning.title = prop_.editorProps->requiredMsgs().dlgTitWarning.value;
    warning.message = prop_.editorProps->requiredMsgs().dlgMsgCopyError.value;
    warning.severity = lib::MessageSeverity::Warning;
    warning.buttons = lib::MessageButtons::OK;
    try {
        if (!prop_.services.dialogs->message_async(warning,
                                                   [](const lib::MessageResult) {}))
            utils::debugln("[CARD_COPY_DIALOG] async warning unavailable");
    } catch (const std::exception& error) {
        utils::debugln("[CARD_COPY_DIALOG] async warning exception: ", error.what());
    } catch (...) {
        utils::debugln("[CARD_COPY_DIALOG] async warning unknown exception");
    }
}

bool MotionView::beginExternalMaterialDrop(
    const ClipboardPayload& payload, const std::optional<std::size_t> targetIndex) {
    if (readOnly_ || !summary_ || !prop_.services.dialogs) return false;
    if (!prop_.services.dialogs->supports_message_async()) return false;

    const String source_summary = payload.node.attr("summId", false);
    const String source_path = payload.node.attr("scenarioPath", false);
    const String source_name = payload.node.attr("scenarioName", false);
    const String source_author = payload.node.attr("scenarioAuthor", false);
    if (!source_summary.empty() && source_summary == summary_->id()) return false;

    MotionPtr motion_value;
    MotionPtr target_motion;
    bool top_level = false;
    if (payload.kind == "motion") {
        try {
            auto node = payload.node.clone();
            motion_value = Motion::createFromNode(node, xmlInfo());
        } catch (...) {
            return false;
        }
    } else if (payload.kind == "beast") {
        target_motion = selection();
        if (!target_motion || !target_motion->detail().use(types::MArg::Beast))
            return false;
        auto node = payload.node.clone();
        const bool wrapped = node.name() == BeastCard::XML_NAME_M;
        top_level = wrapped && node.attr<bool>("topLevel", false, false);
        if (wrapped) node = node.child(BeastCard::XML_NAME, true);
        try {
            motion_value = MotionPtr{};
            auto beast = BeastCard::createFromNode(node, xmlInfo());
            if (!beast) return false;
            motion_value = std::make_shared<Motion>(
                types::MType::SummonBeast, types::Element::All);
            motion_value->newBeast(std::move(beast));
        } catch (...) {
            return false;
        }
    } else {
        return false;
    }

    const auto value = motion_value ? motion_value->beast() : BeastPtr{};
    auto plan = prepareMaterialCopyPlan(value, source_path, source_name, source_author);
    if (!plan) return false;

    struct Pending final {
        std::weak_ptr<void> lifetime;
        MotionView* view{};
        std::shared_ptr<MaterialCopyPlan> plan;
        MotionPtr motion;
        MotionPtr target_motion;
        std::optional<std::size_t> target_index;
        String source_summary;
        String source_path;
        bool top_level{};
        bool motion_payload{};
        bool externalize_binary{};
        bool copy_references{};
    };
    const auto pending = std::make_shared<Pending>(Pending{
        lifetimeToken_, this, plan,
        payload.kind == "motion" ? motion_value : MotionPtr{},
        payload.kind == "beast" ? target_motion : MotionPtr{},
        targetIndex, source_summary, source_path, top_level, payload.kind == "motion",
        false, false});
    const auto dialogs = prop_.services.dialogs;

    const auto complete = std::make_shared<std::function<void()>>();
    *complete = [pending] {
        auto* self = pending->view;
        if (!self || pending->lifetime.expired() || self->disposed_ ||
            self->readOnly_ || !self->summary_) return;
        const auto result = self->applyMaterialCopy(
            pending->plan, pending->externalize_binary,
            pending->copy_references, false);
        if (pending->motion_payload) {
            const auto index = static_cast<std::ptrdiff_t>(std::min(
                pending->target_index.value_or(self->motions_.size()),
                self->motions_.size()));
            if (!self->appendMotion(pending->motion, index)) return;
            self->select(index);
            if (self->status_) self->status_->text = self->prop_.msgs.pasted;
        } else {
            const auto target = pending->target_motion;
            if (!target || self->indexOf(target) < 0 ||
                !target->detail().use(types::MArg::Beast)) return;
            const bool same_scenario =
                (!pending->source_summary.empty() &&
                 pending->source_summary == self->summary_->id()) ||
                (!pending->source_path.empty() &&
                 lowerCopy(pending->source_path) ==
                     lowerCopy(self->summary_->scenarioPath()));
            const auto id = pending->plan->value ? pending->plan->value->id() : 0U;
            const bool can_link = id != 0U && same_scenario && pending->top_level &&
                (self->prop_.linkCard() || !self->summary_ ||
                 !self->summary_->legacy());
            const auto value = pending->plan->value;
            const bool changed = self->mutate([self, target, value, id, can_link] {
                if (can_link) {
                    auto reference = std::make_shared<BeastCard>(
                        self->prop_.sys ? self->prop_.sys : &self->xmlSystem_,
                        1U, "", lib::Array<lib::Owner<card::CardImage>>{}, "");
                    reference->linkId(id);
                    target->newBeast(reference);
                } else {
                    target->newBeast(value);
                }
                self->resetMaxNest(target);
                return true;
            });
            if (!changed) return;
            self->beastChanged.call(target->beast());
            self->refBeasts();
            self->refEnabled();
        }
        utils::debugln("[CARD_ADD_MATERIAL] result=", static_cast<int>(result),
                       " motion=", pending->motion_payload, " async=1");
        if (result == MaterialCopyResult::CopiedWithErrors)
            self->showMaterialCopyWarningAsync();
    };

    const auto make_request = [this](const String& message) {
        lib::MessageDialogRequest request;
        request.title = prop_.editorProps
            ? prop_.editorProps->requiredMsgs().dlgTitQuestion.value : String("確認");
        request.message = message;
        request.severity = lib::MessageSeverity::Question;
        request.buttons = lib::MessageButtons::YesNoCancel;
        return request;
    };
    const auto normal_message = plan->normal_source_names.size() == 1U
        ? utils::tryFormat(prop_.editorProps
                               ? prop_.editorProps->requiredMsgs().dlgMsgCopyMaterial2.value
                               : String("Copy referenced material?\n%1$s"),
                           plan->normal_source_names.front())
        : utils::tryFormat(prop_.editorProps
                               ? prop_.editorProps->requiredMsgs().dlgMsgCopyMaterial3.value
                               : String("Copy %1$s referenced materials?"),
                           plan->normal_source_names.size());
    const auto normal_request = make_request(normal_message);
    const auto binary_request = make_request(
        summary_->legacy() && prop_.editorProps
            ? prop_.editorProps->requiredMsgs().dlgMsgCopyMaterial1.value
            : (prop_.editorProps ? prop_.editorProps->requiredMsgs().dlgMsgExcludeImage.value
                                 : String("Copy embedded image material?")));

    const auto ask_normal = std::make_shared<std::function<void()>>();
    *ask_normal = [pending, dialogs, normal_request, complete] {
        if (pending->plan->normal_source_names.empty()) {
            (*complete)();
            return;
        }
        bool started = false;
        try {
            started = dialogs->message_async(normal_request,
                [pending, complete](const lib::MessageResult answer) {
                    if (answer == lib::MessageResult::Cancel ||
                        answer == lib::MessageResult::None) {
                        utils::debugln("[CARD_COPY_DIALOG] answer=cancel");
                        return;
                    }
                    pending->copy_references = answer == lib::MessageResult::Yes;
                    utils::debugln("[CARD_COPY_DIALOG] answer=",
                                   pending->copy_references ? "yes" : "no");
                    (*complete)();
                });
        } catch (const std::exception& error) {
            utils::debugln("[CARD_COPY_DIALOG] async exception: ", error.what());
        } catch (...) {
            utils::debugln("[CARD_COPY_DIALOG] async unknown exception");
        }
        if (started) {
            utils::debugln("[MATERIAL_DIALOG_ENTER] beast-copy-async");
            return;
        }
        utils::debugln("[CARD_COPY_DIALOG] async unavailable; continuing without copy");
        (*complete)();
    };

    const bool ask_binary = plan->binary_count > 0U && prop_.editorProps &&
        (!summary_->legacy() || prop_.editorProps->var().saveInnerImagePath.value);
    if (ask_binary) {
        bool started = false;
        try {
            started = dialogs->message_async(binary_request,
                [pending, ask_normal](const lib::MessageResult answer) {
                    if (answer == lib::MessageResult::Cancel ||
                        answer == lib::MessageResult::None) {
                        utils::debugln("[CARD_COPY_DIALOG] answer=cancel");
                        return;
                    }
                    pending->externalize_binary = answer == lib::MessageResult::Yes;
                    utils::debugln("[CARD_COPY_DIALOG] answer=",
                                   pending->externalize_binary ? "yes" : "no");
                    (*ask_normal)();
                });
        } catch (const std::exception& error) {
            utils::debugln("[CARD_COPY_DIALOG] async exception: ", error.what());
        } catch (...) {
            utils::debugln("[CARD_COPY_DIALOG] async unknown exception");
        }
        if (started) {
            utils::debugln("[MATERIAL_DIALOG_ENTER] beast-copy-async");
            return true;
        }
        utils::debugln("[CARD_COPY_DIALOG] binary prompt unavailable; asking referenced materials");
        (*ask_normal)();
        return true;
    }
    if (!plan->normal_source_names.empty()) {
        (*ask_normal)();
        return true;
    }
    (*complete)();
    return true;
}

bool MotionView::qCardMaterialCopy(const BeastPtr& value,
                                   const StringView fromScenarioPath,
                                   const StringView sourceName,
                                   const StringView sourceAuthor) const {
    const auto plan = prepareMaterialCopyPlan(value, fromScenarioPath,
                                               sourceName, sourceAuthor);
    if (!plan) return true;

    bool cancelled = false;
    const auto question = [&](const String& message) {
        if (!prop_.editorProps || !prop_.services.dialogs) {
            utils::debugln("[CARD_COPY_DIALOG] unavailable; continuing without copy");
            return false;
        }
        lib::MessageDialogRequest request;
        request.title = prop_.editorProps->requiredMsgs().dlgTitQuestion.value;
        request.message = message;
        request.severity = lib::MessageSeverity::Question;
        request.buttons = lib::MessageButtons::YesNoCancel;
        lib::MessageResult answer = lib::MessageResult::None;
        try {
            utils::debugln("[MATERIAL_DIALOG_ENTER] beast-copy");
            answer = prop_.services.dialogs->message(request);
        } catch (const std::exception& error) {
            utils::debugln("[CARD_COPY_DIALOG] exception: ", error.what());
            return false;
        } catch (...) {
            utils::debugln("[CARD_COPY_DIALOG] unknown exception");
            return false;
        }
        if (answer == lib::MessageResult::Cancel || answer == lib::MessageResult::None) {
            cancelled = true;
            utils::debugln("[CARD_COPY_DIALOG] answer=cancel");
            return false;
        }
        utils::debugln("[CARD_COPY_DIALOG] answer=",
                       answer == lib::MessageResult::Yes ? "yes" : "no");
        return answer == lib::MessageResult::Yes;
    };

    bool externalize_binary = false;
    if (plan->binary_count > 0U && prop_.editorProps &&
        (!summary_->legacy() || prop_.editorProps->var().saveInnerImagePath.value)) {
        externalize_binary = question(
            summary_->legacy() ? prop_.editorProps->requiredMsgs().dlgMsgCopyMaterial1.value
                               : prop_.editorProps->requiredMsgs().dlgMsgExcludeImage.value);
    }
    bool copy_references = false;
    if (!cancelled && !plan->normal_source_names.empty()) {
        const auto message = plan->normal_source_names.size() == 1U
            ? utils::tryFormat(prop_.editorProps
                                   ? prop_.editorProps->requiredMsgs().dlgMsgCopyMaterial2.value
                                   : String("Copy referenced material?\n%1$s"),
                               plan->normal_source_names.front())
            : utils::tryFormat(prop_.editorProps
                                   ? prop_.editorProps->requiredMsgs().dlgMsgCopyMaterial3.value
                                   : String("Copy %1$s referenced materials?"),
                               plan->normal_source_names.size());
        copy_references = question(message);
    }
    if (cancelled) {
        utils::debugln("[CARD_COPY_OUTCOME] cancelled");
        return false;
    }
    (void)applyMaterialCopy(plan, externalize_binary, copy_references, true);
    return true;
}

bool MotionView::clone() {
    if (readOnly_ || !canDoC()) return false;
    const auto xml = itemToXML(-1, false);
    const auto payload = parseClipboard(xml);
    return payload && pasteMotion(*payload);
}

void MotionView::beginDrag() { dragSource_ = selected_; }

lib::ClipboardData MotionView::dragData() const {
    return clipboardData();
}

bool MotionView::drop(lib::DragEvent& event) {
    if (readOnly_) return false;
    // 移動は、この MotionView の内部だけで許可する。書き
    // 込み可能な MotionView は別ペインへのコピー元になれ
    // るが、 読み取り専用の外部の転送元にカードの削除を要求しない。
    if (!event.same_pane() && hasOperation(event.selected, lib::DropOperation::Move))
        event.selected = copyOperation(event.allowed);
    const auto source = event.origin.cursor_index
        ? static_cast<std::ptrdiff_t>(*event.origin.cursor_index) : dragSource_;
    if (hasOperation(event.selected, lib::DropOperation::Move) &&
        event.same_pane() && source >= 0 &&
        source < static_cast<std::ptrdiff_t>(motions_.size())) {
        auto target = static_cast<std::ptrdiff_t>(std::min(
            event.target_index.value_or(motions_.size()), motions_.size()));
        if (target > source) --target;
        const std::weak_ptr<void> lifetime = lifetimeToken_;
        if (target != source) {
            const auto moved = motions_[static_cast<std::size_t>(source)];
            const auto savedSelection = std::make_shared<std::ptrdiff_t>(selected_);
            const auto destination = std::make_shared<std::ptrdiff_t>(source);
            // D版の UndoMove と同様、移動したオブジェクトだけを削除・再挿入する。
            // 影響しない行は objectId とモードレスコメントの未確定入力を保持する。
            const auto exchange = [this, lifetime, moved, destination, savedSelection] {
                if (lifetime.expired() || readOnly_) return;
                const auto current = indexOf(moved);
                if (current < 0 || *destination < 0 ||
                    *destination >= static_cast<std::ptrdiff_t>(motions_.size())) return;
                const auto next = std::exchange(*destination, current);
                motions_.erase(motions_.begin() + current);
                motions_.insert(motions_.begin() + next, moved);
                selected_ = std::exchange(*savedSelection, selected_);
                oldIndex_ = -1;
                if (const auto comment = commentDialogs_.find(moved->objectId()); comment != commentDialogs_.end())
                    pendingReorderCommentCancel_ = comment->second.dialog;
            };
            motions_.erase(motions_.begin() + source);
            motions_.insert(motions_.begin() + target, moved);
            selected_ = target;
            if (const auto comment = commentDialogs_.find(moved->objectId()); comment != commentDialogs_.end())
                pendingReorderCommentCancel_ = comment->second.dialog;
            if (undo_) undo_->push(exchange, exchange);
            event.handled = true;
            if (lifetime.expired()) return true;
            dragSource_ = -1;
            notify();
            return true;
        }
        dragSource_ = -1;
        event.handled = true;
        return true;
    }
    auto xml = xmlbytestransfer::CBtoXML(event.data);
    if (xml.empty()) xml = event.data.text;
    const auto payload = parseClipboard(xml);
    if (!payload) return false;
    if (!event.same_pane() &&
        beginExternalMaterialDrop(*payload, event.target_index)) {
        // この継続処理が動く時点で、共通の転送処理は
        // 終了済み。 モデルの変更は、非ブロッキング
        // の素材ダイアログのコールバックで完了する。
        event.handled = true;
        return true;
    }
    if (payload->kind == "motion") {
        auto node = payload->node.clone();
        try {
            const String source_path = payload->node.attr("scenarioPath", false);
            const String source_name = payload->node.attr("scenarioName", false);
            const String source_author = payload->node.attr("scenarioAuthor", false);
            auto value = Motion::createFromNode(node, xmlInfo());
            if (!qCardMaterialCopy(value->beast(), source_path, source_name,
                                   source_author))
                return false;
            const auto target = static_cast<std::ptrdiff_t>(std::min(
                event.target_index.value_or(motions_.size()), motions_.size()));
            if (!appendMotion(std::move(value), target)) return false;
            select(target);
        } catch (...) { return false; }
    } else if (!pasteBeast(*payload)) return false;
    event.handled = true;
    return true;
}

lib::DragSourceBinding MotionView::dragSourceBinding() {
    lib::DragSourceBinding result;
    result.source = motionsTable_;
    result.allowed = readOnly_ ? lib::DropOperation::Copy | lib::DropOperation::Link
                               : lib::DropOperation::Copy | lib::DropOperation::Move;
    result.provide_data = [this] { beginDrag(); return dragData(); };
    result.provide_origin = [this] {
        lib::DragOrigin origin;
        origin.pane_id = id_;
        if (dragSource_ >= 0)
            origin.cursor_index = static_cast<std::size_t>(dragSource_);
        origin.source_identity = reinterpret_cast<std::uintptr_t>(this);
        return origin;
    };
    result.completed = [this](const lib::DropOperation operation) {
        if (hasOperation(operation, lib::DropOperation::Move) && dragSource_ >= 0)
            (void)removeMotion(dragSource_, true, true, true);
        dragSource_ = -1;
    };
    return result;
}

lib::DropTargetBinding MotionView::dropTargetBinding() {
    lib::DropTargetBinding result;
    result.target = motionsTable_;
    result.accepted_formats = {String(xmlbytestransfer::XML_BYTES_FORMAT),
                               String(xmlbytestransfer::XML_BYTES_MIME),
                               "text/xml", "text/plain"};
    result.pane_id = id_;
    result.accepts_target_index = [this](const std::optional<std::size_t> index) {
        return !index || *index <= motions_.size();
    };
    result.on_enter = [this](lib::DragEvent& event) {
        event.selected = readOnly_ ? lib::DropOperation::None
                                   : (event.same_pane() ? event.allowed
                                                        : copyOperation(event.allowed));
    };
    result.on_over = [this](lib::DragEvent& event) {
        if (readOnly_) event.selected = lib::DropOperation::None;
        else if (!event.same_pane()) event.selected = copyOperation(event.allowed);
    };
    result.on_drop = [this](lib::DragEvent& event) {
        // 外部シナリオからのドロップは、qCardMaterialCopy() で確
        // 認ダイアログを開く場合がある。 ボタンを離したら先に転送を終了し、Web/SDL
        // のモーダル画面が主ウィンドウをドラッグ中のままにしないようにする。
        if (!event.same_pane() && event.defer) {
            const auto data = event.data;
            const auto allowed = event.allowed;
            const auto selected = copyOperation(allowed);
            const auto origin = event.origin;
            const auto target_pane_id = event.target_pane_id;
            const auto position = event.position;
            const auto target_index = event.target_index;
            const std::weak_ptr<void> lifetime = lifetimeToken_;
            if (event.defer([this, lifetime, data, allowed, selected, origin,
                             target_pane_id, position, target_index] {
                    if (lifetime.expired()) return lib::DropOperation::None;
                    lib::DragEvent deferred;
                    deferred.data = data;
                    deferred.allowed = allowed;
                    deferred.selected = selected;
                    deferred.origin = origin;
                    deferred.target_pane_id = target_pane_id;
                    deferred.position = position;
                    deferred.target_index = target_index;
                    if (!drop(deferred) || !deferred.handled)
                        return lib::DropOperation::None;
                    return deferred.selected;
                })) {
                event.handled = true;
                event.selected = selected;
                return;
            }
        }
        (void)drop(event);
    };
    return result;
}

void MotionView::bindDragDrop() {
    const std::weak_ptr<void> lifetime = lifetimeToken_;
    const auto backend = prop_.services.drag_drop;
    if (backend && motionsTable_) {
        backend->bind_source(dragSourceBinding());
        if (lifetime.expired()) return;
        backend->bind_target(dropTargetBinding());
        if (lifetime.expired()) return;
    }
    if (!backend || !beastImage_) return;
    lib::DragSourceBinding source;
    source.source = beastImage_;
    source.allowed = lib::DropOperation::Copy | lib::DropOperation::Move;
    source.provide_data = [this, lifetime] {
        lib::DragEvent event;
        if (lifetime.expired()) return event.data;
        event.selected = lib::DropOperation::Copy;
        BeastDragListener(*this).dragStart(event);
        if (event.selected != lib::DropOperation::None)
            BeastDragListener(*this).dragSetData(event);
        return event.data;
    };
    source.provide_origin = [this, lifetime] {
        lib::DragOrigin origin;
        if (!lifetime.expired()) {
            origin.pane_id = id_;
            origin.source_identity = reinterpret_cast<std::uintptr_t>(this);
        }
        return origin;
    };
    backend->bind_source(std::move(source));
    if (lifetime.expired()) return;
    if (readOnly_ || !summary_ || summary_->scenarioPath().empty()) return;
    lib::DropTargetBinding target;
    target.target = beastImage_;
    target.pane_id = id_;
    target.accepted_formats = {String(xmlbytestransfer::XML_BYTES_FORMAT),
                               String(xmlbytestransfer::XML_BYTES_MIME)};
    target.on_enter = [this, lifetime](lib::DragEvent& event) {
        if (lifetime.expired()) { event.selected = lib::DropOperation::None; return; }
        BeastDropListener(*this).dragEnter(event);
    };
    target.on_over = target.on_enter;
    target.on_drop = [this, lifetime](lib::DragEvent& event) {
        if (lifetime.expired()) return;
        if (event.defer) {
            auto deferred = event;
            deferred.defer = {};
            if (event.defer([this, lifetime, deferred]() mutable {
                    if (lifetime.expired()) return lib::DropOperation::None;
                    BeastDropListener(*this).drop(deferred);
                    return deferred.handled ? deferred.selected : lib::DropOperation::None;
                })) { event.handled = true; return; }
        }
        BeastDropListener(*this).drop(event);
    };
    backend->bind_target(std::move(target));
}

void MotionView::unbindDragDrop() {
    const auto backend = prop_.services.drag_drop;
    const auto table = motionsTable_;
    const auto image = beastImage_;
    if (backend && table) backend->unbind(table);
    if (backend && image) backend->unbind(image);
}

bool MotionView::canDoT() const noexcept { return !readOnly_ && canDoC(); }
bool MotionView::canDoC() const noexcept { return selection() != nullptr; }
bool MotionView::canDoP() const noexcept { return !readOnly_ && clipboardAvailable(); }
bool MotionView::canDoD() const noexcept { return canDoT(); }
bool MotionView::canDoClone() const noexcept { return !readOnly_ && canDoC(); }

bool MotionView::canUndo() const noexcept { return undo_ && undo_->canUndo() && !readOnly_; }
bool MotionView::canRedo() const noexcept { return undo_ && undo_->canRedo() && !readOnly_; }

void MotionView::undo() {
    if (!canUndo()) return;
    const std::weak_ptr<void> lifetime = lifetimeToken_;
    undo_->undo();
    if (!lifetime.expired()) notify();
}

void MotionView::redo() {
    if (!canRedo()) return;
    const std::weak_ptr<void> lifetime = lifetimeToken_;
    undo_->redo();
    if (!lifetime.expired()) notify();
}

void MotionView::changed() {
    const std::weak_ptr<void> lifetime = lifetimeToken_;
    const auto modelChangedCopy = modelChanged;
    const auto motionChangedCopy = motionChanged;
    const auto modEventCopy = modEvent;
    modelChangedCopy.call();
    if (lifetime.expired()) return;
    if (const auto value = selection()) motionChangedCopy.call(value);
    if (lifetime.expired()) return;
    modEventCopy.call();
}

void MotionView::refMenu(types::MenuID) {
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void MotionView::refUndoMax() {
    if (undo_) undo_->max(prop_.etc.undoMaxEtc);
}

String MotionView::summSkin() const {
    return summary_ ? summary_->skinName() : String{};
}

bool MotionView::openCWXPath(const StringView value, const bool shellActivate) {
    if (path::cpcategory(value) != "motion") return path::cpempty(value);
    const auto index = path::cpindex(value);
    if (index >= motions_.size() || !select(static_cast<std::ptrdiff_t>(index)))
        return false;
    auto child = path::cpbottom(value);
    if (!path::cphasattr(child, "nofocus") && motionsTable_)
        dutils::forceFocus(motionsTable_, shellActivate, prop_.services);
    if (path::cpempty(child)) {
        if (comm_.refreshToolBar) comm_.refreshToolBar();
        return true;
    }
    const auto beast = selection() ? selection()->beast() : BeastPtr{};
    if (!beast) return false;
    const auto category = path::cpcategory(child);
    if (category == "beastcard") {
        if (path::cpindex(child) != 0U) return false;
    } else if (category == "beastcard:id") {
        if (path::cpindex(child) != static_cast<std::size_t>(beast->id())) return false;
    } else return false;
    child = path::cpbottom(child);
    if (path::cpcategory(child) == "event") {
        const auto target = beast->linkId() && summary_ ? summary_->beast(beast->linkId()) : beast;
        if (!target || !prop_.openUseEventsPath) return false;
        trackBeastEvent(beast);
        const bool opened = prop_.openUseEventsPath(summary_, target, child, shellActivate);
        if (comm_.refreshToolBar) comm_.refreshToolBar();
        return opened;
    }
    if (path::cphasattr(child, "opendialog"))
        return openBeastCardView(child, shellActivate);
    if (comm_.refreshToolBar) comm_.refreshToolBar();
    return true;
}

MotionView::Snapshot MotionView::snapshot() const {
    return snapshot(motions_, selected_);
}

MotionView::Snapshot MotionView::snapshot(const std::vector<MotionPtr>& values,
                                           const std::ptrdiff_t selected) const {
    Snapshot result;
    result.selected = selected;
    result.values.reserve(values.size());
    if (useCounter_) result.references = std::make_shared<SnapshotReferences>();
    for (const auto& value : values) {
        if (!value) continue;
        auto copy = value->dup();
        result.values.push_back(copy);
        if (result.references) {
            // D版の UndoEdit/UndoInsertDelete と同様、保存した画像参
            // 照を従属カウンターに登録し、 素材の改名が未適用の履歴にも反映されるようにする。
            result.references->values.push_back(copy);
            copy->setUseCounter(useCounter_->sub(), ucOwner_);
        }
    }
    return result;
}

void MotionView::restore(const Snapshot& value) {
    // D版の UndoEdit と同様、履歴の対象だけを置き換える。 変更しない行は保持
    // し、無関係なモードレスコメントが元の Motion 所有者を維持できるようにする。
    std::vector<MotionPtr> restored;
    restored.reserve(value.values.size());
    std::vector<bool> retained(motions_.size());
    for (const auto& item : value.values) {
        if (!item) continue;
        std::size_t match = motions_.size();
        for (std::size_t index = 0; index < motions_.size(); ++index) {
            const auto& live = motions_[index];
            if (retained[index] || !live) continue;
            if (live->objectId() == item->objectId() && live->opEquals(*item)) {
                match = index;
                break;
            }
        }
        if (match < motions_.size()) {
            retained[match] = true;
            restored.push_back(motions_[match]);
        } else {
            auto copy = item->dup();
            attachMotion(copy);
            restored.push_back(std::move(copy));
        }
    }
    for (std::size_t index = 0; index < motions_.size(); ++index)
        if (!retained[index]) detachMotion(motions_[index]);
    motions_ = std::move(restored);
    selected_ = value.selected < 0 || value.selected >= static_cast<std::ptrdiff_t>(motions_.size())
        ? (motions_.empty() ? -1 : static_cast<std::ptrdiff_t>(motions_.size()) - 1)
        : value.selected;
    oldIndex_ = -1;
    refreshWidgets();
}

bool MotionView::mutate(const std::function<bool()>& action, const bool useCount,
                        const bool warning) {
    if (readOnly_ || !action) return false;
    const std::weak_ptr<void> lifetime = lifetimeToken_;
    const auto before = snapshot();
    if (!action()) return false;
    if (lifetime.expired()) return true;
    const auto after = snapshot();
    if (undo_) {
        const std::weak_ptr<void> lifetime = lifetimeToken_;
        // D版の MVUndo と同様、取り消し・やり直しのたびに現在の選択を交換
        // する。 挿入側は appendMotion が戻った後で追加行を選ぶため
        // 、 挿入のスナップショットではなく、取り消し時にその最終選択を取得する。
        const auto saved_selection = std::make_shared<std::ptrdiff_t>(before.selected);
        undo_->push(
            [this, lifetime, before, saved_selection] {
                if (lifetime.expired()) return;
                auto target = before;
                target.selected = *saved_selection;
                *saved_selection = selected_;
                restore(target);
                if (!lifetime.expired()) notify();
            },
            [this, lifetime, after, saved_selection] {
                if (lifetime.expired()) return;
                auto target = after;
                target.selected = *saved_selection;
                *saved_selection = selected_;
                restore(target);
                if (!lifetime.expired()) notify();
            });
    }
    if (!lifetime.expired()) notify(warning, useCount);
    return true;
}

void MotionView::beginPendingUndo() {
    if (!pendingUndo_.active) {
        pendingUndo_.before = snapshot();
        pendingUndo_.active = true;
    }
}

void MotionView::commitPendingUndo() {
    if (!pendingUndo_.active) return;
    const auto before = std::exchange(pendingUndo_.before, Snapshot{});
    const auto after = snapshot();
    pendingUndo_.active = false;
    if (undo_) {
        const std::weak_ptr<void> lifetime = lifetimeToken_;
        undo_->push(
            [this, lifetime, before] {
                if (lifetime.expired()) return;
                restore(before);
                if (!lifetime.expired()) notify();
            },
            [this, lifetime, after] {
                if (lifetime.expired()) return;
                restore(after);
                if (!lifetime.expired()) notify();
            });
    }
}

void MotionView::cancelPendingUndo() noexcept {
    pendingUndo_.before = {};
    pendingUndo_.active = false;
}

void MotionView::attachMotion(const MotionPtr& value) {
    if (!value) return;
    if (useCounter_) value->setUseCounter(useCounter_->sub(), ucOwner_);
    value->changeHandler([this] { changed(); });
}

void MotionView::detachMotion(const MotionPtr& value) noexcept {
    if (!value) return;
    value->changeHandler({});
    value->removeUseCounter();
}

void MotionView::resetMaxNest(const MotionPtr& value) {
    if (!value) return;
    if (!value->beast() || value->beast()->linkId() == 0) value->maxNest(Motion::maxNest_init);
    maxNestValue_ = static_cast<std::int32_t>(value->maxNest());
    if (maxNestControl_) maxNestControl_->value = maxNestValue_;
}

std::shared_ptr<card::XMLOption> MotionView::xmlOption() const {
    return std::make_shared<card::XMLOption>(prop_.sys ? prop_.sys : &xmlSystem_,
                                             versioninfo::LATEST_VERSION);
}

system::XMLInfo MotionView::xmlInfo() const {
    return system::XMLInfo(prop_.sys ? *prop_.sys : xmlSystem_,
                           versioninfo::LATEST_VERSION);
}

void MotionView::storeEdit(const std::ptrdiff_t) { beginPendingUndo(); }
void MotionView::storeSwap(const std::ptrdiff_t, const std::ptrdiff_t) { beginPendingUndo(); }
void MotionView::storeMove(const std::ptrdiff_t, const std::ptrdiff_t) { beginPendingUndo(); }
void MotionView::storeInsert(const std::ptrdiff_t) { beginPendingUndo(); }
void MotionView::storeDelete(const std::ptrdiff_t) { beginPendingUndo(); }

void MotionView::DamageTypeListener::widgetSelected(const types::DamageType value) {
    if (!view_) return;
    if (isEditable && !isEditable()) return;
    if (view_->setDamageType(value) && valueSpinner)
        valueSpinner->set_enabled(value != types::DamageType::Max);
}

void MotionView::AbiValListener::widgetSelected(const std::int32_t value) {
    if (view_) (void)view_->setAbilityValue(value);
}

void MotionView::EditBeast::mouseDoubleClick(lib::PointerEvent& event) {
    if (event.button == lib::PointerButton::Primary && view_ &&
        view_->canEditBeast()) {
        if (view_->openBeastCardView()) {
            // 編集ウィンドウを開いた後、ネイティブのキャンバスが同じ
            // イベントを二度目の有効化として解釈しないようにする。
            event.handled = true;
        }
    }
}

void MotionView::EditBeast::mouseDown(lib::PointerEvent&) {
    if (view_ && view_->beastImage_ && view_->beastImage_->on_focus)
        view_->beastImage_->on_focus();
}

void MotionView::EditBeast::mouseUp(lib::PointerEvent& event) {
    if (!view_ || event.button != lib::PointerButton::Primary) return;
    const auto mark = eventTreeMarkRect(event);
    if (!mark || event.position.x < mark->x ||
        event.position.x > mark->x + mark->width ||
        event.position.y < mark->y ||
        event.position.y > mark->y + mark->height)
        return;
    const auto beast = view_->editBeast();
    if (beast && view_->openBeastEventWin(beast)) {
        event.handled = true;
    }
}

void MotionView::EditBeast::mouseMove(lib::PointerEvent& event) {
    const auto bounds = cardBounds(event);
    const auto mark = eventTreeMarkRect(event);
    inBounds_ = bounds.has_value();
    if (view_ && view_->beastImage_) {
        const bool onMark = mark && event.position.x >= mark->x &&
                            event.position.x <= mark->x + mark->width &&
                            event.position.y >= mark->y &&
                            event.position.y <= mark->y + mark->height;
        view_->beastImage_->set_cursor(onMark ? lib::CursorKind::Hand
                                               : lib::CursorKind::Default);
    }
}

std::optional<lib::Rect> MotionView::EditBeast::cardBounds(
    const lib::PointerEvent& event) const {
    if (!view_ || !view_->editBeast()) return std::nullopt;
    const auto drawn = view_->beastDrawBounds();
    if (!drawn) return std::nullopt;
    const auto result = *drawn;
    return event.position.x >= result.x && event.position.x <= result.x + result.width &&
                   event.position.y >= result.y && event.position.y <= result.y + result.height
               ? std::optional<lib::Rect>{result}
               : std::nullopt;
}

std::optional<lib::Rect> MotionView::EditBeast::eventTreeMarkRect(
    const lib::PointerEvent& event) const {
    if (!view_ || !view_->editBeast() || !view_->beastImage_) return std::nullopt;
    const auto props = view_->prop_.editorProps;
    if (!props || !props->var().showEventTreeMark.value) return std::nullopt;
    const auto beast = view_->editBeast(); // リンクカードは解決済み。
    const auto variables = beast->flagDirRoot();
    const bool hasVariables = variables && (variables->hasFlag() || variables->hasStep() || variables->hasVariant());
    const bool hasEvents = props->var().ignoreEmptyStart.value ? !beast->isEmpty() : !beast->trees().empty();
    if (!props->var().showEventTreeMarkAlways.value && !hasVariables && !hasEvents) return std::nullopt;
    const auto card = view_->beastDrawBounds();
    if (!card) return std::nullopt;
    // イベントツリーの印はカード左上に描く。 呼び出し元はそ
    // の点でのボタン解放を判定するために印の境界を必要とする
    // ので、 矩形の作成にその点を使わないのは意図した動作。
    (void)event;
    const auto mark = dutils::eventTreeMarkRect(
        view_->prop_.editorProps, true, static_cast<int>(std::lround(card->x)),
        static_cast<int>(std::lround(card->y)), view_->summary_,
        *view_->editBeast());
    return lib::Rect{mark.x, mark.y, std::min(mark.width, card->width),
                     std::min(mark.height, card->height)};
}

void MotionView::EditBeast::keyReleased(lib::KeyEvent&) {}

void MotionView::EditBeast::keyPressed(lib::KeyEvent& event) {
    if (event.key == lib::Key::Enter && view_ && view_->canEditBeast()) {
        (void)view_->openBeastCardView();
        event.handled = true;
    }
}

void MotionView::ParamPaneChange::widgetSelected() {
    if (view_) (void)view_->refreshSels(true);
}

void MotionView::SetBeast::widgetSelected() {
    if (view_) (void)view_->setBeast(view_->selectedBeast_);
}

void MotionView::PaintBeast::paintControl(lib::DrawingContext& context,
                                          const lib::Rect bounds) {
    if (!view_) return;
    view_->updateBeastToolTip();
    if (view_->beastPreviewResource_.id() == 0U ||
        view_->beastPreviewSize_.width <= 0.0F ||
        view_->beastPreviewSize_.height <= 0.0F) return;
    const auto drawn = view_->beastDrawBounds();
    if (!drawn) return;
    const auto destination = *drawn;
    context.draw_image(view_->beastPreviewResource_.id(),
                       {0.0F, 0.0F, view_->beastPreviewSize_.width,
                        view_->beastPreviewSize_.height},
                       destination, lib::Interpolation::Best);
    // 描画とポインターの当たり判定で、同じ表示条件を使う。
    const auto marker = EditBeast(*view_).eventTreeMarkRect(lib::PointerEvent{});
    if (const auto beast = view_->editBeast(); beast && marker)
        dutils::putEventTree(view_->beastImage_, context,
                             view_->prop_.editorProps, view_->summary_, true,
                             *beast, *marker);
}

void MotionView::MDropListener::dragEnter(lib::DragEvent& event) {
    if (view_) event.selected = view_->readOnly_ ? lib::DropOperation::None : event.allowed;
}

void MotionView::MDropListener::dragOver(lib::DragEvent& event) { dragEnter(event); }
void MotionView::MDropListener::drop(lib::DragEvent& event) {
    if (view_) (void)view_->drop(event);
}

void MotionView::MDragListener::dragStart(lib::DragEvent& event) {
    if (!view_ || view_->readOnly_ || !view_->selection()) {
        event.selected = lib::DropOperation::None;
        return;
    }
    item_ = view_->selectionIndex();
    view_->beginDrag();
}

void MotionView::MDragListener::dragSetData(lib::DragEvent& event) {
    if (view_) event.data = view_->dragData();
}

void MotionView::MDragListener::dragFinished(const lib::DropOperation operation) {
    if (view_ && hasOperation(operation, lib::DropOperation::Move) && item_ >= 0 &&
        view_->dragSource_ >= 0)
        (void)view_->removeMotion(item_, true, true, true);
    item_ = -1;
}

void MotionView::SelElement::widgetSelected(const std::size_t elementIndex) {
    if (!view_ || elementIndex > static_cast<std::size_t>(types::Element::Ice)) return;
    (void)view_->setElement(static_cast<types::Element>(elementIndex));
}

void MotionView::Dispose::widgetDisposed() {
    if (view_) view_->disposed_ = true;
}

void MotionView::KeyDownFilter::handleEvent(lib::KeyEvent& event) {
    if (!view_ || event.handled || view_->readOnly_) return;
    const auto matches = [&](types::MenuID id, StringView fallback) {
        // 古いキー割り当てを保持せず、現在のメニューテキストを取得する。
        const auto menu = view_->prop_.editorProps
            ? view_->prop_.editorProps->buildMenu(id) : String(fallback);
        std::uint32_t character{};
        if (!event.printable.empty()) character = static_cast<unsigned char>(event.printable.front());
        else if (event.key >= lib::Key::A && event.key <= lib::Key::Z)
            character = 'A' + static_cast<int>(event.key) - static_cast<int>(lib::Key::A);
        else if (event.key >= lib::Key::Digit0 && event.key <= lib::Key::Digit9)
            character = '0' + static_cast<int>(event.key) - static_cast<int>(lib::Key::Digit0);
        const auto accelerator = static_cast<int>(dmenu::convertAccelerator(menu));
        return accelerator && dmenu::eqAcc(accelerator, static_cast<int>(event.key),
            character, static_cast<std::uint32_t>(event.modifiers));
    };
    if (matches(types::MenuID::Undo, "Undo\tCtrl+Z")) {
        view_->undo(); event.handled = true;
    } else if (matches(types::MenuID::Redo, "Redo\tCtrl+Y")) {
        view_->redo(); event.handled = true;
    }
}

void MotionView::BeastDragListener::dragStart(lib::DragEvent& event) {
    const auto value = view_ && view_->selection() ? view_->selection()->beast() : BeastPtr{};
    if (!value || !view_->summary_) event.selected = lib::DropOperation::None;
}

void MotionView::BeastDragListener::dragSetData(lib::DragEvent& event) {
    const auto value = view_ && view_->selection() ? view_->selection()->beast() : BeastPtr{};
    if (!view_ || !value) return;
    auto node = xml::XNode::create(BeastCard::XML_NAME_M);
    node.newAttr("summId", view_->summary_ ? view_->summary_->id() : String{});
    node.newAttr("topLevel", false);
    node.newAttr("paneId", view_->id_);
    node.newAttr("scenarioPath", view_->summary_ ? view_->summary_->scenarioPath() : String{});
    node.newAttr("scenarioName", view_->summary_ ? view_->summary_->scenarioName() : String{});
    node.newAttr("scenarioAuthor", view_->summary_ ? view_->summary_->author() : String{});
    (void)value->toNode(node, *view_->xmlOption());
    event.data = xmlbytestransfer::makeXMLPayload(node.text());
}

void MotionView::BeastDropListener::dragEnter(lib::DragEvent& event) {
    if (view_) event.selected = view_->readOnly_ || !view_->summary_ ||
        view_->summary_->scenarioPath().empty()
        ? lib::DropOperation::None : lib::DropOperation::Copy;
}

void MotionView::BeastDropListener::dragOver(lib::DragEvent& event) { dragEnter(event); }

void MotionView::BeastDropListener::drop(lib::DragEvent& event) {
    event.selected = lib::DropOperation::None;
    event.handled = false;
    if (!view_ || view_->readOnly_ || !view_->summary_ ||
        view_->summary_->scenarioPath().empty() || !view_->selection()) return;
    auto xml = xmlbytestransfer::CBtoXML(event.data);
    if (xml.empty()) xml = event.data.text;
    const auto payload = view_->parseClipboard(xml);
    if (payload && payload->node.attr("paneId", false) == view_->id_) return;
    if (payload && payload->kind == "beast" &&
        view_->beginExternalMaterialDrop(*payload, std::nullopt)) {
        event.selected = lib::DropOperation::Copy;
        event.handled = true;
    } else if (payload && payload->kind == "beast" &&
               view_->pasteBeast(*payload)) {
        event.selected = lib::DropOperation::Copy;
        event.handled = true;
    }
}

void MotionView::MotionTCPD::cut(SelectionEvent& event) {
    event.handled=view_ && view_->cut();
}
void MotionView::MotionTCPD::copy(SelectionEvent& event) {
    event.handled=view_ && view_->copy();
}
void MotionView::MotionTCPD::paste(SelectionEvent& event) {
    if (!view_ || view_->readOnly_) return;
    const auto data = view_->readClipboard();
    auto xml = xmlbytestransfer::CBtoXML(data);
    if (xml.empty()) xml = data.text;
    const auto payload = view_->parseClipboard(xml);
    event.handled = payload && (payload->kind == "motion"
        ? view_->pasteMotion(*payload) : view_->pasteBeast(*payload));
}
void MotionView::MotionTCPD::del(SelectionEvent& event) {
    event.handled=view_ && view_->removeMotion();
}
void MotionView::MotionTCPD::clone(SelectionEvent& event) {
    event.handled=view_ && view_->clone();
}
bool MotionView::MotionTCPD::canDoTCPD() const noexcept { return view_ && view_->canDoTCPD(); }
bool MotionView::MotionTCPD::canDoT() const noexcept { return view_ && view_->canDoT(); }
bool MotionView::MotionTCPD::canDoC() const noexcept { return view_ && view_->canDoC(); }
bool MotionView::MotionTCPD::canDoP() const noexcept {
    if (!view_ || view_->readOnly_) return false;
    try {
        const auto data = view_->readClipboard();
        auto xml = xmlbytestransfer::CBtoXML(data);
        if (xml.empty()) xml = data.text;
        const auto payload = view_->parseClipboard(xml);
        return payload.has_value();
    } catch (...) {
        return false;
    }
}
bool MotionView::MotionTCPD::canDoD() const noexcept { return view_ && view_->canDoD(); }
bool MotionView::MotionTCPD::canDoClone() const noexcept { return view_ && view_->canDoClone(); }

void MotionView::BeastTCPD::cut(SelectionEvent& event) {
    if (!view_ || !view_->selection() || !view_->selection()->beast()) return;
    copy(event);
    if (event.handled) {
        (void)view_->setBeast({});
    }
}

void MotionView::BeastTCPD::copy(SelectionEvent& event) {
    if (!view_) return;
    const auto value = view_->selection() ? view_->selection()->beast() : BeastPtr{};
    if (!value) return;
    auto node = xml::XNode::create(BeastCard::XML_NAME_M);
    if (view_->summary_) {
        node.newAttr("summId", view_->summary_->id());
        node.newAttr("paneId", String{});
        node.newAttr("scenarioPath", utils::nabs(StringView{view_->summary_->scenarioPath()}));
    }
    // 埋め込みのコピーは最上位カードを生成しないため、topLevel を省略する。
    (void)value->toNode(node, *view_->xmlOption());
    const auto xml = node.text();
    auto data = view_->prop_.xmlCopy()
        ? xmlbytestransfer::makeTextPayload(xml)
        : xmlbytestransfer::makeXMLPayload(xml);
    view_->writeClipboard(data);
    event.handled = true;
}

void MotionView::BeastTCPD::paste(SelectionEvent& event) {
    if (!view_ || view_->readOnly_) return;
    const auto data = view_->readClipboard();
    auto xml = xmlbytestransfer::CBtoXML(data);
    if (xml.empty()) xml = data.text;
    const auto payload = view_->parseClipboard(xml);
    event.handled = payload && payload->kind == "beast" &&
                    view_->pasteBeast(*payload);
}

void MotionView::BeastTCPD::del(SelectionEvent& event) {
    if (view_ && view_->selection() && view_->selection()->beast()) {
        (void)view_->setBeast({});
        event.handled = true;
    }
}

void MotionView::BeastTCPD::clone(SelectionEvent& event) {
    if (view_) {
        const auto value = view_->selection() ? view_->selection()->beast() : BeastPtr{};
        if (value) {
            (void)view_->setBeast(value);
            event.handled = true;
        }
    }
}

bool MotionView::BeastTCPD::canDoTCPD() const noexcept { return view_ && view_->canDoTCPD(); }
bool MotionView::BeastTCPD::canDoT() const noexcept { return view_ && !view_->readOnly_ && view_->selection() && view_->selection()->beast(); }
bool MotionView::BeastTCPD::canDoC() const noexcept { return view_ && view_->selection() && view_->selection()->beast(); }
bool MotionView::BeastTCPD::canDoP() const noexcept {
    if (!view_ || view_->readOnly_) return false;
    try {
        const auto data = view_->readClipboard();
        auto xml = xmlbytestransfer::CBtoXML(data);
        if (xml.empty()) xml = data.text;
        const auto payload = view_->parseClipboard(xml);
        return payload && payload->kind == "beast";
    } catch (...) {
        return false;
    }
}
bool MotionView::BeastTCPD::canDoD() const noexcept { return canDoT(); }
bool MotionView::BeastTCPD::canDoClone() const noexcept { return false; }

void bindUseEventHost(Props& props, commons::Commons* common, dprops::Props* editorProps) {
    using Panel = commons::TopLevelPanel;
    using Entry = std::pair<const card::BeastCard*, std::weak_ptr<Panel>>;
    const auto panels = std::make_shared<std::vector<Entry>>();
    const auto alive = common ? common->lifetimeToken() : std::weak_ptr<const int>{};
    if (editorProps) {
        props.commentSizeBinder = [editorProps, alive](const std::shared_ptr<lib::Window>& window) -> std::function<void()> {
            return alive.expired() ? std::function<void()>{} : editorProps->bindDialogSize(window, editorProps->flex().commentDlg);
        };
        const bool initial = editorProps->var().xmlCopy.value;
        props.etc.xmlCopy = initial;
        // D版と同様、コピーのたびに現在の設定を読む。 開いている効果
        // エディタと、内部の召喚獣のクリップボード操作も対象にする。
        props.xmlCopyProvider = [editorProps, alive, initial] {
            return alive.expired() ? initial : editorProps->var().xmlCopy.value;
        };
    }
    const auto open = [common, editorProps, alive, panels](Summary* summary,
                        const BeastPtr& beast, bool activate) -> std::shared_ptr<Panel> {
        if (alive.expired() || !editorProps || !beast) return {};
        const auto panel = common->openUseEvents(*editorProps, summary, beast, activate);
        if (panel && std::none_of(panels->begin(), panels->end(), [&](const Entry& entry) {
                return entry.first == beast.get() && entry.second.lock() == panel;
            })) panels->emplace_back(beast.get(), panel);
        return panel;
    };
    props.openUseEvents = [open](Summary* summary, const BeastPtr& beast, bool activate) {
        return static_cast<bool>(open(summary, beast, activate));
    };
    props.openUseEventsPath = [open](Summary* summary, const BeastPtr& beast,
                                    StringView path, bool activate) {
        const auto panel = open(summary, beast, activate);
        return panel && panel->openCWXPath(String(path), activate);
    };
    props.closeUseEvents = [common, alive, panels](const BeastPtr& beast) {
        if (alive.expired()) return;
        std::vector<std::shared_ptr<Panel>> closing;
        for (auto it = panels->begin(); it != panels->end();) {
            if (it->first == beast.get()) {
                if (const auto panel = it->second.lock()) closing.push_back(panel);
                it = panels->erase(it);
            } else ++it;
        }
        if (beast && !beast->linkId()) {
            const auto owner = beast->cwxParent().lock();
            // Dlg::call は購読先を複製してから呼び出す。
            common->delBeast.call(owner, beast);
            if (alive.expired()) return;
        }
        for (const auto& panel : closing) {
            if (alive.expired()) return;
            common->close(panel->shell());
        }
    };
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::motionview
