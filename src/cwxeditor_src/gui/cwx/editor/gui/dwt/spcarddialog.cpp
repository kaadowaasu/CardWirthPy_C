#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "spcarddialog.hpp"
#include "summaryskinbinding.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/msgutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/messageutils.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "commons.hpp"
#include "xmlbytestransfer.hpp"
#include <charconv>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>

namespace cwxeditor::cwx::editor::gui::dwt::spcarddialog {
namespace {

constexpr int kCardSpeedMax = 10;

[[nodiscard]] Skin::Ptr retainSkin(const Skin* value) {
    if (!value) return {};
    if (auto retained = const_cast<Skin*>(value)->weak_from_this().lock())
        return retained;
    // 生ポインターだけを受け取る公開オーバーロードは、借用の寿命のままにする。
    // 呼び出し元が所有する Skin に、所有権を持つ制御ブロックを新設しない。
    return Skin::Ptr(Skin::Ptr{}, const_cast<Skin*>(value));
}

[[nodiscard]] std::string replaceFirst(std::string value,
                                       std::string_view needle,
                                       std::string_view replacement) {
    const auto position = value.find(needle);
    if (position != std::string::npos)
        value.replace(position, needle.size(), replacement);
    return value;
}

[[nodiscard]] std::string normalizedText(std::string value) {
    return dutils::wrapReturnCode(std::move(value));
}

[[nodiscard]] std::string firstImagePath(const CardImageArray& images) {
    for (const auto& image : images) {
        if (!image) continue;
        if (image->type == types::CardImageType::File && !image->path().empty())
            return image->path();
    }
    return {};
}

[[nodiscard]] std::string actionTypeName(types::ActionCardType type) {
    switch (type) {
    case types::ActionCardType::Exchange: return "カード交換";
    case types::ActionCardType::Attack: return "攻撃";
    case types::ActionCardType::PowerfulAttack: return "渾身の一撃";
    case types::ActionCardType::CriticalAttack: return "会心の一撃";
    case types::ActionCardType::Feint: return "フェイント";
    case types::ActionCardType::Defense: return "防御";
    case types::ActionCardType::Distance: return "見切り";
    case types::ActionCardType::Confuse: return "混乱";
    case types::ActionCardType::RunAway: return "逃走";
    }
    return {};
}

[[nodiscard]] std::vector<types::ActionCardType> defaultActionTypes() {
    return {types::ActionCardType::Exchange,
            types::ActionCardType::Attack,
            types::ActionCardType::PowerfulAttack,
            types::ActionCardType::CriticalAttack,
            types::ActionCardType::Feint,
            types::ActionCardType::Defense,
            types::ActionCardType::Distance,
            types::ActionCardType::Confuse,
            types::ActionCardType::RunAway};
}

template <typename T>
[[nodiscard]] int integerValue(const std::shared_ptr<T>& control,
                               int fallback = 0) noexcept {
    if (!control) return fallback;
    const auto value = std::clamp(control->value,
                                  static_cast<double>(std::numeric_limits<int>::min()),
                                  static_cast<double>(std::numeric_limits<int>::max()));
    return static_cast<int>(std::lround(value));
}

[[nodiscard]] std::shared_ptr<lib::GroupBox> makeGroup(
    const lib::Services& services, const std::shared_ptr<lib::Container>& parent,
    std::string title) {
    auto group = makeControl<lib::GroupBox>(services, lib::ComponentKind::GroupBox);
    group->title = std::move(title);
    auto layout = std::make_shared<lib::LinearLayout>();
    // 部品間の間隔は5論理画素、グループ
    // 余白は7・5・7・5に設定する。
    layout->padding = {7.0F, 5.0F, 7.0F, 5.0F};
    layout->spacing = 5.0F;
    group->set_layout(layout);
    if (parent) parent->add(group);
    return group;
}

[[nodiscard]] std::shared_ptr<lib::Label> makeLabel(
    const lib::Services& services, const std::shared_ptr<lib::Container>& parent,
    std::string text) {
    auto label = makeControl<lib::Label>(services, lib::ComponentKind::Label);
    label->text = std::move(text);
    if (parent) parent->add(label);
    return label;
}

void compactMenuCardAreas(const std::shared_ptr<lib::Container>& parent) {
    if (!parent) return;
    // ALL仕様: 見出し付き領域だけでなく、その内部のパネルも上下paddingを0にする。
    if (const auto layout = parent->layout()) {
        layout->padding.top = 0.0F;
        layout->padding.bottom = 0.0F;
    }
    for (const auto& child : parent->children()) {
        const auto container = std::dynamic_pointer_cast<lib::Container>(child);
        if (!container) continue;
        if (std::dynamic_pointer_cast<lib::GroupBox>(container)) {
            auto item = container->layout_item();
            item.margin.top = 0.0F;
            item.margin.bottom = 0.0F;
            container->set_layout_item(item);
        }
        compactMenuCardAreas(container);
    }
}

}  // namespace

std::shared_ptr<lib::Panel> makePanel(const lib::Services& services) {
    return makeControl<lib::Panel>(services, lib::ComponentKind::Panel);
}

std::string comboValue(const lib::ComboBox& combo) {
    if (combo.editable && !combo.edit_text.empty()) return combo.edit_text;
    if (combo.selected_index && *combo.selected_index < combo.items.size()) {
        const auto& item = combo.items[*combo.selected_index];
        return item.value.empty() ? item.text : item.value;
    }
    return combo.edit_text;
}

void setComboValue(lib::ComboBox& combo, std::string_view value) {
    combo.edit_text = std::string(value);
    combo.selected_index.reset();
    for (std::size_t index = 0; index < combo.items.size(); ++index) {
        const auto& item = combo.items[index];
        if (item.value == value || item.text == value) {
            combo.selected_index = index;
            break;
        }
    }
}

std::string messageValue(const msgs::Message& message) { return message.value; }

std::string formatMessage(std::string value, std::string_view argument) {
    value = replaceFirst(std::move(value), "%1$s", argument);
    value = replaceFirst(std::move(value), "%s", argument);
    return value;
}

std::string formatMessage(std::string value, std::string_view first,
                          std::string_view second) {
    value = replaceFirst(std::move(value), "%1$s", first);
    value = replaceFirst(std::move(value), "%2$s", second);
    return value;
}

bool isTargetVersion(const Props* prop, const Summary* summ,
                    std::string_view version) {
    if (!prop) return true;
    // D版の Props と同様、Summary がない場合は設定された対象バージョンで決める。
    return prop->isTargetVersion(summ, version);
}

CardImageSelector::CardImageSelector(
    SpCardDialogContext context, Props& prop, const Summary* summ,
    const std::shared_ptr<lib::Container>& parent, types::CardImagePosition position,
    const bool read_only, const bool sp_card_definitions,
    const std::optional<structs::CInsets> preview_insets,
    std::function<lib::String()> export_name, const Skin* explicit_force_skin)
    : context_(std::move(context)), prop_(&prop), summ_(summ), position_(position) {
    panel_ = makePanel(context_.services);
    panel_->set_layout(std::make_shared<lib::FillLayout>());
    const auto size = prop.looks().cardSize();
    const auto insets = prop.s(preview_insets.value_or(prop.looks().cardInsets()));
    const float preview_width = static_cast<float>(
        prop.s(static_cast<std::int32_t>(size.width)) + insets.w + insets.e);
    const float preview_height = static_cast<float>(
        prop.s(static_cast<std::int32_t>(size.height)) + insets.n + insets.s);
    const float selector_width = std::max(
        260.0F, preview_width + static_cast<float>(prop.var().filesWidth.value) + 34.0F);
    const float selector_height = std::max(
        220.0F, std::max(preview_height + 92.0F,
                         static_cast<float>(prop.var().filesHeight.value) + 70.0F));
    // ImageSelectBase は内側に GroupBox を持つ。Linear/Grid
    // は外側の Panel 自体を計測するため、 指定がないと汎用の高さ
    // 28pxになり、子の選択部品がすべて隠れる。 外側にDWTの ImageSelect
    // の自然な寸法を与え、ページの残りの高さを使わせる。
    panel_->set_layout_item({.horizontal = lib::Align::Stretch,
                             .vertical = lib::Align::Stretch,
                             .minimum = {selector_width, selector_height},
                             .preferred = {selector_width, selector_height},
                             .grow = 1.0F});
    imageselect::SkinPtr force_skin;
    // 通常のシナリオの操作欄は、D版と同様に Commons.skin に従う。 取
    // り込み元・切り離された状況で解決したスキンと、読み取り専用の操作欄は固定する。
    const bool live_scenario_skin = !read_only && summ && !summ->scenarioPath().empty() &&
        context_.editor_commons && context_.editor_commons->summary().get() == summ;
    const auto* pinned_skin = explicit_force_skin ? explicit_force_skin
        : (!live_scenario_skin ? context_.common.skin : nullptr);
    if (pinned_skin) {
        force_skin = imageselect::SkinPtr(
            const_cast<imageselect::Skin*>(pinned_skin),
            [](imageselect::Skin*) noexcept {});
    }
    imageselect::ImageSelectBase::Definitions definitions;
    if (sp_card_definitions) {
        definitions = [&prop](const bool included) {
            std::vector<lib::String> values{
                prop.requiredMsgs().defaultSelection(prop.requiredMsgs().imageNone.value)};
            if (included)
                values.push_back(prop.requiredMsgs().defaultSelection(
                    prop.requiredMsgs().imageIncluding.value));
            for (std::uint32_t pc = 1U; pc <= prop.var().partyMax.value; ++pc)
                values.push_back(prop.requiredMsgs().defaultSelection(
                    ::cwxeditor::cwx::utils::tryFormat(
                        prop.requiredMsgs().pcNumber.value, pc)));
            return values;
        };
    }
    selector_ = std::make_unique<imageselect::ImageSelectBase>(
        imageselect::ImageSelectKind::Card, false, panel_, read_only ? 1 : 0, prop,
        const_cast<Summary*>(summ),
        prop.s(static_cast<std::int32_t>(size.width)),
        prop.s(static_cast<std::int32_t>(size.height)),
        insets, position, true,
        std::move(export_name), lib::Action{},
        std::move(definitions), sp_card_definitions, std::move(force_skin),
        context_.services);
    if (context_.editor_commons) {
        editor_commons_lifetime_ = context_.editor_commons->lifetimeToken();
        selector_->bindEditorCommons(context_.editor_commons);
    }
    if (sp_card_definitions) {
        selector_->valueFromDef([](int index, const bool included,
                                   const lib::StringView binary_path)
                                    -> lib::Owner<CardImage> {
            if (index <= 0) return std::make_shared<CardImage>(lib::String{});
            if (included && index == 1)
                return std::make_shared<CardImage>(lib::String(binary_path));
            if (included) --index;
            return index > 0
                       ? std::make_shared<CardImage>(
                             static_cast<std::uint32_t>(index))
                       : std::make_shared<CardImage>(lib::String{});
        });
        selector_->valueToDef([](const CardImage& image, const bool included) {
            switch (image.type) {
            case types::CardImageType::File:
                if (image.path().empty()) return 0;
                if (included && ::cwxeditor::cwx::utils::isBinImg(image.path()))
                    return 1;
                return -1;
            case types::CardImageType::PCNumber: {
                auto index = static_cast<int>(image.pcNumber());
                if (included) ++index;
                return index;
            }
            case types::CardImageType::Talker:
                return -1;
            }
            return -1;
        });
        selector_->indexOfBinPath(
            [](const bool included) { return included ? 1 : -1; });
        selector_->refreshList();
    }
    selector_->modEvent.push_back([this] {
        if (syncing_) return;
        images_ = selector_->cardImages();
        notifyChanged();
    });
    if (parent) parent->add(panel_);
}

void CardImageSelector::images(CardImageArray value) {
    images_ = std::move(value);
    if (selector_) {
        syncing_ = true;
        selector_->setCardImages(images_, false);
        syncing_ = false;
    }
}

void CardImageSelector::refreshList(const std::size_t selection) {
    if (!image_list_) return;
    std::vector<lib::ListItem> items;
    items.reserve(images_.size());
    for (const auto& image : images_) {
        std::string label = "(empty)";
        if (image) {
            if (image->type == types::CardImageType::File) label = image->path();
            else if (image->type == types::CardImageType::PCNumber)
                label = "PC#" + std::to_string(image->pcNumber());
            else label = "Talker";
        }
        items.push_back({label, label});
    }
    std::vector<std::size_t> selected;
    if (!images_.empty()) selected.push_back(std::min(selection, images_.size() - 1U));
    (void)lib::replace_list_items(image_list_, std::move(items), std::move(selected));
}

std::vector<lib::String> CardImageSelector::warnings() const {
    if (selector_) return selector_->warnings();
    if (context_.image_warnings) return context_.image_warnings(images_);
    std::vector<lib::String> result;
    if (!prop_ || !summ_) return result;
    for (const auto& image : images_) {
        if (!image) continue;
        if (image->type == types::CardImageType::PCNumber && summ_->legacy() &&
            !prop_->isTargetVersion(summ_, "1")) {
            result.push_back(messageValue(prop_->requiredMsgs().warningPCNumberClassic));
        }
        if (image->type == types::CardImageType::File &&
            image->path().find(".bin") != std::string::npos &&
            !prop_->isTargetVersion(summ_, "1")) {
            result.push_back(messageValue(prop_->requiredMsgs().warningIncludedImage));
        }
    }
    return result;
}

bgimagedialog::MaterialResult CardImageSelector::materialPath(bool force) {
    if (selector_) {
        const auto material = selector_->cardMaterialPath(force);
        images_ = material.images;
        bgimagedialog::MaterialResult result;
        result.cancel = material.cancel;
        result.path = firstImagePath(images_);
        return result;
    }
    if (!context_.select_material) return {};
    const auto result = context_.select_material(path_ ? path_->text : std::string{});
    if (result.cancel) return result;
    if (!result.path.empty()) {
        if (context_.select_images) {
            images_ = context_.select_images(result.path);
        } else {
            images_.clear();
            images_.push_back(std::make_shared<CardImage>(result.path, position_));
        }
        if (path_) path_->text = result.path;
        refreshList(images_.empty() ? 0U : images_.size() - 1U);
        notifyChanged();
    } else if (force && images_.empty()) {
        // 強制適用でも、明示的な空の画像選択は受け付ける。
        images_.clear();
    }
    return result;
}

void CardImageSelector::previewSize(const std::int32_t width,
                                    const std::int32_t height,
                                    const structs::CInsets& insets) noexcept {
    if (selector_) selector_->setPreviewSize(width, height, insets);
}

void CardImageSelector::imageListOwner(lib::Owner<lib::Window> value) noexcept {
    if (selector_) selector_->imageListOwner(std::move(value));
}

void CardImageSelector::active(const bool value) {
    if (!selector_) return;
    selector_->bindEditorCommons(value && !editor_commons_lifetime_.expired()
        ? context_.editor_commons : nullptr);
    // active() が子ウィンドウを同期的に閉じる場合があるため、その後は this にアクセスしない。
    selector_->active(value);
}

void CardImageSelector::enabled(const bool value) {
    if (selector_) selector_->enabled(value);
}

void CardImageSelector::compactForAll() {
    if (!selector_ || !panel_) return;
    const float removed = selector_->compactForAll();
    auto item = panel_->layout_item();
    item.minimum.height = std::max(0.0F, item.minimum.height - removed);
    item.preferred.height = std::max(0.0F, item.preferred.height - removed);
    panel_->set_layout_item(item);
}

void CardImageSelector::notifyChanged() {
    for (const auto& callback : modEvent) {
        if (callback) callback();
    }
}

void CardImageSelector::chooseMaterial(bool force) {
    static_cast<void>(materialPath(force));
}

CardAnimationPanel::CardAnimationPanel(areaviewutils::Commons common,
                                       const Summary* summ,
                                       std::shared_ptr<lib::Container> parent,
                                       CardAnimationPanelType type)
    : common_(std::move(common)), summ_(summ), type_(type) {
    panel_ = makePanel(common_.services);
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = type == CardAnimationPanelType::SetFlag ? 4U : 3U;
    panel_->set_layout(layout);
    const auto* prop = common_.prop;
    const auto override_label = type == CardAnimationPanelType::SetFlag
        ? (prop ? messageValue(prop->requiredMsgs().overrideCardAnimationSpeed)
                : "カード速度を指定")
        : (prop ? messageValue(prop->requiredMsgs().overrideAnimationSpeed)
                : "速度を指定");
    override_card_speed_ = makeControl<lib::CheckBox>(common_.services,
                                                        lib::ComponentKind::CheckBox);
    override_card_speed_->text = override_label;
    override_card_speed_->on_changed = [this](bool) {
        updateEnabled();
        raiseModEvent();
    };
    override_card_speed_->set_layout_item(
        {.row = 0U, .column = 0U,
         .horizontal = type == CardAnimationPanelType::SetFlag
                           ? lib::Align::Stretch
                           : lib::Align::Start,
         .grow = type == CardAnimationPanelType::SetFlag ? 1.0F : 0.0F});
    panel_->add(override_card_speed_);
    if (type == CardAnimationPanelType::SetFlag ||
        type == CardAnimationPanelType::MoveCard) {
        force_override_ = makeControl<lib::CheckBox>(common_.services,
                                                      lib::ComponentKind::CheckBox);
        force_override_->text = prop
            ? messageValue(prop->requiredMsgs().forceOverrideCardAnimationSpeed)
            : "カード本体の設定より優先";
        force_override_->on_changed = [this](bool) { raiseModEvent(); };
        if (type == CardAnimationPanelType::MoveCard) {
            force_override_->set_layout_item({.row = 1U, .column = 0U,
                                               .column_span = 3U,
                                               .horizontal = lib::Align::Stretch});
        } else {
            force_override_->set_layout_item(
                {.row = 0U, .column = 1U, .minimum = {24.0F, 0.0F},
                 .grow = 1.0F});
        }
        panel_->add(force_override_);
    }
    speed_ = makeControl<lib::NumberEdit>(common_.services,
                                          lib::ComponentKind::NumberEdit);
    speed_->minimum = 0.0;
    speed_->maximum = static_cast<double>(kCardSpeedMax);
    speed_->value = static_cast<double>(kCardSpeedMax / 2);
    speed_->on_changed = [this](double) { raiseModEvent(); };
    speed_->set_layout_item({.row = 0U,
                             .column = type == CardAnimationPanelType::SetFlag ? 2U : 1U,
                             .horizontal = lib::Align::Stretch,
                             .vertical = type == CardAnimationPanelType::SetFlag
                                 ? lib::Align::Center
                                 : lib::Align::Stretch,
                             .minimum = {type == CardAnimationPanelType::SetFlag ? 32.0F : 0.0F,
                                         type == CardAnimationPanelType::SetFlag ? 20.0F : 0.0F},
                             .preferred = {type == CardAnimationPanelType::SetFlag ? 32.0F : 0.0F,
                                           type == CardAnimationPanelType::SetFlag ? 24.0F : 0.0F}});
    panel_->add(speed_);
    auto range = makeLabel(common_.services, panel_,
                            prop ? formatMessage(messageValue(prop->requiredMsgs().rangeHint),
                                                 "0", std::to_string(kCardSpeedMax))
                                 : "0 - 10");
    range->set_layout_item({.row = 0U,
                            .column = type == CardAnimationPanelType::SetFlag ? 3U : 2U,
                            .horizontal = lib::Align::Start,
                            .vertical = lib::Align::Center,
                            .grow = type == CardAnimationPanelType::SetFlag ? 1.0F : 0.0F});
    if (parent) parent->add(panel_);
    updateEnabled();
}

CardAnimationPanel::CardAnimationPanel(areaviewutils::Commons common,
                                       const Summary* summ, lib::Container& parent,
                                       CardAnimationPanelType type)
    : CardAnimationPanel(std::move(common), summ,
                         std::shared_ptr<lib::Container>(&parent,
                             [](lib::Container*) {}), type) {}

std::vector<lib::String> CardAnimationPanel::warnings() const {
    if (override_card_speed_ && override_card_speed_->checked &&
        !isTargetVersion(common_.prop, summ_, "4")) {
        return {messageValue(common_.prop->requiredMsgs().warningCardAnimationSpeed)};
    }
    return {};
}

void CardAnimationPanel::speed(int value) {
    if (!override_card_speed_ || !speed_) return;
    override_card_speed_->checked = value != -1;
    speed_->value = static_cast<double>(value == -1 ? kCardSpeedMax / 2
                                                    : std::clamp(value, 0, kCardSpeedMax));
    updateEnabled();
}

int CardAnimationPanel::speed() const {
    if (!override_card_speed_ || !override_card_speed_->checked || !speed_)
        return -1;
    return integerValue(speed_);
}

void CardAnimationPanel::overrideCardSpeed(bool value) {
    if (!force_override_) return;
    force_override_->checked = value;
}

bool CardAnimationPanel::overrideCardSpeed() const {
    return force_override_ && override_card_speed_ &&
           override_card_speed_->checked && force_override_->checked;
}

void CardAnimationPanel::updateEnabled() {
    if (!override_card_speed_) return;
    const bool legacy = summ_ && summ_->legacy();
    override_card_speed_->set_enabled(!legacy || override_card_speed_->checked);
    if (speed_) speed_->set_enabled(override_card_speed_->checked);
    if (force_override_)
        force_override_->set_enabled(override_card_speed_->checked);
    // 部品の状態は HWND とは独立して保持するため、選
    // 択コールバックから明示的に同期を要求する。 そうしな
    // いと、無関係な再描画まで数値欄が無効なままになる。
    if (common_.services.gui) {
        if (speed_) common_.services.gui->request_redraw(speed_);
        if (force_override_) common_.services.gui->request_redraw(force_override_);
    }
}

void CardAnimationPanel::raiseModEvent() {
    for (const auto& callback : modEvent) {
        if (callback) callback();
    }
}

template <typename C>
std::string SpCardDialog<C>::dialogTitle(Props& prop, const Summary* summ,
                                         const CardPtr& card, bool create) {
    if constexpr (std::is_same_v<C, MenuCard>) {
        if (create) return messageValue(prop.requiredMsgs().dlgTitNewMenuCard);
        return formatMessage(messageValue(prop.requiredMsgs().dlgTitMenuCard),
                             card ? card->name() : std::string{});
    } else {
        if (create) return messageValue(prop.requiredMsgs().dlgTitNewEnemyCard);
        std::string name;
        if (card && summ) {
            if (const auto cast = summ->cwCast(card->id())) name = cast->name();
        }
        if (name.empty() && card)
            name = formatMessage(messageValue(prop.requiredMsgs().noCast),
                                 std::to_string(card->id()));
        return formatMessage(messageValue(prop.requiredMsgs().dlgTitEnemyCard), name);
    }
}

namespace {

[[nodiscard]] bool hasServices(const lib::Services& services) noexcept {
    return static_cast<bool>(services.gui) || static_cast<bool>(services.graphics) ||
           static_cast<bool>(services.dialogs) || static_cast<bool>(services.filesystem) ||
           static_cast<bool>(services.dispatcher) || static_cast<bool>(services.drag_drop);
}

[[nodiscard]] lib::Services chooseServices(const SpCardDialogContext& context,
                                           Props& prop) {
    return hasServices(context.services) ? context.services : prop.images().services();
}

}  // namespace

template <typename C>
SpCardDialog<C>::SpCardDialog(SpCardDialogContext context, Props& prop,
                              const Summary* summ, const Skin* force_skin,
                              CardPtr card, bool create,
                              std::shared_ptr<lib::Window> parent,
                              UseCounterPtr counter)
    : absdialog::AbsDialog(prop, parent, dialogTitle(prop, summ, card, create),
                           prop.images().cards(), true,
                           std::is_same_v<C, MenuCard>
                               ? static_cast<DSize*>(&prop.flex().menuCardDlg)
                               : static_cast<DSize*>(&prop.flex().enemyCardDlg),
                           true, true,
                           {}, chooseServices(context, prop)),
      context_(std::move(context)), prop_(&prop), summ_(summ),
      counter_(std::move(counter)), card_(std::move(card)), create_(create),
      summ_skin_(force_skin ? force_skin : context_.common.skin),
      force_skin_(force_skin) {
    context_.common.prop = &prop;
    if (!hasServices(context_.services)) context_.services = prop.images().services();
    if (!hasServices(context_.common.services)) context_.common.services = context_.services;
    summary_skin_pinned_ = summaryskinbinding::pinsSummarySkin(false, summ_);
    if (summary_skin_pinned_ && !force_skin_ && context_.editor_commons) {
        source_skin_owner_ = editor_commons::findSkin(
            *context_.editor_commons, prop, summ_);
        if (source_skin_owner_) summ_skin_ = source_skin_owner_.get();
    }
    // D版では初回寸法を部品から
    // 求めて保存する。C++版は
    // 初回配置前に子孫の寸法を取
    // 得できないため、最小クライ
    // アント寸法を指定して補う。
    dialog()->minimum_size = lib::Size{520.0F, 609.0F};
    if (std::is_same_v<C, EnemyCard> && lib::editor_window_host_options().all_mode) {
        // ALLのエネミーカードでは初期最小クライアント高を565pxにする。
        dialog()->minimum_size = lib::Size{520.0F, 565.0F};
        dialog()->initial_minimum_size = true;
    }
    if (std::is_same_v<C, MenuCard> && lib::editor_window_host_options().all_mode) {
        // モバイルの ALL では、メニューカードの操作欄を低いモーダル画面内でスクロールさせる。
        dialog()->minimum_size = lib::Size{540.0F, 500.0F};
        dialog()->initial_minimum_size = true;
    }
    enterClose(true);
    closeEvent.push_back([this, cast_life = std::weak_ptr<int>{cast_host_lifetime_}] {
        if (cast_life.expired()) return;
        // MainWindow は閉じたモードレスエディタを保持するが、 複製済みの選択・ド
        // ロップのコールバックは終了したセッションに属し、保持された保存領域のものではない。
        cast_host_lifetime_.reset();
        unbindExternalNotifications();
    });
    // 保持されるモードレスのホストでも、登録した素材のドロップ先を無効化する。
    closeEvent.push_back([this] {
        const auto selector = image_selector_;
        if (selector) selector->active(false);
    });
    openedEvent.push_back([this] {
        const auto selector = image_selector_;
        if (selector) selector->active(true);
    });
    if (create_) applyEnabled(true);
}

template <typename C>
SpCardDialog<C>::SpCardDialog(areaviewutils::Commons common, Props& prop,
                              std::shared_ptr<lib::Window> parent,
                              const Summary* summ, const Skin* force_skin,
                              UseCounterPtr counter, CardPtr card, bool create)
    : SpCardDialog(SpCardDialogContext(std::move(common)), prop, summ,
                   force_skin, std::move(card), create, std::move(parent),
                   std::move(counter)) {}

template <typename C>
void SpCardDialog<C>::setup(Container& area) {
    if (setup_complete_) return;
    auto root = makePanel(context_.services);
    auto root_layout = std::make_shared<lib::GridLayout>();
    root_layout->columns = 1U;
    root_layout->spacing = 0.0F;
    root->set_layout(root_layout);
    root->set_layout_item({.horizontal = lib::Align::Stretch,
                           .vertical = lib::Align::Stretch,
                           .grow = 1.0F});
    area.add(root);

    // D版と同様、カード固有の操作欄と参照フラグを横方向の SplitPane
    // に置く。 実際の SplitView を使い、すべて
    // のネイティブバックエンドで調整可能な仕切りを維持する。
    auto split = makeControl<lib::SplitView>(context_.services,
                                             lib::ComponentKind::SplitView);
    split->orientation = lib::Orientation::Horizontal;
    if constexpr (std::is_same_v<C, MenuCard>) {
        split->weights = {
            static_cast<float>(std::max(1, prop_->var().menuCardSashL.value)),
            static_cast<float>(std::max(1, prop_->var().menuCardSashR.value))};
        split->on_weights_changed = [this](const std::vector<float>& weights) {
            if (weights.size() < 2U) return;
            prop_->var().menuCardSashL = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[0])));
            prop_->var().menuCardSashR = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[1])));
        };
    } else {
        split->weights = {
            static_cast<float>(std::max(1, prop_->var().enemyCardSashL.value)),
            static_cast<float>(std::max(1, prop_->var().enemyCardSashR.value))};
        split->on_weights_changed = [this](const std::vector<float>& weights) {
            if (weights.size() < 2U) return;
            prop_->var().enemyCardSashL = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[0])));
            prop_->var().enemyCardSashR = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[1])));
        };
    }
    const float split_height = std::is_same_v<C, MenuCard> &&
        lib::editor_window_host_options().all_mode ? 292.0F : 302.0F;
    split->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .minimum = {0.0F, split_height},
                            .preferred = {0.0F, split_height},
                            .grow = 1.0F});
    root->add(split);
    auto left = makePanel(context_.services);
    left->set_layout(std::make_shared<lib::GridLayout>());
    left->set_layout_item({.horizontal = lib::Align::Stretch,
                           .vertical = lib::Align::Stretch,
                           .grow = 1.0F});
    split->add(left);
    auto side = makePanel(context_.services);
    side->set_layout(std::make_shared<lib::GridLayout>());
    if (std::is_same_v<C, EnemyCard> && lib::editor_window_host_options().all_mode) {
        // ALL右列は各設定を縦に順番に配置し、Gridの固定セルの重複を避ける。
        auto column = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
        column->spacing = 2.0F;
        column->padding = {};
        side->set_layout(column);
    }
    side->set_layout_item({.horizontal = lib::Align::Stretch,
                           .vertical = lib::Align::Stretch,
                           .grow = 1.0F});
    split->add(side);
    if constexpr (std::is_same_v<C, MenuCard>)
        createMenuControls(left);
    else
        createEnemyControls(left);
    createSideControls(side);
    createCommonControls(root, side);
    if constexpr (std::is_same_v<C, MenuCard>) {
        if (lib::editor_window_host_options().all_mode)
            compactMenuCardAreas(root);
    }
    setInitialValues();
    setup_complete_ = true;
    updateEnabled();
    refreshWarning();
    bindExternalNotifications();
}

