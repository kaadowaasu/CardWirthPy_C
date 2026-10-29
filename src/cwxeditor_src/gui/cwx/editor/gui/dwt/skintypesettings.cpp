#include "cwxeditor_src/lib/service_defaults.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/skintypesettings.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaview.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/bgimagedialog.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"

#include <algorithm>
#include <cmath>
#include <array>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::skintypesettings {
namespace {

using lib::ComponentKind;

Services resolveServices(Services supplied) {
    return lib::with_service_defaults({}, std::move(supplied));
}

template <typename Component>
std::shared_ptr<Component> makeComponent(const Services& services,
                                         const ComponentKind kind) {
    if (services.gui) {
        if (auto widget = services.gui->create_component(kind)) {
            if (auto result = std::dynamic_pointer_cast<Component>(widget)) {
                return result;
            }
        }
    }
    return std::make_shared<Component>();
}

std::shared_ptr<lib::Dialog> makeDialog(const Services& services) {
    if (services.gui) {
        if (auto result = services.gui->create_dialog()) return result;
    }
    return std::make_shared<lib::Dialog>();
}

void addAction(std::vector<Action>& actions, const Action& action) {
    if (action) actions.push_back(action);
}

void setSingleColumnLayout(const std::shared_ptr<lib::Container>& container) {
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    layout->spacing = 4.0F;
    container->set_layout(std::move(layout));
}

bool parseInteger(StringView value, std::int64_t& result) {
    if (value.empty()) return false;
    const auto* first = value.data();
    const auto* last = first + value.size();
    const auto parsed = std::from_chars(first, last, result);
    return parsed.ec == std::errc{} && parsed.ptr == last;
}

String elementTargetType(const Props& prop, const Element element) {
    const auto& messages = prop.requiredMsgs();
    switch (element) {
    case Element::All:
        return messages.elementAllTargetType.value;
    case Element::Health:
        return messages.undead.value;
    case Element::Mind:
        return messages.automaton.value;
    case Element::Miracle:
        return messages.unholy.value;
    case Element::Magic:
        return messages.constructure.value;
    case Element::Fire:
    case Element::Ice:
        return messages.elementWeaknessAndResistTargetType.value;
    }
    return {};
}

constexpr std::array<Element, 7> allElements{
    Element::All, Element::Health, Element::Mind, Element::Miracle,
    Element::Magic, Element::Fire, Element::Ice};

bool accepted(const lib::DialogResult result) noexcept {
    return result == lib::DialogResult::OK || result == lib::DialogResult::Yes ||
           result == lib::DialogResult::Apply;
}

bool isChildOf(const std::filesystem::path& path,
               const std::filesystem::path& base) {
    auto current = path.lexically_normal();
    const auto root = base.lexically_normal();
    for (auto it = root.begin(), pathIt = current.begin(); it != root.end();
         ++it, ++pathIt) {
        if (pathIt == current.end() || *it != *pathIt) return false;
    }
    return true;
}

}  // namespace

template <typename Component>
std::shared_ptr<Component> SkinTypeChooser::createComponent(
    const lib::ComponentKind kind) const {
    return makeComponent<Component>(services_, kind);
}

SkinTypeChooser::SkinTypeChooser(Props& prop, Parent parent, const int style,
                                 CanSelect canSelect,
                                 CreateSettingsFromDefault createSettingsFromDefault,
                                 Services services, Window owner)
    : prop_(&prop), root_(std::move(parent)), owner_(std::move(owner)),
      services_(resolveServices(std::move(services))),
      can_select_(std::move(canSelect)), create_settings_(std::move(createSettingsFromDefault)),
      settings_(dprops::arraySnapshot(
          prop.var().settingsWithSkinTypes.value.values)) {
    (void)style;
    if (!root_) root_ = createComponent<lib::Panel>(ComponentKind::Panel);
    build();
}

SkinTypeChooser::SkinTypeChooser(Props& prop, lib::Container& parent,
                                 const int style, CanSelect canSelect,
                                 CreateSettingsFromDefault createSettingsFromDefault,
                                 Services services, Window owner)
    : SkinTypeChooser(prop, Parent{}, style, std::move(canSelect),
                      std::move(createSettingsFromDefault), std::move(services),
                      std::move(owner)) {
    parent.add(root_);
}

SkinTypeChooser::~SkinTypeChooser() {
    lifetime_.reset();
    if (type_) type_->on_selection_changed = {};
    if (edit_) edit_->on_click = {};
    if (add_) add_->on_click = {};
    if (del_) del_->on_click = {};
    if (clone_) clone_->on_click = {};
    selectEvent.clear();
}

void SkinTypeChooser::build() {
    using MenuID = ::cwxeditor::cwx::types::MenuID;
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 3;
    layout->spacing = 4.0F;
    root_->set_layout(std::move(layout));
    // この選択欄はSWTでは一つのネイティブ行となる。明示的な最
    // 小高さがないと、 外側の GridLayout が区切り線
    // の高さまで縮め、四つの操作ボタンの一部しか見えなくなる。
    root_->set_layout_item({.horizontal = lib::Align::Stretch,
                            .vertical = lib::Align::Center,
                            .minimum = {0.0F, lib::kEditorUiMetrics.control_height}});

    auto label = createComponent<lib::Label>(ComponentKind::Label);
    label->text = prop_->requiredMsgs().selectSkinType.value;
    label->set_layout_item({0, 0, 1, 1, lib::Align::Start, lib::Align::Center});
    root_->add(label);

    type_ = createComponent<lib::ComboBox>(ComponentKind::ComboBox);
    type_->set_layout_item({0, 1, 1, 1, lib::Align::Stretch, lib::Align::Center});
    type_->accessibility_name = prop_->requiredMsgs().selectSkinType.value;
    type_->on_selection_changed = [this](const std::optional<std::size_t> index) {
        if (!index) return;
        const auto requested = *index;
        (void)lib::select_combo_item(*type_, static_cast<std::size_t>(last_selected_), false);
        select(static_cast<int>(requested));
    };
    root_->add(type_);

    auto buttons = createComponent<lib::Panel>(ComponentKind::Panel);
    auto buttonLayout = std::make_shared<lib::GridLayout>();
    buttonLayout->columns = 4;
    buttonLayout->equal_width = true;
    buttonLayout->spacing = 2.0F;
    buttons->set_layout(std::move(buttonLayout));
    // D版と同様、コンボは FILL_HORIZONTAL
    // とし、 四つの等幅ボタンを持つ部品は自然な幅に保つ。
    buttons->set_layout_item({0, 2, 1, 1, lib::Align::Start, lib::Align::Center});
    root_->add(buttons);

    edit_ = createComponent<lib::Button>(ComponentKind::Button);
    edit_->text = prop_->requiredMsgs().menuText(MenuID::EditProp);
    edit_->icon = prop_->images().load(StringView{"edit.png"});
    edit_->on_click = [this] { static_cast<void>(editSelected()); };
    buttons->add(edit_);
    add_ = createComponent<lib::Button>(ComponentKind::Button);
    add_->text = prop_->requiredMsgs().menuText(MenuID::AddItem);
    add_->icon = prop_->images().load(StringView{"add_item.png"});
    add_->on_click = [this] { static_cast<void>(addType()); };
    buttons->add(add_);
    del_ = createComponent<lib::Button>(ComponentKind::Button);
    del_->text = prop_->requiredMsgs().menuText(MenuID::DelItem);
    del_->icon = prop_->images().load(StringView{"del_item.png"});
    del_->on_click = [this] { static_cast<void>(deleteSelected()); };
    buttons->add(del_);
    clone_ = createComponent<lib::Button>(ComponentKind::Button);
    clone_->text = prop_->requiredMsgs().menuText(MenuID::Clone);
    clone_->icon = prop_->images().load(StringView{"clone.png"});
    clone_->on_click = [this] { static_cast<void>(cloneSelected()); };
    buttons->add(clone_);

    rebuildTypeItems();
}

