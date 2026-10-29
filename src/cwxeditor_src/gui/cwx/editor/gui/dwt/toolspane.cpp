#include "toolspane.hpp"

#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/cab.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"

#include <cmath>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace cwxeditor::cwx::editor::gui::dwt::toolspane {

namespace dutils = ::cwxeditor::cwx::editor::gui::dwt::dutils;

namespace {

using namespace std::string_literals;

[[nodiscard]] lib::String enumText(const MenuID id) {
    try {
        return ::cwxeditor::cwx::enumutils::enumToString(id);
    } catch (...) {
        return {};
    }
}

[[nodiscard]] std::int32_t intValue(const double value) {
    return static_cast<std::int32_t>(std::lround(value));
}

[[nodiscard]] lib::String featureValue(const structs::ClassicEngine& value,
                                       const FeatureDefinition& row) {
    switch (row.group) {
    case FeatureDefinition::Group::Ok:
        return value.okText.value_or(lib::String{});
    case FeatureDefinition::Group::Sex: {
        const auto it = value.sexName.find(row.key);
        return it == value.sexName.end() ? lib::String{} : it->second;
    }
    case FeatureDefinition::Group::Period: {
        const auto it = value.periodName.find(row.key);
        return it == value.periodName.end() ? lib::String{} : it->second;
    }
    case FeatureDefinition::Group::Nature: {
        const auto it = value.natureName.find(row.key);
        return it == value.natureName.end() ? lib::String{} : it->second;
    }
    case FeatureDefinition::Group::Makings: {
        const auto it = value.makingsName.find(row.key);
        return it == value.makingsName.end() ? lib::String{} : it->second;
    }
    case FeatureDefinition::Group::ActionCard: {
        const auto key = static_cast<types::ActionCardType>(row.action_card);
        const auto it = value.actionCardName.find(key);
        return it == value.actionCardName.end() ? lib::String{} : it->second;
    }
    }
    return {};
}

void assignFeatureValue(structs::ClassicEngine& value,
                        const FeatureDefinition& row, lib::String text) {
    switch (row.group) {
    case FeatureDefinition::Group::Ok:
        if (text.empty()) value.okText.reset();
        else value.okText = std::move(text);
        break;
    case FeatureDefinition::Group::Sex:
        if (text.empty()) value.sexName.erase(row.key);
        else value.sexName[row.key] = std::move(text);
        break;
    case FeatureDefinition::Group::Period:
        if (text.empty()) value.periodName.erase(row.key);
        else value.periodName[row.key] = std::move(text);
        break;
    case FeatureDefinition::Group::Nature:
        if (text.empty()) value.natureName.erase(row.key);
        else value.natureName[row.key] = std::move(text);
        break;
    case FeatureDefinition::Group::Makings:
        if (text.empty()) value.makingsName.erase(row.key);
        else value.makingsName[row.key] = std::move(text);
        break;
    case FeatureDefinition::Group::ActionCard: {
        const auto key = static_cast<types::ActionCardType>(row.action_card);
        if (text.empty()) value.actionCardName.erase(key);
        else value.actionCardName[key] = std::move(text);
        break;
    }
    }
}

[[nodiscard]] bool isDirectory(const std::filesystem::path& value) {
    std::error_code error;
    return std::filesystem::is_directory(value, error);
}

[[nodiscard]] std::filesystem::path parentPath(
    const std::filesystem::path& value) {
    const auto parent = value.parent_path();
    return parent.empty() ? value : parent;
}

}  // namespace

lib::String Presentation::text(const MenuID id) const {
    if (menuText) return menuText(id);
    return enumText(id);
}

lib::ResourceId Presentation::image(const MenuID id) const {
    return menuImage ? menuImage(id) : lib::ResourceId{};
}

lib::String Presentation::menuSample(lib::String name, lib::String mnemonic,
                                     lib::String hotkey, const bool escape) const {
    if (formatMenuSample)
        return formatMenuSample(std::move(name), std::move(mnemonic),
                                std::move(hotkey), escape);
    if (name.empty()) name = noName;
    if (!mnemonic.empty()) name += "\t" + mnemonic;
    if (!hotkey.empty()) name += " [" + hotkey + "]";
    return name;
}

template <typename T>
ToolsPane<T>::ToolsPane(
    Commons comm, std::function<void(bool)> ignoreMod,
    std::function<bool()> catchMod, std::function<void()> applyEnabled,
    Parent parent, const std::int32_t /*style*/, Presentation presentation)
    : comm_(std::move(comm)), presentation_(std::move(presentation)),
      callbacks_{std::move(ignoreMod), std::move(catchMod),
                 std::move(applyEnabled)},
      parent_(parent), undo_(presentation_.undoMaxEtc),
      clipboard_(std::make_unique<xmlbytestransfer::ClipData>(
          comm_.clipboard())),
      featureUndo_(presentation_.undoMaxEtc) {
    if (!comm_.undoMaxEtc) comm_.undoMaxEtc = presentation_.undoMaxEtc;
    if (comm_.presentation.textTabs == 0U)
        comm_.presentation.textTabs = presentation_.tabs;
    if (comm_.presentation.undoMaxEtc == 100U)
        comm_.presentation.undoMaxEtc = presentation_.undoMaxEtc;
    if (!comm_.presentation.menuText)
        comm_.presentation.menuText = [this](const MenuID id) {
            return presentation_.text(id);
        };
    // コンテキストメニューのモデルは直ちに作るが、実体化する
    // のは開くときだけ。 設定ダイアログの構築中に全アイコン
    // を復号せず、表示中のボタンは引き続き Presentation::image
    // を使う。 コマンドを保ちつつ、開かないかも
    // しれないメニューに数秒かけることを避ける。
    if (!comm_.refreshToolBar) comm_.refreshToolBar = [] {};
    if (!callbacks_.catchMod) callbacks_.catchMod = [] { return true; };
    if (!callbacks_.applyEnabled) callbacks_.applyEnabled = [] {};
    build();
    refMenu(MenuID::Undo);
    refMenu(MenuID::Redo);
    const std::weak_ptr<void> life{pointer_lifetime_};
    if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
        if (presentation_.subscribeSkinTypeChanges)
            skinTypesDisconnect_ = presentation_.subscribeSkinTypeChanges(
                [this, life] {
                    if (!life.expired()) refreshSkinTypeChoices();
                });
    }
    if (presentation_.subscribeMenu)
        menuDisconnect_ = presentation_.subscribeMenu(
            [this, life](const MenuID id) {
                if (!life.expired()) refMenu(id);
            });
    // D版の ToolsPane と同様、Commons.refUndoMax を購読
    // し、設定で新しい上限を適用したら、 今後の追加だけでなく既存の履歴も切り詰める。
    if (comm_.subscribeUndoMax)
        undoMaxDisconnect_ = comm_.subscribeUndoMax([this, life] {
            if (!life.expired()) refUndoMax();
        });
}

template <typename T>
std::shared_ptr<lib::TextEdit> ToolsPane<T>::makeText(
    lib::Container& parent, lib::String value, const bool multiline,
    const bool readOnly) {
    auto result = detail::makeComponent<lib::TextEdit>(
        comm_.services, lib::ComponentKind::TextEdit);
    result->text = std::move(value);
    result->multiline = multiline;
    result->wrap = multiline;
    result->read_only = readOnly;
    result->set_layout_item({});
    parent.add(result);
    return result;
}

template <typename T>
std::shared_ptr<lib::NumberEdit> ToolsPane<T>::makeNumber(
    lib::Container& parent, lib::String label, const double minimum,
    const double maximum, lib::String hint) {
    auto text = std::make_shared<lib::Label>(std::move(label));
    parent.add(text);
    auto result = detail::makeComponent<lib::NumberEdit>(
        comm_.services, lib::ComponentKind::NumberEdit);
    result->minimum = minimum;
    result->maximum = maximum;
    result->step = 1.0;
    result->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                             lib::Align::Center, {44.0F, 24.0F},
                             {44.0F, 24.0F}});
    parent.add(result);
    if (!hint.empty()) parent.add(std::make_shared<lib::Label>(std::move(hint)));
    return result;
}

template <typename T>
std::shared_ptr<lib::Button> ToolsPane<T>::makeButton(
    lib::Container& parent, lib::String text, const MenuID image,
    lib::Action action, lib::String tooltip) {
    auto result = detail::makeComponent<lib::Button>(
        comm_.services, lib::ComponentKind::Button);
    result->text = std::move(text);
    result->icon = presentation_.image(image);
    result->set_tooltip(std::move(tooltip));
    result->on_click = std::move(action);
    parent.add(result);
    return result;
}

template <typename T>
void ToolsPane<T>::attachTextMenu(const std::shared_ptr<lib::TextEdit>& text) {
    if (!text) return;
    auto canSave = [this] {
        return !ignoringMod_ && callbacks_.catchMod && callbacks_.catchMod();
    };
    auto menu = dmenu::createTextMenu(
        comm_, comm_.presentation, text,
        std::move(canSave), {}, {});
    if (menu) {
        menu->refreshToolBar = [this] { notifyChanged(); };
        textMenus_.push_back(std::move(menu));
    }
    auto oldChanged = text->on_changed;
    text->on_changed = [this, oldChanged](std::string_view value) {
        if (oldChanged) oldChanged(value);
        markModified();
    };
}

template <typename T>
void ToolsPane<T>::attachComboMenu(
    const std::shared_ptr<lib::ComboBox>& combo) {
    if (!combo) return;
    auto canSave = [this] {
        return !ignoringMod_ && callbacks_.catchMod && callbacks_.catchMod();
    };
    auto menu = dmenu::createTextMenu(
        comm_, comm_.presentation, combo, std::move(canSave), {}, {});
    if (menu) {
        menu->refreshToolBar = [this] { notifyChanged(); };
        textMenus_.push_back(std::move(menu));
    }
    const auto oldSelection = combo->on_selection_changed;
    combo->on_selection_changed =
        [this, oldSelection](std::optional<std::size_t> value) {
            if (oldSelection) oldSelection(value);
            markModified();
        };
}

