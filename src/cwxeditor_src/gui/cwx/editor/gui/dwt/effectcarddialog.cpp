#include "cwxeditor_src/gui/cwx/editor/gui/dwt/effectcarddialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/summaryskinbinding.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cwxeditor_src/gui/cwx/editor/gui/dwt/cardpropertytransaction.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/lib/audio.hpp"
#include "cwxeditor_src/lib/dialogs.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <exception>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::effectcarddialog {
namespace dutils = ::cwxeditor::cwx::editor::gui::dwt::dutils;

namespace {
// 保存したカードダイアログのサイズは、Dのタブ・クライアントサ
// イズ。開くときにウィンドウがこの余白を足すため、close()
// で再び引く。そうしないと開閉のたびに余白分だけ大きくなる。
constexpr float kEffectCardSidePadding = 5.0F;
constexpr float kEffectCardBottomPadding = 6.0F;
// ALLのItemダイアログに追加する幅は、九つの見出し
// の左右余白36pxと追加余白90pxの合計126px。
constexpr float kAllItemTabExtra = 9.0F * 2.0F * 2.0F + 70.0F + 20.0F;

template <typename T>
void setEditable(const std::shared_ptr<T>& widget, bool enabled) {
    if (!widget) return;
    widget->set_enabled(enabled);
    if constexpr (std::is_same_v<T, lib::TextEdit>)
        widget->read_only = !enabled;
    else if constexpr (std::is_same_v<T, lib::NumberEdit>)
        widget->read_only = !enabled;
}

void setNumberDisplayValue(const std::shared_ptr<lib::NumberEdit>& control,
                           const double raw_value) {
    if (!control) return;
    control->value = lib::editor_window_host_options().all_mode
                         ? raw_value
                         : std::clamp(raw_value, control->minimum, control->maximum);
}

template <typename E>
String enumLabel(StringView prefix, E value) {
    return String(prefix) + " " + std::to_string(static_cast<int>(value));
}

template <typename E, std::size_t N>
void setRadioValue(const std::array<RadioPtr, N>& buttons,
                   const std::array<E, N>& values, E value) {
    for (std::size_t i = 0; i < N; ++i)
        if (buttons[i]) buttons[i]->checked = values[i] == value;
}

template <typename E, std::size_t N>
E radioValue(const std::array<RadioPtr, N>& buttons,
             const std::array<E, N>& values, E fallback) {
    for (std::size_t i = 0; i < N; ++i)
        if (buttons[i] && buttons[i]->checked) return values[i];
    return fallback;
}

void verticalLayout(const std::shared_ptr<lib::Container>& container,
                    const float spacing = 5.0F,
                    const lib::Insets padding = {}) {
    if (!container) return;
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    layout->spacing = spacing;
    layout->padding = padding;
    container->set_layout(std::move(layout));
}

void fillPage(const std::shared_ptr<lib::Container>& page) {
    if (!page) return;
    page->set_layout(std::make_shared<lib::FillLayout>());
    page->set_layout_item({.horizontal = lib::Align::Stretch,
                           .vertical = lib::Align::Stretch,
                           .grow = 1.0F});
}

// ALL仕様: スキル(特殊技能)カードは横幅30%減、アイテム/召喚獣カードは25%減。
// authored の幅(既定ウィンドウ幅と各枠の最小幅)にこの係数を掛ける。文字や
// 子部品の必要幅は共通の計測が別途保証するので、切れは生じない。
template <typename C>
[[nodiscard]] float allWidth(const float value) noexcept {
    if (!lib::editor_window_host_options().all_mode) return value;
    const float factor = std::is_same_v<C, card::SkillCard> ? 0.70F : 0.75F;
    return std::round(value * factor);
}

float completeTabStripLogicalWidth(const lib::TabView& tabs,
                                   const float outer_padding) {
    const float scale = std::max(0.01F, lib::editor_ui_text_scale());
    const auto font = tabs.font();
    float physical_width = 2.0F * lib::scaled_ui_metric(
        lib::resolved_tab_horizontal_padding(tabs.chrome_inset));
    for (const auto& page : tabs.pages)
        physical_width += lib::preferred_tab_width(
            page, font ? &*font : nullptr, tabs.header_horizontal_padding);
    return std::ceil(physical_width / scale + outer_padding);
}

template <typename C>
String cardTitle(const C& value, const props::CProps* prop) {
    if (prop) {
        const auto name_with_id = ::cwxeditor::cwx::utils::tryFormat(
            prop->requiredMsgs().nameWithID.value, value.id(), value.name());
        if constexpr (std::is_same_v<C, card::SkillCard>) {
            return ::cwxeditor::cwx::utils::tryFormat(
                prop->requiredMsgs().dlgTitSkill.value, name_with_id);
        } else if constexpr (std::is_same_v<C, card::ItemCard>) {
            return ::cwxeditor::cwx::utils::tryFormat(
                prop->requiredMsgs().dlgTitItem.value, name_with_id);
        } else {
            return ::cwxeditor::cwx::utils::tryFormat(
                prop->requiredMsgs().dlgTitBeast.value, name_with_id);
        }
    }
    if constexpr (std::is_same_v<C, card::SkillCard>) {
        return "Skill " + std::to_string(value.id()) + " " + value.name();
    } else if constexpr (std::is_same_v<C, card::ItemCard>) {
        return "Item " + std::to_string(value.id()) + " " + value.name();
    } else {
        return "Beast " + std::to_string(value.id()) + " " + value.name();
    }
}

template <typename C>
lib::Array<MotionPtr> duplicateMotions(const C& value) {
    lib::Array<MotionPtr> result;
    for (const auto& item : value.motions())
        if (item) result.push_back(item->dup());
    return result;
}

template <typename C>
std::uint64_t newCardId(const SummaryPtr& summary) {
    return summary ? summary->template newId<C>() : 0U;
}

}  // namespace

void SoundSelect::setPath(String value) {
    path = std::move(value);
    filePath = path;
    if (selector && !syncing_) {
        syncing_ = true;
        selector->path2(path, true, true, -1, false);
        syncing_ = false;
    }
    syncPathControl();
    notifyModified();
}

void SoundSelect::syncPathControl() {
    if (!pathControl) return;
    // 素材ファイルのComboは、すでに選択部の共通の選択表示。
    // 選択行はMaterialSelectのパスモデルに従う。
    if (selector && selector->fileCombo() == pathControl) return;
    const auto found = std::find_if(pathControl->items.begin(), pathControl->items.end(),
                                    [this](const lib::ChoiceItem& item) {
                                        // DのMaterialSelectはOSのファイル名
                                        // 比較を使う。ALLの既存の完全一致規則は維持する。
                                        return lib::editor_window_host_options().all_mode
                                            ? item.value == path
                                            : ::cwxeditor::cwx::utils::cfnmatch(item.value, path);
                                    });
    if (found != pathControl->items.end()) {
        (void)lib::select_combo_item(
            *pathControl,
            static_cast<std::size_t>(found - pathControl->items.begin()), false);
    } else {
        (void)lib::select_combo_item(*pathControl, std::nullopt, false);
        pathControl->edit_text = path;
    }
}

void SoundSelect::refreshPathChoices(String noneLabel) {
    if (!pathControl || !selector) return;
    if (selector->fileCombo() == pathControl) return;
    std::vector<lib::ChoiceItem> choices;
    choices.push_back({std::move(noneLabel), {}, {}, true});
    const auto names = selector->showingNames();
    const auto paths = selector->showingPaths();
    choices.reserve(names.size() + 1U);
    for (std::size_t index = 0; index < names.size(); ++index) {
        choices.push_back({names[index], index < paths.size() ? paths[index] : names[index],
                           {}, true});
    }
    std::optional<std::size_t> selected;
    for (std::size_t index = 0; index < choices.size(); ++index) {
        if (lib::editor_window_host_options().all_mode
                ? choices[index].value == path
                : ::cwxeditor::cwx::utils::cfnmatch(choices[index].value, path)) {
            selected = index;
            break;
        }
    }
    const String display = selected ? choices[*selected].text : path;
    (void)lib::replace_combo_items(pathControl, std::move(choices), selected, display);
}

void SoundSelect::setEnabled(const bool value) {
    enabled = value;
    if (selector) selector->enabled(value);
    if (pathControl) pathControl->set_enabled(value && !readOnly);
    if (!selector) {
        if (volumeControl) volumeControl->set_enabled(value && !readOnly && !path.empty());
        if (loopControl) loopControl->set_enabled(value && !readOnly && !path.empty());
    }
}

void SoundSelect::syncControls() {
    syncing_ = true;
    if (selector) {
        selector->path2(path, true, true, -1, false);
        selector->volume(static_cast<int>(volume));
        selector->loopCount(static_cast<int>(loopCount));
        selector->enabled(enabled);
    }
    syncPathControl();
    if (!selector) {
        if (volumeControl) volumeControl->value = static_cast<double>(volume);
        if (loopControl) loopControl->value = static_cast<double>(loopCount);
    }
    syncing_ = false;
    setEnabled(enabled);
}

void SoundSelect::syncFromControls() {
    if (selector) {
        path = selector->path();
        filePath = selector->filePath();
        selectedDefDir = selector->selectedDefDir();
        warnings = selector->warnings();
        volume = static_cast<std::uint32_t>(std::max(0, selector->volume()));
        loopCount = static_cast<std::uint32_t>(std::max(1, selector->loopCount()));
    }
    if (volumeControl)
        volume = static_cast<std::uint32_t>(std::clamp(
            static_cast<int>(std::lround(volumeControl->value)), 0, 100));
    if (loopControl)
        loopCount = static_cast<std::uint32_t>(std::max(
            1, static_cast<int>(std::lround(loopControl->value))));
}

void SoundSelect::notifyModified() {
    const auto callbacks = modEvent;
    for (const auto& callback : callbacks)
        if (callback) callback();
}

void SoundSelect::notifyLoaded() {
    const auto callbacks = loadedEvent;
    for (const auto& callback : callbacks)
        if (callback) callback();
}

ImageSelectState::MaterialPath ImageSelectState::materialPath(
    const bool forceApplying) const {
    if (materializer) return materializer(forceApplying);
    return {images, false};
}

MotionEditor::MotionEditor(const Context& context, SummaryPtr summary,
                           CWXPathPtr owner, UseCounterPtr useCounter,
                           const bool readOnly)
    : read_only_(readOnly) {
    set_layout(std::make_shared<lib::FillLayout>());
    host_ = std::make_shared<lib::Panel>();
    host_->set_layout(std::make_shared<lib::FillLayout>());
    add(host_);

    fullmotion::Commons commons;
    commons.services = context.services;
    commons.useCounter = useCounter;
    commons.refreshToolBar = context.refreshToolBar;
    fullmotion::Props props;
    props.services = context.services;
    props.editorProps = context.editor_props;
    preview_skin_ = context.findSkin(summary.get());
    props.skin = preview_skin_.get();
    if (context.prop) {
        const auto& messages = context.prop->requiredMsgs();
        props.sys = &context.prop->sys();
        props.msgs.motionKind = messages.motionKind.value;
        props.msgs.motionElement = messages.motionElement.value;
        props.msgs.motionBeast = messages.motionBeast.value;
        props.msgs.motionRound = messages.motionRound.value;
        props.msgs.motionEnhValue = messages.motionEnhValue.value;
        props.msgs.motionDamageType = messages.motionDamageType.value;
        props.msgs.motionValue = messages.motionValue.value;
        props.msgs.beastNone = messages.beastNone.value;
        props.msgs.setBeast = messages.setBeast.value;
        props.msgs.cardIsReference = messages.cardIsReference.value;
        props.msgs.referencedCardIsNotFound = messages.referencedCardIsNotFound.value;
        props.msgs.warningUnknownMotion = messages.warningUnknownMotion.value;
        props.msgs.warningUnknownMotionWsn = messages.warningUnknownMotionWsn.value;
        props.msgs.searchErrorLinkIdBeastNotFound = messages.searchErrorLinkIdBeastNotFound.value;
        props.msgs.warningSkillPowerWithFixedValue = messages.warningSkillPowerWithFixedValue.value;
        props.msgs.groupVitality = messages.msnGroupVitality.value;
        props.msgs.groupPhysical = messages.msnGroupPhysical.value;
        props.msgs.groupSkill = messages.msnGroupSkill.value;
        props.msgs.groupMental = messages.msnGroupMental.value;
        props.msgs.groupMagic = messages.msnGroupMagic.value;
        props.msgs.groupEnhance = messages.msnGroupEnhance.value;
        props.msgs.groupVanish = messages.msnGroupVanish.value;
        props.msgs.groupCard = messages.msnGroupCard.value;
        props.msgs.groupBeast = messages.msnGroupBeast.value;
        props.msgs.groupEtc = messages.msnGroupEtc.value;
        props.msgs.undo = messages.menuTextUndo.value;
        props.msgs.redo = messages.menuTextRedo.value;
        props.msgs.deleteMotion = messages.msnDelete.value;
        props.msgs.up = "上へ";
        props.msgs.down = "下へ";
        props.msgs.newMotion = messages.motion.value;
        props.msgs.motionRowFormat = messages.msnDesc.value;
        for (const auto& [type, detail] : motion::MOTION_DETAILS()) {
            (void)detail;
            props.msgs.names[type] = messages.motionName(type);
            props.msgs.descriptions[type] = messages.motionDesc(type);
        }
    }
    if (context.editor_props) {
        auto& images = context.editor_props->images();
        props.motionIcon = [&images](const types::MType type) {
            return images.motion(type);
        };
        const auto element_cache = std::make_shared<dprops::ElementImageCache>();
        const auto owner_type = [context, summary] {
            const auto skin = context.findSkin(summary.get());
            return skin ? skin->type() : String{};
        };
        props.elementIcon = [editor = context.editor_props, owner_type, element_cache](const types::Element element) {
            return editor->elementImage(editor->elementOverrides(owner_type()), element, *element_cache);
        };
        props.elementText = [editor = context.editor_props, owner_type](const types::Element element) {
            const auto overrides = editor->elementOverrides(owner_type());
            const auto found = overrides.find(element);
            return found != overrides.end() && !found->second.name.empty() ? found->second.name
                : editor->requiredMsgs().elementName(element);
        };
        props.elementTooltip = [editor = context.editor_props, owner_type](const types::Element element) -> String {
            const auto& messages = editor->requiredMsgs();
            if (element == types::Element::All) return messages.elementEffectiveAll.value;
            if (element == types::Element::Fire || element == types::Element::Ice)
                return messages.elementWeaknessOrResist.value;
            auto target = element == types::Element::Health ? messages.undead.value
                : element == types::Element::Mind ? messages.automaton.value
                : element == types::Element::Miracle ? messages.unholy.value : messages.constructure.value;
            const auto overrides = editor->elementOverrides(owner_type());
            if (const auto found = overrides.find(element);
                found != overrides.end() && !found->second.targetType.empty()) target = found->second.targetType;
            return ::cwxeditor::cwx::utils::tryFormat(
                element == types::Element::Health || element == types::Element::Mind
                    ? messages.elementNoEffective.value : messages.elementEffective.value, target);
        };
        props.deleteIcon = images.msnDelete();
        props.upIcon = images.menu(types::MenuID::Up);
        props.downIcon = images.menu(types::MenuID::Down);
    }
    if (context.vars) {
        props.etc.undoMaxEtc = static_cast<std::size_t>(
            std::max(0, context.vars->undoMaxEtc.value));
        props.etc.motionsWidth = static_cast<std::size_t>(
            std::max(1, context.vars->motionsWidth.value));
        props.etc.comboVisibleItemCount = static_cast<std::size_t>(
            std::max(1, context.vars->comboVisibleItemCount.value));
        props.etc.roundMax = std::max<std::uint32_t>(
            1U, context.vars->roundMax.value);
        props.etc.uValueMax = std::max<std::uint32_t>(
            1U, context.vars->uValueMax.value);
        props.etc.skillPowerMax = std::max<std::uint32_t>(
            1U, context.vars->skillPowerMax.value);
        props.etc.beastMaxNest = std::max<std::uint32_t>(
            1U, context.vars->beastMaxNest.value);
        props.etc.showMotionDescription =
            context.vars->showMotionDescription.value;
    }
    if (context.prop)
        props.looks.motionRoundDefault = static_cast<std::int32_t>(
            context.prop->looks().motionRoundDefault());
    fullmotion::bindUseEventHost(props, context.editor_commons, context.editor_props);
    if (context.vars) {
        props.etc.linkCard = context.vars->linkCard.value;
        props.linkCardProvider = [vars = context.vars] { return vars->linkCard.value; };
    }
    props.openBeastCard = [&context](
        summary::Summary* source, const lib::Owner<path::CWXPath>& motion_owner,
        const fullmotion::BeastPtr& card, const bool child_read_only,
        const StringView nested_path, const bool shell_activate,
        lib::Action applying, lib::Action applied, lib::Action closed) {
        return context.editBeastCard && context.editBeastCard(
            source, motion_owner, card, child_read_only, nested_path, shell_activate,
            std::move(applying), std::move(applied), std::move(closed));
    };
    if (context.editor_commons) {
        auto* host = context.editor_commons;
        const auto host_lifetime = host->lifetimeToken();
        props.openFilePath = [host, host_lifetime](const StringView file,
                                                  const bool activate, const bool select) {
            return !host_lifetime.expired() &&
                host->openFilePath(String{file}, activate, select);
        };
    }
    // DのCommonsはカテゴリへの移動をMainWindowへ委ねる。
    // 下位のペイン登録だけでは、元のBeast一覧を開いて選択できない。
    props.openCWXPath = [open=context.openCWXPath, host=context.editor_commons,
        life=context.editor_commons ? context.editor_commons->lifetimeToken() : std::weak_ptr<const int>{}](
        const StringView value, const bool activate) {
        if (open) return open(value, activate);
        return host && !life.expired() && host->openCWXPath(String(value), activate);
    };
    view_ = std::make_unique<fullmotion::MotionView>(
        std::move(commons), std::move(props), std::move(summary),
        std::move(owner), std::move(useCounter), host_,
        readOnly ? fullmotion::MotionView::ReadOnlyStyle : 0, nullptr);
    warning_connection_ = view_->warningEvent.connect([this] {
        refreshWarnings();
    });
    model_connection_ = view_->modelChanged.connect([this] {
        refreshWarnings();
        const auto callbacks = modEvent;
        for (const auto& callback : callbacks) if (callback) callback();
    });
    if (context.editor_commons && view_->summary() &&
        !view_->summary()->scenarioPath().empty()) {
        candidate_commons_ = context.editor_commons;
        candidate_commons_lifetime_ = candidate_commons_->lifetimeToken();
        const std::weak_ptr<int> life = candidate_lifetime_;
        candidate_ref_connection_ = candidate_commons_->refBeast.add(
            [this, life](const fullmotion::BeastPtr& beast) {
                if (!life.expired() && view_) view_->refBeast(beast);
            });
        candidate_del_connection_ = candidate_commons_->delBeast.add(
            [this, life](const CWXPathPtr& owner, const fullmotion::BeastPtr& beast) {
                if (!life.expired() && view_) view_->delBeast(owner.get(), beast);
            });
    }
    setReadOnly(readOnly);
}