void SkinTypeChooser::rebuildTypeItems(const StringView selectedType) {
    String selectedText(selectedType);
    if (selectedText.empty() && type_ && type_->selected_index &&
        *type_->selected_index < type_->items.size()) {
        selectedText = type_->items[*type_->selected_index].text;
    }

    std::vector<lib::ChoiceItem> items;
    items.push_back({prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().defaultSettings), {}, 0, true});
    for (const auto& settings : settings_) {
        items.push_back({settings.type, settings.type, 0, true});
    }

    std::optional<std::size_t> selected;
    if (!selectedText.empty()) {
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].text == selectedText) {
                selected = i;
                break;
            }
        }
    }
    if (!selected) selected = 0;
    (void)lib::replace_combo_items(type_, std::move(items), selected);
    last_selected_ = static_cast<int>(*selected);
    refreshButtons();
}

void SkinTypeChooser::refreshButtons() {
    const bool has_selection = selected() >= 0;
    if (edit_) edit_->set_enabled(has_selection);
    if (del_) del_->set_enabled(has_selection);
}

std::vector<String> SkinTypeChooser::configuredTypes() const {
    std::vector<String> result;
    result.reserve(settings_.size());
    for (const auto& settings : settings_) result.push_back(settings.type);
    return result;
}

void SkinTypeChooser::sortTypes() {
    String selectedText;
    if (type_ && type_->selected_index && *type_->selected_index < type_->items.size()) {
        selectedText = type_->items[*type_->selected_index].text;
    }
    const bool logical = prop_->var().logicalSort.value;
    std::stable_sort(settings_.begin(), settings_.end(), [logical](const auto& left,
                                                                   const auto& right) {
        return logical ? ::cwxeditor::cwx::utils::ncmp(left.type, right.type) < 0
                       : left.type < right.type;
    });
    rebuildTypeItems(selectedText);
}

void SkinTypeChooser::raiseSelectEvent() {
    const std::weak_ptr<int> life = lifetime_;
    const auto callbacks = selectEvent;
    for (const auto& callback : callbacks) {
        if (callback) callback();
        if (life.expired()) return;
    }
}

int SkinTypeChooser::selected() const noexcept {
    if (!type_ || !type_->selected_index || *type_->selected_index == 0) return -1;
    const auto index = *type_->selected_index - 1;
    return index < settings_.size() ? static_cast<int>(index) : -1;
}

bool SkinTypeChooser::requestSelection(const bool deleting, std::function<void()> accepted) {
    if (!confirmSelection) {
        if (can_select_ && !can_select_(deleting)) return false;
        accepted();
        return true;
    }
    const std::weak_ptr<int> alive = lifetime_;
    const auto result = std::make_shared<bool>(false);
    root_->set_enabled(false);
    confirmSelection(deleting, [this, alive, accepted = std::move(accepted), result](bool allowed) {
        if (alive.expired()) return;
        root_->set_enabled(true);
        if (!allowed) return;
        *result = true;
        accepted();
    });
    return *result;
}

void SkinTypeChooser::select(const int row, const bool notify) {
    if (!type_ || row < 0 || static_cast<std::size_t>(row) >= type_->items.size()) return;
    const auto selectedType = row == 0 ? String{} : type_->items[static_cast<std::size_t>(row)].text;
    (void)requestSelection(false, [this, selectedType, notify] {
        std::size_t chosen{};
        for (std::size_t i = 1; i < type_->items.size(); ++i)
            if (type_->items[i].text == selectedType) { chosen = i; break; }
        (void)lib::select_combo_item(*type_, chosen, false);
        last_selected_ = static_cast<int>(chosen);
        refreshButtons();
        if (notify) raiseSelectEvent();
    });
}

void SkinTypeChooser::refresh() {
    String selectedText;
    if (type_ && type_->selected_index && *type_->selected_index < type_->items.size()) {
        selectedText = type_->items[*type_->selected_index].text;
    }
    settings_ = dprops::arraySnapshot(
        prop_->var().settingsWithSkinTypes.value.values);
    rebuildTypeItems(selectedText);
}

bool SkinTypeChooser::validNewType(const StringView type, const int except) const {
    if (type.empty()) return false;
    for (std::size_t i = 0; i < settings_.size(); ++i) {
        if (static_cast<int>(i) != except && settings_[i].type == type) return false;
    }
    return true;
}

void SkinTypeChooser::askType(String title, String desc, const lib::ResourceId icon,
                              std::vector<String> excluded, String initial,
                              std::function<void(String)> then) {
    if (!then) return;
    const auto gui = services_.gui;
    if (gui && lib::editor_uses_async_dialog_flow(gui->platform())) {
        auto dialog = std::make_shared<EditSkinTypeDialog>(
            *prop_, owner_, std::move(title), icon, std::move(desc),
            std::move(excluded), std::move(initial), services_);
        pending_type_dialog_ = dialog;
        const std::weak_ptr<EditSkinTypeDialog> weak = dialog;
        if (dialog->openAsync([this, weak, then](const bool accepted) {
                const auto current = weak.lock();
                String type = current && accepted ? current->type() : String{};
                // 自身のコールバックが戻るまで、ダイアログを保持する。
                auto keep = std::exchange(pending_type_dialog_, {});
                if (!type.empty()) then(std::move(type));
            }))
            return;
        pending_type_dialog_.reset();
    }
    EditSkinTypeDialog dialog(*prop_, owner_, std::move(title), icon, std::move(desc),
                              std::move(excluded), std::move(initial), services_);
    if (!dialog.open()) return;
    auto type = dialog.type();
    if (!type.empty()) then(std::move(type));
}

bool SkinTypeChooser::chooseType(String title, String desc,
                                 const StringView defaultValue, String& result,
                                 const lib::ResourceId icon) {
    // D版と同様、設定済みの全種類を excludeTypes
    // に渡して候補から除外する。 重複した名前は確定できない。
    EditSkinTypeDialog dialog(*prop_, owner_, std::move(title), icon, std::move(desc),
                              configuredTypes(), String(defaultValue), services_);
    if (!dialog.open()) return false;
    result = dialog.type();
    return !result.empty();
}