template <typename C>
void SpCardDialog<C>::createMenuControls(
    const std::shared_ptr<lib::Container>& root) {
    const auto name_title = prop_ ? messageValue(prop_->requiredMsgs().name) : "Name";
    auto base = makeGroup(context_.services, root, name_title);
    base->set_layout_item({.horizontal = lib::Align::Stretch,
                           .minimum = {0.0F, 72.0F},
                           .preferred = {0.0F, 72.0F}});
    name_ = makeControl<lib::TextEdit>(context_.services, lib::ComponentKind::TextEdit);
    name_->on_changed = [this](std::string_view) {
        updateToolTip();
        refreshWarning();
    };
    mod(*name_);
    base->add(name_);
    expand_sp_chars_ = makeControl<lib::CheckBox>(context_.services,
                                                   lib::ComponentKind::CheckBox);
    expand_sp_chars_->text = prop_ ? messageValue(prop_->requiredMsgs().expandSPChars)
                                   : "Expand special characters";
    expand_sp_chars_->set_layout_item({.horizontal = lib::Align::End});
    expand_sp_chars_->on_changed = [this](bool) {
        updateEnabled();
        updateToolTip();
        refreshWarning();
    };
    mod(*expand_sp_chars_);
    base->add(expand_sp_chars_);
    text_menus_.push_back(setupSpecialTextMenu(name_, [this] {
        return expand_sp_chars_ && expand_sp_chars_->checked;
    }));

    // D版と同様、左ペインに ImageSelect を直接入れる。 ImageSelect
    // 自身が画像の Group を持つため、別の Group
    // で囲むと見出しが重複し、その高さの分だけ領域を失う。
    image_selector_ = std::make_shared<CardImageSelector>(
        context_, *prop_, summ_, root, types::CardImagePosition::TopLeft,
        false, true, prop_->looks().menuCardInsets(),
        [prop = prop_, summ = summ_, name = std::weak_ptr<lib::TextEdit>(name_)] {
            const auto control = name.lock();
            return summary::toExportedImageNameWithCardName(prop->parent(),
                summ ? summ->scenarioName() : std::string{},
                summ ? summ->author() : std::string{},
                control ? control->text : std::string{});
        }, force_skin_);
    image_selector_->imageListOwner(dialog());
    image_selector_->modEvent.push_back([this] {
        if (!ignoreMod()) applyEnabled();
        refreshWarning();
        notifyToolbar();
    });
    // ALL仕様: 高さ600pxでも縦スクロールが出ないよう、イメージ一覧を1行縮め、
    // レイヤ名の上下を詰める。
    if (lib::editor_window_host_options().all_mode)
        image_selector_->compactForAll();

}

