#include "cwxeditor_src/gui/cwx/editor/gui/dwt/abilityview.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"

#include "cwxeditor_src/core/cwx/msgs.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::abilityview {

namespace {

constexpr std::array<Physical, 6> kPhysicalValues{
    Physical::Dex, Physical::Agl, Physical::Int,
    Physical::Str, Physical::Vit, Physical::Min};

constexpr std::array<Mental, 10> kMentalValues{
    Mental::Aggressive,  Mental::Unaggressive, Mental::Cheerful,
    Mental::Uncheerful,   Mental::Brave,       Mental::Unbrave,
    Mental::Cautious,     Mental::Uncautious,  Mental::Trickish,
    Mental::Untrickish};

[[nodiscard]] std::size_t indexOf(const Physical value) {
    switch (value) {
    case Physical::Dex: return 0;
    case Physical::Agl: return 1;
    case Physical::Int: return 2;
    case Physical::Str: return 3;
    case Physical::Vit: return 4;
    case Physical::Min: return 5;
    }
    throw std::out_of_range("invalid physical ability");
}

[[nodiscard]] std::size_t indexOf(const Mental value) {
    switch (value) {
    case Mental::Aggressive: return 0;
    case Mental::Unaggressive: return 1;
    case Mental::Cheerful: return 2;
    case Mental::Uncheerful: return 3;
    case Mental::Brave: return 4;
    case Mental::Unbrave: return 5;
    case Mental::Cautious: return 6;
    case Mental::Uncautious: return 7;
    case Mental::Trickish: return 8;
    case Mental::Untrickish: return 9;
    }
    throw std::out_of_range("invalid mental ability");
}

void setRadioCell(::cwxeditor::lib::LayoutItem& item, std::size_t row,
                  std::size_t column = 0) {
    item.row = row;
    item.column = column;
    item.horizontal = ::cwxeditor::lib::Align::Start;
    item.vertical = ::cwxeditor::lib::Align::Center;
}

}  // namespace

Messages::Messages()
    : aptPhysical("身体的要素"), aptMental("精神的要素"),
      physicalNames{"器用度", "敏捷度", "知力", "筋力", "生命力", "精神力"},
      mentalNames{"好戦性", "平和性", "社交性", "内向性", "勇猛性",
                  "臆病性", "慎重性", "大胆性", "狡猾性", "正直性"} {}

Messages::Messages(const ::cwxeditor::cwx::msgs::Msgs& catalog)
    : aptPhysical(catalog.aptPhysical.value), aptMental(catalog.aptMental.value),
      physicalNames{catalog.physicalName(Physical::Dex),
                    catalog.physicalName(Physical::Agl),
                    catalog.physicalName(Physical::Int),
                    catalog.physicalName(Physical::Str),
                    catalog.physicalName(Physical::Vit),
                    catalog.physicalName(Physical::Min)},
      mentalNames{catalog.mentalName(Mental::Aggressive),
                  catalog.mentalName(Mental::Unaggressive),
                  catalog.mentalName(Mental::Cheerful),
                  catalog.mentalName(Mental::Uncheerful),
                  catalog.mentalName(Mental::Brave),
                  catalog.mentalName(Mental::Unbrave),
                  catalog.mentalName(Mental::Cautious),
                  catalog.mentalName(Mental::Uncautious),
                  catalog.mentalName(Mental::Trickish),
                  catalog.mentalName(Mental::Untrickish)} {}

const std::string& Messages::physicalName(const Physical value) const {
    return physicalNames.at(indexOf(value));
}

const std::string& Messages::mentalName(const Mental value) const {
    return mentalNames.at(indexOf(value));
}

Commons::Commons(const ::cwxeditor::cwx::msgs::Msgs& catalog)
    : messages(catalog) {}

template <typename Component>
std::shared_ptr<Component> AbilityView::createComponent(
    const ::cwxeditor::lib::ComponentKind kind) const {
    if (services_.gui) {
        auto candidate = services_.gui->create_component(kind);
        if (auto result = std::dynamic_pointer_cast<Component>(candidate))
            return result;
    }
    return std::make_shared<Component>();
}

AbilityView::AbilityView(Commons comm, Parent parent, const int readOnly,
                         Services services)
    : commons_(std::move(comm)), services_(std::move(services)),
      parent_(std::move(parent)), readOnly_(readOnly) {
    build();
    // Dのコンストラクターは、戻る前に有効なラジオ選択を設定する。
    physical(Physical::Dex);
    mental(Mental::Aggressive);
    enabled(true);
}