template <typename T>
void ToolsPane<T>::attachDrop(
    const std::shared_ptr<lib::TextEdit>& text, const bool directory,
    std::function<lib::String(lib::StringView)> converter) {
    if (!text || !comm_.services.drag_drop) return;
    lib::DropTargetBinding binding;
    binding.target = text;
    binding.accepted_formats = {"text/uri-list", "application/x-cwxeditor-xmlbytes"};
    binding.on_drop = [this, text, directory, converter = std::move(converter)](
                          lib::DragEvent& event) {
        if (!event.data.files.empty()) {
            dropInto(text, event.data.files, directory);
            event.handled = true;
        }
        if (!event.handled && !event.data.text.empty()) {
            auto value = converter ? converter(event.data.text) : event.data.text;
            if (!value.empty()) {
                text->text = value;
                markModified();
                event.handled = true;
            }
        }
        if (!event.handled && xmlbytestransfer::CBisXML(event.data)) {
            auto xml = xmlbytestransfer::CBtoXML(event.data);
            auto value = converter ? converter(xml) : xml;
            if (!value.empty()) {
                text->text = std::move(value);
                markModified();
                event.handled = true;
            }
        }
    };
    comm_.services.drag_drop->bind_target(std::move(binding));
    dragTargets_.push_back(text);
}

template <typename T>
void ToolsPane<T>::bindOpenButtonEnabled(
    const std::shared_ptr<lib::Button>& button,
    const std::shared_ptr<lib::TextEdit>& text) {
    const std::weak_ptr<void> life = pointer_lifetime_;
    auto refresh = dutils::bindOpenButtonEnabledToNonEmptyText(
        button, text, [this, life] { return !life.expired() && enabled_; });
    if (refresh) openButtonEnabledRefreshes_.push_back(std::move(refresh));
}

template <typename T>
void ToolsPane<T>::refreshOpenButtonEnabledStates() {
    const auto refreshes = openButtonEnabledRefreshes_;
    for (const auto& refresh : refreshes)
        if (refresh) refresh();
}

template <typename T>
void ToolsPane<T>::markModified() {
    if (ignoringMod_ || (callbacks_.catchMod && !callbacks_.catchMod())) return;
    canApply_ = true;
    altEnabled_ = !array_.empty() && hasSelection();
    if (alt_) alt_->set_enabled(enabled_ && altEnabled_);
    notifyChanged();
}

template <typename T>
void ToolsPane<T>::notifyChanged() const {
    if (comm_.refreshToolBar) comm_.refreshToolBar();
    if (callbacks_.applyEnabled) callbacks_.applyEnabled();
}

template <typename T>
void ToolsPane<T>::build() {
    if (built_) return;
    built_ = true;
    set_layout(std::make_shared<lib::FillLayout>());
    auto group = std::make_shared<lib::GroupBox>();
    group->title = boxName();
    auto groupLayout = std::make_shared<lib::FillLayout>();
    // SWTと同様、Group の子を配置する前に、見出しの領域を確保する。
    groupLayout->padding = {18.0F, 7.0F, 7.0F, 7.0F};
    group->set_layout(groupLayout);
    lib::Container::add(group);
    mainSplit_ = std::make_shared<splitpane::SplitPane>(splitpane::Horizontal);
    leftPanel_ = std::make_shared<lib::Panel>();
    rightPanel_ = std::make_shared<lib::Panel>();
    auto leftLayout = std::make_shared<lib::GridLayout>();
    leftLayout->columns = 1U;
    leftLayout->spacing = 4.0F;
    leftPanel_->set_layout(leftLayout);
    auto rightLayout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    rightLayout->spacing = 4.0F;
    rightPanel_->set_layout(rightLayout);
    mainSplit_->add(leftPanel_);
    mainSplit_->add(rightPanel_);
    group->add(mainSplit_);

    list_ = detail::makeComponent<lib::ListView>(
        comm_.services, lib::ComponentKind::ListView);
    list_->set_bounds({0.0F, 0.0F, static_cast<float>(presentation_.leftWidth),
                       static_cast<float>(presentation_.leftHeight)});
    list_->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                            lib::Align::Stretch,
                            {static_cast<float>(presentation_.leftWidth),
                             static_cast<float>(presentation_.leftHeight)},
                            {}, {}, 1.0F});
    leftPanel_->add(list_);
    auto orderButtons = std::make_shared<lib::Panel>();
    auto orderLayout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
    orderLayout->spacing = 6.0F;
    orderButtons->set_layout(orderLayout);
    orderButtons->set_layout_item({1, 0, 1, 1, lib::Align::Stretch,
                                   lib::Align::Center,
                                   {0.0F, lib::kEditorUiMetrics.control_height}});
    leftPanel_->add(orderButtons);
    up_ = makeButton(*orderButtons, presentation_.upText, MenuID::Up,
                      [this] { up(); });
    down_ = makeButton(*orderButtons, presentation_.downText, MenuID::Down,
                        [this] { down(); });
    buildRight();
    buildContextMenu();
    setupCallbacks();
}

template <typename T>
void ToolsPane<T>::buildShortcut(lib::Container& parent) {
    parent.add(std::make_shared<lib::Label>(presentation_.mnemonicText));
    shortcutSplit_ = std::make_shared<splitpane::SplitPane>(splitpane::Horizontal);
    shortcutSplit_->set_layout_item({.horizontal = lib::Align::Stretch,
                                     .vertical = lib::Align::Center,
                                     .grow = 1.0F});
    parent.add(shortcutSplit_);
    mnemonic_ = customtext::mnemonicText(nullptr, customtext::STYLE_NONE,
                                         comm_.graphics());
    if (!mnemonic_) mnemonic_ = makeText(*shortcutSplit_);
    else shortcutSplit_->add(mnemonic_);
    attachTextMenu(mnemonic_);

    auto hotkeyPanel = std::make_shared<lib::Panel>();
    auto hotkeyLayout = std::make_shared<lib::GridLayout>();
    hotkeyLayout->columns = 3U;
    hotkeyLayout->spacing = 5.0F;
    hotkeyPanel->set_layout(hotkeyLayout);
    shortcutSplit_->add(hotkeyPanel);
    hotkeyPanel->add(std::make_shared<lib::Label>(presentation_.hotkeyText));
    hotkey_ = std::make_shared<customtext::HotKeyField>(
        nullptr, customtext::STYLE_NONE, comm_.graphics());
    hotkeyWidget_ = hotkey_->widget();
    hotkeyWidget_->set_layout_item({.row = 0, .column = 1,
                                    .horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Center,
                                    .grow = 1.0F});
    hotkeyPanel->add(hotkeyWidget_);
    attachTextMenu(hotkeyWidget_);
    auto clear = makeButton(*hotkeyPanel, presentation_.deleteText, MenuID::Delete,
                             [this] {
                                 if (mnemonic_)
                                     // D版の Text.setText と同様、読み取り専用のアクセスキー欄でも Modify を通知する。
                                     (void)lib::set_text_value(*mnemonic_, {},
                                         lib::TextValueOrigin::Programmatic);
                                 if (hotkey_) hotkey_->accelerator({});
                            });
    clear->icon = 0;
    clear->set_tooltip(presentation_.deleteText);
}

