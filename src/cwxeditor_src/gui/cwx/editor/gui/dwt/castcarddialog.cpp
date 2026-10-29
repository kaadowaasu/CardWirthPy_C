#include "castcarddialog.hpp"
#include "summaryskinbinding.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "commons.hpp"
#include "dskin.hpp"
#include "dutils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::castcarddialog {
namespace {

template <typename T>
std::shared_ptr<T> makeComponent(const lib::Services& services,
                                 lib::ComponentKind kind) {
    if (services.gui) {
        if (auto component = services.gui->create_component(kind)) {
            if (auto typed = std::dynamic_pointer_cast<T>(std::move(component)))
                return typed;
        }
    }
    return std::make_shared<T>();
}

[[nodiscard]] bool hasServices(const lib::Services& services) noexcept {
    return static_cast<bool>(services.gui) ||
           static_cast<bool>(services.graphics) ||
           static_cast<bool>(services.image_codecs) ||
           static_cast<bool>(services.dialogs) ||
           static_cast<bool>(services.clipboard) ||
           static_cast<bool>(services.drag_drop) ||
           static_cast<bool>(services.audio) ||
           static_cast<bool>(services.filesystem) ||
           static_cast<bool>(services.processes) ||
           static_cast<bool>(services.shell) ||
           static_cast<bool>(services.ipc) ||
           static_cast<bool>(services.archives) ||
           static_cast<bool>(services.system) ||
           static_cast<bool>(services.text) ||
           static_cast<bool>(services.dispatcher) ||
           static_cast<bool>(services.tasks);
}

[[nodiscard]] lib::Services dialogServices(const CastCardDialogContext& context,
                                            Props& prop) {
    if (hasServices(context.services)) return context.services;
    return prop.images().services();
}

[[nodiscard]] std::string replaceFirst(std::string value,
                                       std::string_view needle,
                                       std::string_view replacement) {
    const auto pos = value.find(needle);
    if (pos != std::string::npos)
        value.replace(pos, needle.size(), replacement);
    return value;
}

[[nodiscard]] int integerValue(const std::shared_ptr<lib::NumberEdit>& control,
                               int fallback = 0) noexcept {
    if (!control) return fallback;
    const auto value = std::clamp(control->value,
                                  static_cast<double>(std::numeric_limits<int>::min()),
                                  static_cast<double>(std::numeric_limits<int>::max()));
    return static_cast<int>(std::lround(value));
}

void setNumberDisplayValue(const std::shared_ptr<lib::NumberEdit>& control,
                           const double raw_value) {
    if (!control) return;
    control->value = lib::editor_window_host_options().all_mode
                         ? raw_value
                         : std::clamp(raw_value, control->minimum, control->maximum);
}

[[nodiscard]] int roundedMental(const double value) noexcept {
    // Dの既存カードの初期化は、小数値の場合だけ半段階を除く。CalcMental
    // の種族の境界値には、別の無条件の規則がある。
    if (std::trunc(value) == value) return static_cast<int>(value);
    return static_cast<int>(value < 0.0 ? value + 0.5 : value - 0.5);
}

[[nodiscard]] lib::ImageData fallbackAptitudeMark(const std::size_t grade) {
    constexpr std::uint32_t side = 12U;
    lib::ImageFrame frame;
    frame.logical_size = {static_cast<float>(side), static_cast<float>(side)};
    frame.pixel_width = side;
    frame.pixel_height = side;
    frame.pixel_format = lib::PixelFormat::RGBA32;
    frame.pixels.assign(static_cast<std::size_t>(side) * side * 4U,
                        std::byte{0});
    const std::array<std::array<std::uint8_t, 3>, 4> colours{{
        {{0x12, 0xE8, 0x2D}}, {{0x0C, 0xC9, 0x28}},
        {{0x72, 0xB7, 0x23}}, {{0xA0, 0xA0, 0xA0}}}};
    const auto colour = colours[std::min<std::size_t>(grade, 3U)];
    for (std::uint32_t y = 0; y < side; ++y) {
        for (std::uint32_t x = 0; x < side; ++x) {
            const int dx = static_cast<int>(x) - 5;
            const int dy = static_cast<int>(y) - 5;
            const int distance = dx * dx + dy * dy;
            if (distance > 30) continue;
            const auto offset = (static_cast<std::size_t>(y) * side + x) * 4U;
            const bool outline = distance >= 21;
            frame.pixels[offset + 0U] = std::byte{static_cast<unsigned char>(outline ? 0U : colour[0])};
            frame.pixels[offset + 1U] = std::byte{static_cast<unsigned char>(outline ? 0U : colour[1])};
            frame.pixels[offset + 2U] = std::byte{static_cast<unsigned char>(outline ? 0U : colour[2])};
            frame.pixels[offset + 3U] = std::byte{0xFF};
        }
    }
    lib::ImageData result;
    result.format = lib::ImageFormat::Bitmap;
    result.frames.push_back(std::move(frame));
    return result;
}

[[nodiscard]] std::string featureText(const props::Msgs& messages,
                                      std::string_view value) {
    if (value == "name") return messages.name.value;
    if (value == "desc") return messages.desc.value;
    if (value == "level") return messages.level.value;
    if (value == "life") return messages.life.value;
    if (value == "race") return messages.race.value;
    if (value == "coupons") return messages.coupons.value;
    if (value == "history") return messages.history.value;
    if (value == "tolerant") return messages.tolerant.value;
    if (value == "physical") return messages.physicalParams.value;
    if (value == "mental") return messages.mentalParams.value;
    if (value == "enhance") return messages.castEnhance.value;
    if (value == "status") return messages.liveStatus.value;
    return std::string(value);
}

[[nodiscard]] std::string physicalName(const props::Msgs& m,
                                       const types::Physical value) {
    switch (value) {
    case types::Physical::Dex: return m.physicalNameDex.value;
    case types::Physical::Agl: return m.physicalNameAgl.value;
    case types::Physical::Int: return m.physicalNameInt.value;
    case types::Physical::Str: return m.physicalNameStr.value;
    case types::Physical::Vit: return m.physicalNameVit.value;
    case types::Physical::Min: return m.physicalNameMin.value;
    }
    return {};
}

[[nodiscard]] std::string mentalName(const props::Msgs& m,
                                     const types::Mental value) {
    switch (value) {
    case types::Mental::Aggressive: return m.mentalNameAggressive.value;
    case types::Mental::Unaggressive: return m.mentalNameUnaggressive.value;
    case types::Mental::Cheerful: return m.mentalNameCheerful.value;
    case types::Mental::Uncheerful: return m.mentalNameUncheerful.value;
    case types::Mental::Brave: return m.mentalNameBrave.value;
    case types::Mental::Unbrave: return m.mentalNameUnbrave.value;
    case types::Mental::Cautious: return m.mentalNameCautious.value;
    case types::Mental::Uncautious: return m.mentalNameUncautious.value;
    case types::Mental::Trickish: return m.mentalNameTrickish.value;
    case types::Mental::Untrickish: return m.mentalNameUntrickish.value;
    }
    return {};
}

[[nodiscard]] types::Mental oppositeMental(const types::Mental value) noexcept {
    switch (value) {
    case types::Mental::Aggressive: return types::Mental::Unaggressive;
    case types::Mental::Unaggressive: return types::Mental::Aggressive;
    case types::Mental::Cheerful: return types::Mental::Uncheerful;
    case types::Mental::Uncheerful: return types::Mental::Cheerful;
    case types::Mental::Brave: return types::Mental::Unbrave;
    case types::Mental::Unbrave: return types::Mental::Brave;
    case types::Mental::Cautious: return types::Mental::Uncautious;
    case types::Mental::Uncautious: return types::Mental::Cautious;
    case types::Mental::Trickish: return types::Mental::Untrickish;
    case types::Mental::Untrickish: return types::Mental::Trickish;
    }
    return value;
}

[[nodiscard]] std::string enhanceName(const props::Msgs& m,
                                      const types::Enhance value) {
    switch (value) {
    case types::Enhance::Action: return m.enhanceNameAction.value;
    case types::Enhance::Avoid: return m.enhanceNameAvoid.value;
    case types::Enhance::Resist: return m.enhanceNameResist.value;
    case types::Enhance::Defense: return m.enhanceNameDefense.value;
    }
    return {};
}

[[nodiscard]] std::string mentalityName(const props::Msgs& m,
                                        const types::Mentality value) {
    switch (value) {
    case types::Mentality::Normal: return m.mentalityNameNormal.value;
    case types::Mentality::Sleep: return m.mentalityNameSleep.value;
    case types::Mentality::Confuse: return m.mentalityNameConfuse.value;
    case types::Mentality::Overheat: return m.mentalityNameOverheat.value;
    case types::Mentality::Brave: return m.mentalityNameBrave.value;
    case types::Mentality::Panic: return m.mentalityNamePanic.value;
    }
    return {};
}

[[nodiscard]] std::string elementName(const props::Msgs& m,
                                      const types::Element value) {
    switch (value) {
    case types::Element::Fire: return m.elementNameFire.value;
    case types::Element::Ice: return m.elementNameIce.value;
    default: return {};
    }
}

[[nodiscard]] std::string dialogTitle(Props& prop, const Summary* summ,
                                      const CastCardPtr& card) {
    if (!card) return prop.requiredMsgs().dlgTitNewCast.value;
    auto identity = replaceFirst(prop.requiredMsgs().nameWithID.value, "%1$s",
                                 std::to_string(card->id()));
    identity = replaceFirst(std::move(identity), "%2$s", card->name());
    if (identity == prop.requiredMsgs().nameWithID.value) {
        identity = std::to_string(card->id()) + "." + card->name();
    }
    auto result = replaceFirst(prop.requiredMsgs().dlgTitCast.value, "%1$s", identity);
    if (result == prop.requiredMsgs().dlgTitCast.value && summ)
        result += " [ " + identity + " ]";
    return result;
}

[[nodiscard]] std::string selectedComboText(const std::shared_ptr<lib::ComboBox>& combo) {
    if (!combo) return {};
    if (combo->selected_index && *combo->selected_index < combo->items.size())
        return combo->items[*combo->selected_index].text;
    return combo->edit_text;
}

[[nodiscard]] std::shared_ptr<lib::Container> nonOwning(
    lib::Container& value) {
    return std::shared_ptr<lib::Container>(&value, [](lib::Container*) {});
}

}  // namespace

CouponEditor::CouponEditor(lib::Services services,
                           std::shared_ptr<lib::Container> parent,
                           const bool read_only)
    : services_(std::move(services)), read_only_(read_only) {
    panel_ = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    panel_->set_layout(std::make_shared<lib::GridLayout>());
    auto* grid = dynamic_cast<lib::GridLayout*>(panel_->layout().get());
    if (grid) {
        grid->columns = 4U;
        grid->spacing = 4.0F;
    }
    list_ = makeComponent<lib::ListView>(services_, lib::ComponentKind::ListView);
    name_ = makeComponent<lib::TextEdit>(services_, lib::ComponentKind::TextEdit);
    value_ = makeComponent<lib::NumberEdit>(services_, lib::ComponentKind::NumberEdit);
    add_ = makeComponent<lib::Button>(services_, lib::ComponentKind::Button);
    remove_ = makeComponent<lib::Button>(services_, lib::ComponentKind::Button);
    add_->text = "+";
    remove_->text = "-";
    list_->set_layout_item({.row = 0U, .column = 0U, .column_span = 4U,
                            .horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .minimum = {260.0F, 180.0F}, .grow = 1.0F});
    name_->set_layout_item({.row = 1U, .column = 0U,
                            .horizontal = lib::Align::Stretch, .grow = 1.0F});
    value_->set_layout_item({.row = 1U, .column = 1U,
                             .horizontal = lib::Align::Stretch});
    add_->set_layout_item({.row = 1U, .column = 2U});
    remove_->set_layout_item({.row = 1U, .column = 3U});
    name_->on_enter = [weak = std::weak_ptr<Binding>(binding_)] {
        const auto binding = weak.lock();
        auto* owner = binding ? binding->owner : nullptr;
        if (!owner || owner->read_only_ || owner->name_->text.empty()) return;
        const auto revision = binding->revision + 1;
        owner->addCoupon(std::make_shared<Coupon>(owner->name_->text,
                                                 integerValue(owner->value_)));
        if (binding->owner != owner || binding->revision != revision) return;
        owner->name_->text.clear();
        owner->value_->value = 0.0;
    };
    add_->on_click = name_->on_enter;
    remove_->on_click = [weak = std::weak_ptr<Binding>(binding_)] {
        const auto binding = weak.lock();
        auto* owner = binding ? binding->owner : nullptr;
        if (!owner || owner->read_only_ || !owner->list_ || owner->list_->selection.empty()) return;
        auto indices = owner->list_->selection;
        std::sort(indices.rbegin(), indices.rend());
        for (const auto index : indices) {
            const auto revision = binding->revision + 1;
            owner->removeCoupon(index);
            if (binding->owner != owner || binding->revision != revision) return;
        }
    };
    panel_->add(list_);
    panel_->add(name_);
    panel_->add(value_);
    panel_->add(add_);
    panel_->add(remove_);
    if (parent) parent->add(panel_);
    readOnly(read_only);
}

CouponEditor::~CouponEditor() {
    if (binding_) binding_->owner = nullptr;
}

CouponEditor::CouponEditor(CouponEditor&& value) noexcept {
    *this = std::move(value);
}

CouponEditor& CouponEditor::operator=(CouponEditor&& value) noexcept {
    if (&value == this) return *this;
    if (binding_) binding_->owner = nullptr;
    binding_ = std::move(value.binding_);
    if (binding_) binding_->owner = this;
    services_ = std::move(value.services_); panel_ = std::move(value.panel_);
    list_ = std::move(value.list_); name_ = std::move(value.name_);
    value_ = std::move(value.value_); add_ = std::move(value.add_);
    remove_ = std::move(value.remove_); coupons_ = std::move(value.coupons_);
    modEvent = std::move(value.modEvent); read_only_ = value.read_only_;
    return *this;
}

void CouponEditor::coupons(CouponArray value) {
    if (!binding_) binding_ = std::make_shared<Binding>(Binding{this});
    ++binding_->revision;
    coupons_ = std::move(value);
    rebuild();
    notifyChanged();
}

void CouponEditor::addCoupon(CouponPtr value) {
    if (read_only_ || !value) return;
    if (!binding_) binding_ = std::make_shared<Binding>(Binding{this});
    ++binding_->revision;
    coupons_.push_back(std::move(value));
    rebuild();
    notifyChanged();
}

void CouponEditor::removeCoupon(const std::size_t index) {
    if (read_only_ || index >= coupons_.size()) return;
    if (!binding_) binding_ = std::make_shared<Binding>(Binding{this});
    ++binding_->revision;
    coupons_.erase(coupons_.begin() + static_cast<std::ptrdiff_t>(index));
    rebuild();
    notifyChanged();
}

std::vector<lib::String> CouponEditor::warnings(
    const Props* prop, const Summary* summ,
    const std::function<std::vector<lib::String>(const CouponArray&)>& callback) const {
    if (callback) return callback(coupons_);
    std::vector<lib::String> result;
    if (!prop) return result;
    const bool legacy = summ && summ->legacy();
    const auto version = summ ? summ->dataVersion() : lib::String{};
    for (const auto& value : coupons_) {
        if (!value) continue;
        const auto current = warning::couponWarnings(prop->parent(), legacy, version,
                                                     "4", value->name(), false, {});
        result.insert(result.end(), current.begin(), current.end());
    }
    return result;
}

void CouponEditor::readOnly(const bool value) {
    read_only_ = value;
    if (name_) name_->set_enabled(!value);
    if (value_) value_->set_enabled(!value);
    if (add_) add_->set_enabled(!value);
    if (remove_) remove_->set_enabled(!value);
}

void CouponEditor::rebuild() {
    if (!list_) return;
    std::vector<lib::ListItem> items;
    items.reserve(coupons_.size());
    for (const auto& value : coupons_) {
        if (!value) continue;
        items.push_back({value->name(), std::to_string(value->value()), {}, false,
                         true});
    }
    (void)lib::replace_list_items(list_, std::move(items), {});
}

void CouponEditor::notifyChanged() {
    const auto binding = binding_;
    const auto revision = binding->revision;
    const auto callbacks = modEvent;
    for (const auto& callback : callbacks) {
        if (callback) callback();
        if (binding->owner != this || binding->revision != revision) return;
    }
}

CastCardDialog::CastCardDialog(CastCardDialogContext context, Props& prop,
                               std::shared_ptr<lib::Window> parent,
                               const Summary* summ, CastCardPtr card,
                               const bool read_only, UseCounterPtr counter, const bool creationMode)
    : absdialog::AbsDialog(prop, std::move(parent), false,
                           dialogTitle(prop, summ, card), prop.images().menu(::cwxeditor::cwx::types::MenuID::CastView), true,
                           static_cast<DSize*>(&prop.flex().castCardDlg),
                           !read_only, true, {}, false,
                           dialogServices(context, prop)),
      context_(std::move(context)), prop_(&prop), summ_(summ), card_(std::move(card)),
      counter_(std::move(counter)), read_only_(read_only),
      creation_mode_(creationMode || !card_) {
    context_.common.prop = &prop;
    context_.common.summary = summ;
    if (!hasServices(context_.services)) context_.services = prop.images().services();
    if (!hasServices(context_.common.services)) context_.common.services = context_.services;
    summary_skin_pinned_ = summaryskinbinding::pinsSummarySkin(
        read_only_, summ_);
    (void)resolveSourceSkin();
    coupon_view_.readOnly(read_only_);
    // MainWindowが有効ダイアログの登録を解放する間、AbsDialog
    // は閉じたネイティブウィンドウより長く残り得る。終了時にDのDispose
    // リスナーと同じ処理を行い、閉じた画面へ後のEditor通知を送らない。
    closeEvent.push_back([this] {
        ++cast_revision_;
        on_applying = {};
        unbindExternalNotifications();
    });
    closeEvent.push_back([this] {
        const auto selector = image_selector_;
        if (selector) selector->active(false);
    });
    openedEvent.push_back([this] {
        const auto selector = image_selector_;
        if (selector) selector->active(true);
    });
    if (creation_mode_ && !read_only_) applyEnabled(true);
    enterClose(true);
    // Dの内容に合わせたCastCardDialogのクライアント寸法は
    // 論理532×469。482×432の小さい代替値では、FixedWidthText
    // がグループの高さを使うはずの説明ページが潰れる。
    dialog()->minimum_size = lib::Size{532.0F, 469.0F};
}

