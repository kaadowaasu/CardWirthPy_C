#include "bgimagedialog.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/messageutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cellrender.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::bgimagedialog {
namespace {

[[nodiscard]] Skin::Ptr retainSkin(const Skin* value, const Skin::Ptr& owner) {
    if (!value) return {};
    if (owner.get() == value) return owner;
    // DはGC管理のSkinを保持する。C++は既存の制御ブロックを使う必要がある。
    // 何もしない削除処理では、借用ポインターを所有しているように見せるだけになる。
    if (auto retained = const_cast<Skin*>(value)->weak_from_this().lock())
        return retained;
    // 公開の生ポインター用オーバーロードは、従来どおり呼び出し側の寿命内で借
    // 用する。空の所有元の別名は監視参照（use_count==0）であり、
    // 新しい所有権や制御ブロックを作らず、既存のプレビューAPIを維持する。
    return Skin::Ptr(Skin::Ptr{}, const_cast<Skin*>(value));
}

[[nodiscard]] std::uint32_t acceleratorCharacter(const lib::KeyEvent& event) {
    if (!event.printable.empty())
        return static_cast<unsigned char>(event.printable.front());
    if (event.key >= lib::Key::A && event.key <= lib::Key::Z)
        return static_cast<std::uint32_t>(
            'A' + static_cast<int>(event.key) - static_cast<int>(lib::Key::A));
    if (event.key >= lib::Key::Digit0 && event.key <= lib::Key::Digit9)
        return static_cast<std::uint32_t>(
            '0' + static_cast<int>(event.key) - static_cast<int>(lib::Key::Digit0));
    return 0U;
}

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

[[nodiscard]] int integerValue(const std::shared_ptr<lib::NumberEdit>& control,
                               int fallback = 0) noexcept {
    if (!control) return fallback;
    const auto value = std::clamp(control->value,
                                  static_cast<double>(std::numeric_limits<int>::min()),
                                  static_cast<double>(std::numeric_limits<int>::max()));
    return static_cast<int>(std::lround(value));
}

[[nodiscard]] std::string message(const props::Msgs& msgs,
                                  const msgs::Message& value) {
    (void)msgs;
    return value.value;
}

[[nodiscard]] std::string smoothingName(const props::Msgs& msgs,
                                        types::Smoothing value) {
    switch (value) {
    case types::Smoothing::True: return message(msgs, msgs.smoothingNameTrue);
    case types::Smoothing::False: return message(msgs, msgs.smoothingNameFalse);
    case types::Smoothing::Default:
    default: return message(msgs, msgs.smoothingNameDefault);
    }
}

[[nodiscard]] std::string blendName(const props::Msgs& msgs,
                                    types::BlendMode value) {
    switch (value) {
    case types::BlendMode::Add: return message(msgs, msgs.blendModeNameAdd);
    case types::BlendMode::Subtract:
        return message(msgs, msgs.blendModeNameSubtract);
    case types::BlendMode::Multiply:
        return message(msgs, msgs.blendModeNameMultiply);
    case types::BlendMode::Mask: return message(msgs, msgs.blendModeNameMask);
    case types::BlendMode::Normal:
    default: return message(msgs, msgs.blendModeNameNormal);
    }
}

[[nodiscard]] std::string gradientName(const props::Msgs& msgs,
                                       types::GradientDir value) {
    switch (value) {
    case types::GradientDir::LeftToRight:
        return message(msgs, msgs.gradientDirNameLeftToRight);
    case types::GradientDir::TopToBottom:
        return message(msgs, msgs.gradientDirNameTopToBottom);
    case types::GradientDir::None:
    default: return message(msgs, msgs.gradientDirNameNone);
    }
}

[[nodiscard]] std::string borderingName(const props::Msgs& msgs,
                                         types::BorderingType value) {
    switch (value) {
    case types::BorderingType::Outline:
        return message(msgs, msgs.borderingTypeNameOutline);
    case types::BorderingType::Inline:
        return message(msgs, msgs.borderingTypeNameInline);
    case types::BorderingType::None:
    default: return message(msgs, msgs.borderingTypeNameNone);
    }
}

[[nodiscard]] std::string updateName(const props::Msgs& msgs,
                                     types::UpdateType value) {
    switch (value) {
    case types::UpdateType::Fixed: return message(msgs, msgs.updateTypeNameFixed);
    case types::UpdateType::All: return message(msgs, msgs.updateTypeNameAll);
    case types::UpdateType::Variables:
    default: return message(msgs, msgs.updateTypeNameVariables);
    }
}

[[nodiscard]] std::string cellName(const BgImage& image) {
    return image.cellName();
}

[[nodiscard]] bool hasService(const lib::Services& services) noexcept {
    return static_cast<bool>(services.gui) ||
           static_cast<bool>(services.graphics) ||
           static_cast<bool>(services.dialogs) ||
           static_cast<bool>(services.filesystem) ||
           static_cast<bool>(services.dispatcher);
}

[[nodiscard]] bool isSelected(const std::shared_ptr<lib::ComboBox>& combo,
                              std::size_t index) noexcept {
    return combo && combo->selected_index && *combo->selected_index == index;
}

template <typename Enum>
[[nodiscard]] std::size_t enumIndex(const std::vector<Enum>& values,
                                    Enum value) noexcept {
    const auto found = std::find(values.begin(), values.end(), value);
    return found == values.end() ? values.size()
                                 : static_cast<std::size_t>(found - values.begin());
}

[[nodiscard]] structs::CRGB colorFrom(const ColorPicker& picker) noexcept {
    auto value = picker.color();
    value.a = static_cast<std::uint32_t>(std::clamp(picker.alpha(), 0, 255));
    return value;
}

[[nodiscard]] lib::Color portableColor(const structs::CRGB& value,
                                       int alpha = -1) noexcept {
    return {static_cast<std::uint8_t>(std::min<std::uint32_t>(value.r, 255U)),
            static_cast<std::uint8_t>(std::min<std::uint32_t>(value.g, 255U)),
            static_cast<std::uint8_t>(std::min<std::uint32_t>(value.b, 255U)),
            static_cast<std::uint8_t>(std::clamp(
                alpha < 0 ? static_cast<int>(value.a) : alpha, 0, 255))};
}

[[nodiscard]] lib::FontSpec portableFont(const structs::CFont& value) {
    lib::FontSpec result;
    result.family = value.name;
    result.points = static_cast<float>(std::max<std::uint32_t>(value.point, 1U));
    result.weight = value.bold ? 700U : 400U;
    result.italic = value.italic;
    return result;
}

template <typename T>
void setPreferred(const std::shared_ptr<T>& widget, float width, float height,
                  float grow = 0.0F) {
    if (!widget) return;
    widget->set_layout_item({.horizontal = lib::Align::Stretch,
                             .vertical = lib::Align::Stretch,
                             .minimum = {width, height},
                             .preferred = {width, height},
                             .grow = grow});
}

[[nodiscard]] std::shared_ptr<lib::Panel> makePanel(
    const lib::Services& services, lib::Orientation orientation,
    float spacing = 4.0F) {
    auto panel = makeComponent<lib::Panel>(services, lib::ComponentKind::Panel);
    auto layout = std::make_shared<lib::LinearLayout>(orientation);
    layout->spacing = spacing;
    panel->set_layout(layout);
    return panel;
}

[[nodiscard]] std::shared_ptr<lib::GroupBox> makeGroup(
    const lib::Services& services, std::string title,
    std::shared_ptr<lib::Layout> layout = {}) {
    auto group = makeComponent<lib::GroupBox>(services,
                                              lib::ComponentKind::GroupBox);
    group->title = std::move(title);
    if (!layout) layout = std::make_shared<lib::LinearLayout>(
        lib::Orientation::Vertical);
    group->set_layout(std::move(layout));
    return group;
}

// D版の参照画像から計測したTextCellDialogの幅。
constexpr float kTextCellSideWidth = 110.0F;   // スタイル・色の列。
constexpr float kTextCellUpdateWidth = 196.0F; // 再表示時の処理
constexpr float kTextCellSpinnerWidth = 50.0F;
// テキストセルの縦幅を抑える。一行のプレビュー文字（既定18pxフォン
// ト）、フラグツリー一行（共通20px）、セル名の幅500pxを使う。

// グループ内容を一つの内側パネルへ移し、グループ内で中央配置す
// る。DのGroup内のCenterLayoutに相当する。
void centerGroupContent(lib::GroupBox& group) {
    auto inner = std::make_shared<lib::Panel>();
    inner->set_layout(group.layout());
    const auto children = group.children();
    group.clear();
    for (const auto& child : children) inner->add(child);
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1U;
    group.set_layout(layout);
    inner->set_layout_item({.horizontal = lib::Align::Center,
                            .vertical = lib::Align::Center,
                            .grow = 1.0F});
    group.add(inner);
}

}  // namespace

ColorPicker::ColorPicker(Props& prop, lib::Container& parent, bool transparency) {
    initialize(prop, &parent, transparency);
}

ColorPicker::ColorPicker(Props& prop,
                         const std::shared_ptr<lib::Container>& parent,
                         bool transparency) {
    initialize(prop, parent.get(), transparency);
}

ColorPicker::ColorPicker(ColorPicker&& other) noexcept {
    *this = std::move(other);
}

ColorPicker& ColorPicker::operator=(ColorPicker&& other) noexcept {
    if (this == &other) return *this;
    detachCallbacks();
    modEvent = std::move(other.modEvent);
    panel_ = std::move(other.panel_);
    color_well_ = std::move(other.color_well_);
    color_button_ = std::move(other.color_button_);
    alpha_control_ = std::move(other.alpha_control_);
    services_ = std::move(other.services_);
    color_ = other.color_;
    alpha_ = other.alpha_;
    transparency_ = other.transparency_;
    enabled_ = other.enabled_;
    if (self_) *self_ = nullptr;
    self_ = std::move(other.self_);
    if (self_) *self_ = this;
    bindCallbacks();
    return *this;
}

ColorPicker::~ColorPicker() {
    if (self_) *self_ = nullptr;
    detachCallbacks();
}

void ColorPicker::detachCallbacks() noexcept {
    if (color_well_) color_well_->on_changed = {};
    if (color_button_) color_button_->on_click = {};
    if (alpha_control_) alpha_control_->on_changed = {};
}

void ColorPicker::chooseColor() {
    if (!enabled_ || !services_.dialogs || !color_well_) return;
    if (!self_) self_ = std::make_shared<ColorPicker*>(this);
    // 非同期で選択する。同期の選択画面は入れ子のモーダルループを使い、ブラ
    // ウザ内のWeb ALLのEditorが「...」で停止してしまう。
    std::weak_ptr<ColorPicker*> weak = self_;
    (void)services_.dialogs->select_color_async(
        color_well_->value, [weak](std::optional<lib::Color> selected) {
            const auto self = weak.lock();
            if (!selected || !self || !*self) return;
            auto& picker = **self;
            if (!picker.color_well_) return;
            // 共通の選択画面はRGBAを扱う（Dのネイティブ版は
            // RGB）。透過対応の場合は選択したアルファも反映し
            // 、所有元への通知前に外側のアルファ入力を同期する。
            picker.color({selected->red, selected->green, selected->blue,
                picker.transparency_ ? selected->alpha
                                     : static_cast<std::uint32_t>(picker.alpha_)});
            picker.notifyChanged();
        });
}

void ColorPicker::bindCallbacks() {
    if (color_well_) {
        color_well_->on_changed = [this](lib::Color value) {
            color({value.red, value.green, value.blue,
                transparency_ ? value.alpha : static_cast<std::uint32_t>(alpha_)});
            notifyChanged();
        };
    }
    if (color_button_) color_button_->on_click = [this] { chooseColor(); };
    if (alpha_control_) {
        alpha_control_->on_changed = [this](double value) {
            alpha_ = std::clamp(static_cast<int>(std::lround(value)), 0, 255);
            color_.a = static_cast<std::uint32_t>(alpha_);
            if (color_well_)
                color_well_->value.alpha = static_cast<std::uint8_t>(alpha_);
            notifyChanged();
        };
    }
}

void ColorPicker::initialize(Props& prop, lib::Container* parent,
                             bool transparency) {
    transparency_ = transparency;
    services_ = prop.images().services();
    panel_ = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = transparency ? 4U : 1U;
    layout->equal_width = false;
    layout->spacing = 4.0F;
    panel_->set_layout(layout);

    auto sample = makeComponent<lib::Panel>(services_, lib::ComponentKind::Panel);
    auto sample_layout = std::make_shared<lib::GridLayout>();
    sample_layout->columns = 2U;
    sample_layout->spacing = 2.0F;
    sample->set_layout(sample_layout);

    color_well_ = makeComponent<lib::ColorWell>(services_,
                                                lib::ComponentKind::ColorWell);
    color_well_->value = {0, 0, 0, 255};
    setPreferred(color_well_,
                 static_cast<float>(prop.var().colorExampleWidth.value), 24.0F,
                 1.0F);
    sample->add(color_well_);

    color_button_ = makeComponent<lib::Button>(services_,
                                               lib::ComponentKind::Button);
    color_button_->text = "...";
    color_button_->set_tooltip(prop.requiredMsgs().fontColor.value);
    sample->add(color_button_);
    panel_->add(sample);

    if (transparency) {
        panel_->add(std::make_shared<lib::Label>(
            message(prop.requiredMsgs(), prop.requiredMsgs().alphaChannel)));
        alpha_control_ = makeComponent<lib::NumberEdit>(
            services_, lib::ComponentKind::NumberEdit);
        alpha_control_->minimum = 0.0;
        alpha_control_->maximum = 255.0;
        alpha_control_->value = 255.0;
        panel_->add(alpha_control_);
        panel_->add(std::make_shared<lib::Label>(
            ::cwxeditor::cwx::utils::tryFormat(prop.requiredMsgs().rangeHint.value,
                                                0, 255)));
    }
    bindCallbacks();
    if (parent) parent->add(panel_);
}

void ColorPicker::color(structs::CRGB value) noexcept {
    value.r = std::min<std::uint32_t>(value.r, 255U);
    value.g = std::min<std::uint32_t>(value.g, 255U);
    value.b = std::min<std::uint32_t>(value.b, 255U);
    // DのRGB専用選択画面は、常に不透明な見本を描く。
    value.a = transparency_ ? std::min<std::uint32_t>(value.a, 255U) : 255U;
    color_ = value;
    alpha_ = static_cast<int>(value.a);
    if (color_well_) {
        color_well_->value = {static_cast<std::uint8_t>(value.r),
                              static_cast<std::uint8_t>(value.g),
                              static_cast<std::uint8_t>(value.b),
                              static_cast<std::uint8_t>(value.a)};
    }
    if (alpha_control_) alpha_control_->value = static_cast<double>(alpha_);
}