template <typename C>
void SpCardDialog<C>::createEnemyControls(
    const std::shared_ptr<lib::Container>& root) {
    auto base = makeGroup(context_.services, root,
                          prop_ ? messageValue(prop_->requiredMsgs().enemyCardBase)
                                : "Enemy card");
    casts_ = makeControl<lib::ComboBox>(context_.services,
                                        lib::ComponentKind::ComboBox);
    casts_->on_selection_changed = [this, host_life = std::weak_ptr<int>{cast_host_lifetime_}](std::optional<std::size_t> index) {
        if (host_life.expired()) return;
        if (index && *index < cast_ids_.size()) selected_id_ = cast_ids_[*index];
        redrawPreview();
        if (host_life.expired()) return;
        refreshWarning();
        if (!host_life.expired()) updateEnabled();
    };
    mod(*casts_);
    base->add(casts_);
    const std::weak_ptr<int> host_life = cast_host_lifetime_;
    incsearch::IncSearchSettings searchSettings;
    if (prop_) {
        searchSettings.startIncrementalSearchWhenKeyDown = prop_->var().startIncrementalSearchWhenKeyDown.value;
        const auto* search_prop = prop_;
        searchSettings.startIncrementalSearchWhenKeyDownProvider = [search_prop] {
            return search_prop && search_prop->var().startIncrementalSearchWhenKeyDown.value;
        };
        searchSettings.incrementalSearchBoxWidth = prop_->var().incrementalSearchBoxWidth.value;
        searchSettings.incrementalSearchType = prop_->var().incrementalSearchType.value;
        searchSettings.containsLabel = prop_->requiredMsgs().incSearchContains.value;
        searchSettings.wildcardLabel = prop_->requiredMsgs().incSearchWildcard.value;
        searchSettings.regexLabel = prop_->requiredMsgs().incSearchRegex.value;
        searchSettings.windowTitle = prop_->requiredMsgs().menuText(types::MenuID::IncSearch);
    }
    cast_inc_search_ = std::make_unique<incsearch::IncSearch>(context_.services, casts_.get(),
        [this, host_life] { return !host_life.expired() && !castList().empty(); },
        std::vector<incsearch::AdditionMatcher>{}, std::move(searchSettings));
    cast_inc_search_->modEvent.push_back([this, host_life] { if (!host_life.expired()) refreshCasts(); });
    casts_->on_key_down = [this, host_life](lib::KeyEvent& event) {
        if (!host_life.expired() && cast_inc_search_)
            event.handled = cast_inc_search_->handleHostKeyDown(event, casts_.get()) || event.handled;
    };
    casts_->on_text_input = [this, host_life](lib::TextInputEvent& event) {
        if (!host_life.expired() && cast_inc_search_)
            event.handled = cast_inc_search_->handleHostTextInput(event, casts_.get()) || event.handled;
    };
    cast_popup_ = std::make_shared<lib::Menu>(true);
    cast_popup_->items = {{prop_ ? prop_->requiredMsgs().menuText(types::MenuID::IncSearch) : "Search", "incremental-search"},
                          {}, {prop_ ? prop_->requiredMsgs().menuText(types::MenuID::OpenAtCardView) : "Open at card view", "open-card-view"}};
    cast_popup_->items[1].separator = true;
    cast_popup_->on_open = [this, host_life] {
        if (host_life.expired()) return;
        const auto menu = cast_popup_;
        const auto casts = casts_;
        const auto searchable = !castList().empty();
        if (host_life.expired()) return;
        menu->items[0].enabled = searchable;
        menu->items[2].enabled = casts && casts->selected_index.has_value();
    };
    cast_popup_->on_command = [this, host_life](std::string_view command) {
        if (host_life.expired()) return;
        if (command == "incremental-search") {
            const auto gui = context_.services.gui;
            const auto casts = casts_;
            const auto search = cast_inc_search_.get();
            if (!gui || !casts || !search) return;
            // D版と同様、検索を開く前にキャスト一覧を有効化する。 先に別の編集を確定し
            // 、ポップアップが戻り先のフォーカス所有者として一覧を記憶できるようにする。
            (void)gui->focus_control(casts, true);
            if (host_life.expired() || !dialog()->visible()) return;
            if (context_.services.gui != gui || casts != casts_ ||
                search != cast_inc_search_.get()) return;
            search->startIncSearch();
        } else if (command == "open-card-view") openCardView();
    };
    casts_->on_pointer = [this, host_life](lib::PointerEvent& event) {
        if (host_life.expired()) return;
        const bool popup = (event.action == lib::PointerAction::Up && event.button == lib::PointerButton::Secondary) ||
                           event.action == lib::PointerAction::LongPress;
        if (!popup || !context_.services.gui) return;
        const auto gui = context_.services.gui; const auto menu = cast_popup_; const auto owner = casts_;
        gui->show_context_menu(menu, owner, event.position); event.handled = true;
    };
    cast_drop_target_ = root;
    if (context_.services.drag_drop) {
        lib::DropTargetBinding drop;
        drop.target = cast_drop_target_;
        drop.accepted_formats = {std::string(xmlbytestransfer::XML_BYTES_FORMAT), std::string(xmlbytestransfer::XML_BYTES_MIME), "text/xml"};
        drop.on_enter = drop.on_over = [host_life](lib::DragEvent& event) {
            event.selected = host_life.expired() ? lib::DropOperation::None : lib::DropOperation::Link;
        };
        drop.on_drop = [this, host_life](lib::DragEvent& event) {
            event.selected = lib::DropOperation::None;
            if (host_life.expired() || !summ_) return;
            auto xml = xmlbytestransfer::CBtoXML(event.data);
            if (xml.empty()) xml = event.data.text;
            EnemyDrop chooser; chooser.summary_id = summ_->id();
            chooser.accepted = [this, host_life](std::uint64_t id) {
                if (host_life.expired() || !summ_ || !summ_->cwCast(id)) return false;
                if (selected_id_ == id) return false;
                // D との意図的な差。D は検索で隠れたキャストをドロップするとアサーションで停止する
                // （spcarddialog.d:366-370）。C++ ではこの停止を再現しない。
                if (std::find(cast_ids_.begin(), cast_ids_.end(), id) == cast_ids_.end()) {
                    cast_filter_.clear();
                    if (cast_inc_search_) cast_inc_search_->setSearchText("");
                    if (host_life.expired()) return false;
                    refreshCasts();
                    if (host_life.expired()) return false;
                }
                const auto found = std::find(cast_ids_.begin(), cast_ids_.end(), id);
                if (found == cast_ids_.end()) return false;
                const auto combo = casts_;
                return lib::select_combo_item(*combo, static_cast<std::size_t>(found - cast_ids_.begin()), true);
            };
            if (chooser.drop(xml)) { event.selected = lib::DropOperation::Link; event.handled = true; }
        };
        context_.services.drag_drop->bind_target(std::move(drop));
    }

    auto image_group = makeGroup(context_.services, root,
                                 prop_ ? messageValue(prop_->requiredMsgs().image) : "Image");
    image_ = makeControl<lib::Canvas>(context_.services, lib::ComponentKind::Canvas);
    const auto preview_size = prop_ ? prop_->looks().cardSize()
                                    : structs::CSize{64U, 96U};
    image_->set_layout_item({
        .horizontal = lib::Align::Center,
        .vertical = lib::Align::Center,
        .preferred = {static_cast<float>(prop_ ? prop_->s(
                                                  static_cast<std::int32_t>(preview_size.width))
                                                : preview_size.width),
                      static_cast<float>(prop_ ? prop_->s(
                                                  static_cast<std::int32_t>(preview_size.height))
                                                : preview_size.height)}});
    image_->on_paint_context = [this](lib::DrawingContext& context,
                                       const lib::Rect bounds) {
        paintPreview(context, bounds);
    };
    image_group->add(image_);

    enemy_left_ = root;
    auto visual_group = makeGroup(
        context_.services, root,
        prop_ ? messageValue(prop_->requiredMsgs().overrideEnemyCardVisual)
              : "Override appearance");
    if (lib::editor_window_host_options().all_mode) {
        // ALL仕様: 外観の上書きの内部パディングを詰めて縦スペースを確保する。
        if (const auto layout = std::dynamic_pointer_cast<lib::LinearLayout>(
                visual_group->layout())) {
            layout->padding = {4.0F, 1.0F, 4.0F, 2.0F};
            layout->spacing = 2.0F;
        }
    }
    override_name_enabled_ = makeControl<lib::CheckBox>(
        context_.services, lib::ComponentKind::CheckBox);
    override_name_enabled_->text = prop_ ? messageValue(prop_->requiredMsgs().name) : "Name";
    override_name_enabled_->on_changed = [this](bool) {
        updateEnabled();
        updateToolTip();
        redrawPreview();
        refreshWarning();
    };
    mod(*override_name_enabled_);
    // ALL では、名前とイメージのチェックボックスを、名前欄の上の同じ行に置く。
    const bool all_visual_row = lib::editor_window_host_options().all_mode;
    std::shared_ptr<lib::Panel> visual_checks;
    if (all_visual_row) {
        visual_checks = makePanel(context_.services);
        auto checks_layout = std::make_shared<lib::LinearLayout>(
            lib::Orientation::Horizontal);
        checks_layout->spacing = 12.0F;
        checks_layout->padding = {};
        checks_layout->cross_axis = lib::Align::Center;
        visual_checks->set_layout(checks_layout);
        visual_checks->set_layout_item({.horizontal = lib::Align::Stretch});
        visual_checks->add(override_name_enabled_);
        visual_group->add(visual_checks);
    } else {
        visual_group->add(override_name_enabled_);
    }
    override_name_ = makeControl<lib::TextEdit>(context_.services,
                                                 lib::ComponentKind::TextEdit);
    override_name_->on_changed = [this](std::string_view) {
        updateToolTip();
        redrawPreview();
        refreshWarning();
    };
    text_menus_.push_back(setupSpecialTextMenu(override_name_, [] { return true; }));
    mod(*override_name_);
    visual_group->add(override_name_);
    override_image_enabled_ = makeControl<lib::CheckBox>(
        context_.services, lib::ComponentKind::CheckBox);
    override_image_enabled_->text = prop_ ? messageValue(prop_->requiredMsgs().image) : "Image";
    override_image_enabled_->on_changed = [this](bool) {
        updateEnabled();
        redrawPreview();
        refreshWarning();
    };
    mod(*override_image_enabled_);
    if (visual_checks) visual_checks->add(override_image_enabled_);
    else visual_group->add(override_image_enabled_);
    image_selector_ = std::make_shared<CardImageSelector>(
        context_, *prop_, summ_, visual_group, types::CardImagePosition::Center,
        false, true, prop_->looks().castCardInsets(), std::function<lib::String()>{}, force_skin_);
    image_selector_->imageListOwner(dialog());
    image_selector_->modEvent.push_back([this] {
        if (!ignoreMod()) applyEnabled();
        refreshWarning();
        redrawPreview();
        notifyToolbar();
    });
    if (lib::editor_window_host_options().all_mode)
        image_selector_->compactForAll();

    refreshCasts();
}