CastCardDialog::CastCardDialog(Commons common, Props& prop,
                               std::shared_ptr<lib::Window> parent,
                               const Summary* summ, CastCardPtr card,
                               const bool read_only, UseCounterPtr counter)
    : CastCardDialog([&] {
          CastCardDialogContext value(std::move(common));
          value.common.prop = &prop;
          value.common.summary = summ;
          value.services = value.common.services;
          return value;
      }(), prop, std::move(parent), summ, std::move(card), read_only,
      std::move(counter)) {}

CastCardDialog::~CastCardDialog() {
    cast_lifetime_.reset();
    unbindExternalNotifications();
    if (!context_.services.graphics) return;
    for (const auto image : aptitude_mark_images_)
        if (image != 0U) context_.services.graphics->destroy_image(image);
}

void CastCardDialog::bindExternalNotifications() {
    if (notification_commons_ || !context_.editor_commons) return;
    notification_commons_ = context_.editor_commons;
    ref_cast_connection_ = notification_commons_->refCast.add(
        [this](const CastCardPtr& value) { updateTitle(value.get()); });
    del_cast_connection_ = notification_commons_->delCast.add(
        [this](const CastCardPtr& value) { delCard(value); });
    ref_scenario_connection_ = notification_commons_->refScenario.add(
        [this](const editor_commons::SummaryPtr& value) { refScenario(value.get()); });
    ref_skin_connection_ = notification_commons_->refSkin.add([this] { refSkin(); });
    // DはSkinと画像倍率の変更に同じrefSkinハンドラーを
    // 使う。共通の選択部のプレビュー更新も、そのハンドラーで扱う。
    ref_image_scale_connection_ = notification_commons_->refImageScale.add(
        [this] { refSkin(); });
    ref_data_version_connection_ = notification_commons_->refDataVersion.add(
        [this] { refDataVersion(); });
    ref_target_version_connection_ = notification_commons_->refTargetVersion.add(
        [this] { refDataVersion(); });
    ref_coupons_connection_ = notification_commons_->refCoupons.add(
        [this] { updateNature(); });
    ref_cast_card_parameter_edit_style_connection_ =
        notification_commons_->refCastCardParameterEditStyle.add(
            [this] { refCastCardParameterEditStyle(); });
    ref_radar_style_connection_ = notification_commons_->refRadarStyle.add(
        [this] { initEnhance(); });
    // 共通の耐性操作部は現在のスキンからラベルを得る。
    // 同じ更新経路で、Dの属性上書きの更新も維持する。
    ref_element_overrides_connection_ = notification_commons_->refElementOverrides.add(
        [this] { refSkin(); });
}

void CastCardDialog::unbindExternalNotifications() noexcept {
    if (!notification_commons_) return;
    notification_commons_->refCast.remove(ref_cast_connection_);
    notification_commons_->delCast.remove(del_cast_connection_);
    notification_commons_->refScenario.remove(ref_scenario_connection_);
    notification_commons_->refSkin.remove(ref_skin_connection_);
    notification_commons_->refImageScale.remove(ref_image_scale_connection_);
    notification_commons_->refDataVersion.remove(ref_data_version_connection_);
    notification_commons_->refTargetVersion.remove(ref_target_version_connection_);
    notification_commons_->refCoupons.remove(ref_coupons_connection_);
    notification_commons_->refCastCardParameterEditStyle.remove(
        ref_cast_card_parameter_edit_style_connection_);
    notification_commons_->refRadarStyle.remove(ref_radar_style_connection_);
    notification_commons_->refElementOverrides.remove(ref_element_overrides_connection_);
    notification_commons_ = nullptr;
    ref_cast_connection_ = 0;
    del_cast_connection_ = 0;
    ref_scenario_connection_ = 0;
    ref_skin_connection_ = 0;
    ref_image_scale_connection_ = 0;
    ref_data_version_connection_ = 0;
    ref_target_version_connection_ = 0;
    ref_coupons_connection_ = 0;
    ref_cast_card_parameter_edit_style_connection_ = 0;
    ref_radar_style_connection_ = 0;
    ref_element_overrides_connection_ = 0;
}

const Skin* CastCardDialog::summSkin() const noexcept {
    return summ_skin_ ? summ_skin_ : context_.common.skin;
}

bool CastCardDialog::resolveSourceSkin() {
    const std::weak_ptr<void> life = cast_lifetime_;
    const auto revision = cast_revision_;
    const auto previous_owner = source_skin_owner_;
    const auto callback = context_.resolve_source_skin;
    const auto source = summ_;
    const auto comm = context_.editor_commons;
    const auto prop = prop_;
    const auto fallback = context_.common.skin;
    const bool source_needed = source && summary_skin_pinned_;
    lib::Owner<Skin> resolved;
    if (source_needed) {
        if (callback) resolved = callback(source);
        else if (comm) resolved = editor_commons::findSkin(*comm, *prop, source);
    }
    if (life.expired() || cast_revision_ != revision) return false;
    source_skin_owner_ = std::move(resolved);
    summ_skin_ = source_skin_owner_ ? source_skin_owner_.get() : fallback;
    return true;
}

const race::Race* CastCardDialog::selectedRace() const noexcept {
    if (!race_ || !race_->selected_index || *race_->selected_index == 0U)
        return nullptr;
    const auto* skin_value = summSkin();
    if (!skin_value) return nullptr;
    const auto index = *race_->selected_index - 1U;
    if (index >= skin_value->races().size()) return nullptr;
    return skin_value->races()[index].get();
}

bool CastCardDialog::openCWXPath(const std::string_view path,
                                 const bool shell_activate) {
    (void)shell_activate;
    // このダイアログは自身の空パス・属性だけのパスを受け
    // 付ける。ホストの汎用資源の振り分け処理ではない。
    return ::cwxeditor::cwx::path::cpempty(path);
}

void CastCardDialog::raceToolTip() {
    if (!race_) return;
    const auto* value = selectedRace();
    auto description = value ? value->desc() : std::string{};
    const auto first = description.find_first_not_of('\n');
    description = first == std::string::npos ? std::string{}
        : description.substr(first, description.find_last_not_of('\n') - first + 1);
    // 共通ツールチップAPIは文字列をそのまま表示する。SWTのア
    // ンパサンド二つのエスケープを渡すと、余分な文字が表示される。
    race_->set_tooltip(std::move(description));
}

void CastCardDialog::refreshWarning() {
    const std::weak_ptr<void> owner_life = cast_lifetime_;
    const auto revision = cast_revision_;
    const auto current = [&] { return !owner_life.expired() && cast_revision_ == revision; };
    if (name_limit_) name_limit_->refreshLimit();
    warnings_.clear();
    if (!prop_) return;
    if (const auto callback = context_.text_warnings) {
        const auto values = callback(name_ ? name_->text : std::string{});
        if (!current()) return;
        warnings_.insert(warnings_.end(), values.begin(), values.end());
    } else if (summSkin()) {
        const auto values = warning::sjisWarnings(prop_->parent(), summ_,
                                                  name_ ? name_->text : std::string{},
                                                  prop_->requiredMsgs().name.value);
        warnings_.insert(warnings_.end(), values.begin(), values.end());
    }
    if (name_limit_ && name_limit_->over()) {
        auto value = replaceFirst(prop_->requiredMsgs().warningNameLenOver.value, "%2$s",
                                  std::to_string(prop_->looks().castNameLimit() / 2U));
        value = replaceFirst(std::move(value), "%1$s",
                             std::to_string(prop_->looks().castNameLimit()));
        warnings_.push_back(std::move(value));
    }
    if (image_selector_) {
        const auto selector = image_selector_;
        const auto values = selector->warnings();
        if (!current()) return;
        warnings_.insert(warnings_.end(), values.begin(), values.end());
    }
    if (const auto callback = context_.text_warnings) {
        const auto values = callback(desc_ ? desc_->text : std::string{});
        if (!current()) return;
        warnings_.insert(warnings_.end(), values.begin(), values.end());
    } else if (summSkin()) {
        const auto values = warning::sjisWarnings(prop_->parent(), summ_,
                                                  desc_ ? desc_->text : std::string{},
                                                  prop_->requiredMsgs().desc.value);
        warnings_.insert(warnings_.end(), values.begin(), values.end());
    }
    const auto coupon_records = full_coupon_view_ ? full_coupon_view_->coupons()
                                                  : coupon_view_.coupons();
    const auto coupon_warning = context_.coupon_warnings;
    const auto coupon_values = coupon_warning
        ? coupon_warning(coupon_records)
        : full_coupon_view_ ? full_coupon_view_->warnings()
                            : coupon_view_.warnings(prop_, summ_);
    if (!current()) return;
    warnings_.insert(warnings_.end(), coupon_values.begin(), coupon_values.end());

    std::vector<types::Status> statuses;
    const int maximum = life_max_ ? integerValue(life_max_) : 1;
    const int life = life_ ? integerValue(life_) : maximum;
    if (life_use_max_ && life_use_max_->checked) statuses.push_back(types::Status::Fine);
    else if (life <= 0) statuses.push_back(types::Status::Unconscious);
    else if (life <= maximum / 5) statuses.push_back(types::Status::HeavyInjured);
    else if (life < maximum) statuses.push_back(types::Status::Injured);
    else statuses.push_back(types::Status::Fine);
    for (const auto enhance : enhancements()) {
        const auto value = integerValue(live_enhance_[enhance]);
        if (value < 0) {
            statuses.push_back(static_cast<types::Status>(
                static_cast<int>(types::Status::DownAction) +
                static_cast<int>(enhance)));
        } else if (value > 0) {
            statuses.push_back(static_cast<types::Status>(
                static_cast<int>(types::Status::UpAction) +
                static_cast<int>(enhance)));
        }
    }
    if (integerValue(paralyze_) > 0) statuses.push_back(types::Status::Paralyze);
    if (integerValue(poison_) > 0) statuses.push_back(types::Status::Poison);
    if (integerValue(bind_) > 0) statuses.push_back(types::Status::Bind);
    if (integerValue(silence_) > 0) statuses.push_back(types::Status::Silence);
    if (integerValue(face_up_) > 0) statuses.push_back(types::Status::FaceUp);
    if (integerValue(anti_magic_) > 0) statuses.push_back(types::Status::AntiMagic);
    if (mentality_ && mentality_->selected_index &&
        *mentality_->selected_index < mentality_table_.size() &&
        mentality_table_[*mentality_->selected_index] != types::Mentality::Normal) {
        switch (mentality_table_[*mentality_->selected_index]) {
        case types::Mentality::Normal: break;
        case types::Mentality::Sleep: statuses.push_back(types::Status::Sleep); break;
        case types::Mentality::Confuse: statuses.push_back(types::Status::Confuse); break;
        case types::Mentality::Overheat: statuses.push_back(types::Status::Overheat); break;
        case types::Mentality::Brave: statuses.push_back(types::Status::Brave); break;
        case types::Mentality::Panic: statuses.push_back(types::Status::Panic); break;
        }
    }
    const auto inconsistent = inconsistencyWarnings(statuses);
    if (!current()) return;
    warnings_.insert(warnings_.end(), inconsistent.begin(), inconsistent.end());
    warning(warnings_);
    if (const auto callback = context_.refresh_warnings) callback();
}

std::vector<lib::String> CastCardDialog::inconsistencyWarnings(
    const std::span<const types::Status> statuses) const {
    if (const auto callback = context_.status_warnings) return callback(statuses);
    if (!prop_) return {};
    return warning::warningInconsistency(prop_->parent(), statuses);
}

void CastCardDialog::setup(Container& area) {
    // AbsDialogの内容領域の既定は縦方向の流し込み。埋める
    // 指定がなければPanelは推奨高28pxしか得られず、ネイテ
    // ィブTabViewの見出しは見えても全ページの本体が切れる。
    area.set_layout(std::make_shared<lib::FillLayout>());
    root_ = makeComponent<lib::Panel>(context_.services, lib::ComponentKind::Panel);
    // ダイアログ本体はタブフォルダーを一つ持つ。各ページはそこだけに
    // 所属させる。同じページパネルをroot_の兄弟にも追加すると、
    // 最後の通常は空のパネルが選択中のネイティブタブを覆ってしまう。
    root_->set_layout(std::make_shared<lib::FillLayout>());
    auto tabs = makeComponent<lib::TabView>(context_.services,
                                             lib::ComponentKind::TabView);
    tabs->selected_index = 0U;
    root_->add(tabs);
    area.add(root_);

    const std::shared_ptr<lib::Container> detached_page_owner;
    buildBase(detached_page_owner);
    buildDescription(detached_page_owner);
    buildHistory(detached_page_owner);
    buildMakings(detached_page_owner);
    buildResist(detached_page_owner);
    buildPhysical(detached_page_owner);
    buildMental(detached_page_owner);
    buildEnhance(detached_page_owner);
    buildStatus(detached_page_owner);

    tabs->pages.push_back({prop_->requiredMsgs().card.value, {}, base_panel_, false});
    tabs->pages.push_back({featureText(prop_->requiredMsgs(), "desc"), {}, description_panel_, false});
    tabs->pages.push_back({featureText(prop_->requiredMsgs(), "history"), {}, history_panel_, false});
    tabs->pages.push_back({prop_->requiredMsgs().makingsTitle.value, {}, makings_panel_, false});
    tabs->pages.push_back({featureText(prop_->requiredMsgs(), "tolerant"), {}, resist_panel_, false});
    tabs->pages.push_back({featureText(prop_->requiredMsgs(), "physical"), {}, physical_panel_, false});
    tabs->pages.push_back({featureText(prop_->requiredMsgs(), "mental"), {}, mental_panel_, false});
    tabs->pages.push_back({featureText(prop_->requiredMsgs(), "enhance"), {}, enhance_panel_, false});
    tabs->pages.push_back({featureText(prop_->requiredMsgs(), "status"), {}, status_panel_, false});

    // コンポーネントの契約は、タブごとに一つの内容パネルを保持
    // する。ネイティブのタブを使わないバックエンドも同じ一覧か
    // らページを得られ、ダイアログ本体に重複した兄弟は不要。
    refCard(card_);
    refDataVersion();
    createAptitudes();
    updateAptitudes();
    refreshWarning();
    bindExternalNotifications();
    dmenu::Commons textCommons;
    textCommons.services = context_.services;
    textCommons.undoMaxEtc = static_cast<std::size_t>(std::max<std::int32_t>(0, prop_->var().undoMaxEtc));
    textCommons.presentation.menuText = [prop = prop_](auto id) { return prop->buildMenu(id); };
    textCommons.presentation.menuImage = [prop = prop_](auto id) { return prop->images().menu(id); };
    text_menus_.clear();
    for (const auto& text : {name_, desc_})
        text_menus_.push_back(dmenu::createTextMenu(textCommons, dmenu::TMM(text)));
}