void ColorPicker::alpha(int value) noexcept {
    if (!transparency_) return;  // DのRGB専用モードにはアルファ入力がない。
    alpha_ = std::clamp(value, 0, 255);
    color_.a = static_cast<std::uint32_t>(alpha_);
    if (alpha_control_) alpha_control_->value = static_cast<double>(alpha_);
    if (color_well_) color_well_->value.alpha = static_cast<std::uint8_t>(alpha_);
}

void ColorPicker::enabled(bool value) noexcept {
    enabled_ = value;
    if (panel_) panel_->set_enabled(value);
    if (color_well_) color_well_->set_enabled(value);
    if (color_button_) color_button_->set_enabled(value);
    if (alpha_control_) alpha_control_->set_enabled(value);
}

void ColorPicker::notifyChanged() {
    const auto callbacks = modEvent;
    for (const auto& callback : callbacks)
        if (callback) callback();
}

lib::Services BgImageDialog::servicesFor(const BgImageDialogContext& context,
                                         Props& prop) {
    if (hasService(context.services)) return context.services;
    if (hasService(context.common.services)) return context.common.services;
    return prop.images().services();
}

BgImageDialog::BgImageDialog(BgImageDialogContext context, Props& prop,
                             const Summary* summ,
                             std::shared_ptr<lib::Window> parent,
                             lib::String title, lib::ResourceId image,
                             bool resizable, std::shared_ptr<DSize> size,
                             BgImagePtr back, bool create,
                             UseCounterPtr counter)
    : AbsDialog(prop, std::move(parent), false, std::move(title), image,
                resizable, std::move(size), true, true, {}, false,
                servicesFor(context, prop)),
      context_(std::move(context)),
      editor_commons_lifetime_(context_.editor_commons
                                   ? context_.editor_commons->lifetimeToken()
                                   : std::weak_ptr<const int>{}),
      prop_(&prop), summ_(summ),
      counter_(std::move(counter)), back_(std::move(back)), create_(create),
      selected_(!create) {
    enterClose(true);
    if (create_) applyEnabled(true);
}

BgImageDialog::~BgImageDialog() { disconnectExternalNotifications(); }

BgImageDialog::BgImageDialog(BgImageDialogContext context, Props& prop,
                             const Summary* summ,
                             std::shared_ptr<lib::Window> parent,
                             lib::String title, lib::ResourceId image,
                             bool resizable, DSize* size, BgImagePtr back,
                             bool create, UseCounterPtr counter)
    : AbsDialog(prop, std::move(parent), false, std::move(title), image,
                resizable, size, true, true, {}, false,
                servicesFor(context, prop)),
      context_(std::move(context)),
      editor_commons_lifetime_(context_.editor_commons
                                   ? context_.editor_commons->lifetimeToken()
                                   : std::weak_ptr<const int>{}),
      prop_(&prop), summ_(summ),
      counter_(std::move(counter)), back_(std::move(back)), create_(create),
      selected_(!create) {
    enterClose(true);
    if (create_) applyEnabled(true);
}

void BgImageDialog::setup(Container& area) {
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1U;
    layout->equal_width = false;
    area.set_layout(layout);

    auto body = makeComponent<lib::Panel>(context_.services,
                                           lib::ComponentKind::Panel);
    if (!body) body = std::make_shared<lib::Panel>();
    body->set_layout(std::make_shared<lib::GridLayout>());
    body->set_layout_item({.horizontal = lib::Align::Stretch,
                           .vertical = lib::Align::Stretch,
                           .grow = 1.0F});
    area.add(body);

    if (summ_ && splitSpecificAndFlag()) {
        auto top = makeComponent<lib::SplitView>(context_.services,
                                                 lib::ComponentKind::SplitView);
        if (!top) top = std::make_shared<lib::SplitView>();
        top->orientation = lib::Orientation::Horizontal;
        const auto left = std::max(1, prop_->var().backSashL.value);
        const auto right = std::max(1, prop_->var().backSashR.value);
        top->weights = {static_cast<float>(left), static_cast<float>(right)};
        top->on_weights_changed = [this](const std::vector<float>& weights) {
            if (weights.size() < 2U) return;
            prop_->var().backSashL = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[0])));
            prop_->var().backSashR = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[1])));
        };
        top->set_layout_item({.horizontal = lib::Align::Stretch,
                              .vertical = lib::Align::Stretch,
                              .grow = 1.0F});
        body->add(top);
        setupSpecific(*top);
        (void)createFlagPanel(*top);
    } else {
        setupSpecific(*body);
        if (summ_) (void)createFlagPanel(*body);
    }
    createPosPanel(*body, usesMask(), usesSmoothing());
    setFirstParams();
    initializeSpecific();
    refDataVersion();
    connectExternalNotifications();
}

std::shared_ptr<lib::GroupBox> BgImageDialog::createFlagPanel(
    lib::Container& parent) {
    auto group = makeComponent<lib::GroupBox>(context_.services,
                                               lib::ComponentKind::GroupBox);
    if (!group) group = std::make_shared<lib::GroupBox>();
    group->title = message(prop_->requiredMsgs(), prop_->requiredMsgs().refFlag);
    auto group_layout = std::make_shared<lib::GridLayout>();
    group_layout->columns = 1U;
    group_layout->spacing = 4.0F;
    group->set_layout(group_layout);
    group->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .minimum = {
                                static_cast<float>(prop_->var().flagsWidth.value),
                                static_cast<float>(prop_->var().flagsHeight.value)},
                            .preferred = {
                                static_cast<float>(prop_->var().flagsWidth.value),
                                static_cast<float>(prop_->var().flagsHeight.value)},
                            .grow = 1.0F});
    flag_ = makeComponent<lib::ComboBox>(context_.services,
                                         lib::ComponentKind::ComboBox);
    if (!flag_) flag_ = std::make_shared<lib::ComboBox>();
    flag_->editable = true;
    for (const auto& value : context_.flags)
        flag_->items.push_back({value, value});
    const auto summary_owner =
        summ_ ? const_cast<Summary*>(summ_)->weak_from_this().lock()
              : Summary::Owner{};
    if (summary_owner) {
        chooser::Commons commons;
        commons.services = context_.services;
        commons.refreshToolBar = context_.refresh_toolbar;
        commons.menuText = [prop = prop_](types::MenuID id) { return prop->buildMenu(id); };
        commons.menuImage = [prop = prop_](types::MenuID id) { return prop->images().menu(id); };
        chooser::ChooserProperties properties;
        properties.editor_commons = context_.editor_commons;
        if (context_.editor_commons) {
            properties.openCWXPath = [common = context_.editor_commons](std::string_view value, bool activate) {
                (void)common->openCWXPath(std::string(value), activate);
            };
        }
        properties.logicalSort = prop_->var().logicalSort.value;
        properties.selectVariableWithTree =
            prop_->var().selectVariableWithTree.value;
        properties.expandChooserItems = prop_->var().expandChooserItems.value;
        properties.flagsWidth = prop_->var().flagsWidth.value;
        properties.flagsHeight = prop_->var().flagsHeight.value;
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
                // Dの背景表示フラグはシナリオ全体の変数。選択画面にイベント内のロ
                // ーカルカウンターは渡さない（bgimagedialog.d）。
                std::move(commons), summary_owner,
                UseCounterPtr{},
                group.get(), true, std::move(properties));
        if (flag_chooser_->widget()) {
            flag_chooser_->widget()->set_layout_item(
                {.horizontal = lib::Align::Stretch,
                 .vertical = lib::Align::Stretch,
                 .preferred = {
                     static_cast<float>(prop_->var().flagsWidth.value),
                     static_cast<float>(prop_->var().flagsHeight.value)},
                 .grow = 1.0F});
        }
        if (flag_chooser_->tree()) {
            setPreferred(flag_chooser_->tree(),
                         static_cast<float>(prop_->var().flagsWidth.value),
                         static_cast<float>(prop_->var().flagsHeight.value), 1.0F);
        }
        flag_chooser_->modEvent.push_back([this] {
            setComboValue(*flag_, flag_chooser_->selected());
            refreshWarning();
            if (!ignoreMod()) applyEnabled();
        });
    } else {
        group->add(flag_);
    }
    parent.add(group);
    flag_->on_selection_changed = [this](std::optional<std::size_t>) {
        refreshWarning();
    };
    mod(*flag_);
    return group;
}

std::shared_ptr<lib::GroupBox> BgImageDialog::createPositionPanel(
    lib::Container& parent, bool include_mask, bool include_smoothing) {
    auto group = makeComponent<lib::GroupBox>(context_.services,
                                               lib::ComponentKind::GroupBox);
    if (!group) group = std::make_shared<lib::GroupBox>();
    group->title = message(prop_->requiredMsgs(), prop_->requiredMsgs().backPosition);
    const std::size_t columns = include_mask ? 6U : 5U;
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = columns;
    layout->equal_width = false;
    layout->spacing = 4.0F;
    group->set_layout(layout);
    group->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Start});

    const auto pos_max = static_cast<double>(prop_->var().posLeftMax.value);
    const auto top_max = static_cast<double>(prop_->var().posTopMax.value);
    const auto width_max = static_cast<double>(prop_->var().backWidthMax.value);
    const auto height_max = static_cast<double>(prop_->var().backHeightMax.value);
    const auto layer_max = static_cast<double>(prop_->var().layerMax.value);
    auto number = [this](double minimum, double maximum) {
        auto result = makeComponent<lib::NumberEdit>(context_.services,
                                                     lib::ComponentKind::NumberEdit);
        if (!result) result = std::make_shared<lib::NumberEdit>();
        result->minimum = minimum;
        result->maximum = maximum;
        result->step = 1.0;
        result->decimals = 0U;
        return result;
    };
    std::size_t column = 0U;
    auto add_number = [this, &group, &column](
                          std::string label,
                          const std::shared_ptr<lib::NumberEdit>& control,
                          std::string hint = {}) {
        auto panel = makeComponent<lib::Panel>(context_.services,
                                               lib::ComponentKind::Panel);
        auto panel_layout = std::make_shared<lib::GridLayout>();
        panel_layout->columns = hint.empty() ? 2U : 3U;
        panel_layout->spacing = 3.0F;
        panel->set_layout(panel_layout);
        panel->set_layout_item({.row = 0U, .column = column++,
                                .horizontal = lib::Align::Start,
                                .vertical = lib::Align::Center});
        panel->add(std::make_shared<lib::Label>(std::move(label)));
        control->set_layout_item({.minimum = {44.0F, 0.0F},
                                  .preferred = {50.0F, 0.0F}});
        panel->add(control);
        if (!hint.empty()) panel->add(std::make_shared<lib::Label>(std::move(hint)));
        group->add(panel);
    };

    x_ = number(-pos_max, pos_max);
    y_ = number(-top_max, top_max);
    w_ = number(0.0, width_max);
    h_ = number(0.0, height_max);
    layer_ = number(
        static_cast<double>(background::BgImage::LAYER_BACK_CELL), layer_max);
    add_number(message(prop_->requiredMsgs(), prop_->requiredMsgs().left), x_);
    add_number(message(prop_->requiredMsgs(), prop_->requiredMsgs().top), y_);
    add_number(message(prop_->requiredMsgs(), prop_->requiredMsgs().width), w_);
    add_number(message(prop_->requiredMsgs(), prop_->requiredMsgs().height), h_);

    if (include_mask) {
        mask_ = makeComponent<lib::CheckBox>(context_.services,
                                             lib::ComponentKind::CheckBox);
        if (!mask_) mask_ = std::make_shared<lib::CheckBox>();
        mask_->text.clear();
        mask_->icon = prop_->images().load(dprops::StringView{"mask.png"});
        mask_->icon_offset_x = -3.0F;
        mask_->set_tooltip(prop_->requiredMsgs().menuTextMask.value);
        mask_->set_layout_item({.row = 0U, .column = column++,
                                .horizontal = lib::Align::Center,
                                .vertical = lib::Align::Center,
                                 .minimum = {40.0F, 24.0F},
                                 .preferred = {40.0F, 24.0F}});
        group->add(mask_);
        mask_->on_changed = [this](bool) {
            markCustomSetting();
            updateMask();
            refreshWarning();
        };
        mod(*mask_);
    }

    add_number(message(prop_->requiredMsgs(), prop_->requiredMsgs().layer), layer_,
               ::cwxeditor::cwx::utils::tryFormat(
                   prop_->requiredMsgs().layerHint.value,
                   background::BgImage::LAYER_BACK_CELL));
    layer_->set_tooltip(::cwxeditor::cwx::utils::tryFormat(prop_->requiredMsgs().layerValues.value,
        background::BgImage::LAYER_BACK_CELL, types::LAYER_MENU_CARD,
        types::LAYER_PLAYER_CARD, types::LAYER_MESSAGE));
    if (include_smoothing) {
        auto smooth_panel = makeComponent<lib::Panel>(
            context_.services, lib::ComponentKind::Panel);
        auto smooth_layout = std::make_shared<lib::GridLayout>();
        smooth_layout->columns = 2U;
        smooth_layout->spacing = 4.0F;
        smooth_panel->set_layout(smooth_layout);
        smooth_panel->set_layout_item({.row = 1U, .column = 0U,
                                       .column_span = columns,
                                       .horizontal = lib::Align::End,
                                       .vertical = lib::Align::Center});
        smooth_panel->add(std::make_shared<lib::Label>(
            message(prop_->requiredMsgs(), prop_->requiredMsgs().smoothing)));
        smoothing_ = makeComponent<lib::ComboBox>(context_.services,
                                                  lib::ComponentKind::ComboBox);
        if (!smoothing_) smoothing_ = std::make_shared<lib::ComboBox>();
        smoothings_ = {types::Smoothing::Default, types::Smoothing::True,
                       types::Smoothing::False};
        for (const auto value : smoothings_)
            smoothing_->items.push_back({smoothingName(prop_->requiredMsgs(), value), {}});
        smoothing_->selected_index = 0U;
        smooth_panel->add(smoothing_);
        group->add(smooth_panel);
        smoothing_->on_selection_changed = [this](std::optional<std::size_t>) {
            markCustomSetting();
            refDataVersion();
        };
        mod(*smoothing_);
    }

    const auto number_changed = [this](bool size_changed) {
        if (ignoreMod()) return;
        markCustomSetting(size_changed);
        refDataVersion();
    };
    for (const auto& control : {x_, y_, w_, h_, layer_}) {
        control->on_changed = [number_changed, size_changed = (control == w_ || control == h_)](double) {
            number_changed(size_changed);
        };
        mod(*control);
    }
    // DのcreatePositionPanelは、数値入力をCenterLayoutで包む。
    centerGroupContent(*group);
    parent.add(group);
    return group;
}