template <typename C>
void SpCardDialog<C>::createSideControls(
    const std::shared_ptr<lib::Container>& side) {
    if constexpr (std::is_same_v<C, EnemyCard>) {
        actions_panel_ = makeGroup(context_.services, side,
                                   prop_ ? messageValue(prop_->requiredMsgs().hasActions)
                                         : "Actions");
        actions_panel_->accessibility_name = "enemy-card-actions";
        if (lib::editor_window_host_options().all_mode) {
            auto layout = std::make_shared<lib::GridLayout>();
            // ALL右列の幅に収まる3列で、選択肢の文字を重ねず表示する。
            layout->columns = 3U;
            layout->equal_width = false;
            layout->spacing = 2.0F;
            layout->padding = {7.0F, 3.0F, 7.0F, 3.0F};
            actions_panel_->set_layout(layout);
        }
        createActionControls();
    }
    const bool compact_all = lib::editor_window_host_options().all_mode;
    const auto stored_flags_height = prop_
        ? std::max(0, prop_->var().flagsHeight.value) : 180;
    const float flag_reference_minimum = compact_all
        ? (std::is_same_v<C, EnemyCard> ? 130.0F : 150.0F) : 300.0F;
    const float flag_reference_height = compact_all
        ? static_cast<float>(std::min(stored_flags_height, 140))
        : static_cast<float>(stored_flags_height);
    auto flag_group = makeGroup(context_.services, side,
                                prop_ ? messageValue(prop_->requiredMsgs().refFlag)
                                      : "Flag");
    if constexpr (std::is_same_v<C, EnemyCard>)
        flag_group->accessibility_name = "enemy-card-flag-reference";
    flag_group->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Stretch,
                                 .minimum = {0.0F, flag_reference_minimum},
                                 .grow = 1.0F});
    flag_ = makeControl<lib::ComboBox>(context_.services,
                                       lib::ComponentKind::ComboBox);
    flag_->editable = true;
    for (const auto& value : context_.flags)
        flag_->items.push_back({value, value, 0U, true});
    flag_->on_selection_changed = [this](std::optional<std::size_t>) {
        refreshWarning();
    };
    mod(*flag_);
    const auto summary_owner =
        summ_ ? const_cast<Summary*>(summ_)->weak_from_this().lock()
              : Summary::Owner{};
    if (summary_owner) {
        chooser::Commons commons;
        commons.services = context_.services;
        commons.refreshToolBar = context_.refresh_toolbar;
        chooser::ChooserProperties properties;
        properties.editor_commons = context_.editor_commons;
        properties.logicalSort = prop_->var().logicalSort.value;
        properties.selectVariableWithTree =
            prop_->var().selectVariableWithTree.value;
        properties.expandChooserItems = prop_->var().expandChooserItems.value;
        properties.flagsWidth = prop_->var().flagsWidth.value;
        properties.flagsHeight = static_cast<std::int32_t>(flag_reference_height);
        properties.messages.allExpanded = prop_->requiredMsgs().allExpanded.value;
        properties.messages.noFlagRef = prop_->requiredMsgs().noFlagRef.value;
        properties.messages.flagDirRoot = prop_->requiredMsgs().flagDirRoot.value;
        properties.images.flag =
            prop_->images().load(dprops::StringView{"flag.png"});
        properties.images.localFlag =
            prop_->images().load(dprops::StringView{"flag_l.png"});
        properties.images.flagDir =
            prop_->images().load(dprops::StringView{"flagdir.png"});
        properties.images.emptyIcon =
            prop_->images().load(dprops::StringView{"empty.png"});
        flag_chooser_ =
            std::make_unique<chooser::FlagChooser<flag::Flag, true>>(
                std::move(commons), summary_owner, counter_, flag_group.get(), true,
                std::move(properties));
        if (const auto widget = flag_chooser_->widget()) {
            float flag_height = flag_reference_height;
            if (compact_all) {
                // ALL仕様(メニュー/エネミーカード): フラグ参照先を1行縮める。
                const float unit = std::max(0.01F, lib::scaled_ui_metric(1.0F));
                const float row = lib::item_row_height(*widget, 0.0F) / unit;
                // エネミーカードはさらに1行(計2行)縮める。
                const float rows = std::is_same_v<C, EnemyCard> ? 2.0F : 1.0F;
                const float removed = (row > 1.0F ? row : 20.0F) * rows;
                flag_height = std::max(0.0F, flag_height - removed - 10.0F);
                auto group_item = flag_group->layout_item();
                group_item.minimum.height =
                    std::max(0.0F, group_item.minimum.height - removed);
                flag_group->set_layout_item(group_item);
            }
            // ALL仕様(メニューカード): フラグ参照先を50px広げる。
            const float flag_width = static_cast<float>(prop_->var().flagsWidth.value) +
                (std::is_same_v<C, MenuCard> && compact_all ? 50.0F : 0.0F);
            widget->set_layout_item({
                .horizontal = lib::Align::Stretch,
                .vertical = lib::Align::Stretch,
                .minimum = {flag_width, flag_height},
                .preferred = {flag_width, flag_height},
                .grow = 1.0F});
        }
        flag_chooser_->modEvent.push_back([this] {
            setComboValue(*flag_, flag_chooser_->selectedWithDir());
            refreshWarning();
        });
    } else {
        flag_group->add(flag_);
    }
}

