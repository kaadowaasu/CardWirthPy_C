#include "cwxeditor_src/lib/service_defaults.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/eventtreedialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"

#include <algorithm>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::eventtreedialog {
namespace {

lib::Services resolveServices(lib::Services supplied) {
    return lib::with_service_defaults({}, std::move(supplied));
}

template <typename Component>
std::shared_ptr<Component> makeComponent(const lib::Services& services,
                                         const lib::ComponentKind kind) {
    if (services.gui) {
        if (auto candidate = services.gui->create_component(kind)) {
            if (auto component = std::dynamic_pointer_cast<Component>(candidate))
                return component;
        }
    }
    return std::make_shared<Component>();
}

template <typename Component>
std::shared_ptr<Component> makeChild(const lib::Services& services,
                                     const lib::ComponentKind kind,
                                     lib::Container& parent) {
    auto component = makeComponent<Component>(services, kind);
    parent.add(component);
    return component;
}

void setGridPosition(const std::shared_ptr<lib::Widget>& widget,
                     const std::size_t row, const std::size_t column,
                     const std::size_t columnSpan = 1U,
                     const float grow = 0.0F) {
    if (!widget) return;
    lib::LayoutItem item;
    item.row = row;
    item.column = column;
    item.column_span = std::max<std::size_t>(columnSpan, 1U);
    item.horizontal = lib::Align::Stretch;
    item.vertical = grow > 0.0F ? lib::Align::Stretch : lib::Align::Start;
    item.grow = grow;
    widget->set_layout_item(item);
}

String joinWarnings(const std::vector<String>& warnings) {
    String result;
    for (const auto& warning : warnings) {
        if (!result.empty()) result.push_back('\n');
        result.append(warning);
    }
    return result;
}

String keyCodeName(const Props& prop, const types::FKCKind kind) {
    return prop.requiredMsgs().keyCodeTiming(kind);
}

}  // namespace

OwnerCapabilities OwnerCapabilities::generic() noexcept {
    return {};
}

OwnerCapabilities OwnerCapabilities::menuCard() noexcept {
    OwnerCapabilities result;
    result.kind = OwnerKind::MenuCard;
    return result;
}

OwnerCapabilities OwnerCapabilities::enemyCard() noexcept {
    OwnerCapabilities result;
    result.kind = OwnerKind::EnemyCard;
    result.withKeyCodeIgnitionType = true;
    return result;
}

OwnerCapabilities OwnerCapabilities::area() noexcept {
    OwnerCapabilities result;
    result.kind = OwnerKind::Area;
    return result;
}

OwnerCapabilities OwnerCapabilities::battle() noexcept {
    OwnerCapabilities result;
    result.kind = OwnerKind::Battle;
    result.canHasFireLose = true;
    result.canHasFireEscape = true;
    result.canHasFireEveryRound = true;
    result.canHasFireRoundEnd = true;
    result.canHasFireRound0 = true;
    result.canHasFireRound = true;
    return result;
}

OwnerCapabilities OwnerCapabilities::playerCardEvents() noexcept {
    OwnerCapabilities result;
    result.kind = OwnerKind::PlayerCardEvents;
    result.withKeyCodeIgnitionType = true;
    return result;
}

OwnerCapabilities OwnerCapabilities::package() noexcept {
    OwnerCapabilities result;
    result.kind = OwnerKind::Package;
    result.canHasFireEnter = false;
    result.canHasFireKeyCode = false;
    return result;
}