template <typename T>
void ToolsPane<T>::buildRight() {
    auto makeRow = [this]() {
        auto result = std::make_shared<lib::Panel>();
        auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
        layout->spacing = 5.0F;
        result->set_layout(layout);
        rightPanel_->add(result);
        return result;
    };
    auto addLabel = [](lib::Container& parent, const lib::String& text) {
        parent.add(std::make_shared<lib::Label>(text));
    };
    if constexpr (std::is_same_v<T, structs::BgImageSetting>) {
        auto nameRow = makeRow();
        addLabel(*nameRow, presentation_.nameText);
        name_ = makeText(*nameRow);
        attachTextMenu(name_);
        bgMask_ = detail::makeComponent<lib::CheckBox>(
            comm_.services, lib::ComponentKind::CheckBox);
        bgMask_->icon = presentation_.image(MenuID::Mask);
        bgMask_->set_tooltip(presentation_.maskTooltip);
        bgMask_->on_changed = [this](bool) { markModified(); };
        nameRow->add(bgMask_);
        auto boundsRow = makeRow();
        bgX_ = makeNumber(*boundsRow, presentation_.leftText,
                          -presentation_.posLeftMax, presentation_.posLeftMax);
        bgY_ = makeNumber(*boundsRow, presentation_.topText,
                          -presentation_.posTopMax, presentation_.posTopMax);
        bgW_ = makeNumber(*boundsRow, presentation_.widthText, 0,
                          presentation_.backWidthMax);
        bgH_ = makeNumber(*boundsRow, presentation_.heightText, 0,
                          presentation_.backHeightMax);
        auto layerRow = makeRow();
        bgLayer_ = makeNumber(*layerRow, presentation_.layerText,
                              presentation_.layerDefault, presentation_.layerMax,
                              detail::layerHint(presentation_.layerHint,
                                                presentation_.layerDefault));
        for (auto* value : {bgX_.get(), bgY_.get(), bgW_.get(), bgH_.get(),
                            bgLayer_.get()})
            value->on_changed = [this](double) { markModified(); };
    } else if constexpr (std::is_same_v<T, structs::OuterTool>) {
        auto nameRow = makeRow();
        addLabel(*nameRow, presentation_.nameText);
        name_ = makeText(*nameRow);
        attachTextMenu(name_);
        buildShortcut(*rightPanel_);
        auto commandRow = makeRow();
        addLabel(*commandRow, presentation_.commandText);
        toolCommand_ = makeText(*commandRow);
        attachTextMenu(toolCommand_);
        toolCommandRef_ = makeButton(*commandRow, presentation_.referenceText,
                                     MenuID::Open, [this] { browse(toolCommand_, false); });
        toolCommandOpen_ = makeButton(*commandRow, presentation_.text(MenuID::OpenPlace),
                                      MenuID::OpenDir, [this] { open(toolCommand_, false); },
                                      presentation_.text(MenuID::OpenPlace));
        attachDrop(toolCommand_, false);
        bindOpenButtonEnabled(toolCommandOpen_, toolCommand_);
        auto workRow = makeRow();
        addLabel(*workRow, presentation_.workDirText);
        toolWorkDir_ = makeText(*workRow);
        attachTextMenu(toolWorkDir_);
        toolWorkDirRef_ = makeButton(*workRow, presentation_.referenceText,
                                     MenuID::OpenDir, [this] { browse(toolWorkDir_, true); });
        toolWorkDirOpen_ = makeButton(*workRow, presentation_.text(MenuID::OpenDir),
                                      MenuID::OpenDir, [this] { open(toolWorkDir_, true); },
                                      presentation_.text(MenuID::OpenDir));
        attachDrop(toolWorkDir_, true);
        bindOpenButtonEnabled(toolWorkDirOpen_, toolWorkDir_);
        rightPanel_->add(std::make_shared<lib::Label>(presentation_.outerToolHint1));
        rightPanel_->add(std::make_shared<lib::Label>(presentation_.outerToolHint2));
        rightPanel_->add(std::make_shared<lib::Label>(presentation_.outerToolHint3));
    } else if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
        auto nameRow = makeRow();
        addLabel(*nameRow, presentation_.nameText);
        nameTypeSplit_ = std::make_shared<splitpane::SplitPane>(
            splitpane::Horizontal);
        nameTypeSplit_->set_layout_item({.horizontal = lib::Align::Stretch,
                                         .vertical = lib::Align::Center,
                                         .grow = 1.0F});
        nameRow->add(nameTypeSplit_);
        name_ = makeText(*nameTypeSplit_);
        attachTextMenu(name_);
        auto typePanel = std::make_shared<lib::Panel>();
        auto typeLayout = std::make_shared<lib::LinearLayout>(
            lib::Orientation::Horizontal);
        typeLayout->spacing = 5.0F;
        typePanel->set_layout(typeLayout);
        addLabel(*typePanel, presentation_.typeText);
        skinType_ = detail::makeComponent<lib::ComboBox>(
            comm_.services, lib::ComponentKind::ComboBox);
        skinType_->editable = true;
        skinType_->visible_item_count = presentation_.comboVisibleItemCount;
        for (const auto& type : presentation_.skinTypes)
            skinType_->items.push_back({type, type, {}, true});
        skinType_->set_layout_item({.horizontal = lib::Align::Stretch,
                                    .vertical = lib::Align::Center,
                                    .grow = 1.0F});
        typePanel->add(skinType_);
        nameTypeSplit_->add(typePanel);
        skinType_->on_selection_changed = [this](std::optional<std::size_t> index) {
            static_cast<void>(index);
            markModified();
        };
        attachComboMenu(skinType_);
        buildShortcut(*rightPanel_);
        auto engineRow = makeRow();
        addLabel(*engineRow, presentation_.enginePathText);
        enginePath_ = makeText(*engineRow);
        attachTextMenu(enginePath_);
        enginePathRef_ = makeButton(*engineRow, presentation_.referenceText,
                                    MenuID::Open, [this] { browse(enginePath_, false); });
        enginePathOpen_ = makeButton(*engineRow, presentation_.text(MenuID::OpenPlace),
                                     MenuID::OpenDir, [this] { open(enginePath_, false); },
                                     presentation_.text(MenuID::OpenPlace));
        attachDrop(enginePath_, false);
        bindOpenButtonEnabled(enginePathOpen_, enginePath_);
        auto dataRow = makeRow();
        addLabel(*dataRow, presentation_.dataDirText);
        engineDataDir_ = makeText(*dataRow);
        attachTextMenu(engineDataDir_);
        engineDataDirRef_ = makeButton(*dataRow, presentation_.referenceText,
                                       MenuID::OpenDir, [this] { browse(engineDataDir_, true); });
        engineDataDirOpen_ = makeButton(*dataRow, presentation_.text(MenuID::OpenDir),
                                        MenuID::OpenDir, [this] { open(engineDataDir_, true); },
                                        presentation_.text(MenuID::OpenDir));
        attachDrop(engineDataDir_, true);
        auto executeRow = makeRow();
        addLabel(*executeRow, presentation_.executeText);
        engineExecute_ = makeText(*executeRow);
        attachTextMenu(engineExecute_);
        engineExecuteRef_ = makeButton(*executeRow, presentation_.referenceText,
                                       MenuID::Open, [this] { browse(engineExecute_, false); });
        // D版と同様、実行パスの補助ボタンには dir=true を渡
        // す。 見出し・アイコンは格納ディレクトリを開く意味だが、 空
        // でない Execute の値は引き続きファイルとして扱う。
        engineExecuteOpen_ = makeButton(*executeRow, presentation_.text(MenuID::OpenDir),
                                        MenuID::OpenDir, [this] { open(engineExecute_, false); },
                                        presentation_.text(MenuID::OpenDir));
        attachDrop(engineExecute_, false);
        rightPanel_->add(std::make_shared<lib::Label>(presentation_.classicEngineHint));
        buildFeatures();
    } else if constexpr (std::is_same_v<T, structs::ScTemplate>) {
        auto nameRow = makeRow();
        addLabel(*nameRow, presentation_.templateNameText);
        name_ = makeText(*nameRow);
        attachTextMenu(name_);
        auto pathRow = makeRow();
        addLabel(*pathRow, presentation_.scenarioPathText);
        templatePath_ = makeText(*pathRow);
        attachTextMenu(templatePath_);
        templatePathRef_ = makeButton(*pathRow, presentation_.referenceText,
                                      MenuID::Open, [this] { browse(templatePath_, false); });
        templatePathOpen_ = makeButton(*pathRow, presentation_.text(MenuID::OpenPlace),
                                       MenuID::OpenDir, [this] { open(templatePath_, false); },
                                       presentation_.text(MenuID::OpenPlace));
        attachDrop(templatePath_, false);
        bindOpenButtonEnabled(templatePathOpen_, templatePath_);
    } else if constexpr (std::is_same_v<T, structs::EvTemplate>) {
        auto nameRow = makeRow();
        addLabel(*nameRow, presentation_.templateNameText);
        name_ = makeText(*nameRow);
        attachTextMenu(name_);
        auto shortcutRow = makeRow();
        buildShortcut(*shortcutRow);
        auto scriptRow = makeRow();
        addLabel(*scriptRow, presentation_.eventScriptText);
        templateScript_ = makeText(*scriptRow, {}, true);
        scriptRow->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                    lib::Align::Stretch, {260, 90}, {}, {}, 1.0F});
        templateScript_->multiline = true;
        templateScript_->wrap = true;
        templateScript_->horizontal_scroll = true;
        templateScript_->tab_width = presentation_.tabs;
        attachTextMenu(templateScript_);
        rightPanel_->add(std::make_shared<lib::Label>(presentation_.eventTemplateHint1));
        auto hint = makeText(*rightPanel_, presentation_.eventTemplateHint2, true, true);
        // D版と同様、読み取り専用の使用例は MULTI|WRAP とし、スクロール可能なスクリプト欄とは分ける。
        hint->minimum_visible_lines = 2U;
        hint->vertical_scroll = false;
        attachTextMenu(hint);
    }

    auto buttons = std::make_shared<lib::Panel>();
    buttons->set_layout(std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal));
    buttons->set_layout_item({0, 0, 1, 1, lib::Align::End,
                              lib::Align::Center});
    rightPanel_->add(buttons);
    add_ = makeButton(*buttons, presentation_.newText, MenuID::AddItem,
                      [this] { create(); });
    alt_ = makeButton(*buttons, presentation_.alterText, MenuID::EditProp,
                      [this] { alt(); });
    del_ = makeButton(*buttons, presentation_.deleteText, MenuID::Delete,
                      [this] { del(); });
    // DWTと同様、これらの操作には文字だけのボタンを使う。 ツール
    // バーの記号も付けると見た目が異なり、日本語の見出しが欠ける。
    add_->icon = 0;
    alt_->icon = 0;
    del_->icon = 0;
    alt_->set_enabled(false);
    del_->set_enabled(false);
}