bool SkinTypeChooser::editSelected(const StringView requestedType) {
    const auto index = selected();
    if (index < 0) return false;
    String type(requestedType);
    if (type.empty()) {
        std::vector<String> excluded;
        excluded.reserve(settings_.size() - 1);
        for (std::size_t i = 0; i < settings_.size(); ++i) {
            if (static_cast<int>(i) != index) excluded.push_back(settings_[i].type);
        }
        askType(prop_->requiredMsgs().editSkinTypeName.value,
                prop_->requiredMsgs().editSkinTypeNameDesc.value,
                prop_->images().load(StringView{"edit.png"}),
                std::move(excluded), settings_[index].type,
                [this](String chosen) { (void)editSelected(chosen); });
        return false;
    }
    if (!validNewType(type, index)) return false;
    if (beforeEdit) beforeEdit();
    const auto previousType = settings_[index].type;
    settings_[index].type = type;
    // D との意図的な差。D は種類名の変更後に旧名を探して選択を復元できない（gui/cwx/editor/gui/dwt/skintypesettings.d:85-89,184-205
    // ）。C++ は新しい名前で復元し、並べ替
    // え後もモデルと表示の選択を一致させる。
    sortTypes();
    rebuildTypeItems(type);
    if (renamed) renamed(previousType, type);
    raiseSelectEvent();
    return true;
}

bool SkinTypeChooser::addType(const StringView requestedType) {
    String type(requestedType);
    if (type.empty()) {
        askType(prop_->requiredMsgs().addSkinType.value, prop_->requiredMsgs().addSkinTypeDesc.value,
                prop_->images().load(StringView{"add_item.png"}), configuredTypes(), {},
                [this](String chosen) { (void)addType(chosen); });
        return false;
    }
    if (!validNewType(type)) return false;
    if (beforeEdit) beforeEdit();
    settings_.push_back(SettingsWithSkinType{});
    settings_.back().type = type;
    sortTypes();
    raiseSelectEvent();
    for (std::size_t i = 1; i < type_->items.size(); ++i)
        if (type_->items[i].text == type) { select(static_cast<int>(i)); break; }
    return true;
}

bool SkinTypeChooser::deleteSelected(const bool confirm) {
    const auto index = selected();
    if (index < 0 || (can_select_ && !can_select_(true))) return false;
    if (confirm && !services_.dialogs) return false;
    if (confirm) {
        lib::MessageDialogRequest request;
        request.title = prop_->requiredMsgs().delSkinType.value;
        request.message = ::cwxeditor::cwx::utils::tryFormat(
            prop_->requiredMsgs().delSkinTypeDesc.value, settings_[index].type);
        request.severity = lib::MessageSeverity::Question;
        request.buttons = lib::MessageButtons::YesNo;
        if (services_.dialogs->message(request) != lib::MessageResult::Yes) return false;
    }
    if (beforeEdit) beforeEdit();
    settings_.erase(settings_.begin() + index);
    sortTypes();
    const auto row = std::min<std::size_t>(static_cast<std::size_t>(index + 1),
                                           type_->items.empty() ? 0 : type_->items.size() - 1);
    (void)lib::select_combo_item(*type_, row, false);
    last_selected_ = static_cast<int>(row);
    refreshButtons();
    raiseSelectEvent();
    return true;
}

bool SkinTypeChooser::cloneSelected(const StringView requestedType) {
    String type(requestedType);
    if (type.empty()) {
        askType(prop_->requiredMsgs().cloneSkinType.value, prop_->requiredMsgs().cloneSkinTypeDesc.value,
                prop_->images().load(StringView{"clone.png"}), configuredTypes(), {},
                [this](String chosen) { (void)cloneSelected(chosen); });
        return false;
    }
    if (!validNewType(type)) return false;
    if (beforeEdit) beforeEdit();
    const auto index = selected();
    SettingsWithSkinType clone;
    if (index < 0) {
        clone = create_settings_ ? create_settings_() : SettingsWithSkinType{};
    } else {
        clone = settings_[index];
    }
    clone.type = std::move(type);
    settings_.push_back(std::move(clone));
    const auto clonedType = settings_.back().type;
    sortTypes();
    raiseSelectEvent();
    for (std::size_t i = 1; i < type_->items.size(); ++i)
        if (type_->items[i].text == clonedType) { select(static_cast<int>(i)); break; }
    return true;
}

EditSkinTypeDialog::EditSkinTypeDialog(Props& prop, Window parent, String title,
                                       const lib::ResourceId icon, String desc,
                                       std::vector<String> excludeTypes,
                                       String defaultValue, Services services)
    : prop_(&prop), parent_(std::move(parent)), services_(resolveServices(std::move(services))),
      title_(std::move(title)), icon_(icon), desc_(std::move(desc)),
      exclude_types_(std::move(excludeTypes)), type_(std::move(defaultValue)) {}

EditSkinTypeDialog::~EditSkinTypeDialog() {
    if (clear_size_binding_) clear_size_binding_();
    if (type_control_) {
        type_control_->on_selection_changed = {};
        type_control_->on_text_changed = {};
    }
    dialog_.reset();
}

void EditSkinTypeDialog::createDialog() {
    if (dialog_) return;
    dialog_ = makeDialog(services_);
    dialog_->title = title_;
    dialog_->icon = icon_;
    dialog_->resizable = true;
    lib::configure_owned_window(dialog_, parent_,
        lib::OwnedWindowKind::Modal);
    dialog_->minimum_size = lib::Size{360.0F, 145.0F};
    dialog_->set_layout(std::make_shared<lib::FillLayout>());
    content_ = makeComponent<lib::Panel>(services_, ComponentKind::Panel);
    setSingleColumnLayout(content_);
    dialog_->add(content_);
    setup(content_);
    auto buttons = makeComponent<lib::Panel>(services_, ComponentKind::Panel);
    auto buttonLayout = std::make_shared<lib::GridLayout>();
    buttonLayout->columns = 2;
    buttonLayout->equal_width = true;
    buttons->set_layout(std::move(buttonLayout));
    ok_button_ = makeComponent<lib::Button>(services_, ComponentKind::Button);
    ok_button_->text = prop_->requiredMsgs().dlgTextOK.value;
    ok_button_->default_button = true;
    ok_button_->on_click = [this] {
        if (type_control_) type_ = type_control_->edit_text;
        if (!valid()) return;
        dialog_->result = lib::DialogResult::OK;
        if (async_done_) { finishAsync(true); return; }
        if (services_.gui) services_.gui->close(dialog_);
    };
    buttons->add(ok_button_);
    auto cancel = makeComponent<lib::Button>(services_, ComponentKind::Button);
    cancel->text = prop_->requiredMsgs().dlgTextCancel.value;
    cancel->cancel_button = true;
    cancel->on_click = [this] {
        dialog_->result = lib::DialogResult::Cancel;
        if (async_done_) { finishAsync(false); return; }
        if (services_.gui) services_.gui->close(dialog_);
    };
    buttons->add(cancel);
    content_->add(buttons);
    dialog_->on_close_requested = [this] {
        dialog_->result = lib::DialogResult::Cancel;
        if (async_done_) { finishAsync(false); return; }
        if (services_.gui) services_.gui->close(dialog_);
    };
    clear_size_binding_ = prop_->bindDialogSize(dialog_, prop_->flex().editSkinTypeDlg);
    refreshValidity();
}