EventTreeDialog::EventTreeDialog(Props& prop, Window parent, Summary* summ,
                                 EventTreeOwner& owner, EventTree& eventTree,
                                 const bool create, const bool readOnly,
                                 OwnerCapabilities capabilities,
                                 lib::Services services,
                                 commons::Commons* const editorCommons)
    : prop_(&prop),
      summary_(summ),
      owner_(&owner),
      event_tree_(&eventTree),
      capabilities_(capabilities),
      parent_(std::move(parent)),
      services_(resolveServices(std::move(services))),
      editor_commons_(editorCommons),
      editor_commons_lifetime_(editorCommons ? editorCommons->lifetimeToken()
                                             : std::weak_ptr<const int>{}),
      create_(create),
      read_only_(readOnly),
      set_auto_name_(create) {
    // 所有元を正とする。省略可能な記述子は、種別・
    // 発火種別の列など表示の詳細だけを提供する。
    capabilities_.canHasFireEnter = owner.canHasFireEnter();
    capabilities_.canHasFireLose = owner.canHasFireLose();
    capabilities_.canHasFireEscape = owner.canHasFireEscape();
    capabilities_.canHasFireEveryRound = owner.canHasFireEveryRound();
    capabilities_.canHasFireRoundEnd = owner.canHasFireRoundEnd();
    capabilities_.canHasFireRound0 = owner.canHasFireRound0();
    capabilities_.canHasFireRound = owner.canHasFireRound();
    capabilities_.canHasFireKeyCode = owner.canHasFireKeyCode();
    createDialog();
}

void EventTreeDialog::createDialog() {
    if (dialog_) return;
    if (services_.gui) dialog_ = services_.gui->create_dialog();
    if (!dialog_) dialog_ = std::make_shared<lib::Dialog>();

    dialog_->resizable = true;
    lib::configure_owned_window(dialog_, parent_,
        lib::OwnedWindowKind::Modeless);
    dialog_->set_layout(std::make_shared<lib::FillLayout>());
    dialog_->icon = prop_->images().load(StringView{"event_tree.png"});
    const auto savedWidth = capabilities_.canHasFireRound
                                ? prop_->flex().eventIgnitionsWithRoundsDlg.width()
                                : prop_->flex().eventIgnitionsDlg.width();
    const auto savedHeight = capabilities_.canHasFireRound
                                 ? prop_->flex().eventIgnitionsWithRoundsDlg.height()
                                 : prop_->flex().eventIgnitionsDlg.height();
    const float width = savedWidth > 0
                            ? static_cast<float>(savedWidth)
                            : (capabilities_.canHasFireRound ? 450.0F : 350.0F);
    const float height = savedHeight > 0
                             ? static_cast<float>(savedHeight)
                             : 300.0F;
    dialog_->minimum_size = {350.0F, 220.0F};
    dialog_->set_bounds({0.0F, 0.0F, width, height});
    dialog_->title = create_
        ? prop_->requiredMsgs().dlgTitNewEvent
        : utils::tryFormat(prop_->requiredMsgs().dlgTitEventTree.value, event_tree_->name());
}