template <typename C>
void SpCardDialog<C>::createCommonControls(
    const std::shared_ptr<lib::Container>& root,
    const std::shared_ptr<lib::Container>& side) {
    // ALL の敵カードでは、カード位置のグループを右列のカードグルー
    // プ・アニメーションの直上に置く。 位置欄はX/Yと拡大率/レイ
    // ヤーの二行とし、OK・キャンセル・適用は通常のフッターに残す。
    const bool enemy_all = std::is_same_v<C, EnemyCard> &&
        lib::editor_window_host_options().all_mode;
    const auto position_parent = enemy_all && side ? side : root;
    auto position = makeGroup(context_.services, position_parent,
                              prop_ ? messageValue(prop_->requiredMsgs().cardPosition)
                                    : "Card position");
    if (enemy_all) {
        if (const auto layout = std::dynamic_pointer_cast<lib::LinearLayout>(
                position->layout())) {
            layout->padding = {4.0F, 1.0F, 4.0F, 2.0F};
            layout->spacing = 2.0F;
        }
        // 縦方向の配置に既定値以外を指定し、自動配置されるグ
        // リッドの子にする。 すべて既定値だと明示的な (0,0)
        // のセルとなり、列の最初のグループと重なる。
        position->set_layout_item({.horizontal = lib::Align::Stretch,
                                   .vertical = lib::Align::Start});
    } else {
        position->set_layout_item({.horizontal = lib::Align::Stretch,
                                   .minimum = {0.0F, 56.0F},
                                   .preferred = {0.0F, 56.0F}});
    }
    auto grid = makePanel(context_.services);
    auto grid_layout = std::make_shared<lib::GridLayout>();
    // 各項目をラベル・数値欄・案内の三セルとして一つのグリッ
    // ドへ直接置き、ALL の二行で列を共有する。 Xと拡大
    // 率、Yとレイヤーの数値欄を揃え、ラベル・案内を隣に置
    // く。 D版は各数値欄の行を Composite で囲
    // むが、項目ごとのパネルでは幅がばらつくため、 同じ位
    // 置に揃える意図的な違いとして、一つのグリッドを使う。
    constexpr std::size_t kCellsPerField = 3U;
    const std::size_t fields_per_row = enemy_all ? 2U : 4U;
    grid_layout->columns = fields_per_row * kCellsPerField;
    grid_layout->spacing = 4.0F;
    grid->set_layout(grid_layout);
    // D版と同様、この四列のグリッドはグル
    // ープ全幅に伸ばさず、中央配置にする。
    grid->set_layout_item({.horizontal = lib::Align::Center,
                           .vertical = lib::Align::Center});
    position->add(grid);

    std::size_t field_index = 0U;
    auto makeNumber = [this, &grid, &field_index, fields_per_row](
                          std::string label, double min, double max,
                          double value, std::string hint = {}) {
        // 一行の二つの項目の間には、広めの間隔を設ける。
        const bool row_start = field_index % fields_per_row == 0U;
        ++field_index;
        auto caption = makeLabel(context_.services, grid, std::move(label));
        caption->set_layout_item({.horizontal = lib::Align::End,
                                  .vertical = lib::Align::Center,
                                  // ラベルの文字とスピナーの枠の間に
                                  // も、項目間と同様に間隔を空ける。
                                  .margin = {0.0F, 8.0F, 0.0F, row_start ? 0.0F : 8.0F}});
        auto control = makeControl<lib::NumberEdit>(context_.services,
                                                     lib::ComponentKind::NumberEdit);
        control->minimum = min;
        control->maximum = max;
        control->value = value;
        // 値と増減ボタンが収まるだけの、ネイティブの編集欄の幅を確保する。
        control->set_layout_item({.horizontal = lib::Align::Start,
                                  .vertical = lib::Align::Center,
                                  .minimum = {54.0F, 24.0F},
                                  .preferred = {54.0F, 24.0F},
                                  });
        grid->add(control);
        // 案内が空でもセルを残し、二行で列を共有する。
        auto note = makeLabel(context_.services, grid, std::move(hint));
        note->set_layout_item({.horizontal = lib::Align::Start,
                               .vertical = lib::Align::Center});
        return control;
    };
    const auto pos_max = prop_ ? static_cast<double>(prop_->var().posLeftMax.value)
                               : 9999.0;
    const auto top_max = prop_ ? static_cast<double>(prop_->var().posTopMax.value)
                               : 9999.0;
    const auto scale_min = prop_ ? static_cast<double>(prop_->var().cardScaleMin.value)
                                 : 1.0;
    const auto scale_max = prop_ ? static_cast<double>(prop_->var().cardScaleMax.value)
                                 : 300.0;
    const auto layer_max = prop_ ? static_cast<double>(prop_->var().layerMax.value)
                                 : 9999.0;
    x_ = makeNumber(prop_ ? messageValue(prop_->requiredMsgs().left) : "Left",
                    -pos_max, pos_max, 0.0);
    y_ = makeNumber(prop_ ? messageValue(prop_->requiredMsgs().top) : "Top",
                    -top_max, top_max, 0.0);
    scale_ = makeNumber(prop_ ? messageValue(prop_->requiredMsgs().scale) : "Scale",
                         scale_min, scale_max, 100.0,
                         prop_ ? messageValue(prop_->requiredMsgs().scalePer) : "%");
    layer_ = makeNumber(prop_ ? messageValue(prop_->requiredMsgs().layer) : "Layer",
                         static_cast<double>(types::LAYER_BACK_CELL), layer_max,
                         static_cast<double>(types::LAYER_MENU_CARD),
                         prop_ ? formatMessage(messageValue(prop_->requiredMsgs().layerHint),
                                               std::to_string(types::LAYER_MENU_CARD))
                               : std::string{});
    x_->on_changed = [this](double) { refreshWarning(); };
    y_->on_changed = [this](double) { refreshWarning(); };
    scale_->on_changed = [this](double) { refreshWarning(); };
    layer_->on_changed = [this](double) { updateEnabled(); };
    mod(*x_);
    mod(*y_);
    mod(*scale_);
    mod(*layer_);

    const bool compact_menu_card = std::is_same_v<C, MenuCard> &&
        lib::editor_window_host_options().all_mode;
    const bool compact_settings_column = compact_menu_card || enemy_all;
    std::shared_ptr<lib::Panel> compact_details;
    if constexpr (std::is_same_v<C, MenuCard>) {
        auto desc_group = makeGroup(
            context_.services, compact_menu_card
                ? std::shared_ptr<lib::Container>{} : root,
            prop_ ? messageValue(prop_->requiredMsgs().desc) : "Description");
        desc_group->set_layout_item({.horizontal = lib::Align::Stretch,
                                     .vertical = lib::Align::Start});
        desc_ = makeControl<lib::TextEdit>(context_.services,
                                           lib::ComponentKind::TextEdit);
        desc_->multiline = true;
        desc_->wrap = true;
        desc_->vertical_scroll = false; // D の FixedWidthText と同様、縦スクロールなし。
        refreshDescriptionLayout();
        desc_->on_changed = [this](std::string_view) { refreshWarning(); };
        mod(*desc_);
        desc_group->add(desc_);
        if (compact_menu_card) {
            // 高さの低い ALL エディタでは、説明に一行全体を使わ
            // ず、二つのコンパクトなカード設定の隣に置く。 右列は
            // 下で、カードグループ、アニメーションの順に構成する。 Card group above Animation
            compact_details = makePanel(context_.services);
            auto details_layout = std::make_shared<lib::LinearLayout>(
                lib::Orientation::Horizontal);
            details_layout->spacing = 0.0F;
            details_layout->padding = {};
            compact_details->set_layout(details_layout);
            compact_details->set_layout_item({
                .horizontal = lib::Align::Stretch,
                .vertical = lib::Align::Stretch,
                .minimum = {0.0F, 80.0F},
                .preferred = {0.0F, 80.0F}, .grow = 1.0F});
            auto desc_item = desc_group->layout_item();
            desc_item.horizontal = lib::Align::Stretch;
            desc_item.vertical = lib::Align::Stretch;
            desc_item.minimum = {0.0F, 80.0F};
            desc_item.preferred = {0.0F, 80.0F};
            desc_item.grow = 1.0F;
            desc_group->set_layout_item(desc_item);
            compact_details->add(desc_group);
            root->add(compact_details);
            auto desc_control_item = desc_->layout_item();
            desc_control_item.horizontal = lib::Align::Stretch;
            desc_control_item.vertical = lib::Align::Stretch;
            desc_control_item.grow = 1.0F;
            desc_->set_layout_item(desc_control_item);
        }
    }

    auto bottom = makePanel(context_.services);
    if constexpr (std::is_same_v<C, EnemyCard>)
        bottom->accessibility_name = "enemy-card-group-animation";
    std::shared_ptr<lib::Layout> bottom_layout;
    if (compact_settings_column) {
        auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
        layout->spacing = 0.0F;
        layout->padding = {};
        bottom_layout = layout;
    } else {
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 2U;
        layout->spacing = 6.0F;
        bottom_layout = layout;
    }
    bottom->set_layout(bottom_layout);
    bottom->set_layout_item(compact_settings_column
        ? lib::LayoutItem{.horizontal = lib::Align::Stretch,
                          .vertical = lib::Align::Start,
                          .minimum = {196.0F, 0.0F},
                          .preferred = {290.0F, 0.0F}}
        : lib::LayoutItem{.horizontal = lib::Align::Stretch,
                          .minimum = {0.0F, 0.0F},
                          .preferred = {0.0F, 0.0F}});
    if constexpr (std::is_same_v<C, EnemyCard>) {
        if (lib::editor_window_host_options().all_mode)
            side->add(bottom);
        else
            root->add(bottom);
    } else {
        if (!compact_menu_card) root->add(bottom);
        else if (compact_details) compact_details->add(bottom);
    }

    auto group = makeGroup(context_.services, bottom,
                           prop_ ? messageValue(prop_->requiredMsgs().cardGroup)
                                 : "Card group");
    group->set_layout_item(compact_settings_column
        ? lib::LayoutItem{.horizontal = lib::Align::Stretch,
                          .vertical = lib::Align::Start}
        : lib::LayoutItem{.horizontal = lib::Align::Stretch,
                          .vertical = lib::Align::Stretch,
                          .minimum = {290.0F, 0.0F},
                          .preferred = {290.0F, 0.0F}, .grow = 1.0F});
    {
        // 元実装との差: 枠の内部上下余白を詰める処理をALL限定にせず、
        // standalone でも同じ共有実装を通す(コンボ下の不要な余白を出さない)。
        if (const auto layout =
                std::dynamic_pointer_cast<lib::LinearLayout>(group->layout())) {
            layout->padding.top = 0.0F;
            layout->padding.bottom = 1.0F;
            layout->spacing = 0.0F;
        }
    }
    card_group_ = makeControl<lib::ComboBox>(context_.services,
                                             lib::ComponentKind::ComboBox);
    card_group_->editable = true;
    for (const auto& value : context_.card_groups)
        card_group_->items.push_back({value, value, 0U, true});
    card_group_->on_selection_changed = [this](std::optional<std::size_t>) {
        updateEnabled();
    };
    card_group_->on_text_changed = [this](std::string_view) { updateEnabled(); };
    mod(*card_group_);
    group->add(card_group_);

    auto speed_group = makeGroup(context_.services, bottom,
                                  prop_ ? messageValue(prop_->requiredMsgs().cardSpeed)
                                        : "Animation");
    speed_group->set_layout_item(compact_settings_column
        ? lib::LayoutItem{.horizontal = lib::Align::Stretch,
                          .vertical = lib::Align::Start}
        : lib::LayoutItem{.horizontal = lib::Align::Stretch,
                          .vertical = lib::Align::Stretch,
                          .minimum = {196.0F, 0.0F},
                          .preferred = {196.0F, 0.0F}, .grow = 1.0F});
    {
        // 元実装との差: 枠の内部上下余白を詰める処理をALL限定にせず、
        // standalone でも同じ共有実装を通す(コンボ下の不要な余白を出さない)。
        if (const auto layout =
                std::dynamic_pointer_cast<lib::LinearLayout>(speed_group->layout())) {
            layout->padding.top = 0.0F;
            layout->padding.bottom = 1.0F;
            layout->spacing = 0.0F;
        }
    }
    animation_speed_ = std::make_shared<CardAnimationPanel>(
        context_.common, summ_, speed_group, CardAnimationPanelType::MenuCard);
    animation_speed_->modEvent.push_back([this] {
        applyEnabled();
        refreshWarning();
    });
}