void CastCardDialog::buildBase(const std::shared_ptr<lib::Container>& parent) {
    base_panel_ = makePanel(context_.services, parent);
    auto base_layout = std::make_shared<lib::GridLayout>();
    base_layout->columns = 2U;
    base_layout->spacing = 6.0F;
    base_layout->padding = {6.0F, 6.0F, 6.0F, 6.0F};
    base_panel_->set_layout(base_layout);
    base_panel_->set_layout_item({.horizontal = lib::Align::Stretch,
                                  .vertical = lib::Align::Stretch,
                                  .minimum = {465.0F, 310.0F}, .grow = 1.0F});
    auto left = makeComponent<lib::Panel>(context_.services, lib::ComponentKind::Panel);
    auto left_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    left_layout->spacing = 6.0F;
    left->set_layout(left_layout);
    left->set_layout_item({.row = 0U, .column = 0U,
                           .horizontal = lib::Align::Stretch,
                           .vertical = lib::Align::Stretch,
                           .minimum = {332.0F, 298.0F}, .grow = 1.0F});
    auto right = makeComponent<lib::Panel>(context_.services, lib::ComponentKind::Panel);
    auto right_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    right_layout->spacing = 6.0F;
    right->set_layout(right_layout);
    right->set_layout_item({.row = 0U, .column = 1U,
                            .horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .minimum = {118.0F, 298.0F}, .grow = 1.0F});
    base_panel_->add(left);
    base_panel_->add(right);

    auto name_parent = makePanel(context_.services, left, prop_->requiredMsgs().name.value);
    auto name_layout = std::make_shared<lib::GridLayout>();
    name_layout->columns = 2U;
    name_layout->spacing = 6.0F;
    name_parent->set_layout(name_layout);
    name_parent->set_layout_item({.horizontal = lib::Align::Stretch,
                                  .minimum = {322.0F, 43.0F},
                                  .preferred = {322.0F, 43.0F}});
    // Dはキャスト名にGBLimitTextの計測幅のしきい値
    // を使う。半角カタカナもここでは描画どおり半セル幅とする。
    name_limit_ = std::make_unique<customtext::GBLimitText>(
        prop_->looks().monospace(),
        static_cast<std::int32_t>(prop_->looks().castNameLimit()),
        name_parent.get(), customtext::STYLE_NONE,
        context_.services.graphics.get());
    name_ = name_limit_->widget();
    name_->max_length = 0U;
    name_->on_changed = [this](std::string_view value) {
        if (!name_ || !prop_) return;
        withRedrawSuspended([this, value] {
            name_->text = std::string(value);
            refreshWarning();
        });
    };
    name_->set_layout_item({.row = 0U, .column = 0U,
                            .horizontal = lib::Align::Stretch, .grow = 1.0F});
    setEditable(name_);
    mod(*name_);
    auto name_hint = makeComponent<lib::Label>(context_.services,
                                                lib::ComponentKind::Label);
    name_hint->text = ::cwxeditor::cwx::utils::tryFormat(
        prop_->requiredMsgs().nameLimit.value, prop_->looks().castNameLimit(),
        prop_->looks().castNameLimit() / 2U);
    name_hint->set_layout_item({.row = 0U, .column = 1U,
                                .horizontal = lib::Align::Start,
                                .vertical = lib::Align::Center});
    name_parent->add(name_hint);

    auto image_context = spcarddialog::SpCardDialogContext{};
    image_context.common = context_.common;
    image_context.editor_commons = context_.editor_commons;
    image_context.services = context_.services;
    image_context.select_material = context_.select_material;
    image_context.select_images = context_.select_images;
    image_context.text_warnings = context_.text_warnings;
    image_context.image_warnings = context_.image_warnings;
    image_selector_ = std::make_shared<spcarddialog::CardImageSelector>(
        std::move(image_context), *prop_, summ_, left,
        types::CardImagePosition::Center, read_only_, false,
        prop_->looks().castCardInsets(),
        [prop = prop_, summ = summ_, name = std::weak_ptr<lib::TextEdit>(name_)] {
            const auto control = name.lock();
            return summary::toExportedImageNameWithCardName(prop->parent(),
                summ ? summ->scenarioName() : std::string{},
                summ ? summ->author() : std::string{},
                control ? control->text : std::string{});
        });
    image_selector_->imageListOwner(dialog());
    image_selector_->modEvent.push_back([this] { if (!ignoreMod()) applyEnabled(); refreshWarning(); });

    auto level_parent = makePanel(context_.services, right, prop_->requiredMsgs().level.value);
    auto level_layout = std::make_shared<lib::LinearLayout>(
        lib::Orientation::Vertical);
    level_layout->spacing = 5.0F;
    level_layout->padding = {0.0F, 6.0F, 0.0F, 6.0F};
    level_parent->set_layout(level_layout);
    level_parent->set_layout_item({.horizontal = lib::Align::Stretch,
                                   .vertical = lib::Align::Stretch,
                                   .minimum = {118.0F, 58.0F},
                                   .preferred = {118.0F, 58.0F}, .grow = 1.0F});
    auto level_top = makeComponent<lib::Panel>(context_.services,
                                                lib::ComponentKind::Panel);
    level_top->set_layout_item({.vertical = lib::Align::Stretch,
                                .minimum = {0.0F, 1.0F},
                                .preferred = {0.0F, 1.0F}, .grow = 1.0F});
    level_parent->add(level_top);
    auto level_row = makeComponent<lib::Panel>(context_.services,
                                                lib::ComponentKind::Panel);
    auto level_row_layout = std::make_shared<lib::GridLayout>();
    level_row_layout->columns = 2U;
    level_row_layout->spacing = 5.0F;
    level_row->set_layout(level_row_layout);
    level_row->set_layout_item({.horizontal = lib::Align::Stretch,
                                .minimum = {0.0F, 24.0F},
                                .preferred = {0.0F, 24.0F}});
    level_parent->add(level_row);
    level_ = makeNumber(level_row, 1.0,
                        static_cast<double>(prop_->var().castLevelMax.value), 1.0);
    level_->set_layout_item({.row = 0U, .column = 0U,
                             .horizontal = lib::Align::Stretch, .grow = 1.0F});
    auto level_hint = std::make_shared<lib::Label>(
        ::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().rangeHint.value, 1, prop_->var().castLevelMax.value));
    level_hint->set_layout_item({.row = 0U, .column = 1U,
                                 .vertical = lib::Align::Center});
    level_row->add(level_hint);
    auto level_bottom = makeComponent<lib::Panel>(context_.services,
                                                   lib::ComponentKind::Panel);
    level_bottom->set_layout_item({.vertical = lib::Align::Stretch,
                                   .minimum = {0.0F, 1.0F},
                                   .preferred = {0.0F, 1.0F}, .grow = 1.0F});
    level_parent->add(level_bottom);
    auto life_parent = makePanel(context_.services, right, prop_->requiredMsgs().life.value);
    auto life_layout = std::make_shared<lib::LinearLayout>(
        lib::Orientation::Vertical);
    life_layout->spacing = 5.0F;
    life_layout->padding = {0.0F, 6.0F, 0.0F, 6.0F};
    life_parent->set_layout(life_layout);
    life_parent->set_layout_item({.horizontal = lib::Align::Stretch,
                                  .vertical = lib::Align::Stretch,
                                  .minimum = {118.0F, 92.0F},
                                  .preferred = {118.0F, 92.0F}, .grow = 1.0F});
    auto life_top = makeComponent<lib::Panel>(context_.services,
                                               lib::ComponentKind::Panel);
    life_top->set_layout_item({.vertical = lib::Align::Stretch,
                               .minimum = {0.0F, 1.0F},
                               .preferred = {0.0F, 1.0F}, .grow = 1.0F});
    life_parent->add(life_top);
    auto life_row = makeComponent<lib::Panel>(context_.services,
                                               lib::ComponentKind::Panel);
    auto life_row_layout = std::make_shared<lib::GridLayout>();
    life_row_layout->columns = 2U;
    life_row_layout->spacing = 5.0F;
    life_row->set_layout(life_row_layout);
    life_row->set_layout_item({.horizontal = lib::Align::Stretch,
                               .minimum = {0.0F, 24.0F},
                               .preferred = {0.0F, 24.0F}});
    life_parent->add(life_row);
    life_max_ = makeNumber(life_row, 1.0,
                            static_cast<double>(prop_->var().lifeMax.value), 1.0);
    life_max_->set_layout_item({.row = 0U, .column = 0U,
                                .horizontal = lib::Align::Stretch, .grow = 1.0F});
    auto life_hint = std::make_shared<lib::Label>(
        ::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().rangeHint.value, 1, prop_->var().lifeMax.value));
    life_hint->set_layout_item({.row = 0U, .column = 1U,
                                .vertical = lib::Align::Center});
    life_row->add(life_hint);
    level_->on_changed = [this](double) { refreshWarning(); };
    life_max_->on_changed = [this](double) {
        setMaxLife();
        refreshWarning();
    };
    mod(*level_);
    mod(*life_max_);
    auto life_calc = makeComponent<lib::Button>(context_.services,
                                                lib::ComponentKind::Button);
    life_calc->text = prop_->requiredMsgs().lifeCalc.value;
    life_calc->on_click = [this] {
        if (!life_max_ || !level_) return;
        const auto vit = physicalValue(types::Physical::Vit);
        const auto min = physicalValue(types::Physical::Min);
        const auto calculated = prop_->looks().lifeCalc(
            static_cast<std::uint32_t>(std::max(1, integerValue(level_))),
            static_cast<std::uint32_t>(std::max(0, vit)),
            static_cast<std::uint32_t>(std::max(0, min)));
        life_max_->value = std::clamp(static_cast<double>(calculated),
                                      life_max_->minimum, life_max_->maximum);
        setMaxLife();
        refreshWarning();
        if (!ignoreMod()) applyEnabled();
    };
    life_calc->set_layout_item({.horizontal = lib::Align::Stretch,
                                .minimum = {0.0F, 24.0F},
                                .preferred = {0.0F, 24.0F}});
    setEditable(life_calc);
    life_parent->add(life_calc);
    auto life_bottom = makeComponent<lib::Panel>(context_.services,
                                                  lib::ComponentKind::Panel);
    life_bottom->set_layout_item({.vertical = lib::Align::Stretch,
                                  .minimum = {0.0F, 1.0F},
                                  .preferred = {0.0F, 1.0F}, .grow = 1.0F});
    life_parent->add(life_bottom);

    auto race_parent = makePanel(context_.services, right, prop_->requiredMsgs().race.value);
    auto race_layout = std::make_shared<lib::GridLayout>();
    race_layout->columns = 1U;
    race_parent->set_layout(race_layout);
    race_parent->set_layout_item({.horizontal = lib::Align::Stretch,
                                  .vertical = lib::Align::Stretch,
                                  .minimum = {118.0F, 58.0F},
                                  .preferred = {118.0F, 58.0F}, .grow = 1.0F});
    auto race_top = makeComponent<lib::Panel>(context_.services,
                                               lib::ComponentKind::Panel);
    race_top->set_layout_item({.row = 0U, .column = 0U,
                               .vertical = lib::Align::Stretch,
                               .minimum = {0.0F, 1.0F},
                               .preferred = {0.0F, 1.0F}, .grow = 1.0F});
    race_parent->add(race_top);
    race_ = makeControl<lib::ComboBox>(lib::ComponentKind::ComboBox);
    race_->items.push_back({prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().noRace.value),
                            {}, {}, true});
    if (const auto* value = summSkin()) {
        for (const auto& entry : value->races()) {
            if (entry) race_->items.push_back({entry->name(), entry->name(), {}, true});
        }
    }
    race_->selected_index = 0U;
    race_->on_selection_changed = [this](std::optional<std::size_t>) {
        raceToolTip();
        refreshWarning();
    };
    setEditable(race_);
    mod(*race_);
    race_->set_layout_item({.row = 1U, .column = 0U,
                            .horizontal = lib::Align::Stretch, .grow = 1.0F});
    race_parent->add(race_);
    auto race_bottom = makeComponent<lib::Panel>(context_.services,
                                                  lib::ComponentKind::Panel);
    race_bottom->set_layout_item({.row = 2U, .column = 0U,
                                  .vertical = lib::Align::Stretch,
                                  .minimum = {0.0F, 1.0F},
                                  .preferred = {0.0F, 1.0F}, .grow = 1.0F});
    race_parent->add(race_bottom);
}

void CastCardDialog::buildDescription(const std::shared_ptr<lib::Container>& parent) {
    description_panel_ = makePanel(context_.services, parent);
    description_panel_->set_layout(std::make_shared<lib::FillLayout>());
    description_panel_->set_layout_item({.horizontal = lib::Align::Stretch,
                                         .vertical = lib::Align::Stretch,
                                         .minimum = {470.0F, 315.0F}, .grow = 1.0F});
    auto group = makePanel(context_.services, description_panel_,
                           featureText(prop_->requiredMsgs(), "desc"));
    auto description_layout = std::make_shared<lib::GridLayout>();
    description_layout->columns = 3U;
    // DはここでCenterLayout(HORIZONTAL)とfillVertical=true
    // を使う。編集欄の周囲に通常のグリッド余白は加えず
    // 、縦方向はGroupの見出しだけが領域を使う。
    description_layout->padding = {};
    group->set_layout(description_layout);
    group->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .minimum = {460.0F, 300.0F}, .grow = 1.0F});
    auto left_space = makeComponent<lib::Panel>(context_.services,
                                                 lib::ComponentKind::Panel);
    left_space->set_layout_item({.row = 0U, .column = 0U,
                                 .horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Stretch,
                                 .grow = 1.0F});
    group->add(left_space);
    desc_ = makeControl<lib::TextEdit>(lib::ComponentKind::TextEdit);
    desc_->multiline = true;
    desc_->wrap = true;
    desc_->vertical_scroll = false;
    desc_->set_layout_item({.row = 0U, .column = 1U,
                            .horizontal = lib::Align::Center,
                            .vertical = lib::Align::Stretch,
                            // DはcomputeTextBaseSize(1).xを設定
                            // し、高さのヒントをSWT.DEFAULTへ置き換える。幅
                            // を固定し、高さはグループのクライアント領域全体に広げる。
                            .minimum = {282.0F, 0.0F},
                            .preferred = {282.0F, 0.0F},
                            .constrain_minimum_to_preferred = true});
    // cardDescLenはDのFixedWidthText
    // の表示上の固定行幅。保存する説明文を切り詰めない。
    desc_->max_length = 0U;
    refreshDescriptionFont();
    desc_->on_changed = [this](std::string_view) { refreshWarning(); };
    setEditable(desc_);
    mod(*desc_);
    group->add(desc_);
    auto right_space = makeComponent<lib::Panel>(context_.services,
                                                  lib::ComponentKind::Panel);
    right_space->set_layout_item({.row = 0U, .column = 2U,
                                  .horizontal = lib::Align::Stretch,
                                  .vertical = lib::Align::Stretch,
                                  .grow = 1.0F});
    group->add(right_space);
}

void CastCardDialog::refreshDescriptionFont() {
    if (!desc_ || !prop_) return;
    desc_->minimum_visible_columns = static_cast<std::size_t>(std::max(1, prop_->looks().cardDescLen()));
    // DのFixedWidthTextはcreateFontFromPixels(CFont)
    // でフォントを作るため、正のサイズはセルの画素高の指定。共通描画側
    // が選択書体に対応するセル高を解決する際も、この意味を維持する。
    auto font = dutils::createFontFromPixels(
        prop_->adjustFont(prop_->looks().cardDescFont(
            summSkin() ? summSkin()->legacy() : (summ_ && summ_->legacy()))));
    // カードの説明はシナリオ内容であり、ダイアロ
    // グの操作部ではない。Dの固定幅の説明列に、
    // ALLのUIフォント拡大を二重適用しない。
    font.content_font = true;
    desc_->set_font(std::move(font));
}

std::shared_ptr<lib::RadioButton> CastCardDialog::makeRadio(
    const std::shared_ptr<lib::Container>& parent, std::string text,
    std::string group) {
    auto control = makeControl<lib::RadioButton>(lib::ComponentKind::RadioButton);
    control->text = std::move(text);
    control->group = std::move(group);
    setEditable(control);
    if (parent) parent->add(control);
    return control;
}

std::shared_ptr<lib::CheckBox> CastCardDialog::makeCheck(
    const std::shared_ptr<lib::Container>& parent, std::string text) {
    auto control = makeControl<lib::CheckBox>(lib::ComponentKind::CheckBox);
    control->text = std::move(text);
    setEditable(control);
    if (parent) parent->add(control);
    return control;
}

std::shared_ptr<lib::NumberEdit> CastCardDialog::makeNumber(
    const std::shared_ptr<lib::Container>& parent, const double minimum,
    const double maximum, const double value) {
    auto control = makeControl<lib::NumberEdit>(lib::ComponentKind::NumberEdit);
    control->minimum = minimum;
    control->maximum = std::max(minimum, maximum);
    control->value = std::clamp(value, control->minimum, control->maximum);
    setEditable(control);
    if (parent) parent->add(control);
    return control;
}

std::shared_ptr<lib::Slider> CastCardDialog::makeSlider(
    const std::shared_ptr<lib::Container>& parent, const double minimum,
    const double maximum, const double value) {
    auto control = makeControl<lib::Slider>(lib::ComponentKind::Slider);
    control->minimum = minimum;
    control->maximum = std::max(minimum, maximum);
    control->value = std::clamp(value, control->minimum, control->maximum);
    setEditable(control);
    if (parent) parent->add(control);
    return control;
}