keycodeview::KeyCodeViewConfig EventTreeDialog::keyCodeConfig() const {
    keycodeview::KeyCodeViewConfig config;
    config.system = prop_ ? &prop_->sys() : nullptr;
    config.readOnly = read_only_;
    config.canDuplicate = false;
    config.withIgnitionType = capabilities_.withKeyCodeIgnitionType;
    config.summaryLegacy = summary_ != nullptr && summary_->legacy();
    config.targetVersion150 = prop_->targetVersion(summary_, "1.50");
    config.keyCodesMaxLegacy = static_cast<std::size_t>(std::max(
        prop_->looks().keyCodesMaxLegacy(), 0));
    config.dataVersion = summary_ ? summary_->dataVersion()
                                  : String(versioninfo::LATEST_VERSION);
    config.editorCommons = editor_commons_;
    config.services = services_;
    if (services_.clipboard) {
        const auto clipboard = services_.clipboard;
        config.copyText = [clipboard](const StringView value) {
            lib::ClipboardData data;
            data.text = String(value);
            clipboard->clear();
            clipboard->write(data);
        };
    }
    config.standardKeyCodes = [prop = prop_, summary = summary_]() {
        lib::Array<String> result;
        const auto append_unique = [&result](const String& value) {
            if (!value.empty() &&
                std::find(result.begin(), result.end(), value) == result.end())
                result.push_back(value);
        };
        if (prop) {
            for (const auto& value :
                 prop->standardKeyCodes(summary ? summary->type() : StringView{}))
                append_unique(value);
        }
        if (summary) {
            if (const auto counter = summary->useCounter()) {
                for (const auto& id :
                     counter->template keys<usecounter::KeyCodeId>())
                    append_unique(id.id);
            }
        }
        return result;
    };
    config.system = &prop_->sys();
    config.services = services_;

    const auto& messages = prop_->requiredMsgs();
    config.resources.keyCodeLabel = messages.keyCode;
    config.resources.addKeyCodeText = messages.addKeyCode;
    config.resources.delKeyCodeText = messages.delKeyCode;
    config.resources.addByFeaturesText = messages.menuTextAddKeyCodesByFeatures;
    config.resources.upText = messages.menuTextUp;
    config.resources.downText = messages.menuTextDown;
    config.resources.keyCodeImage = prop_->images().load(StringView{"key_code.png"});
    config.resources.addKeyCodeImage = prop_->images().load(StringView{"add_key_code.png"});
    config.resources.delKeyCodeImage = prop_->images().load(StringView{"del_key_code.png"});
    config.resources.warningImage = prop_->images().load(StringView{"warning.png"});
    config.resources.addByFeaturesImage =
        prop_->images().load(StringView{"add_key_codes_by_features.png"});
    config.resources.upImage = prop_->images().menu(types::MenuID::Up);
    config.resources.downImage = prop_->images().menu(types::MenuID::Down);
    config.resources.keyCodeTimingText = messages.keyCodeTimingUse;
    config.resources.timingLabels = {
        keyCodeName(*prop_, types::FKCKind::Use),
        keyCodeName(*prop_, types::FKCKind::Success),
        keyCodeName(*prop_, types::FKCKind::Failure),
        keyCodeName(*prop_, types::FKCKind::HasNot),
    };
    config.resources.timingImages = {
        prop_->images().load(StringView{"key_code.png"}),
        prop_->images().load(StringView{"key_code_suc.png"}),
        prop_->images().load(StringView{"key_code_fail.png"}),
        prop_->images().load(StringView{"key_code_hasnot.png"}),
    };
    config.resources.searchErrorKeyCodeMatchingAll =
        messages.searchErrorKeyCodeMatchingAll;
    config.resources.warningHasNotKeyCode = messages.warningHasNotKeyCode;
    config.resources.warningRunAwayCard = messages.warningRunAwayCard;
    config.resources.warningKeyCodeCount = messages.warningKeyCodeCount;
    config.warningProvider = [this, life=std::weak_ptr<int>(callback_lifetime_)](StringView value) {
        if (life.expired()) return std::vector<String>{};
        return warning::sjisWarnings(prop_->parent(), summary_, value,
                                     prop_->requiredMsgs().keyCode.value);
    };
    return config;
}

roundview::RoundViewConfig EventTreeDialog::roundConfig() const {
    roundview::RoundViewConfig config;
    config.services = services_;
    config.readOnly = read_only_;
    config.roundMax = prop_->var().roundMax.value;
    config.undoMax = static_cast<std::size_t>(std::max(0, prop_->var().undoMaxEtc.value));
    config.dataVersion = summary_ ? summary_->dataVersion()
                                  : String(versioninfo::LATEST_VERSION);
    const auto& messages = prop_->requiredMsgs();
    config.resources.roundImage = prop_->images().load(StringView{"round.png"});
    config.resources.addRoundImage = prop_->images().load(StringView{"add_round.png"});
    config.resources.delRoundImage = prop_->images().load(StringView{"del_round.png"});
    config.resources.addManyRoundsImage =
        prop_->images().menu(types::MenuID::AddRangeOfRound);
    config.resources.addRoundText = messages.addRound;
    config.resources.delRoundText = messages.delRound;
    config.resources.manyRoundsText = messages.manyRounds;
    config.resources.undoText = prop_->buildMenu(types::MenuID::Undo);
    config.resources.redoText = prop_->buildMenu(types::MenuID::Redo);
    config.resources.cutText = prop_->buildMenu(types::MenuID::Cut);
    config.resources.copyText = prop_->buildMenu(types::MenuID::Copy);
    config.resources.pasteText = prop_->buildMenu(types::MenuID::Paste);
    config.resources.deleteText = prop_->buildMenu(types::MenuID::Delete);
    config.resources.selectAllText = prop_->buildMenu(types::MenuID::SelectAll);
    config.resources.roundSeparator = messages.roundSep;
    config.resources.rangeHint = messages.rangeHint;
    config.manyRoundsDialog = [owner = parent_](roundview::ManyRoundsDialog& dialog) {
        dialog.setOwner(owner);
        return dialog.open();
    };
    return config;
}