void MotionEditor::retireCandidateSubscriptions() noexcept {
    candidate_lifetime_.reset();
    if (candidate_commons_ && !candidate_commons_lifetime_.expired()) {
        candidate_commons_->refBeast.remove(candidate_ref_connection_);
        candidate_commons_->delBeast.remove(candidate_del_connection_);
    }
    candidate_commons_ = nullptr;
    candidate_ref_connection_ = candidate_del_connection_ = 0;
}

void MotionEditor::refSkin(SkinPtr skin) {
    // MotionViewがポインターを置き換えるまで、以前に借用したスキンを生存させる。
    const auto previous = std::move(preview_skin_);
    preview_skin_ = std::move(skin);
    if (view_) view_->refSkin(preview_skin_.get());
}

MotionEditor::~MotionEditor() {
    retireCandidateSubscriptions();
    if (view_) {
        view_->warningEvent.remove(warning_connection_);
        view_->modelChanged.remove(model_connection_);
    }
}

void MotionEditor::setReadOnly(const bool value) noexcept {
    read_only_ = value;
    set_enabled(true);
    if (view_) view_->setReadOnly(value);
}

void MotionEditor::setMotions(lib::Array<MotionPtr> value) {
    if (view_) view_->motions(std::move(value));
    refreshWarnings();
}

lib::Array<MotionPtr> MotionEditor::motions() const {
    return view_ ? view_->motions() : lib::Array<MotionPtr>{};
}

void MotionEditor::closeAuxiliaryEditors() {
    retireCandidateSubscriptions();
    if (view_) view_->closeAuxiliaryEditors();
}

void MotionEditor::setPathOpener(PathOpener value) {
    path_opener_ = std::move(value);
}

void MotionEditor::refreshWarnings() {
    warnings_.clear();
    if (view_) warnings_ = view_->warnings();
    const auto callbacks = warningEvent;
    for (const auto& callback : callbacks)
        if (callback) callback();
}

void MotionEditor::refDataVersion() {
    if (view_) view_->refEnabled();
}

bool MotionEditor::openCWXPath(const StringView value,
                               const bool activate) const {
    if (path_opener_ && path_opener_(value, activate)) return true;
    return view_ && view_->openCWXPath(value, activate);
}

template <typename C>
typename EffectCardDialog<C>::CardPtr
EffectCardDialog<C>::duplicateCard(const CardPtr& value) {
    if (!value) return {};
    if constexpr (std::is_same_v<C, card::SkillCard>) {
        return value->dupSkill();
    } else if constexpr (std::is_same_v<C, card::ItemCard>) {
        return value->dupItem();
    } else {
        return value->dupBeast();
    }
}

template <typename C>
void EffectCardDialog<C>::connectCommonsObservers() {
    auto* const common = _context.editor_commons;
    if (!common || _ref_skin_connection_) return;
    if constexpr (std::is_same_v<C, card::SkillCard>) {
        _ref_card_connection_ = common->refSkill.add(
            [this](const CardPtr& value) { updateTitle(value); });
        _del_card_connection_ = common->delSkill.add(
            [this](const CWXPathPtr& owner, const CardPtr& value) {
                delCard(owner.get(), value);
            });
    } else if constexpr (std::is_same_v<C, card::ItemCard>) {
        _ref_card_connection_ = common->refItem.add(
            [this](const CardPtr& value) { updateTitle(value); });
        _del_card_connection_ = common->delItem.add(
            [this](const CWXPathPtr& owner, const CardPtr& value) {
                delCard(owner.get(), value);
            });
    } else {
        _ref_card_connection_ = common->refBeast.add(
            [this](const CardPtr& value) { updateTitle(value); });
        _del_card_connection_ = common->delBeast.add(
            [this](const CWXPathPtr& owner, const CardPtr& value) {
                delCard(owner.get(), value);
            });
    }
    _ref_skin_connection_ = common->refSkin.add([this] { refSkin(); });
    _ref_image_scale_connection_ = common->refImageScale.add(
        [this, weak = std::weak_ptr<bool>(_callbackLifetime)] {
            const auto life = weak.lock();
            if (!life || !*life || !_open || !_imageSelect || !_context.editor_props) return;
            auto& prop = *_context.editor_props;
            const auto size = prop.looks().cardSize();
            // Dは設定で画像倍率を適用すると、実際に組み込まれたプレビューをサイズ変更す
            // る。プロバイダーがダイアログを同期終了し得るため、その後はアクセスしない。
            _imageSelect->setPreviewSize(prop.s(static_cast<std::int32_t>(size.width)),
                prop.s(static_cast<std::int32_t>(size.height)), prop.s(prop.looks().cardInsets()));
        });
    _ref_data_version_connection_ = common->refDataVersion.add([this] { refDataVersion(); });
    _ref_target_version_connection_ = common->refTargetVersion.add([this] { refDataVersion(); });
    _ref_scenario_connection_ = common->refScenario.add(
        [this](const SummaryPtr& value) { refScenario(value); });
    _ref_event_tree_connection_ = common->refEventTree.add(
        [this](const event::EventTreePtr& tree) { refEventTree(tree.get()); });
    _del_event_tree_connection_ = common->delEventTree.add(
        [this](const event::EventTreePtr& tree) { refEventTree(tree.get()); });
    _ref_radar_style_connection_ = common->refRadarStyle.add([this] {
        initUseMod();
        if constexpr (std::is_same_v<C, card::ItemCard>) initHasMod();
    });
}

template <typename C>
void EffectCardDialog<C>::disconnectCommonsObservers() noexcept {
    auto* const common = _context.editor_commons;
    if (!common) return;
    if constexpr (std::is_same_v<C, card::SkillCard>) {
        common->refSkill.remove(_ref_card_connection_);
        common->delSkill.remove(_del_card_connection_);
    } else if constexpr (std::is_same_v<C, card::ItemCard>) {
        common->refItem.remove(_ref_card_connection_);
        common->delItem.remove(_del_card_connection_);
    } else {
        common->refBeast.remove(_ref_card_connection_);
        common->delBeast.remove(_del_card_connection_);
    }
    common->refSkin.remove(_ref_skin_connection_);
    common->refImageScale.remove(_ref_image_scale_connection_);
    common->refDataVersion.remove(_ref_data_version_connection_);
    common->refTargetVersion.remove(_ref_target_version_connection_);
    common->refScenario.remove(_ref_scenario_connection_);
    common->refEventTree.remove(_ref_event_tree_connection_);
    common->delEventTree.remove(_del_event_tree_connection_);
    common->refRadarStyle.remove(_ref_radar_style_connection_);
    _ref_card_connection_ = _del_card_connection_ = _ref_skin_connection_ = _ref_image_scale_connection_ = 0U;
    _ref_data_version_connection_ = _ref_target_version_connection_ = 0U;
    _ref_scenario_connection_ = _ref_event_tree_connection_ = 0U;
    _del_event_tree_connection_ = _ref_radar_style_connection_ = 0U;
}

template <typename C>
EffectCardDialog<C>::EffectCardDialog(Context context, SummaryPtr summary,
                                      Parent parent, CWXPathPtr ucOwner,
                                      UseCounterPtr useCounter, CardPtr value,
                                      const bool readOnly, const bool creationMode)
    : _readOnly(readOnly), _creationMode(creationMode || !value), _context(std::move(context)),
      _prop(_context.prop), _summ(std::move(summary)),
      _parent(std::move(parent)), _ucOwner(std::move(ucOwner)),
      _useCounter(std::move(useCounter)), _card(std::move(value)),
      _ability([this] {
          abilityview::Commons comm;
          if (_prop) comm = abilityview::Commons(_prop->requiredMsgs());
          if (_context.editor_props) comm.radioGroupSeparatorWidth =
              _context.editor_props->var().radioGroupSeparatorWidth.value;
          return comm;
      }(), {}, readOnly ? 1 : 0, _context.services) {
    _summarySkinPinned = summaryskinbinding::pinsSummarySkin(
        _readOnly, _summ.get());
    _summSkin = resolveSkin();
    prepareDraft();
    _motions = std::make_shared<MotionEditor>(
        _context, _summ, _ucOwner, _useCounter, _readOnly);
    _motions->setPathOpener([this](StringView path, bool activate) {
        return _callbacks.openPath ? _callbacks.openPath(path, activate) : false;
    });
}

template <typename C>
EffectCardDialog<C>::EffectCardDialog(Context context, SummaryPtr summary,
                                      CWXPathPtr ucOwner,
                                      UseCounterPtr useCounter, CardPtr value,
                                      const bool readOnly, const bool creationMode)
    : EffectCardDialog(std::move(context), std::move(summary), Parent{},
                       std::move(ucOwner), std::move(useCounter),
                       std::move(value), readOnly, creationMode) {}

template <typename C>
void EffectCardDialog<C>::prepareDraft() {
    _draft = duplicateCard(_card);
}

template <typename C>
std::uint32_t EffectCardDialog<C>::skillPrice(const std::uint32_t level) const {
    const auto result = 400ULL + static_cast<std::uint64_t>(level) * 200ULL;
    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(result, std::numeric_limits<std::uint32_t>::max()));
}

template <typename C>
std::uint32_t EffectCardDialog<C>::skillLevelMax() const {
    return _context.vars ? static_cast<std::uint32_t>(_context.vars->skillLevelMax) : 999U;
}

template <typename C>
std::uint32_t EffectCardDialog<C>::useCountMax() const {
    return _context.vars ? static_cast<std::uint32_t>(_context.vars->useCountMax) : 999U;
}

template <typename C>
std::uint32_t EffectCardDialog<C>::priceMax() const {
    return _context.vars ? static_cast<std::uint32_t>(_context.vars->priceMax) : 999999U;
}

template <typename C>
std::uint32_t EffectCardDialog<C>::enhanceMax() const {
    return _context.vars ? static_cast<std::uint32_t>(_context.vars->enhanceMax) : 10U;
}

template <typename C>
bool EffectCardDialog<C>::radarStyle() const {
    return _context.vars ? static_cast<bool>(_context.vars->radarStyleParams) : true;
}

template <typename C>
SkinPtr EffectCardDialog<C>::resolveSkin() const {
    return _context.findSkin(_summ.get());
}

template <typename C>
SkinPtr EffectCardDialog<C>::summSkin() const {
    if (_summarySkinPinned) {
        if (_summSkin) return _summSkin;
        return _context.editor_commons ? _context.editor_commons->skin() : SkinPtr{};
    }
    if (_summSkin) return _summSkin;
    return resolveSkin();
}

template <typename C>
void EffectCardDialog<C>::invoke(const std::vector<lib::Action>& callbacks) const {
    const auto copy = callbacks;
    for (const auto& callback : copy)
        if (callback) callback();
}

template <typename C>
void EffectCardDialog<C>::addPage(String title, const PanelPtr& panel) {
    if (!_tabf || !panel) return;
    _tabf->pages.push_back({std::move(title), {}, panel, false});
}

template <typename C>
void EffectCardDialog<C>::setup(Parent area) {
    if (_tabf) return;
    _window = std::make_shared<lib::Dialog>();
    if (_context.editor_props) {
        if constexpr (std::is_same_v<C, card::SkillCard>) _window->icon = _context.editor_props->images().menu(::cwxeditor::cwx::types::MenuID::SkillView);
        else if constexpr (std::is_same_v<C, card::ItemCard>) _window->icon = _context.editor_props->images().menu(::cwxeditor::cwx::types::MenuID::ItemView);
        else _window->icon = _context.editor_props->images().menu(::cwxeditor::cwx::types::MenuID::BeastView);
    }
    auto owner = std::dynamic_pointer_cast<lib::Window>(area);
    if (!owner) owner = _context.dialog_owner;
    lib::configure_owned_window(_window, owner,
        lib::OwnedWindowKind::Modeless);
    // このカード編集部が作ったコントロールは、自身のダイアログを開く場合が
    // ある。その所有元は開いたメインウィンドウではなく、この編集部にする。
    _context.dialog_owner = _window;
    // DのEffectCardDialogはAbsDialogにmodal=false
    // を渡す。show()で開くモデルをモーダルにすると、Editorが無効のま
    // ま残ったり、モードレスBeastダイアログで停止したように見えたりする。
    _window->resizable = true;
    const bool all_mode = lib::editor_window_host_options().all_mode;
    const bool compact_standalone_item =
        std::is_same_v<C, card::ItemCard> && !all_mode;
    // ALLとコンパクトな単体版Itemダイアログは指定幅を保
    // つ。ページの推奨幅で、以前のタブ列全体の幅へ戻さない。
    _window->initial_preferred_width = !all_mode && !compact_standalone_item;
    // lib::Windowの矩形はクライアント領域を表すが、D/SWTの取得サ
    // イズはWin32の外枠を含む。この既定値でDの参照寸法（96DPIでItem/Skill
    // が504×454、Beastが約533×458）に合わせる。
    lib::Size dialog_size = std::is_same_v<C, card::BeastCard>
                                ? lib::Size{517.0F, 419.0F}
                                : lib::Size{488.0F, 415.0F};
    bool restored_width = false;
    if (_context.editor_props) {
        const auto apply_saved_size = [&dialog_size, &restored_width](const auto& parameter) {
            if (parameter.width() > 0) {
                dialog_size.width = static_cast<float>(parameter.width());
                restored_width = true;
            }
            if (parameter.height() > 0)
                dialog_size.height = static_cast<float>(parameter.height());
        };
        const float default_width = dialog_size.width;
        if constexpr (std::is_same_v<C, card::SkillCard>)
            apply_saved_size(_context.editor_props->flex().skillCardDlg);
        else if constexpr (std::is_same_v<C, card::ItemCard>)
            apply_saved_size(_context.editor_props->flex().itemCardDlg);
        else
            apply_saved_size(_context.editor_props->flex().beastCardDlg);
        // ALLの旧保存値には、タブ列全体に合わせた広い幅
        // が残る場合がある。コンパクトな既定幅を超えて復
        // 元せず、必要な拡張は内容の最小サイズに任せる。
        if (lib::editor_window_host_options().all_mode)
            dialog_size.width = std::min(dialog_size.width, allWidth<C>(default_width));
    }
    // ALLのItemダイアログは、kAllItemTabExtraだけ広
    // げる（下記header_horizontal_padding参照）。
    const float item_tab_extra =
        std::is_same_v<C, card::ItemCard> && lib::editor_window_host_options().all_mode
            ? kAllItemTabExtra : 0.0F;
    dialog_size.width += item_tab_extra;
    // タブの横スクロールは共通の内容最小幅で防ぐ。外側の余白だけを増やさない。
    constexpr float side_padding = kEffectCardSidePadding;
    // dialog_sizeはDのタブ・クライアント幅。Window
    // の指定幅には、通常の左右5pxずつの余白も含める。
    _window->set_bounds({0.0F, 0.0F,
                         dialog_size.width + side_padding * 2.0F,
                         dialog_size.height + kEffectCardBottomPadding});
    auto window_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    window_layout->spacing = 0.0F;
    // 横の余白はWindow自身に設け、別の枠付きパネ
    // ルを追加したりタブ内部の配置を変えたりしない。
    // 下側も共通ダイアログと同じ操作部の余白をクライ
    // アント高に確保し、タブ・本体の寸法を維持する。
    window_layout->padding = {0.0F, side_padding, kEffectCardBottomPadding, side_padding};
    _window->set_layout(window_layout);
    _window->on_close_requested = [this] { forceCancel(); };
    _area = std::make_shared<lib::Panel>();
    _area->set_layout(std::make_shared<lib::FillLayout>());
    _area->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .minimum = {allWidth<C>(480.0F) + item_tab_extra, 320.0F},
                            .grow = 1.0F});
    _window->add(_area);
    _tabf = std::make_shared<lib::TabView>();
    const bool all_mode_tabs = all_mode;
    // コンパクトなALLと単体版Itemダイアログは、全見出しの
    // ためにウィンドウを広げず、タブ見出しをスクロールできる。
    _tabf->header_horizontal_padding = 4.0F;
    _tabf->dialog_header_scroll = all_mode_tabs || compact_standalone_item;
    _tabf->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .grow = 1.0F});
    _area->add(_tabf);

    constructMain();
    constructDesc();
    constructApt();
    constructUseModify();
    constructHaveModify();
    constructMotion();
    constructProps();
    constructBehavior();
    constructKeyCode();
    constructSettings();
    {
        // 以前のタブ列全体の幅を、描画・ヒット判定と同じ
        // 計測で求める。単体版Itemだけがその三分の二
        // を新しい既定幅とし、他のダイアログは変えない。
        const float full_tab_width = completeTabStripLogicalWidth(
            *_tabf, side_padding * 2.0F);
        const float tab_width = _tabf->dialog_header_scroll ? 0.0F : full_tab_width;
        auto bounds = _window->bounds();
        // 見出しが収まる場合は、指定・保存されたクライアント幅
        // と通常の外側余白を維持する。tab_width自体が
        // ウィンドウ全体の要求幅なので、余白を二重加算しない。
        if (compact_standalone_item) {
            // 従来の単体版ダイアログはタブ列全体へ広がってい
            // た。その三分の二を使うが、内容の最小480px
            // と左右余白は維持する。旧保存値の広い幅は一度
            // だけ制限し、開くたびに縮小しないようにする。
            const float target = std::max(490.0F,
                std::round(std::max(498.0F, full_tab_width) * (2.0F / 3.0F)));
            bounds.width = restored_width ? std::clamp(bounds.width, 490.0F, target)
                                          : target;
        } else bounds.width = std::max(bounds.width, tab_width);
        _window->set_bounds(bounds);
        _window->minimum_size = lib::Size{
            compact_standalone_item ? 490.0F :
                all_mode && std::is_same_v<C, card::ItemCard>
                    ? allWidth<C>(480.0F) + item_tab_extra + side_padding * 2.0F : tab_width, 0.0F};
        if (!all_mode && !std::is_same_v<C, card::ItemCard>)
            _window->minimum_size = lib::Size{bounds.width, bounds.height};
    }
    _tabf->selected_index = 0U;
    createControls();
    // 外部変更でも発生する警告更新ではなく、実際の編集イベントを登録する。
    const auto bindChanges = [this](auto&& self, const lib::WidgetPtr& widget) -> void {
        if (!widget) return;
        const auto wrap = [this](auto& callback) {
            const auto previous = callback;
            const std::weak_ptr<bool> lifetime = _callbackLifetime;
            callback = [this, previous, lifetime](auto value) {
                const auto alive = lifetime.lock();
                if (!alive || !*alive) return;
                if (previous) previous(value);
                if (*alive) abilityModified();
            };
        };
        if (const auto text = std::dynamic_pointer_cast<lib::TextEdit>(widget)) wrap(text->on_changed);
        else if (const auto number = std::dynamic_pointer_cast<lib::NumberEdit>(widget)) wrap(number->on_changed);
        else if (const auto slider = std::dynamic_pointer_cast<lib::Slider>(widget)) wrap(slider->on_changed);
        else if (const auto check = std::dynamic_pointer_cast<lib::CheckBox>(widget)) wrap(check->on_changed);
        else if (const auto radio = std::dynamic_pointer_cast<lib::RadioButton>(widget)) wrap(radio->on_changed);
        else if (const auto combo = std::dynamic_pointer_cast<lib::ComboBox>(widget)) {
            wrap(combo->on_selection_changed);
            wrap(combo->on_text_changed);
        }
        if (const auto tabs = std::dynamic_pointer_cast<lib::TabView>(widget))
            for (const auto& page : tabs->pages) self(self, page.content);
        if (const auto container = std::dynamic_pointer_cast<lib::Container>(widget))
            for (const auto& child : container->children()) self(self, child);
    };
    bindChanges(bindChanges, _tabf);
    auto footer = std::make_shared<lib::Panel>();
    auto footer_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
    footer_layout->spacing = 6.0F;
    footer_layout->cross_axis = lib::Align::Center;
    // 本体との6pxの間隔は保つが、フッター内で均等に
    // 配分する。行間として全6pxをフッター上に置く
    // と、標準操作ボタンが下のバーで下寄せに見える。
    footer_layout->padding = {3.0F, 0.0F, 3.0F, 0.0F};
    footer->set_layout(footer_layout);
    footer->set_layout_item({.horizontal = lib::Align::Stretch,
                             .vertical = lib::Align::Center});
    auto footer_spacer = std::make_shared<lib::Panel>();
    footer_spacer->set_layout_item({.horizontal = lib::Align::Stretch,
                                    .grow = 1.0F});
    footer->add(footer_spacer);
    _okButton = std::make_shared<lib::Button>(
        _readOnly ? (_prop ? _prop->requiredMsgs().dlgTextClose.value : "閉じる")
                  : (_prop ? _prop->requiredMsgs().dlgTextOK.value : "OK"));
    _okButton->set_layout_item({.horizontal = lib::Align::Stretch,
                                .vertical = lib::Align::Center,
                                .minimum = {96.0F, 0.0F},
                                .preferred = {96.0F, 0.0F}});
    _okButton->default_button = true;
    _okButton->on_click = [this] {
        // 新しく開いた警告なしの読み取り専用プロパティは、確定しない閉じる操
        // 作を提供する。読み取り専用による適用拒否を、確定成功へ変えない。
        if (_readOnly && _readOnlyCloseEligible) { forceCancel(); return; }
        const std::weak_ptr<bool> lifetime = _callbackLifetime;
        if (apply()) {
            const auto alive = lifetime.lock();
            if (!alive || !*alive || !_open || !_window) return;
            _window->result = lib::DialogResult::OK;
            close();
        }
    };
    _applyButton = std::make_shared<lib::Button>(
        _prop ? _prop->requiredMsgs().dlgTextApply.value : "適用");
    _applyButton->set_layout_item({.horizontal = lib::Align::Stretch,
                                   .vertical = lib::Align::Center,
                                   .minimum = {96.0F, 0.0F},
                                   .preferred = {96.0F, 0.0F}});
    _applyButton->set_enabled(!_readOnly);
    _applyButton->on_click = [this] { (void)apply(); };
    _cancelButton = std::make_shared<lib::Button>(
        _prop ? _prop->requiredMsgs().dlgTextCancel.value : "キャンセル");
    _cancelButton->set_layout_item({.horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Center,
                                    .minimum = {96.0F, 0.0F},
                                    .preferred = {96.0F, 0.0F}});
    _cancelButton->cancel_button = true;
    _cancelButton->on_click = [this] { forceCancel(); };
    // D/AbsDialogの標準ボタン順は、OK、取消、適用。
    footer->add(_okButton);
    footer->add(_cancelButton);
    footer->add(_applyButton);
    auto trailing_footer_spacer = std::make_shared<lib::Panel>();
    trailing_footer_spacer->set_layout_item({.horizontal = lib::Align::Stretch,
                                             .grow = 1.0F});
    footer->add(trailing_footer_spacer);
    _window->add(footer);
    updateEnabledImpl();
}