bool EditSkinTypeDialog::openAsync(std::function<void(bool)> done) {
    createDialog();
    if (!services_.gui || !done) return false;
    if (type_control_) type_ = type_control_->edit_text;
    async_done_ = std::move(done);
    if (services_.gui->begin_modal(dialog_, parent_)) return true;
    async_done_ = {};
    return false;
}

void EditSkinTypeDialog::finishAsync(const bool accepted) {
    auto done = std::exchange(async_done_, {});
    if (type_control_) type_ = type_control_->edit_text;
    if (services_.gui) services_.gui->end_modal(dialog_);
    if (done) done(accepted && valid());
}

void EditSkinTypeDialog::setup(lib::Container& area) {
    if (initialized_) return;
    auto group = makeComponent<lib::GroupBox>(services_, ComponentKind::GroupBox);
    group->title = desc_;
    setSingleColumnLayout(group);
    area.add(group);
    type_control_ = makeComponent<lib::ComboBox>(services_, ComponentKind::ComboBox);
    type_control_->editable = true;
    type_control_->visible_item_count = static_cast<std::size_t>(
        std::max<std::int32_t>(1, prop_->var().comboVisibleItemCount.value));
    type_control_->edit_text = type_;
    type_control_->on_selection_changed = [this](const std::optional<std::size_t> index) {
        if (index && *index < type_control_->items.size()) {
            type_ = type_control_->items[*index].text;
            type_control_->edit_text = type_;
        } else {
            type_ = type_control_->edit_text;
        }
        refreshValidity();
    };
    type_control_->on_text_changed = [this](const StringView value) {
        type_ = String(value);
        type_control_->edit_text = type_;
        refreshValidity();
    };
    group->add(type_control_);
    initialized_ = true;
    refreshTypes();
    setType(type_);
    refreshValidity();
}

void EditSkinTypeDialog::setup(const std::shared_ptr<lib::Container>& area) {
    if (area) setup(*area);
}

void EditSkinTypeDialog::refreshTypes() {
    if (!type_control_) return;
    const auto current = type_control_->edit_text.empty() ? type_ : type_control_->edit_text;
    std::vector<lib::ChoiceItem> items;
    for (const auto& type : dskin::findSkinTypes(*prop_)) {
        if (std::find(exclude_types_.begin(), exclude_types_.end(), type) !=
            exclude_types_.end()) {
            continue;
        }
        items.push_back({type, type, 0, true});
    }
    type_ = current;
    std::optional<std::size_t> selected;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].text == current) {
            selected = i;
            break;
        }
    }
    (void)lib::replace_combo_items(type_control_, std::move(items), selected,
                                   current);
    refreshValidity();
}

void EditSkinTypeDialog::setType(String value) {
    type_ = std::move(value);
    if (!type_control_) return;
    std::optional<std::size_t> selected;
    for (std::size_t i = 0; i < type_control_->items.size(); ++i) {
        if (type_control_->items[i].text == type_) {
            selected = i;
            break;
        }
    }
    auto items = type_control_->items;
    (void)lib::replace_combo_items(type_control_, std::move(items), selected,
                                   type_);
    refreshValidity();
}

bool EditSkinTypeDialog::valid() const {
    return !type_.empty() &&
           std::find(exclude_types_.begin(), exclude_types_.end(), type_) ==
               exclude_types_.end();
}

void EditSkinTypeDialog::refreshValidity() noexcept {
    if (ok_button_) ok_button_->set_enabled(valid());
}

bool EditSkinTypeDialog::open() {
    createDialog();
    if (type_control_) type_ = type_control_->edit_text;
    const auto result = services_.gui ? services_.gui->show_modal(dialog_, parent_) :
                                        lib::DialogResult::Cancel;
    if (!accepted(result)) return false;
    if (type_control_) type_ = type_control_->edit_text;
    return valid();
}

DefBgImgDialog::DefBgImgDialog(Props& prop, Window parent,
                               const std::vector<BgImageS>& bgImagesDefault,
                               const Skin& skin, Services services,
                               std::function<lib::Action(lib::Action)> subscribeUndoMax)
    : prop_(&prop), parent_(std::move(parent)), skin_(&skin),
      services_(resolveServices(std::move(services))),
      subscribe_undo_max_(std::move(subscribeUndoMax)),
      container_(std::make_shared<background::BgImageContainer>(
          ::cwxeditor::cwx::skin::createBgImages(skin, bgImagesDefault))) {}

DefBgImgDialog::~DefBgImgDialog() {
    if (clear_size_binding_) clear_size_binding_();
    lifetime_.reset();
    if(background_view_){background_view_->modEvent.clear();background_view_->retire();}
    retireBackgroundEditors();
    background_view_.reset();
    if (dialog_) dialog_->on_close_requested = {};
    if (view_) {
        view_->on_cell_edited = {};
        view_->on_selection_changed = {};
    }
    appliedEvent.clear();
    closeEvent.clear();
    if (dialog_) {
        dialog_->set_visible(false);
        if (services_.gui) services_.gui->close(dialog_);
    }
}