bool EventTreeDialog::catchModify() noexcept {
    if (!ignore_mod_) {
        modified_ = true;
        if (apply_button_ && !read_only_) apply_button_->set_enabled(true);
    }
    return false;
}

void EventTreeDialog::markChanged() {
    if (!ignore_mod_) {
        modified_ = true;
        if (apply_button_ && !read_only_) apply_button_->set_enabled(true);
    }
}

void EventTreeDialog::buildNameGroup(lib::Container& area,
                                     const std::size_t columns) {
    auto group = makeChild<lib::GroupBox>(services_, lib::ComponentKind::GroupBox,
                                          area);
    group->title = prop_->requiredMsgs().eventName;
    group->set_layout(std::make_shared<lib::FillLayout>());
    setGridPosition(group, 0U, 0U, columns);

    name_ = makeChild<lib::TextEdit>(services_, lib::ComponentKind::TextEdit,
                                     *group);
    name_->read_only = read_only_;
    name_->on_changed = [this, life=std::weak_ptr<int>(callback_lifetime_)](StringView value) {
        if (life.expired()) return;
        if (name_) name_->text = String(value);
        markChanged();
        setAutoName();
        refreshWarning();
    };
}

void EventTreeDialog::buildSystemGroup(lib::Container& area,
                                       const std::size_t column) {
    auto group = makeChild<lib::GroupBox>(services_, lib::ComponentKind::GroupBox,
                                          area);
    group->title = prop_->requiredMsgs().eventTreeKindSystem;
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1;
    layout->equal_width = true;
    group->set_layout(std::move(layout));
    setGridPosition(group, 1U, column, 1U, 1.0F);

    const auto createCheck = [this, group](String text, const bool warningCheck) {
        auto check = makeChild<lib::CheckBox>(services_, lib::ComponentKind::CheckBox,
                                              *group);
        check->text = std::move(text);
        check->set_enabled(!read_only_);
        check->on_changed = [this, warningCheck, life=std::weak_ptr<int>(callback_lifetime_)](const bool) {
            if (life.expired()) return;
            markChanged();
            setAutoName();
            if (warningCheck) refreshWarning();
        };
        return check;
    };

    if (capabilities_.canHasFireEnter) {
        String text = prop_->requiredMsgs().enterTree;
        switch (capabilities_.kind) {
        case OwnerKind::MenuCard: text = prop_->requiredMsgs().selectTree; break;
        case OwnerKind::EnemyCard:
        case OwnerKind::PlayerCardEvents: text = prop_->requiredMsgs().deadTree; break;
        case OwnerKind::Battle: text = prop_->requiredMsgs().victoryTree; break;
        case OwnerKind::Area:
        case OwnerKind::Generic:
        case OwnerKind::Package: break;
        }
        enter_ = createCheck(std::move(text), false);
    }
    if (capabilities_.canHasFireLose)
        lose_ = createCheck(prop_->requiredMsgs().loseTree, false);
    if (capabilities_.canHasFireEscape)
        escape_ = createCheck(prop_->requiredMsgs().escapeTree, false);
    if (capabilities_.canHasFireEveryRound)
        every_round_ = createCheck(prop_->requiredMsgs().everyRoundTree, true);
    if (capabilities_.canHasFireRoundEnd)
        round_end_ = createCheck(prop_->requiredMsgs().roundEndTree, true);
    if (capabilities_.canHasFireRound0)
        round0_ = createCheck(prop_->requiredMsgs().round0Tree, true);
}