template <typename T>
void ToolsPane<T>::buildFeatures() {
    if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
        features_ = detail::makeComponent<lib::ToggleButton>(
            comm_.services, lib::ComponentKind::ToggleButton);
        features_->text = presentation_.featureText;
        features_->on_changed = [this, life = std::weak_ptr<void>{feature_lifetime_}](const bool value) {
            if (life.expired()) return;
            const auto window = featureWindow_;
            const auto gui = comm_.services.gui;
            if (!window) return;
            if (!lib::editor_window_host_options().all_mode && gui) {
                if (value) gui->show(window);
                else gui->hide(window);
            } else window->set_visible(value);
        };
        rightPanel_->add(features_);
        featureWindow_ = detail::makeComponent<lib::Window>(
            comm_.services, lib::ComponentKind::Window);
        featureWindow_->title = presentation_.featureText;
        featureWindow_->set_visible(false);
        if (!lib::editor_window_host_options().all_mode) {
            featureWindow_->set_layout(std::make_shared<lib::FillLayout>());
            featureWindow_->on_close_requested = [this, life = std::weak_ptr<void>{feature_lifetime_}] {
                if (life.expired()) return;
                const auto window = featureWindow_;
                const auto toggle = features_;
                const auto gui = comm_.services.gui;
                if (toggle) toggle->checked = false;
                if (window) {
                    if (gui) gui->hide(window);
                    else window->set_visible(false);
                }
            };
        }
        featureTable_ = detail::makeComponent<lib::TableView>(
            comm_.services, lib::ComponentKind::TableView);
        featureTable_->multi_select = true;
        featureTable_->grid_lines = true; // D の toolspane と同じく setLinesVisible(true) を設定する。
        featureTable_->columns = {
            {presentation_.featureDefaultText,
             presentation_.featureDefaultNameWidth},
            {presentation_.featureVariantText,
             presentation_.featureVariantNameWidth},
            {presentation_.featureManualText,
             presentation_.featureManualNameWidth}};
        featureTable_->on_column_resized = [this](const std::size_t column,
                                                   const float width) {
            if (column < 3U && presentation_.storeFeatureColumnWidth)
                presentation_.storeFeatureColumnWidth(column, width);
        };
        featureTable_->on_selection_changed = [this](const auto&) { notifyChanged(); };
        featureTable_->on_cell_edited =
            [this](std::size_t row, std::size_t column, lib::String value) {
                featureCellEdited(row, column, std::move(value));
            };
        featureWindow_->add(featureTable_);
        featureTcpd_ = std::make_shared<FeatureTCPD>(*this);
        featureMenu_ = std::make_shared<dmenu::Menu>(true);
        (void)dmenu::createMenuItem(
            comm_, *featureMenu_, MenuID::Undo,
            [this] { featureUndo_.undo(); },
            [this] { return featureUndo_.canUndo(); });
        (void)dmenu::createMenuItem(
            comm_, *featureMenu_, MenuID::Redo,
            [this] { featureUndo_.redo(); },
            [this] { return featureUndo_.canRedo(); });
        (void)featureMenu_->appendSeparator();
        dmenu::appendMenuTCPD(comm_, *featureMenu_, featureTcpd_, true, true,
                              true, true, false);
        (void)featureMenu_->appendSeparator();
        (void)dmenu::createMenuItem(
            comm_, *featureMenu_, MenuID::SelectAll,
            [this] {
                if (!featureTable_) return;
                std::vector<std::size_t> selection;
                for (std::size_t i = 0; i < featureTable_->rows.size(); ++i)
                    selection.push_back(i);
                setFeatureSelection(std::move(selection), true);
            },
            [this] {
                return featureTable_ && featureTable_->selection.size() <
                                      featureTable_->rows.size();
            });
    }
}

template <typename T>
void ToolsPane<T>::buildContextMenu() {
    contextMenu_ = std::make_shared<dmenu::Menu>(true);
    (void)dmenu::createMenuItem(comm_, *contextMenu_, MenuID::Undo,
                          [this] { undo_.undo(); }, [this] { return undo_.canUndo(); });
    (void)dmenu::createMenuItem(comm_, *contextMenu_, MenuID::Redo,
                          [this] { undo_.redo(); }, [this] { return undo_.canRedo(); });
    (void)contextMenu_->appendSeparator();
    (void)dmenu::createMenuItem(comm_, *contextMenu_, MenuID::Up,
                                [this] { up(); }, [this] { return canUp(); });
    (void)dmenu::createMenuItem(comm_, *contextMenu_, MenuID::Down,
                                [this] { down(); }, [this] { return canDown(); });
    (void)contextMenu_->appendSeparator();
    tcpd_ = std::make_shared<UTCPD>(*this);
    dmenu::appendMenuTCPD(comm_, *contextMenu_, tcpd_, true, true, true, true,
                          true);
}

template <typename T>
void ToolsPane<T>::setupCallbacks() {
    list_->on_pointer = [this, life = std::weak_ptr<void>{pointer_lifetime_}](lib::PointerEvent& event) {
        if (life.expired() || event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up || !comm_.services.gui || !list_->enabled()) return;
        const auto menu = contextMenu_;
        const auto owner = list_;
        const auto gui = comm_.services.gui;
        menu->refreshItems();
        if (life.expired()) return;
        gui->show_context_menu(menu, owner, event.position);
        event.handled = true;
    };
    list_->on_selection_changed = [this](const std::vector<std::size_t>& values) {
        if (ignoringMod_) return;
        const auto index = values.empty() ? std::numeric_limits<std::size_t>::max()
                                          : values.front();
        if (index == selectedIndex_) return;
        if (!confirmSelectionChange(index)) {
            ignoringMod_ = true;
            std::vector<std::size_t> selection;
            if (selectedIndex_ != std::numeric_limits<std::size_t>::max())
                selection.push_back(selectedIndex_);
            (void)lib::select_list_rows(*list_, std::move(selection), false, false);
            ignoringMod_ = false;
            return;
        }
        selectedIndex_ = index;
        selected();
    };
    add_->on_click = [this] { create(); };
    alt_->on_click = [this] { alt(); };
    del_->on_click = [this] { del(); };
    up_->on_click = [this] { up(); };
    down_->on_click = [this] { down(); };
    attachKey(list_);
    // Ctrl+X/C/V は二つの一覧だけで処理する。これらの切り取り・コピー・貼り付
    // けはポップアップにしかないため。 テキスト欄は自身のクリップボード処理を維持する。
    const auto attachClipboard = [](const lib::WidgetPtr& widget,
                                    const std::shared_ptr<dmenu::TCPD>& tcpd) {
        if (!widget || !tcpd) return;
        const auto old = widget->on_key_down;
        const std::weak_ptr<dmenu::TCPD> weak = tcpd;
        widget->on_key_down = [old, weak](lib::KeyEvent& event) {
            if (old) old(event);
            if (const auto current = weak.lock())
                (void)dmenu::handleTCPDShortcut(event, *current);
        };
    };
    attachClipboard(list_, tcpd_);
    attachClipboard(featureTable_, featureTcpd_);
    attachKey(name_);
    attachKey(toolCommand_); attachKey(toolWorkDir_);
    attachKey(enginePath_); attachKey(engineDataDir_); attachKey(engineExecute_);
    attachKey(templatePath_); attachKey(templateScript_);
    attachKey(mnemonic_); attachKey(hotkeyWidget_);
    attachKey(skinType_);
    attachKey(featureTable_);
}

template <typename T>
void ToolsPane<T>::attachKey(const lib::WidgetPtr& widget) {
    if (!widget) return;
    const auto old = widget->on_key_down;
    widget->on_key_down = [this, old](lib::KeyEvent& event) {
        if (old) old(event);
        if (!event.handled) handleKey(event);
    };
}

template <typename T>
void ToolsPane<T>::handleKey(lib::KeyEvent& event) {
    const auto key = static_cast<int>(event.key);
    std::uint32_t character = static_cast<std::uint32_t>(key);
    if (event.key >= lib::Key::A && event.key <= lib::Key::Z)
        character = static_cast<std::uint32_t>('A' +
            (static_cast<int>(event.key) - static_cast<int>(lib::Key::A)));
    const auto state = static_cast<std::uint32_t>(event.modifiers);
    if (dmenu::eqAcc(static_cast<int>(undoAccelerator_), key, character, state)) {
        undo_.undo(); event.handled = true;
    } else if (dmenu::eqAcc(static_cast<int>(redoAccelerator_), key, character, state)) {
        undo_.redo(); event.handled = true;
    }
}

template <typename T>
void ToolsPane<T>::setupWeights(const std::int32_t left,
                                const std::int32_t right) {
    if (!mainSplit_) return;
    const auto first = left == 0 && right == 0 ? 1 : left;
    const auto second = left == 0 && right == 0 ? 1 : right;
    mainSplit_->setWeights({first, second});
}

template <typename T>
void ToolsPane<T>::setup(const std::vector<T>& values,
                          std::int32_t& sashLeft, std::int32_t& sashRight) {
    if (!built_) build();
    setupWeights(sashLeft, sashRight);
    if (mainSplit_) {
        auto* const left = &sashLeft;
        auto* const right = &sashRight;
        mainSplit_->on_weights_changed = [left, right](const std::vector<float>& weights) {
            if (weights.size() < 2U) return;
            *left = static_cast<std::int32_t>(std::max(1.0F, std::round(weights[0])));
            *right = static_cast<std::int32_t>(std::max(1.0F, std::round(weights[1])));
        };
    }
    setArray(values);
}

template <typename T>
void ToolsPane<T>::setup(const std::vector<T>& values,
                         std::vector<std::int32_t>& weights) {
    if (weights.size() < 2U) weights = {1, 1};
    setup(values, weights[0], weights[1]);
}

template <typename T>
std::filesystem::path ToolsPane<T>::classicApplicationDirectory() const {
    if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
        if (!presentation_.applicationPath.empty()) {
            const auto appPath = structs::pathFromUtf8(
                presentation_.applicationPath);
            if (!appPath.empty() && !appPath.parent_path().empty())
                return appPath.parent_path();
        }
        if (comm_.services.system)
            return comm_.services.system->executable_path().parent_path();
    }
    return {};
}

template <typename T>
std::filesystem::path ToolsPane<T>::classicEngineFile() const {
    if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
        if (!enginePath_ || enginePath_->text.empty()) return {};
        auto path = structs::pathFromUtf8(enginePath_->text);
        if (path.is_relative()) {
            // D版の curCEnginePath と同様、Props.appPath を基準に解
            // 決する。 所有者が提供する同じ基準を使い、単独のペインではサービスを代わりに使う。
            const auto app = classicApplicationDirectory();
            if (app.empty()) return {};
            path = app / path;
        }
        return path.lexically_normal();
    }
    return {};
}