template <typename C>
EffectCardDialog<C>::~EffectCardDialog() {
    *_callbackLifetime = false;
    *_keyCodeHostActive = false;
    disconnectCommonsObservers();
    if (_keyCodes) {
        // テスト用・補助Shellは、D側の所有元の破棄後もこの子を保持
        // する場合がある。逆向きの所有関係を外す前に操作を無効にする。
        const auto keys = _keyCodes;
        try { keys->hostActive(false); } catch (...) {
            // ネイティブの終了コールバックを呼ぶ前に、状態を無効にする。
        }
        _keyCodes->clearModifyListeners();
        // KeyCodeViewの補助ダイアログ所有元はこのWindowを保
        // 持する。逆向きの子の接続を外し、閉じたホストに循環参照を残さない。
        if (auto* parent = _keyCodes->parent()) parent->remove(_keyCodes);
        _keyCodes.reset();
    }
    // AbilityViewはこのダイアログが所有する。D形式の公
    // 開コールバック一覧にthisをキャプチャするラムダを残すと
    // 、ウィジェット破棄中の最後の通知で呼ばれ得るため解除する。
    _ability.modEvent.clear();
    if (_context.services.audio) {
        if (_se1.previewHandle) _context.services.audio->stop(_se1.previewHandle);
        if (_se2.previewHandle) _context.services.audio->stop(_se2.previewHandle);
    }
    if (_window) _window->on_close_requested = {};
    if (_okButton) _okButton->on_click = {};
    if (_applyButton) _applyButton->on_click = {};
    if (_cancelButton) _cancelButton->on_click = {};
    applyEvent.clear();
    appliedEvent.clear();
    closeEvent.clear();
}

template <typename C>
void EffectCardDialog<C>::open() {
    const auto lifetime = _callbackLifetime;
    const auto revision = ++_hostRevision;
    const auto current = [this, lifetime, revision] {
        return *lifetime && _hostRevision == revision;
    };
    // DのAbsDialogはopen内で、showの前に操作部を構築・初
    // 期化する。保持したモデルの再アクティブ化では未確定の編集を維持する。
    if (!_tabf) {
        setup(_parent);
        refCard(_card);
        refDataVersion();
    }
    if (!current()) return;
    connectCommonsObservers();
    _open = true;
    *_keyCodeHostActive = true;
    if (const auto keys = _keyCodes) {
        const auto lifetime = _callbackLifetime;
        keys->hostActive(true);
        if (!current() || !_open) return;
    }
    if (_imageSelect) {
        const auto lifetime = _callbackLifetime;
        auto* const selector = _imageSelect.get();
        selector->active(true);
        if (!current() || !_open || _imageSelect.get() != selector) return;
        selector->bindEditorCommons(_context.editor_commons);
    }
    _window->result = lib::DialogResult::None;
    if (_context.services.gui) _context.services.gui->show(_window);
}

template <typename C>
void EffectCardDialog<C>::active() {
    open();
    if (_context.services.gui) _context.services.gui->focus(_tabf);
}

template <typename C>
void EffectCardDialog<C>::forceCancel() {
    if (_window) _window->result = lib::DialogResult::Cancel;
    close();
}

template <typename C>
void EffectCardDialog<C>::createControls() {
    if (_settingsComment) _settingsComment->multiline = true;
}

template <typename C>
void EffectCardDialog<C>::constructMain() {
    const auto panel = std::make_shared<lib::Panel>();
    auto panel_layout = std::make_shared<lib::GridLayout>();
    panel_layout->columns = 2U;
    panel_layout->spacing = 6.0F;
    // Dのページにある小さい左の内容余白だけを残す。他
    // の辺の余白はネイティブのタブページが提供する。
    panel_layout->padding = {0.0F, 0.0F, 0.0F, 6.0F};
    panel->set_layout(panel_layout);
    const auto left = std::make_shared<lib::Panel>();
    auto left_layout = std::make_shared<lib::LinearLayout>();
    left_layout->spacing = 6.0F;
    left->set_layout(left_layout);
    left->set_layout_item({.row = 0, .column = 0,
                           .horizontal = lib::Align::Stretch,
                           .vertical = lib::Align::Stretch,
                           .minimum = {allWidth<C>(300.0F), 0.0F}, .grow = 1.0F});
    const auto right = std::make_shared<lib::Panel>();
    auto right_layout = std::make_shared<lib::LinearLayout>();
    right_layout->spacing = 6.0F;
    right->set_layout(right_layout);
    right->set_layout_item({.row = 0, .column = 1,
                            .horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .minimum = {allWidth<C>(158.0F), 0.0F},
                            // ALLではこのPanelの固定推奨幅が
                            // 「沈黙時に発動」などの群を切ってしまう
                            // ため、列に計測した内容幅を使わせる。
                            .preferred = {lib::editor_window_host_options().all_mode
                                              ? 0.0F : 158.0F, 0.0F}});
    panel->add(left);
    panel->add(right);

    const auto cardGroup = std::make_shared<lib::GroupBox>();
    cardGroup->title = _prop ? _prop->requiredMsgs().name.value : "Name";
    auto name_layout = std::make_shared<lib::GridLayout>();
    name_layout->columns = 2U;
    name_layout->spacing = 6.0F;
    cardGroup->set_layout(name_layout);
    cardGroup->set_layout_item({
        .horizontal = lib::Align::Stretch,
        .vertical = lib::Align::Stretch,
        .minimum = {0.0F, 49.0F},
        .preferred = {0.0F, 49.0F}});
    _name = std::make_shared<lib::TextEdit>();
    if (_prop && !_prop->looks().monospace().empty()) {
        lib::FontSpec font;
        font.family = _prop->looks().monospace();
        // ALLのカード名は、従来の10pt等幅文字の1.25倍にする。
        font.points = lib::editor_window_host_options().all_mode ? 12.5F : 10.0F;
        _name->set_font(std::move(font));
    }
    _name->on_changed = [this](const StringView value) {
        if (_prop && _name) {
            _name->text = String(value);
        }
        refreshWarning();
    };
    _name->set_layout_item({.row = 0, .column = 0,
                            .horizontal = lib::Align::Stretch, .grow = 1.0F});
    cardGroup->add(_name);
    if (_prop) {
        cardGroup->add(std::make_shared<lib::Label>(
            ::cwxeditor::cwx::utils::tryFormat(
                _prop->requiredMsgs().nameLimit.value, _prop->looks().nameLimit(),
                _prop->looks().nameLimit() / 2U)));
    }
    left->add(cardGroup);

    if (_context.editor_props) {
        auto& editor_props = *_context.editor_props;
        const auto size = editor_props.looks().cardSize();
        _imageSelect = std::make_unique<imageselect::ImageSelectBase>(
            imageselect::ImageSelectKind::Card, false, left,
            _readOnly ? 1 : 0, editor_props, _summ.get(),
            editor_props.s(static_cast<std::int32_t>(size.width)),
            editor_props.s(static_cast<std::int32_t>(size.height)),
            editor_props.s(editor_props.looks().cardInsets()),
            types::CardImagePosition::TopLeft, true,
            [this] {
                if (!_prop) return String{};
                const auto scenario_name = _scenario
                                               ? _scenario->text
                                               : (_summ ? _summ->scenarioName()
                                                        : String{});
                const auto author = _author
                                        ? _author->text
                                        : (_summ ? _summ->author() : String{});
                return ::cwxeditor::cwx::summary::
                    toExportedImageNameWithCardName(
                        *_prop, scenario_name, author,
                        _name ? StringView{_name->text} : StringView{});
            },
            lib::Action{}, imageselect::ImageSelectBase::Definitions{}, false,
            _summarySkinPinned ? _summSkin : SkinPtr{},
            _context.services);
        // ALLのImageListは、このカードダイアログのモードレスな子
        // 。所有元を明示しないと、Webホストが親の後ろへ置く場合がある。
        _imageSelect->imageListOwner(_window);
        _imageSelect->bindEditorCommons(_context.editor_commons);
        _imageSelect->setCardImages(_imgPath.images, false);
        _imageSelect->modEvent.push_back([this] {
            _imgPath.images = _imageSelect->cardImages();
            abilityModified();
        });
        _imgPath.materializer = [this](const bool force_applying) {
            const auto material = _imageSelect->cardMaterialPath(force_applying);
            return ImageSelectState::MaterialPath{material.images,
                                                   material.cancel};
        };
        _imageSelect->widget()->set_layout_item({
            .horizontal = lib::Align::Stretch,
            .vertical = lib::Align::Stretch, .grow = 1.0F});
    }

    _needSpell = std::make_shared<lib::CheckBox>(
        _prop ? (std::is_same_v<C, card::BeastCard>
                     ? _prop->requiredMsgs().needSpellAndActive.value
                     : _prop->requiredMsgs().needSpell.value)
              : "Need spell");
    _needSpell->on_changed = [this](bool) { refreshWarning(); };
    const auto conditionGroup = std::make_shared<lib::GroupBox>();
    conditionGroup->title =
        _prop ? _prop->requiredMsgs().workConditionGroup.value : "Activation condition";
    verticalLayout(conditionGroup, 2.0F);
    conditionGroup->set_layout_item({
        .horizontal = lib::Align::Stretch,
        .vertical = lib::Align::Stretch,
        .minimum = {allWidth<C>(158.0F), 70.0F},
        .preferred = {allWidth<C>(158.0F), 70.0F}});
    conditionGroup->add(_needSpell);
    right->add(conditionGroup);

    const auto effectGroup = std::make_shared<lib::GroupBox>();
    effectGroup->title = _prop ? _prop->requiredMsgs().elementProps.value : "Effect type";
    auto effect_layout = std::make_shared<lib::GridLayout>();
    effect_layout->columns = 2U;
    effect_layout->equal_width = true;
    effect_layout->spacing = 2.0F;
    effectGroup->set_layout(effect_layout);
    effectGroup->set_layout_item({
        .horizontal = lib::Align::Stretch,
        .vertical = lib::Align::Stretch,
        .minimum = {allWidth<C>(158.0F), 122.0F},
        .preferred = {allWidth<C>(158.0F), 122.0F}});
    for (std::size_t i = 0; i < effectTypes.size(); ++i) {
        const auto effect_label = _prop
                                      ? ::cwxeditor::cwx::utils::tryFormat(
                                            _prop->requiredMsgs().effectTypeElement.value,
                                            _prop->requiredMsgs().effectTypeName(effectTypes[i]))
                                      : enumLabel("Effect", effectTypes[i]);
        _effTyp[i] = std::make_shared<lib::RadioButton>(effect_label);
        if (_prop)
            _effTyp[i]->set_tooltip(
                _prop->requiredMsgs().effectTypeDesc(effectTypes[i]));
        _effTyp[i]->set_enabled(!_readOnly);
        _effTyp[i]->on_changed = [this, i](bool checked) {
            if (!checked) return;
            for (std::size_t j = 0; j < _effTyp.size(); ++j)
                if (j != i && _effTyp[j]) _effTyp[j]->checked = false;
            refreshWarning();
        };
        if (i < 2U) {
            _effTyp[i]->set_layout_item({
                .row = 0U, .column = i,
                .horizontal = lib::Align::Start,
                .vertical = lib::Align::Center, .grow = 1.0F});
        } else {
            _effTyp[i]->set_layout_item({
                .row = i - 1U, .column = 0U, .column_span = 2U,
                .horizontal = lib::Align::Start,
                .vertical = lib::Align::Center, .grow = 1.0F});
        }
        effectGroup->add(_effTyp[i]);
    }
    right->add(effectGroup);

    const auto resistGroup = std::make_shared<lib::GroupBox>();
    resistGroup->title = _prop ? _prop->requiredMsgs().resistProps.value : "Resistance";
    auto resist_layout = std::make_shared<lib::GridLayout>();
    resist_layout->columns = 2U;
    resist_layout->equal_width = true;
    resist_layout->spacing = 2.0F;
    resistGroup->set_layout(resist_layout);
    resistGroup->set_layout_item({
        .horizontal = lib::Align::Stretch,
        .vertical = lib::Align::Stretch,
        .minimum = {allWidth<C>(158.0F), 85.0F},
        .preferred = {allWidth<C>(158.0F), 85.0F}});
    for (std::size_t i = 0; i < resistTypes.size(); ++i) {
        _res[i] = std::make_shared<lib::RadioButton>(
            _prop ? _prop->requiredMsgs().resistName(resistTypes[i])
                  : enumLabel("Resist", resistTypes[i]));
        if (_prop)
            _res[i]->set_tooltip(_prop->requiredMsgs().resistDesc(resistTypes[i]));
        _res[i]->set_enabled(!_readOnly);
        _res[i]->on_changed = [this, i](bool checked) {
            if (!checked) return;
            for (std::size_t j = 0; j < _res.size(); ++j)
                if (j != i && _res[j]) _res[j]->checked = false;
            refreshWarning();
        };
        _res[i]->set_layout_item({
            .row = i / 2U, .column = i % 2U,
            .horizontal = lib::Align::Start,
            .vertical = lib::Align::Center, .grow = 1.0F});
        resistGroup->add(_res[i]);
    }
    right->add(resistGroup);
    if constexpr (std::is_same_v<C, card::BeastCard>) {
        const auto show_style_group = std::make_shared<lib::GroupBox>();
        show_style_group->title =
            _prop ? _prop->requiredMsgs().showStyleForBeastCard.value
                   : "Display when invoked";
        show_style_group->set_layout(std::make_shared<lib::FillLayout>());
        show_style_group->set_layout_item({
            .horizontal = lib::Align::Stretch,
            .vertical = lib::Align::Stretch,
            .minimum = {allWidth<C>(158.0F), 49.0F},
            .preferred = {allWidth<C>(158.0F), 49.0F}});
        _showStyle = std::make_shared<lib::ComboBox>();
        for (const auto value : _showStyles) {
            _showStyle->items.push_back(
                {_prop ? _prop->requiredMsgs().showStyleName(value)
                       : enumLabel("Style", value),
                 std::to_string(static_cast<int>(value))});
        }
        _showStyle->selected_index = 1U;
        _showStyle->on_selection_changed =
            [this](std::optional<std::size_t>) { refreshWarning(); };
        show_style_group->add(_showStyle);
        right->add(show_style_group);
    }
    addPage(_prop ? _prop->requiredMsgs().card.value : "Card", panel);
}