std::shared_ptr<lib::Panel> BgImageDialog::createEasySettingsPanel(
    lib::Container& parent) {
    auto panel = makeComponent<lib::Panel>(context_.services,
                                            lib::ComponentKind::Panel);
    if (!panel) panel = std::make_shared<lib::Panel>();
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 2U;
    layout->equal_width = false;
    panel->set_layout(layout);
    panel->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Start});

    auto names = makeComponent<lib::GroupBox>(context_.services,
                                               lib::ComponentKind::GroupBox);
    if (!names) names = std::make_shared<lib::GroupBox>();
    names->title = message(prop_->requiredMsgs(), prop_->requiredMsgs().bgImageCellName);
    chooser::Commons chooser_comm;
    chooser_comm.services = context_.services;
    chooser_comm.clipboard = context_.services.clipboard;
    chooser_comm.menuText = [this](types::MenuID id) {
        return prop_ ? prop_->buildMenu(id) : lib::String{};
    };
    chooser_comm.menuImage = [this](types::MenuID id) {
        return prop_ ? prop_->images().menu(id) : lib::ResourceId{};
    };
    chooser::ChooserProperties chooser_props;
    chooser_props.editor_commons = context_.editor_commons;
    if (prop_) {
        chooser_props.comboVisibleItemCount = static_cast<std::size_t>(
            std::max<std::int32_t>(1, prop_->var().comboVisibleItemCount.value));
        chooser_props.logicalSort = prop_->var().logicalSort.value;
    }
    const auto initial_name = back_ ? cellName(*back_) : lib::String{};
    cell_name_chooser_ = chooser::detail::createValueCombo<chooser::ChooserCombo>(
        chooser_comm, names.get(), [this] { refDataVersion(); return true; },
        initial_name, [this] {
            return context_.cell_names_provider ? context_.cell_names_provider()
                                                : context_.cell_names;
        }, chooser_props, true);
    cell_name_ = cell_name_chooser_;
    names->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .grow = 1.0F});
    cell_name_->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .grow = 1.0F});
    panel->add(names);

    auto settings_group = makeComponent<lib::GroupBox>(context_.services,
                                                       lib::ComponentKind::GroupBox);
    if (!settings_group) settings_group = std::make_shared<lib::GroupBox>();
    settings_group->title = message(prop_->requiredMsgs(), prop_->requiredMsgs().bgImageSettings);
    easy_ = makeComponent<lib::ComboBox>(context_.services,
                                         lib::ComponentKind::ComboBox);
    if (!easy_) easy_ = std::make_shared<lib::ComboBox>();
    settings_group->add(easy_);
    settings_group->set_layout_item({.horizontal = lib::Align::Start,
                                     .vertical = lib::Align::Stretch});
    panel->add(settings_group);
    parent.add(panel);

    const auto previous_selection = cell_name_->on_selection_changed;
    cell_name_->on_selection_changed = [this, previous_selection](std::optional<std::size_t> value) {
        if (previous_selection) previous_selection(value);
        refDataVersion();
    };
    const auto previous_text = cell_name_->on_text_changed;
    cell_name_->on_text_changed = [this, previous_text](std::string_view value) {
        if (previous_text) previous_text(value);
        refDataVersion();
    };
    mod(*cell_name_);

    refreshBgImageSettings();
    const std::size_t setting_offset = supportsOriginalSetting() ? 2U : 1U;
    easy_->on_selection_changed = [this, setting_offset](std::optional<std::size_t> index) {
        if (!index) return;
        if (original_setting_ && *index == 1U) {
            selectEasySetting();
        } else if (*index >= setting_offset) {
            selectSetting(*index - setting_offset);
        }
        notifyToolbar();
        applyEnabled();
    };
    mod(*easy_);
    // Customは表示の選択であり、モデル編集ではない（DのSettingsListenerの
    // case 0）。mod()の汎用コンボ処理は全選択を変更扱いにするため、その外で除外する。
    const auto apply_setting = easy_->on_selection_changed;
    const auto refresh_toolbar = context_.refresh_toolbar;
    easy_->on_selection_changed = [apply_setting, refresh_toolbar](
        std::optional<std::size_t> index) {
        if (!index) return;
        if (*index == 0U) {
            if (refresh_toolbar) refresh_toolbar();
            return;
        }
        if (apply_setting) apply_setting(index);
    };
    return panel;
}

void BgImageDialog::refreshBgImageSettings() {
    if (!easy_) return;
    const std::size_t offset = supportsOriginalSetting() ? 2U : 1U;
    const auto old_index = easy_->selected_index;
    const auto old_name = old_index && *old_index < easy_->items.size() &&
                                  *old_index >= offset
                              ? easy_->items[*old_index].text
                              : lib::String{};
    settings_.clear();
    if (context_.settings) {
        settings_ = context_.settings();
    } else {
        const auto type = context_.common.skin ? context_.common.skin->type()
                                               : lib::String{};
        settings_ = dprops::arraySnapshot(prop_->bgImageSettings(type));
        if (settings_.empty())
            settings_ = dprops::arraySnapshot(prop_->bgImageSettings({}));
    }
    easy_->items.clear();
    easy_->items.push_back(
        {prop_->requiredMsgs().defaultSelection(message(prop_->requiredMsgs(), prop_->requiredMsgs().bgImageSettingCustom)), {}});
    original_setting_ = supportsOriginalSetting();
    if (original_setting_)
        easy_->items.push_back(
            {prop_->requiredMsgs().defaultSelection(message(prop_->requiredMsgs(), prop_->requiredMsgs().bgImageSettingOriginal)), {}});
    for (const auto& setting : settings_) easy_->items.push_back({setting.name, {}});
    // Dはプリセットの置換時も、組み込みのCustom/Original行を保持する。
    easy_->selected_index = old_index && *old_index < offset ? *old_index : 0U;
    if (!old_name.empty()) {
        for (std::size_t i = 0; i < settings_.size(); ++i) {
            const auto& setting = settings_[i];
            if (setting.name != old_name || !x_ || !y_ || !w_ || !h_ || !layer_)
                continue;
            if (integerValue(x_) != setting.x || integerValue(y_) != setting.y ||
                integerValue(w_) != setting.width || integerValue(h_) != setting.height ||
                integerValue(layer_) != setting.layer ||
                (mask_ && mask_->checked != setting.mask))
                continue;
            easy_->selected_index = offset + i;
            break;
        }
    }
    ++easy_->item_update_revision;
}

Skin::Ptr BgImageDialog::resolveSummSkin(
    const Skin* force_skin, const Skin::Ptr& force_owner,
    const Skin* fallback_skin, const Skin::Ptr& fallback_owner) const noexcept {
    if (force_skin) return retainSkin(force_skin, force_owner);
    if (summ_ && !summ_->scenarioPath().empty() && context_.editor_commons &&
        !editor_commons_lifetime_.expired())
        return context_.editor_commons->skin();
    return retainSkin(fallback_skin, fallback_owner);
}

void BgImageDialog::connectExternalNotifications() {
    if (external_notifications_connected_ || !context_.editor_commons ||
        editor_commons_lifetime_.expired()) return;
    auto& comm = *context_.editor_commons;
    if (summ_ && !summ_->scenarioPath().empty()) {
        del_bg_image_connection_ = comm.delBgImage.add(
            [this](lib::String value) { delBgImage(value); });
        data_version_connection_ = comm.refDataVersion.add(
            [this] { refDataVersion(); });
    }
    target_version_connection_ = comm.refTargetVersion.add(
        [this] { refDataVersion(); });
    skin_connection_ = comm.refSkin.add([this] { refreshBgImageSettings(); updatePreview(); });
    image_scale_connection_ = comm.refImageScale.add([this] { updatePreview(); });
    bg_settings_connection_ = comm.refBgImageSettings.add(
        [this] { refreshBgImageSettings(); });
    cell_names_connection_ = comm.refCellNames.add([this] {
        if (cell_name_chooser_ && cell_name_chooser_->refresh)
            cell_name_chooser_->refresh();
    });
    external_notifications_connected_ = true;
}

void BgImageDialog::disconnectExternalNotifications() noexcept {
    if (!external_notifications_connected_ || !context_.editor_commons) return;
    if (editor_commons_lifetime_.expired()) {
        del_bg_image_connection_ = data_version_connection_ =
            target_version_connection_ = skin_connection_ =
                bg_settings_connection_ = cell_names_connection_ = 0U;
        external_notifications_connected_ = false;
        return;
    }
    auto& comm = *context_.editor_commons;
    comm.delBgImage.remove(del_bg_image_connection_);
    comm.refDataVersion.remove(data_version_connection_);
    comm.refTargetVersion.remove(target_version_connection_);
    comm.refSkin.remove(skin_connection_);
    comm.refImageScale.remove(image_scale_connection_);
    comm.refBgImageSettings.remove(bg_settings_connection_);
    comm.refCellNames.remove(cell_names_connection_);
    del_bg_image_connection_ = data_version_connection_ =
        target_version_connection_ = skin_connection_ =
            bg_settings_connection_ = cell_names_connection_ = 0U;
    external_notifications_connected_ = false;
}

void BgImageDialog::createPosPanel(lib::Container& parent, bool include_mask,
                                   bool include_smoothing) {
    (void)createPositionPanel(parent, include_mask, include_smoothing);
    (void)createEasySettingsPanel(parent);
}

void BgImageDialog::setFirstParams() {
    ignoreMod(true);
    if (back_) {
        if (flag_) setComboValue(*flag_, back_->flag());
        if (flag_chooser_) flag_chooser_->selected(back_->flag());
        if (x_) x_->value = back_->x();
        if (y_) y_->value = back_->y();
        if (w_) w_->value = back_->width();
        if (h_) h_->value = back_->height();
        if (mask_) mask_->checked = back_->mask();
        if (layer_) layer_->value = back_->layer();
        if (cell_name_) setComboValue(*cell_name_, cellName(*back_));
    } else {
        if (flag_) setComboValue(*flag_, {});
        if (flag_chooser_) flag_chooser_->selected({});
        if (x_) x_->value = 0.0;
        if (y_) y_->value = 0.0;
        if (w_) w_->value = 0.0;
        if (h_) h_->value = 0.0;
        if (mask_) mask_->checked = false;
        if (layer_) layer_->value = background::BgImage::LAYER_BACK_CELL;
        if (cell_name_) setComboValue(*cell_name_, {});
    }
    ignoreMod(false);
}

void BgImageDialog::applyParams(BgImage& back) {
    if (flag_chooser_) back.flag(flag_chooser_->selected());
    else if (flag_) back.flag(comboValue(*flag_));
    if (x_) back.x(integerValue(x_));
    if (y_) back.y(integerValue(y_));
    if (w_) back.width(integerValue(w_));
    if (h_) back.height(integerValue(h_));
    if (mask_) back.mask(mask_->checked);
    if (layer_) back.layer(integerValue(layer_));
    if (cell_name_) {
        const auto value = comboValue(*cell_name_);
        if (back.cellName() != value) {
            back.cellName(value);
            if (summ_ && context_.refresh_cell_names) context_.refresh_cell_names();
        }
    }
    create_ = false;
}

void BgImageDialog::refDataVersion() {
    const bool legacy = summ_ && summ_->legacy();
    if (layer_) {
        layer_->set_enabled(!legacy || integerValue(layer_) !=
                            background::BgImage::LAYER_BACK_CELL);
    }
    if (cell_name_) {
        cell_name_->set_enabled(!legacy || !comboValue(*cell_name_).empty());
    }
    if (smoothing_ && smoothing_->selected_index &&
        *smoothing_->selected_index < smoothings_.size()) {
        smoothing_->set_enabled(!legacy ||
                                smoothings_[*smoothing_->selected_index] !=
                                    types::Smoothing::Default);
    }
    refreshWarning();
}

std::vector<lib::String> BgImageDialog::warningCommon() const {
    std::vector<lib::String> result;
    const auto cell_name = cell_name_ ? comboValue(*cell_name_) : std::string{};
    const auto encoding_warnings = warning::sjisWarnings(
        prop_->parent(), summ_, cell_name, prop_->requiredMsgs().bgImageCellName.value);
    result.insert(result.end(), encoding_warnings.begin(), encoding_warnings.end());
    if (!cell_name.empty() && summ_ &&
        !prop_->isTargetVersion(summ_, "1")) {
        result.push_back(message(prop_->requiredMsgs(), prop_->requiredMsgs().warningBgImageCellName));
    }
    if (layer_ && integerValue(layer_) != background::BgImage::LAYER_BACK_CELL &&
        !prop_->isTargetVersion(summ_, "1")) {
        // Dのbgimagedialog.d94は、Summaryがない既定背景
        // でも全体の対象バージョンを使う。全ホストで同じ共通の警告規則を使う。
        result.push_back(message(prop_->requiredMsgs(), prop_->requiredMsgs().warningLayer));
    }
    if (smoothing_ && smoothing_->selected_index &&
        *smoothing_->selected_index < smoothings_.size() &&
        smoothings_[*smoothing_->selected_index] != types::Smoothing::Default &&
        !prop_->isTargetVersion(summ_, "2")) {
        // 隣接するD98の平滑化警告も、同じSummaryなしの場合の規則を使う。
        result.push_back(message(prop_->requiredMsgs(), prop_->requiredMsgs().warningBgImageSmoothing));
    }
    return result;
}

void BgImageDialog::refreshWarning() {
    warnings_ = warningCommon();
    warning(warnings_);
    if (context_.refresh_warnings) context_.refresh_warnings();
}

void BgImageDialog::updateMask() {}

void BgImageDialog::selectEasySetting() {}

void BgImageDialog::delBgImage(std::string_view cwx_path) {
    if (back_ && back_->cwxPath(true) == cwx_path) forceCancel();
}

void BgImageDialog::notifyToolbar() const {
    if (context_.refresh_toolbar) context_.refresh_toolbar();
}

void BgImageDialog::markCustomSetting(bool size_changed) {
    // DはSModLを幅・高さだけに設定する。位置、レイヤー、マスク、平滑
    // 化の編集では、最初の有効画像によるサイズ初期化を保留したままにする。
    if (size_changed) selected_ = true;
    if (!size_changed && original_setting_ && easy_ && easy_->selected_index == 1U) return;
    if (easy_ && !ignoreMod() && easy_->selected_index &&
        *easy_->selected_index != 0U) {
        easy_->selected_index = 0U;
    }
}