template <typename T>
std::filesystem::path ToolsPane<T>::classicEngineDirectory() const {
    const auto engine = classicEngineFile();
    return engine.empty() ? std::filesystem::path{} : engine.parent_path();
}

template <typename T>
lib::String ToolsPane<T>::classicSubpath(
    const std::filesystem::path& path) const {
    const auto base = classicEngineDirectory();
    if (base.empty()) return structs::pathToUtf8(path);
    auto absolute = path;
    if (absolute.is_relative()) {
        const auto app = classicApplicationDirectory();
        if (app.empty()) return structs::pathToUtf8(path);
        absolute = app / absolute;
    }
    const auto relative = absolute.lexically_normal().lexically_relative(
        base.lexically_normal());
    // D版の abs2rel と同様、別ボリュームのパスも保持する。
    // C++版で字句的な相対パスを作れない場合も、同じ代替動作にする。
    return relative.empty() ? structs::pathToUtf8(path)
                            : structs::pathToUtf8(relative);
}

template <typename T>
void ToolsPane<T>::applyPathSelection(
    const std::shared_ptr<lib::TextEdit>& text,
    const std::filesystem::path& selectedPath) {
    if (!text) return;
    auto path = selectedPath.lexically_normal();
    if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
        if (text == enginePath_ && isDirectory(path)) {
            lib::String resource;
            lib::String engine;
            if (::cwxeditor::cwx::skin::Skin::hasClassicEngine(
                    structs::pathToUtf8(path), resource, engine,
                    presentation_.classicEngineRegex,
                    presentation_.classicDataDirRegex,
                    presentation_.classicMatchKey, array_))
                path = structs::pathFromUtf8(engine);
        }
        lib::String value = (text == engineDataDir_ || text == engineExecute_)
                                ? classicSubpath(path)
                                : structs::pathToUtf8(path);
        const std::weak_ptr<void> life{pointer_lifetime_};
        (void)lib::set_text_value(*text, value);
        if (life.expired()) return;
        if (text == enginePath_) {
            const auto resource = ::cwxeditor::cwx::skin::Skin::findResDir(
                structs::pathToUtf8(path.parent_path()),
                presentation_.classicDataDirRegex,
                presentation_.classicMatchKey);
            if (!resource.empty() && engineDataDir_)
                (void)lib::set_text_value(*engineDataDir_, resource);
        }
        return;
    }
    (void)lib::set_text_value(*text, structs::pathToUtf8(path));
}

template <typename T>
void ToolsPane<T>::browse(const std::shared_ptr<lib::TextEdit>& text,
                          const bool directory) {
    if (!text) return;
    const std::weak_ptr<void> life{pointer_lifetime_};
    std::filesystem::path initial;
    if (!text->text.empty()) initial = structs::pathFromUtf8(text->text);
    if constexpr (std::is_same_v<T, structs::OuterTool>) {
        if (text == toolWorkDir_) {
            auto app = structs::pathFromUtf8(presentation_.applicationPath);
            auto appDirectory = app.empty() ? std::filesystem::path{}
                                            : app.parent_path();
            if (appDirectory.empty() && comm_.services.system)
                appDirectory = comm_.services.system->executable_path().parent_path();
            if (initial.empty()) initial = appDirectory;
            else if (initial.is_relative() && !appDirectory.empty())
                initial = appDirectory / initial;
        } else if (text == toolCommand_ && initial.is_relative()) {
            std::error_code error;
            const auto absolute = std::filesystem::absolute(initial, error);
            if (!error) initial = absolute.lexically_normal();
        }
    } else if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
        if (text == engineDataDir_ || text == engineExecute_) {
            const auto root = classicEngineDirectory();
            // 空のパスを連結すると末尾に要素が追加されるため、 下位パ
            // スが未保存ならD版と同じエンジンディレクトリ自体を保つ。
            if (initial.empty()) initial = root;
            else if (initial.is_relative() && !root.empty()) initial = root / initial;
        } else if (text == enginePath_ && initial.is_relative() &&
                   comm_.services.system) {
            const auto app = comm_.services.system->executable_path().parent_path();
            if (!app.empty()) initial = app / initial;
        }
    }
    lib::String current = initial.empty() ? text->text
                                          : structs::pathToUtf8(initial);
    if (presentation_.browsePath) {
        auto value = presentation_.browsePath(text->text, directory, current);
        if (life.expired() || value.empty()) return;
        applyPathSelection(text, structs::pathFromUtf8(value));
        return;
    }
    if (comm_.services.file_picker) {
        lib::String title;
        lib::String description;
        if constexpr (std::is_same_v<T, structs::ScTemplate>) {
            title = presentation_.scenarioTemplateDialogTitle;
        } else if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
            if (text == engineDataDir_) {
                title = presentation_.dataDirText;
                description = presentation_.dataDirDescription;
            } else {
                title = text == engineExecute_
                            ? presentation_.engineExecuteDialogTitle
                            : presentation_.enginePathDialogTitle;
            }
        } else if constexpr (std::is_same_v<T, structs::OuterTool>) {
            if (text == toolCommand_) {
                title = presentation_.outerToolDialogTitle;
            } else if (text == toolWorkDir_) {
                title = presentation_.workDirDialogTitle;
                description = presentation_.workDirDescription;
            } else {
                title = presentation_.referenceText;
            }
        } else if (directory) {
            title = presentation_.workDirDialogTitle;
            description = presentation_.workDirDescription;
        } else title = presentation_.referenceText;
        auto request = directory
                           ? dutils::makeDirectoryPickerRequest(title, description)
                           : lib::FilePickerRequest{};
        request.title = std::move(title);
        if constexpr (std::is_same_v<T, structs::ScTemplate>)
            request.filters = presentation_.scenarioTemplateFilters;
        if constexpr (std::is_same_v<T, structs::OuterTool>) {
            if (text == toolCommand_) {
                if (comm_.services.system &&
                    comm_.services.system->platform() == lib::Platform::Windows)
                    request.filters.push_back(
                        {presentation_.executableFilterText, {"*.exe"}, {}});
                request.filters.push_back(
                    {presentation_.allFilesFilterText, {"*.*"}, {}});
            }
        } else if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
            if (text == enginePath_ || text == engineExecute_) {
                if (comm_.services.system &&
                    comm_.services.system->platform() == lib::Platform::Windows)
                    request.filters.push_back(
                        {presentation_.executableFilterText, {"*.exe"}, {}});
                request.filters.push_back(
                    {presentation_.allFilesFilterText, {"*.*"}, {}});
            }
            // D版の selectCEngineExecute と同様、保存済みのファイル名を fileName
            // として渡す。 存在する場合は、その実行ファイルを選択画面の対象にする。
            if (text == engineExecute_ && !text->text.empty())
                request.initial_name = structs::pathToUtf8(initial.filename());
        }
        if (!initial.empty()) {
            if (directory) {
                request.initial_directory = initial;
            } else {
                std::error_code error;
                request.initial_directory =
                    std::filesystem::is_directory(initial, error) && !error
                        ? initial : initial.parent_path();
            }
        } else if (text == engineExecute_ &&
                   std::is_same_v<T, structs::ClassicEngine>) {
            request.initial_directory = classicEngineDirectory();
        }
        const std::weak_ptr<lib::TextEdit> target = text;
        if (directory) {
            (void)comm_.services.file_picker->select_directory_async(
                std::move(request), [this, target, life](lib::FilePickerResult selected) {
                    if (!selected.succeeded() || selected.selections.empty()) return;
                    const auto path = selected.selections.front().native_path();
                    const auto control = target.lock();
                    if (life.expired() || !path || !control) return;
                    applyPathSelection(control, *path);
                });
        } else {
            (void)comm_.services.file_picker->open_file_async(
                std::move(request), [this, target, life](lib::FilePickerResult selected) {
                    if (!selected.succeeded() || selected.selections.empty()) return;
                    const auto path = selected.selections.front().native_path();
                    const auto control = target.lock();
                    if (life.expired() || !path || !control) return;
                    applyPathSelection(control, *path);
                });
        }
        return;
    }
}

template <typename T>
void ToolsPane<T>::open(const std::shared_ptr<lib::TextEdit>& text,
                        const bool directory) {
    if (!text) return;
    bool classicSubpath = false;
    if constexpr (std::is_same_v<T, structs::ClassicEngine>)
        classicSubpath = text == engineDataDir_ || text == engineExecute_;
    std::optional<lib::String> resolved;
    if (classicSubpath) {
        const auto root = classicEngineDirectory();
        auto path = text->text.empty() ? root : structs::pathFromUtf8(text->text);
        if (path.is_relative() && !root.empty()) path = root / path;
        // D版と同様、エンジンルートが未設定なら、相対的なData・実行パスは現在のディレクトリを基準に保つ。
        resolved = dutils::resolveOpenDirectory(
            structs::pathToUtf8(path.lexically_normal()));
    } else {
        if (text->text.empty()) return;
        // D版と同様、相対的なツールパスはエディタの実行ファイルのディレクトリを基準に解決する。
        auto applicationPath = presentation_.applicationPath;
        if (applicationPath.empty() && comm_.services.system)
            applicationPath = structs::pathToUtf8(
                comm_.services.system->executable_path());
        resolved = dutils::openButtonDirectory(
            applicationPath, text->text);
    }
    if (!resolved) return;
    if (presentation_.openPath) {
        presentation_.openPath(*resolved, directory);
        return;
    }
    if (!comm_.services.shell) return;
    std::string error;
    const auto path = structs::pathFromUtf8(*resolved);
    comm_.services.shell->open_path(path, error);
}