void EventTreeDialog::buildKeyCodeGroup(lib::Container& area,
                                        const std::size_t column) {
    auto group = makeChild<lib::GroupBox>(services_, lib::ComponentKind::GroupBox,
                                          area);
    group->title = prop_->requiredMsgs().eventTreeKindKeyCode;
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1;
    layout->equal_width = true;
    group->set_layout(std::move(layout));
    setGridPosition(group, 1U, column, 1U, 1.0F);

    key_codes_ = std::make_shared<keycodeview::KeyCodeView>(
        group.get(), keycodeview::STYLE_NONE, keyCodeConfig(),
        [this, life=std::weak_ptr<int>(callback_lifetime_)]() { return !life.expired() && catchModify(); });
    group->add(key_codes_);
    key_codes_->addModifyListener([this, life=std::weak_ptr<int>(callback_lifetime_)]() {
        if (life.expired()) return;
        markChanged();
        setAutoName();
        refreshWarning();
    });

    auto footer = makeChild<lib::Panel>(services_, lib::ComponentKind::Panel,
                                        *group);
    auto footerLayout = std::make_shared<lib::GridLayout>();
    footerLayout->columns = 2;
    footerLayout->equal_width = false;
    footer->set_layout(std::move(footerLayout));

    auto label = makeChild<lib::Label>(services_, lib::ComponentKind::Label,
                                       *footer);
    label->text = prop_->requiredMsgs().matchingType;
    key_code_matching_type_ = makeChild<lib::ComboBox>(
        services_, lib::ComponentKind::ComboBox, *footer);
    key_code_matching_type_->editable = false;
    key_code_matching_type_->set_enabled(!read_only_);
    key_code_matching_type_->items.push_back({
        prop_->requiredMsgs().matchingTypeName(MatchingType::Or), "Or"});
    key_code_matching_type_->items.push_back({
        prop_->requiredMsgs().matchingTypeName(MatchingType::And), "And"});
    key_code_matching_type_->on_selection_changed = [this, life=std::weak_ptr<int>(callback_lifetime_)](
        std::optional<std::size_t>) {
        if (life.expired()) return;
        markChanged();
        refreshWarning();
    };
}

void EventTreeDialog::buildRoundGroup(lib::Container& area,
                                      const std::size_t column) {
    auto group = makeChild<lib::GroupBox>(services_, lib::ComponentKind::GroupBox,
                                          area);
    group->title = prop_->requiredMsgs().eventTreeKindRound;
    group->set_layout(std::make_shared<lib::FillLayout>());
    setGridPosition(group, 1U, column, 1U, 1.0F);
    rounds_ = std::make_shared<roundview::RoundView>(
        group.get(), roundview::STYLE_NONE, roundConfig(),
        [this, life=std::weak_ptr<int>(callback_lifetime_)]() { return !life.expired() && catchModify(); });
    group->add(rounds_);
    rounds_->addModifyListener([this, life=std::weak_ptr<int>(callback_lifetime_)]() {
        if (life.expired()) return;
        markChanged();
        setAutoName();
        refreshWarning();
    });
}

void EventTreeDialog::populateControls() {
    if (!event_tree_) return;
    ignore_mod_ = true;
    if (name_) name_->text = event_tree_->name();
    if (enter_) enter_->checked = event_tree_->fireEnter();
    if (lose_) lose_->checked = event_tree_->fireLose();
    if (escape_) escape_->checked = event_tree_->fireEscape();
    if (every_round_) every_round_->checked = event_tree_->fireEveryRound();
    if (round_end_) round_end_->checked = event_tree_->fireRoundEnd();
    if (round0_) round0_->checked = event_tree_->fireRound0();

    if (key_codes_) {
        keycodeview::KeyCodes values;
        values.reserve(event_tree_->keyCodes().size());
        for (const auto& keyCode : event_tree_->keyCodes())
            values.push_back(prop_->sys().convFireKeyCode(keyCode));
        key_codes_->setValues(values);
    }
    if (key_code_matching_type_) {
        const auto matching = event_tree_->keyCodeMatchingType();
        (void)lib::select_combo_item(
            *key_code_matching_type_,
            matching == MatchingType::Or ? std::optional<std::size_t>(0U)
                                         : std::optional<std::size_t>(1U),
            false);
    }
    if (rounds_) {
        const auto values = event_tree_->rounds();
        rounds_->setValues(
            lib::Array<std::uint32_t>(values.begin(), values.end()));
    }
    ignore_mod_ = false;
    modified_ = false;
}