template <typename C>
void SpCardDialog<C>::createActionControls() {
    if constexpr (std::is_same_v<C, EnemyCard>) {
        if (!actions_panel_) return;
        std::map<types::ActionCardType, bool> previous;
        for (const auto& [type, control] : actions_)
            previous[type] = control && control->checked;
        actions_panel_->clear();
        actions_.clear();
        std::vector<types::ActionCardType> types_to_show;
        if (summSkin()) {
            const auto values = summSkin()->actionCardTypes();
            types_to_show.assign(values.begin(), values.end());
        }
        if (types_to_show.empty()) types_to_show = defaultActionTypes();
        for (const auto type : types_to_show) {
            auto control = makeControl<lib::CheckBox>(context_.services,
                                                       lib::ComponentKind::CheckBox);
            if (prop_) {
                control->text = summSkin()
                                    ? summSkin()->actionCardName(prop_->sys(), type)
                                    : prop_->sys().actionCardName(type, {});
            } else {
                control->text = actionTypeName(type);
            }
            control->checked = previous.count(type) ? previous[type]
                                                     : type != types::ActionCardType::RunAway;
            control->on_changed = [this](bool) {
                updateEnabled();
                refreshWarning();
                redrawPreview();
            };
            mod(*control);
            actions_panel_->add(control);
            actions_[type] = std::move(control);
        }
    }
}

template <typename C>
void SpCardDialog<C>::setInitialValues() {
    ignoreMod(true);
    if (card_) {
        if constexpr (std::is_same_v<C, MenuCard>) {
            name_->text = card_->name();
            desc_->text = card_->desc();
            expand_sp_chars_->checked = card_->expandSPChars();
            image_selector_->images(card_->paths());
        } else {
            // 仮のIDや存在しないIDは、表示している先頭のキャストに解決する。
            selected_id_ = cast_ids_.empty() ? 0U : cast_ids_.front();
            if (casts_) {
                if (!cast_ids_.empty()) (void)lib::select_combo_item(*casts_, 0U, false);
                for (std::size_t index = 0; index < cast_ids_.size(); ++index) {
                    if (cast_ids_[index] == card_->id()) {
                        selected_id_ = card_->id();
                        (void)lib::select_combo_item(*casts_, index, false);
                        break;
                    }
                }
            }
            for (const auto& [type, control] : actions_)
                control->checked = card_->action(type);
            override_name_enabled_->checked = card_->isOverrideName();
            override_name_->text = card_->overrideName();
            override_image_enabled_->checked = card_->isOverrideImage();
            image_selector_->images(card_->overrideImages());
        }
        setFlagText(card_->flag());
        x_->value = card_->x();
        y_->value = card_->y();
        scale_->value = card_->scale();
        layer_->value = card_->layer();
        setCardGroupText(card_->cardGroup());
        animation_speed_->speed(card_->animationSpeed());
    } else {
        if constexpr (std::is_same_v<C, MenuCard>) {
            name_->text.clear();
            desc_->text.clear();
            expand_sp_chars_->checked = false;
            image_selector_->images({});
        } else {
            if (!cast_ids_.empty()) {
                selected_id_ = cast_ids_.front();
                if (casts_) (void)lib::select_combo_item(*casts_, 0U, false);
            }
            for (const auto& [type, control] : actions_)
                control->checked = type != types::ActionCardType::RunAway;
            override_name_enabled_->checked = false;
            override_name_->text.clear();
            override_image_enabled_->checked = false;
            image_selector_->images({});
        }
        setFlagText({});
        x_->value = 0.0;
        y_->value = 0.0;
        scale_->value = 100.0;
        layer_->value = static_cast<double>(types::LAYER_MENU_CARD);
        setCardGroupText({});
        animation_speed_->speed(-1);
    }
    ignoreMod(false);
}

template <typename C>
void SpCardDialog<C>::setCardGroupText(std::string_view value) {
    if (card_group_) {
        // D版と同様、現在ほかのカードで使っていなくても初期値を含める。
        if (!value.empty() && std::none_of(card_group_->items.begin(), card_group_->items.end(),
            [value](const auto& item) { return item.text == value; }))
            card_group_->items.push_back({std::string(value), std::string(value), 0U, true});
        setComboValue(*card_group_, value);
    }
}

template <typename C>
void SpCardDialog<C>::setFlagText(std::string_view value) {
    if (flag_) setComboValue(*flag_, value);
    if (flag_chooser_) flag_chooser_->selected(std::string(value));
}

template <typename C>
std::string SpCardDialog<C>::flagValue() const {
    if (flag_chooser_) return flag_chooser_->selected();
    return flag_ ? comboValue(*flag_) : std::string{};
}

template <typename C>
std::string SpCardDialog<C>::cardGroupValue() const {
    return card_group_ ? comboValue(*card_group_) : std::string{};
}

template <typename C>
CardImageArray SpCardDialog<C>::selectedImages(bool force) {
    if (!image_selector_) return {};
    static_cast<void>(image_selector_->materialPath(force));
    return image_selector_->images();
}

template <typename C>
std::vector<lib::String> SpCardDialog<C>::textWarnings(
    std::string_view value, std::string_view itemName) const {
    if (context_.text_warnings) return context_.text_warnings(value);
    // 通常の MainWindow ホストはテキスト用コールバックを渡さない。
    // ここでD版と同じ項目名付きのSJIS検査を行い、明示的な上書きも維持する。
    return prop_ ? ::cwxeditor::cwx::warning::sjisWarnings(
                       prop_->parent(), summ_, value, itemName)
                 : std::vector<lib::String>{};
}

template <typename C>
void SpCardDialog<C>::refreshWarning() {
    if (!setup_complete_ && !name_ && !casts_) return;
    warnings_.clear();
    const auto append = [this](const std::vector<lib::String>& values) {
        warnings_.insert(warnings_.end(), values.begin(), values.end());
    };
    const auto target = [this](std::string_view version) {
        return isTargetVersion(prop_, summ_, version);
    };
    if constexpr (std::is_same_v<C, MenuCard>) {
        if (name_) append(textWarnings(name_->text, prop_ ? messageValue(prop_->requiredMsgs().name) : ""));
        if (desc_) append(textWarnings(desc_->text, prop_ ? messageValue(prop_->requiredMsgs().desc) : ""));
        if (card_group_) append(textWarnings(cardGroupValue(), prop_ ? messageValue(prop_->requiredMsgs().cardGroup) : ""));
        if (expand_sp_chars_ && expand_sp_chars_->checked && !target("4") && prop_)
            warnings_.push_back(messageValue(prop_->requiredMsgs().warningExpandSPCharsInMenuCardName));
    } else {
        if (card_group_) append(textWarnings(cardGroupValue(), prop_ ? messageValue(prop_->requiredMsgs().cardGroup) : ""));
        if (override_name_enabled_ && override_name_enabled_->checked && !target("4") &&
            prop_)
            warnings_.push_back(messageValue(prop_->requiredMsgs().warningOverrideEnemyCardName));
        if (override_image_enabled_ && override_image_enabled_->checked && !target("4") &&
            prop_)
            warnings_.push_back(messageValue(prop_->requiredMsgs().warningOverrideEnemyCardImage));
    }
    // D版と同様、特殊文字・敵の行動の警告より先に、ImageSelect の診断を表示する。
    if (image_selector_) append(image_selector_->warnings());
    // D版と同様、展開が有効なメニューカード名だけを展開する。 ただし敵
    // カードの上書き文字列は、有効化のチェックが外れていても検査する。
    const lib::TextEdit* specialText = nullptr;
    if constexpr (std::is_same_v<C, MenuCard>) {
        if (expand_sp_chars_ && expand_sp_chars_->checked) specialText = name_.get();
    } else {
        specialText = override_name_.get();
    }
    if (!context_.text_warnings && specialText && prop_ && summSkin()) {
        std::vector<lib::String> flags, steps, variants, fonts;
        std::vector<char> colors;
        ::cwxeditor::cwx::msgutils::textUseItems(
            normalizedText(specialText->text), flags, steps, variants, fonts, colors);
        // カード名では、メッセージ用のフォント・色を意図的に検証しない。
        fonts.clear();
        colors.clear();
        lib::Dictionary<lib::String, bool> wf, ws, wv, wfont;
        lib::Dictionary<char, bool> wc;
        append(::cwxeditor::cwx::warning::textWarnings(
            prop_->parent(), *summSkin(), summ_, nullptr, summ_ && summ_->legacy(),
            summ_ ? summ_->dataVersion() : ::cwxeditor::cwx::versioninfo::LATEST_VERSION,
            prop_->var().targetVersion.value, specialText->text,
            flags, steps, variants, fonts, colors, wf, ws, wv, wfont, wc).all);
    }
    if constexpr (std::is_same_v<C, EnemyCard>) {
        bool no_action_warning = false;
        if (summ_ && summ_->legacy()) {
            const bool exchange = actions_.contains(types::ActionCardType::Exchange) &&
                                  actions_.at(types::ActionCardType::Exchange)->checked;
            if (exchange && hasExchangeItem() && prop_ && summSkin()) {
                warnings_.push_back(formatMessage(
                    messageValue(prop_->requiredMsgs().warningExchangeIsItemInClassic),
                    summSkin()->actionCardName(prop_->sys(),
                                               types::ActionCardType::Exchange)));
            } else if (!exchange && !hasExchangeItem() && prop_) {
                warnings_.push_back(messageValue(prop_->requiredMsgs().warningNoActionCard));
                no_action_warning = true;
            }
        }
        if (!no_action_warning && !target("4") && !allRequiredActionsSelected() && prop_)
            warnings_.push_back(messageValue(prop_->requiredMsgs().warningNoActionCard));
    }
    if (layer_ && layer_->enabled() && integerValue(layer_) != types::LAYER_MENU_CARD &&
        !target("1") && prop_)
        warnings_.push_back(messageValue(prop_->requiredMsgs().warningLayer));
    if (summ_ && !cardGroupValue().empty() && !target("3") && prop_)
        warnings_.push_back(messageValue(prop_->requiredMsgs().warningCardGroup));
    if (animation_speed_) append(animation_speed_->warnings());
    // 警告はこのダイアログの警告欄に表示する。 数値欄を調整し
    // ている間、所有するシーンビュー全体の更新を必要としない。
    warning(warnings_);
    notifyWarnings();
}