template <typename C>
void EffectCardDialog<C>::constructDesc() {
    const auto panel = std::make_shared<lib::Panel>();
    verticalLayout(panel, 6.0F, {6.0F, 6.0F, 6.0F, 6.0F});
    const auto valueGroup = std::make_shared<lib::GroupBox>();
    valueGroup->title =
        _prop ? (std::is_same_v<C, card::SkillCard>
                     ? _prop->requiredMsgs().skillLevel.value
                     : _prop->requiredMsgs().useCountGroup.value)
              : "Values";
    auto value_layout = std::make_shared<lib::GridLayout>();
    value_layout->columns = 6U;
    value_layout->spacing = 5.0F;
    valueGroup->set_layout(value_layout);
    valueGroup->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Stretch,
                                 .minimum = {0.0F, 58.0F},
                                 .preferred = {0.0F, 58.0F}});

    if constexpr (std::is_same_v<C, card::SkillCard>) {
        _level = std::make_shared<lib::NumberEdit>();
        _level->minimum = 0;
        _level->maximum = skillLevelMax();
        _level->on_changed = [this](double value) {
            updatePriceByLevel(static_cast<int>(value));
        };
        valueGroup->add(_level);
        valueGroup->add(std::make_shared<lib::Label>(
            _prop ? ::cwxeditor::cwx::utils::tryFormat(
                        _prop->requiredMsgs().rangeHint.value, 0, skillLevelMax())
                  : String{}));
    } else {
        _useCount = std::make_shared<lib::NumberEdit>();
        _useCount->minimum = 0;
        _useCount->maximum = useCountMax();
        _useCount->on_changed = [this](double) { refreshWarning(); };
        valueGroup->add(_useCount);
        valueGroup->add(std::make_shared<lib::Label>(
            _prop ? ::cwxeditor::cwx::utils::tryFormat(
                        _prop->requiredMsgs().useCountRange.value, useCountMax())
                  : String{}));
    }
    valueGroup->add(std::make_shared<lib::Label>(
        _prop ? _prop->requiredMsgs().price.value : "Price"));
    _price = std::make_shared<lib::NumberEdit>();
    _price->minimum = 0;
    _price->maximum = priceMax();
    valueGroup->add(_price);
    valueGroup->add(std::make_shared<lib::Label>(
        _prop ? ::cwxeditor::cwx::utils::tryFormat(
                    _prop->requiredMsgs().rangeHint.value, 0, priceMax())
              : String{}));
    panel->add(valueGroup);

    if (!_imageSelect) {
        const auto imageGroup = std::make_shared<lib::GroupBox>();
        imageGroup->title = _prop ? _prop->requiredMsgs().image.value : "Card images";
        _imagePathEdit = std::make_shared<lib::TextEdit>();
        _imagePathEdit->multiline = true;
        _imagePathEdit->read_only = _readOnly;
        _imagePathEdit->on_changed = [this](const StringView text) {
            _imgPath.images.clear();
            std::size_t begin = 0;
            while (begin <= text.size()) {
                const auto end = text.find('\n', begin);
                auto line = String(text.substr(begin, end == StringView::npos
                                                         ? text.size() - begin
                                                         : end - begin));
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) _imgPath.images.push_back(
                    std::make_shared<card::CardImage>(std::move(line)));
                if (end == StringView::npos) break;
                begin = end + 1U;
            }
            refreshWarning();
        };
        imageGroup->add(_imagePathEdit);
        if (!_readOnly) {
            auto browse = std::make_shared<lib::Button>(
                _prop ? _prop->requiredMsgs().reference.value : "...");
            browse->on_click = [this] {
                if (!_context.services.file_picker) return;
                lib::FilePickerRequest request;
                request.title = _prop ? _prop->requiredMsgs().image.value
                                      : "Select card images";
                request.allow_multiple = true;
                request.filters = {
                    {"Images", {"*.bmp", "*.png", "*.jpg", "*.jpeg", "*.gif",
                                "*.jpy1", "*.jptx", "*.jpdc"}}};
                const std::weak_ptr<lib::TextEdit> target = _imagePathEdit;
                (void)_context.services.file_picker->open_files_async(
                    std::move(request), [target](lib::FilePickerResult picked) {
                        if (!picked.succeeded()) return;
                        String text;
                        for (const auto& selected : picked.selections) {
                            const auto file = selected.native_path();
                            if (!file) continue;
                            if (!text.empty()) text.push_back('\n');
                            text += structs::pathToUtf8(*file);
                        }
                        if (const auto edit = target.lock())
                            (void)lib::set_text_value(*edit, std::move(text));
                    });
            };
            imageGroup->add(browse);
        }
        panel->add(imageGroup);
    }

    _desc = std::make_shared<lib::TextEdit>();
    _desc->multiline = true;
    _desc->wrap = true;
    // DのFixedWidthTextはここでMULTI|WRAPを使い、V_SCROLL
    // は付けない。スクロールバーの余白で折り返しが変わらないよう、指定の説明幅を維持する。
    _desc->vertical_scroll = false;
    if (_prop) {
        const auto skin = summSkin();
        auto font = _prop->looks().cardDescFont(
            skin ? skin->legacy() : (_summ && _summ->legacy()));
        if (_context.editor_props)
            font = _context.editor_props->adjustFont(std::move(font));
        auto content_font = dutils::createFontFromPixels(std::move(font));
        // FixedWidthTextは、メニュー・キャストのダイアログと同じく
        // 制作された内容。ALLの追加のダイアログ用フォント拡大を適用しない。
        content_font.content_font = true;
        _desc->set_font(std::move(content_font));
    }
    _desc->on_changed = [this](StringView) { refreshWarning(); };
    const auto descGroup = std::make_shared<lib::GroupBox>();
    descGroup->title = _prop ? _prop->requiredMsgs().desc.value : "Description";
    // DはここでFixedWidthTextをCenterLayout
    // で包む。カード説明の幅を維持し、引き伸ばしで折り返
    // しや元のダイアログの見た目が変わらないようにする。
    auto desc_layout = std::make_shared<lib::GridLayout>();
    desc_layout->columns = 3U;
    desc_layout->spacing = 0.0F;
    desc_layout->padding = {};
    descGroup->set_layout(desc_layout);
    descGroup->set_layout_item({.horizontal = lib::Align::Stretch,
                                .vertical = lib::Align::Stretch,
                                .minimum = {allWidth<C>(320.0F), 120.0F}, .grow = 1.0F});
    const auto top_desc_space = std::make_shared<lib::Panel>();
    top_desc_space->set_layout_item({.row = 0U, .column = 0U,
                                     .column_span = 3U,
                                     .horizontal = lib::Align::Stretch,
                                     .vertical = lib::Align::Center,
                                     .minimum = {0.0F, 4.0F},
                                     .preferred = {0.0F, 4.0F}});
    descGroup->add(top_desc_space);
    const auto left_desc_space = std::make_shared<lib::Panel>();
    left_desc_space->set_layout_item({.row = 1U, .column = 0U,
                                      .horizontal = lib::Align::Stretch,
                                      .vertical = lib::Align::Stretch,
                                      .grow = 1.0F});
    descGroup->add(left_desc_space);
    _desc->set_layout_item({.row = 1U, .column = 1U,
                            .horizontal = lib::Align::Center,
                            .vertical = lib::Align::Center});
    descGroup->add(_desc);
    const auto right_desc_space = std::make_shared<lib::Panel>();
    right_desc_space->set_layout_item({.row = 1U, .column = 2U,
                                       .horizontal = lib::Align::Stretch,
                                       .vertical = lib::Align::Stretch,
                                       .grow = 1.0F});
    descGroup->add(right_desc_space);
    const auto bottom_desc_space = std::make_shared<lib::Panel>();
    bottom_desc_space->set_layout_item({.row = 2U, .column = 0U,
                                        .column_span = 3U,
                                        .horizontal = lib::Align::Stretch,
                                        .vertical = lib::Align::Stretch,
                                        .grow = 1.0F});
    descGroup->add(bottom_desc_space);
    panel->add(descGroup);

    const auto sourceGroup = std::make_shared<lib::GroupBox>();
    sourceGroup->title = _prop ? _prop->requiredMsgs().source.value : "Source";
    auto source_layout = std::make_shared<lib::GridLayout>();
    source_layout->columns = 2U;
    source_layout->spacing = 5.0F;
    sourceGroup->set_layout(source_layout);
    sourceGroup->set_layout_item({.horizontal = lib::Align::Stretch,
                                  .vertical = lib::Align::Stretch,
                                  .minimum = {0.0F, 96.0F},
                                  .preferred = {0.0F, 96.0F}});
    _scenario = std::make_shared<lib::TextEdit>();
    _author = std::make_shared<lib::TextEdit>();
    _scenario->on_changed = [this](StringView) { abilityModified(); };
    _author->on_changed = [this](StringView) { abilityModified(); };
    _scenario->set_layout_item({.horizontal = lib::Align::Stretch, .grow = 1.0F});
    _author->set_layout_item({.horizontal = lib::Align::Stretch, .grow = 1.0F});
    auto scenario_label = std::make_shared<lib::Label>(
        _prop ? _prop->requiredMsgs().sourceScenario.value : "Scenario");
    scenario_label->set_layout_item({.row = 0U, .column = 0U,
                                     .vertical = lib::Align::Center});
    _scenario->set_layout_item({.row = 0U, .column = 1U,
                                .horizontal = lib::Align::Stretch,
                                .vertical = lib::Align::Center, .grow = 1.0F});
    sourceGroup->add(scenario_label);
    sourceGroup->add(_scenario);
    auto author_label = std::make_shared<lib::Label>(
        _prop ? _prop->requiredMsgs().sourceAuthor.value : "Author");
    author_label->set_layout_item({.row = 1U, .column = 0U,
                                   .vertical = lib::Align::Center});
    _author->set_layout_item({.row = 1U, .column = 1U,
                              .horizontal = lib::Align::Stretch,
                              .vertical = lib::Align::Center, .grow = 1.0F});
    sourceGroup->add(author_label);
    sourceGroup->add(_author);
    if (_context.editor_props) {
        dmenu::Commons text_comm;
        text_comm.services = _context.services;
        text_comm.undoMaxEtc = static_cast<std::size_t>(std::max<std::int32_t>(
            0, _context.editor_props->var().undoMaxEtc.value));
        text_comm.presentation.textTabs = static_cast<std::size_t>(std::max<std::int32_t>(
            1, _context.editor_props->var().textTabs.value));
        text_comm.presentation.menuText = [prop = _context.editor_props](auto id) { return prop->buildMenu(id); };
        text_comm.presentation.menuImage = [prop = _context.editor_props](auto id) { return prop->images().menu(id); };
        // DのcreateLineはcatchModを渡す。初期化では保存済
        // みテキストの基準を更新し、ignoreModで履歴追加を抑止する。
        const auto canSaveSourceHistory =
            [this, life = std::weak_ptr<bool>{_callbackLifetime}] {
                const auto token = life.lock();
                return token && *token && !_ignoreMod;
            };
        _scenarioTextMenu = dmenu::createTextMenu(
            text_comm, dmenu::TMM(_scenario), canSaveSourceHistory);
        _authorTextMenu = dmenu::createTextMenu(
            text_comm, dmenu::TMM(_author), canSaveSourceHistory);
        const auto scenarioChanged = _scenario->on_changed;
        _scenario->on_changed = [scenarioChanged,
            menu = std::weak_ptr<dmenu::TextMenuModify>(_scenarioTextMenu)](StringView value) {
            if (scenarioChanged) scenarioChanged(value);
            if (const auto owner = menu.lock()) owner->modifyText();
        };
        const auto authorChanged = _author->on_changed;
        _author->on_changed = [authorChanged,
            menu = std::weak_ptr<dmenu::TextMenuModify>(_authorTextMenu)](StringView value) {
            if (authorChanged) authorChanged(value);
            if (const auto owner = menu.lock()) owner->modifyText();
        };
    }
    const auto reset = std::make_shared<lib::Button>(
        _prop ? _prop->requiredMsgs().resetSource.value : "Reset source");
    reset->on_click = [this] { resetSource(); };
    reset->set_layout_item({.row = 2U, .column = 0U, .column_span = 2U,
                            .horizontal = lib::Align::End,
                            .vertical = lib::Align::Center});
    sourceGroup->add(reset);
    panel->add(sourceGroup);
    addPage(_prop ? (std::is_same_v<C, card::SkillCard>
                         ? _prop->requiredMsgs().levelAndDesc.value
                         : _prop->requiredMsgs().useCountAndDesc.value)
                  : (std::is_same_v<C, card::SkillCard>
                         ? "Level and description"
                         : "Uses and description"),
            panel);
}

template <typename C>
void EffectCardDialog<C>::constructApt() {
    const auto panel = std::make_shared<lib::Panel>();
    fillPage(panel);
    // DのconstructAptはmod(_ability)を呼ぶ。
    // 適性ラジオの変更も、他の操作部と同じ変更済み・適用の経路を通す。
    _ability.addModificationListener([this] { abilityModified(); });
    if (auto ability = _ability.widget()) panel->add(ability);
    addPage(_prop ? _prop->requiredMsgs().apt.value : "Aptitudes", panel);
}

template <typename C>
GroupPtr EffectCardDialog<C>::createModParent(const Parent& parent, String title) {
    auto group = std::make_shared<lib::GroupBox>();
    group->title = std::move(title);
    group->set_layout(std::make_shared<lib::FillLayout>());
    if (parent) parent->add(group);
    return group;
}

template <typename C>
std::vector<int> EffectCardDialog<C>::readEnhanceControls(
    const std::array<std::size_t, 4>& table,
    const std::shared_ptr<radarspinner::RadarSpinner>& radar,
    const std::shared_ptr<scales::Scales>& scalesControl) const {
    const auto values = radar ? radar->getValues() : (scalesControl ? scalesControl->getValues()
                                                                      : std::vector<std::int32_t>{});
    std::vector<int> result(editableEnhancements.size(), 0);
    for (std::size_t i = 0; i < editableEnhancements.size(); ++i)
        if (table[static_cast<std::size_t>(editableEnhancements[i])] < values.size())
            result[i] = values[table[static_cast<std::size_t>(editableEnhancements[i])]];
    return result;
}

template <typename C>
void EffectCardDialog<C>::writeEnhanceControls(
    const std::array<std::size_t, 4>& table, const std::vector<int>& values,
    std::shared_ptr<radarspinner::RadarSpinner>& radar,
    std::shared_ptr<scales::Scales>& scalesControl) {
    std::vector<int> normalized(editableEnhancements.size(), 0);
    for (std::size_t i = 0; i < normalized.size() && i < values.size(); ++i)
        normalized[i] = values[i];
    if (radar) radar->setValues(normalized);
    if (scalesControl) scalesControl->setValues(normalized);
    (void)table;
}

template <typename C>
void EffectCardDialog<C>::createMod(
    const Parent& parent, std::array<std::size_t, 4>& table,
    std::shared_ptr<radarspinner::RadarSpinner>& radar,
    std::shared_ptr<scales::Scales>& scalesControl) {
    const auto old = readEnhanceControls(table, radar, scalesControl);
    if (parent && radar) parent->remove(radar);
    if (parent && scalesControl) parent->remove(scalesControl);
    radar.reset();
    scalesControl.reset();
    table.fill(invalidEnhancementIndex);
    for (std::size_t i = 0; i < editableEnhancements.size(); ++i)
        table[static_cast<std::size_t>(editableEnhancements[i])] = i;

    const auto maximum = enhanceMax();
    const auto steps = maximum * 2U + 1U;
    const auto minimum = -static_cast<int>(maximum);
    std::vector<String> labels;
    labels.reserve(editableEnhancements.size());
    for (const auto value : editableEnhancements) {
        labels.push_back(
            _prop ? ::cwxeditor::cwx::utils::tryFormat(
                        _prop->requiredMsgs().enhanceBonus.value,
                        _prop->requiredMsgs().enhanceName(value))
                  : enumLabel("Enhance", value));
    }
    if (radarStyle()) {
        radar = std::make_shared<radarspinner::RadarSpinner>(
            parent, _readOnly ? radarspinner::RadarSpinner::ReadOnly
                              : radarspinner::RadarSpinner::None, _context.services.gui);
        radar->setRadar(steps, labels, minimum);
        radar->borderlines({0});
        radar->setValues(old);
        radar->modEvent.push_back([this] { abilityModified(); });
        if (parent) parent->add(radar);
    } else {
        scalesControl = std::make_shared<scales::Scales>(
            parent.get(), _readOnly ? scales::STYLE_READ_ONLY : scales::STYLE_NONE);
        scalesControl->setScales(steps, labels, static_cast<int>(maximum / 2U), minimum);
        scalesControl->borderlines({0});
        scalesControl->setValues(old);
        scalesControl->modEvent.push_back([this] { abilityModified(); });
        if (parent) parent->add(scalesControl);
    }
}

template <typename C>
void EffectCardDialog<C>::initUseMod() {
    createMod(_useModParent, _useModTbl, _useModR, _useModS);
}

template <typename C>
void EffectCardDialog<C>::initHasMod() {
    createMod(_hasModParent, _hasModTbl, _hasModR, _hasModS);
}

template <typename C>
void EffectCardDialog<C>::constructUseModify() {
    const auto panel = std::make_shared<lib::Panel>();
    fillPage(panel);
    _useModParent = createModParent(
        panel, _prop ? _prop->requiredMsgs().useModify.value : "Use bonus");
    initUseMod();
    addPage(_prop ? _prop->requiredMsgs().useBonus.value : "Use bonus", panel);
}