void DefBgImgDialog::createDialog() {
    if (dialog_) return;
    dialog_ = makeDialog(services_);
    dialog_->title = prop_->requiredMsgs().dlgTitBgImagesDefault.value;
    dialog_->icon = prop_->images().load(StringView{"settings.png"});
    dialog_->resizable = true;
    lib::configure_owned_window(dialog_, parent_,
        lib::OwnedWindowKind::Modeless);
    dialog_->minimum_size = lib::Size{700.0F, 430.0F};
    dialog_->set_layout(std::make_shared<lib::FillLayout>());
    content_ = makeComponent<lib::Panel>(services_, ComponentKind::Panel);
    setSingleColumnLayout(content_);
    dialog_->add(content_);
    setup(content_);
    auto buttons = makeComponent<lib::Panel>(services_, ComponentKind::Panel);
    auto buttonLayout = std::make_shared<lib::GridLayout>();
    buttonLayout->columns = 3;
    buttonLayout->equal_width = true;
    buttons->set_layout(std::move(buttonLayout));
    auto ok = makeComponent<lib::Button>(services_, ComponentKind::Button);
    ok->text = prop_->requiredMsgs().dlgTextOK.value;
    ok->default_button = true;
    const std::weak_ptr<void> weakLife = lifetime_;
    ok->on_click = [this, weakLife] {
        if (weakLife.expired() || retired_) return;
        if (!apply()) return;
        if (weakLife.expired()) return;
        retired_=true;
        const auto model=container_;const auto view=background_view_;
        if(view){view->modEvent.clear();view->retire();}
        if(weakLife.expired())return;
        retireBackgroundEditors();
        if(weakLife.expired())return;
        background_view_.reset();
        const auto window=dialog_;const auto gui=services_.gui;
        window->result = lib::DialogResult::OK;
        if (gui) gui->close(window);
        if (!weakLife.expired()) raiseClosed();
    };
    buttons->add(ok);
    auto cancel = makeComponent<lib::Button>(services_, ComponentKind::Button);
    cancel->text = prop_->requiredMsgs().dlgTextCancel.value;
    cancel->cancel_button = true;
    cancel->on_click = [this, weakLife] {
        if (weakLife.expired() || retired_) return;
        forceCancel();
    };
    buttons->add(cancel);
    apply_button_ = makeComponent<lib::Button>(services_, ComponentKind::Button);
    apply_button_->text = prop_->requiredMsgs().dlgTextApply.value;
    apply_button_->set_enabled(false);
    apply_button_->on_click = [this, weakLife] {
        if (weakLife.expired() || retired_) return;
        if (!apply()) return;
        if (weakLife.expired()) return;
        dialog_->result = lib::DialogResult::Apply;
    };
    buttons->add(apply_button_);
    content_->add(buttons);
    dialog_->on_close_requested = [this, weakLife] {
        if (weakLife.expired() || retired_) return;
        forceCancel();
    };
    clear_size_binding_ = prop_->bindDialogSize(dialog_, prop_->flex().defBgImagesDlg);
}

void DefBgImgDialog::setup(lib::Container& area) {
    if(initialized_ || retired_ || !skin_ || !container_)return;
    areaviewutils::Commons common(*prop_,*skin_);common.services=services_;common.dialogOwner=dialog_;
    common.undoMaxEvent=static_cast<std::size_t>(std::max(0,prop_->var().undoMaxEvent.value));
    const std::weak_ptr<void> life=lifetime_;
    common.undoMaxProvider=[prop=prop_,life]{return life.expired()?0U:static_cast<std::size_t>(std::max(0,prop->var().undoMaxEvent.value));};
    common.subscribeUndoMax=subscribe_undo_max_;
    background_view_=areaviewutils::createBgImagesViewAndMenu(common,*prop_,nullptr,*skin_,{},*container_,area,nullptr,prop_->var().showInheritBackground.value,false);
    // MainWindow のシーンダイアログと同じ、公開のホスト連携経路を再利用する。
    background_view_->context().create_background=[this,life](int type){if(!life.expired()&&!retired_)createBackgroundEditor(type);};
    background_view_->context().callbacks.edit=[this,life]{
        if(life.expired()||retired_||!background_view_)return;
        const auto selection=background_view_->backList().selection;const auto backs=container_->backs();
        for(const auto row:selection){if(life.expired()||retired_)return;if(row<backs.size())openBackgroundEditor(backs[row],false);}
    };
    background_view_->context().callbacks.edit_target=[this,life](
        const ::cwxeditor::cwx::editor::gui::dwt::areawindow::SceneEditTargetKind kind,
        const std::size_t index){
        if(life.expired()||retired_||!background_view_||!container_||
           kind!=::cwxeditor::cwx::editor::gui::dwt::areawindow::SceneEditTargetKind::Background)return;
        const auto backs=container_->backs();if(index>=backs.size()||!backs[index])return;
        const auto old=background_editors_.find(backs[index].get());
        if(old!=background_editors_.end()){
            if(old->second)old->second->active();
            return;
        }
        // D版と同様、既存のモードレスエディタを再利用してからクリック行を選び
        // 、新規エディタはその行だけに結び付ける。 このコールバックは BgImagesView::editBack
        // の選択同期後に実行されるため、 editBack
        // に入り直さず、ダイアログを開く処理を直接呼ぶ。
        background_view_->selectBackIndex(index);
        if (life.expired() || retired_ || !background_view_) return;
        openBackgroundEditor(backs[index], false);
    };
    background_view_->modEvent.push_back([this,life]{
        if(life.expired()||retired_)return;modified_=true;
        if(dialog_)dialog_->modified=true;if(apply_button_)apply_button_->set_enabled(true);
    });
    // 別名のポインターではなく、実際の表ウィジェットを保持する。 コントロー
    // ラーを、未確定モデルやコンテナーの寿命より長く保持しないようにする。
    const auto target=&background_view_->backList();
    const std::function<std::shared_ptr<lib::TableView>(const lib::WidgetPtr&)> find=[&](const auto& widget)->std::shared_ptr<lib::TableView>{
        if(widget.get()==target)return std::dynamic_pointer_cast<lib::TableView>(widget);
        if(const auto parent=std::dynamic_pointer_cast<lib::Container>(widget))for(const auto& child:parent->children())if(auto found=find(child))return found;
        return {};
    };
    view_=find(background_view_->root());
    initialized_=true;
}

void DefBgImgDialog::createBackgroundEditor(int type) {
    if(retired_ || !background_view_)return;
    background::BgImagePtr value;
    if(type==1){auto cell=std::make_shared<background::TextCell>();cell->width(prop_->var().textCellDefaultWidth.value);cell->height(prop_->var().textCellDefaultHeight.value);value=std::move(cell);}
    else if(type==2){auto cell=std::make_shared<background::ColorCell>();cell->width(prop_->var().colorCellDefaultWidth.value);cell->height(prop_->var().colorCellDefaultHeight.value);value=std::move(cell);}
    else if(type==3){auto cell=std::make_shared<background::PCCell>();const auto size=prop_->looks().cardSize();cell->width(static_cast<std::int32_t>(size.width));cell->height(static_cast<std::int32_t>(size.height));value=std::move(cell);}
    else value=std::make_shared<background::ImageCell>();
    openBackgroundEditor(std::move(value),true);
}