void EventTreeDialog::setup(lib::Container& area) {
    if (initialized_) return;
    const bool system = capabilities_.canHasFireEnter ||
                        capabilities_.canHasFireLose ||
                        capabilities_.canHasFireEscape ||
                        capabilities_.canHasFireEveryRound ||
                        capabilities_.canHasFireRoundEnd ||
                        capabilities_.canHasFireRound0;
    const bool keyCodes = capabilities_.canHasFireKeyCode;
    const bool rounds = capabilities_.canHasFireRound;
    const std::size_t columns = static_cast<std::size_t>(system) +
                                static_cast<std::size_t>(keyCodes) +
                                static_cast<std::size_t>(rounds);
    auto layout = std::make_shared<lib::GridLayout>();
    // DのSWT配置では、発火の操作部のない所有元は発火列が
    // 0になる。共通GridLayoutは最低一つの実列を確
    // 保し、共通の名前グループとフッターを使える列に保つ。
    layout->columns = std::max<std::size_t>(columns, 1U);
    layout->equal_width = false;
    // このダイアログはAbsDialogを使わず、自身で操作行を作る。
    layout->padding.bottom = 6.0F;
    area.set_layout(std::move(layout));

    const auto gridColumns = std::max<std::size_t>(columns, 1U);
    buildNameGroup(area, gridColumns);
    std::size_t column = 0U;
    if (system) buildSystemGroup(area, column++);
    if (keyCodes) buildKeyCodeGroup(area, column++);
    if (rounds) buildRoundGroup(area, column++);

    const std::size_t lowerRow = columns == 0U ? 1U : 2U;

    warning_label_ = makeChild<lib::Label>(services_, lib::ComponentKind::Label,
                                           area);
    warning_label_->wrap = true;
    setGridPosition(warning_label_, lowerRow, 0U, gridColumns);

    auto footer = makeChild<lib::Panel>(services_, lib::ComponentKind::Panel, area);
    auto footer_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
    footer_layout->spacing = 6.0F;
    footer->set_layout(footer_layout);
    setGridPosition(footer, lowerRow + 1U, 0U, gridColumns);
    ok_button_ = makeChild<lib::Button>(services_, lib::ComponentKind::Button, *footer);
    ok_button_->text = "OK";
    ok_button_->default_button = false;
    ok_button_->on_click = [this, life=std::weak_ptr<int>(callback_lifetime_)] {
        if (life.expired()) return;
        if (!read_only_ && (modified_ || create_) && !apply()) return;
        if (!life.expired()) close(lib::DialogResult::OK);
    };
    cancel_button_ = makeChild<lib::Button>(services_, lib::ComponentKind::Button, *footer);
    cancel_button_->text = "キャンセル";
    cancel_button_->cancel_button = true;
    cancel_button_->on_click = [this, life=std::weak_ptr<int>(callback_lifetime_)] {
        if (life.expired()) return;
        close(lib::DialogResult::Cancel);
    };

    if (!read_only_) {
        apply_button_ = makeChild<lib::Button>(services_, lib::ComponentKind::Button, *footer);
        apply_button_->text = "適用";
        apply_button_->set_enabled(create_);
        apply_button_->on_click = [this, life=std::weak_ptr<int>(callback_lifetime_)] { if (!life.expired()) (void)apply(); };
    }
    dialog_->on_close_requested = [this, life=std::weak_ptr<int>(callback_lifetime_)] { if (!life.expired()) close(lib::DialogResult::Cancel); return true; };
    populateControls();
    if (editor_commons_) {
        const std::weak_ptr<int> life = callback_lifetime_;
        ref_data_version_connection_ = editor_commons_->refDataVersion.add(
            [this, life] { if (!life.expired()) refreshWarning(); });
        ref_target_version_connection_ = editor_commons_->refTargetVersion.add(
            [this, life] { if (!life.expired()) refreshWarning(); });
    }
    initialized_ = true;
    refreshWarning();
}