template <typename C>
void EffectCardDialog<C>::constructHaveModify() {
    if constexpr (std::is_same_v<C, card::ItemCard>) {
        const auto panel = std::make_shared<lib::Panel>();
        fillPage(panel);
        _hasModParent = createModParent(
            panel, _prop ? _prop->requiredMsgs().haveModify.value : "Owner bonus");
        initHasMod();
        addPage(_prop ? _prop->requiredMsgs().haveBonus.value : "Owner bonus", panel);
    }
}

template <typename C>
void EffectCardDialog<C>::constructMotion() {
    const auto panel = std::make_shared<lib::Panel>();
    fillPage(panel);
    if (_motions) {
        _motions->warningEvent.push_back([this] { refreshWarning(); });
        _motions->modEvent.push_back([this] { abilityModified(); });
        panel->add(_motions);
    }
    addPage(_prop ? _prop->requiredMsgs().motion.value : "Motions", panel);
}

template <typename C>
void EffectCardDialog<C>::constructProps() {
    const auto panel = std::make_shared<lib::Panel>();
    verticalLayout(panel, 6.0F, {6.0F, 6.0F, 6.0F, 6.0F});

    const auto top = std::make_shared<lib::Panel>();
    auto top_layout = std::make_shared<lib::GridLayout>();
    top_layout->columns = 2U;
    top_layout->spacing = 6.0F;
    top->set_layout(top_layout);
    top->set_layout_item({.horizontal = lib::Align::Stretch,
                          .vertical = lib::Align::Stretch,
                          .minimum = {0.0F, 208.0F},
                          .preferred = {0.0F, 208.0F}});
    panel->add(top);

    const auto left = std::make_shared<lib::Panel>();
    verticalLayout(left, 6.0F);
    left->set_layout_item({.row = 0U, .column = 0U,
                           .horizontal = lib::Align::Stretch,
                           .vertical = lib::Align::Stretch,
                           .minimum = {allWidth<C>(205.0F), 208.0F}, .grow = 1.0F});
    top->add(left);
    const auto right = std::make_shared<lib::Panel>();
    verticalLayout(right, 6.0F);
    right->set_layout_item({.row = 0U, .column = 1U,
                            .horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .minimum = {allWidth<C>(245.0F), 208.0F}, .grow = 1.0F});
    top->add(right);

    const auto targetGroup = std::make_shared<lib::GroupBox>();
    targetGroup->title = _prop ? _prop->requiredMsgs().effectTarget.value : "Target";
    auto target_layout = std::make_shared<lib::GridLayout>();
    target_layout->columns = 2U;
    target_layout->spacing = 2.0F;
    targetGroup->set_layout(target_layout);
    targetGroup->set_layout_item({.horizontal = lib::Align::Stretch,
                                  .vertical = lib::Align::Stretch,
                                  .minimum = {allWidth<C>(205.0F), 130.0F},
                                  .preferred = {allWidth<C>(205.0F), 130.0F}});
    for (std::size_t i = 0; i < targetTypes.size(); ++i) {
        _targ[i] = std::make_shared<lib::RadioButton>(
            _prop ? _prop->requiredMsgs().cardTargetName(targetTypes[i])
                  : enumLabel("Target", targetTypes[i]));
        _targ[i]->set_enabled(!_readOnly);
        _targ[i]->on_changed = [this, i](bool checked) {
            if (!checked) return;
            for (std::size_t j = 0; j < _targ.size(); ++j)
                if (j != i && _targ[j]) _targ[j]->checked = false;
            refreshEnblOneAll();
        };
        _targ[i]->set_layout_item({.row = i / 2U, .column = i % 2U,
                                   .horizontal = lib::Align::Start,
                                   .vertical = lib::Align::Center,
                                   .grow = 1.0F});
        targetGroup->add(_targ[i]);
    }
    left->add(targetGroup);

    _oneAllGrp = std::make_shared<lib::GroupBox>();
    _oneAllGrp->title =
        _prop ? _prop->requiredMsgs().effectRange.value : "Effect range";
    auto range_layout = std::make_shared<lib::GridLayout>();
    range_layout->columns = 2U;
    range_layout->spacing = 2.0F;
    _oneAllGrp->set_layout(range_layout);
    _oneAllGrp->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Stretch,
                                 .minimum = {allWidth<C>(205.0F), 70.0F},
                                 .preferred = {allWidth<C>(205.0F), 70.0F}});
    _one = std::make_shared<lib::RadioButton>(
        _prop ? _prop->requiredMsgs().cardTargetOne.value : "One");
    _all = std::make_shared<lib::RadioButton>(
        _prop ? _prop->requiredMsgs().cardTargetAll.value : "All");
    _one->set_enabled(!_readOnly);
    _all->set_enabled(!_readOnly);
    _one->on_changed = [this](bool) { refreshWarning(); };
    _all->on_changed = [this](bool) { refreshWarning(); };
    _one->set_layout_item({.row = 0U, .column = 0U,
                           .horizontal = lib::Align::Start,
                           .vertical = lib::Align::Center, .grow = 1.0F});
    _all->set_layout_item({.row = 0U, .column = 1U,
                           .horizontal = lib::Align::Start,
                           .vertical = lib::Align::Center, .grow = 1.0F});
    _oneAllGrp->add(_one);
    _oneAllGrp->add(_all);
    left->add(_oneAllGrp);

    const auto visualGroup = std::make_shared<lib::GroupBox>();
    visualGroup->title = _prop ? _prop->requiredMsgs().effectVisual.value : "Visual";
    auto visual_layout = std::make_shared<lib::GridLayout>();
    visual_layout->columns = 2U;
    visual_layout->spacing = 2.0F;
    visualGroup->set_layout(visual_layout);
    visualGroup->set_layout_item({.horizontal = lib::Align::Stretch,
                                  .vertical = lib::Align::Stretch,
                                  .minimum = {allWidth<C>(245.0F), 112.0F},
                                  .preferred = {allWidth<C>(245.0F), 112.0F}});
    for (std::size_t i = 0; i < visualTypes.size(); ++i) {
        _vis[i] = std::make_shared<lib::RadioButton>(
            _prop ? _prop->requiredMsgs().cardVisualName(visualTypes[i])
                  : enumLabel("Visual", visualTypes[i]));
        _vis[i]->set_enabled(!_readOnly);
        _vis[i]->on_changed = [this, i](bool checked) {
            if (!checked) return;
            for (std::size_t j = 0; j < _vis.size(); ++j)
                if (j != i && _vis[j]) _vis[j]->checked = false;
            refreshWarning();
        };
        _vis[i]->set_layout_item({.row = i / 2U, .column = i % 2U,
                                  .horizontal = lib::Align::Start,
                                  .vertical = lib::Align::Center,
                                  .grow = 1.0F});
        visualGroup->add(_vis[i]);
    }
    right->add(visualGroup);

    const auto premiumGroup = std::make_shared<lib::GroupBox>();
    premiumGroup->title =
        _prop ? _prop->requiredMsgs().cardPremium.value : "Premium";
    verticalLayout(premiumGroup, 2.0F);
    premiumGroup->set_layout_item({.horizontal = lib::Align::Stretch,
                                   .vertical = lib::Align::Stretch,
                                   .minimum = {allWidth<C>(245.0F), 90.0F},
                                   .preferred = {allWidth<C>(245.0F), 90.0F}});
    for (std::size_t i = 0; i < premiumTypes.size(); ++i) {
        _prem[i] = std::make_shared<lib::RadioButton>(
            _prop ? _prop->requiredMsgs().premiumName(premiumTypes[i])
                  : enumLabel("Premium", premiumTypes[i]));
        _prem[i]->set_enabled(!_readOnly);
        _prem[i]->on_changed = [this, i](bool checked) {
            if (!checked) return;
            for (std::size_t j = 0; j < _prem.size(); ++j)
                if (j != i && _prem[j]) _prem[j]->checked = false;
        };
        _prem[i]->set_layout_item({.horizontal = lib::Align::Start,
                                   .vertical = lib::Align::Center});
        premiumGroup->add(_prem[i]);
    }
    right->add(premiumGroup);

    const auto success_group = std::make_shared<lib::GroupBox>();
    success_group->title = _prop ? _prop->requiredMsgs().successRate.value : "Success rate";
    auto success_layout = std::make_shared<lib::GridLayout>();
    success_layout->columns = 3U;
    success_layout->spacing = 6.0F;
    success_group->set_layout(success_layout);
    success_group->set_layout_item({.horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Stretch,
                                    .minimum = {0.0F, 72.0F},
                                    .preferred = {0.0F, 72.0F}});
    const auto failure = std::make_shared<lib::Label>("絶対失敗\n(-5)");
    failure->set_layout_item({.row = 0U, .column = 0U,
                              .vertical = lib::Align::Center});
    _sucRate = std::make_shared<lib::Slider>();
    _sucRate->minimum = -successRateMax;
    _sucRate->maximum = successRateMax;
    _sucRate->step = 1.0;
    // DのSWT Scaleは一刻みごとに目盛りを描く。-5～+5の各値に一つずつ付ける。
    _sucRate->tick_interval = 1.0;
    if (!lib::editor_window_host_options().all_mode) _sucRate->page_step = successRateMax;
    _sucRate->value = 0.0;
    _sucRate->on_changed = [this](double) { refreshWarning(); };
    _sucRate->set_layout_item({.row = 0U, .column = 1U,
                               .horizontal = lib::Align::Stretch,
                               .vertical = lib::Align::Center,
                               .grow = 1.0F});
    const auto success = std::make_shared<lib::Label>("絶対成功\n(+5)");
    success->set_layout_item({.row = 0U, .column = 2U,
                              .horizontal = lib::Align::End,
                              .vertical = lib::Align::Center});
    success_group->add(failure);
    success_group->add(_sucRate);
    success_group->add(success);
    panel->add(success_group);
    addPage(_prop ? _prop->requiredMsgs().cardProps.value : "Card properties", panel);
}

template <typename C>
void EffectCardDialog<C>::constructBehavior() {
    if constexpr (std::is_same_v<C, card::BeastCard>) {
        const auto panel = std::make_shared<lib::Panel>();
        auto behavior_layout = std::make_shared<lib::GridLayout>();
        behavior_layout->columns = 1U;
        behavior_layout->spacing = 6.0F;
        behavior_layout->padding = {6.0F, 6.0F, 6.0F, 6.0F};
        panel->set_layout(behavior_layout);
        const auto conditions = std::make_shared<lib::GroupBox>();
        conditions->title =
            _prop ? _prop->requiredMsgs().invocationCondition.value
                   : "Invocation condition";
        auto condition_layout = std::make_shared<lib::GridLayout>();
        condition_layout->columns = 4U;
        condition_layout->equal_width = true;
        condition_layout->spacing = 2.0F;
        conditions->set_layout(condition_layout);
        conditions->set_layout_item({
            .row = 0U, .column = 0U,
            .horizontal = lib::Align::Stretch,
            .vertical = lib::Align::Stretch,
            .minimum = {0.0F, 218.0F},
            .preferred = {0.0F, 218.0F}});
        for (std::size_t i = 0; i < statusTypes.size(); ++i) {
            _invokeCond[i] = std::make_shared<lib::CheckBox>(
                _prop ? _prop->requiredMsgs().statusName(statusTypes[i])
                      : enumLabel("Status", statusTypes[i]));
            _invokeCond[i]->set_enabled(!_readOnly);
            _invokeCond[i]->on_changed = [this](bool) { refDataVersion(); };
            _invokeCond[i]->set_layout_item({
                .row = i / 4U, .column = i % 4U,
                .horizontal = lib::Align::Stretch,
                .vertical = lib::Align::Center});
            conditions->add(_invokeCond[i]);
        }
        panel->add(conditions);
        const auto removal = std::make_shared<lib::GroupBox>();
        removal->title =
            _prop ? _prop->requiredMsgs().removalCondition.value : "Removal condition";
        verticalLayout(removal, 2.0F);
        _removeWithUncons = std::make_shared<lib::CheckBox>(
            _prop ? _prop->requiredMsgs().removeWithUnconscious.value
                  : "Remove with unconscious");
        _removeWithUncons->set_enabled(!_readOnly);
        _removeWithUncons->on_changed = [this](bool) { refDataVersion(); };
        removal->add(_removeWithUncons);
        removal->set_layout_item({
            .row = 1U, .column = 0U,
            .horizontal = lib::Align::Stretch,
            .vertical = lib::Align::Center,
            .minimum = {0.0F, 58.0F},
            .preferred = {0.0F, 58.0F}});
        panel->add(removal);

        // DのcreateStatusHintは、編集可能
        // な条件表の下に四つの集約判定の定義を表示する。
        const auto hint = std::make_shared<lib::GroupBox>();
        hint->title = _prop ? _prop->requiredMsgs().stateHint.value : "Hint";
        verticalLayout(hint, 1.0F);
        const std::array<String, 4> hint_lines{
            _prop ? _prop->requiredMsgs().statusActive.value
                  : "Active status definition",
            _prop ? _prop->requiredMsgs().statusInactive.value
                  : "Inactive status definition",
            _prop ? _prop->requiredMsgs().statusAlive.value
                  : "Alive status definition",
            _prop ? _prop->requiredMsgs().statusDead.value
                  : "Dead status definition"};
        for (const auto& line : hint_lines)
            hint->add(std::make_shared<lib::Label>(line));
        hint->set_layout_item({
            .row = 2U, .column = 0U,
            .horizontal = lib::Align::Stretch,
            .vertical = lib::Align::Stretch,
            .minimum = {0.0F, 92.0F}, .grow = 1.0F});
        panel->add(hint);
        addPage(_prop ? _prop->requiredMsgs().behaviorOfBeastCard.value
                      : "Beast behavior",
                panel);
    }
}

template <typename C>
void EffectCardDialog<C>::constructKeyCode() {
    const std::weak_ptr<bool> host_life = _callbackLifetime;
    const std::weak_ptr<bool> active_life = _keyCodeHostActive;
    const auto host_alive = [host_life, active_life] {
        const auto alive = host_life.lock();
        const auto active = active_life.lock();
        return alive && *alive && active && *active;
    };
    const auto panel = std::make_shared<lib::Panel>();
    fillPage(panel);
    auto split = std::make_shared<lib::SplitView>();
    split->orientation = lib::Orientation::Horizontal;
    split->weights = {
        static_cast<float>(std::max(1, _context.vars ?
            _context.vars->seKeyCodeSashL.value : 36)),
        static_cast<float>(std::max(1, _context.vars ?
            _context.vars->seKeyCodeSashR.value : 64))};
    split->on_weights_changed = [this, host_alive](const std::vector<float>& weights) {
        if (!host_alive() || !_context.vars || weights.size() < 2U) return;
        _context.vars->seKeyCodeSashL.value =
            std::max(1, static_cast<std::int32_t>(std::lround(weights[0])));
        _context.vars->seKeyCodeSashR.value =
            std::max(1, static_cast<std::int32_t>(std::lround(weights[1])));
    };
    split->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch, .grow = 1.0F});
    panel->add(split);
    std::shared_ptr<lib::CheckBox> initialEffect;
    static_cast<void>(createEffectSoundPanel(
        split, _context, _summ,
        _prop ? _prop->requiredMsgs().se.value : "Sound",
                                             _readOnly, false, initialEffect,
                                             _se1, _se2));

    keycodeview::KeyCodeViewConfig config;
    config.system = _context.prop ? &_context.prop->sys() : nullptr;
    config.readOnly = _readOnly;
    config.canDuplicate = true;
    config.withIgnitionType = false;
    config.summaryLegacy = _summ && _summ->legacy();
    config.dataVersion = _summ ? _summ->dataVersion() : String{"4"};
    config.services = _context.services;
    config.dialogOwner = _window;
    config.editorCommons = _context.editor_commons;
    if (_context.editor_props) {
        auto& images = _context.editor_props->images();
        config.resources.keyCodeImage = images.load(StringView{"key_code.png"});
        config.resources.addKeyCodeImage = images.load(StringView{"add_key_code.png"});
        config.resources.delKeyCodeImage = images.load(StringView{"del_key_code.png"});
        config.resources.warningImage = images.load(StringView{"warning.png"});
        config.resources.addByFeaturesImage =
            images.load(StringView{"add_key_codes_by_features.png"});
        config.resources.upImage = images.menu(types::MenuID::Up);
        config.resources.downImage = images.menu(types::MenuID::Down);
    }
    if (_prop) {
        const auto& messages = _prop->requiredMsgs();
        config.keyCodesMaxLegacy = static_cast<std::size_t>(
            std::max(0, _prop->looks().keyCodesMaxLegacy()));
        config.resources.keyCodeLabel = messages.keyCode.value;
        config.resources.addKeyCodeText = messages.addKeyCode.value;
        config.resources.delKeyCodeText = messages.delKeyCode.value;
        config.resources.addByFeaturesText = messages.menuTextAddKeyCodesByFeatures.value;
        config.resources.upText = messages.menuTextUp.value;
        config.resources.downText = messages.menuTextDown.value;
        config.resources.timingLabels = {
            messages.keyCodeTimingUse.value,
            messages.keyCodeTimingSuccess.value,
            messages.keyCodeTimingFailure.value,
            messages.keyCodeTimingHasNot.value};
        config.resources.selectFeaturesText = messages.selectFeatures.value;
        config.resources.selectFeaturesHint = messages.selectFeaturesHint.value;
        config.resources.addKeyCodesErrorText = messages.addKeyCodesError.value;
        config.resources.addKeyCodesErrorDesc = messages.addKeyCodesErrorDesc.value;
        config.resources.searchErrorKeyCodeMatchingAll =
            messages.searchErrorKeyCodeMatchingAll.value;
        config.resources.warningHasNotKeyCode = messages.warningHasNotKeyCode.value;
        config.resources.warningRunAwayCard = messages.warningRunAwayCard.value;
        config.resources.warningKeyCodeCount = messages.warningKeyCodeCount.value;
        const auto* prop = _prop;
        const auto summary = _summ;
        config.warningProvider = [prop, summary, host_alive](const StringView value) {
            if (!host_alive()) return lib::Array<String>{};
            return warning::sjisWarnings(*prop, summary, value,
                                         prop->requiredMsgs().keyCode.value);
        };
    }
    if (_context.vars)
        config.undoMax = static_cast<std::size_t>(
            std::max(0, _context.vars->undoMaxEtc.value));
    if (_context.editor_props) {
        auto* editor_props = _context.editor_props;
        // Dは特徴選択の使用時に現在のスキンを読む。既存の状
        // 態から解決し、refSkinの通知順に依存せず、強
        // 制・読み取り専用スキンの通常の優先順位を維持する。
        const auto current_skin = [this, host_alive]() -> SkinPtr {
            if (!host_alive()) return {};
            const auto context = _context;
            const auto summary = _summ;
            return context.findSkin(summary.get());
        };
        config.keyCodesByFeatures = [editor_props, current_skin, host_alive] {
            if (!host_alive()) return lib::Array<keycodeview::KeyCodeByFeature>{};
            const auto skin = current_skin();
            return host_alive() ? ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                                      editor_props->keyCodesByFeatures(
                                          skin ? skin->type() : String{}))
                                : lib::Array<keycodeview::KeyCodeByFeature>{};
        };
        config.keyCodesByMotions = [editor_props, current_skin, host_alive] {
            if (!host_alive()) return lib::Array<keycodeview::KeyCodeByMotion>{};
            const auto skin = current_skin();
            return host_alive() ? ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                                      editor_props->keyCodesByMotions(
                                          skin ? skin->type() : String{}))
                                : lib::Array<keycodeview::KeyCodeByMotion>{};
        };
        config.standardKeyCodes = [editor_props, current_skin, host_alive] {
            if (!host_alive()) return lib::Array<String>{};
            const auto skin = current_skin();
            return host_alive() ? ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                                      editor_props->standardKeyCodes(
                                          skin ? skin->type() : String{}))
                                : lib::Array<String>{};
        };
    }
    if (_context.services.clipboard) {
        const auto clipboard = _context.services.clipboard;
        config.copyText = [clipboard](const StringView value) {
            lib::ClipboardData data;
            data.text = String(value);
            clipboard->write(data);
        };
    }
    // 共通のKeyCodeViewやWindowは、このダイアログより長く保持され
    // 得る。借用ホストのプロバイダーは、失効した所有ダイアログに触る前に停止する。
    config.summaryLegacyProvider = [this, host_alive] {
        return host_alive() && _summ && _summ->legacy();
    };
    config.dataVersionProvider = [this, host_alive] {
        if (!host_alive()) return String{};
        return _summ ? _summ->dataVersion() : String{"4"};
    };
    config.keyCodesMaxLegacyProvider = [this, host_alive] {
        if (!host_alive()) return std::size_t{0U};
        return static_cast<std::size_t>(
            std::max(0, _prop ? _prop->looks().keyCodesMaxLegacy() : 5));
    };
    config.undoMaxProvider = [this, host_alive] {
        if (!host_alive()) return std::size_t{0U};
        return static_cast<std::size_t>(
            std::max(0, _context.vars ? _context.vars->undoMaxEtc.value : 100));
    };
    config.xmlCopyProvider = [this, host_alive] {
        return host_alive() && _context.vars && _context.vars->xmlCopy.value;
    };
    config.getMotions = [this, host_alive] {
        lib::Array<keycodeview::MotionInfo> result;
        if (!host_alive()) return result;
        if (_motions) {
            for (const auto& value : _motions->motions())
                if (value) result.emplace_back(*value);
        }
        return result;
    };
    config.catchModify = [this, host_alive] { return host_alive() && !_ignoreMod; };
    _keyCodes = std::make_shared<keycodeview::KeyCodeView>(
        split.get(), _readOnly ? keycodeview::STYLE_READ_ONLY
                               : keycodeview::STYLE_NONE,
        std::move(config), [this, host_alive] { return host_alive() && !_ignoreMod; });
    const std::weak_ptr<bool> lifetime = _callbackLifetime;
    _keyCodes->addModifyListener([this, lifetime] {
        const auto alive = lifetime.lock();
        if (alive && *alive) abilityModified();
    });
    split->add(_keyCodes);

    _se1.modEvent.push_back([this] { abilityModified(); });
    _se1.loadedEvent.push_back([this] { refreshWarning(); });
    _se2.modEvent.push_back([this] { abilityModified(); });
    _se2.loadedEvent.push_back([this] { refreshWarning(); });
    addPage(_prop ? _prop->requiredMsgs().seAndKeyCode.value : "Sounds and key codes",
            panel);
}