std::string BgImageDialog::comboValue(const lib::ComboBox& combo) const {
    if (combo.editable && !combo.edit_text.empty()) return combo.edit_text;
    if (combo.selected_index && *combo.selected_index < combo.items.size()) {
        const auto& item = combo.items[*combo.selected_index];
        return item.value.empty() ? item.text : item.value;
    }
    return combo.editable ? combo.edit_text : std::string{};
}

void BgImageDialog::setComboValue(lib::ComboBox& combo,
                                  std::string_view value) {
    combo.edit_text = std::string(value);
    combo.selected_index.reset();
    for (std::size_t index = 0; index < combo.items.size(); ++index) {
        if (combo.items[index].value == value || combo.items[index].text == value) {
            combo.selected_index = index;
            break;
        }
    }
    if (!combo.editable && !combo.selected_index && !combo.items.empty())
        combo.selected_index = 0U;
}

void BgImageDialog::selectSetting(std::size_t index) {
    if (index >= settings_.size()) return;
    const auto& setting = settings_[index];
    if (x_) x_->value = setting.x;
    if (y_) y_->value = setting.y;
    if (w_) w_->value = setting.width;
    if (h_) h_->value = setting.height;
    if (mask_) mask_->checked = setting.mask;
    if (layer_) layer_->value = setting.layer;
    selected_ = true;
    updateMask();
    refDataVersion();
}

void BgImageDialog::addCommonControls(lib::Container&, bool) {}

ImageCellDialog::ImageCellDialog(BgImageDialogContext context, Props& prop,
                                 const Summary* summ, const Skin* force_skin,
                                 ImageCellPtr back, bool create,
                                 std::shared_ptr<lib::Window> parent,
                                 UseCounterPtr counter)
    : BgImageDialog(std::move(context), prop, summ, std::move(parent),
                     create ? prop.requiredMsgs().dlgTitNewBgImage.value
                            : prop.requiredMsgs().dlgTitBgImage.value,
                     prop.images().load(dprops::StringView{"backs.png"}), true,
                     summ ? static_cast<DSize*>(&prop.flex().areaBackgroundDlg)
                          : static_cast<DSize*>(&prop.flex().areaBackgroundNFDlg),
                     back, create, std::move(counter)),
      summ_skin_(context_.common.skin), force_skin_(force_skin),
      summ_skin_owner_(retainSkin(summ_skin_, context_.skin_owner)),
      force_skin_owner_(retainSkin(force_skin_, context_.skin_owner)),
      image_back_(std::move(back)) {}

ImageCellDialog::ImageCellDialog(areaviewutils::Commons common, Props& prop,
                                 std::shared_ptr<lib::Window> parent,
                                 const Summary* summ, const Skin* force_skin,
                                 UseCounterPtr counter, ImageCellPtr back,
                                 bool create)
    : ImageCellDialog(BgImageDialogContext(std::move(common)), prop, summ,
                      force_skin, std::move(back), create, std::move(parent),
                      std::move(counter)) {}

ImageCellPtr ImageCellDialog::imageBack() const noexcept {
    return image_back_;
}

void ImageCellDialog::setupSpecific(Container& area) {
    image_path_ = std::make_shared<lib::TextEdit>();
    const auto sample_width =
        static_cast<int>(prop_->var().bgImageSampleWidth.value);
    const auto sample_height =
        static_cast<int>(prop_->var().bgImageSampleHeight.value);
    image_select_ = std::make_unique<imageselect::ImageSelectBase>(
        imageselect::ImageSelectKind::Background, false, *prop_,
        const_cast<Summary*>(summ_), sample_width, sample_height,
        structs::CInsets{}, types::CardImagePosition::TopLeft, false,
        [] { return lib::String{}; }, [this] { selectEasySetting(); },
        imageselect::ImageSelectBase::Definitions{}, false,
        force_skin_owner_,
        context_.services);
    // Dはモードレスの一覧へEditorのCommonsを渡し、実
    // 行中の画像倍率の設定変更が背景画像の選択部へ届くようにする。
    image_select_->bindEditorCommons(context_.editor_commons);
    closeEvent.push_back([this] {
        if (image_select_) image_select_->active(false);
    });
    openedEvent.push_back([this] {
        if (image_select_) image_select_->active(true);
    });
    area.add(image_select_->widget());
    image_select_->modEvent.push_back([this] {
        if (image_path_) image_path_->text = image_select_->backgroundImage();
        if (!ignoreMod()) {
            markCustomSetting();
            applyEnabled();
        }
        refreshWarning();
    });
    image_path_->on_changed = [this](std::string_view) {
        if (image_select_ &&
            image_select_->backgroundImage() != image_path_->text) {
            image_select_->setBackgroundImage(image_path_->text);
        }
        if (!ignoreMod()) {
            markCustomSetting();
        }
        refreshWarning();
    };
    mod(*image_path_);
}

void ImageCellDialog::initializeSpecific() {
    if (image_back_) setBack(image_back_);
    if (!image_path_) return;
    image_path_->text = image_back_ ? image_back_->path() : lib::String{};
    if (image_select_) {
        image_select_->setBackgroundImage(image_path_->text, false);
        image_select_->mask(image_back_ && image_back_->mask());
    }
    if (smoothing_) {
        const auto smoothing = image_back_ ? image_back_->smoothing()
                                           : types::Smoothing::Default;
        smoothing_->selected_index = enumIndex(smoothings_, smoothing);
        if (*smoothing_->selected_index >= smoothings_.size())
            smoothing_->selected_index = 0U;
    }
}

void ImageCellDialog::refreshWarning() {
    warnings_ = warningCommon();
    const auto image = image_select_
                           ? image_select_->backgroundImage()
                           : (image_path_ ? image_path_->text : lib::String{});
    if (!prop_->targetVersion(summ_, "1.60") && utils::isBinImg(image)) {
        warnings_.push_back(message(prop_->requiredMsgs(), prop_->requiredMsgs().warningBgImageIncluded));
    }
    if (image_select_) {
        const auto image_warnings = image_select_->warnings();
        warnings_.insert(warnings_.end(), image_warnings.begin(),
                         image_warnings.end());
    }
    warning(warnings_);
    if (context_.refresh_warnings) context_.refresh_warnings();
}

void ImageCellDialog::updateMask() {
    if (image_select_ && mask_) image_select_->mask(mask_->checked);
}

const Skin* ImageCellDialog::summSkin() const noexcept {
    resolved_summ_skin_owner_ = resolveSummSkin(
        force_skin_, force_skin_owner_, summ_skin_, summ_skin_owner_);
    return resolved_summ_skin_owner_.get();
}

void ImageCellDialog::selectEasySetting() {
    if (selected_ && !(easy_ && isSelected(easy_, 1U))) return;
    const auto image = image_select_
                           ? image_select_->backgroundImage()
                           : (image_path_ ? image_path_->text : lib::String{});
    std::optional<lib::Size> size;
    if (context_.image_size) size = context_.image_size(image);
    else {
        std::uint32_t width{}, height{};
        const auto skin_owner = resolveSummSkin(force_skin_, force_skin_owner_,
                                                summ_skin_, summ_skin_owner_);
        const auto* skin = skin_owner.get();
        const auto file = image_select_ ? image_select_->backgroundFilePath() : image;
        if (dutils::dwtImageSize(prop_, skin, summ_, file, width, height, context_.services))
            size = lib::Size{static_cast<float>(width), static_cast<float>(height)};
    }
    if (!size) return;
    ignoreMod(true);
    if (w_) w_->value = std::clamp(static_cast<double>(size->width),
                                   w_->minimum, w_->maximum);
    if (h_) h_->value = std::clamp(static_cast<double>(size->height),
                                   h_->minimum, h_->maximum);
    ignoreMod(false);
    selected_ = true;
    notifyToolbar();
}

bool ImageCellDialog::apply() {
    auto selected = MaterialResult{
        false, image_select_
                   ? image_select_->backgroundImage()
                   : (image_path_ ? image_path_->text : lib::String{})};
    if (image_select_) {
        const auto material =
            image_select_->backgroundMaterialPath(forceApplying());
        selected = {material.cancel, material.images};
    } else if (context_.select_material) {
        selected = context_.select_material(selected.path);
    }
    if (selected.cancel) return false;
    if (selected.path.empty() && image_path_) selected.path = image_path_->text;
    if (!image_back_) {
        image_back_ = std::make_shared<ImageCell>();
        if (counter_) image_back_->setUseCounter(counter_);
    }
    image_back_->path(selected.path);
    if (smoothing_ && smoothing_->selected_index &&
        *smoothing_->selected_index < smoothings_.size()) {
        image_back_->smoothing(smoothings_[*smoothing_->selected_index]);
    }
    applyParams(*image_back_);
    setBack(image_back_);
    title(prop_->requiredMsgs().dlgTitBgImage.value);
    return true;
}

TextCellDialog::TextCellDialog(BgImageDialogContext context, Props& prop,
                               const Summary* summ, const Skin* force_skin,
                               TextCellPtr back, bool create,
                               std::shared_ptr<lib::Window> parent,
                               UseCounterPtr counter)
    : BgImageDialog(std::move(context), prop, summ, std::move(parent),
                     create ? prop.requiredMsgs().dlgTitNewTextCell.value
                            : prop.requiredMsgs().dlgTitTextCell.value,
                     prop.images().load(dprops::StringView{"text_cell.png"}), true,
                     summ ? static_cast<DSize*>(&prop.flex().areaTextCellDlg)
                          : static_cast<DSize*>(&prop.flex().areaTextCellNFDlg),
                     back,
                     create, std::move(counter)),
      summ_skin_(context_.common.skin), force_skin_(force_skin),
      summ_skin_owner_(retainSkin(summ_skin_, context_.skin_owner)),
      force_skin_owner_(retainSkin(force_skin_, context_.skin_owner)),
      text_back_(std::move(back)) {
    enterClose(false);
    // DのareaTextCell[NF]Dialogの既定はShellサイ
    // ズ750×650で、配置は参照画像のクライアント領域722×600に収
    // まる。クライアント領域自体の最小値を750×650にするとDより大きく
    // なり、縮小もできない。論理単位で指定し、UI倍率は表示側で適用する。
    dialog()->minimum_size = lib::Size{600.0F, 520.0F};
}

TextCellDialog::TextCellDialog(areaviewutils::Commons common, Props& prop,
                               std::shared_ptr<lib::Window> parent,
                               const Summary* summ, const Skin* force_skin,
                               UseCounterPtr counter, TextCellPtr back,
                               bool create)
    : TextCellDialog(BgImageDialogContext(std::move(common)), prop, summ,
                     force_skin, std::move(back), create, std::move(parent),
                     std::move(counter)) {}

TextCellPtr TextCellDialog::textBack() const noexcept {
    return text_back_;
}

void TextCellDialog::setup(Container& area) {
    // DのTextCellDialog.setupと
    // 参照表示に合わせた配置。縦分割（textCellVSashT:B
    // ）の上側は、画像・フォント・縁取りとスタイ
    // ル・文字色・縁色の列、その右にフラグ参照（
    // HSashL:R）を置く。下側はテキストと
    // 挿入バー、その右にプレビュー値（PreviewSashL:R
    // ）。さらに更新方式と中央配置の位置指定
    // 、セル名と簡単設定を並べる。挿入バー
    // をテキスト欄と同じセルに重ねず、全推
    // 奨幅でダイアログを無制限に広げない。
    connectExternalNotifications();
    auto root_layout = std::make_shared<lib::GridLayout>();
    root_layout->columns = 1U;
    root_layout->spacing = 0.0F;
    area.set_layout(root_layout);

    auto body = makeComponent<lib::Panel>(context_.services,
                                          lib::ComponentKind::Panel);
    auto body_layout = std::make_shared<lib::GridLayout>();
    body_layout->columns = 1U;
    body_layout->equal_width = false;
    body_layout->spacing = 2.0F;
    body->set_layout(body_layout);
    body->set_layout_item({.row = 0U, .column = 0U,
                           .horizontal = lib::Align::Stretch,
                           .vertical = lib::Align::Stretch,
                           .grow = 1.0F});
    area.add(body);
    setupSpecific(*body);

    auto update_and_position = makeComponent<lib::Panel>(
        context_.services, lib::ComponentKind::Panel);
    auto footer_layout = std::make_shared<lib::GridLayout>();
    footer_layout->columns = 2U;
    footer_layout->equal_width = false;
    footer_layout->spacing = 4.0F;
    update_and_position->set_layout(footer_layout);
    update_and_position->set_layout_item({.row = 1U, .column = 0U,
                                          .horizontal = lib::Align::Stretch,
                                          .vertical = lib::Align::Start});
    body->add(update_and_position);

    auto update_layout = std::make_shared<lib::GridLayout>();
    update_layout->columns = 1U;
    auto update_group = makeGroup(
        context_.services, message(prop_->requiredMsgs(), prop_->requiredMsgs().updateType),
        update_layout);
    update_group->set_layout_item({.row = 0U, .column = 0U,
                                   .horizontal = lib::Align::Stretch,
                                   .vertical = lib::Align::Stretch,
                                   .minimum = {kTextCellUpdateWidth, 0.0F},
                                   .preferred = {kTextCellUpdateWidth, 0.0F}});
    update_type_ = makeComponent<lib::ComboBox>(context_.services,
                                                lib::ComponentKind::ComboBox);
    update_types_ = {types::UpdateType::Fixed, types::UpdateType::Variables,
                     types::UpdateType::All};
    for (const auto value : update_types_)
        update_type_->items.push_back({updateName(prop_->requiredMsgs(), value), {}});
    update_type_->selected_index = 1U;
    update_type_->set_layout_item({.horizontal = lib::Align::Stretch,
                                   .vertical = lib::Align::Center,
                                   .grow = 1.0F});
    update_group->add(update_type_);
    update_and_position->add(update_group);
    if (auto position = createPositionPanel(*update_and_position, false, false)) {
        position->set_layout_item({.row = 0U, .column = 1U,
                                   .horizontal = lib::Align::Stretch,
                                   .vertical = lib::Align::Stretch,
                                   .grow = 1.0F});
    }
    if (auto easy = createEasySettingsPanel(*body)) {
        easy->set_layout_item({.row = 2U, .column = 0U,
                               .horizontal = lib::Align::Stretch,
                               .vertical = lib::Align::Start});
    }

    update_type_->on_selection_changed = [this](std::optional<std::size_t>) {
        refDataVersion();
    };
    mod(*update_type_);


    setFirstParams();
    initializeSpecific();
    refDataVersion();
}