template <typename C>
void SpCardDialog<C>::updateEnabled() {
    if (!layer_ || !card_group_) return;
    const bool legacy = summ_ && summ_->legacy();
    layer_->set_enabled(!legacy || integerValue(layer_) != types::LAYER_MENU_CARD);
    card_group_->set_enabled(!legacy || !cardGroupValue().empty());
    // C++版の意図的な追加として、編集可能なネイティブコンボに空状態の案内を表示
    // できる。 D版のSWT Comboは、対応する有効状態の制限だけを適用する。
    card_group_->empty_hint = card_group_->enabled()
        ? "入力または候補を選択" : "旧形式では設定できません";
    card_group_->set_tooltip(card_group_->enabled()
        ? "カードグループ名を直接入力できます。右の▼で既存の名前を選択します。"
        : "D仕様: 旧形式では空のカードグループに新しい値を設定できません。");
    if constexpr (std::is_same_v<C, MenuCard>) {
        if (expand_sp_chars_)
            expand_sp_chars_->set_enabled(!legacy || expand_sp_chars_->checked);
    } else {
        if (override_name_enabled_) {
            override_name_enabled_->set_enabled(!legacy || override_name_enabled_->checked);
            override_name_->set_enabled(override_name_enabled_->checked);
        }
        if (override_image_enabled_) {
            override_image_enabled_->set_enabled(!legacy || override_image_enabled_->checked);
            if (image_selector_) image_selector_->enabled(override_image_enabled_->checked);
        }
        const bool exchange_item = hasExchangeItem();
        for (const auto& [type, control] : actions_) {
            if (!control) continue;
            if (type == types::ActionCardType::RunAway) {
                control->set_enabled(true);
            } else if (legacy && type == types::ActionCardType::Exchange) {
                control->set_enabled(control->checked == exchange_item);
            } else {
                control->set_enabled(!legacy || !control->checked);
            }
        }
    }
    if (animation_speed_) animation_speed_->updateEnabled();
    updateToolTip();
    refreshWarning();
    notifyToolbar();
}

template <typename C>
void SpCardDialog<C>::refreshDescriptionLayout() {
    if constexpr (!std::is_same_v<C, MenuCard>) return;
    if (!desc_ || !prop_) return;

    auto font = dutils::createFontFromPixels(prop_->adjustFont(
        prop_->looks().cardDescFont(summSkin()
                                         ? summSkin()->legacy()
                                         : (summ_ && summ_->legacy()))));
    // D版の FixedWidthText と同様、createFontFromPixels(CFont)
    // の正のサイズをセルの画素寸法として扱う。 共通レンダラ
    // ーが、選んだ書体の対応するセル高さを解決する。 これはダ
    // イアログの操作文字ではなくカード本文であり、 編集領域
    // の基準となる実測行高に、UIフォントの拡大を重ねない。
    font.content_font = true;
    desc_->set_font(std::move(font));
    desc_->minimum_visible_columns = static_cast<std::size_t>(
        std::max(1, prop_->looks().cardDescLen()));
    const int desc_lines = prop_->looks().cardDescLine(summ_ && summ_->legacy());
    // ALL のメニューカードは、以前のコンパクトな配置より一行減らす。
    desc_->minimum_visible_lines = static_cast<std::size_t>(std::max(1,
        lib::editor_window_host_options().all_mode ? desc_lines - 3 : desc_lines));
    desc_->set_layout_item({.horizontal = lib::Align::Center,
                            .vertical = lib::Align::Start});
}

template <typename C>
void SpCardDialog<C>::refDataVersion() {
    refreshDescriptionLayout();
    updateEnabled();
}

template <typename C>
void SpCardDialog<C>::refSkin() {
    if (notification_commons_ && !force_skin_ && !summary_skin_pinned_) {
        context_.common.skin = notification_commons_->skin().get();
        summ_skin_ = context_.common.skin;
    }
    if constexpr (std::is_same_v<C, EnemyCard>) {
        if (setup_complete_ && actions_panel_) createActionControls();
    }
    refreshDescriptionLayout();
    updateToolTip();
    redrawPreview();
    refreshWarning();
}

template <typename C>
void SpCardDialog<C>::refImageScale() {
    if (image_selector_ && prop_) {
        const auto size = prop_->looks().cardSize();
        const auto insets = std::is_same_v<C, MenuCard>
            ? prop_->looks().menuCardInsets()
            : prop_->looks().castCardInsets();
        image_selector_->previewSize(
            prop_->s(static_cast<std::int32_t>(size.width)),
            prop_->s(static_cast<std::int32_t>(size.height)), insets);
    }
    redrawPreview();
}

template <typename C>
void SpCardDialog<C>::refCast(const CastCard* cast) {
    if constexpr (std::is_same_v<C, EnemyCard>) {
        // D版と同様、未選択カードの改名や現在の候補の削除も
        // 含め、 キャストの変更ごとに候補一覧を組み直す。
        const bool was_selected = cast && cast->id() == selected_id_;
        const std::weak_ptr<int> host_life = cast_host_lifetime_;
        refreshCasts();
        // D版と同様、行動のグループも refCast を監視する。 手札の削除で、選
        // 択キャストが変わらなくても Exchange の利用可否が変わる場合がある。
        if (!host_life.expired() && was_selected) updateEnabled();
    } else {
        static_cast<void>(cast);
    }
}

template <typename C>
void SpCardDialog<C>::delMenuCard(std::string_view cwx_path) {
    if (card_ && card_->cwxPath(true) == cwx_path) forceCancel();
}

template <typename C>
dmenu::TextMenuModifyPtr SpCardDialog<C>::setupSpecialTextMenu(
    const std::shared_ptr<lib::TextEdit>& editor, std::function<bool()> enabled) {
    if (!editor || !prop_) return {};
    dmenu::Commons comm;
    comm.services = context_.services;
    comm.undoMaxEtc = static_cast<std::size_t>(std::max<std::int32_t>(
        0, prop_->var().undoMaxEtc.value));
    comm.presentation.textTabs = static_cast<std::size_t>(std::max<std::int32_t>(
        1, prop_->var().textTabs.value));
    comm.presentation.menuText = [prop = prop_](auto id) { return prop->buildMenu(id); };
    comm.presentation.menuImage = [prop = prop_](auto id) { return prop->images().menu(id); };
    auto binding = dmenu::createTextMenu(comm, dmenu::TMM(editor));
    messageutils::MessageContext message;
    message.services = context_.services;
    message.props = prop_;
    message.summary = summ_;
    message.editor_commons = context_.editor_commons;
    // D版の null の UseCounter とは異なり、特殊文字
    // サブメニューで 編集中のカードのローカル変数を使えるようにする。
    message.use_counter = counter_.get();
    message.use_counter_owner = counter_;
    const auto skin = retainSkin(summSkin());
    (void)messageutils::setupSPCharsMenu(
        message, summ_, [skin] { return skin; }, counter_.get(),
        binding->menu(), false, true, enabled,
        [weak = std::weak_ptr<dmenu::TextMenuModify>(binding)](lib::StringView value) {
            if (const auto owner = weak.lock()) {
                auto text = owner->text();
                auto next = text.getText();
                const auto selection = text.getSelection();
                const auto start = std::min(next.size(), static_cast<std::size_t>(
                    std::max(0.0F, selection.x)));
                const auto end = std::clamp(static_cast<std::size_t>(
                    std::max(0.0F, selection.y)), start, next.size());
                next.replace(start, end - start, value);
                text.setText(next);
                const auto caret = static_cast<float>(start + value.size());
                text.setSelection({caret, caret});
            }
        });
    const auto changed = editor->on_changed;
    editor->on_changed = [changed, weak = std::weak_ptr<dmenu::TextMenuModify>(binding)](
        std::string_view value) {
        if (changed) changed(value);
        if (const auto owner = weak.lock()) owner->modifyText();
    };
    return binding;
}

template <typename C>
void SpCardDialog<C>::updateToolTip() {
    const auto preview = [this](std::string value) {
        // SWT のネイティブのツールチップは & を二重にする必要がある
        // が、共通のC++描画は文字列をそのまま描く。 そのエスケープをす
        // ると余分な & が見えるため、D版の表示文字列をそのまま保つ。
        return context_.format_text
            ? context_.format_text(value)
            : messageutils::createSPCharPreview(
                  *prop_, summ_, retainSkin(summSkin()), nullptr,
                  std::move(value), false);
    };
    if constexpr (std::is_same_v<C, MenuCard>) {
        if (!name_) return;
        std::string tooltip;
        if (expand_sp_chars_ && expand_sp_chars_->checked)
            tooltip = preview(name_->text);
        name_->set_tooltip(std::move(tooltip));
    } else {
        if (!override_name_) return;
        override_name_->set_tooltip(preview(override_name_->text));
    }
}

template <typename C>
void SpCardDialog<C>::notifyToolbar() const {
    if (context_.refresh_toolbar) context_.refresh_toolbar();
}

template <typename C>
void SpCardDialog<C>::notifyCardGroups() const {
    if (context_.refresh_card_groups) context_.refresh_card_groups();
}

template <typename C>
void SpCardDialog<C>::notifyWarnings() const {
    if (context_.refresh_warnings) context_.refresh_warnings();
}

template <typename C>
bool SpCardDialog<C>::hasExchangeItem() const {
    if constexpr (!std::is_same_v<C, EnemyCard>) {
        return false;
    } else {
        const auto cast = selectedCast();
        if (!cast || cast->items().empty() || !summ_ || !summSkin() || !prop_)
            return false;
        const auto item = cast->items().front();
        const auto base = summ_->baseCard(item);
        return base && base->name() ==
            summSkin()->actionCardName(prop_->sys(), types::ActionCardType::Exchange);
    }
}

template <typename C>
bool SpCardDialog<C>::allRequiredActionsSelected() const {
    if constexpr (!std::is_same_v<C, EnemyCard>) {
        return true;
    } else {
        for (const auto& [type, control] : actions_) {
            if (type == types::ActionCardType::RunAway ||
                (type == types::ActionCardType::Exchange && summ_ && summ_->legacy())) continue;
            if (control && !control->checked) return false;
        }
        return true;
    }
}

template <typename C>
std::vector<ConstCastCardPtr> SpCardDialog<C>::castList() const {
    if constexpr (std::is_same_v<C, EnemyCard>) {
        const auto provider = context_.casts;
        const auto summary = summ_;
        if (provider) return provider();
        if (summary) {
            const auto source = summary->casts();
            std::vector<ConstCastCardPtr> values;
            values.reserve(source.size());
            for (const auto& cast : source) values.push_back(cast);
            return values;
        }
    }
    return {};
}

template <typename C>
ConstCastCardPtr SpCardDialog<C>::selectedCast() const {
    if constexpr (std::is_same_v<C, EnemyCard>) {
        if (!selected_id_) return {};
        if (summ_) return summ_->cwCast(selected_id_);
        for (const auto& cast : castList())
            if (cast && cast->id() == selected_id_) return cast;
    }
    return {};
}

template <typename C>
SpCardDialog<C>::~SpCardDialog() {
    cast_host_lifetime_.reset();
    if (context_.services.drag_drop && cast_drop_target_) context_.services.drag_drop->unbind(cast_drop_target_);
    cast_inc_search_.reset();
    if (cast_popup_) { cast_popup_->on_open = {}; cast_popup_->on_command = {}; }
    if (casts_) { casts_->on_pointer = {}; casts_->on_key_down = {}; casts_->on_text_input = {}; }
    unbindExternalNotifications();
    releasePreviewResource();
}

template <typename C>
void SpCardDialog<C>::bindExternalNotifications() {
    if (notification_commons_ || !context_.editor_commons) return;
    notification_commons_ = context_.editor_commons;
    del_menu_card_connection_ = notification_commons_->delMenuCard.add(
        [this](const lib::String& value) { delMenuCard(value); });
    ref_skin_connection_ = notification_commons_->refSkin.add([this] { refSkin(); });
    ref_data_version_connection_ = notification_commons_->refDataVersion.add(
        [this] { refDataVersion(); });
    ref_target_version_connection_ = notification_commons_->refTargetVersion.add(
        [this] { refDataVersion(); });
    ref_preview_values_connection_ = notification_commons_->refPreviewValues.add(
        [this] { updateToolTip(); });
    ref_image_scale_connection_ = notification_commons_->refImageScale.add(
        [this] { refImageScale(); });
    if constexpr (std::is_same_v<C, EnemyCard>) {
        ref_cast_connection_ = notification_commons_->refCast.add(
            [this](const CastCardPtr& value) { refCast(value.get()); });
        del_cast_connection_ = notification_commons_->delCast.add(
            [this](const CastCardPtr&) { refreshCasts(); });
        const std::weak_ptr<int> host_life = cast_host_lifetime_;
        ref_item_connection_ = notification_commons_->refItem.add(
            [this, host_life](const auto&) {
                if (!host_life.expired()) updateEnabled();
            });
    }
}