void DefBgImgDialog::openBackgroundEditor(background::BgImagePtr back,bool create) {
    if(retired_||!background_view_||!back||!skin_)return;
    const std::weak_ptr<void> life=lifetime_;
    if(const auto old=background_editors_.find(back.get());old!=background_editors_.end()){const auto dialog=old->second;if(dialog)dialog->active();return;}
    bgimagedialog::BgImageDialogContext context(areaviewutils::Commons(*prop_,*skin_));
    context.services=services_;context.common.services=services_;context.common.dialogOwner=dialog_;
    context.skin_owner=const_cast<Skin*>(skin_)->weak_from_this().lock();
    std::shared_ptr<bgimagedialog::BgImageDialog> dialog;
    if(auto cell=std::dynamic_pointer_cast<background::ImageCell>(back))dialog=std::make_shared<bgimagedialog::ImageCellDialog>(context,*prop_,nullptr,skin_,cell,create,dialog_);
    else if(auto cell=std::dynamic_pointer_cast<background::TextCell>(back))dialog=std::make_shared<bgimagedialog::TextCellDialog>(context,*prop_,nullptr,skin_,cell,create,dialog_);
    else if(auto cell=std::dynamic_pointer_cast<background::ColorCell>(back))dialog=std::make_shared<bgimagedialog::ColorCellDialog>(context,*prop_,nullptr,cell,create,dialog_);
    else if(auto cell=std::dynamic_pointer_cast<background::PCCell>(back))dialog=std::make_shared<bgimagedialog::PCCellDialog>(context,*prop_,nullptr,skin_,cell,create,dialog_);
    if(!dialog)return;
    const auto key=back.get();const auto inserted=std::make_shared<bool>(!create);
    const auto transaction=std::make_shared<areaview::BgImagesView::ExternalBackEditPtr>();
    const auto weak_dialog=std::weak_ptr<bgimagedialog::BgImageDialog>(dialog);
    const auto weak_view=std::weak_ptr<areaview::BgImagesView>(background_view_);
    const auto model=container_;
    dialog->applyEvent.push_back([life,weak_view,weak_dialog,inserted,transaction,model]{
        if(life.expired()||!*inserted)return;
        const auto view=weak_view.lock();const auto current=weak_dialog.lock();if(view&&current)*transaction=view->beginExternalBackEdit(current->back());
    });
    dialog->appliedEvent.push_back([this,life,weak_view,weak_dialog,inserted,transaction,model]{
        if(life.expired()||retired_)return;
        const auto view=weak_view.lock();const auto current=weak_dialog.lock();if(!view||!current)return;
        if(!*inserted){const auto index=view->insertCreatedBgImage(current->back());if(index>=0)*inserted=true;}
        else (void)view->commitExternalBackEdit(*transaction);
    });
    dialog->closeEvent.push_back([this,life,key,weak_dialog,weak_view,transaction,model]{
        if(life.expired())return;
        if(const auto view=weak_view.lock())view->endExternalBackEdit(*transaction);
        if(life.expired())return;
        transaction->reset();const auto current=weak_dialog.lock();
        const auto found=background_editors_.find(key);if(found!=background_editors_.end()&&found->second==current)background_editors_.erase(found);
    });
    background_editors_.emplace(key,dialog);
    dialog->modal(false);(void)dialog->open();
}

void DefBgImgDialog::retireBackgroundEditors() {
    auto editors=std::move(background_editors_);background_editors_.clear();
    for(auto& [key,dialog]:editors){if(!dialog)continue;dialog->applyEvent.clear();dialog->appliedEvent.clear();dialog->closeEvent.clear();dialog->forceCancel();}
}

void DefBgImgDialog::setup(const std::shared_ptr<lib::Container>& area) {
    if (area) setup(*area);
}

void DefBgImgDialog::refreshView() {
    if(background_view_ && !retired_)background_view_->refresh();
}

std::vector<BgImageS> DefBgImgDialog::backs() const {
    if (!container_) return {};
    const auto cells = container_->backs();
    // ネイティブのスキンDTOへの変換では、独立した配列のスナップショットを取得する。
    ::cwxeditor::cwx::background::BgImageArray snapshot;
    snapshot.reserve(cells.size());
    for (const auto& cell : cells) snapshot.push_back(cell);
    return ::cwxeditor::cwx::skin::createBgImageSs(snapshot);
}

void DefBgImgDialog::setBacks(const std::vector<BgImageS>& value) {
    if (retired_ || !container_ || !skin_) return;
    container_->backs(::cwxeditor::cwx::skin::createBgImages(*skin_, value));
    modified_ = true;
    if (dialog_) dialog_->modified = true;
    if (apply_button_) apply_button_->set_enabled(true);
    refreshView();
}

bool DefBgImgDialog::append(const BgImageS& value) {
    auto values = backs();
    values.push_back(value);
    setBacks(values);
    return true;
}

bool DefBgImgDialog::remove(const std::size_t index) {
    auto values = backs();
    if (index >= values.size()) return false;
    values.erase(values.begin() + static_cast<std::ptrdiff_t>(index));
    setBacks(values);
    return true;
}

bool DefBgImgDialog::move(const std::size_t first, const std::size_t second) {
    auto values = backs();
    if (first >= values.size() || second >= values.size()) return false;
    if (first == second) return true;
    const auto value = values[first];
    values.erase(values.begin() + static_cast<std::ptrdiff_t>(first));
    values.insert(values.begin() + static_cast<std::ptrdiff_t>(second), value);
    setBacks(values);
    return true;
}

void DefBgImgDialog::raiseApplied() {
    const std::weak_ptr<void> life = lifetime_;
    const auto callbacks = appliedEvent;
    for (const auto& callback : callbacks) {
        if (callback) callback();
        if (life.expired()) return;
    }
}

void DefBgImgDialog::raiseClosed() {
    const std::weak_ptr<void> life = lifetime_;
    const auto callbacks = closeEvent;
    for (const auto& callback : callbacks) {
        if (callback) callback();
        if (life.expired()) return;
    }
}

bool DefBgImgDialog::apply() {
    if (retired_) return false;
    modified_ = false;
    if (dialog_) dialog_->modified = false;
    if (apply_button_) apply_button_->set_enabled(false);
    raiseApplied();
    return true;
}

bool DefBgImgDialog::open() {
    if (retired_) return false;
    createDialog();
    dialog_->result = lib::DialogResult::None;
    dialog_->set_visible(true);
    if (services_.gui) services_.gui->show(dialog_);
    return false;
}

bool DefBgImgDialog::active() {
    if (retired_) return false;
    if (!dialog_ || !dialog_->visible()) return open();
    if (services_.gui) services_.gui->focus(dialog_);
    return true;
}

void DefBgImgDialog::forceCancel() {
    if (retired_) return;
    retired_ = true;
    const std::weak_ptr<void> guard=lifetime_;
    const auto model=container_;const auto view=background_view_;
    if(view){view->modEvent.clear();view->retire();}
    if(guard.expired())return;
    retireBackgroundEditors();
    if(guard.expired())return;
    background_view_.reset();
    modified_ = false;
    appliedEvent.clear();
    if (apply_button_) apply_button_->set_enabled(false);
    if(guard.expired())return;
    const auto window = dialog_;
    const auto gui = services_.gui;
    const std::weak_ptr<void> life = lifetime_;
    if (window) {
        window->modified = false;
        window->result = lib::DialogResult::Cancel;
        window->set_visible(false);
        if (gui) gui->close(window);
    }
    if (!life.expired()) raiseClosed();
}

template <typename Component>
std::shared_ptr<Component> ElementOverrideView::createComponent(
    const lib::ComponentKind kind) const {
    return makeComponent<Component>(services_, kind);
}

ElementOverrideView::ElementOverrideView(Props& prop, Parent parent, const int style,
                                         Services services)
    : prop_(&prop), root_(std::move(parent)), services_(resolveServices(std::move(services))) {
    (void)style;
    if (!root_) root_ = createComponent<lib::Panel>(ComponentKind::Panel);
    build();
}