void TextCellDialog::setupSpecific(Container& area) {
    auto vertical_split = makeComponent<lib::SplitView>(
        context_.services, lib::ComponentKind::SplitView);
    vertical_split->orientation = lib::Orientation::Vertical;
    vertical_split->weights = {
        static_cast<float>(std::max(1, prop_->var().textCellVSashT.value)),
        static_cast<float>(std::max(1, prop_->var().textCellVSashB.value))};
    vertical_split->on_weights_changed = [this](const std::vector<float>& value) {
        if (value.size() < 2U) return;
        prop_->var().textCellVSashT = static_cast<std::int32_t>(
            std::max(1.0F, std::round(value[0])));
        prop_->var().textCellVSashB = static_cast<std::int32_t>(
            std::max(1.0F, std::round(value[1])));
    };
    vertical_split->set_layout_item({.row = 0U, .column = 0U,
                                     .horizontal = lib::Align::Stretch,
                                     .vertical = lib::Align::Stretch,
                                     .grow = 1.0F});
    area.add(vertical_split);

    std::shared_ptr<lib::Container> top_parent = vertical_split;
    if (summ_) {
        auto top_split = makeComponent<lib::SplitView>(
            context_.services, lib::ComponentKind::SplitView);
        top_split->orientation = lib::Orientation::Horizontal;
        top_split->weights = {
            static_cast<float>(std::max(1, prop_->var().textCellHSashL.value)),
            static_cast<float>(std::max(1, prop_->var().textCellHSashR.value))};
        top_split->on_weights_changed = [this](const std::vector<float>& value) {
            if (value.size() < 2U) return;
            prop_->var().textCellHSashL = static_cast<std::int32_t>(
                std::max(1.0F, std::round(value[0])));
            prop_->var().textCellHSashR = static_cast<std::int32_t>(
                std::max(1.0F, std::round(value[1])));
        };
        vertical_split->add(top_split);
        top_parent = top_split;
    }

    // 二列の一つのグリッドでスタイル・色の列をそろえる。Dは
    // 二つの入れ子グリッドを使うが、右セルは同じ幅になる。
    auto settings = makeComponent<lib::Panel>(context_.services,
                                               lib::ComponentKind::Panel);
    auto settings_layout = std::make_shared<lib::GridLayout>();
    settings_layout->columns = 2U;
    settings_layout->equal_width = false;
    settings_layout->spacing = 4.0F;
    settings->set_layout(settings_layout);
    settings->set_layout_item({.horizontal = lib::Align::Stretch,
                               .vertical = lib::Align::Stretch,
                               .grow = 1.0F});
    top_parent->add(settings);

    auto preview_layout = std::make_shared<lib::GridLayout>();
    preview_layout->columns = 1U;
    auto preview_group = makeGroup(
        context_.services, message(prop_->requiredMsgs(), prop_->requiredMsgs().image),
        preview_layout);
    // SWTのGridData.FILL_BOTHに合わせ、画像が余った幅と高さを使う。
    preview_group->set_layout_item({.row = 0U, .column = 0U,
                                    .horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Stretch,
                                    .grow = 1.0F});
    preview_ = makeComponent<lib::Canvas>(context_.services,
                                          lib::ComponentKind::Canvas);
    // DのCanvasが同じ設定済みクライアントサイズとPileImageの規
    // 則を使うのと同様に、指定プレビューサイズと描画規則を全ホストで共有する。
    setPreferred(preview_,
                 static_cast<float>(prop_->var().textCellPreviewWidth.value),
                 static_cast<float>(prop_->var().textCellPreviewHeight.value), 1.0F);
    preview_->on_paint_context = [this, life=std::weak_ptr<void>(preview_lifetime_)](
                                     lib::DrawingContext& drawing, lib::Rect dirty) {
        if (life.expired()) return;
        const auto revision=preview_revision_;
        const auto text_cache=text_cache_;
        const auto current=[this,life,revision] {
            return !life.expired() && preview_revision_==revision;
        };
        // DのGCはダイアログを生存させる。共通描画の境界でバックエンドが
        // この単独所有元を終了させ得るため、呼び出し前に状態を取得する。
        auto font=dutils::createFontFromPixels(
            prop_->adjustFont(font_name_ ? comboValue(*font_name_) : lib::String{}),
            std::max(1,integerValue(size_,18)),bold_ && bold_->checked,
            italic_ && italic_->checked,underline_ && underline_->checked,
            strike_ && strike_->checked,!antialias_ || antialias_->checked);
        font.content_font=true;
        const auto border=bordering_type_ && bordering_type_->selected_index &&
                *bordering_type_->selected_index<bordering_types_.size()
            ? bordering_types_[*bordering_type_->selected_index] : types::BorderingType::None;
        const auto bounds=preview_->bounds();
        const lib::Rect canvas{0,0,bounds.width,bounds.height};
        cellrender::Text text{preview_description_,font,portableColor(colorFrom(color_)),
            portableColor(colorFrom(bordering_color_)),border,
            static_cast<float>(std::max(1,integerValue(bordering_width_,1))),
            vertical_ && vertical_->checked,context_.services.graphics,font.antialias};
        drawing.save();
        struct Restore {lib::DrawingContext& draw;~Restore(){draw.restore();}} restore{drawing};
        if (!current()) return;
        drawing.clip(dirty);if (!current()) return;
        drawing.clear({255,255,255,255});if (!current()) return;
        cellrender::draw_text(drawing,canvas,{0,0},std::move(text),current,text_cache);
    };
    preview_group->add(preview_);
    settings->add(preview_group);

    const auto side_item = [](std::size_t row, bool grow_row) {
        return lib::LayoutItem{.row = row, .column = 1U,
                               .horizontal = lib::Align::Stretch,
                               .vertical = lib::Align::Stretch,
                               .minimum = {kTextCellSideWidth, 0.0F},
                               .preferred = {kTextCellSideWidth, 0.0F},
                               .grow = grow_row ? 1.0F : 0.0F};
    };

    auto style_layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    // 共通の間隔指定で、Dの一つのGridLayout規則を全ホストに適用する。
    style_layout->spacing = 4.0F;
    auto style_group = makeGroup(
        context_.services, message(prop_->requiredMsgs(), prop_->requiredMsgs().fontStyle),
        style_layout);
    style_group->set_layout_item(side_item(0U, true));
    bold_ = std::make_shared<lib::CheckBox>(message(prop_->requiredMsgs(), prop_->requiredMsgs().bold));
    italic_ = std::make_shared<lib::CheckBox>(message(prop_->requiredMsgs(), prop_->requiredMsgs().italic));
    underline_ = std::make_shared<lib::CheckBox>(message(prop_->requiredMsgs(), prop_->requiredMsgs().underline));
    strike_ = std::make_shared<lib::CheckBox>(message(prop_->requiredMsgs(), prop_->requiredMsgs().strike));
    vertical_ = std::make_shared<lib::CheckBox>(message(prop_->requiredMsgs(), prop_->requiredMsgs().vertical));
    antialias_ = std::make_shared<lib::CheckBox>(message(prop_->requiredMsgs(), prop_->requiredMsgs().antialias));
    for (const auto& control : {bold_, italic_, underline_, strike_, vertical_, antialias_})
        style_group->add(control);
    settings->add(style_group);

    const auto fixed_label = [](const lib::String& text, std::size_t column) {
        auto label = std::make_shared<lib::Label>(text);
        label->set_layout_item({.row = 0U, .column = column,
                                .horizontal = lib::Align::Start,
                                .vertical = lib::Align::Center});
        return label;
    };
    const auto spinner_item = [](std::size_t column) {
        return lib::LayoutItem{.row = 0U, .column = column,
                               .horizontal = lib::Align::Start,
                               .vertical = lib::Align::Center,
                               .minimum = {kTextCellSpinnerWidth, 0.0F},
                               .preferred = {kTextCellSpinnerWidth, 0.0F}};
    };

    // フォント欄: 名前（残り幅）| サイズ: | 数値 | ピクセル。
    auto font_layout = std::make_shared<lib::GridLayout>();
    font_layout->columns = 4U;
    font_layout->equal_width = false;
    font_layout->spacing = 4.0F;
    auto font_group = makeGroup(
        context_.services, message(prop_->requiredMsgs(), prop_->requiredMsgs().font), font_layout);
    font_group->set_layout_item({.row = 1U, .column = 0U,
                                 .horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Stretch});
    font_name_ = makeComponent<lib::ComboBox>(context_.services,
                                              lib::ComponentKind::ComboBox);
    font_name_->editable = false;
    font_name_->set_layout_item({.row = 0U, .column = 0U,
                                 .horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Center,
                                 .grow = 1.0F});
    std::vector<lib::String> fonts;
    if (context_.services.graphics)
        fonts = context_.services.graphics->font_families();
    // Dに合わせ、インストール済みの表示用フォントに、標準Windows名と縦
    // 書き形式を加える。ユーザーの既定設定から存在しないフォントを作らない。
    for (const auto& name : {prop_->looks().gothic(true), prop_->looks().pgothic(true),
            prop_->looks().mincho(true), prop_->looks().pmincho(true), prop_->looks().uigothic(true)}) {
        fonts.push_back(name);
        fonts.push_back("@" + name);
    }
    std::sort(fonts.begin(), fonts.end(), [this](const lib::String& a, const lib::String& b) {
        return (prop_->var().logicalSort.value ? ::cwxeditor::cwx::utils::fnncmp(a, b)
                                              : ::cwxeditor::cwx::utils::fncmp(a, b)) < 0;
    });
    fonts.erase(std::unique(fonts.begin(), fonts.end()), fonts.end());
    for (const auto& font : fonts) {
        if (!font.empty()) font_name_->items.push_back({font, font});
    }
    size_ = makeComponent<lib::NumberEdit>(context_.services,
                                           lib::ComponentKind::NumberEdit);
    size_->minimum = 1.0;
    size_->maximum = static_cast<double>(prop_->var().fontSizeMax.value);
    size_->set_layout_item(spinner_item(2U));
    font_group->add(font_name_);
    font_group->add(fixed_label(message(prop_->requiredMsgs(), prop_->requiredMsgs().size), 1U));
    font_group->add(size_);
    font_group->add(fixed_label(message(prop_->requiredMsgs(), prop_->requiredMsgs().pixel), 3U));
    settings->add(font_group);

    auto color_layout = std::make_shared<lib::GridLayout>();
    color_layout->columns = 1U;
    auto color_group = makeGroup(
        context_.services, message(prop_->requiredMsgs(), prop_->requiredMsgs().fontColor),
        color_layout);
    color_group->set_layout_item(side_item(1U, false));
    color_ = ColorPicker(*prop_, *color_group, false);
    if (const auto& picker = color_.widget())
        picker->set_layout_item({.horizontal = lib::Align::Start,
                                 .vertical = lib::Align::Center});
    settings->add(color_group);

    // 縁取りはDのCenterLayoutで、種別・幅ラベル・幅入力・ピクセル単位を包む。
    auto border_layout = std::make_shared<lib::GridLayout>();
    border_layout->columns = 1U;
    auto border_group = makeGroup(
        context_.services, message(prop_->requiredMsgs(), prop_->requiredMsgs().bordering),
        border_layout);
    border_group->set_layout_item({.row = 2U, .column = 0U,
                                   .horizontal = lib::Align::Stretch,
                                   .vertical = lib::Align::Stretch});
    auto border_row = makeComponent<lib::Panel>(context_.services,
                                                lib::ComponentKind::Panel);
    auto border_row_layout = std::make_shared<lib::GridLayout>();
    border_row_layout->columns = 4U;
    border_row_layout->equal_width = false;
    border_row_layout->spacing = 4.0F;
    border_row->set_layout(border_row_layout);
    border_row->set_layout_item({.horizontal = lib::Align::Center,
                                 .vertical = lib::Align::Center,
                                 .grow = 1.0F});
    bordering_type_ = makeComponent<lib::ComboBox>(context_.services,
                                                   lib::ComponentKind::ComboBox);
    bordering_types_ = {types::BorderingType::None, types::BorderingType::Outline,
                         types::BorderingType::Inline};
    for (const auto value : bordering_types_)
        bordering_type_->items.push_back({borderingName(prop_->requiredMsgs(), value), {}});
    bordering_type_->selected_index = 0U;
    bordering_type_->set_layout_item({.row = 0U, .column = 0U,
                                      .horizontal = lib::Align::Start,
                                      .vertical = lib::Align::Center});
    bordering_width_ = makeComponent<lib::NumberEdit>(context_.services,
                                                      lib::ComponentKind::NumberEdit);
    bordering_width_->minimum = 1.0;
    bordering_width_->maximum = static_cast<double>(prop_->var().borderingWidthMax.value);
    bordering_width_->set_layout_item(spinner_item(2U));
    border_row->add(bordering_type_);
    border_row->add(fixed_label(message(prop_->requiredMsgs(), prop_->requiredMsgs().borderingWidth), 1U));
    border_row->add(bordering_width_);
    border_row->add(fixed_label(message(prop_->requiredMsgs(), prop_->requiredMsgs().pixel), 3U));
    border_group->add(border_row);
    settings->add(border_group);

    auto border_color_layout = std::make_shared<lib::GridLayout>();
    border_color_layout->columns = 1U;
    auto border_color_group = makeGroup(
        context_.services, message(prop_->requiredMsgs(), prop_->requiredMsgs().borderingColor),
        border_color_layout);
    border_color_group->set_layout_item(side_item(2U, false));
    bordering_color_ = ColorPicker(*prop_, *border_color_group, false);
    if (const auto& picker = bordering_color_.widget())
        picker->set_layout_item({.horizontal = lib::Align::Start,
                                 .vertical = lib::Align::Center});
    settings->add(border_color_group);

    if (summ_) {
        const auto flag_group = createFlagPanel(*top_parent);
    }

    auto lower = makeComponent<lib::SplitView>(context_.services,
                                               lib::ComponentKind::SplitView);
    lower->orientation = lib::Orientation::Horizontal;
    lower->weights = {
        static_cast<float>(std::max(1, prop_->var().textCellPreviewSashL.value)),
        static_cast<float>(std::max(1, prop_->var().textCellPreviewSashR.value))};
    lower->on_weights_changed = [this](const std::vector<float>& value) {
        if (value.size() < 2U) return;
        prop_->var().textCellPreviewSashL = static_cast<std::int32_t>(
            std::max(1.0F, std::round(value[0])));
        prop_->var().textCellPreviewSashR = static_cast<std::int32_t>(
            std::max(1.0F, std::round(value[1])));
    };
    vertical_split->add(lower);

    // テキスト欄は二列にまたがり、その下の行を特殊文字バーとフラグ・ステッ
    // プバーで共有する（Dのgd1.horizontalSpan=2）。
    auto text_layout = std::make_shared<lib::GridLayout>();
    text_layout->columns = 2U;
    text_layout->equal_width = false;
    text_layout->spacing = 3.0F;
    auto text_group = makeGroup(
        context_.services, message(prop_->requiredMsgs(), prop_->requiredMsgs().text), text_layout);
    text_ = makeComponent<lib::TextEdit>(context_.services,
                                         lib::ComponentKind::TextEdit);
    text_->multiline = true;
    text_->wrap = false;
    text_->horizontal_scroll = true;
    text_->tab_width = static_cast<std::size_t>(std::max(1, prop_->var().textTabs.value));
    text_->set_layout_item({.row = 0U, .column = 0U, .column_span = 2U,
                            .horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .minimum = {
                                static_cast<float>(prop_->var().textCellBoxWidth.value),
                                static_cast<float>(prop_->var().textCellBoxHeight.value)},
                            .preferred = {
                                static_cast<float>(prop_->var().textCellBoxWidth.value),
                                static_cast<float>(prop_->var().textCellBoxHeight.value)},
                            .grow = 1.0F});
    text_group->add(text_);
    const auto insert = [this](std::string_view value) {
        if (!text_) return;
        const auto start = std::min(text_->selection_start, text_->text.size());
        const auto length = std::min(text_->selection_length,
                                     text_->text.size() - start);
        text_->text.replace(start, length, value);
        text_->selection_start = start + value.size();
        text_->selection_length = 0U;
        if (text_->on_changed) text_->on_changed(text_->text);
    };
    messageutils::MessageContext message_context;
    message_context.services = context_.services;
    message_context.editor_commons = context_.editor_commons;
    message_context.props = prop_;
    message_context.summary = summ_;
    // Dは背景テキストの変数操作部とPreviewValuesにnull
    // を渡す。counter_はモデル参照の管理用に保持するが、イベ
    // ント内のローカル変数を背景文字や表示フラグの解決に使わない。
    message_context.on_changed = [this] { updatePreview(); };
    // 両補助部品の既定セルは(0,0)で、ここではテキスト欄の位置になる。
    const auto place_bar = [&text_group](std::size_t before, std::size_t column,
                                         lib::Align horizontal, float grow) {
        const auto& children = text_group->children();
        if (children.size() <= before) return;
        auto item = children.back()->layout_item();
        item.row = 1U;
        item.column = column;
        item.column_span = 1U;
        item.horizontal = horizontal;
        item.vertical = lib::Align::Center;
        item.grow = grow;
        children.back()->set_layout_item(item);
    };
    auto count = text_group->children().size();
    (void)messageutils::createSimpleSCharBar(
        text_group, insert, message_context, prop_, summ_);
    place_bar(count, 0U, lib::Align::Start, 0.0F);
    if (summ_) {
        count = text_group->children().size();
        (void)messageutils::createFlagStepBar(
            text_group, insert, message_context, prop_, summ_, nullptr,
            force_skin_owner_, false);
        place_bar(count, 1U, lib::Align::Stretch, 0.0F);
    }
    text_group->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Stretch,
                                 .grow = 1.0F});
    lower->add(text_group);
    preview_values_ = std::make_shared<messageutils::PreviewValues>(
        message_context, false);
    preview_values_->modEvent.push_back([this] { updatePreview(); });
    auto values = preview_values_->control();
    values->set_layout_item({.horizontal = lib::Align::Stretch,
                             .vertical = lib::Align::Stretch,
                             .minimum = {180.0F, 100.0F},
                             .grow = 1.0F});
    lower->add(values);

    text_->on_changed = [this](std::string_view) { updatePreview(); };
    font_name_->on_selection_changed = [this](std::optional<std::size_t>) {
        updatePreview();
    };
    bordering_type_->on_selection_changed = [this](std::optional<std::size_t>) {
        updatePreview();
        refDataVersion();
    };
    for (const auto& control : {bold_, italic_, underline_, strike_, vertical_, antialias_}) {
        control->on_changed = [this, is_antialias = control == antialias_](bool) {
            updatePreview();
            if (is_antialias) refDataVersion();
        };
        mod(*control);
    }
    for (const auto& control : {size_, bordering_width_}) {
        control->on_changed = [this](double) { updatePreview(); };
        mod(*control);
    }
    mod(*text_);
    mod(*font_name_);
    mod(*bordering_type_);
    dmenu::Commons textCommons;
    textCommons.services = context_.services;
    textCommons.undoMaxEtc = static_cast<std::size_t>(std::max<std::int32_t>(0, prop_->var().undoMaxEtc));
    textCommons.presentation.menuText = [prop = prop_](auto id) { return prop->buildMenu(id); };
    textCommons.presentation.menuImage = [prop = prop_](auto id) { return prop->images().menu(id); };
    text_menu_ = dmenu::createTextMenu(textCommons, dmenu::TMM(text_), [this] { return catchMod(); });
    (void)messageutils::setupSPCharsMenu(message_context, summ_,
        [owner = force_skin_ ? force_skin_owner_ : summ_skin_owner_] { return owner; },
        nullptr, text_menu_->menu(), false, true, [] { return true; }, insert);
    color_.modEvent.push_back([this] { if (!ignoreMod()) applyEnabled(); updatePreview(); });
    bordering_color_.modEvent.push_back([this] { if (!ignoreMod()) applyEnabled(); updatePreview(); });
}