template <typename C>
void EffectCardDialog<C>::constructSettings() {
    const auto panel = std::make_shared<lib::Panel>();
    verticalLayout(panel, 6.0F, {6.0F, 6.0F, 6.0F, 6.0F});
    const auto comment_group = std::make_shared<lib::GroupBox>();
    comment_group->title = "コメント";
    comment_group->set_layout(std::make_shared<lib::FillLayout>());
    _settingsComment = std::make_shared<lib::TextEdit>();
    _settingsComment->multiline = true;
    _settingsComment->set_layout_item({.horizontal = lib::Align::Stretch,
                                       .vertical = lib::Align::Stretch,
                                       .minimum = {allWidth<C>(320.0F), 90.0F}, .grow = 1.0F});
    _settingsComment->on_changed = [this](StringView) { refreshWarning(); };
    comment_group->add(_settingsComment);
    comment_group->set_layout_item({.horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Stretch,
                                    .minimum = {0.0F, 116.0F},
                                    .preferred = {0.0F, 116.0F}});
    panel->add(comment_group);

    const auto scope_group = std::make_shared<lib::GroupBox>();
    scope_group->title = "外部からの変数書き込み許可";
    verticalLayout(scope_group, 4.0F);
    scope_group->set_layout_item({.horizontal = lib::Align::Stretch,
                                  .vertical = lib::Align::Stretch,
                                  .minimum = {0.0F, 92.0F},
                                  .preferred = {0.0F, 92.0F}});
    _externalVariantWriteScopes = {
        "False", "True", "Scenario", "Author", "Scenario,Author", "Scenario_in"};
    _externalVariantWriteScope = std::make_shared<lib::ComboBox>();
    const std::array<String, 6> labels{
        "すべてのシナリオで不可", "すべてのシナリオで可能",
        "カードとシナリオ名が一致", "カードと作者名が一致",
        "カードとシナリオ名と作者が一致", "変数でシナリオを指定"};
    for (std::size_t i = 0; i < _externalVariantWriteScopes.size(); ++i)
        _externalVariantWriteScope->items.push_back(
            {labels[i], _externalVariantWriteScopes[i], {}, true});
    _externalVariantWriteScope->selected_index = 0U;
    _externalVariantWriteScope->on_selection_changed =
        [this](std::optional<std::size_t>) { refreshExternalVariantWriteEnabled(); };
    scope_group->add(_externalVariantWriteScope);

    _externalVariantWriteScenarioVar = std::make_shared<lib::ComboBox>();
    _externalVariantWriteScenarioVar->on_selection_changed =
        [this](std::optional<std::size_t>) { refreshWarning(); };
    scope_group->add(_externalVariantWriteScenarioVar);
    panel->add(scope_group);

    const auto inn_group = std::make_shared<lib::GroupBox>();
    inn_group->title = "宿から変更の許可";
    inn_group->set_layout(std::make_shared<lib::FillLayout>());
    inn_group->set_layout_item({.horizontal = lib::Align::Stretch,
                                .vertical = lib::Align::Stretch,
                                .minimum = {0.0F, 54.0F},
                                .preferred = {0.0F, 54.0F}});
    _externalVariantWriteYado = std::make_shared<lib::ComboBox>();
    _externalVariantWriteYado->items = {
        {"宿からは不可", "False", {}, true},
        {"宿からは可能", "True", {}, true}};
    _externalVariantWriteYado->selected_index = 0U;
    _externalVariantWriteYado->on_selection_changed =
        [this](std::optional<std::size_t>) { refreshWarning(); };
    inn_group->add(_externalVariantWriteYado);
    panel->add(inn_group);
    addPage("設定", panel);
}

template <typename C>
void EffectCardDialog<C>::loadDefaults() {
    _imgPath.images.clear();
    _imgPath.warnings.clear();
    if (_imageSelect) _imageSelect->setCardImages({}, false);
    if (_name) _name->text.clear();
    if (_desc) _desc->text.clear();
    if (_scenario) _scenario->text = _summ ? _summ->scenarioName() : String{};
    if (_author) _author->text = _summ ? _summ->author() : String{};
    if (_needSpell) _needSpell->checked = false;
    setRadioValue(_effTyp, effectTypes, types::EffectType::Physic);
    setRadioValue(_res, resistTypes, types::Resist::Avoid);
    _ability.physical(types::Physical::Dex);
    _ability.mental(types::Mental::Aggressive);

    if constexpr (std::is_same_v<C, card::SkillCard>) {
        setNumberDisplayValue(_level, 1.0);
        const auto selected_level = _level
                                        ? static_cast<std::uint32_t>(_level->value)
                                        : 1U;
        _lastAutoPrice = skillPrice(selected_level);
        setNumberDisplayValue(_price, static_cast<double>(_lastAutoPrice));
    } else if constexpr (std::is_same_v<C, card::ItemCard>) {
        if (_useCount) _useCount->value = 0;
        if (_price) _price->value = 0;
    } else {
        if (_useCount) _useCount->value = 0;
        if (_price) _price->value = beastPrice();
        if (_showStyle)
            (void)lib::select_combo_item(*_showStyle, 1U, false);
        for (auto& button : _invokeCond)
            if (button) button->checked = false;
        if (_invokeCond[static_cast<std::size_t>(types::Status::Alive)])
            _invokeCond[static_cast<std::size_t>(types::Status::Alive)]->checked = true;
        if (_removeWithUncons) _removeWithUncons->checked = true;
    }

    std::vector<int> zero(editableEnhancements.size(), 0);
    writeEnhanceControls(_useModTbl, zero, _useModR, _useModS);
    writeEnhanceControls(_hasModTbl, zero, _hasModR, _hasModS);
    setRadioValue(_targ, targetTypes, types::CardTarget::None);
    if (_one) _one->checked = true;
    if (_all) _all->checked = false;
    setRadioValue(_vis, visualTypes, types::CardVisual::None);
    setRadioValue(_prem, premiumTypes, types::Premium::Normal);
    if (_sucRate) _sucRate->value = 0.0;
    _se1.setPath({});
    _se2.setPath({});
    _se1.volume = _se2.volume = 100U;
    _se1.loopCount = _se2.loopCount = 1U;
    _se1.fadeIn = _se2.fadeIn = _se1.channel = _se2.channel = 0U;
    _se1.syncControls();
    _se2.syncControls();
    if (_keyCodes) _keyCodes->setKeyCodes({});
    if (_settingsComment) _settingsComment->text.clear();
    selectExternalVariantWriteScope("False");
    refreshExternalVariantWriteScenarioVars({});
    if (_externalVariantWriteYado)
        (void)lib::select_combo_item(*_externalVariantWriteYado, 0U, false);
    refreshEnblOneAll();
    refreshExternalVariantWriteEnabled();
}

template <typename C>
void EffectCardDialog<C>::loadEnhanceControls(const C& value) {
    std::vector<int> use(editableEnhancements.size(), 0);
    for (std::size_t i = 0; i < editableEnhancements.size(); ++i)
        use[i] = value.enhance(editableEnhancements[i]);
    writeEnhanceControls(_useModTbl, use, _useModR, _useModS);
    if constexpr (std::is_same_v<C, card::ItemCard>) {
        std::vector<int> owner(editableEnhancements.size(), 0);
        for (std::size_t i = 0; i < editableEnhancements.size(); ++i)
            owner[i] = value.enhanceOwner(editableEnhancements[i]);
        writeEnhanceControls(_hasModTbl, owner, _hasModR, _hasModS);
    }
}

template <typename C>
void EffectCardDialog<C>::loadCard(const C& value) {
    _imgPath.images = value.paths();
    if (_imageSelect) _imageSelect->setCardImages(_imgPath.images, false);
    if (_imagePathEdit) {
        _imagePathEdit->text.clear();
        for (const auto& image : _imgPath.images) {
            if (!image || image->type != card::CardImageType::File) continue;
            if (!_imagePathEdit->text.empty()) _imagePathEdit->text.push_back('\n');
            _imagePathEdit->text += image->path();
        }
    }
    if (_desc) _desc->text = value.desc();
    if (_scenario) _scenario->text = value.scenario();
    if (_author) _author->text = value.author();
    if (_name) _name->text = value.name();
    if (_needSpell) _needSpell->checked = value.spell();
    setRadioValue(_effTyp, effectTypes, value.effectType());
    setRadioValue(_res, resistTypes, value.resist());
    _ability.physical(value.physical());
    _ability.mental(value.mental());

    if constexpr (std::is_same_v<C, card::SkillCard>) {
        setNumberDisplayValue(_level, static_cast<double>(value.level()));
        _lastAutoPrice = skillPrice(value.level());
        setNumberDisplayValue(_price, static_cast<double>(value.price()));
    } else if constexpr (std::is_same_v<C, card::ItemCard>) {
        if (_useCount) _useCount->value = value.useLimit();
        if (_price) _price->value = value.price();
    } else {
        if (_useCount) _useCount->value = value.useLimit();
        if (_price) _price->value = value.price();
        if (_showStyle) {
            const auto it = std::find(showStyles.begin(), showStyles.end(), value.showStyle());
            (void)lib::select_combo_item(
                *_showStyle,
                it == showStyles.end() ? std::optional<std::size_t>{0U}
                                       : std::optional<std::size_t>{
                                             static_cast<std::size_t>(it - showStyles.begin())},
                false);
        }
        for (auto& button : _invokeCond)
            if (button) button->checked = false;
        for (const auto status : value.invocationCondition()) {
            const auto found = std::find(statusTypes.begin(), statusTypes.end(),
                                         status);
            if (found == statusTypes.end()) continue;
            const auto index = static_cast<std::size_t>(found - statusTypes.begin());
            if (_invokeCond[index]) _invokeCond[index]->checked = true;
        }
        if (_removeWithUncons) _removeWithUncons->checked = value.removeWithUnconscious();
    }

    loadEnhanceControls(value);
    _motions->setMotions(duplicateMotions(value));
    setRadioValue(_targ, targetTypes, value.target());
    if (_one) _one->checked = !value.allRange();
    if (_all) _all->checked = value.allRange();
    refreshEnblOneAll();
    setRadioValue(_vis, visualTypes, value.visual());
    setRadioValue(_prem, premiumTypes, value.premium());
    if (_sucRate) _sucRate->value = value.successRate();

    _se1.setPath(value.soundPath1());
    _se1.volume = value.volume1();
    _se1.loopCount = value.loopCount1();
    _se2.setPath(value.soundPath2());
    _se2.volume = value.volume2();
    _se2.loopCount = value.loopCount2();
    _se1.syncControls();
    _se2.syncControls();
    if (_keyCodes) _keyCodes->setKeyCodes(value.keyCodes());
    if (_settingsComment) _settingsComment->text = value.settingsComment();
    selectExternalVariantWriteScope(value.externalVariantWriteScope());
    refreshExternalVariantWriteScenarioVars(value.externalVariantWriteScenarioVar());
    if (_externalVariantWriteYado)
        (void)lib::select_combo_item(
            *_externalVariantWriteYado,
            value.externalVariantWriteYado() ? std::optional<std::size_t>{1U}
                                             : std::optional<std::size_t>{0U},
            false);
    refreshExternalVariantWriteEnabled();
}

template <typename C>
void EffectCardDialog<C>::refCard(const CardPtr& value) {
    if (_card && value && _card.get() != value.get()) return;
    // 新規ダイアログの既定値はUIで扱い、モデルのコンストラクターには持ち込まない。
    if (_creationMode && _creationInitialized) return;
    {
        _ignoreMod = true;
        struct Restore {
            bool* flag;
            std::weak_ptr<bool> life;
            ~Restore() { const auto alive = life.lock(); if (alive && *alive) *flag = false; }
        } restore{&_ignoreMod, _callbackLifetime};
        const std::weak_ptr<bool> life = _callbackLifetime;
        const auto scenarioMenu = _scenarioTextMenu;
        const auto authorMenu = _authorTextMenu;
        if (_card && !_creationMode) loadCard(*_card);
        else loadDefaults();
        // DのText.setTextはrefCard中にもModifyを通知
        // する。共通の項目読み込みは直接文字列を代入するため、最初の編集前に同
        // じ履歴状態を公開する。省くと取り消しで以前の空のコントロールに戻る。
        const auto alive = [&life] {
            const auto token = life.lock();
            return token && *token;
        };
        if (!alive()) return;
        if (scenarioMenu) scenarioMenu->modifyText();
        if (!alive()) return;
        if (authorMenu) authorMenu->modifyText();
        if (!alive()) return;
        _creationInitialized = true;
    }
    updateTitle(_card);
    refreshWarning();
}

template <typename C>
void EffectCardDialog<C>::card(CardPtr value) {
    // Dの公開設定関数は対象だけを変える。保留中の操作部の内容を捨て
    // たい呼び出し側は、その後にrefCardを明示的に要求する。
    _card = std::move(value);
}

template <typename C>
void EffectCardDialog<C>::updateTitle(const CardPtr& value) {
    if (!_window) return;
    if (value) {
        if (!_card || _card.get() != value.get()) return;
        _window->title = cardTitle(*value, _prop);
    } else if constexpr (std::is_same_v<C, card::SkillCard>) {
        _window->title = _prop ? _prop->requiredMsgs().dlgTitNewSkill.value : "New skill";
    } else if constexpr (std::is_same_v<C, card::ItemCard>) {
        _window->title = _prop ? _prop->requiredMsgs().dlgTitNewItem.value : "New item";
    } else {
        _window->title = _prop ? _prop->requiredMsgs().dlgTitNewBeast.value : "New beast";
    }
}

template <typename C>
void EffectCardDialog<C>::resetSource() {
    if (_readOnly || !_summ) return;
    {
        _ignoreMod = true;
        struct Restore {
            bool* flag;
            std::weak_ptr<bool> life;
            ~Restore() { const auto alive = life.lock(); if (alive && *alive) *flag = false; }
        } restore{&_ignoreMod, _callbackLifetime};
        if (_scenario) _scenario->text = _summ->scenarioName();
        if (_author) _author->text = _summ->author();
        _scenarioTM.reset();
        _authorTM.reset();
        if (_scenarioTextMenu) _scenarioTextMenu->reset();
        if (_authorTextMenu) _authorTextMenu->reset();
    }
    refreshWarning();
}