void CastCardDialog::buildHistory(const std::shared_ptr<lib::Container>& parent) {
    history_panel_ = makePanel(context_.services, parent);
    auto history_layout = std::make_shared<lib::GridLayout>();
    history_layout->columns = 2U;
    history_layout->spacing = 8.0F;
    history_layout->padding = {};
    history_panel_->set_layout(history_layout);
    history_panel_->set_layout_item({.horizontal = lib::Align::Stretch,
                                     .vertical = lib::Align::Stretch,
                                     .minimum = {470.0F, 310.0F}, .grow = 1.0F});

    auto coupon_group = makePanel(context_.services, history_panel_,
                                  prop_->requiredMsgs().coupons.value);
    coupon_group->set_layout(std::make_shared<lib::FillLayout>());
    coupon_group->set_layout_item({.row = 0U, .column = 0U,
                                   .horizontal = lib::Align::Stretch,
                                   .vertical = lib::Align::Stretch,
                                   .minimum = {270.0F, 285.0F}, .grow = 1.0F});
    if (context_.editor_commons) {
        // Dは取り込みCastカードも含め、カードの入力元シナリオを渡す。現
        // 在のCommonsのシナリオは形式や文字コードが異なる場合がある。
        Summary::Owner coupon_source;
        if (summ_) {
            coupon_source = std::const_pointer_cast<Summary>(summ_->weak_from_this().lock());
            if (!coupon_source) coupon_source = Summary::Owner(
                const_cast<Summary*>(summ_), [](Summary*) {});
        }
        full_coupon_view_ = std::make_shared<
            couponview::CouponView<couponview::CVType::Cast>>(
                *context_.editor_commons, std::move(coupon_source),
                counter_, coupon_group,
                read_only_ ? customtable::STYLE_READ_ONLY : customtable::STYLE_NONE);
        full_coupon_view_->panel()->set_layout_item({
            .row = 0U, .column = 0U,
            .horizontal = lib::Align::Stretch,
            .vertical = lib::Align::Stretch,
            .minimum = {275.0F, 260.0F}, .grow = 1.0F});
        const std::weak_ptr<void> coupon_life = cast_lifetime_;
        full_coupon_view_->addModifyListener([this, coupon_life] {
            if (coupon_life.expired()) return;
            if (!ignoreMod()) applyEnabled();
            if (!coupon_life.expired()) refreshWarning();
        });
        full_coupon_view_->setFeatureProvider([this, coupon_life] {
            couponview::FeatureInfo result;
            result.period = features::Period(static_cast<std::size_t>(-1));
            if (coupon_life.expired()) return result;
            for (const auto& [feature, button] : period_)
                if (button->checked) { result.period = feature; break; }
            if (const auto* currentSkin = summSkin(); currentSkin && race_ &&
                race_->selected_index && *race_->selected_index > 0 &&
                *race_->selected_index <= currentSkin->races().size())
                result.race = currentSkin->races()[*race_->selected_index - 1];
            return result;
        });
    } else {
        coupon_view_ = CouponEditor(context_.services, coupon_group, read_only_);
        coupon_view_.widget()->set_layout_item({.row = 0U, .column = 0U,
                                                .horizontal = lib::Align::Stretch,
                                                .vertical = lib::Align::Stretch,
                                                .minimum = {275.0F, 260.0F},
                                                .grow = 1.0F});
        coupon_view_.modEvent.push_back([this] { if (!ignoreMod()) applyEnabled(); refreshWarning(); });
    }

    auto traits = makeComponent<lib::Panel>(context_.services,
                                            lib::ComponentKind::Panel);
    auto traits_layout = std::make_shared<lib::GridLayout>();
    traits_layout->columns = 2U;
    traits_layout->spacing = 5.0F;
    traits->set_layout(traits_layout);
    traits->set_layout_item({.row = 0U, .column = 1U,
                             .horizontal = lib::Align::Stretch,
                             .vertical = lib::Align::Stretch,
                             .minimum = {180.0F, 285.0F}});
    history_panel_->add(traits);

    auto sex_panel = makePanel(context_.services, traits, prop_->requiredMsgs().sexTitle.value);
    auto sex_layout = std::make_shared<lib::GridLayout>();
    sex_layout->columns = 1U;
    sex_panel->set_layout(sex_layout);
    sex_panel->set_layout_item({.row = 0U, .column = 0U,
                                .horizontal = lib::Align::Stretch,
                                .vertical = lib::Align::Stretch,
                                .minimum = {98.0F, 84.0F}, .grow = 1.0F});
    if (const auto* value = summSkin()) {
        for (const auto feature : value->allSexes()) {
            const auto name = value->sexName(feature);
            const auto control = makeRadio(
                sex_panel, prop_->requiredMsgs().sex.get(name, name), "sex");
            control->on_changed = [this, feature](bool checked) {
                if (checked && sex_unknown_) sex_unknown_->checked = false;
                refreshWarning();
            };
            mod(*control);
            sex_[feature] = control;
        }
    }
    sex_unknown_ = makeRadio(sex_panel, prop_->requiredMsgs().sexUnknown.value, "sex");
    sex_unknown_->on_changed = [this](bool checked) {
        if (!checked) return;
        for (const auto& entry : sex_) entry.second->checked = false;
        refreshWarning();
    };
    mod(*sex_unknown_);

    auto period_panel = makePanel(context_.services, traits,
                                  prop_->requiredMsgs().periodTitle.value);
    auto period_layout = std::make_shared<lib::GridLayout>();
    period_layout->columns = 2U;
    period_panel->set_layout(period_layout);
    period_panel->set_layout_item({.row = 0U, .column = 1U,
                                   .horizontal = lib::Align::Stretch,
                                   .vertical = lib::Align::Stretch,
                                   .minimum = {98.0F, 84.0F}, .grow = 1.0F});
    if (const auto* value = summSkin()) {
        for (const auto feature : value->allPeriods()) {
            const auto name = value->periodName(feature);
            const auto control = makeRadio(
                period_panel, prop_->requiredMsgs().period.get(name, name), "period");
            control->on_changed = [this, feature](bool checked) {
                if (checked && period_unknown_) period_unknown_->checked = false;
                refreshWarning();
            };
            mod(*control);
            period_[feature] = control;
        }
    }
    period_unknown_ = makeRadio(period_panel, prop_->requiredMsgs().periodUnknown.value, "period");
    period_unknown_->on_changed = [this](bool checked) {
        if (!checked) return;
        for (const auto& entry : period_) entry.second->checked = false;
        refreshWarning();
    };
    mod(*period_unknown_);

    nature_parent_ = makePanel(context_.services, traits,
                               prop_->requiredMsgs().natureTitle.value);
    auto nature_layout = std::make_shared<lib::GridLayout>();
    nature_layout->columns = 2U;
    nature_layout->equal_width = true;
    nature_parent_->set_layout(nature_layout);
    nature_parent_->set_layout_item({.row = 1U, .column = 0U,
                                     .column_span = 2U,
                                     .horizontal = lib::Align::Stretch,
                                     .vertical = lib::Align::Stretch,
                                     .minimum = {200.0F, 170.0F}, .grow = 1.0F});
    updateNature();
}

void CastCardDialog::buildMakings(const std::shared_ptr<lib::Container>& parent) {
    makings_panel_ = makePanel(context_.services, parent);
    makings_panel_->set_layout(std::make_shared<lib::FillLayout>());
    makings_panel_->set_layout_item({.horizontal = lib::Align::Stretch,
                                     .vertical = lib::Align::Stretch,
                                     .minimum = {470.0F, 300.0F}, .grow = 1.0F});
    auto group = makePanel(context_.services, makings_panel_,
                           prop_->requiredMsgs().coupons.value);
    auto makings_layout = std::make_shared<lib::GridLayout>();
    makings_layout->columns = 4U;
    makings_layout->equal_width = true;
    makings_layout->spacing = 5.0F;
    makings_layout->padding = {8.0F, 8.0F, 8.0F, 8.0F};
    group->set_layout(makings_layout);
    group->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .minimum = {450.0F, 280.0F}, .grow = 1.0F});
    if (const auto* value = summSkin()) {
        for (const auto making : value->leftMakings()) {
            const auto makeOne = [this, group, value](features::Makings feature) {
                const auto name = value->makingsName(feature);
                const auto control = makeCheck(
                    group, prop_->requiredMsgs().makings.get(name, name));
                control->on_changed = [this, feature](bool checked) {
                    if (checked && summSkin()) {
                        const auto reverse = summSkin()->reverseMakings(feature);
                        const auto found = makings_.find(reverse);
                        if (found != makings_.end()) found->second->checked = false;
                    }
                    refreshWarning();
                };
                mod(*control);
                makings_[feature] = control;
            };
            makeOne(making);
            makeOne(value->reverseMakings(making));
        }
    }
}

void CastCardDialog::buildResist(const std::shared_ptr<lib::Container>& parent) {
    if (resist_panel_) resist_panel_->clear();
    else resist_panel_ = makePanel(context_.services, parent);
    auto page_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    page_layout->spacing = 6.0F;
    page_layout->padding = {};
    resist_panel_->set_layout(page_layout);
    resist_panel_->set_layout_item({.horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Stretch,
                                    .minimum = {460.0F, 330.0F}, .grow = 1.0F});

    const auto makeTolerance = [this](const std::shared_ptr<lib::Container>& group,
                                      std::string title, std::string description,
                                      const std::size_t row,
                                      const std::size_t column) {
        auto item = makeComponent<lib::Panel>(context_.services,
                                              lib::ComponentKind::Panel);
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 2U;
        layout->spacing = 2.0F;
        item->set_layout(layout);
        item->set_layout_item({.row = row, .column = column,
                               .horizontal = lib::Align::Stretch,
                               .vertical = lib::Align::Stretch,
                               .minimum = {200.0F, 54.0F}, .grow = 1.0F});
        group->add(item);
        auto check = makeCheck(item, std::move(title));
        check->set_layout_item({.row = 0U, .column = 0U, .column_span = 2U,
                                .horizontal = lib::Align::Stretch});
        auto indent = makeComponent<lib::Panel>(context_.services,
                                                lib::ComponentKind::Panel);
        indent->set_layout_item({.row = 1U, .column = 0U,
                                 .minimum = {20.0F, 1.0F},
                                 .preferred = {20.0F, 1.0F}});
        item->add(indent);
        auto desc = makeComponent<lib::Label>(context_.services,
                                              lib::ComponentKind::Label);
        desc->text = std::move(description);
        desc->set_layout_item({.row = 1U, .column = 1U,
                               .horizontal = lib::Align::Stretch,
                               .vertical = lib::Align::Center, .grow = 1.0F});
        item->add(desc);
        check->on_changed = [this](bool) { refreshWarning(); };
        mod(*check);
        return check;
    };

    auto base_group = makePanel(context_.services, resist_panel_,
                                prop_->requiredMsgs().tolerantBase.value);
    auto base_layout = std::make_shared<lib::GridLayout>();
    base_layout->columns = 2U;
    base_layout->spacing = static_cast<float>(
        std::max(5, prop_->var().radioGroupSeparatorWidth.value));
    base_group->set_layout(base_layout);
    base_group->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .minimum = {450.0F, 82.0F},
                                 .preferred = {450.0F, 82.0F}});
    resist_weapon_ = makeTolerance(base_group, prop_->requiredMsgs().resistWeapon.value,
                                    prop_->requiredMsgs().descResistWeapon.value, 0U, 0U);
    resist_magic_ = makeTolerance(base_group, prop_->requiredMsgs().resistMagic.value,
                                   prop_->requiredMsgs().descResistMagic.value, 0U, 1U);

    auto element_group = makePanel(context_.services, resist_panel_,
                                   prop_->requiredMsgs().tolerantElement.value);
    auto element_layout = std::make_shared<lib::GridLayout>();
    element_layout->columns = 2U;
    element_layout->spacing = static_cast<float>(
        std::max(5, prop_->var().radioGroupSeparatorWidth.value));
    element_group->set_layout(element_layout);
    element_group->set_layout_item({.horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Stretch,
                                    .minimum = {450.0F, 205.0F}, .grow = 1.0F});

    const auto overrides = summSkin()
        ? prop_->elementOverrides(summSkin()->type())
        : dprops::ElementOverrideTable{};
    const auto elementText = [this, &overrides](const types::Element element) {
        const auto found = overrides.find(element);
        if (found != overrides.end() && !found->second.name.empty())
            return found->second.name;
        return prop_->requiredMsgs().elementName(element);
    };
    const auto targetText = [this, &overrides](const types::Element element,
                                                const std::string& fallback) {
        const auto found = overrides.find(element);
        if (found != overrides.end() && !found->second.targetType.empty())
            return found->second.targetType;
        return fallback;
    };
    undead_ = makeTolerance(element_group,
                            targetText(types::Element::Health,
                                       prop_->requiredMsgs().undead.value),
                            format(prop_->requiredMsgs().descResist.value,
                                   elementText(types::Element::Health)), 0U, 0U);
    automaton_ = makeTolerance(element_group,
                               targetText(types::Element::Mind,
                                          prop_->requiredMsgs().automaton.value),
                               format(prop_->requiredMsgs().descResist.value,
                                      elementText(types::Element::Mind)), 0U, 1U);
    unholy_ = makeTolerance(element_group,
                            targetText(types::Element::Miracle,
                                       prop_->requiredMsgs().unholy.value),
                            format(prop_->requiredMsgs().descEffective.value,
                                   elementText(types::Element::Miracle)), 1U, 0U);
    constructure_ = makeTolerance(element_group,
                                  targetText(types::Element::Magic,
                                             prop_->requiredMsgs().constructure.value),
                                  format(prop_->requiredMsgs().descEffective.value,
                                         elementText(types::Element::Magic)), 1U, 1U);
    for (const auto element : elements()) {
        const auto element_name = elementText(element);
        auto resist = makeTolerance(
            element_group, format(prop_->requiredMsgs().resistText.value, element_name),
            format(prop_->requiredMsgs().descResist.value, element_name),
            element == types::Element::Fire ? 2U : 3U, 0U);
        auto weak = makeTolerance(
            element_group, format(prop_->requiredMsgs().weaknessText.value, element_name),
            format(prop_->requiredMsgs().descWeakness.value, element_name),
            element == types::Element::Fire ? 2U : 3U, 1U);
        resist->on_changed = [this, weak](bool checked) {
            if (checked) weak->checked = false;
            refreshWarning();
        };
        weak->on_changed = [this, resist](bool checked) {
            if (checked) resist->checked = false;
            refreshWarning();
        };
        mod(*resist);
        mod(*weak);
        resist_[element] = resist;
        weakness_[element] = weak;
    }
    auto basic = makeComponent<lib::Button>(context_.services, lib::ComponentKind::Button);
    basic->text = prop_->requiredMsgs().basicResist.value;
    basic->on_click = [this] { BasicResist(*this).widgetSelected(); };
    basic->set_layout_item({.horizontal = lib::Align::End,
                            .vertical = lib::Align::Center});
    setEditable(basic);
    mod(*basic);
    resist_panel_->add(basic);
}

void CastCardDialog::buildPhysical(const std::shared_ptr<lib::Container>& parent) {
    if (physical_panel_) physical_panel_->clear();
    else physical_panel_ = makePanel(context_.services, parent);
    aptitude_physical_.reset();
    auto page_layout = std::make_shared<lib::GridLayout>();
    page_layout->columns = prop_->var().showAptitudeOnCastCardEditor.value ? 2U : 1U;
    page_layout->spacing = 8.0F;
    page_layout->padding = {6.0F, 6.0F, 6.0F, 6.0F};
    physical_panel_->set_layout(page_layout);
    physical_panel_->set_layout_item({.horizontal = lib::Align::Stretch,
                                      .vertical = lib::Align::Stretch,
                                      .minimum = {470.0F, 315.0F}, .grow = 1.0F});

    auto left = makeComponent<lib::Panel>(context_.services,
                                          lib::ComponentKind::Panel);
    auto left_layout = std::make_shared<lib::GridLayout>();
    left_layout->columns = 2U;
    left_layout->spacing = 5.0F;
    left->set_layout(left_layout);
    left->set_layout_item({.row = 0U, .column = 0U,
                           .horizontal = lib::Align::Stretch,
                           .vertical = lib::Align::Stretch,
                           .minimum = {382.0F, 295.0F}, .grow = 1.0F});
    physical_panel_->add(left);
    physical_parent_ = makePanel(context_.services, left,
                                 prop_->requiredMsgs().physicalParams.value);
    physical_parent_->set_layout(std::make_shared<lib::FillLayout>());
    physical_parent_->set_layout_item({.row = 0U, .column = 0U,
                                       .column_span = 2U,
                                       .horizontal = lib::Align::Stretch,
                                       .vertical = lib::Align::Stretch,
                                       .minimum = {300.0F, 245.0F}, .grow = 1.0F});
    initPhysical();
    physical_sum_ = makeComponent<lib::Label>(context_.services, lib::ComponentKind::Label);
    physical_sum_->text = prop_->requiredMsgs().physicalSum.value;
    physical_sum_->set_layout_item({.row = 1U, .column = 0U,
                                    .horizontal = lib::Align::Start,
                                    .vertical = lib::Align::Center});
    left->add(physical_sum_);
    auto calc = makeComponent<lib::Button>(context_.services, lib::ComponentKind::Button);
    calc->text = prop_->requiredMsgs().physicalCalc.value;
    calc->on_click = [this] { CalcPhysical(*this).widgetSelected(); };
    calc->set_layout_item({.row = 1U, .column = 1U,
                           .horizontal = lib::Align::End,
                           .vertical = lib::Align::Center});
    setEditable(calc);
    mod(*calc);
    left->add(calc);
    if (prop_->var().showAptitudeOnCastCardEditor.value) {
        aptitude_physical_ = makePanel(context_.services, physical_panel_,
                                       prop_->requiredMsgs().aptitude.value);
        auto aptitude_layout = std::make_shared<lib::GridLayout>();
        aptitude_layout->columns = 3U;
        aptitude_layout->spacing = 3.0F;
        aptitude_physical_->set_layout(aptitude_layout);
        aptitude_physical_->set_layout_item({.row = 0U, .column = 1U,
                                               .horizontal = lib::Align::Start,
                                               .vertical = lib::Align::Stretch,
                                               .minimum = {82.0F, 295.0F},
                                               .preferred = {82.0F, 295.0F}});
    }
}