template <typename T>
void ToolsPane<T>::dropInto(
    const std::shared_ptr<lib::TextEdit>& text,
    const std::vector<std::filesystem::path>& files, const bool directory) {
    if (!text || files.empty()) return;
    auto path = files.front();
    if (directory) {
        // D版の dropDir と同様、ドロップを受けた時点で最初の項目を検証す
        // る。 ドラッグ開始後にファイルが削除されても、残っている親を選択しない。
        std::error_code error;
        if (!std::filesystem::exists(path,error) || error) return;
        if (!isDirectory(path)) path = parentPath(path);
    }
    if constexpr (std::is_same_v<T, structs::ScTemplate>) {
        if (!directory && !isDirectory(path)) {
            // D版はネイティブのCABライブラリを調べるが、C++版は意図的に、 移
            // 植可能な読み取り機能を含む共通アーカイブバックエンドの対応状況を使う。
            const bool cabDropAllowed = ::cwxeditor::cwx::cab::canUncab();
            auto extension = path.extension().string();
            auto filename = structs::pathToUtf8(path.filename());
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](const unsigned char value) {
                               return static_cast<char>(std::tolower(value));
                           });
            std::transform(filename.begin(), filename.end(), filename.begin(),
                           [](const unsigned char value) {
                               return static_cast<char>(std::tolower(value));
                           });
            const bool summary = filename == "summary.wsm" || filename == "summary.xml";
            const bool archive = extension == ".zip" || extension == ".wsn" ||
                                 extension == ".lzh" || extension == ".lha" ||
                                 (extension == ".cab" && cabDropAllowed);
            if (!summary && !archive) return;
        }
    }
    applyPathSelection(text, path);
}

template <typename T>
bool ToolsPane<T>::hasSelection() const noexcept {
    return selectedIndex_ < array_.size();
}

template <typename T>
bool ToolsPane<T>::clipboardHasXml() const noexcept {
    if (!clipboard_ || !xmlbytestransfer::CBisXML(*clipboard_)) return false;
    T value{};
    return deserialize(xmlbytestransfer::CBtoXML(*clipboard_), value);
}

template <typename T>
bool ToolsPane<T>::clipboardHasText() const noexcept {
    if (!clipboard_) return false;
    const auto data = clipboard_->getContents();
    return !data.text.empty();
}

template <typename T>
lib::String ToolsPane<T>::clipboardText() const {
    if (!clipboard_) return {};
    return clipboard_->getContents().text;
}

template <typename T>
lib::String ToolsPane<T>::serialize(const T& value) const {
    return value.toNode().text();
}

template <typename T>
bool ToolsPane<T>::deserialize(const lib::StringView value, T& result) const {
    if (value.empty()) return false;
    try {
        auto node = structs::XNode::parse(value);
        if (node.name() != std::string(T::XML_NAME)) return false;
        result.fromNode(node);
        return true;
    } catch (...) {
        return false;
    }
}

template <typename T>
void ToolsPane<T>::copySelected() {
    if (!hasSelection() || !clipboard_) return;
    xmlbytestransfer::XMLtoCB(presentation_.xmlCopy, *clipboard_,
                              serialize(array_[selectedIndex_]));
    notifyChanged();
}

template <typename T>
void ToolsPane<T>::pasteSelected() {
    if (!clipboard_) return;
    const auto xml = xmlbytestransfer::CBtoXML(*clipboard_);
    T value{};
    if (deserialize(xml, value)) add(std::move(value));
}

template <typename T>
void ToolsPane<T>::cloneSelected() {
    if (!hasSelection() || !clipboard_) return;
    clipboard_->memoryMode(true);
    // D版の scope(exit) と同様、コールバック・直列化の例外でも内部メモリモードを
    // 解除する。 後の通常コピーにその状態を残さず、ネイティブのクリップボードも変更しない。
    struct MemoryScope {
        xmlbytestransfer::ClipData& clipboard;
        ~MemoryScope() { clipboard.memoryMode(false); }
    } scope{*clipboard_};
    copySelected();
    pasteSelected();
}

template <typename T>
void ToolsPane<T>::syncList(const std::size_t selected) {
    if (!list_) return;
    ignoringMod_ = true;
    std::vector<lib::ListItem> items;
    items.reserve(array_.size());
    for (const auto& value : array_)
        items.push_back({createName(value), value.name, {}, false, true});
    selectedIndex_ = selected < array_.size() ? selected
                                               : std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> selection;
    if (hasSelection()) selection.push_back(selectedIndex_);
    const auto reveal = hasSelection()
                            ? std::optional<std::size_t>{selectedIndex_}
                            : std::nullopt;
    (void)lib::replace_list_items(list_, std::move(items), std::move(selection),
                                  reveal);
    ignoringMod_ = false;
}

template <typename T>
bool ToolsPane<T>::selectionChanged(const std::size_t index) {
    if (index == selectedIndex_) return true;
    return confirmSelectionChange(index);
}

template <typename T>
bool ToolsPane<T>::confirmSelectionChange(const std::size_t index) {
    if (!canApply_ || index == selectedIndex_) return true;
    if (presentation_.confirmSelection) {
        const auto decision = presentation_.confirmSelection(editingName());
        if (decision == SelectionDecision::Apply) forceApply();
        return decision != SelectionDecision::Cancel;
    }
    if (comm_.services.dialogs) {
        lib::MessageDialogRequest request;
        request.title = presentation_.questionTitle;
        request.message = presentation_.selectionApplyMessage;
        request.severity = lib::MessageSeverity::Question;
        request.buttons = lib::MessageButtons::YesNoCancel;
        const auto result = comm_.services.dialogs->message(request);
        if (result == lib::MessageResult::Yes) forceApply();
        if (result == lib::MessageResult::Cancel) return false;
    } else return false;
    return true;
}

template <typename T>
void ToolsPane<T>::setSelected(const std::size_t index) {
    if (index >= array_.size()) {
        selectedIndex_ = std::numeric_limits<std::size_t>::max();
    } else if (!selectionChanged(index)) {
        return;
    } else {
        selectedIndex_ = index;
    }
    ignoringMod_ = true;
    std::vector<std::size_t> selection;
    if (hasSelection()) selection.push_back(selectedIndex_);
    (void)lib::select_list_rows(*list_, std::move(selection), false, false);
    ignoringMod_ = false;
    selected();
}

template <typename T>
void ToolsPane<T>::setFieldValues(const T* value) {
    ignoringMod_ = true;
    if (!value) {
        name_->text.clear();
        if constexpr (std::is_same_v<T, structs::BgImageSetting>) {
            bgX_->value = 0; bgY_->value = 0; bgW_->value = 0; bgH_->value = 0;
            bgMask_->checked = false; bgLayer_->value = presentation_.layerDefault;
        } else if constexpr (std::is_same_v<T, structs::OuterTool>) {
            toolCommand_->text.clear(); toolWorkDir_->text.clear();
            mnemonic_->text.clear(); hotkey_->accelerator({});
        } else if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
            enginePath_->text.clear(); engineDataDir_->text.clear();
            engineExecute_->text.clear(); mnemonic_->text.clear(); hotkey_->accelerator({});
            (void)lib::replace_combo_items(skinType_, skinType_->items,
                                           std::nullopt, lib::String{});
        } else if constexpr (std::is_same_v<T, structs::ScTemplate>) {
            templatePath_->text.clear();
        } else if constexpr (std::is_same_v<T, structs::EvTemplate>) {
            templateScript_->text.clear(); mnemonic_->text.clear(); hotkey_->accelerator({});
        }
        ignoringMod_ = false;
        return;
    }
    name_->text = value->name;
    if constexpr (std::is_same_v<T, structs::BgImageSetting>) {
        bgX_->value = value->x; bgY_->value = value->y; bgW_->value = value->width;
        bgH_->value = value->height; bgMask_->checked = value->mask; bgLayer_->value = value->layer;
    } else if constexpr (std::is_same_v<T, structs::OuterTool>) {
        toolCommand_->text = value->command; toolWorkDir_->text = value->workDir;
        mnemonic_->text = value->mnemonic; hotkey_->accelerator(value->hotkey);
    } else if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
        enginePath_->text = value->enginePath; engineDataDir_->text = value->dataDirName;
        engineExecute_->text = value->execute; mnemonic_->text = value->mnemonic;
        hotkey_->accelerator(value->hotkey);
        std::optional<std::size_t> selected;
        for (std::size_t i = 0; i < skinType_->items.size(); ++i)
            if (skinType_->items[i].text == value->type) {
                selected = i;
                break;
            }
        (void)lib::replace_combo_items(skinType_, skinType_->items, selected,
                                       value->type);
    } else if constexpr (std::is_same_v<T, structs::ScTemplate>) {
        templatePath_->text = value->path;
    } else if constexpr (std::is_same_v<T, structs::EvTemplate>) {
        templateScript_->text = value->script; mnemonic_->text = value->mnemonic;
        hotkey_->accelerator(value->hotkey);
    }
    ignoringMod_ = false;
}

template <typename T>
void ToolsPane<T>::selected() {
    if (!list_ || !name_) return;
    const auto revision = ++selection_revision_;
    const std::weak_ptr<void> life{feature_lifetime_};
    if (selectedIndex_ >= array_.size()) selectedIndex_ = std::numeric_limits<std::size_t>::max();
    ignoringMod_ = true;
    setFieldValues(hasSelection() ? &array_[selectedIndex_] : nullptr);
    if ((life.expired() || revision != selection_revision_)) return;
    canApply_ = false;
    altEnabled_ = false;
    if (alt_) alt_->set_enabled(false);
    if ((life.expired() || revision != selection_revision_)) return;
    if (del_) del_->set_enabled(enabled_ && hasSelection());
    if ((life.expired() || revision != selection_revision_)) return;
    if (up_) up_->set_enabled(enabled_ && canUp());
    if ((life.expired() || revision != selection_revision_)) return;
    if (down_) down_->set_enabled(enabled_ && canDown());
    if ((life.expired() || revision != selection_revision_)) return;
    if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
        const bool preserveManual = !hasSelection() && featureTable_ &&
                                    !featureTable_->rows.empty();
        if (!preserveManual)
            syncFeatureTable(hasSelection() ? &array_[selectedIndex_] : nullptr);
        if ((life.expired() || revision != selection_revision_)) return;
    }
    const auto menus = textMenus_;
    for (const auto& menu : menus) {
        if (life.expired() || revision != selection_revision_) return;
        if (menu) menu->reset();
    }
    if ((life.expired() || revision != selection_revision_)) return;
    ignoringMod_ = false;
    refreshOpenButtonEnabledStates();
    // D版の selected() と同様、設定の適用を有効にせずにコマンド
    // 状態だけを更新する。 変更操作の入口が、それぞれ自身の変更通知を行う。
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