template <typename C>
void EffectCardDialog<C>::updatePriceByLevel(const int value) {
    if constexpr (std::is_same_v<C, card::SkillCard>) {
        const auto level = static_cast<std::uint32_t>(std::max(0, value));
        const auto automatic = skillPrice(level);
        if (_price && static_cast<std::uint32_t>(_price->value) == _lastAutoPrice)
            setNumberDisplayValue(_price, static_cast<double>(automatic));
        _lastAutoPrice = automatic;
    } else {
        (void)value;
    }
}

template <typename C>
void EffectCardDialog<C>::updateUseLimitMax() {
    if constexpr (std::is_same_v<C, card::ItemCard>) {
        if (!_useCountCur || !_useCount) return;
        _useCountCur->maximum = _useCount->value;
        if (_useCountIsMax && _useCountIsMax->checked)
            _useCountCur->value = _useCount->value;
    }
}

template <typename C>
void EffectCardDialog<C>::updateEnabledImpl() {
    const bool enabled = !_readOnly;
    if (_imageSelect) _imageSelect->enabled(true);
    setEditable(_name, enabled);
    setEditable(_desc, enabled);
    // DのSWT.READ_ONLYは、入力元テキストのフォーカス、選択、コピーを維持する。
    for (const auto& text : {_scenario, _author}) {
        if (!text) continue;
        text->set_enabled(true);
        text->read_only = _readOnly;
    }
    setEditable(_settingsComment, enabled);
    setEditable(_level, enabled);
    setEditable(_useCount, enabled);
    setEditable(_useCountCur, enabled);
    setEditable(_price, enabled);
    setEditable(_sucRate, enabled);
    if (_needSpell) _needSpell->set_enabled(enabled);
    for (const auto& button : _effTyp) if (button) button->set_enabled(enabled);
    for (const auto& button : _res) if (button) button->set_enabled(enabled);
    for (const auto& button : _targ) if (button) button->set_enabled(enabled);
    for (const auto& button : _vis) if (button) button->set_enabled(enabled);
    for (const auto& button : _prem) if (button) button->set_enabled(enabled);
    if (_one) _one->set_enabled(enabled && _oneAllGrp && _oneAllGrp->enabled());
    if (_all) _all->set_enabled(enabled && _oneAllGrp && _oneAllGrp->enabled());
    if (_externalVariantWriteScope) _externalVariantWriteScope->set_enabled(enabled);
    if (_externalVariantWriteYado) _externalVariantWriteYado->set_enabled(enabled);
    if (_keyCodes) {
        _keyCodes->setReadOnly(_readOnly);
        _keyCodes->setEnabled(enabled);
    }
    if (_motions) _motions->setReadOnly(_readOnly);
    // 読み取り専用の選択部は、自身のスタイルで編集を拒否しつつ試聴を維持する。
    _se1.setEnabled(true);
    _se2.setEnabled(true);

    if constexpr (std::is_same_v<C, card::BeastCard>) {
        const bool legacy = _summ && _summ->legacy();
        bool aliveOnly = true;
        bool aliveSelected = false;
        for (std::size_t i = 0; i < _invokeCond.size(); ++i)
            if (_invokeCond[i] && _invokeCond[i]->checked) {
                if (statusTypes[i] == types::Status::Alive) aliveSelected = true;
                else aliveOnly = false;
            }
        aliveOnly = aliveOnly && aliveSelected;
        const bool conditionEnabled = enabled && (!legacy || !aliveOnly);
        for (const auto& button : _invokeCond)
            if (button) button->set_enabled(conditionEnabled);
        if (_removeWithUncons)
            _removeWithUncons->set_enabled(enabled && (!legacy || !_removeWithUncons->checked));
        bool showStyleEnabled = enabled;
        if (legacy && _showStyle && _showStyle->selected_index &&
            *_showStyle->selected_index == 1U)
            showStyleEnabled = false;
        if (_showStyle) _showStyle->set_enabled(showStyleEnabled);
    }
    refreshExternalVariantWriteEnabled();
}

template <typename C>
void EffectCardDialog<C>::updateEnabled() {
    updateEnabledImpl();
}

template <typename C>
void EffectCardDialog<C>::refreshEnblOneAll() {
    bool selected = false;
    for (const auto target : {types::CardTarget::Party, types::CardTarget::Enemy,
                              types::CardTarget::Both}) {
        const auto i = static_cast<std::size_t>(target);
        if (_targ[i] && _targ[i]->checked) selected = true;
    }
    if (_oneAllGrp) _oneAllGrp->set_enabled(selected);
    if (_one) _one->set_enabled(!_readOnly && selected);
    if (_all) _all->set_enabled(!_readOnly && selected);
}

template <typename C>
void EffectCardDialog<C>::refreshExternalVariantWriteEnabled() {
    const bool scenarioIn = externalVariantWriteScopeSelection() == "Scenario_in";
    if (_externalVariantWriteScenarioVar)
        _externalVariantWriteScenarioVar->set_enabled(!_readOnly && scenarioIn);
    if (_externalVariantWriteYado) _externalVariantWriteYado->set_enabled(!_readOnly);
    refreshWarning();
}

template <typename C>
String EffectCardDialog<C>::externalVariantWriteScopeSelection() const {
    if (!_externalVariantWriteScope || !_externalVariantWriteScope->selected_index)
        return "False";
    const auto index = *_externalVariantWriteScope->selected_index;
    return index < _externalVariantWriteScopes.size()
               ? _externalVariantWriteScopes[index]
               : String{"False"};
}

template <typename C>
void EffectCardDialog<C>::selectExternalVariantWriteScope(
    const StringView accessScope) {
    if (!_externalVariantWriteScope) return;
    auto it = std::find(_externalVariantWriteScopes.begin(),
                        _externalVariantWriteScopes.end(), accessScope);
    (void)lib::select_combo_item(
        *_externalVariantWriteScope,
        it == _externalVariantWriteScopes.end()
            ? std::optional<std::size_t>{0U}
            : std::optional<std::size_t>{
                  static_cast<std::size_t>(it - _externalVariantWriteScopes.begin())},
        false);
}

template <typename C>
void EffectCardDialog<C>::refreshExternalVariantWriteScenarioVars(
    const StringView selected) {
    if (!_externalVariantWriteScenarioVar) return;
    std::vector<String> values{""};
    const auto source = _draft ? _draft : _card;
    if (source) {
        if (const auto root = source->flagDirRoot()) {
            for (const auto& value : root->allVariants())
                if (value) values.push_back(value->path());
        }
    }
    if (!selected.empty() &&
        std::find(values.begin(), values.end(), selected) == values.end())
        values.push_back(String(selected));
    std::vector<lib::ChoiceItem> items;
    items.reserve(values.size());
    for (const auto& value : values)
        items.push_back({value, value, {}, true});
    auto it = std::find(values.begin(), values.end(), selected);
    const auto selection =
        it == values.end() ? std::optional<std::size_t>{0U}
                           : std::optional<std::size_t>{
                                 static_cast<std::size_t>(it - values.begin())};
    (void)lib::replace_combo_items(_externalVariantWriteScenarioVar,
                                   std::move(items), selection,
                                   String(selected));
}

template <typename C>
void EffectCardDialog<C>::refreshWarningsForCard() {
    static const ::cwxeditor::cwx::msgs::Msgs defaultMessages;
    const auto& messages = _prop ? _prop->requiredMsgs() : defaultMessages;
    _warnings.clear();
    const auto append = [this](const auto& values) {
        _warnings.insert(_warnings.end(), values.begin(), values.end());
    };
    if (_prop && _name)
        append(warning::sjisWarnings(*_prop, _summ, _name->text, messages.name.value));
    if (_name && _prop) {
        const auto limit = _prop->looks().nameLimit();
        bool exceeds = false;
        if (_context.services.graphics) {
            const auto font = _name->font().value_or(lib::effective_editor_widget_font());
            const auto width = _context.services.graphics->measure_text(
                _name->text, font, 0.0F).width;
            const auto unit = _context.services.graphics->measure_text(
                " ", font, 0.0F).width;
            exceeds = unit > 0.0F && width / unit > static_cast<float>(limit);
        } else {
            exceeds = _name->text.size() > limit;
        }
        if (exceeds)
            _warnings.push_back(::cwxeditor::cwx::utils::tryFormat(
                _prop->requiredMsgs().warningNameLenOver.value, limit, limit / 2));
    }
    if (_imageSelect) append(_imageSelect->warnings());
    if (_effTyp[static_cast<std::size_t>(types::EffectType::None)] &&
        _effTyp[static_cast<std::size_t>(types::EffectType::None)]->checked)
        _warnings.push_back(messages.warningEffectTypeNone.value);
    if (_prop) {
        if (_desc) append(warning::sjisWarnings(
            *_prop, _summ, _desc->text, messages.desc.value));
        if (_scenario) append(warning::sjisWarnings(
            *_prop, _summ, _scenario->text, messages.sourceScenario.value));
        if (_author) append(warning::sjisWarnings(
            *_prop, _summ, _author->text, messages.sourceAuthor.value));
    }
    if (_motions) {
        _warnings.insert(_warnings.end(), _motions->warnings().begin(),
                         _motions->warnings().end());
        for (const auto& motion : _motions->motions()) {
            if (motion && motion->type() == types::MType::VanishTarget &&
                motion->element() != types::Element::Miracle) {
                _warnings.push_back(messages.warningVanishCast.value);
                break;
            }
        }
    }
    if constexpr (std::is_same_v<C, card::BeastCard>) {
        if (_showStyle && _showStyle->selected_index && _summ &&
            *_showStyle->selected_index != 1U && !_summ->isTargetVersion("4"))
            _warnings.push_back(messages.warningShowStyleForBeastCard.value);
        bool aliveOnly = true;
        bool aliveSelected = false;
        for (std::size_t i = 0; i < _invokeCond.size(); ++i)
            if (_invokeCond[i] && _invokeCond[i]->checked) {
                if (statusTypes[i] == types::Status::Alive) aliveSelected = true;
                else aliveOnly = false;
            }
        aliveOnly = aliveOnly && aliveSelected;
        if (_summ && !_summ->isTargetVersion("3") && !aliveOnly)
            _warnings.push_back(messages.warningInvocationCondition.value);
        if (_summ && !_summ->isTargetVersion("3") &&
            _removeWithUncons && !_removeWithUncons->checked)
            _warnings.push_back(messages.warningRemoveWithUnconscious.value);
        if (_invokeCond[static_cast<std::size_t>(types::Status::Unconscious)] &&
            _invokeCond[static_cast<std::size_t>(types::Status::Unconscious)]->checked &&
            _removeWithUncons && _removeWithUncons->checked)
            _warnings.push_back(messages.warningUnconsciousCondition.value);
    }
    const auto append_sound_warnings = [this, &messages, &append](
        const SoundSelect& sound) {
        const bool legacy = _summ && _summ->legacy();
        if (legacy && !sound.filePath.empty() && !sound.selectedDefDir)
            _warnings.push_back(messages.warningNotDefaultSE.value);
        if (_prop) append(skin::Skin::warningSE(
            *_prop, sound.filePath, legacy,
            _context.vars ? _context.vars->targetVersion.value : String{}));
        append(sound.warnings);
    };
    append_sound_warnings(_se1);
    append_sound_warnings(_se2);
    if (_card && !_card->trees().empty() && _scenario && _author &&
        _summ && (_scenario->text != _summ->scenarioName() ||
                  _author->text != _summ->author()))
        _warnings.push_back(messages.diffSource.value);
    if (_keyCodes) {
        const auto values = _keyCodes->warnings();
        _warnings.insert(_warnings.end(), values.begin(), values.end());
    }
    if (externalVariantWriteScopeSelection() == "Scenario_in" &&
        _externalVariantWriteScenarioVar &&
        !_externalVariantWriteScenarioVar->edit_text.empty()) {
        // 選択部は直下の子の名前ではなく、完全な変数パスを保存する
        // 。現在のカードとそのローカルカウンターに対して解決する。
        const auto root = _card ? _card->flagDirRoot() : nullptr;
        const auto counter = _card ? _card->useCounter() : nullptr;
        if (!::cwxeditor::cwx::flag::findVar<::cwxeditor::cwx::flag::Variant>(
                root, counter, _externalVariantWriteScenarioVar->edit_text)) {
            _warnings.push_back(::cwxeditor::cwx::utils::tryFormat(
                messages.searchErrorVariantNotFound.value,
                _externalVariantWriteScenarioVar->edit_text));
        }
    }
}

template <typename C>
void EffectCardDialog<C>::refreshWarning() {
    if (_ignoreMod) return;
    refreshWarningsForCard();
    // 閉じる操作の修正対象には、Dの警告による再構築状態を含めない。
    // 一度警告が現れた後は、以前のOK・適用拒否の経路を維持する。
    if (!_warnings.empty()) _readOnlyCloseEligible = false;
    if (_okButton) {
        if (_readOnly && !_readOnlyCloseEligible)
            _okButton->text = _prop ? _prop->requiredMsgs().dlgTextOK.value : "OK";
        String tooltip;
        for (const auto& warning : _warnings) {
            if (!tooltip.empty()) tooltip += '\n';
            tooltip += warning;
        }
        _okButton->set_tooltip(std::move(tooltip));
        _okButton->icon = !_warnings.empty() && _context.editor_props
            ? _context.editor_props->images().load(StringView{"warning.png"}) : 0;
        if (_context.services.gui) _context.services.gui->request_redraw(_okButton);
    }
}

template <typename C>
void EffectCardDialog<C>::abilityModified() {
    if (_ignoreMod || _readOnly) return;
    if (_window) _window->modified = true;
    // 書き込み可能なEffectCardDialogでは適用操作を使用可
    // 能に保つ。有効状態の更新後にこの条件を再設定し、DのAbsDialog::mod
    // によるapplyEnabled()の効果に合わせる。
    if (_applyButton) _applyButton->set_enabled(true);
    refreshWarning();
}

template <typename C>
void EffectCardDialog<C>::writeControlsToDraft() {
    if (!_draft) {
        const auto system = _prop ? &_prop->sys() : nullptr;
        _draft = std::make_shared<C>(
            system, newCardId<C>(_summ), _name ? _name->text : String{},
            _imgPath.images, _desc ? dutils::wrapReturnCode(_desc->text) : String{});
    }
    _draft->paths(_imgPath.images);
    _draft->desc(_desc ? dutils::wrapReturnCode(_desc->text) : String{});
    _draft->name(_name ? _name->text : String{});
    _draft->spell(_needSpell && _needSpell->checked);
    _draft->effectType(radioValue(_effTyp, effectTypes, types::EffectType::Physic));
    _draft->resist(radioValue(_res, resistTypes, types::Resist::Avoid));
    _draft->physical(_ability.physical());
    _draft->mental(_ability.mental());

    if constexpr (std::is_same_v<C, card::SkillCard>) {
        if (_level) _draft->level(static_cast<std::uint32_t>(std::max(0.0, _level->value)));
        if (_price) _draft->price(static_cast<std::uint32_t>(std::max(0.0, _price->value)));
    } else if constexpr (std::is_same_v<C, card::ItemCard>) {
        if (_useCount) {
            const auto limit = static_cast<std::uint32_t>(std::max(0.0, _useCount->value));
            _draft->useLimitMax(limit);
            _draft->useLimit(limit);
        }
        if (_price) _draft->price(static_cast<std::uint32_t>(std::max(0.0, _price->value)));
    } else {
        if (_useCount) _draft->useLimit(static_cast<std::uint32_t>(std::max(0.0, _useCount->value)));
        if (_price) _draft->price(static_cast<std::uint32_t>(std::max(0.0, _price->value)));
        if (_showStyle && _showStyle->selected_index &&
            *_showStyle->selected_index < _showStyles.size())
            _draft->showStyle(_showStyles[*_showStyle->selected_index]);
        lib::Array<types::Status> conditions;
        for (std::size_t i = 0; i < _invokeCond.size(); ++i)
            if (_invokeCond[i] && _invokeCond[i]->checked)
                conditions.push_back(statusTypes[i]);
        _draft->invocationCondition(conditions);
        _draft->removeWithUnconscious(_removeWithUncons && _removeWithUncons->checked);
    }

    if (_motions) _draft->motions(_motions->motions());
    writeEnhanceControls(*_draft);
    _draft->target(radioValue(_targ, targetTypes, types::CardTarget::None));
    _draft->allRange(_oneAllGrp && _oneAllGrp->enabled() && _all && _all->checked);
    _draft->visual(radioValue(_vis, visualTypes, types::CardVisual::None));
    _draft->premium(radioValue(_prem, premiumTypes, types::Premium::Normal));
    if (_sucRate) _draft->successRate(static_cast<int>(std::lround(_sucRate->value)));
    _se1.syncFromControls();
    _se2.syncFromControls();
    _draft->soundPath1(_se1.path);
    _draft->volume1(_se1.volume);
    _draft->loopCount1(_se1.loopCount);
    _draft->soundPath2(_se2.path);
    _draft->volume2(_se2.volume);
    _draft->loopCount2(_se2.loopCount);
    _draft->scenario(_scenario ? _scenario->text : String{});
    _draft->author(_author ? _author->text : String{});
    if (_keyCodes) _draft->keyCodes(_keyCodes->keyCodes());
    _draft->settingsComment(_settingsComment ? _settingsComment->text : String{});
    _draft->externalVariantWriteScope(externalVariantWriteScopeSelection());
    _draft->externalVariantWriteScenarioVar(
        _externalVariantWriteScenarioVar ? _externalVariantWriteScenarioVar->edit_text
                                          : String{});
    _draft->externalVariantWriteYado(
        _externalVariantWriteYado && _externalVariantWriteYado->selected_index &&
        *_externalVariantWriteYado->selected_index == 1U);
}

template <typename C>
void EffectCardDialog<C>::writeEnhanceControls(C& value) {
    const auto use = readEnhanceControls(_useModTbl, _useModR, _useModS);
    for (std::size_t i = 0; i < editableEnhancements.size(); ++i)
        value.enhance(editableEnhancements[i], use[i]);
    if constexpr (std::is_same_v<C, card::ItemCard>) {
        const auto owner = readEnhanceControls(_hasModTbl, _hasModR, _hasModS);
        for (std::size_t i = 0; i < editableEnhancements.size(); ++i)
            value.enhanceOwner(editableEnhancements[i], owner[i]);
    }
}