void TextCellDialog::initializeSpecific() {
    if (text_back_) setBack(text_back_);
    if (!text_) return;
    if (text_back_ && !create_) {
        text_->text = text_back_->text();
        const auto selectFont = [this](std::string_view requested) {
            const auto found = std::find_if(font_name_->items.begin(), font_name_->items.end(),
                [requested](const auto& item) { return item.value == requested; });
            font_name_->selected_index = found != font_name_->items.end()
                ? std::optional<std::size_t>{static_cast<std::size_t>(found - font_name_->items.begin())}
                : (!font_name_->items.empty() ? std::optional<std::size_t>{0U} : std::nullopt);
        };
        selectFont(text_back_->fontName());
        size_->value = text_back_->size();
        color_.color(text_back_->color());
        color_.alpha(static_cast<int>(text_back_->color().a));
        bold_->checked = text_back_->bold();
        italic_->checked = text_back_->italic();
        underline_->checked = text_back_->underline();
        strike_->checked = text_back_->strike();
        vertical_->checked = text_back_->vertical();
        antialias_->checked = text_back_->antialias();
        bordering_type_->selected_index = enumIndex(bordering_types_,
                                                    text_back_->borderingType());
        if (!bordering_type_->selected_index ||
            *bordering_type_->selected_index >= bordering_types_.size())
            bordering_type_->selected_index = 0U;
        bordering_color_.color(text_back_->borderingColor());
        bordering_color_.alpha(static_cast<int>(text_back_->borderingColor().a));
        bordering_width_->value = text_back_->borderingWidth();
        update_type_->selected_index = enumIndex(update_types_, text_back_->updateType());
        if (!update_type_->selected_index ||
            *update_type_->selected_index >= update_types_.size())
            update_type_->selected_index = summ_ && summ_->legacy() ? 0U : 1U;
    } else {
        const auto default_fonts = summ_ && summ_->legacy()
                                      ? std::array<lib::String, 2>{
                                            prop_->var().textCellDefaultFontClassic.value,
                                            prop_->var().textCellDefaultFont.value}
                                      : std::array<lib::String, 2>{
                                            prop_->var().textCellDefaultFont.value,
                                            prop_->var().textCellDefaultFontClassic.value};
        font_name_->selected_index = font_name_->items.empty()
            ? std::nullopt : std::optional<std::size_t>{0U};
        for (const auto& candidate : default_fonts) {
            const auto found = std::find_if(font_name_->items.begin(), font_name_->items.end(),
                [&candidate](const auto& item) { return item.value == candidate; });
            if (found != font_name_->items.end()) {
                font_name_->selected_index = static_cast<std::size_t>(found - font_name_->items.begin());
                break;
            }
        }
        size_->value = prop_->var().textCellDefaultFontSize.value;
        color_.color(prop_->var().textCellDefaultColor.value);
        bordering_color_.color(prop_->var().textCellDefaultBorderingColor.value);
        bordering_width_->value = 1.0;
        update_type_->selected_index = summ_ && summ_->legacy() ? 0U : 1U;
    }
    if (text_menu_) text_menu_->reset();
    updatePreview();
}

void TextCellDialog::refreshWarning() {
    warnings_ = warningCommon();
    if (!prop_->targetVersion(summ_, "1.50"))
        warnings_.push_back(message(prop_->requiredMsgs(), prop_->requiredMsgs().warningTextCell));
    if (summ_ && text_) {
        const auto* skin = force_skin_ ? force_skin_ : summ_skin_;
        if (skin) {
            std::vector<lib::String> flags, steps, variants, fonts;
            std::vector<char> colors;
            ::cwxeditor::cwx::msgutils::textUseItems(text_->text, flags, steps, variants, fonts, colors);
            fonts.clear(); colors.clear();
            lib::Dictionary<lib::String, bool> warned_flags, warned_steps, warned_variants, warned_fonts;
            lib::Dictionary<char, bool> warned_colors;
            const auto messages = ::cwxeditor::cwx::warning::textWarnings(prop_->parent(), *skin,
                summ_, nullptr, summ_->legacy(), summ_->dataVersion(), prop_->var().targetVersion.value,
                text_->text, flags, steps, variants, fonts, colors,
                warned_flags, warned_steps, warned_variants, warned_fonts, warned_colors);
            const auto& all = messages.all;
            warnings_.insert(warnings_.end(), all.begin(), all.end());
        }
    }
    if (update_type_ && update_type_->selected_index &&
        *update_type_->selected_index < update_types_.size()) {
        const auto type = update_types_[*update_type_->selected_index];
        if (summ_ && summ_->legacy() && type != types::UpdateType::Fixed)
            warnings_.push_back(message(prop_->requiredMsgs(), prop_->requiredMsgs().warningUpdateTypeNotFixed));
        if ((!summ_ || !summ_->legacy()) && type != types::UpdateType::Variables &&
            !prop_->isTargetVersion(summ_, "4"))
            warnings_.push_back(message(prop_->requiredMsgs(), prop_->requiredMsgs().warningUpdateType));
    }
    if (antialias_ && antialias_->checked &&
        !prop_->isTargetVersion(summ_, "4"))
        warnings_.push_back(message(prop_->requiredMsgs(), prop_->requiredMsgs().warningAntialiasedTextCell));
    warning(warnings_);
    if (context_.refresh_warnings) context_.refresh_warnings();
}

void TextCellDialog::refDataVersion() {
    BgImageDialog::refDataVersion();
    if (update_type_ && update_type_->selected_index &&
        *update_type_->selected_index < update_types_.size()) {
        const auto value = update_types_[*update_type_->selected_index];
        update_type_->set_enabled(!summ_ || !summ_->legacy() ||
                                  value != types::UpdateType::Fixed);
    }
    if (antialias_)
        antialias_->set_enabled(!summ_ || !summ_->legacy() || antialias_->checked);
}

void TextCellDialog::updatePreview() {
    if (!text_) return;
    const auto life=std::weak_ptr<void>(preview_lifetime_);
    const auto revision=++preview_revision_;
    // すべての共通テキストコントロールで、DモデルのLF契約を使う。ネイティ
    // ブ側がSWTのWindowsのCRLF表記を公開しないホストも同様。
    const auto value=previewText(dutils::wrapReturnCode(text_->text));
    if (life.expired() || preview_revision_!=revision) return;
    preview_description_ = value;
    if (bordering_type_ && bordering_type_->selected_index &&
        *bordering_type_->selected_index < bordering_types_.size()) {
        const auto type = bordering_types_[*bordering_type_->selected_index];
        bordering_color_.enabled(type != types::BorderingType::None);
        if (bordering_width_)
            bordering_width_->set_enabled(type == types::BorderingType::Inline);
    }
    if (preview_) {
        preview_->accessibility_description = preview_description_;
        const auto invalidate=preview_->invalidate;
        if (invalidate) invalidate();
        if (life.expired() || preview_revision_!=revision) return;
    }
    refreshWarning();
}

lib::String TextCellDialog::previewText(std::string_view base) const {
    if (preview_values_ && prop_) {
        messageutils::NameMap names;
        messageutils::ValueMap flags, steps, variants, systems;
        preview_values_->getValues(names, flags, steps, variants, systems);
        const auto getter = [](const messageutils::ValueMap& values) {
            return [&values](const lib::String& name) -> messageutils::VarValue {
                const auto found = values.find(name);
                return found == values.end() ? messageutils::VarValue{} : found->second;
            };
        };
        return ::cwxeditor::cwx::msgutils::simpleFormatMsg(base, getter(flags), getter(steps),
            getter(variants), getter(systems), names,
            [this](const lib::String& version) { return prop_->isTargetVersion(summ_, version); },
            prop_->sys().prefixSystemVarName());
    }
    if (context_.format_text) return context_.format_text(base);
    if (prop_) {
        return messageutils::createSPCharPreview(
            *prop_, summ_, force_skin_ ? force_skin_owner_ : summ_skin_owner_,
            nullptr, lib::String(base), true);
    }
    return lib::String(base);
}

bool TextCellDialog::apply() {
    if (!text_) return false;
    if (!text_back_) {
        text_back_ = std::make_shared<TextCell>();
        if (counter_) text_back_->setUseCounter(counter_);
    }
    text_back_->text(dutils::wrapReturnCode(text_->text));
    text_back_->fontName(font_name_ ? comboValue(*font_name_) : std::string{});
    text_back_->size(static_cast<std::uint32_t>(std::max(1, integerValue(size_, 18))));
    text_back_->color(colorFrom(color_));
    text_back_->bold(bold_ && bold_->checked);
    text_back_->italic(italic_ && italic_->checked);
    text_back_->underline(underline_ && underline_->checked);
    text_back_->strike(strike_ && strike_->checked);
    text_back_->vertical(vertical_ && vertical_->checked);
    text_back_->antialias(antialias_ && antialias_->checked);
    if (bordering_type_ && bordering_type_->selected_index &&
        *bordering_type_->selected_index < bordering_types_.size()) {
        text_back_->borderingType(bordering_types_[*bordering_type_->selected_index]);
    }
    text_back_->borderingColor(colorFrom(bordering_color_));
    text_back_->borderingWidth(static_cast<std::uint32_t>(
        std::max(1, integerValue(bordering_width_, 1))));
    if (update_type_ && update_type_->selected_index &&
        *update_type_->selected_index < update_types_.size()) {
        text_back_->updateType(update_types_[*update_type_->selected_index]);
    }
    applyParams(*text_back_);
    setBack(text_back_);
    title(prop_->requiredMsgs().dlgTitTextCell.value);
    return true;
}