ElementOverrideView::ElementOverrideView(Props& prop, lib::Container& parent,
                                         const int style, Services services)
    : ElementOverrideView(prop, Parent{}, style, std::move(services)) {
    parent.add(root_);
}

ElementOverrideView::~ElementOverrideView() {
    lifetime_.reset();
    if(menu_) {menu_->on_open={};menu_->on_command={};}
    if(elements_)elements_->on_column_resized={};
    if (elements_) {
        elements_->on_cell_edited = {};
        elements_->on_selection_changed = {};
    }
    modEvent.clear();
}

void ElementOverrideView::build() {
    auto layout = std::make_shared<lib::FillLayout>();
    root_->set_layout(std::move(layout));
    elements_ = createComponent<lib::TableView>(ComponentKind::TableView);
    elements_->columns = {
        {prop_->requiredMsgs().elementOverrideName.value,
         static_cast<float>(prop_->var().elementOverrideNameColumn.value),
         lib::TextAlignment::Start, true, false},
        {prop_->requiredMsgs().elementOverrideTargetType.value,
         static_cast<float>(prop_->var().elementOverrideTargetTypeColumn.value),
         lib::TextAlignment::Start, true, false},
    };
    elements_->on_column_resized = [this,life=std::weak_ptr<void>(lifetime_)](const std::size_t column,
                                           const float width) {
        if (life.expired() || !prop_ || !std::isfinite(width) || width <= 0.0F) return;
        const auto saved = static_cast<std::int32_t>(std::lround(width));
        if (column == 0U) prop_->var().elementOverrideNameColumn.value = saved;
        else if (column == 1U)
            prop_->var().elementOverrideTargetTypeColumn.value = saved;
        else return;
        (void)prop_->apply();
    };
    elements_->on_cell_edited = [this,life=std::weak_ptr<void>(lifetime_)](const std::size_t row, const std::size_t column,
                                       String value) {
        if (life.expired() || !enabled_ || row >= list_.size() || column > 1) return;
        const auto element = list_[row].element;
        if (column == 1 && !canEditTarget(element)) return;
        auto& current = overrideFor(element);
        if (column == 0) {
            if (current.name == value) return;
            store();
            current.name = std::move(value);
        } else {
            if (current.targetType == value) return;
            store();
            current.targetType = std::move(value);
        }
        const auto revision=model_revision_;
        refreshTable();
        if(life.expired() || model_revision_!=revision)return;
        raiseModEvent();
    };
    root_->add(elements_);
    list_.resize(allElements.size());
    for (std::size_t i = 0; i < allElements.size(); ++i) list_[i].element = allElements[i];
    setupActions();
    refreshTable();
}

void ElementOverrideView::setupActions() {
    const std::weak_ptr<void> life=lifetime_;
    menu_=std::make_shared<lib::Menu>(true);
    const auto item=[this](types::MenuID id,String command){return lib::MenuItem{prop_->requiredMsgs().menuText(id),std::move(command),prop_->flex().menu.hotkey(id),prop_->images().menu(id)};};
    menu_->items={item(types::MenuID::Undo,"undo"),item(types::MenuID::Redo,"redo"),{.separator=true},item(types::MenuID::SelectIcon,"select_icon"),item(types::MenuID::SelectPresetIcon,"preset"),item(types::MenuID::DeleteIcon,"delete_icon")};
    const auto weak_menu=std::weak_ptr<lib::Menu>(menu_);
    menu_->on_open=[this,life,weak_menu]{
        if(life.expired())return;const auto menu=weak_menu.lock();if(!menu)return;
        const auto rows=elements_->selection;const auto row=rows.empty()?list_.size():rows.front();const bool selected=enabled_&&row<list_.size();
        menu->items[0].enabled=!undo_.empty();menu->items[1].enabled=!redo_.empty();
        menu->items[3].enabled=selected;menu->items[4].enabled=selected;menu->items[5].enabled=selected&&!list_[row].icon.empty();
        auto& children=menu->items[4].children;children.clear();
        for(const auto& icon:dprops::presetIcons(*prop_))children.push_back({icon.name,"preset:"+icon.file,{},icon.image,selected && list_[row].icon!=String(dprops::PRESET_IMAGE_SCHEMA)+icon.file});
    };
    menu_->on_command=[this,life](StringView command){
        if(life.expired())return;if(command=="undo"){(void)undo();return;}if(command=="redo"){(void)redo();return;}
        const auto rows=elements_->selection;if(!enabled_||rows.empty()||rows.front()>=list_.size())return;
        const auto element=list_[rows.front()].element;
        if(command=="select_icon")(void)selectIcon(element);
        else if(command=="delete_icon")(void)deleteIcon(element);
        else if(command.starts_with("preset:"))(void)setPresetIcon(element,command.substr(7));
    };
    const auto gui=services_.gui;const auto menu=menu_;const auto weak_table=std::weak_ptr<lib::TableView>(elements_);
    elements_->on_pointer=[life,weak_table,gui,menu](lib::PointerEvent& event){
        if(life.expired()||event.handled||!gui||event.action!=lib::PointerAction::Down||event.button!=lib::PointerButton::Secondary)return;
        const auto table=weak_table.lock();if(!table||!table->enabled())return;
        if(menu->on_open)menu->on_open();if(life.expired())return;event.handled=true;gui->show_context_menu(menu,table,event.position);
    };
    elements_->on_key_down=[life,menu](lib::KeyEvent& event){if(!life.expired()&&!event.handled && lib::dispatch_menu_shortcut(menu,event.key,event.modifiers,event.printable))event.handled=true;};
}

void ElementOverrideView::refreshTable(const bool resetTop) {
    if (!elements_) return;
    const auto table = elements_;
    std::vector<lib::TableRow> rows;
    rows.reserve(list_.size());
    for (const auto& value : list_) {
        lib::TableRow row;
        row.cells.resize(2);
        row.cells[0].text = value.name.empty() ? prop_->requiredMsgs().elementName(value.element)
                                               : value.name;
        row.cells[0].value = value.name;
        row.cells[0].editor_value = value.name;
        row.cells[0].icon = prop_->elementImage(value, image_cache_);
        row.cells[1].text = value.targetType.empty() ? elementTargetType(*prop_, value.element)
                                                     : value.targetType;
        row.cells[1].value = value.targetType;
        row.cells[1].editor_value = value.targetType;
        rows.push_back(std::move(row));
    }
    (void)lib::replace_table_rows(*table, std::move(rows),
                                  table->selection, std::nullopt, {}, resetTop);
}

void ElementOverrideView::store() {
    ++model_revision_;
    const auto configured = prop_->var().undoMaxEtc.value;
    const auto limit = configured > 0 ? static_cast<std::size_t>(configured) : 0U;
    if (limit == 0) {
        redo_.clear();
        return;
    }
    undo_.push_back(list_);
    if (undo_.size() > limit) undo_.erase(undo_.begin());
    redo_.clear();
}

std::size_t ElementOverrideView::indexOf(const Element element) const noexcept {
    const auto value = static_cast<std::int32_t>(element);
    return value >= 0 && static_cast<std::size_t>(value) < list_.size()
               ? static_cast<std::size_t>(value)
               : list_.size();
}