template <typename C>
void EffectCardDialog<C>::commitDraft() {
    if (!_draft) return;
    if (_card) {
        cardpropertytransaction::copyProperties(*_card, *_draft);
    } else {
        _card = _draft;
        if (_useCounter) _card->setUseCounter(_useCounter, _ucOwner);
    }
    _creationMode = false;
    if (_context.refreshToolBar) _context.refreshToolBar();
    invoke(appliedEvent);
    if (_callbacks.applied) _callbacks.applied();
}

template <typename C>
bool EffectCardDialog<C>::apply() {
    if (_readOnly) return false;
    const auto material = _imgPath.materialPath(_forceApplying);
    if (material.cancel) return false;
    _imgPath.images = material.images;
    invoke(applyEvent);
    writeControlsToDraft();
    const std::weak_ptr<bool> lifetime = _callbackLifetime;
    const bool was_open = _open;
    const auto applying = on_applying;
    if (applying) applying();
    const auto alive = lifetime.lock();
    if (!alive || !*alive || _open != was_open) return false;
    // Dはカード値の適用成功後にだけrefKeyCodesを公開する。
    auto* keycode_commons = _context.editor_commons;
    const auto keycode_lifetime = keycode_commons
        ? keycode_commons->lifetimeToken() : std::weak_ptr<const int>{};
    const bool refresh_keycodes = !lib::editor_window_host_options().all_mode;
    commitDraft();
    const auto after_commit = lifetime.lock();
    if (!after_commit || !*after_commit || _open != was_open) return false;
    refreshWarning();
    if (_window) _window->modified = false;
    if (_applyButton) _applyButton->set_enabled(false);
    // リスナーがダイアログを閉じたり破棄したりし得るため、通知は最後に行う。
    if (refresh_keycodes && keycode_commons && !keycode_lifetime.expired())
        keycode_commons->refKeyCodes.call();
    return true;
}

template <typename C>
bool EffectCardDialog<C>::openCWXPath(String pathValue, const bool shellActivate) {
    if (_motions && _motions->openCWXPath(pathValue, shellActivate)) return true;
    return _callbacks.openPath ? _callbacks.openPath(pathValue, shellActivate) : false;
}

template <typename C>
void EffectCardDialog<C>::close() {
    if (!_open) return;
    if (_window && _context.editor_props) {
        // 未スケールの寸法を保存する(再オープン時の二重スケールで巨大化するのを防ぐ)。
        // さらに、開く時に足した外側の余白(と ALL のアイテムのタブ分)を引き、
        // 開く時と同じ「タブ/クライアントの寸法」で保存する。引かないと
        // 開閉のたびに余白分ずつ大きくなる。
        const auto size = lib::authored_window_bounds(*_window);
        const float item_tab_extra =
            std::is_same_v<C, card::ItemCard> && lib::editor_window_host_options().all_mode
                ? kAllItemTabExtra : 0.0F;
        const float width = size.width - kEffectCardSidePadding * 2.0F - item_tab_extra;
        const float height = size.height - kEffectCardBottomPadding;
        const auto save = [&](auto& geometry) {
            geometry.width(static_cast<std::int32_t>(std::lround(std::max(1.0F, width))));
            geometry.height(static_cast<std::int32_t>(std::lround(std::max(1.0F, height))));
        };
        if constexpr (std::is_same_v<C, card::SkillCard>) save(_context.editor_props->flex().skillCardDlg);
        else if constexpr (std::is_same_v<C, card::ItemCard>) save(_context.editor_props->flex().itemCardDlg);
        else save(_context.editor_props->flex().beastCardDlg);
    }
    _open = false;
    *_keyCodeHostActive = false;
    const auto close_lifetime = _callbackLifetime;
    const auto revision = ++_hostRevision;
    std::exception_ptr failure;
    const auto current = [this, close_lifetime, revision] {
        return *close_lifetime && _hostRevision == revision && !_open;
    };
    const auto step = [&failure](auto&& callback) {
        try { callback(); } catch (...) { if (!failure) failure = std::current_exception(); }
    };
    const auto continue_close = [&] {
        if (current()) return true;
        if (failure) std::rethrow_exception(failure);
        return false;
    };
    if (const auto keys = _keyCodes) step([&] { keys->hostActive(false); });
    if (!continue_close()) return;
    if (auto* const selector = _imageSelect.get()) {
        step([&] { selector->bindEditorCommons(nullptr); });
        if (!continue_close()) return;
        step([&] { selector->active(false); });
        if (!continue_close()) return;
    }
    const auto audio = _context.services.audio;
    for (auto* sound : {&_se1, &_se2}) {
        ++sound->previewRevision;
        const auto handle = std::exchange(sound->previewHandle, 0);
        auto* const selector = sound->selector.get();
        if (selector) step([&] { selector->stopSE(); });
        if (!continue_close()) return;
        if (audio && handle) step([&] { audio->stop(handle); });
        if (!continue_close()) return;
    }
    if (const auto editor = _motions) step([&] { editor->closeAuxiliaryEditors(); });
    if (!continue_close()) return;
    on_applying = {};
    disconnectCommonsObservers();
    const auto gui = _context.services.gui;
    const auto window = _window;
    if (gui && window) step([&] { gui->close(window); });
    if (!continue_close()) return;
    const auto observers = closeEvent;
    for (const auto& callback : observers) {
        if (callback) step(callback);
        if (!continue_close()) return;
    }
    const auto closed = _callbacks.closed;
    if (closed) step(closed);
    if (failure) std::rethrow_exception(failure);
}

template <typename C>
void EffectCardDialog<C>::refEventTree(const event::EventTree* tree) {
    // Dはこのカードが所有するツリー、または削除中ですでに切り離されたツリー
    // だけを更新する。C++カードはEventTreeOwnerを内包するた
    // め、所有元オブジェクトのアドレスではなく具体的なツリー集合を比較する。
    if (!tree || tree->owner() == nullptr || (_card && std::any_of(
            _card->trees().begin(), _card->trees().end(), [tree](const auto& value) {
                return value.get() == tree;
            })))
        refreshWarning();
}

template <typename C>
void EffectCardDialog<C>::delCard(const path::CWXPath* owner,
                                  const CardPtr& value) {
    (void)owner;
    if (_card && value && _card.get() == value.get()) forceCancel();
}

template <typename C>
void EffectCardDialog<C>::refScenario(const SummaryPtr& value) {
    // C++は適用後の同じSummaryの更新通知にもrefScenario
    // を使う。カード・スキン・使用カウンターの所有関係を無
    // 効にするのは、実際にシナリオが置き換わった場合だけ。
    if (value.get() != _summ.get()) forceCancel();
}

template <typename C>
void EffectCardDialog<C>::refSkin() {
    // Dは読み取り専用やパスのない所有元に対して、構築時に解決したスキンを保持する。
    if (!_summarySkinPinned) _summSkin = resolveSkin();
    const auto skin = summSkin();
    if (_motions) _motions->refSkin(skin);
    if (_desc && _prop) {
        auto font = _prop->looks().cardDescFont(
            skin ? skin->legacy() : (_summ && _summ->legacy()));
        if (_context.editor_props)
            font = _context.editor_props->adjustFont(std::move(font));
        auto content_font = dutils::createFontFromPixels(std::move(font));
        content_font.content_font = true;
        _desc->set_font(std::move(content_font));
    }
    if (_imageSelect) _imageSelect->refresh();
    refreshWarning();
}

template <typename C>
void EffectCardDialog<C>::refDataVersion() {
    if (_desc && _prop) {
        _desc->minimum_visible_columns = static_cast<std::size_t>(std::max(1, _prop->looks().cardDescLen()));
        _desc->minimum_visible_lines = static_cast<std::size_t>(std::max(1, _prop->looks().cardDescLine(_summ && _summ->legacy())));
        auto font = _prop->looks().cardDescFont(
            _summSkin ? _summSkin->legacy() : (_summ && _summ->legacy()));
        if (_context.editor_props)
            font = _context.editor_props->adjustFont(std::move(font));
        auto content_font = dutils::createFontFromPixels(std::move(font));
        content_font.content_font = true;
        _desc->set_font(std::move(content_font));
    }
    updateEnabledImpl();
    if (_motions) _motions->refDataVersion();
    if (_keyCodes) _keyCodes->refDataVersion();
    refreshWarning();
}

template <typename C>
void EffectCardDialog<C>::ResetSource::widgetSelected() {
    if (owner_) owner_->resetSource();
}

template <typename C>
void EffectCardDialog<C>::ModSource::modifyText() {
    if (owner_) owner_->refreshWarning();
}

template <typename C>
void EffectCardDialog<C>::OASelect::widgetSelected() {
    if (owner_) owner_->refreshEnblOneAll();
}

template <typename C>
void EffectCardDialog<C>::SelEffectType::widgetSelected() {
    if (owner_) owner_->refreshWarning();
}

template <typename C>
void EffectCardDialog<C>::Dispose::widgetDisposed() {
    if (owner_) owner_->close();
}

std::shared_ptr<lib::GroupBox> createEffectSoundPanel(
    const Parent& parent, const Context& context, const SummaryPtr& summary,
    String name, const bool readOnly, const bool isEffectContent,
    std::shared_ptr<lib::CheckBox>& initialEffect, SoundSelect& se1,
    SoundSelect& se2) {
    auto group = std::make_shared<lib::GroupBox>();
    group->title = std::move(name);
    // Dは503×503のEffectダイアログの左半分に、二
    // つの音声選択部を収める。汎用カードダイアログの大きい間隔
    // を使うと、二つ目がクライアント領域の下に消えてしまう。
    verticalLayout(group, 3.0F, {3.0F, 3.0F, 3.0F, 3.0F});
    if (parent) parent->add(group);

    auto createSound = [&](SoundSelect& select, String title,
                           const bool hasInitialEffect) {
        auto comp = std::make_shared<lib::Panel>();
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 3U;
        layout->spacing = 2.0F;
        comp->set_layout(layout);
        comp->set_layout_item({.horizontal = lib::Align::Stretch,
                               .vertical = lib::Align::Start});
        group->add(comp);

        select.parent = comp;
        select.readOnly = readOnly;
        select.enabled = true;
        select.path.clear();
        select.filePath.clear();
        select.volume = 100U;
        select.loopCount = 1U;
        select.fadeIn = 0U;
        select.channel = 0U;
        std::size_t row = 0U;
        if (hasInitialEffect) {
            initialEffect = std::make_shared<lib::CheckBox>(
                context.prop ? context.prop->requiredMsgs().hasInitialEffect.value
                             : title + " initial effect");
            initialEffect->set_enabled(!readOnly);
            initialEffect->set_layout_item({.row = row++, .column = 0U,
                                            .column_span = 3U,
                                            .horizontal = lib::Align::Stretch});
            initialEffect->on_changed = [&select](const bool checked) {
                select.setEnabled(checked);
            };
            comp->add(initialEffect);
        }
        auto label = std::make_shared<lib::Label>(std::move(title));
        label->set_layout_item({.row = row++, .column = 0U,
                                .column_span = 3U,
                                .horizontal = lib::Align::Stretch});
        comp->add(label);

        if (context.editor_props) {
            auto definitions = [&context](const bool) {
                return std::vector<String>{
                    context.editor_props->requiredMsgs().defaultSelection(
                        context.editor_props->requiredMsgs().soundNone.value)};
            };
            select.selector = std::make_unique<materialselect::MaterialSelectBase>(
                materialselect::MtType::SE, *context.editor_props, summary.get(),
                materialselect::SkinPtr{}, readOnly, lib::Action{},
                std::move(definitions), false, false, nullptr, lib::Action{},
                context.services);
            select.selector->bindEditorCommons(context.editor_commons);
            const auto update = [&select] {
                if (select.synchronizing()) return;
                select.syncFromControls();
                select.notifyModified();
            };
            select.selector->modEvent.push_back(update);
            select.selector->loadedEvent.push_back([&select] {
                if (select.synchronizing()) return;
                select.syncFromControls();
                select.notifyLoaded();
            });
            const auto dirs = select.selector->createDirsCombo(comp);
            dirs->set_layout_item({.row = row, .column = 0U,
                                   .horizontal = lib::Align::Stretch,
                                   .grow = 1.0F});
            const auto stop = select.selector->createStopButton(comp);
            const auto play = select.selector->createPlayButton(comp);
            // アイコン専用ボタン。色付きアイコンだけを使い、四角や再生記号の文字は加えない。
            if (stop) { stop->accessibility_name = stop->text; stop->text.clear(); }
            if (play) { play->accessibility_name = play->text; play->text.clear(); }
            if (stop) stop->set_layout_item({.row = row, .column = 1U,
                                             .minimum = {25.0F, 21.0F},
                                             .preferred = {25.0F, 21.0F}});
            if (play) play->set_layout_item({.row = row, .column = 2U,
                                              .minimum = {25.0F, 21.0F},
                                              .preferred = {25.0F, 21.0F}});
            ++row;
            if (stop)
                stop->set_tooltip(context.editor_props->requiredMsgs().menuTextStopSE.value);
            if (play)
                play->set_tooltip(context.editor_props->requiredMsgs().menuTextPlaySE.value);
            // ダイアログとMaterialSelectで同じComboモデルを使い
            // 、選択、なし、検索、メニュー、ドロップ操作を一つの経路にまとめる。
            const auto files = select.selector->createFileCombo(comp);
            files->set_layout_item({.row = row++, .column = 0U,
                                    .column_span = 3U,
                                    .horizontal = lib::Align::Stretch,
                                    .minimum = {0.0F, 21.0F},
                                    .preferred = {0.0F, 21.0F}});
            select.pathControl = files;
            select.selector->refresh();
            // refresh()はMaterialSelectの長い
            // ラベルを設定する。Dの音声ページはアイコン専用ボタンな
            // ので、選択部が操作部を同期した後に、ラベルを再び消す。
            if (stop) stop->text.clear();
            if (play) play->text.clear();
        } else {
            auto combo = std::make_shared<lib::ComboBox>();
            combo->items.push_back(
                {context.prop ? context.prop->requiredMsgs().soundNone.value : "No sound",
                 "", {}, true});
            combo->selected_index = 0U;
            combo->editable = true;
            combo->set_enabled(!readOnly);
            combo->on_selection_changed = [&select, combo](
                                              const std::optional<std::size_t> index) {
                if (!index || *index >= combo->items.size()) return;
                select.setPath(combo->items[*index].value);
            };
            combo->on_text_changed = [&select](const StringView value) {
                select.setPath(String(value));
            };
            combo->set_layout_item({.row = row, .column = 0U,
                                    .horizontal = lib::Align::Stretch,
                                    .grow = 1.0F});
            select.pathControl = combo;
            comp->add(combo);
            auto stop = std::make_shared<lib::Button>(
                context.prop ? context.prop->requiredMsgs().menuTextStopSE.value : "Stop");
            auto play = std::make_shared<lib::Button>(
                context.prop ? context.prop->requiredMsgs().menuTextPlaySE.value : "Play");
            const std::weak_ptr<int> preview_life = select.previewLifetime;
            const auto audio = context.services.audio;
            stop->on_click = [audio, &select, preview_life] {
                if (preview_life.expired()) return;
                ++select.previewRevision;
                const auto handle = std::exchange(select.previewHandle, 0);
                const auto ticket = handle ? materialselect::beginSoundPreview(audio)
                                           : materialselect::SoundPreviewTicket{};
                (void)ticket;
                if (audio && handle) audio->stop_bus(lib::AudioBus::SoundEffect);
            };
            play->on_click = [audio, summary, &select, preview_life] {
                if (preview_life.expired()) return;
                select.syncFromControls();
                if (preview_life.expired() || !audio || select.path.empty()) return;
                const auto revision = ++select.previewRevision;
                const auto ticket = materialselect::beginSoundPreview(audio);
                const auto current = [&] {
                    return ticket.current() && !preview_life.expired() &&
                           select.previewRevision == revision;
                };
                auto path = structs::pathFromUtf8(select.path);
                if (path.is_relative() && summary && !summary->scenarioPath().empty())
                    path = structs::pathFromUtf8(summary->scenarioPath()) / path;
                lib::AudioPlayOptions options;
                options.bus = lib::AudioBus::SoundEffect;
                options.volume = static_cast<float>(select.volume) / 100.0F;
                options.loop_count = select.loopCount;
                std::string initialization_error;
                if (!audio->initialize(initialization_error) || !current()) return;
                select.previewHandle = 0;
                audio->stop_bus(lib::AudioBus::SoundEffect);
                if (!current()) return;
                const auto handle = audio->play({path}, options);
                if (!current()) {
                    if (handle) audio->stop(handle);
                    return;
                }
                select.previewHandle = handle;
            };
            comp->add(stop);
            comp->add(play);
            stop->set_layout_item({.row = row, .column = 1U});
            play->set_layout_item({.row = row, .column = 2U});
            ++row;
        }

        Parent options;
        if (select.selector) {
            options = select.selector->createPlayingOptions(comp, false, true);
            select.volumeControl = select.selector->volumeControl();
            select.loopControl = select.selector->loopControl();
        } else {
            auto controls = materialselect::createPlayingOptionsControls(
                comp, context.editor_props, materialselect::MtType::SE, false, true);
            options = controls.panel;
            select.volumeControl = controls.volume;
            select.loopControl = controls.loops;
            select.volumeControl->on_changed = [&select](const double value) {
                select.volume = static_cast<std::uint32_t>(std::clamp(
                    static_cast<int>(std::lround(value)), 0, 100));
                select.notifyModified();
            };
            select.loopControl->on_changed = [&select](const double value) {
                select.loopCount = static_cast<std::uint32_t>(std::max(
                    1, static_cast<int>(std::lround(value))));
                select.notifyModified();
            };
        }
        select.volumeControl->read_only = readOnly;
        select.loopControl->read_only = readOnly;
        options->set_layout_item({.row = row, .column = 0U,
                                  .column_span = 3U,
                                  .horizontal = lib::Align::Stretch});
        select.syncControls();
        if (hasInitialEffect) select.setEnabled(false);
    };

    createSound(se1, context.prop ? context.prop->requiredMsgs().se1.value : "Sound 1",
                isEffectContent);
    createSound(se2, context.prop ? context.prop->requiredMsgs().se2.value : "Sound 2",
                false);
    return group;
}

template class EffectCardDialog<card::SkillCard>;
template class EffectCardDialog<card::ItemCard>;
template class EffectCardDialog<card::BeastCard>;

}  // namespace cwxeditor::cwx::editor::gui::dwt::effectcarddialog