ColorCellDialog::ColorCellDialog(BgImageDialogContext context, Props& prop,
                                 const Summary* summ, ColorCellPtr back,
                                 bool create, std::shared_ptr<lib::Window> parent,
                                 UseCounterPtr counter)
    : BgImageDialog(std::move(context), prop, summ, std::move(parent),
                     create ? prop.requiredMsgs().dlgTitNewColorCell.value
                            : prop.requiredMsgs().dlgTitColorCell.value,
                     prop.images().load(dprops::StringView{"color_cell.png"}), true,
                     summ ? static_cast<DSize*>(&prop.flex().areaColorCellDlg)
                          : static_cast<DSize*>(&prop.flex().areaColorCellNFDlg),
                     back,
                     create, std::move(counter)),
      color_back_(std::move(back)) {}

ColorCellDialog::ColorCellDialog(areaviewutils::Commons common, Props& prop,
                                 std::shared_ptr<lib::Window> parent,
                                 const Summary* summ, UseCounterPtr counter,
                                 ColorCellPtr back, bool create)
    : ColorCellDialog(BgImageDialogContext(std::move(common)), prop, summ,
                      std::move(back), create, std::move(parent),
                      std::move(counter)) {}

ColorCellPtr ColorCellDialog::colorBack() const noexcept {
    return color_back_;
}

void ColorCellDialog::setup(Container& area) {
    connectExternalNotifications();
    auto root_layout = std::make_shared<lib::GridLayout>();
    root_layout->columns = 1U;
    root_layout->spacing = 4.0F;
    area.set_layout(root_layout);
    auto body = makePanel(context_.services, lib::Orientation::Vertical, 4.0F);
    body->set_layout_item({.horizontal = lib::Align::Stretch,
                           .vertical = lib::Align::Stretch,
                           .grow = 1.0F});
    area.add(body);
    setupSpecific(*body);
    (void)createPositionPanel(*body, false, false);
    (void)createEasySettingsPanel(*body);
    setFirstParams();
    initializeSpecific();
    refDataVersion();

    // Dのカラーセルのショートカット判定は、このダイアロ
    // グの全子孫をDisplay全体で対象とし、ユーザー
    // 設定のメニューキーを読む。左の設定列だけでなく、
    // 完成した共通部分木へ同等のハンドラーを接続する。
    const std::weak_ptr<void> lifetime = key_filter_lifetime_;
    struct ColorAccelerator {
        int accelerator{};
        types::MenuID id{types::MenuID::None};
    };
    const auto accelerator = [this](const types::MenuID id) {
        return ColorAccelerator{
            prop_ ? static_cast<int>(dmenu::convertAccelerator(
                        prop_->buildMenu(id))) : 0,
            id};
    };
    const std::function<void(const lib::WidgetPtr&)> bind_key_filter =
        [this, lifetime, accelerator,
         &bind_key_filter](const lib::WidgetPtr& widget) {
            if (!widget) return;
            const auto previous = widget->on_key_down;
            widget->on_key_down =
                [this, lifetime, previous, owner = std::weak_ptr<lib::Widget>(widget),
                 accelerator](lib::KeyEvent& event) {
                    if (lifetime.expired()) return;
                    if (previous) previous(event);
                    if (event.handled || lifetime.expired()) return;
                    const auto state = static_cast<std::uint32_t>(event.modifiers);
                    const auto character = acceleratorCharacter(event);
                    // DはColorCellのショートカットより先に、イベント元コン
                    // トロールの設定済みテキストメニューを調べる。共通の文字入力のキ
                    // ー割り当てだけでは、通常のCtrl+C/X/Vしか扱わない。
                    if (!lib::editor_window_host_options().all_mode) {
                        if (const auto text_menu = dmenu::textMenuOf(owner.lock())) {
                            text_menu->refresh();
                            if (lifetime.expired()) return;
                            const auto menu = text_menu->menu();
                            if (menu) {
                                const auto items = menu->items;
                                for (const auto& item : items) {
                                    if (!item.enabled || item.separator) continue;
                                    const bool match = dmenu::eqAcc(
                                        static_cast<int>(dmenu::convertAccelerator(item.text)),
                                        static_cast<int>(event.key), character, state) ||
                                        (!item.shortcut.empty() && dmenu::eqAcc(
                                            static_cast<int>(dmenu::convertAccelerator2(item.shortcut)),
                                            static_cast<int>(event.key), character, state));
                                    if (!match) continue;
                                    const auto command = menu->on_command;
                                    event.handled = true;
                                    if (command) command(item.command);
                                    return;
                                }
                            }
                        }
                    }
                    const std::array accelerators{
                        accelerator(types::MenuID::CopyColor1ToColor2),
                        accelerator(types::MenuID::CopyColor2ToColor1),
                        accelerator(types::MenuID::ExchangeColors)};
                    for (const auto& candidate : accelerators) {
                        if (!dmenu::eqAcc(candidate.accelerator,
                                          static_cast<int>(event.key), character,
                                          state))
                            continue;
                        refMenu(candidate.id);
                        event.handled = true;
                        return;
                    }
                };
            if (const auto container =
                    std::dynamic_pointer_cast<lib::Container>(widget))
                for (const auto& child : container->children())
                    bind_key_filter(child);
        };
    for (const auto& child : area.children()) bind_key_filter(child);
}

void ColorCellDialog::setupSpecific(Container& area) {
    auto top = makeComponent<lib::Panel>(context_.services,
                                         lib::ComponentKind::Panel);
    auto top_layout = std::make_shared<lib::GridLayout>();
    top_layout->columns = summ_ ? 2U : 1U;
    top_layout->spacing = 4.0F;
    top->set_layout(top_layout);
    top->set_layout_item({.horizontal = lib::Align::Stretch,
                          .vertical = lib::Align::Stretch,
                          .grow = 1.0F});
    area.add(top);

    auto left = makePanel(context_.services, lib::Orientation::Vertical, 4.0F);
    left->set_layout_item({.horizontal = lib::Align::Stretch,
                           .vertical = lib::Align::Stretch,
                           .grow = 1.0F});
    top->add(left);

    auto preview_group = makeGroup(
        context_.services, message(prop_->requiredMsgs(), prop_->requiredMsgs().image));
    preview_group->set_layout_item({.horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Stretch,
                                    .grow = 1.0F});
    preview_ = makeComponent<lib::Canvas>(context_.services,
                                          lib::ComponentKind::Canvas);
    setPreferred(preview_, 320.0F, 96.0F, 1.0F);
    preview_->on_paint_context = [this](lib::DrawingContext& context,
                                        lib::Rect dirty) {
        context.save();
        context.clip(dirty);
        context.clear({255U, 255U, 255U, 255U});
        const auto bounds = preview_ ? preview_->bounds() : lib::Rect{};
        const auto safe_dimension = [](const float preferred,
                                       const float fallback) {
            const float value = preferred > 0.0F ? preferred : fallback;
            return std::isfinite(value) && value > 0.0F ? value : 1.0F;
        };
        const float width = safe_dimension(bounds.width, dirty.width);
        const float height = safe_dimension(bounds.height, dirty.height);
        const auto c1 = colorFrom(color1_);
        const auto c2 = colorFrom(color2_);
        const auto direction = gradientDir();
        const float gradient_extent = direction == types::GradientDir::TopToBottom
            ? height : width;
        // 初回フレームや全体無効化では、変更矩形に上限がない場
        // 合がある。それを何十億回もの一画素反復に変換しない。
        // 1024点なら指定プレビューサイズを超えており、表示
        // 上の階調を保ちつつモーダル構築の処理量を制限できる。
        const int strips = std::clamp(
            static_cast<int>(std::ceil(std::min(gradient_extent, 1024.0F))),
            1, 1024);
        const float strip_extent = gradient_extent / static_cast<float>(strips);
        for (int index = 0; index < strips; ++index) {
            const float ratio = direction == types::GradientDir::None || strips <= 1
                                    ? 0.0F
                                    : static_cast<float>(index) /
                                          static_cast<float>(strips - 1);
            const auto channel = [ratio](std::uint32_t first,
                                         std::uint32_t second) {
                return static_cast<int>(std::lround(
                    static_cast<float>(first) +
                    (static_cast<float>(second) - static_cast<float>(first)) *
                        ratio));
            };
            const int alpha = channel(c1.a, c2.a);
            int red = channel(c1.r, c2.r);
            int green = channel(c1.g, c2.g);
            int blue = channel(c1.b, c2.b);
            const auto composite = [alpha](int value) {
                return (value * alpha + 255 * (255 - alpha)) / 255;
            };
            switch (blend_mode_) {
            case types::BlendMode::Subtract:
                red = 255 - red * alpha / 255;
                green = 255 - green * alpha / 255;
                blue = 255 - blue * alpha / 255;
                break;
            case types::BlendMode::Multiply:
                red = composite(red);
                green = composite(green);
                blue = composite(blue);
                break;
            case types::BlendMode::Add:
                // Dは既存のプレビュー背景に加算フィルターを重ね
                // る。この共通プレビューは背景を白にするため、
                // 255にアルファ加重した入力を加え、逆にはし
                // ない。別のALLプレビューの規則は変えない。
                if (!lib::editor_window_host_options().all_mode) {
                    red = std::min(255, 255 + red * alpha / 255);
                    green = std::min(255, 255 + green * alpha / 255);
                    blue = std::min(255, 255 + blue * alpha / 255);
                } else {
                    red = std::min(255, red + 255 * (255 - alpha) / 255);
                    green = std::min(255, green + 255 * (255 - alpha) / 255);
                    blue = std::min(255, blue + 255 * (255 - alpha) / 255);
                }
                break;
            case types::BlendMode::Normal:
            case types::BlendMode::Mask:
            default:
                red = composite(red);
                green = composite(green);
                blue = composite(blue);
                break;
            }
            const lib::Color color{
                static_cast<std::uint8_t>(std::clamp(red, 0, 255)),
                static_cast<std::uint8_t>(std::clamp(green, 0, 255)),
                static_cast<std::uint8_t>(std::clamp(blue, 0, 255)), 255U};
            if (direction == types::GradientDir::TopToBottom) {
                context.fill_rect({0.0F, static_cast<float>(index) * strip_extent,
                                   width, strip_extent + 0.5F},
                                  color);
            } else {
                context.fill_rect({static_cast<float>(index) * strip_extent, 0.0F,
                                   strip_extent + 0.5F, height},
                                  color);
            }
        }
        context.restore();
    };
    preview_group->add(preview_);
    left->add(preview_group);

    auto settings = makeComponent<lib::Panel>(context_.services,
                                               lib::ComponentKind::Panel);
    auto settings_layout = std::make_shared<lib::GridLayout>();
    settings_layout->columns = 3U;
    settings_layout->spacing = 4.0F;
    settings->set_layout(settings_layout);
    settings->set_layout_item({.horizontal = lib::Align::Stretch,
                               .vertical = lib::Align::Start});
    left->add(settings);

    auto blend_layout = std::make_shared<lib::GridLayout>();
    blend_layout->columns = 4U;
    blend_layout->spacing = 3.0F;
    auto blend_group = makeGroup(
        context_.services, message(prop_->requiredMsgs(), prop_->requiredMsgs().blendMode),
        blend_layout);
    for (const auto mode : {types::BlendMode::Normal, types::BlendMode::Add,
                            types::BlendMode::Subtract, types::BlendMode::Multiply}) {
        auto radio = std::make_shared<lib::RadioButton>(blendName(prop_->requiredMsgs(), mode));
        radio->group = "blend-mode";
        radio->on_changed = [this, mode](bool checked) {
            if (checked) selectBlend(mode);
        };
        blend_group->add(radio);
        blend_controls_[mode] = radio;
        mod(*radio);
    }
    blend_group->set_layout_item({.row = 0U, .column = 0U,
                                  .horizontal = lib::Align::Stretch});

    auto color1_group = makeGroup(
        context_.services,
        message(prop_->requiredMsgs(), prop_->requiredMsgs().colorCellBaseColor));
    color1_ = ColorPicker(*prop_, *color1_group, true);
    color1_group->set_layout_item({.row = 0U, .column = 1U,
                                   .horizontal = lib::Align::Stretch});

    auto tools_group = makeGroup(
        context_.services, message(prop_->requiredMsgs(), prop_->requiredMsgs().moveColors));
    tools_group->set_layout_item({.row = 0U, .column = 2U, .row_span = 2U,
                                  .horizontal = lib::Align::Center,
                                  .vertical = lib::Align::Stretch});
    color_tools_ = makePanel(context_.services, lib::Orientation::Vertical,
                             2.0F);
    color_tools_->set_layout_item({.horizontal = lib::Align::Center,
                                   .vertical = lib::Align::Center});
    const auto add_color_tool = [this](types::MenuID id,
                                       dprops::StringView image,
                                       lib::Action action) {
        // Dはこれらを枠付きボタンではなく、縦のフラットToolBarに置く。
        auto bar = makeComponent<lib::ToolBar>(context_.services,
                                               lib::ComponentKind::ToolBar);
        lib::ToolItem item;
        item.command = "color-tool";
        item.icon = prop_->images().load(image);
        item.tooltip = prop_->requiredMsgs().menuText(id);
        bar->items.push_back(std::move(item));
        bar->on_command = [action = std::move(action)](std::string_view) {
            if (action) action();
        };
        bar->set_layout_item({.horizontal = lib::Align::Center,
                              .vertical = lib::Align::Center});
        color_tools_->add(bar);
    };
    add_color_tool(types::MenuID::CopyColor1ToColor2,
                   dprops::StringView{"copy_color1_to_color2.png"},
                   [this] { copyColor1ToColor2(); });
    add_color_tool(types::MenuID::CopyColor2ToColor1,
                   dprops::StringView{"copy_color2_to_color1.png"},
                   [this] { copyColor2ToColor1(); });
    auto separator = makeComponent<lib::Separator>(
        context_.services, lib::ComponentKind::Separator);
    separator->orientation = lib::Orientation::Horizontal;
    separator->set_layout_item({.horizontal = lib::Align::Stretch,
                                .vertical = lib::Align::Center,
                                .minimum = {26.0F, 1.0F},
                                .preferred = {26.0F, 1.0F}});
    color_tools_->add(separator);
    add_color_tool(types::MenuID::ExchangeColors,
                   dprops::StringView{"exchange_colors.png"},
                   [this] { exchangeColors(); });
    tools_group->add(color_tools_);

    auto gradient_layout = std::make_shared<lib::GridLayout>();
    gradient_layout->columns = 2U;
    gradient_layout->spacing = 4.0F;
    auto gradient_group = makeGroup(
        context_.services, message(prop_->requiredMsgs(), prop_->requiredMsgs().gradient),
        gradient_layout);
    gradient_group->add(std::make_shared<lib::Label>(
        message(prop_->requiredMsgs(), prop_->requiredMsgs().direction)));
    gradient_dir_ = makeComponent<lib::ComboBox>(context_.services,
                                                 lib::ComponentKind::ComboBox);
    gradient_dirs_ = {types::GradientDir::None, types::GradientDir::LeftToRight,
                      types::GradientDir::TopToBottom};
    for (const auto value : gradient_dirs_)
        gradient_dir_->items.push_back({gradientName(prop_->requiredMsgs(), value), {}});
    gradient_dir_->selected_index = 0U;
    gradient_dir_->set_layout_item({.horizontal = lib::Align::Start,
                                    .vertical = lib::Align::Center});
    gradient_group->add(gradient_dir_);
    gradient_group->set_layout_item({.row = 1U, .column = 0U,
                                     .horizontal = lib::Align::Stretch});

    auto color2_group = makeGroup(
        context_.services, message(prop_->requiredMsgs(), prop_->requiredMsgs().endColor));
    color2_ = ColorPicker(*prop_, *color2_group, true);
    color2_group->set_layout_item({.row = 1U, .column = 1U,
                                   .horizontal = lib::Align::Stretch});
    // SWTの明示的なタブ順は、合成方式、階調、基本色、
    // 終端色、転送ツール。共通の巡回は子の順に従い、明
    // 示的なグリッド座標でDの二行の見た目を維持する。
    settings->add(blend_group);
    settings->add(gradient_group);
    settings->add(color1_group);
    settings->add(color2_group);
    settings->add(tools_group);

    if (summ_) (void)createFlagPanel(*top);
    gradient_dir_->on_selection_changed = [this](std::optional<std::size_t>) {
        updatePreview();
        refreshWarning();
    };
    mod(*gradient_dir_);
    color1_.modEvent.push_back([this] { if (!ignoreMod()) applyEnabled(); updatePreview(); refreshWarning(); });
    color2_.modEvent.push_back([this] { if (!ignoreMod()) applyEnabled(); updatePreview(); });
}