void CastCardDialog::buildMental(const std::shared_ptr<lib::Container>& parent) {
    if (mental_panel_) mental_panel_->clear();
    else mental_panel_ = makePanel(context_.services, parent);
    aptitude_mental_.reset();
    auto page_layout = std::make_shared<lib::GridLayout>();
    page_layout->columns = prop_->var().showAptitudeOnCastCardEditor.value ? 2U : 1U;
    page_layout->spacing = 8.0F;
    page_layout->padding = {6.0F, 6.0F, 6.0F, 6.0F};
    mental_panel_->set_layout(page_layout);
    mental_panel_->set_layout_item({.horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Stretch,
                                    .minimum = {470.0F, 315.0F}, .grow = 1.0F});

    auto left = makeComponent<lib::Panel>(context_.services,
                                          lib::ComponentKind::Panel);
    auto left_layout = std::make_shared<lib::LinearLayout>(
        lib::Orientation::Vertical);
    left_layout->spacing = 5.0F;
    left->set_layout(left_layout);
    left->set_layout_item({.row = 0U, .column = 0U,
                           .horizontal = lib::Align::Stretch,
                           .vertical = lib::Align::Stretch,
                           .minimum = {382.0F, 295.0F}, .grow = 1.0F});
    mental_panel_->add(left);

    auto group = makePanel(context_.services, left,
                           prop_->requiredMsgs().mentalParams.value);
    auto mental_layout = std::make_shared<lib::GridLayout>();
    mental_layout->columns = 3U;
    mental_layout->spacing = 4.0F;
    group->set_layout(mental_layout);
    group->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .minimum = {300.0F, 245.0F}, .grow = 1.0F});
    const auto max = static_cast<double>(prop_->var().mentalMax.value * 2U);
    for (const auto mental : mentalPositives()) {
        auto min_label = makeControl<lib::Label>(lib::ComponentKind::Label);
        min_label->text = mentalName(prop_->requiredMsgs(), oppositeMental(mental));
        min_label->set_layout_item({.horizontal = lib::Align::End,
                                    .vertical = lib::Align::Center,
                                    .minimum = {44.0F, 47.0F},
                                    .preferred = {44.0F, 47.0F}});
        group->add(min_label);
        const auto control = makeSlider(group, 0.0, max,
                                        static_cast<double>(prop_->var().mentalMax.value));
        control->step = 1.0;
        control->page_step = static_cast<double>(prop_->var().mentalMax.value);
        control->tick_interval = control->page_step;
        control->set_layout_item({.horizontal = lib::Align::Stretch,
                                  .vertical = lib::Align::Center,
                                  .minimum = {190.0F, 47.0F}, .grow = 1.0F});
        control->on_changed = [this](double) { if (!ignoreMod()) applyEnabled(); updateAptitudes(); };
        mental_[mental] = control;
        auto max_label = makeControl<lib::Label>(lib::ComponentKind::Label);
        max_label->text = mentalName(prop_->requiredMsgs(), mental);
        max_label->set_layout_item({.horizontal = lib::Align::Start,
                                    .vertical = lib::Align::Center,
                                    .minimum = {44.0F, 47.0F},
                                    .preferred = {44.0F, 47.0F}});
        group->add(max_label);
    }
    auto calc = makeComponent<lib::Button>(context_.services, lib::ComponentKind::Button);
    calc->text = prop_->requiredMsgs().mentalCalc.value;
    calc->on_click = [this] { CalcMental(*this).widgetSelected(); };
    calc->set_layout_item({.horizontal = lib::Align::End,
                           .vertical = lib::Align::Center});
    setEditable(calc);
    mod(*calc);
    left->add(calc);
    if (prop_->var().showAptitudeOnCastCardEditor.value) {
        aptitude_mental_ = makePanel(context_.services, mental_panel_,
                                     prop_->requiredMsgs().aptitude.value);
        auto aptitude_layout = std::make_shared<lib::GridLayout>();
        aptitude_layout->columns = 3U;
        aptitude_layout->spacing = 3.0F;
        aptitude_mental_->set_layout(aptitude_layout);
        aptitude_mental_->set_layout_item({.row = 0U, .column = 1U,
                                             .horizontal = lib::Align::Start,
                                             .vertical = lib::Align::Stretch,
                                             .minimum = {82.0F, 295.0F},
                                             .preferred = {82.0F, 295.0F}});
    }
}

void CastCardDialog::buildEnhance(const std::shared_ptr<lib::Container>& parent) {
    enhance_panel_ = makePanel(context_.services, parent);
    auto page_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    page_layout->spacing = 6.0F;
    page_layout->padding = {6.0F, 6.0F, 6.0F, 6.0F};
    enhance_panel_->set_layout(page_layout);
    enhance_panel_->set_layout_item({.horizontal = lib::Align::Stretch,
                                     .vertical = lib::Align::Stretch,
                                     .minimum = {470.0F, 315.0F}, .grow = 1.0F});
    enhance_parent_ = makePanel(context_.services, enhance_panel_,
                                prop_->requiredMsgs().castEnhance.value);
    enhance_parent_->set_layout(std::make_shared<lib::FillLayout>());
    enhance_parent_->set_layout_item({.horizontal = lib::Align::Stretch,
                                      .vertical = lib::Align::Stretch,
                                      .minimum = {450.0F, 260.0F}, .grow = 1.0F});
    initEnhance();
    auto basic = makeComponent<lib::Button>(context_.services, lib::ComponentKind::Button);
    basic->text = prop_->requiredMsgs().basicEnhance.value;
    basic->on_click = [this] { BasicEnhance(*this).widgetSelected(); };
    basic->set_layout_item({.horizontal = lib::Align::End,
                            .vertical = lib::Align::Center});
    setEditable(basic);
    mod(*basic);
    enhance_panel_->add(basic);
}

void CastCardDialog::buildStatus(const std::shared_ptr<lib::Container>& parent) {
    status_panel_ = makePanel(context_.services, parent);
    auto status_layout = std::make_shared<lib::LinearLayout>(
        lib::Orientation::Vertical);
    status_layout->spacing = 5.0F;
    status_layout->padding = {6.0F, 6.0F, 6.0F, 6.0F};
    status_panel_->set_layout(status_layout);
    status_panel_->set_layout_item({.horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Stretch,
                                    .minimum = {460.0F, 335.0F}, .grow = 1.0F});

    const auto makeStatusGroup = [this](std::string title, const float height) {
        auto group = makePanel(context_.services, status_panel_, std::move(title));
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = 2U;
        layout->spacing = static_cast<float>(
            std::max(5, prop_->var().radioGroupSeparatorWidth.value));
        group->set_layout(layout);
        group->set_layout_item({.horizontal = lib::Align::Stretch,
                                .minimum = {445.0F, height},
                                .preferred = {445.0F, height}});
        return group;
    };
    const auto makeRow = [this](const std::shared_ptr<lib::Container>& group,
                                const std::size_t columns = 4U) {
        auto row = makeComponent<lib::Panel>(context_.services,
                                             lib::ComponentKind::Panel);
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = columns;
        layout->spacing = 5.0F;
        row->set_layout(layout);
        row->set_layout_item({.horizontal = lib::Align::Stretch,
                              .minimum = {205.0F, 28.0F},
                              .preferred = {205.0F, 28.0F}, .grow = 1.0F});
        group->add(row);
        return row;
    };
    const auto addLabel = [this](const std::shared_ptr<lib::Container>& row,
                                 std::string text, const std::size_t column) {
        auto label = makeControl<lib::Label>(lib::ComponentKind::Label);
        label->text = std::move(text);
        label->set_layout_item({.row = 0U, .column = column,
                                .horizontal = lib::Align::Start,
                                .vertical = lib::Align::Center});
        row->add(label);
        return label;
    };

    auto life_group = makeStatusGroup(prop_->requiredMsgs().lifeAndMentality.value, 58.0F);
    auto life_row = makeRow(life_group, 3U);
    addLabel(life_row, prop_->requiredMsgs().life.value, 0U);
    life_ = makeNumber(life_row, 0.0,
                       static_cast<double>(prop_->var().lifeMax.value), 1.0);
    life_->set_layout_item({.row = 0U, .column = 1U,
                            .horizontal = lib::Align::Stretch});
    life_->on_changed = [this](double) { refreshWarning(); };
    mod(*life_);
    life_use_max_ = makeCheck(life_row, prop_->requiredMsgs().useMax.value);
    life_use_max_->set_layout_item({.row = 0U, .column = 2U,
                                    .horizontal = lib::Align::Start});
    life_use_max_->on_changed = [this](bool) { changeLifeUseMax(); refreshWarning(); };
    mod(*life_use_max_);

    auto mentality_row = makeRow(life_group, 5U);
    mentality_ = makeControl<lib::ComboBox>(lib::ComponentKind::ComboBox);
    for (const auto value : mentalities()) {
        mentality_table_.push_back(value);
        mentality_->items.push_back({mentalityName(prop_->requiredMsgs(), value), {}, {}, true});
    }
    mentality_->selected_index = 0U;
    mentality_->on_selection_changed = [this](std::optional<std::size_t>) {
        changeMentality();
        refreshWarning();
    };
    setEditable(mentality_);
    mod(*mentality_);
    addLabel(mentality_row, prop_->requiredMsgs().mentality.value, 0U);
    mentality_->set_layout_item({.row = 0U, .column = 1U,
                                 .column_span = 2U,
                                 .horizontal = lib::Align::Stretch, .grow = 1.0F});
    mentality_row->add(mentality_);
    mentality_round_ = makeNumber(mentality_row, 1.0,
                                   static_cast<double>(prop_->var().roundMax.value), 1.0);
    mentality_round_->set_layout_item({.row = 0U, .column = 3U,
                                       .horizontal = lib::Align::Stretch});
    mod(*mentality_round_);
    addLabel(mentality_row, prop_->requiredMsgs().unitRound.value, 4U);

    const auto enhance_max = static_cast<double>(prop_->var().enhanceMax.value);
    auto live_group = makeStatusGroup(prop_->requiredMsgs().enhanceLiveBonus.value, 88.0F);
    for (const auto enhance : enhancements()) {
        auto row = makeRow(live_group, 4U);
        addLabel(row, prop_->requiredMsgs().enhanceLiveBonusName(enhance), 0U);
        const auto value = makeNumber(row, -enhance_max, enhance_max, 0.0);
        value->set_layout_item({.row = 0U, .column = 1U,
                                .horizontal = lib::Align::Stretch});
        value->on_changed = [this](double) { changeLiveEnhance(); refreshWarning(); };
        mod(*value);
        live_enhance_[enhance] = value;
        const auto round = makeNumber(row, 1.0,
                                       static_cast<double>(prop_->var().roundMax.value), 1.0);
        round->set_layout_item({.row = 0U, .column = 2U,
                                .horizontal = lib::Align::Stretch});
        mod(*round);
        enhance_round_[enhance] = round;
        addLabel(row, prop_->requiredMsgs().unitRound.value, 3U);
    }

    auto state_group = makeStatusGroup(prop_->requiredMsgs().status.value, 118.0F);
    std::vector<std::shared_ptr<lib::Label>> state_name_labels;
    std::vector<std::shared_ptr<lib::Label>> state_unit_labels;
    const auto makeState = [&](std::string name, const double maximum,
                               std::string unit) {
        auto row = makeRow(state_group, 3U);
        state_name_labels.push_back(addLabel(row, std::move(name), 0U));
        auto value = makeNumber(row, 0.0, maximum, 0.0);
        value->set_layout_item({.row = 0U, .column = 1U,
                                .horizontal = lib::Align::Stretch});
        state_unit_labels.push_back(addLabel(row, std::move(unit), 2U));
        return value;
    };
    paralyze_ = makeState(prop_->requiredMsgs().paralyze.value,
                          static_cast<double>(prop_->var().paralyzeMax.value),
                          prop_->requiredMsgs().unitValue.value);
    poison_ = makeState(prop_->requiredMsgs().poison.value,
                        static_cast<double>(prop_->var().poisonMax.value),
                        prop_->requiredMsgs().unitValue.value);
    bind_ = makeState(prop_->requiredMsgs().bind.value,
                      static_cast<double>(prop_->var().roundMax.value),
                      prop_->requiredMsgs().unitRound.value);
    silence_ = makeState(prop_->requiredMsgs().silence.value,
                         static_cast<double>(prop_->var().roundMax.value),
                         prop_->requiredMsgs().unitRound.value);
    face_up_ = makeState(prop_->requiredMsgs().faceUp.value,
                         static_cast<double>(prop_->var().roundMax.value),
                         prop_->requiredMsgs().unitRound.value);
    anti_magic_ = makeState(prop_->requiredMsgs().antiMagic.value,
                             static_cast<double>(prop_->var().roundMax.value),
                             prop_->requiredMsgs().unitRound.value);
    const auto alignLabelColumn = [](const auto& labels,
                                     const std::size_t column) {
        float width = 0.0F;
        for (const auto& label : labels)
            width = std::max(width, lib::ui_text_width(label->text, 0.0F));
        for (const auto& label : labels) {
            label->set_layout_item({.row = 0U, .column = column,
                                    .horizontal = lib::Align::Start,
                                    .vertical = lib::Align::Center,
                                    .minimum = {width, 0.0F},
                                    .preferred = {width, 0.0F}});
        }
    };
    alignLabelColumn(state_name_labels, 0U);
    alignLabelColumn(state_unit_labels, 2U);
    for (const auto& control : {paralyze_, poison_, bind_, silence_, face_up_, anti_magic_}) {
        control->on_changed = [this](double) { refreshWarning(); };
        mod(*control);
    }
    auto reset = makeComponent<lib::Button>(context_.services, lib::ComponentKind::Button);
    reset->text = prop_->requiredMsgs().resetLiveStatus.value;
    reset->on_click = [this] { ResetLiveStatus(*this).widgetSelected(); };
    reset->set_layout_item({.horizontal = lib::Align::End,
                            .vertical = lib::Align::Center});
    setEditable(reset);
    mod(*reset);
    status_panel_->add(reset);
    changeLiveEnhance();
    changeMentality();
    changeLifeUseMax();
}

void CastCardDialog::constructBase(const std::shared_ptr<lib::Container>& parent) {
    buildBase(parent);
}

void CastCardDialog::constructDesc(const std::shared_ptr<lib::Container>& parent) {
    buildDescription(parent);
}

void CastCardDialog::constructHistory(const std::shared_ptr<lib::Container>& parent) {
    buildHistory(parent);
}

void CastCardDialog::constructMakings(const std::shared_ptr<lib::Container>& parent) {
    buildMakings(parent);
}

void CastCardDialog::constructResist(const std::shared_ptr<lib::Container>& parent) {
    buildResist(parent);
}

void CastCardDialog::constructPhysical(const std::shared_ptr<lib::Container>& parent) {
    buildPhysical(parent);
}

void CastCardDialog::constructMental(const std::shared_ptr<lib::Container>& parent) {
    buildMental(parent);
}

void CastCardDialog::constructEnhance(const std::shared_ptr<lib::Container>& parent) {
    buildEnhance(parent);
}

void CastCardDialog::constructStatus(const std::shared_ptr<lib::Container>& parent) {
    buildStatus(parent);
}

std::shared_ptr<lib::Container> CastCardDialog::makePanel(
    const lib::Services& services, const std::shared_ptr<lib::Container>& parent,
    std::string title) {
    std::shared_ptr<lib::Container> panel;
    if (title.empty()) {
        panel = makeComponent<lib::Panel>(services, lib::ComponentKind::Panel);
    } else {
        auto group = makeComponent<lib::GroupBox>(services,
                                                  lib::ComponentKind::GroupBox);
        group->title = std::move(title);
        panel = std::move(group);
    }
    auto panel_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    panel_layout->spacing = 5.0F;
    panel_layout->padding = {6.0F, 6.0F, 6.0F, 6.0F};
    panel->set_layout(panel_layout);
    panel->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .grow = 1.0F});
    if (parent) parent->add(panel);
    return panel;
}

template <typename T>
std::shared_ptr<T> CastCardDialog::makeControl(const lib::ComponentKind kind) const {
    return makeComponent<T>(context_.services, kind);
}

void CastCardDialog::setEditable(const std::shared_ptr<lib::Widget>& widget) const {
    if (const auto text = std::dynamic_pointer_cast<lib::TextEdit>(widget)) {
        text->read_only = read_only_;
        text->set_enabled(true);
    } else if (const auto number = std::dynamic_pointer_cast<lib::NumberEdit>(widget)) {
        number->read_only = read_only_;
        number->set_enabled(true);
        dutils::initSpinner(number);
    } else if (widget) widget->set_enabled(!read_only_);
}

std::shared_ptr<lib::Button> CastCardDialog::createR(
    const std::shared_ptr<lib::Container>& parent, std::string text,
    std::string group) {
    return makeRadio(parent, std::move(text), std::move(group));
}

std::shared_ptr<lib::Container> CastCardDialog::createButtonGroup(
    const std::shared_ptr<lib::Container>& parent, std::string title,
    const unsigned columns) {
    auto panel = makePanel(context_.services, parent, std::move(title));
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = std::max(1U, columns);
    panel->set_layout(layout);
    return panel;
}

std::vector<types::Physical> CastCardDialog::physicals() {
    return {types::Physical::Dex, types::Physical::Agl, types::Physical::Int,
            types::Physical::Str, types::Physical::Vit, types::Physical::Min};
}

std::vector<types::Mental> CastCardDialog::mentalPositives() {
    return {types::Mental::Aggressive, types::Mental::Cheerful,
            types::Mental::Brave, types::Mental::Cautious,
            types::Mental::Trickish};
}

std::vector<types::Enhance> CastCardDialog::enhancements() {
    return {types::Enhance::Action, types::Enhance::Avoid,
            types::Enhance::Resist, types::Enhance::Defense};
}

std::vector<types::Element> CastCardDialog::elements() {
    return {types::Element::Fire, types::Element::Ice};
}

std::vector<types::Mentality> CastCardDialog::mentalities() {
    return {types::Mentality::Normal, types::Mentality::Sleep,
            types::Mentality::Confuse, types::Mentality::Overheat,
            types::Mentality::Brave, types::Mentality::Panic};
}

lib::String CastCardDialog::format(lib::String value,
                                   const std::string_view first,
                                   const std::string_view second,
                                   const std::string_view third) {
    value = replaceFirst(std::move(value), "%1$s", first);
    value = replaceFirst(std::move(value), "%s", first);
    if (!second.empty()) value = replaceFirst(std::move(value), "%2$s", second);
    if (!third.empty()) value = replaceFirst(std::move(value), "%3$s", third);
    return value;
}

void CastCardDialog::setMaxLife() {
    if (!life_max_ || !life_) return;
    life_max_->value = std::clamp(life_max_->value, life_max_->minimum,
                                  life_max_->maximum);
    life_->maximum = life_max_->value;
    if (life_use_max_ && life_use_max_->checked) life_->value = life_max_->value;
    if (life_->value > life_->maximum) life_->value = life_->maximum;
}

void CastCardDialog::refDataVersion() {
    const std::weak_ptr<void> life = cast_lifetime_;
    const auto revision = cast_revision_;
    if (race_) race_->set_enabled(!read_only_ && (!summ_ || !summ_->legacy()));
    refreshWarning();
    if (life.expired() || cast_revision_ != revision) return;
    if (const auto callback = context_.refresh_data_version) callback();
}

void CastCardDialog::refreshSex() {
    const auto* value = summSkin();
    if (!value) return;
    for (const auto& entry : sex_) {
        const auto name = value->sexName(entry.first);
        entry.second->text = prop_->requiredMsgs().sex.get(name, name);
    }
}

void CastCardDialog::refreshPeriod() {
    const auto* value = summSkin();
    if (!value) return;
    for (const auto& entry : period_) {
        const auto name = value->periodName(entry.first);
        entry.second->text = prop_->requiredMsgs().period.get(name, name);
    }
}