template <typename C>
void SpCardDialog<C>::unbindExternalNotifications() noexcept {
    if (!notification_commons_) return;
    notification_commons_->delMenuCard.remove(del_menu_card_connection_);
    notification_commons_->refSkin.remove(ref_skin_connection_);
    notification_commons_->refDataVersion.remove(ref_data_version_connection_);
    notification_commons_->refTargetVersion.remove(ref_target_version_connection_);
    notification_commons_->refPreviewValues.remove(ref_preview_values_connection_);
    notification_commons_->refImageScale.remove(ref_image_scale_connection_);
    if constexpr (std::is_same_v<C, EnemyCard>) {
        notification_commons_->refCast.remove(ref_cast_connection_);
        notification_commons_->delCast.remove(del_cast_connection_);
        notification_commons_->refItem.remove(ref_item_connection_);
    }
    notification_commons_ = nullptr;
    del_menu_card_connection_ = 0;
    ref_cast_connection_ = 0;
    del_cast_connection_ = 0;
    ref_item_connection_ = 0;
    ref_skin_connection_ = 0;
    ref_data_version_connection_ = 0;
    ref_target_version_connection_ = 0;
    ref_preview_values_connection_ = 0;
    ref_image_scale_connection_ = 0;
}

template <typename C>
void SpCardDialog<C>::redrawPreview() {
    if constexpr (std::is_same_v<C, EnemyCard>) {
        if (!image_) return;
        lib::ImageData next_image;
        const auto cast = selectedCast();
        if (cast && prop_) {
            auto images = image_selector_ ? image_selector_->images()
                                          : CardImageArray{};
            const auto override_name = override_name_enabled_ &&
                                               override_name_enabled_->checked &&
                                               override_name_
                                           ? override_name_->text
                                           : lib::String{};
            auto rendered = dutils::castCardImage(
                prop_, summSkin(), summ_, cast.get(), true,
                override_name_enabled_ && override_name_enabled_->checked,
                override_name,
                override_image_enabled_ && override_image_enabled_->checked,
                images, context_.services);
            next_image = rendered.scaledPercent(prop_->drawingScalePercent());
        }
        const auto resource = preview_image_resource_.publish(
            context_.services.graphics, next_image);
        if (next_image.frames.empty()) preview_image_data_ = {};
        else if (resource != 0U &&
                 preview_image_resource_.graphics() == context_.services.graphics)
            preview_image_data_ = std::move(next_image);
        if (image_->invalidate) image_->invalidate();
        else if (context_.services.gui)
            context_.services.gui->request_redraw(image_);
    }
}

template <typename C>
void SpCardDialog<C>::paintPreview(lib::DrawingContext& context,
                                   const lib::Rect bounds) const {
    if constexpr (std::is_same_v<C, EnemyCard>) {
        if (preview_image_resource_.id() == 0U || preview_image_data_.frames.empty())
            return;
        const auto& frame = preview_image_data_.frames.front();
        const float source_width = static_cast<float>(frame.pixel_width);
        const float source_height = static_cast<float>(frame.pixel_height);
        if (source_width <= 0.0F || source_height <= 0.0F) return;
        const float ratio = std::min(bounds.width / source_width,
                                     bounds.height / source_height);
        const float width = source_width * ratio;
        const float height = source_height * ratio;
        const lib::Rect destination{
            bounds.x + (bounds.width - width) / 2.0F,
            bounds.y + (bounds.height - height) / 2.0F,
            width, height};
        context.draw_image(preview_image_resource_.id(),
                           {0.0F, 0.0F, source_width, source_height},
                           destination, lib::Interpolation::Best);
    }
}

template <typename C>
void SpCardDialog<C>::releasePreviewResource() noexcept {
    if constexpr (std::is_same_v<C, EnemyCard>) {
        preview_image_resource_.reset();
        preview_image_data_ = {};
    }
}

template <typename C>
void SpCardDialog<C>::refreshCasts() {
    if constexpr (std::is_same_v<C, EnemyCard>) {
        if (!casts_) return;
        const std::weak_ptr<int> host_life = cast_host_lifetime_;
        const auto list = castList();
        if (host_life.expired()) return;
        if (summ_ && list.empty()) { forceCancel(); return; }
        std::vector<lib::ChoiceItem> items;
        decltype(cast_ids_) cast_ids;
        std::optional<std::size_t> selected_index;
        auto selected_id = selected_id_;
        bool selected = false;
        for (const auto& cast : list) {
            if (!cast) continue;
            if (cast->id() == selected_id_) selected = true;
            if (cast_inc_search_ ? !cast_inc_search_->match(cast->name()) :
                (!cast_filter_.empty() && cast->name().find(cast_filter_) == std::string::npos)) continue;
            items.push_back({std::to_string(cast->id()) + "." + cast->name(),
                             std::to_string(cast->id()), 0U, true});
            cast_ids.push_back(cast->id());
            if (cast->id() == selected_id_) {
                selected_index = cast_ids.size() - 1U;
            }
        }
        if (!selected && !cast_ids.empty()) {
            selected_id = cast_ids.front();
            selected_index = 0U;
        }
        if (!summ_ && list.empty()) selected_id = 0U;
        // replace_combo_items は同期的に選択
        // を通知する場合がある。 先に対応するID表を設定し、新
        // しい行が同じ位置の古いキャストを描かないようにする。
        const auto old_selected_id = selected_id_;
        cast_ids_.swap(cast_ids);
        selected_id_ = selected_id;
        const auto published = lib::replace_combo_items(casts_, std::move(items), selected_index);
        if (host_life.expired()) return;
        if (!published) {
            cast_ids_.swap(cast_ids);
            selected_id_ = old_selected_id;
            return;
        }
        // D版と同様、検索結果が空でも選択画面は有効なままにする。
        // 元の一覧自体がなくなった場合だけ、エディタを閉じる。
        casts_->set_enabled(!list.empty());
        const auto refreshed = context_.refresh_casts;
        redrawPreview();
        if (!host_life.expired() && refreshed) refreshed();
    }
}

template <typename C>
void SpCardDialog<C>::cardIncSearch(std::string_view value) {
    if constexpr (std::is_same_v<C, EnemyCard>) {
        if (cast_inc_search_) cast_inc_search_->setSearchText(std::string(value));
        else { cast_filter_ = std::string(value); refreshCasts(); }
    }
}

template <typename C>
void SpCardDialog<C>::openCardView() {
    if constexpr (std::is_same_v<C, EnemyCard>) {
        if (!casts_ || !casts_->selected_index || *casts_->selected_index >= cast_ids_.size()) return;
        // D版は絞り込み後の表示位置を、絞り込み前のキャスト一覧に適用す
        // るため、別のキャストを開く場合がある。 ここでは意図的に cast_ids_
        // を使い、表示行から正しい対象を解決する。 ただしD版の
        // 未選択時の動作は維持し、選択行がなければ、絞り込みで隠
        // れた selected_id_ のキャストを開かない。
        const auto open_cast = context_.open_cast_view;
        const auto open_card = context_.open_card_view;
        const auto card = card_;
        const auto id = cast_ids_[*casts_->selected_index];
        if (open_cast) open_cast(id);
        else if (open_card && card) open_card(card.get());
    }
}

template <typename C>
bool SpCardDialog<C>::apply() {
    if (!image_selector_) return false;
    const auto material = image_selector_->materialPath(forceApplying());
    if (material.cancel) return false;
    const auto images = image_selector_->images();
    const auto flag = flagValue();
    const auto group = cardGroupValue();
    const auto x = integerValue(x_);
    const auto y = integerValue(y_);
    const auto scale = static_cast<std::uint32_t>(std::max(1, integerValue(scale_, 100)));
    const auto layer = integerValue(layer_, types::LAYER_MENU_CARD);
    const auto animation = animation_speed_ ? animation_speed_->speed() : -1;
    const auto old_group = card_ ? card_->cardGroup() : std::string{};
    if (card_) {
        if constexpr (std::is_same_v<C, MenuCard>) {
            card_->paths(images);
            card_->desc(normalizedText(desc_->text));
            card_->name(name_->text);
            card_->expandSPChars(expand_sp_chars_->checked);
        } else {
            card_->id(selected_id_);
            // D版と同様、見えている行動だけを更新し、隠れているスキンの上書き設定は保持する。
            for (const auto& [type, control] : actions_)
                card_->action(type, control && control->checked);
            card_->isOverrideName(override_name_enabled_->checked);
            card_->overrideName(override_name_->text);
            card_->isOverrideImage(override_image_enabled_->checked);
            card_->overrideImages(images);
        }
        card_->flag(flag);
        card_->x(x);
        card_->y(y);
        card_->scale(scale);
        card_->layer(layer);
        card_->cardGroup(group);
        card_->animationSpeed(animation);
    } else {
        if constexpr (std::is_same_v<C, MenuCard>) {
            card_ = std::make_shared<MenuCard>(
                name_->text, expand_sp_chars_->checked, images,
                normalizedText(desc_->text), flag, x, y, scale, layer, group,
                animation);
        } else {
            area::ActionMap action_values;
            for (const auto& [type, control] : actions_)
                action_values[type] = control && control->checked;
            card_ = std::make_shared<EnemyCard>(
                selected_id_, action_values, flag, x, y, scale, layer, group,
                animation, override_name_enabled_->checked, override_name_->text,
                override_image_enabled_->checked, images);
        }
    }
    // MainWindow が新しいシーンカードを先に用意し、ダイアログ
    // が存続中に同じオブジェクトを編集する場合がある。 そのため card_
    // の有無では新規・編集を区別せず、create_ を使う。 D版
    // と同様、最初の適用成功時に、その渡されたオブジェクトを追加する。
    if (create_ && card_) {
        if (counter_ && card_->useCounter() != counter_) card_->setUseCounter(counter_);
        if (context_.add_card) context_.add_card(card_);
        create_ = false;
    }
    if (counter_ && card_->useCounter() != counter_) card_->setUseCounter(counter_);
    if (old_group != group && summ_ && !summ_->scenarioPath().empty())
        notifyCardGroups();
    if constexpr (std::is_same_v<C, MenuCard>) {
        title(formatMessage(messageValue(prop_->requiredMsgs().dlgTitMenuCard), card_->name()));
    } else {
        std::string name;
        if (summ_) {
            if (const auto cast = summ_->cwCast(card_->id())) name = cast->name();
        }
        if (name.empty() && prop_)
            name = formatMessage(messageValue(prop_->requiredMsgs().noCast),
                                 std::to_string(card_->id()));
        title(formatMessage(messageValue(prop_->requiredMsgs().dlgTitEnemyCard), name));
    }
    notifyToolbar();
    refreshWarning();
    return true;
}

template <typename C>
bool SpCardDialog<C>::EnemyDrop::drop(std::string_view data) const {
    if (summary_id.empty() || !accepted) return false;
    try {
        auto node = xml::XNode::parse(data);
        if (node.attr("summId", false) != summary_id || !node.template attr<bool>("topLevel", false, false)) return false;
        if (node.name() != CastCard::XML_NAME && node.name() != CastCard::XML_NAME_M) return false;
        const auto candidates = node.name() == CastCard::XML_NAME_M ? node.element_children() : std::vector<xml::XNode>{node};
        for (const auto& candidate : candidates) {
            if (candidate.name() != CastCard::XML_NAME) continue;
            const auto text = candidate.child("Property", true).child("Id", true).value();
            std::uint64_t id{};
            const auto result = std::from_chars(text.data(), text.data() + text.size(), id);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
            if (id) return accepted(id);
        }
        return false;
    } catch (const std::exception&) { return false; }
}

template class SpCardDialog<MenuCard>;
template class SpCardDialog<EnemyCard>;

}  // namespace cwxeditor::cwx::editor::gui::dwt::spcarddialog