void ColorCellDialog::initializeSpecific() {
    if (color_back_) setBack(color_back_);
    if (color_back_ && !create_) {
        blend_mode_ = color_back_->blendMode() == types::BlendMode::Mask ||
                              color_back_->mask()
                          ? types::BlendMode::Normal
                          : color_back_->blendMode();
        if (gradient_dir_) gradient_dir_->selected_index = enumIndex(
            gradient_dirs_, color_back_->gradientDir());
        color1_.color(color_back_->color1());
        color1_.alpha(static_cast<int>(color_back_->color1().a));
        color2_.color(color_back_->color2());
        color2_.alpha(static_cast<int>(color_back_->color2().a));
    } else {
        blend_mode_ = types::BlendMode::Normal;
        if (gradient_dir_) gradient_dir_->selected_index = 0U;
        color1_.color(prop_->var().colorCellDefaultColor1.value);
        color2_.color(prop_->var().colorCellDefaultColor2.value);
    }
    selectBlend(blend_mode_);
    updatePreview();
}

void ColorCellDialog::refreshWarning() {
    warnings_ = warningCommon();
    if (!prop_->targetVersion(summ_, "1.50"))
        warnings_.push_back(message(prop_->requiredMsgs(), prop_->requiredMsgs().warningColorCell));
    const auto direction = gradientDir();
    if (summ_ && summ_->legacy() && blend_mode_ != types::BlendMode::Normal &&
        direction == types::GradientDir::None && color1_.alpha() != 255)
        warnings_.push_back(message(prop_->requiredMsgs(),
                                    prop_->requiredMsgs().warningColorCellNoGradientAlphaBlend160));
    warning(warnings_);
    if (context_.refresh_warnings) context_.refresh_warnings();
}

types::GradientDir ColorCellDialog::gradientDir() const noexcept {
    if (!gradient_dir_ || !gradient_dir_->selected_index ||
        *gradient_dir_->selected_index >= gradient_dirs_.size())
        return types::GradientDir::None;
    return gradient_dirs_[*gradient_dir_->selected_index];
}

void ColorCellDialog::updatePreview() {
    const auto direction = gradientDir();
    preview_description_ = blendName(prop_->requiredMsgs(), blend_mode_) + ": " +
                           gradientName(prop_->requiredMsgs(), direction);
    if (preview_) {
        preview_->accessibility_description = preview_description_;
        if (preview_->invalidate) preview_->invalidate();
    }
    if (color2_.enabled() != (direction != types::GradientDir::None))
        color2_.enabled(direction != types::GradientDir::None);
}

void ColorCellDialog::selectBlend(types::BlendMode mode) {
    if (mode == types::BlendMode::Mask) mode = types::BlendMode::Normal;
    blend_mode_ = mode;
    for (const auto& [value, control] : blend_controls_)
        if (control) control->checked = value == mode;
    updatePreview();
    refreshWarning();
}

void ColorCellDialog::copyColor1ToColor2() {
    const bool changed = color2_.color() != color1_.color() ||
                         color2_.alpha() != color1_.alpha();
    color2_.color(color1_.color());
    color2_.alpha(color1_.alpha());
    if (changed && !ignoreMod()) applyEnabled();
    updatePreview();
    refreshWarning();
}

void ColorCellDialog::copyColor2ToColor1() {
    const bool changed = color1_.color() != color2_.color() ||
                         color1_.alpha() != color2_.alpha();
    color1_.color(color2_.color());
    color1_.alpha(color2_.alpha());
    if (changed && !ignoreMod()) applyEnabled();
    updatePreview();
    refreshWarning();
}

void ColorCellDialog::exchangeColors() {
    const bool changed = color1_.color() != color2_.color() ||
                         color1_.alpha() != color2_.alpha();
    const auto color = color1_.color();
    const auto alpha = color1_.alpha();
    color1_.color(color2_.color());
    color1_.alpha(color2_.alpha());
    color2_.color(color);
    color2_.alpha(alpha);
    if (changed && !ignoreMod()) applyEnabled();
    updatePreview();
    refreshWarning();
}

void ColorCellDialog::refMenu(types::MenuID id) {
    switch (id) {
    case types::MenuID::CopyColor1ToColor2: copyColor1ToColor2(); break;
    case types::MenuID::CopyColor2ToColor1: copyColor2ToColor1(); break;
    case types::MenuID::ExchangeColors: exchangeColors(); break;
    default: break;
    }
}

bool ColorCellDialog::apply() {
    if (!color_back_) {
        color_back_ = std::make_shared<ColorCell>();
        if (counter_) color_back_->setUseCounter(counter_);
    }
    color_back_->blendMode(blend_mode_);
    color_back_->gradientDir(gradientDir());
    color_back_->color1(colorFrom(color1_));
    color_back_->color2(colorFrom(color2_));
    applyParams(*color_back_);
    setBack(color_back_);
    title(prop_->requiredMsgs().dlgTitColorCell.value);
    return true;
}

PCCellDialog::PCCellDialog(BgImageDialogContext context, Props& prop,
                           const Summary* summ, const Skin* skin_value,
                           PCCellPtr back, bool create,
                           std::shared_ptr<lib::Window> parent,
                           UseCounterPtr counter)
    : BgImageDialog(std::move(context), prop, summ, std::move(parent),
                     create ? prop.requiredMsgs().dlgTitNewPCCell.value
                            : prop.requiredMsgs().dlgTitPCCell.value,
                     prop.images().load(dprops::StringView{"pc_cell.png"}), true,
                     summ ? static_cast<DSize*>(&prop.flex().areaPCCellDlg)
                          : static_cast<DSize*>(&prop.flex().areaPCCellNFDlg),
                     back,
                     create, std::move(counter)),
      summ_skin_(skin_value ? skin_value : context_.common.skin),
      pc_back_(std::move(back)) {}

PCCellDialog::PCCellDialog(areaviewutils::Commons common, Props& prop,
                           std::shared_ptr<lib::Window> parent,
                           const Summary* summ, const Skin* skin_value,
                           UseCounterPtr counter, PCCellPtr back, bool create)
    : PCCellDialog(BgImageDialogContext(std::move(common)), prop, summ,
                   skin_value, std::move(back), create, std::move(parent),
                   std::move(counter)) {}

PCCellPtr PCCellDialog::pcBack() const noexcept {
    return pc_back_;
}

void PCCellDialog::setupSpecific(Container& area) {
    auto group = makeGroup(
        context_.services, message(prop_->requiredMsgs(), prop_->requiredMsgs().cellPCNumber));
    group->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Stretch,
                            .minimum = {
                                static_cast<float>(prop_->var().cellPCNumberWidth.value),
                                180.0F},
                            .grow = 1.0F});
    pc_number_ = makeComponent<lib::ComboBox>(context_.services,
                                              lib::ComponentKind::ComboBox);
    const auto party_size = std::max<std::size_t>(
        1U, prop_->looks().partyCardXY().size());
    for (std::size_t index = 0; index < party_size; ++index)
        pc_number_->items.push_back({::cwxeditor::cwx::utils::tryFormat(
                                         prop_->requiredMsgs().pc.value, index + 1U),
                                     std::to_string(index + 1U)});
    pc_number_->selected_index = 0U;
    pc_number_->set_layout_item({.horizontal = lib::Align::Stretch});
    group->add(pc_number_);
    preview_ = makeComponent<lib::Canvas>(context_.services,
                                          lib::ComponentKind::Canvas);
    setPreferred(preview_,
                 static_cast<float>(prop_->var().cellPCNumberWidth.value),
                 150.0F, 1.0F);
    preview_->on_paint_context = [this](lib::DrawingContext& context,
                                        lib::Rect dirty) {
        context.save();
        context.clip(dirty);
        context.clear({255U, 255U, 255U, 255U});
        const auto index = pc_number_ && pc_number_->selected_index
                               ? *pc_number_->selected_index + 1U
                               : 1U;
        const auto font_value = prop_->adjustFont(prop_->looks().pcNumberFont(
            summ_skin_ ? summ_skin_->legacy() : (summ_ && summ_->legacy())));
        const auto font = portableFont(font_value);
        const auto text = std::to_string(index);
        const auto bounds = preview_ ? preview_->bounds() : lib::Rect{};
        const float width = bounds.width > 0.0F ? bounds.width : dirty.width;
        const float height = bounds.height > 0.0F ? bounds.height : dirty.height;
        const auto measured = context.measure_text(text, font, width);
        context.draw_text(text,
                          {std::max(0.0F, (width - measured.width) / 2.0F),
                           std::max(0.0F, (height - measured.height) / 2.0F)},
                          font, {0U, 0U, 0U, 255U});
        context.restore();
    };
    group->add(preview_);
    expand_ = makeComponent<lib::CheckBox>(context_.services,
                                           lib::ComponentKind::CheckBox);
    expand_->text = message(prop_->requiredMsgs(), prop_->requiredMsgs().pcCellExpanding);
    expand_->on_changed = [this](bool) { refreshWarning(); };
    group->add(expand_);
    area.add(group);
    pc_number_->on_selection_changed = [this](std::optional<std::size_t>) {
        if (preview_) {
            preview_->accessibility_description = pc_number_ &&
                    pc_number_->selected_index
                ? pc_number_->items[*pc_number_->selected_index].text
                : lib::String{};
            if (preview_->invalidate) preview_->invalidate();
        }
    };
    mod(*pc_number_);
    mod(*expand_);
}

void PCCellDialog::initializeSpecific() {
    if (pc_back_) setBack(pc_back_);
    if (pc_back_ && !creating()) {
        if (pc_number_ && !pc_number_->items.empty()) {
            const auto value = std::min<std::size_t>(
                pc_back_->pcNumber() > 0U ? pc_back_->pcNumber() - 1U : 0U,
                pc_number_->items.size() - 1U);
            pc_number_->selected_index = value;
        }
        if (expand_) expand_->checked = pc_back_->expand();
        if (smoothing_) {
            smoothing_->selected_index = enumIndex(
                smoothings_, pc_back_->smoothing());
            if (*smoothing_->selected_index >= smoothings_.size())
                smoothing_->selected_index = 0U;
        }
    }
}

void PCCellDialog::updatePreview() {
    if (preview_ && preview_->invalidate) preview_->invalidate();
}

void PCCellDialog::refreshWarning() {
    warnings_ = warningCommon();
    if (!prop_->isTargetVersion(summ_, "1"))
        warnings_.push_back(message(prop_->requiredMsgs(), prop_->requiredMsgs().warningPCCell));
    warning(warnings_);
    if (context_.refresh_warnings) context_.refresh_warnings();
}

void PCCellDialog::selectEasySetting() {
    if (selected_ && !(easy_ && isSelected(easy_, 1U))) return;
    const auto card_size = prop_->looks().cardSize();
    ignoreMod(true);
    if (w_) w_->value = card_size.width;
    if (h_) h_->value = card_size.height;
    ignoreMod(false);
    selected_ = true;
    notifyToolbar();
}

bool PCCellDialog::apply() {
    if (!pc_back_) {
        pc_back_ = std::make_shared<PCCell>();
        if (counter_) pc_back_->setUseCounter(counter_);
    }
    if (pc_number_ && pc_number_->selected_index)
        pc_back_->pcNumber(static_cast<std::uint32_t>(*pc_number_->selected_index + 1U));
    if (expand_) pc_back_->expand(expand_->checked);
    if (smoothing_ && smoothing_->selected_index &&
        *smoothing_->selected_index < smoothings_.size())
        pc_back_->smoothing(smoothings_[*smoothing_->selected_index]);
    applyParams(*pc_back_);
    setBack(pc_back_);
    title(prop_->requiredMsgs().dlgTitPCCell.value);
    return true;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::bgimagedialog