void EventTreeDialog::setup(const std::shared_ptr<lib::Container>& area) {
    if (!area) return;
    content_ = area;
    setup(*area);
}

MatchingType EventTreeDialog::keyCodeMatchingType() const noexcept {
    if (!key_code_matching_type_ ||
        !key_code_matching_type_->selected_index ||
        *key_code_matching_type_->selected_index > 1U)
        return MatchingType::Or;
    return *key_code_matching_type_->selected_index == 0U
        ? MatchingType::Or
        : MatchingType::And;
}

void EventTreeDialog::refreshWarning() {
    if (!name_ || !prop_) return;
    warnings_.clear();
    const auto nameWarnings = warning::sjisWarnings(
        prop_->parent(), summary_, name_->text, prop_->requiredMsgs().eventName.value);
    warnings_.insert(warnings_.end(), nameWarnings.begin(), nameWarnings.end());

    if (every_round_ && every_round_->checked &&
        !prop_->targetVersion(summary_, "1.50"))
        warnings_.push_back(prop_->requiredMsgs().warningEveryRound);
    if (round_end_ && round_end_->checked &&
        !prop_->isTargetVersion(summary_, "4"))
        warnings_.push_back(prop_->requiredMsgs().warningRoundEnd);
    if (round0_ && round0_->checked &&
        !prop_->targetVersion(summary_, "1.50"))
        warnings_.push_back(prop_->requiredMsgs().warningRound0);

    if (key_codes_) {
        const auto keyWarnings = key_codes_->warnings();
        warnings_.insert(warnings_.end(), keyWarnings.begin(), keyWarnings.end());
    }
    if (key_code_matching_type_ && key_code_matching_type_->selected_index &&
        keyCodeMatchingType() == MatchingType::And &&
        !prop_->targetVersion(summary_, "1.50"))
        warnings_.push_back(prop_->requiredMsgs().warningKeyCodeMatchingTypeAnd);
    if (rounds_) {
        const auto roundWarnings = rounds_->warnings();
        warnings_.insert(warnings_.end(), roundWarnings.begin(), roundWarnings.end());
    }
    updateWarningLabel();
}

void EventTreeDialog::updateWarningLabel() {
    if (warning_label_) warning_label_->text = joinWarnings(warnings_);
}

void EventTreeDialog::setAutoName() {
    if (ignore_mod_ || !set_auto_name_ || !name_) return;
    if (!name_->text.empty()) {
        set_auto_name_ = false;
        return;
    }

    String treeName;
    if (enter_ && enter_->checked) {
        switch (capabilities_.kind) {
        case OwnerKind::MenuCard: treeName = prop_->requiredMsgs().selectTree; break;
        case OwnerKind::EnemyCard:
        case OwnerKind::PlayerCardEvents: treeName = prop_->requiredMsgs().deadTree; break;
        case OwnerKind::Battle: treeName = prop_->requiredMsgs().victoryTree; break;
        case OwnerKind::Area:
        case OwnerKind::Generic:
        case OwnerKind::Package: treeName = prop_->requiredMsgs().enterTree; break;
        }
    } else if (lose_ && lose_->checked) {
        treeName = prop_->requiredMsgs().loseTree;
    } else if (escape_ && escape_->checked) {
        treeName = prop_->requiredMsgs().escapeTree;
    } else if (every_round_ && every_round_->checked) {
        treeName = prop_->requiredMsgs().everyRoundTree;
    } else if (round_end_ && round_end_->checked) {
        treeName = prop_->requiredMsgs().roundEndTree;
    } else if (round0_ && round0_->checked) {
        treeName = prop_->requiredMsgs().round0Tree;
    } else if (key_codes_ && !key_codes_->isNewItemEditing() &&
               !key_codes_->keyCodes().empty()) {
        treeName = utils::tryFormat(prop_->requiredMsgs().keyCodeTree.value,
                                    key_codes_->keyCodes().front());
    } else if (rounds_ && !rounds_->isNewItemEditing() &&
               !rounds_->rounds().empty()) {
        treeName = utils::tryFormat(prop_->requiredMsgs().roundTree.value,
                                    rounds_->rounds().front());
    }
    if (treeName.empty()) return;
    set_auto_name_ = false;
    name_->text = std::move(treeName);
}