AbilityView::~AbilityView() {
    // この制御オブジェクトの破棄後も親が共通ルートを保持する場合がある。自
    // 身の参照を解放する前に、thisをキャプチャするコールバックを外す。
    for (const auto& radio : physicalButtons_)
        if (radio) radio->on_changed = {};
    for (const auto& radio : mentalButtons_)
        if (radio) radio->on_changed = {};
}

void AbilityView::build() {
    using namespace ::cwxeditor::lib;

    root_ = createComponent<Panel>(ComponentKind::Panel);
    auto rootLayout = std::make_shared<GridLayout>();
    rootLayout->columns = 2;
    rootLayout->equal_width = false;
    rootLayout->padding = {};
    rootLayout->spacing = 0.0F;
    root_->set_layout(std::move(rootLayout));
    root_->set_layout_item({.horizontal = Align::Stretch,
                            .vertical = Align::Stretch, .grow = 1.0F});

    physicalGroup_ = createComponent<GroupBox>(ComponentKind::GroupBox);
    mentalGroup_ = createComponent<GroupBox>(ComponentKind::GroupBox);
    physicalGroup_->title = commons_.messages.aptPhysical;
    mentalGroup_->title = commons_.messages.aptMental;
    physicalGroup_->set_layout_item({.row = 0U, .column = 0U,
                                     .horizontal = Align::Stretch,
                                     .vertical = Align::Stretch, .grow = 1.0F});
    mentalGroup_->set_layout_item({.row = 0U, .column = 1U,
                                   .horizontal = Align::Stretch,
                                   .vertical = Align::Stretch, .grow = 1.0F});

    auto physicalGroupLayout =
        std::make_shared<LinearLayout>(Orientation::Vertical);
    physicalGroupLayout->cross_axis = Align::Center;
    physicalGroup_->set_layout(std::move(physicalGroupLayout));

    auto mentalGroupLayout =
        std::make_shared<LinearLayout>(Orientation::Vertical);
    mentalGroupLayout->cross_axis = Align::Center;
    mentalGroup_->set_layout(std::move(mentalGroupLayout));

    physicalPanel_ = createComponent<Panel>(ComponentKind::Panel);
    auto physicalPanelLayout = std::make_shared<GridLayout>();
    physicalPanelLayout->columns = 1;
    physicalPanelLayout->equal_width = true;
    physicalPanelLayout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
    physicalPanelLayout->spacing = 5.0F;
    physicalPanel_->set_layout(std::move(physicalPanelLayout));
    physicalPanel_->set_layout_item({.horizontal = Align::Center,
                                     .vertical = Align::Stretch, .grow = 1.0F});

    mentalPanel_ = createComponent<Panel>(ComponentKind::Panel);
    auto mentalPanelLayout = std::make_shared<GridLayout>();
    mentalPanelLayout->columns = 2;
    mentalPanelLayout->equal_width = true;
    mentalPanelLayout->padding = {5.0F, 5.0F, 5.0F, 5.0F};
    mentalPanelLayout->spacing = 0.0F;
    mentalPanel_->set_layout(std::move(mentalPanelLayout));
    mentalPanel_->set_layout_item({.horizontal = Align::Center,
                                   .vertical = Align::Stretch, .grow = 1.0F});

    for (std::size_t i = 0; i < kPhysicalValues.size(); ++i) {
        const auto value = kPhysicalValues[i];
        auto radio = createComponent<RadioButton::element_type>(
            ComponentKind::RadioButton);
        radio->text = commons_.messages.physicalName(value);
        radio->group = "physical";
        radio->accessibility_name = radio->text;
        LayoutItem item;
        setRadioCell(item, i);
        item.grow = 1.0F;
        radio->set_layout_item(item);
        radio->set_enabled(false);
        radio->on_changed = [this, value](const bool selected) {
            if (!selected || readOnly_ != 0 || !enabled_) return;
            selectPhysical(value, true);
        };
        physicalButtons_[i] = std::move(radio);
        physicalPanel_->add(physicalButtons_[i]);
    }

    for (std::size_t i = 0; i < kMentalValues.size(); ++i) {
        const auto value = kMentalValues[i];
        auto radio = createComponent<RadioButton::element_type>(
            ComponentKind::RadioButton);
        radio->text = commons_.messages.mentalName(value);
        radio->group = "mental";
        radio->accessibility_name = radio->text;
        LayoutItem item;
        setRadioCell(item, i / 2, i % 2);
        item.margin.right = i % 2 == 0
            ? static_cast<float>(std::max(0, commons_.radioGroupSeparatorWidth)) : 0.0F;
        item.margin.bottom = i / 2 + 1 < (kMentalValues.size() + 1) / 2 ? 5.0F : 0.0F;
        item.grow = 1.0F;
        radio->set_layout_item(item);
        radio->set_enabled(false);
        radio->on_changed = [this, value](const bool selected) {
            if (!selected || readOnly_ != 0 || !enabled_) return;
            selectMental(value, true);
        };
        mentalButtons_[i] = std::move(radio);
        mentalPanel_->add(mentalButtons_[i]);
    }

    physicalGroup_->add(physicalPanel_);
    mentalGroup_->add(mentalPanel_);

    root_->add(physicalGroup_);
    root_->add(mentalGroup_);

    if (parent_) parent_->add(root_);
}