void CastCardDialog::refreshNature() {
    const auto* value = summSkin();
    if (!value) return;
    for (const auto& entry : nature_) {
        const auto name = value->natureName(entry.first);
        entry.second->text = prop_->requiredMsgs().nature.get(name, name);
    }
}

void CastCardDialog::refreshMakings() {
    const auto* value = summSkin();
    if (!value) return;
    for (const auto& entry : makings_) {
        const auto name = value->makingsName(entry.first);
        entry.second->text = prop_->requiredMsgs().makings.get(name, name);
    }
}

void CastCardDialog::updateNature() {
    if (!nature_parent_) return;
    const auto* value = summSkin();
    if (!value) return;
    std::string selected_coupon;
    for (const auto& entry : nature_) {
        if (entry.second->checked) {
            selected_coupon = value->natureCoupon(entry.first);
            break;
        }
    }
    if (nature_unknown_ && nature_unknown_->checked) selected_coupon.clear();
    const bool displayChanged = !nature_.empty() && show_sp_nature_ != prop_->var().showSpNature.value;
    nature_parent_->clear();
    nature_.clear();
    const auto parent = std::shared_ptr<lib::Container>(nature_parent_.get(),
                                                        [](lib::Container*) {});
    bool selected = false;
    // DのSWT GridLayoutは、複数列をまた
    // ぐ区切りを専用の行へ進める。C++ではcolumn_span
    // の指定で明示配置にもなるため、行を既定0にする
    // と全区切りが最初の性格のラジオボタンを横切る
    // 。右列の「不明」も含めDの二列配置を明示し、
    // 動的グループの再構築で重ならないようにする。
    std::size_t grid_row = 0U;
    std::size_t grid_column = 0U;
    const auto addSeparator = [this, parent, &grid_row, &grid_column] {
        if (grid_column != 0U) {
            ++grid_row;
            grid_column = 0U;
        }
        auto separator = makeControl<lib::Separator>(lib::ComponentKind::Separator);
        separator->orientation = lib::Orientation::Horizontal;
        separator->set_layout_item({.row = grid_row, .column = 0U,
                                    .column_span = 2U,
                                    .horizontal = lib::Align::Stretch});
        parent->add(separator);
        ++grid_row;
    };
    const auto addNature = [this, parent, value, &selected, &selected_coupon,
                            &grid_row, &grid_column](
                               const features::Nature feature) {
        const auto name = value->natureName(feature);
        const auto control = makeRadio(
            parent, prop_->requiredMsgs().nature.get(name, name), "nature");
        control->set_layout_item({.row = grid_row, .column = grid_column});
        if (++grid_column == 2U) {
            grid_column = 0U;
            ++grid_row;
        }
        control->on_changed = [this](bool checked) {
            if (checked && nature_unknown_) nature_unknown_->checked = false;
            refreshWarning();
        };
        mod(*control);
        if (!selected_coupon.empty() && value->natureCoupon(feature) == selected_coupon) {
            control->checked = true;
            selected = true;
        }
        nature_[feature] = control;
    };
    for (const auto feature : value->normalNatures()) addNature(feature);
    show_sp_nature_ = prop_->var().showSpNature.value;
    if (show_sp_nature_) {
        addSeparator();
        for (const auto feature : value->extraNatures()) addNature(feature);
    }
    addSeparator();
    nature_unknown_ = makeRadio(parent, prop_->requiredMsgs().natureUnknown.value, "nature");
    nature_unknown_->set_layout_item({.row = grid_row, .column = 1U});
    nature_unknown_->on_changed = [this](bool checked) {
        if (!checked) return;
        for (const auto& entry : nature_) entry.second->checked = false;
        refreshWarning();
    };
    mod(*nature_unknown_);
    if (!selected) nature_unknown_->checked = true;
    if (displayChanged) {
        auto coupons = full_coupon_view_ ? full_coupon_view_->coupons() : coupon_view_.coupons();
        bool changed = false;
        std::optional<std::size_t> removedIndex;
        CouponPtr addedCoupon;
        if (show_sp_nature_ && nature_unknown_->checked) {
            for (auto it = coupons.begin(); it != coupons.end() && !changed; ++it) {
                if (!*it || (*it)->value() != 0) continue;
                for (const auto feature : value->extraNatures()) {
                    if ((*it)->name() != value->natureCoupon(feature)) continue;
                    nature_.at(feature)->checked = true;
                    nature_unknown_->checked = false;
                    removedIndex = static_cast<std::size_t>(it - coupons.begin());
                    coupons.erase(it);
                    changed = true;
                    break;
                }
                if (changed) break; // eraseで無効になったため、イテレーターを進めない。
            }
        } else if (!show_sp_nature_ && nature_unknown_->checked && !selected_coupon.empty()) {
            addedCoupon = std::make_shared<Coupon>(selected_coupon, 0);
            coupons.push_back(addedCoupon);
            changed = true;
        }
        if (changed) {
            const std::weak_ptr<void> life = cast_lifetime_;
            const bool ignoring = ignoreMod();
            ignoreMod(true);
            struct RestoreIgnore {
                CastCardDialog* owner; std::weak_ptr<void> life; bool value;
                ~RestoreIgnore() { if (!life.expired()) owner->ignoreMod(value); }
            } restore{this, life, ignoring};
            const auto view = full_coupon_view_;
            if (view) {
                // DのupdateNature:1584-1608は、追加・削除した
                // 特殊な性格クーポン一つだけを記録し、以前のクーポン履歴を保持する。
                if (removedIndex) view->delCoupon(std::vector<std::size_t>{*removedIndex});
                else if (addedCoupon) view->addCoupon(addedCoupon);
            } else coupon_view_.coupons(std::move(coupons));
        }
    }
}

void CastCardDialog::refSkin() {
    ++cast_revision_;
    const std::weak_ptr<void> life = cast_lifetime_;
    const auto revision = cast_revision_;
    const auto current = [&] { return !life.expired() && cast_revision_ == revision; };
    const bool was_ignoring = ignoreMod();
    ignoreMod(true);
    struct RestoreIgnoreMod {
        lib::Action restore;
        ~RestoreIgnoreMod() { restore(); }
    } restore{[this, life, was_ignoring] {
        // DのGC受信先と異なり、共通サービスはこのダイアログを同期的に終
        // 了させ得る。所有元が生存している場合だけ、一時状態を復元する。
        if (!life.expired()) ignoreMod(was_ignoring);
    }};
    if (notification_commons_)
        context_.common.skin = notification_commons_->skin().get();
    // インデックス0は表示専用の種族なしの番兵であり、種族名ではない。
    const auto old_race = race_ && race_->selected_index &&
                                  *race_->selected_index > 0U &&
                                  *race_->selected_index < race_->items.size()
        ? selectedComboText(race_) : std::string{};
    if (summary_skin_pinned_) {
        // DはrefSkin通知をまたいで、構築時に解決した入力元スキンを保持する。
        if (!source_skin_owner_) summ_skin_ = context_.common.skin;
    } else if (!resolveSourceSkin()) {
        return;
    }
    if (const auto selector = image_selector_) {
        const auto size = prop_->looks().cardSize();
        selector->previewSize(prop_->s(static_cast<std::int32_t>(size.width)),
            prop_->s(static_cast<std::int32_t>(size.height)), prop_->s(prop_->looks().castCardInsets()));
        if (!current()) return;
        selector->images(selector->images());
        if (!current()) return;
    }
    refreshDescriptionFont();
    if (race_) {
        const bool was_ignoring = ignoreMod();
        ignoreMod(true);
        std::vector<lib::ChoiceItem> items;
        items.push_back({prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().noRace.value),
                         {}, {}, true});
        if (const auto* value = summSkin()) {
            for (const auto& entry : value->races()) {
                if (entry) items.push_back({entry->name(), entry->name(), {}, true});
            }
        }
        std::size_t selected = 0U;
        for (std::size_t i = 1; i < items.size(); ++i) {
            if (items[i].text == old_race) {
                selected = i;
                break;
            }
        }
        (void)lib::replace_combo_items(race_, std::move(items), selected);
        const auto old_race_coupon = old_race.empty()
            ? std::string{} : prop_->sys().raceCoupon(old_race);
        if (full_coupon_view_ || coupon_view_.widget()) {
            std::map<std::string, std::size_t> race_coupons;
            if (const auto* value = summSkin()) {
                for (std::size_t i = 0; i < value->races().size(); ++i) {
                    const auto& entry = value->races()[i];
                    if (entry)
                        race_coupons[prop_->sys().raceCoupon(entry->name())] = i;
                }
            }
            auto source = full_coupon_view_ ? full_coupon_view_->coupons()
                                            : coupon_view_.coupons();
            CouponArray history;
            history.reserve(source.size() + 1U);
            // Dの双方向変換を維持する。有効な種
            // 族になったクーポンはコンボへ移し
            // 、以前の選択種族は履歴に未登録な
            // ら値0の履歴クーポンとして残す。
            bool found_old = old_race.empty();
            for (const auto& coupon : source) {
                if (!coupon) continue;
                const auto found = race_coupons.find(coupon->name());
                if (found != race_coupons.end()) {
                    (void)lib::select_combo_item(*race_, found->second + 1U, false);
                } else {
                    history.push_back(coupon);
                }
                if (coupon->name() == old_race_coupon) found_old = true;
            }
            if (!found_old)
                history.push_back(std::make_shared<Coupon>(old_race_coupon, 0));
            if (full_coupon_view_) full_coupon_view_->coupons(history);
            else coupon_view_.coupons(std::move(history));
        }
        ignoreMod(was_ignoring);
        raceToolTip();
    }
    refreshSex();
    refreshPeriod();
    updateNature();
    refreshMakings();
    if (resist_panel_) {
        std::vector<bool> base;
        for (const auto& check : {resist_weapon_, resist_magic_, undead_, automaton_, unholy_, constructure_})
            base.push_back(check && check->checked);
        std::map<types::Element, bool> resistance, weakness;
        for (const auto& [element, check] : resist_) resistance[element] = check->checked;
        for (const auto& [element, check] : weakness_) weakness[element] = check->checked;
        buildResist({});
        std::size_t index = 0;
        for (const auto& check : {resist_weapon_, resist_magic_, undead_, automaton_, unholy_, constructure_})
            check->checked = base[index++];
        for (const auto& [element, check] : resist_) check->checked = resistance[element];
        for (const auto& [element, check] : weakness_) check->checked = weakness[element];
    }
    refCastCardParameterEditStyle();
    refreshWarning();
    if (life.expired() || cast_revision_ != revision) return;
    if (const auto callback = context_.refresh_skin) callback();
}

void CastCardDialog::refScenario(const Summary* summ) {
    // C++は通常の編集でもこの通知を出す。元の資源が
    // 同じシナリオに属する間は、編集中の値を保持する。
    if (summ == summ_ && (!card_ || !summ || summ->cwCast(card_->id()) == card_)) return;
    forceCancel();
}

void CastCardDialog::delCard(const CastCardPtr& value) {
    if (card_ && card_ == value) forceCancel();
}

void CastCardDialog::updateTitle(const CastCard* value) {
    if (!value || !card_ || value != card_.get()) return;
    title(dialogTitle(*prop_, summ_, card_));
}

void CastCardDialog::refCastCardParameterEditStyle() {
    const bool ignoring = ignoreMod();
    ignoreMod(true);
    struct Restore {
        CastCardDialog* owner;
        std::weak_ptr<void> life;
        bool previous;
        ~Restore() { if (!life.expired()) owner->ignoreMod(previous); }
    } restore{this, cast_lifetime_, ignoring};
    // タブページのオブジェクトは維持し、内容とグリッドの列を再構築する。initPhysical
    // が読み取るまで、旧レーダー・スケールが値を保持する。
    if (physical_panel_) buildPhysical({});
    auto mentalValues = mental_;
    if (mental_panel_) {
        buildMental({});
        for (const auto& [key, oldControl] : mentalValues)
            if (mental_.contains(key) && oldControl) mental_.at(key)->value = oldControl->value;
    }
    initEnhance();
    createAptitudes();
    updateAptitudes();
}

void CastCardDialog::initPhysical() {
    if (!physical_parent_) return;

    std::vector<int> values(physicals().size(), prop_->looks().physicalNormal());
    if (physical_radar_) values = physical_radar_->getValues();
    if (physical_scales_) values = physical_scales_->getValues();
    values.resize(physicals().size(), prop_->looks().physicalNormal());

    physical_radar_.reset();
    physical_scales_.reset();
    physical_indices_.clear();
    physical_parent_->clear();

    std::vector<lib::String> names;
    names.reserve(physicals().size());
    for (std::size_t index = 0; index < physicals().size(); ++index) {
        const auto physical = physicals()[index];
        physical_indices_[physical] = index;
        names.push_back(physicalName(prop_->requiredMsgs(), physical));
    }
    const auto maximum = prop_->var().physicalMax.value;
    const auto page = std::max(1U, maximum / 5U);
    std::vector<int> borders;
    borders.reserve(prop_->looks().physicalBorders().size());
    for (const auto value : prop_->looks().physicalBorders())
        borders.push_back(static_cast<int>(value));

    if (prop_->var().radarStyleParams.value) {
        physical_radar_ = std::make_shared<radarspinner::RadarSpinner>(
            physical_parent_, read_only_ ? radarspinner::RadarSpinner::ReadOnly
                                         : radarspinner::RadarSpinner::None, prop_->images().services().gui);
        physical_radar_->setRadar(maximum + 1U, names, 0);
        physical_radar_->antialias(1);
        physical_radar_->borderlines(borders);
        physical_radar_->lineStep(page);
        physical_radar_->setValues(values);
        physical_radar_->modEvent.push_back([this] { modPhysical(); });
        physical_radar_->set_layout_item({.horizontal = lib::Align::Stretch,
                                          .vertical = lib::Align::Stretch,
                                          .minimum = {285.0F, 225.0F},
                                          .grow = 1.0F});
        physical_parent_->add(physical_radar_);
    } else {
        physical_scales_ = std::make_shared<scales::Scales>(
            physical_parent_.get(),
            read_only_ ? static_cast<std::int32_t>(scales::STYLE_READ_ONLY)
                       : static_cast<std::int32_t>(scales::STYLE_NONE),
            context_.services.system.get(), context_.services.graphics.get());
        physical_scales_->setScales(maximum + 1U, names,
                                    static_cast<std::int32_t>(page), 0);
        physical_scales_->borderlines(borders);
        physical_scales_->setValues(values);
        physical_scales_->modEvent.push_back([this] { modPhysical(); });
        physical_scales_->set_layout_item({.horizontal = lib::Align::Stretch,
                                           .vertical = lib::Align::Stretch,
                                           .minimum = {285.0F, 225.0F},
                                           .grow = 1.0F});
        physical_parent_->add(physical_scales_);
    }
}

void CastCardDialog::updatePhysical() {
    modPhysical(true);
}

void CastCardDialog::constructPhysicalTab() {
    updatePhysical();
}

void CastCardDialog::initEnhance() {
    if (!enhance_parent_) return;

    static constexpr std::array enhance_values{
        types::Enhance::Avoid, types::Enhance::Resist, types::Enhance::Defense};
    std::vector<int> values(enhance_values.size(), 0);
    if (enhance_radar_) values = enhance_radar_->getValues();
    if (enhance_scales_) values = enhance_scales_->getValues();
    values.resize(enhance_values.size(), 0);

    enhance_radar_.reset();
    enhance_scales_.reset();
    enhance_indices_.clear();
    enhance_parent_->clear();

    std::vector<lib::String> names;
    names.reserve(enhance_values.size());
    for (std::size_t index = 0; index < enhance_values.size(); ++index) {
        const auto enhance = enhance_values[index];
        enhance_indices_[enhance] = index;
        names.push_back(format(prop_->requiredMsgs().enhanceBonus.value,
                               prop_->requiredMsgs().enhanceName(enhance)));
    }
    const auto maximum = prop_->var().enhanceMax.value;
    const auto steps = maximum * 2U + 1U;
    const auto minimum = -static_cast<int>(maximum);
    const auto page = std::max(1U, maximum / 2U);
    if (prop_->var().radarStyleParams.value) {
        enhance_radar_ = std::make_shared<radarspinner::RadarSpinner>(
            enhance_parent_, read_only_ ? radarspinner::RadarSpinner::ReadOnly
                                        : radarspinner::RadarSpinner::None, prop_->images().services().gui);
        enhance_radar_->setRadar(steps, names, minimum);
        enhance_radar_->antialias(1);
        enhance_radar_->borderlines({0});
        enhance_radar_->lineStep(page);
        enhance_radar_->setValues(values);
        enhance_radar_->modEvent.push_back([this] { if (!ignoreMod()) applyEnabled(); refreshWarning(); });
        enhance_radar_->set_layout_item({.horizontal = lib::Align::Stretch,
                                         .vertical = lib::Align::Stretch,
                                         .minimum = {430.0F, 245.0F},
                                         .grow = 1.0F});
        enhance_parent_->add(enhance_radar_);
    } else {
        enhance_scales_ = std::make_shared<scales::Scales>(
            enhance_parent_.get(),
            read_only_ ? static_cast<std::int32_t>(scales::STYLE_READ_ONLY)
                       : static_cast<std::int32_t>(scales::STYLE_NONE),
            context_.services.system.get(), context_.services.graphics.get());
        // Dは小さい最大値に0を渡す。Scalesはその値でSWTの既
        // 定のページ増分10を維持する。レーダーとALLは変えない。
        const auto scale_page = lib::editor_window_host_options().all_mode
            ? page : maximum / 2U;
        enhance_scales_->setScales(steps, names, static_cast<std::int32_t>(scale_page),
                                   minimum);
        enhance_scales_->borderlines({0});
        enhance_scales_->setValues(values);
        enhance_scales_->modEvent.push_back([this] { if (!ignoreMod()) applyEnabled(); refreshWarning(); });
        enhance_scales_->set_layout_item({.horizontal = lib::Align::Stretch,
                                          .vertical = lib::Align::Stretch,
                                          .minimum = {430.0F, 245.0F},
                                          .grow = 1.0F});
        enhance_parent_->add(enhance_scales_);
    }
}