template <typename T>
std::vector<T> ToolsPane<T>::array() const {
    std::vector<T> result;
    result.reserve(array_.size());
    for (const auto& value : array_) result.push_back(detail::copyValue(value));
    return result;
}

template <typename T>
void ToolsPane<T>::setArray(const std::vector<T>& values) {
    undo_.reset();
    array_.clear();
    array_.reserve(values.size());
    for (const auto& value : values) array_.push_back(detail::copyValue(value));
    syncList(array_.empty() ? std::numeric_limits<std::size_t>::max() : 0U);
    selected();
}

template <typename T>
bool ToolsPane<T>::canUp() const noexcept {
    return hasSelection() && selectedIndex_ > 0U;
}

template <typename T>
bool ToolsPane<T>::canDown() const noexcept {
    return hasSelection() && selectedIndex_ + 1U < array_.size();
}

template <typename T>
void ToolsPane<T>::forceApply() {
    if (!canApply_) return;
    if (hasSelection()) alt();
    else create();
}

template <typename T>
lib::String ToolsPane<T>::boxName() const {
    if constexpr (std::is_same_v<T, structs::BgImageSetting>) return presentation_.boxBgImage;
    else if constexpr (std::is_same_v<T, structs::OuterTool>) return presentation_.boxOuterTool;
    else if constexpr (std::is_same_v<T, structs::ClassicEngine>) return presentation_.boxClassicEngine;
    else if constexpr (std::is_same_v<T, structs::ScTemplate>) return presentation_.boxScenarioTemplate;
    else return presentation_.boxEventTemplate;
}

template <typename T>
lib::String ToolsPane<T>::editingName() const { return name_ ? name_->text : lib::String{}; }

template <typename T>
void ToolsPane<T>::setShortcutWeights(std::int32_t& left, std::int32_t& right) {
    if (!shortcutSplit_) return;
    const auto first = left == 0 && right == 0 ? presentation_.shortcutLeft : left;
    const auto second = left == 0 && right == 0 ? presentation_.shortcutRight : right;
    shortcutSplit_->setWeights({first, second});
    auto* const leftValue = &left;
    auto* const rightValue = &right;
    shortcutSplit_->on_weights_changed =
        [leftValue, rightValue](const std::vector<float>& weights) {
            if (weights.size() < 2U) return;
            *leftValue = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[0])));
            *rightValue = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[1])));
        };
}

template <typename T>
void ToolsPane<T>::setNameTypeWeights(std::int32_t& left,
                                      std::int32_t& right) {
    if constexpr (!std::is_same_v<T, structs::ClassicEngine>) {
        static_cast<void>(left);
        static_cast<void>(right);
        return;
    } else {
        if (!nameTypeSplit_) return;
        nameTypeSplit_->setWeights({left == 0 ? 4 : left,
                                    right == 0 ? 3 : right});
        auto* const leftValue = &left;
        auto* const rightValue = &right;
        nameTypeSplit_->on_weights_changed =
            [leftValue, rightValue](const std::vector<float>& weights) {
                if (weights.size() < 2U) return;
                *leftValue = static_cast<std::int32_t>(
                    std::max(1.0F, std::round(weights[0])));
                *rightValue = static_cast<std::int32_t>(
                    std::max(1.0F, std::round(weights[1])));
            };
    }
}

template <typename T>
void ToolsPane<T>::closeFeatureWindow() {
    if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
        const auto window = featureWindow_;
        if (!window || !window->visible()) return;
        const auto toggle = features_;
        if (toggle) {
            toggle->checked = false;
            if (toggle->on_changed) toggle->on_changed(false);
        } else if (comm_.services.gui) {
            comm_.services.gui->hide(window);
        } else {
            window->set_visible(false);
        }
    }
}

template <typename T>
void ToolsPane<T>::enabled(const bool value) {
    const std::weak_ptr<void> life = pointer_lifetime_;
    enabled_ = value;
    if (!value) closeFeatureWindow();
    if (life.expired()) return;
    if (list_) list_->set_enabled(value);
    auto set = [value](const auto& widget) { if (widget) widget->set_enabled(value); };
    set(name_); set(bgX_); set(bgY_); set(bgW_); set(bgH_); set(bgMask_); set(bgLayer_);
    set(toolCommand_); set(toolCommandRef_); set(toolCommandOpen_); set(toolWorkDir_);
    set(toolWorkDirRef_); set(toolWorkDirOpen_); set(enginePath_); set(enginePathRef_);
    set(enginePathOpen_); set(engineDataDir_); set(engineDataDirRef_); set(engineDataDirOpen_);
    set(engineExecute_); set(engineExecuteRef_); set(engineExecuteOpen_); set(skinType_);
    set(templatePath_); set(templatePathRef_); set(templatePathOpen_); set(templateScript_);
    set(mnemonic_); set(hotkeyWidget_); set(features_); set(featureTable_);
    refreshOpenButtonEnabledStates();
    set(add_); if (alt_) alt_->set_enabled(value && altEnabled_);
    if (del_) del_->set_enabled(value && hasSelection());
    if (up_) up_->set_enabled(value && canUp());
    if (down_) down_->set_enabled(value && canDown());
}

template <typename T>
void ToolsPane<T>::up() {
    if (!canUp()) return;
    undo_ += std::make_shared<UndoU>(*this);
    std::swap(array_[selectedIndex_], array_[selectedIndex_ - 1U]);
    --selectedIndex_;
    syncList(selectedIndex_);
    // D版の upImpl と同様、編集フォームを読み直さずに選択レコードを移動す
    // る。 未確定の文字列、変更状態、テキストメニューの取り消し履歴を保持する。
    if (up_) up_->set_enabled(enabled_ && canUp());
    if (down_) down_->set_enabled(enabled_ && canDown());
    notifyChanged();
}

template <typename T>
void ToolsPane<T>::down() {
    if (!canDown()) return;
    undo_ += std::make_shared<UndoU>(*this);
    std::swap(array_[selectedIndex_], array_[selectedIndex_ + 1U]);
    ++selectedIndex_;
    syncList(selectedIndex_);
    if (up_) up_->set_enabled(enabled_ && canUp());
    if (down_) down_->set_enabled(enabled_ && canDown());
    notifyChanged();
}

template <typename T>
void ToolsPane<T>::add(T value) {
    undo_ += std::make_shared<UndoU>(*this);
    array_.push_back(std::move(value));
    selectedIndex_ = array_.size() - 1U;
    syncList(selectedIndex_);
    selected();
    notifyChanged();
}

template <typename T>
T ToolsPane<T>::readFields() const {
    const auto name = name_ ? name_->text : lib::String{};
    if constexpr (std::is_same_v<T, structs::BgImageSetting>) {
        return T::opCall(name, intValue(bgX_->value), intValue(bgY_->value),
                         intValue(bgW_->value), intValue(bgH_->value),
                         bgMask_->checked, intValue(bgLayer_->value));
    } else if constexpr (std::is_same_v<T, structs::OuterTool>) {
        return T::opCall(name, toolCommand_->text, toolWorkDir_->text,
                         mnemonic_->text, hotkey_->acceleratorText());
    } else if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
        T value;
        value.name = name; value.enginePath = enginePath_->text;
        value.dataDirName = engineDataDir_->text; value.execute = engineExecute_->text;
        value.mnemonic = mnemonic_->text; value.hotkey = hotkey_->acceleratorText();
        value.type = skinType_->edit_text;
        writeFeatures(value);
        return value;
    } else if constexpr (std::is_same_v<T, structs::ScTemplate>) {
        T value; value.name = name; value.path = templatePath_->text; return value;
    } else {
        T value; value.name = name;
        value.script = detail::normalizeReturnCode(templateScript_->text);
        value.mnemonic = mnemonic_->text; value.hotkey = hotkey_->acceleratorText();
        return value;
    }
}

template <typename T>
void ToolsPane<T>::writeFields(T& value) const {
    value.name = name_->text;
    if constexpr (std::is_same_v<T, structs::BgImageSetting>) {
        value.x = intValue(bgX_->value); value.y = intValue(bgY_->value);
        value.width = intValue(bgW_->value); value.height = intValue(bgH_->value);
        value.mask = bgMask_->checked; value.layer = intValue(bgLayer_->value);
    } else if constexpr (std::is_same_v<T, structs::OuterTool>) {
        value.command = toolCommand_->text; value.workDir = toolWorkDir_->text;
        value.mnemonic = mnemonic_->text; value.hotkey = hotkey_->acceleratorText();
    } else if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
        value.enginePath = enginePath_->text; value.dataDirName = engineDataDir_->text;
        value.execute = engineExecute_->text; value.mnemonic = mnemonic_->text;
        value.hotkey = hotkey_->acceleratorText(); value.type = skinType_->edit_text;
        writeFeatures(value);
    } else if constexpr (std::is_same_v<T, structs::ScTemplate>) {
        value.path = templatePath_->text;
    } else if constexpr (std::is_same_v<T, structs::EvTemplate>) {
        value.script = detail::normalizeReturnCode(templateScript_->text);
        value.mnemonic = mnemonic_->text; value.hotkey = hotkey_->acceleratorText();
    }
}