void AbilityView::updatePhysicalButtons() noexcept {
    for (const auto& radio : physicalButtons_)
        if (radio) radio->set_enabled(readOnly_ == 0 && enabled_);
}

void AbilityView::updateMentalButtons() noexcept {
    for (const auto& radio : mentalButtons_)
        if (radio) radio->set_enabled(readOnly_ == 0 && enabled_);
}

void AbilityView::enabled(const bool value) noexcept {
    ::cwxeditor::cwx::perf::TraceScope trace_enabled_set(__FILE__, __LINE__);
    enabled_ = value;
    updatePhysicalButtons();
    updateMentalButtons();
}

void AbilityView::physical(const Physical value) {
    ::cwxeditor::cwx::perf::TraceScope trace_physical_set(__FILE__, __LINE__);
    selectPhysical(value, false);
}

void AbilityView::mental(const Mental value) {
    ::cwxeditor::cwx::perf::TraceScope trace_mental_set(__FILE__, __LINE__);
    selectMental(value, false);
}

Physical AbilityView::physical() const {
    ::cwxeditor::cwx::perf::TraceScope trace_physical_get(__FILE__, __LINE__);
    for (std::size_t i = 0; i < physicalButtons_.size(); ++i)
        if (physicalButtons_[i] && physicalButtons_[i]->checked)
            return kPhysicalValues[i];
    throw std::logic_error("AbilityView has no selected physical ability");
}

Mental AbilityView::mental() const {
    ::cwxeditor::cwx::perf::TraceScope trace_mental_get(__FILE__, __LINE__);
    for (std::size_t i = 0; i < mentalButtons_.size(); ++i)
        if (mentalButtons_[i] && mentalButtons_[i]->checked)
            return kMentalValues[i];
    throw std::logic_error("AbilityView has no selected mental ability");
}

void AbilityView::selectPhysical(const Physical value, const bool notify) {
    const auto selected = physicalIndex(value);
    for (std::size_t i = 0; i < physicalButtons_.size(); ++i)
        if (physicalButtons_[i]) physicalButtons_[i]->checked = i == selected;
    if (notify) notifyModification();
}

void AbilityView::selectMental(const Mental value, const bool notify) {
    const auto selected = mentalIndex(value);
    for (std::size_t i = 0; i < mentalButtons_.size(); ++i)
        if (mentalButtons_[i]) mentalButtons_[i]->checked = i == selected;
    if (notify) notifyModification();
}

void AbilityView::notifyModification() {
    // 通知中にリスナーを追加・削除しても
    // 巡回が無効にならないよう、選択通知
    // ではデリゲート一覧をコピーする。
    const auto listeners = modEvent;
    for (const auto& callback : listeners)
        if (callback) callback();
}

void AbilityView::addModificationListener(ModCallback callback) {
    modEvent.push_back(std::move(callback));
}

const RadioButton& AbilityView::physicalButton(const Physical value) const {
    return physicalButtons_.at(physicalIndex(value));
}

const RadioButton& AbilityView::mentalButton(const Mental value) const {
    return mentalButtons_.at(mentalIndex(value));
}

std::size_t AbilityView::physicalIndex(const Physical value) {
    return indexOf(value);
}

std::size_t AbilityView::mentalIndex(const Mental value) {
    return indexOf(value);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::abilityview