void CastCardDialog::createAptitudes() {
    if (aptitude_physical_) aptitude_physical_->clear();
    if (aptitude_mental_) aptitude_mental_->clear();
    aptitude_displays_.clear();
    if (context_.services.graphics) {
        for (auto& image : aptitude_mark_images_) {
            if (image != 0U) context_.services.graphics->destroy_image(image);
            image = 0U;
        }
    }
    if (!prop_->var().showAptitudeOnCastCardEditor.value || !summSkin()) return;

    const auto install_mark = [this](const dskin::ImageDataWithScale& data,
                                     const std::size_t grade) {
        if (data.resource != 0U) return data.resource;
        if (!context_.services.graphics)
            return lib::ResourceId{};
        if (!data.data.frames.empty()) {
            auto image = data.data;
            const auto sourceScale = static_cast<float>(std::max(1U, data.source_scale));
            // Dは選択した高解像度スキン画像を正規化してから表示DPI
            // を適用する。ここでは画素バッファ全体を保持し、共通ImageView
            // に論理寸法を渡して、描画側でDPIを一度適用す
            // る。素材倍率はEditorの拡大率ではない。
            for (auto& frame : image.frames) {
                frame.logical_size = {
                    static_cast<float>(frame.pixel_width) / sourceScale,
                    static_cast<float>(frame.pixel_height) / sourceScale};
            }
            return context_.services.graphics->create_image(image);
        }
        return context_.services.graphics->create_image(fallbackAptitudeMark(grade));
    };
    const auto scale = prop_->drawingScale();
    aptitude_mark_images_[0] = install_mark(dskin::aptVeryHigh(*summSkin(), scale), 0U);
    aptitude_mark_images_[1] = install_mark(dskin::aptHigh(*summSkin(), scale), 1U);
    aptitude_mark_images_[2] = install_mark(dskin::aptNormal(*summSkin(), scale), 2U);
    aptitude_mark_images_[3] = install_mark(dskin::aptLow(*summSkin(), scale), 3U);

    const auto populate = [this](const std::shared_ptr<lib::Container>& panel) {
        if (!panel) return;
        std::size_t row = 0U;
        const auto addValue = [this, &panel, &row](std::string text,
                                                   std::string description,
                                                   const skin::Aptitude aptitude,
                                                   const bool action,
                                                   const lib::ResourceId image) {
            std::shared_ptr<lib::Widget> first;
            if (action && image != 0U) {
                auto icon = makeControl<lib::ImageView>(lib::ComponentKind::ImageView);
                icon->image = image;
                icon->scale_mode = lib::ImageScaleMode::None;
                // Dは適性アイコンに装飾なしのSWT Labelを使う
                // 。カードタブのくぼんだ画像選択プレビューは使わない。
                icon->border = false;
                icon->set_layout_item({.row = row, .column = 0U,
                                       .horizontal = lib::Align::Center,
                                       .vertical = lib::Align::Center,
                                       .minimum = {16.0F, 16.0F},
                                       .preferred = {16.0F, 16.0F}});
                first = icon;
            } else {
                auto label = makeControl<lib::Label>(lib::ComponentKind::Label);
                label->text = std::move(text);
                label->set_layout_item({.row = row, .column = 0U,
                                        .column_span = 2U,
                                        .horizontal = lib::Align::Start,
                                        .vertical = lib::Align::Center});
                first = label;
            }
            first->set_tooltip(format(prop_->requiredMsgs().aptitudeHint.value,
                                      description,
                                      physicalName(prop_->requiredMsgs(), aptitude.physical),
                                      mentalName(prop_->requiredMsgs(), aptitude.mental)));
            panel->add(first);

            std::shared_ptr<lib::ImageView> mark;
            if (action) {
                mark = makeControl<lib::ImageView>(lib::ComponentKind::ImageView);
                mark->scale_mode = lib::ImageScaleMode::None;
                mark->border = false;
                mark->set_layout_item({.row = row, .column = 1U,
                                       .horizontal = lib::Align::Center,
                                       .vertical = lib::Align::Center,
                                       .minimum = {16.0F, 16.0F},
                                       .preferred = {16.0F, 16.0F}});
                panel->add(mark);
            }
            auto value = makeControl<lib::Label>(lib::ComponentKind::Label);
            value->set_layout_item({.row = row, .column = 2U,
                                    .horizontal = lib::Align::End,
                                    .vertical = lib::Align::Center,
                                    .minimum = {20.0F, 16.0F}});
            panel->add(value);
            aptitude_displays_.push_back({aptitude, std::move(mark), std::move(value)});
            ++row;
        };

        for (const auto type : summSkin()->actionCardTypes()) {
            const auto name = summSkin()->actionCardName(prop_->sys(), type);
            addValue(name, name, summSkin()->actionCardAptitude(prop_->sys(), type),
                     true, context_.action_card_image
                               ? context_.action_card_image(type)
                               : lib::ResourceId{});
        }
        auto separator = makeControl<lib::Separator>(lib::ComponentKind::Separator);
        separator->orientation = lib::Orientation::Horizontal;
        separator->set_layout_item({.row = row++, .column = 0U, .column_span = 3U,
                                    .horizontal = lib::Align::Stretch});
        panel->add(separator);

        addValue(prop_->requiredMsgs().actionOrder.value, prop_->requiredMsgs().actionOrderDesc.value,
                 prop_->sys().actionOrderAptitude(), false, {});
        addValue(prop_->requiredMsgs().resilience.value, prop_->requiredMsgs().resilienceDesc.value,
                 prop_->sys().resilienceAptitude(), false, {});
        addValue(prop_->requiredMsgs().resistance.value, prop_->requiredMsgs().resistanceDesc.value,
                 prop_->sys().resistanceAptitude(), false, {});
        addValue(prop_->requiredMsgs().avoidance.value, prop_->requiredMsgs().avoidanceDesc.value,
                 prop_->sys().avoidanceAptitude(), false, {});
        addValue(prop_->requiredMsgs().runAwaySpeed.value, prop_->requiredMsgs().runAwaySpeedDesc.value,
                 prop_->sys().runAwaySpeedAptitude(), false, {});
    };
    populate(aptitude_physical_);
    populate(aptitude_mental_);
}

void CastCardDialog::updateAptitudes() {
    if (!prop_->var().showAptitudeOnCastCardEditor.value) return;
    for (const auto& display : aptitude_displays_)
        updateAptitude(display.aptitude.physical, display.aptitude.mental);
}

void CastCardDialog::updateAptitude(const types::Physical physical,
                                    const types::Mental mental) {
    const auto value = physicalValue(physical) + mentalValue(mental);
    for (auto& display : aptitude_displays_) {
        if (display.aptitude.physical != physical ||
            display.aptitude.mental != mental)
            continue;
        if (display.value) display.value->text = std::to_string(value);
        if (display.mark) {
            std::size_t image = 3U;
            if (value >= prop_->looks().aptVeryHigh()) image = 0U;
            else if (value >= prop_->looks().aptHigh()) image = 1U;
            else if (value >= prop_->looks().aptNormal()) image = 2U;
            display.mark->image = aptitude_mark_images_[image];
            // 従来の単一画像のNone経路は実画素の寸法を使う。こ
            // の論理サイズ対応の層では、高解像度バッファを保持しつ
            // つ、16単位の適性の印を32・64単位へ拡大しない。
            display.mark->layers.clear();
            if (display.mark->image != 0U)
                display.mark->layers.push_back({.image = display.mark->image,
                                               .centered = false});
            if (context_.services.gui)
                context_.services.gui->request_redraw(display.mark);
        }
    }
}

int CastCardDialog::physicalValue(const types::Physical physical) const {
    const auto found = physical_indices_.find(physical);
    if (found == physical_indices_.end()) return 0;
    if (physical_radar_) return physical_radar_->getValue(found->second);
    if (physical_scales_) return physical_scales_->getValue(found->second);
    return 0;
}

void CastCardDialog::setPhysicalValue(const types::Physical physical,
                                      const int value) {
    const auto found = physical_indices_.find(physical);
    if (found == physical_indices_.end()) return;
    if (physical_radar_) physical_radar_->setValue(found->second, value);
    if (physical_scales_) physical_scales_->setValue(found->second, value);
}

int CastCardDialog::defaultEnhanceValue(const types::Enhance enhance) const {
    const auto found = enhance_indices_.find(enhance);
    if (found == enhance_indices_.end()) return 0;
    if (enhance_radar_) return enhance_radar_->getValue(found->second);
    if (enhance_scales_) return enhance_scales_->getValue(found->second);
    return 0;
}

void CastCardDialog::setDefaultEnhanceValue(const types::Enhance enhance,
                                            const int value) {
    const auto found = enhance_indices_.find(enhance);
    if (found == enhance_indices_.end()) return;
    if (enhance_radar_) enhance_radar_->setValue(found->second, value);
    if (enhance_scales_) enhance_scales_->setValue(found->second, value);
}

int CastCardDialog::mentalValue(const types::Mental mental) const {
    const auto positive = [&]() {
        switch (mental) {
        case types::Mental::Unaggressive: return types::Mental::Aggressive;
        case types::Mental::Uncheerful: return types::Mental::Cheerful;
        case types::Mental::Unbrave: return types::Mental::Brave;
        case types::Mental::Uncautious: return types::Mental::Cautious;
        case types::Mental::Untrickish: return types::Mental::Trickish;
        default: return mental;
        }
    }();
    const auto found = mental_.find(positive);
    if (found == mental_.end()) return 0;
    const auto slider = static_cast<int>(std::lround(found->second->value));
    const auto centered = slider - static_cast<int>(prop_->var().mentalMax.value);
    const bool negative = mental == types::Mental::Unaggressive ||
                          mental == types::Mental::Uncheerful ||
                          mental == types::Mental::Unbrave ||
                          mental == types::Mental::Uncautious ||
                          mental == types::Mental::Untrickish;
    return negative ? -centered : centered;
}

double CastCardDialog::calcPhy(const types::Physical physical, const bool all) const {
    const auto* value = summSkin();
    if (!value) return 0.0;
    double result = 0.0;
    const auto add = [&](const auto& values, const bool include_all) {
        for (const auto& entry : values) {
            if (entry.second && entry.second->checked) {
                result += value->physicalMod(entry.first, physical);
                if (!include_all) break;
            }
        }
    };
    add(sex_, false);
    add(period_, false);
    add(nature_, false);
    add(makings_, all);
    return result;
}

double CastCardDialog::calcMtl(const types::Mental mental, const bool all) const {
    const auto* value = summSkin();
    if (!value) return 0.0;
    double result = 0.0;
    const auto add = [&](const auto& values, const bool include_all) {
        for (const auto& entry : values) {
            if (entry.second && entry.second->checked) {
                result += value->mentalMod(entry.first, mental);
                if (!include_all) break;
            }
        }
    };
    add(sex_, false);
    add(period_, false);
    add(nature_, false);
    add(makings_, all);
    return result;
}

void CastCardDialog::modPhysical() { modPhysical(false); }

void CastCardDialog::modPhysical(const bool force) {
    if (!force && ignoreMod()) return;
    if (!ignoreMod()) applyEnabled();
    int sum = 0;
    for (const auto physical : physicals()) sum += physicalValue(physical);
    if (physical_sum_) {
        physical_sum_->text = format(prop_->requiredMsgs().physicalSum.value, std::to_string(sum));
    }
    updateAptitudes();
}

void CastCardDialog::refCard(CastCardPtr value) {
    if (card_ && value && card_ != value) return;
    // 初回確定まで、新しい編集中の内容を外部更新から保護する。
    if (creation_mode_ && creation_initialized_) return;
    creation_initialized_ = true;
    ++cast_revision_;
    const bool previous_ignore = ignoreMod();
    ignoreMod(true);
    struct Restore {
        CastCardDialog* owner;
        std::weak_ptr<void> life;
        bool previous;
        ~Restore() { if (!life.expired()) owner->ignoreMod(previous); }
    } restore{this, cast_lifetime_, previous_ignore};
    const auto* value_skin = summSkin();
    if (card_ && !creation_mode_) {
        if (image_selector_) image_selector_->images(card_->paths());
        if (desc_) desc_->text = card_->desc();
        if (name_) name_->text = card_->name();
        setNumberDisplayValue(level_, static_cast<double>(card_->level()));
        if (life_max_) life_max_->value = card_->lifeMax();
        if (race_) (void)lib::select_combo_item(*race_, 0U, false);
        for (const auto& entry : sex_) entry.second->checked = false;
        for (const auto& entry : period_) entry.second->checked = false;
        for (const auto& entry : nature_) entry.second->checked = false;
        for (const auto& entry : makings_) entry.second->checked = false;
        if (sex_unknown_) sex_unknown_->checked = true;
        if (period_unknown_) period_unknown_->checked = true;
        if (nature_unknown_) nature_unknown_->checked = true;
        CouponArray remaining;
        bool sex_found = false;
        bool period_found = false;
        bool nature_found = false;
        std::set<features::Makings> making_found;
        for (const auto& current : card_->coupons()) {
            if (!current) continue;
            bool consumed = false;
            if (value_skin) {
                for (const auto& entry : sex_) {
                    if (current->name() == value_skin->sexCoupon(entry.first)) {
                        if (!sex_found) entry.second->checked = true;
                        sex_found = true;
                        consumed = true;
                        break;
                    }
                }
                if (!consumed) {
                    for (const auto& entry : period_) {
                        if (current->name() == value_skin->periodCoupon(entry.first)) {
                            if (!period_found) entry.second->checked = true;
                            period_found = true;
                            consumed = true;
                            break;
                        }
                    }
                }
                if (!consumed) {
                    for (const auto& entry : nature_) {
                        if (current->name() == value_skin->natureCoupon(entry.first)) {
                            if (!nature_found) entry.second->checked = true;
                            nature_found = true;
                            consumed = true;
                            break;
                        }
                    }
                }
                if (!consumed) {
                    for (const auto& entry : makings_) {
                        if (current->name() == value_skin->makingsCoupon(entry.first)) {
                            if (making_found.insert(entry.first).second)
                                entry.second->checked = true;
                            consumed = true;
                            break;
                        }
                    }
                }
                if (!consumed && race_ && value_skin) {
                    for (std::size_t i = 0; i < value_skin->races().size(); ++i) {
                        const auto& race_value = value_skin->races()[i];
                        if (race_value && current->name() ==
                                              prop_->sys().raceCoupon(race_value->name())) {
                            (void)lib::select_combo_item(*race_, i + 1U, false);
                            consumed = true;
                            break;
                        }
                    }
                }
            }
            if (!consumed) remaining.push_back(std::make_shared<Coupon>(*current));
        }
        if (sex_unknown_) sex_unknown_->checked = !sex_found;
        if (period_unknown_) period_unknown_->checked = !period_found;
        if (nature_unknown_) nature_unknown_->checked = !nature_found;
        if (full_coupon_view_) full_coupon_view_->coupons(remaining);
        else coupon_view_.coupons(std::move(remaining));
        if (resist_weapon_) resist_weapon_->checked = card_->weaponResist();
        if (resist_magic_) resist_magic_->checked = card_->magicResist();
        if (undead_) undead_->checked = card_->undead();
        if (automaton_) automaton_->checked = card_->automaton();
        if (unholy_) unholy_->checked = card_->unholy();
        if (constructure_) constructure_->checked = card_->constructure();
        for (const auto element : elements()) {
            if (resist_[element]) resist_[element]->checked = card_->resist(element);
            if (weakness_[element]) weakness_[element]->checked = card_->weakness(element);
        }
        for (const auto physical : physicals())
            setPhysicalValue(physical, static_cast<int>(card_->physical(physical)));
        for (const auto mental : mentalPositives()) {
            if (mental_[mental]) {
                const auto value_mental = roundedMental(card_->mental(mental));
                mental_[mental]->value = static_cast<double>(
                    static_cast<int>(prop_->var().mentalMax.value) + value_mental);
            }
        }
        for (const auto enhance : {types::Enhance::Avoid, types::Enhance::Resist,
                                   types::Enhance::Defense})
            setDefaultEnhanceValue(enhance, card_->defaultEnhance(enhance));
        if (life_) life_->value = card_->life();
        if (life_use_max_) life_use_max_->checked = card_->life() == card_->lifeMax();
        for (const auto enhance : enhancements()) {
            if (live_enhance_[enhance]) live_enhance_[enhance]->value = card_->enhance(enhance);
            if (enhance_round_[enhance])
                enhance_round_[enhance]->value = std::max(1U, card_->enhanceRound(enhance));
        }
        if (paralyze_) paralyze_->value = card_->paralyze();
        if (poison_) poison_->value = card_->poison();
        if (bind_) bind_->value = card_->bindRound();
        if (silence_) silence_->value = card_->silenceRound();
        if (face_up_) face_up_->value = card_->faceUpRound();
        if (anti_magic_) anti_magic_->value = card_->antiMagicRound();
        if (mentality_) {
            (void)lib::select_combo_item(*mentality_, 0U, false);
            for (std::size_t i = 0; i < mentality_table_.size(); ++i) {
                if (mentality_table_[i] == card_->mentality()) {
                    (void)lib::select_combo_item(*mentality_, i, false);
                    break;
                }
            }
        }
        if (mentality_round_) mentality_round_->value = std::max(1U, card_->mentalityRound());
        raceToolTip();
        updateTitle(card_.get());
    } else {
        if (image_selector_) image_selector_->images({});
        if (desc_) desc_->text.clear();
        if (name_) name_->text.clear();
        if (level_) level_->value = 1.0;
        if (life_max_) life_max_->value = 1.0;
        if (race_) (void)lib::select_combo_item(*race_, 0U, false);
        for (const auto& entry : sex_) entry.second->checked = false;
        for (const auto& entry : period_) entry.second->checked = false;
        for (const auto& entry : nature_) entry.second->checked = false;
        for (const auto& entry : makings_) entry.second->checked = false;
        if (sex_unknown_) sex_unknown_->checked = true;
        if (period_unknown_) period_unknown_->checked = true;
        if (nature_unknown_) nature_unknown_->checked = true;
        if (full_coupon_view_) full_coupon_view_->coupons({});
        else coupon_view_.coupons({});
        if (resist_weapon_) resist_weapon_->checked = false;
        if (resist_magic_) resist_magic_->checked = false;
        if (undead_) undead_->checked = false;
        if (automaton_) automaton_->checked = false;
        if (unholy_) unholy_->checked = false;
        if (constructure_) constructure_->checked = false;
        for (const auto element : elements()) {
            if (resist_[element]) resist_[element]->checked = false;
            if (weakness_[element]) weakness_[element]->checked = false;
        }
        for (const auto physical : physicals())
            setPhysicalValue(physical, prop_->looks().physicalNormal());
        for (const auto& entry : mental_)
            entry.second->value = static_cast<double>(prop_->var().mentalMax.value);
        for (const auto enhance : {types::Enhance::Avoid, types::Enhance::Resist,
                                   types::Enhance::Defense})
            setDefaultEnhanceValue(enhance, 0);
        resetLiveStatus();
    }
    setMaxLife();
    modPhysical(true);
    changeLiveEnhance();
    changeMentality();
    changeLifeUseMax();
    if (name_limit_) name_limit_->refreshLimit();
}