bool ElementOverrideView::canEditTarget(const Element element) const noexcept {
    switch (element) {
    case Element::Health:
    case Element::Mind:
    case Element::Miracle:
    case Element::Magic:
        return true;
    case Element::All:
    case Element::Fire:
    case Element::Ice:
        return false;
    }
    return false;
}

ElementOverride& ElementOverrideView::overrideFor(const Element element) noexcept {
    return list_[indexOf(element)];
}

const ElementOverride& ElementOverrideView::overrideFor(const Element element) const noexcept {
    return list_[indexOf(element)];
}

void ElementOverrideView::raiseModEvent() {
    const std::weak_ptr<void> life=lifetime_;const auto callbacks=modEvent;
    for (const auto& callback : callbacks) {
        if(life.expired())return;
        if (callback) callback();
    }
}

std::vector<ElementOverride> ElementOverrideView::elementOverrides() const {
    std::vector<ElementOverride> result;
    for (const auto& value : list_) {
        if (!value.icon.empty() || !value.name.empty() || !value.targetType.empty()) {
            result.push_back(value);
        }
    }
    return result;
}

void ElementOverrideView::elementOverrides(const std::vector<ElementOverride>& values) {
    ++model_revision_;
    clearHistory();
    list_.resize(allElements.size());
    for (std::size_t i = 0; i < allElements.size(); ++i) list_[i] = ElementOverride{allElements[i]};
    for (const auto& value : values) {
        const auto index = indexOf(value.element);
        if (index < list_.size()) list_[index] = value;
    }
    refreshTable(true);
}

bool ElementOverrideView::setName(const Element element, String value) {
    const std::weak_ptr<void> life=lifetime_;
    const auto index = indexOf(element);
    if (!enabled_ || index >= list_.size()) return false;
    auto& current = list_[index];
    if (current.name == value) return false;
    store();
    current.name = std::move(value);
    const auto revision=model_revision_;
    refreshTable();
    if(life.expired() || model_revision_!=revision)return true;
    raiseModEvent();
    return true;
}

bool ElementOverrideView::setTargetType(const Element element, String value) {
    const std::weak_ptr<void> life=lifetime_;
    const auto index = indexOf(element);
    if (index >= list_.size() || !canEditTarget(element)) return false;
    auto& current = list_[index];
    if (current.targetType == value) return false;
    store();
    current.targetType = std::move(value);
    const auto revision=model_revision_;
    refreshTable();
    if(life.expired() || model_revision_!=revision)return true;
    raiseModEvent();
    return true;
}

bool ElementOverrideView::setIcon(const Element element, String value) {
    const std::weak_ptr<void> life=lifetime_;
    const auto index = indexOf(element);
    if (!enabled_ || index >= list_.size()) return false;
    auto& current = list_[index];
    if (current.icon == value) return false;
    store();
    current.icon = std::move(value);
    const auto revision=model_revision_;
    refreshTable();
    if(life.expired() || model_revision_!=revision)return true;
    raiseModEvent();
    return true;
}

bool ElementOverrideView::selectIcon(const Element element) {
    const auto request_revision=++picker_revision_;const auto revision=model_revision_;
    const auto selection=elements_?elements_->selection:std::vector<std::size_t>{};
    const std::weak_ptr<void> life=lifetime_;
    const auto index = indexOf(element);
    if (!enabled_ || index >= list_.size() || !services_.file_picker) return false;
    lib::FilePickerRequest request;
    request.title = prop_->requiredMsgs().selectElementIcon.value;
    std::vector<String> patterns;
    for (const auto& extension : image_extensions_)
        patterns.push_back(extension.starts_with("*") ? extension : "*" + extension);
    request.filters = {{prop_->requiredMsgs().filterImage.value, std::move(patterns), {}},
                       {prop_->requiredMsgs().filterAll.value, {"*"}, {}}};
    const auto& existing = list_[index].icon;
    if (!existing.empty() && !existing.starts_with(dprops::PRESET_IMAGE_SCHEMA)) {
        const auto absolute = structs::pathFromUtf8(prop_->parent().toAppAbs(existing));
        request.initial_directory = absolute.parent_path();
        request.initial_name = structs::pathToUtf8(absolute.filename());
    } else {
        std::error_code error;
        request.initial_directory = std::filesystem::current_path(error);
    }
    request.allow_multiple = false;
    const auto picker=services_.file_picker;
    const auto files = picker->open_file(request);
    if(life.expired() || picker_revision_!=request_revision || model_revision_!=revision || !enabled_ || (elements_ && elements_->selection!=selection))return false;
    if (!files.succeeded() || files.selections.empty()) return false;
    const auto chosen = files.selections.front().native_path();
    if (!chosen) return false;
    const auto base = structs::pathFromUtf8(prop_->parent().appPath()).parent_path();
    std::error_code error;
    const auto relative = std::filesystem::relative(*chosen, base, error);
    const String value = structs::pathToUtf8(
        !error && !relative.empty() && isChildOf(*chosen, base) ? relative : *chosen);
    return setIcon(element, value);
}

bool ElementOverrideView::setPresetIcon(const Element element, const StringView file) {
    if (file.empty()) return false;
    return setIcon(element, String(dprops::PRESET_IMAGE_SCHEMA) + String(file));
}

bool ElementOverrideView::deleteIcon(const Element element) {
    const std::weak_ptr<void> life=lifetime_;
    const auto index = indexOf(element);
    if (index >= list_.size() || list_[index].icon.empty()) return false;
    return setIcon(element, {});
}

bool ElementOverrideView::undo() {
    const std::weak_ptr<void> life=lifetime_;
    ++model_revision_;
    if (undo_.empty()) return false;
    redo_.push_back(list_);
    list_ = std::move(undo_.back());
    undo_.pop_back();
    const auto revision=model_revision_;
    refreshTable();
    if(life.expired() || model_revision_!=revision)return true;
    raiseModEvent();
    return true;
}

bool ElementOverrideView::redo() {
    const std::weak_ptr<void> life=lifetime_;
    ++model_revision_;
    if (redo_.empty()) return false;
    undo_.push_back(list_);
    list_ = std::move(redo_.back());
    redo_.pop_back();
    const auto revision=model_revision_;
    refreshTable();
    if(life.expired() || model_revision_!=revision)return true;
    raiseModEvent();
    return true;
}

void ElementOverrideView::clearHistory() noexcept {
    undo_.clear();
    redo_.clear();
}

void ElementOverrideView::refUndoMax() noexcept {
    if (!prop_) return;
    const auto configured = prop_->var().undoMaxEtc.value;
    const auto limit = configured > 0 ? static_cast<std::size_t>(configured) : 0U;
    if (undo_.size() > limit) {
        undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(undo_.size() - limit));
    }
}

void ElementOverrideView::enabled(const bool value) noexcept {
    enabled_ = value;
    if (elements_) elements_->set_enabled(value);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::skintypesettings