template <typename T>
lib::String ToolsPane<T>::createName(const T& value) const {
    if constexpr (std::is_same_v<T, structs::OuterTool> ||
                  std::is_same_v<T, structs::ClassicEngine> ||
                  std::is_same_v<T, structs::EvTemplate>)
        return presentation_.menuSample(value.name, value.mnemonic, value.hotkey);
    else return value.name;
}

template <typename T>
void ToolsPane<T>::create() { add(readFields()); }

template <typename T>
void ToolsPane<T>::alt() {
    if (!hasSelection()) return;
    undo_ += std::make_shared<UndoU>(*this);
    writeFields(array_[selectedIndex_]);
    syncList(selectedIndex_);
    canApply_ = false; altEnabled_ = false; alt_->set_enabled(false);
    notifyChanged();
}

template <typename T>
void ToolsPane<T>::del() {
    if (!hasSelection()) return;
    undo_ += std::make_shared<UndoU>(*this);
    array_.erase(array_.begin() + static_cast<std::ptrdiff_t>(selectedIndex_));
    const auto next = array_.empty()
                          ? std::numeric_limits<std::size_t>::max()
                          : std::min(selectedIndex_, array_.size() - 1U);
    syncList(next);
    selected();
    notifyChanged();
}

template <typename T>
void ToolsPane<T>::refUndoMax() {
    undo_.max(comm_.effectiveUndoMax());
}

template <typename T>
void ToolsPane<T>::refMenu(const MenuID id) {
    const std::weak_ptr<void> life{pointer_lifetime_};
    if (id == MenuID::Undo) {
        if (presentation_.menuAccelerator) {
            const auto text = presentation_.menuAccelerator(id);
            if (life.expired()) return;
            undoAccelerator_ = dmenu::convertAccelerator(text);
        } else undoAccelerator_ =
            dmenu::convertAccelerator2(presentation_.undoShortcut);
    } else if (id == MenuID::Redo) {
        if (presentation_.menuAccelerator) {
            const auto text = presentation_.menuAccelerator(id);
            if (life.expired()) return;
            redoAccelerator_ = dmenu::convertAccelerator(text);
        } else redoAccelerator_ =
            dmenu::convertAccelerator2(presentation_.redoShortcut);
    }
}

template <typename T>
void ToolsPane<T>::refreshSkinTypeChoices() {
    if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
        if (!skinType_ || !presentation_.skinTypeChoices) return;
        const std::weak_ptr<void> life{pointer_lifetime_};
        auto choices = presentation_.skinTypeChoices();
        if (life.expired() || !skinType_) return;
        std::vector<lib::ChoiceItem> items;
        items.reserve(choices.size());
        for (auto& choice : choices)
            items.push_back({choice, choice, {}, true});
        const auto editText = skinType_->edit_text;
        ignoringMod_ = true;
        (void)lib::replace_combo_items(skinType_, std::move(items),
                                       std::nullopt, editText,
                                       [life] { return !life.expired(); });
        if (!life.expired()) ignoringMod_ = false;
    }
}

template <typename T>
void ToolsPane<T>::buildFeatureDefinitions(const structs::ClassicEngine* value) {
    if (presentation_.featureRowsFor) {
        featureDefinitions_ = presentation_.featureRowsFor(value);
        return;
    }
    featureDefinitions_ = presentation_.featureRows;
    if (!featureDefinitions_.empty()) return;
    featureDefinitions_.push_back({FeatureDefinition::Group::Ok, {}, 0,
                                   enumText(MenuID::Edit), {}});
    if (!value) return;
    auto append = [this](FeatureDefinition::Group group,
                         const auto& map) {
        for (const auto& item : map) {
            if (std::none_of(featureDefinitions_.begin(), featureDefinitions_.end(),
                            [&](const auto& row) {
                                return row.group == group && row.key == item.first;
                            }))
                featureDefinitions_.push_back({group, item.first, 0, item.first, {}});
        }
    };
    append(FeatureDefinition::Group::Sex, value->sexName);
    append(FeatureDefinition::Group::Period, value->periodName);
    append(FeatureDefinition::Group::Nature, value->natureName);
    append(FeatureDefinition::Group::Makings, value->makingsName);
    for (const auto& item : value->actionCardName)
        featureDefinitions_.push_back({FeatureDefinition::Group::ActionCard, {},
                                       static_cast<std::int32_t>(item.first),
                                       std::to_string(static_cast<std::int32_t>(item.first)), {}});
}

template <typename T>
void ToolsPane<T>::syncFeatureTable(const structs::ClassicEngine* value) {
    if constexpr (std::is_same_v<T, structs::ClassicEngine>) {
        if (!featureTable_) return;
        buildFeatureDefinitions(value);
        std::vector<lib::TableRow> rows;
        rows.reserve(featureDefinitions_.size());
        for (const auto& definition : featureDefinitions_) {
            lib::String variant = definition.variant_name.empty()
                                      ? definition.default_name
                                      : definition.variant_name;
            lib::String manual;
            if (value) manual = featureValue(*value, definition);
            rows.push_back({{
                {definition.default_name, {}, {}, false, true},
                {variant, {}, {}, false, true},
                {manual, {}, {}, false, true}}, false});
        }
        (void)lib::replace_table_rows(featureTable_, std::move(rows), {}, std::nullopt);
        featureUndo_.reset();
    }
}

template <typename T>
void ToolsPane<T>::setFeatureSelection(std::vector<std::size_t> selection,
                                       const bool notify) {
    if (!featureTable_) return;
    (void)lib::select_table_rows(*featureTable_, std::move(selection), false, notify);
}

template <typename T>
void ToolsPane<T>::setFeatureManual(const std::vector<lib::String>& values) {
    if (!featureTable_) return;
    for (std::size_t i = 0; i < featureTable_->rows.size(); ++i) {
        if (featureTable_->rows[i].cells.size() < 3U) continue;
        (void)lib::publish_table_cell_text(
            *featureTable_, i, 2U, i < values.size() ? values[i] : lib::String{});
    }
}

template <typename T>
void ToolsPane<T>::storeFeatures() {
    if (featureTable_) featureUndo_ += std::make_shared<FeatureUndo>(*this);
}

template <typename T>
bool ToolsPane<T>::featureHasSelection() const noexcept {
    return featureTable_ && !featureTable_->selection.empty();
}

template <typename T>
void ToolsPane<T>::copyFeatures() {
    if (!featureHasSelection() || !clipboard_) return;
    std::vector<std::size_t> selection = featureTable_->selection;
    std::sort(selection.begin(), selection.end());
    lib::String text;
    for (const auto row : selection) {
        if (row >= featureTable_->rows.size() || featureTable_->rows[row].cells.size() < 3U) continue;
        if (!text.empty()) text.push_back('\n');
        text += featureTable_->rows[row].cells[2].text;
    }
    lib::ClipboardData data;
    data.text = std::move(text);
    clipboard_->setContents(data);
    notifyChanged();
}

template <typename T>
void ToolsPane<T>::pasteFeatures() {
    if (!featureHasSelection() || !clipboard_) return;
    const auto text = clipboardText();
    if (text.empty()) return;
    storeFeatures();
    std::vector<lib::String> lines;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto end = text.find('\n', begin);
        lines.push_back(text.substr(begin, end == lib::String::npos ? end : end - begin));
        if (end == lib::String::npos) break;
        begin = end + 1U;
    }
    auto selection = featureTable_->selection;
    std::sort(selection.begin(), selection.end());
    const auto first = selection.front();
    std::vector<std::size_t> selected;
    for (std::size_t i = 0; i < lines.size() && first + i < featureTable_->rows.size(); ++i) {
        if (featureTable_->rows[first + i].cells.size() < 3U) continue;
        (void)lib::publish_table_cell_text(*featureTable_, first + i, 2U, lines[i]);
        selected.push_back(first + i);
    }
    setFeatureSelection(std::move(selected), false);
    markModified();
}

template <typename T>
void ToolsPane<T>::deleteFeatures() {
    if (!featureHasSelection()) return;
    storeFeatures();
    for (const auto row : featureTable_->selection)
        if (row < featureTable_->rows.size() && featureTable_->rows[row].cells.size() >= 3U)
            (void)lib::publish_table_cell_text(*featureTable_, row, 2U, {});
    markModified();
}

template <typename T>
void ToolsPane<T>::writeFeatures(structs::ClassicEngine& value) const {
    value.clearFeatures();
    if (!featureTable_) return;
    for (std::size_t i = 0; i < featureDefinitions_.size() && i < featureTable_->rows.size(); ++i) {
        if (featureTable_->rows[i].cells.size() < 3U) continue;
        assignFeatureValue(value, featureDefinitions_[i], featureTable_->rows[i].cells[2].text);
    }
}

template <typename T>
void ToolsPane<T>::featureCellEdited(const std::size_t row,
                                     const std::size_t column,
                                     lib::String value) {
    if (!featureTable_ || column != 2U || row >= featureTable_->rows.size() ||
        featureTable_->rows[row].cells.size() < 3U)
        return;
    storeFeatures();
    (void)lib::publish_table_cell_text(*featureTable_, row, column, std::move(value));
    markModified();
}

template class ToolsPane<structs::BgImageSetting>;
template class ToolsPane<structs::OuterTool>;
template class ToolsPane<structs::ClassicEngine>;
template class ToolsPane<structs::ScTemplate>;
template class ToolsPane<structs::EvTemplate>;

}  // namespace cwxeditor::cwx::editor::gui::dwt::toolspane