void CastCardDialog::appendSelectedCoupons(CouponArray& result) const {
    const auto* value = summSkin();
    if (!value || !prop_) return;
    const auto append = [&result](CouponPtr coupon) {
        if (coupon) result.push_back(std::move(coupon));
    };
    // Dは整列した性別・年代・性格のキーから、最初に選択された
    // ものを使う。特徴は独立した選択。重複を抑止するのは手動編
    // 集の表だけで、異なる選択特徴が同じ名前を持つ場合もある。
    append(createCoupon(sex_, [value](auto key) { return value->sexCoupon(key); }));
    if (const auto* race_value = selectedRace())
        append(std::make_shared<Coupon>(prop_->sys().raceCoupon(race_value->name()), 0));
    append(createCoupon(period_, [value](auto key) { return value->periodCoupon(key); }));
    append(createCoupon(nature_, [value](auto key) { return value->natureCoupon(key); }));
    for (const auto& entry : makings_)
        if (entry.second && entry.second->checked)
            append(std::make_shared<Coupon>(value->makingsCoupon(entry.first), 0));
}

bool CastCardDialog::hasCoupon(const CouponArray& values,
                               const std::string_view name) const {
    return std::any_of(values.begin(), values.end(), [name](const CouponPtr& value) {
        return value && value->name() == name;
    });
}

bool CastCardDialog::apply() {
    if (!prop_) return false;
    const std::weak_ptr<void> life = cast_lifetime_;
    const auto revision = cast_revision_;
    const auto current = [&] { return !life.expired() && cast_revision_ == revision; };
    const auto selector = image_selector_;
    const auto result = selector ? selector->materialPath(forceApplying())
                                        : bgimagedialog::MaterialResult{};
    if (!current()) return false;
    if (result.cancel) return false;

    const auto description = desc_ ? customtext::FixedWidthText<>::toRRText(
        desc_->text, prop_->looks().cardDescLen(), desc_->font().value_or(lib::FontSpec{}), true,
        context_.services.graphics.get()) : std::string{};
    if (!current()) return false;
    const auto applying = on_applying;
    if (applying) applying();
    if (!current()) return false;

    if (card_) {
        if (image_selector_) card_->paths(image_selector_->images());
        card_->desc(description);
        card_->name(name_ ? name_->text : std::string{});
        card_->level(static_cast<std::uint32_t>(std::max(1, integerValue(level_, 1))));
        card_->lifeMax(static_cast<std::uint32_t>(
            std::max(1, integerValue(life_max_, 1))));
    } else {
        if (!summ_) return false;
        const auto images = image_selector_ ? image_selector_->images() : CardImageArray{};
        card_ = std::make_shared<CastCard>(summ_->newId<CastCard>(),
                                            name_ ? name_->text : std::string{}, images,
                                            description,
                                            static_cast<std::uint32_t>(
                                                std::max(1, integerValue(level_, 1))),
                                            static_cast<std::uint32_t>(
                                                std::max(1, integerValue(life_max_, 1))));
    }

    CouponArray values = full_coupon_view_ ? full_coupon_view_->coupons()
                                           : coupon_view_.coupons();
    CouponArray selected;
    appendSelectedCoupons(selected);
    std::erase_if(selected, [&](const auto& value) { return !value || hasCoupon(values, value->name()); });
    selected.insert(selected.end(), values.begin(), values.end());
    card_->coupons(std::move(selected));
    card_->weaponResist(resist_weapon_ && resist_weapon_->checked);
    card_->magicResist(resist_magic_ && resist_magic_->checked);
    card_->undead(undead_ && undead_->checked);
    card_->automaton(automaton_ && automaton_->checked);
    card_->unholy(unholy_ && unholy_->checked);
    card_->constructure(constructure_ && constructure_->checked);
    for (const auto element : elements()) {
        card_->resist(element, resist_[element] && resist_[element]->checked);
        card_->weakness(element, weakness_[element] && weakness_[element]->checked);
    }
    for (const auto physical : physicals())
        card_->physical(physical, static_cast<std::uint32_t>(
            std::max(0, physicalValue(physical))));
    for (const auto mental : mentalPositives()) card_->mental(mental, mentalValue(mental));
    for (const auto enhance : {types::Enhance::Avoid, types::Enhance::Resist,
                               types::Enhance::Defense})
        card_->defaultEnhance(enhance, defaultEnhanceValue(enhance));

    const auto maximum = static_cast<std::uint32_t>(std::max(1, integerValue(life_max_, 1)));
    card_->life(life_use_max_ && life_use_max_->checked
                    ? maximum
                    : static_cast<std::uint32_t>(std::max(0, integerValue(life_))));
    for (const auto enhance : enhancements()) {
        const auto round = static_cast<std::uint32_t>(
            std::max(0, integerValue(enhance_round_[enhance])));
        card_->enhance(enhance, round > 0U ? integerValue(live_enhance_[enhance]) : 0);
        card_->enhanceRound(enhance,
                            card_->enhance(enhance) == 0 ? 0U : round);
    }
    card_->paralyze(static_cast<std::uint32_t>(std::max(0, integerValue(paralyze_))));
    card_->poison(static_cast<std::uint32_t>(std::max(0, integerValue(poison_))));
    card_->bindRound(static_cast<std::uint32_t>(std::max(0, integerValue(bind_))));
    card_->silenceRound(static_cast<std::uint32_t>(std::max(0, integerValue(silence_))));
    card_->faceUpRound(static_cast<std::uint32_t>(std::max(0, integerValue(face_up_))));
    card_->antiMagicRound(static_cast<std::uint32_t>(std::max(0, integerValue(anti_magic_))));
    types::Mentality mentality = types::Mentality::Normal;
    if (mentality_ && mentality_->selected_index &&
        *mentality_->selected_index < mentality_table_.size())
        mentality = mentality_table_[*mentality_->selected_index];
    const auto mentality_round = static_cast<std::uint32_t>(
        std::max(0, integerValue(mentality_round_)));
    card_->mentality(mentality_round == 0U ? types::Mentality::Normal : mentality);
    card_->mentalityRound(card_->mentality() == types::Mentality::Normal
                              ? 0U : mentality_round);

    creation_mode_ = false;
    const auto changed = context_.changed_card;
    const auto coupons = context_.refresh_coupons;
    const auto toolbar = context_.refresh_toolbar;
    const auto applied_card = card_;
    updateCoefficients(context_.common, {applied_card});
    if (!current()) return false;
    if (changed) changed(applied_card.get());
    if (!current()) return false;
    if (coupons) coupons();
    if (!current()) return false;
    if (toolbar) toolbar();
    if (!current()) return false;
    updateTitle(card_.get());
    refreshWarning();
    return current();
}

void CastCardDialog::changeLiveEnhance() {
    for (const auto enhance : enhancements()) {
        if (enhance_round_[enhance]) {
            enhance_round_[enhance]->set_enabled(
                !read_only_ && integerValue(live_enhance_[enhance]) != 0);
        }
    }
}

void CastCardDialog::changeMentality() {
    const bool active = mentality_ && mentality_->selected_index &&
                        *mentality_->selected_index != 0U;
    if (mentality_round_) mentality_round_->set_enabled(!read_only_ && active);
}

void CastCardDialog::changeLifeUseMax() {
    if (life_) life_->set_enabled(!read_only_ &&
                                  !(life_use_max_ && life_use_max_->checked));
}

void CastCardDialog::resetLiveStatus() {
    if (life_) life_->value = life_max_ ? life_max_->value : 1.0;
    if (life_use_max_) life_use_max_->checked = true;
    for (const auto enhance : enhancements()) {
        if (live_enhance_[enhance]) live_enhance_[enhance]->value = 0.0;
        if (enhance_round_[enhance]) enhance_round_[enhance]->value = 1.0;
    }
    for (const auto& value : {paralyze_, poison_, bind_, silence_, face_up_, anti_magic_})
        if (value) value->value = 0.0;
    if (mentality_) (void)lib::select_combo_item(*mentality_, 0U, false);
    if (mentality_round_) mentality_round_->value = 1.0;
    changeLiveEnhance();
    changeMentality();
    changeLifeUseMax();
    refreshWarning();
}

void CastCardDialog::setFeatureSelection() {
    const auto clearOthers = [](auto& values, const auto& selected) {
        for (const auto& entry : values)
            if (entry.second != selected) entry.second->checked = false;
    };
    if (sex_unknown_ && sex_unknown_->checked) clearOthers(sex_, sex_unknown_);
    if (period_unknown_ && period_unknown_->checked) clearOthers(period_, period_unknown_);
    if (nature_unknown_ && nature_unknown_->checked) clearOthers(nature_, nature_unknown_);
}

void CastCardDialog::SelectRace::widgetSelected() {
    if (this_) {
        this_->raceToolTip();
        this_->refreshWarning();
    }
}

void CastCardDialog::BasicResist::widgetSelected() {
    if (!this_) return;
    const auto* value = this_->selectedRace();
    const bool automaton = value && value->automaton();
    const bool constructure = value && value->constructure();
    const bool undead = value && value->undead();
    const bool unholy = value && value->unholy();
    if (this_->automaton_) this_->automaton_->checked = automaton;
    if (this_->constructure_) this_->constructure_->checked = constructure;
    if (this_->undead_) this_->undead_->checked = undead;
    if (this_->unholy_) this_->unholy_->checked = unholy;
    if (this_->resist_weapon_) this_->resist_weapon_->checked = value && value->weaponResist();
    if (this_->resist_magic_) this_->resist_magic_->checked = value && value->magicResist();
    for (const auto element : elements()) {
        if (this_->resist_[element])
            this_->resist_[element]->checked = value && value->resist(element);
        if (this_->weakness_[element])
            this_->weakness_[element]->checked = value && value->weakness(element);
    }
    this_->refreshWarning();
}

void CastCardDialog::BasicEnhance::widgetSelected() {
    if (!this_) return;
    const auto* value = this_->selectedRace();
    for (const auto enhance : {types::Enhance::Avoid, types::Enhance::Resist,
                               types::Enhance::Defense}) {
        this_->setDefaultEnhanceValue(
            enhance, value ? value->defaultEnhance(enhance) : 0);
    }
    this_->refreshWarning();
}

void CastCardDialog::SelLifeC::widgetSelected() {
    if (!this_) return;
    const auto level = static_cast<std::uint32_t>(std::max(1, integerValue(this_->level_, 1)));
    const auto vit = static_cast<std::uint32_t>(std::max(0,
        this_->physicalValue(types::Physical::Vit)));
    const auto min = static_cast<std::uint32_t>(std::max(0,
        this_->physicalValue(types::Physical::Min)));
    if (this_->life_max_) this_->life_max_->value = this_->prop_->looks().lifeCalc(level, vit, min);
    this_->setMaxLife();
    this_->refreshWarning();
}

void CastCardDialog::LifeMaxL::modifyText() {
    if (this_) this_->setMaxLife();
}

void CastCardDialog::MSListener::widgetSelected(const features::Makings making) {
    if (!this_ || !this_->summSkin()) return;
    const auto reverse = this_->summSkin()->reverseMakings(making);
    const auto found = this_->makings_.find(reverse);
    if (found != this_->makings_.end() && found->second->checked)
        found->second->checked = false;
}

void CastCardDialog::ESListener::widgetSelected() {
    if (auto target = target_.lock()) target->checked = false;
}

void CastCardDialog::CalcPhysical::widgetSelected() {
    if (!this_) return;
    const auto* value = this_->selectedRace();
    for (const auto physical : physicals()) {
        int minimum = static_cast<int>(this_->prop_->looks().physicalCutMin());
        int base = static_cast<int>(this_->prop_->looks().physicalNormal());
        if (value) {
            base = static_cast<int>(value->physical(physical));
            minimum = std::min(minimum, base);
        }
        int maximum = base + static_cast<int>(this_->prop_->looks().physicalCutMaxBase());
        if (value) maximum = std::min(maximum,
                           static_cast<int>(this_->prop_->var().physicalMax.value));
        const auto computed = std::clamp(
            static_cast<int>(static_cast<double>(base) +
                             this_->calcPhy(physical, true)),
            minimum, maximum);
        this_->setPhysicalValue(physical, computed);
    }
    this_->modPhysical(false);
}

void CastCardDialog::CalcMental::widgetSelected() {
    if (!this_) return;
    const auto* value = this_->selectedRace();
    const int cut = static_cast<int>(this_->prop_->looks().mentalCut());
    for (const auto mental : mentalPositives()) {
        double amount = value ? value->mental(mental) : 0.0;
        int minimum = -cut;
        int maximum = cut;
        if (value) {
            // DのCalcMentalは種族の境界を求める際、
            // 整数の種族値も含め、常に半段階を除く。ここでは既
            // 存カードの初期化時の条件付き丸めを再利用しない。
            const auto race_amount = value->mental(mental);
            const auto race_value = static_cast<int>(race_amount < 0.0
                ? race_amount + 0.5 : race_amount - 0.5);
            minimum = std::min(minimum, race_value);
            maximum = std::max(maximum, race_value);
        }
        if (lib::editor_window_host_options().all_mode) {
            amount += this_->calcMtl(mental, true);
        } else if (const auto* skin = this_->summSkin()) {
            // Dは特徴群ごとの小計を先に計算し、Sex、Period、
            // Nature、Makingsの順に種族の基準へ加える。
            // 全項目を平坦化すると、同じ精度でも相殺の結果が変わる。
            const auto addGroup = [&](const auto& group, const bool include_all) {
                double subtotal = 0.0;
                for (const auto& entry : group) {
                    if (entry.second && entry.second->checked) {
                        subtotal += skin->mentalMod(entry.first, mental);
                        if (!include_all) break;
                    }
                }
                amount += subtotal;
            };
            addGroup(this_->sex_, false);
            addGroup(this_->period_, false);
            addGroup(this_->nature_, false);
            addGroup(this_->makings_, true);
        }
        const auto computed = std::clamp(static_cast<int>(amount), minimum, maximum);
        if (this_->mental_[mental])
            this_->mental_[mental]->value = computed +
                static_cast<int>(this_->prop_->var().mentalMax.value);
    }
    this_->updateAptitudes();
}

void CastCardDialog::LiveEnh::widgetSelected() {
    if (this_) this_->changeLiveEnhance();
}

void CastCardDialog::LiveEnh::modifyText() {
    if (this_) this_->changeLiveEnhance();
}

void CastCardDialog::SelMentality::widgetSelected() {
    if (this_) this_->changeMentality();
}

void CastCardDialog::LifeUseMax::widgetSelected() {
    if (this_) this_->changeLifeUseMax();
}

void CastCardDialog::ResetLiveStatus::widgetSelected() {
    if (this_) this_->resetLiveStatus();
}

void CastCardDialog::Dispose::widgetDisposed() {
    if (!this_) return;
    this_->unbindExternalNotifications();
    this_->context_.refresh_warnings = {};
    this_->context_.refresh_skin = {};
    this_->context_.refresh_data_version = {};
}

void updateCoefficients(const Commons& common,
                        const lib::Array<CastCardPtr>& casts) {
    updateCoefficients(common, lib::SharedSlice<const CastCardPtr>(casts));
}

void updateCoefficients(const Commons& common,
                        lib::SharedSlice<const CastCardPtr> casts) {
    if (!common.prop || !common.summary || common.summary->legacy() || !common.skin)
        return;
    std::map<lib::String, const race::Race*> races;
    for (const auto& value : common.skin->races()) {
        if (value) races[common.prop->sys().raceCoupon(value->name())] = value.get();
    }
    for (const auto& cast : casts) {
        if (!cast) continue;
        bool found = false;
        for (const auto& value : cast->coupons()) {
            if (!value || !common.prop->sys().isRaceCoupon(value->name())) continue;
            const auto race_value = races.find(value->name());
            if (race_value == races.end()) continue;
            cast->levelCoefficient(race_value->second->levelCoefficient());
            cast->epPerLevel(race_value->second->epPerLevel());
            found = true;
            break;
        }
        if (!found) {
            cast->levelCoefficient(1.0);
            cast->epPerLevel(race::DEFAULT_EP_PER_LEVEL);
        }
    }
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::castcarddialog