void EventTreeDialog::unbindExternalNotifications() noexcept {
    if (editor_commons_ && !editor_commons_lifetime_.expired()) {
        if (ref_data_version_connection_)
            editor_commons_->refDataVersion.remove(ref_data_version_connection_);
        if (ref_target_version_connection_)
            editor_commons_->refTargetVersion.remove(ref_target_version_connection_);
    }
    ref_data_version_connection_ = 0;
    ref_target_version_connection_ = 0;
}

EventTreeDialog::~EventTreeDialog() {
    unbindExternalNotifications();
    callback_lifetime_.reset();
    if (ok_button_) ok_button_->on_click = {};
    if (cancel_button_) cancel_button_->on_click = {};
    if (apply_button_) apply_button_->on_click = {};
    if (dialog_) dialog_->on_close_requested = {};
    close();
}

void EventTreeDialog::close(const lib::DialogResult result) {
    if (!dialog_) return;
    const auto dialog=dialog_;
    const auto gui=services_.gui;
    dialog->result = result;
    dialog->set_visible(false);
    if (gui) gui->close(dialog);
}

bool EventTreeDialog::apply() {
    const auto transaction = transaction_;
    const std::weak_ptr<int> life=callback_lifetime_;
    return transaction ? transaction([this, life] { return !life.expired() && applyImpl(); }) : applyImpl();
}

bool EventTreeDialog::applyImpl() {
    if (!initialized_ || !prop_ || !owner_ || !event_tree_ || !name_)
        return false;

    if (read_only_) return true;
    event_tree_->name(name_->text);
    if (enter_) event_tree_->enter(enter_->checked);
    if (lose_) event_tree_->lose(lose_->checked);
    if (escape_) event_tree_->escape(escape_->checked);
    if (every_round_) event_tree_->everyRound(every_round_->checked);
    if (round_end_) event_tree_->roundEnd(round_end_->checked);
    if (round0_) event_tree_->round0(round0_->checked);
    if (key_codes_) {
        lib::Array<system::FKeyCode> values;
        values.reserve(key_codes_->keyCodes().size());
        for (const auto& keyCode : key_codes_->keyCodes())
            values.push_back(prop_->sys().toFKeyCode(keyCode));
        event_tree_->keyCodes(std::move(values));
    }
    if (key_code_matching_type_ && key_code_matching_type_->selected_index)
        event_tree_->keyCodeMatchingType(keyCodeMatchingType());
    if (rounds_) event_tree_->rounds(rounds_->rounds());

    dialog_->title = utils::tryFormat(prop_->requiredMsgs().dlgTitEventTree.value,
                                      event_tree_->name());
    modified_ = false;
    create_ = false;
    if (apply_button_) apply_button_->set_enabled(false);
    return true;
}

bool EventTreeDialog::open() {
    createDialog();
    if (!dialog_) return false;
    if (!content_) {
        content_ = std::make_shared<lib::Panel>();
        content_->set_layout_item({.horizontal = lib::Align::Stretch,
                                   .vertical = lib::Align::Stretch,
                                   .minimum = {350.0F, 220.0F},
                                   .preferred = {
                                       capabilities_.canHasFireRound ? 450.0F : 350.0F,
                                       300.0F},
                                   .grow = 1.0F});
    }
    if (!initialized_) setup(*content_);
    if (std::find(dialog_->children().begin(), dialog_->children().end(),
                  content_) == dialog_->children().end())
        dialog_->add(content_);

    if (!services_.gui) return false;
    dialog_->result = lib::DialogResult::None;
    services_.gui->show(dialog_);
    services_.gui->restore_window(dialog_);
    return true;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::eventtreedialog
