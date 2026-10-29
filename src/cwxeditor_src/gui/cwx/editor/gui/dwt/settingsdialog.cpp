#include "settingsdialog.hpp"
#include "cwxeditor_src/lib/ui_edit_preferences.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/chooser.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/loader.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/commons.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/history.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/partyhistory.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/smalldialogs.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/scripterrordialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/sound.hpp"
#include "cwxeditor_src/lib/editor_diagnostics.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <system_error>

namespace cwxeditor::cwx::editor::gui::dwt::settingsdialog {
namespace commons = ::cwxeditor::cwx::editor::gui::dwt::commons;
namespace dskin = ::cwxeditor::cwx::editor::gui::dwt::dskin;
namespace loader = ::cwxeditor::cwx::editor::gui::dwt::loader;
namespace {

etcsettings::EtcSettings::Presentation etcPresentation(
    const ::cwxeditor::cwx::msgs::Msgs& messages) {
    etcsettings::EtcSettings::Presentation result;
    result.message = [&messages](const std::string_view key) {
        if (const auto* value = messages.findMessage(key)) return value->value;
        return std::string{};
    };
    result.categories = {
        {"common", messages.etcSettingsCommon.value},
        {"load", messages.etcSettingsLoad.value},
        {"file", messages.etcSettingsFile.value},
        {"find", messages.etcSettingsFind.value},
        {"table", messages.etcSettingsTable.value},
        {"scene", messages.etcSettingsScene.value},
        {"event", messages.etcSettingsEvent.value},
        {"card", messages.etcSettingsCard.value},
    };
    result.labels["editTriggerType"] = messages.editTriggerTypeIsQuick.value;
    result.labels["saveChangedOnly"] = messages.saveChangedOnlyClassic.value;
    return result;
}

void bindToolsPaneUndoSettings(
    toolspane::Commons& paneCommons, dprops::Props& prop,
    commons::Commons* editorCommons,
    const std::weak_ptr<const int> common_lifetime,
    const std::weak_ptr<int> owner_lifetime) {
    auto* const properties = &prop;
    // D版の refUndoMax と同様、適用が通知された時点の最新の Props を読む。
    paneCommons.undoLimit = [properties, owner_lifetime] {
        if (owner_lifetime.expired()) return std::size_t{};
        return static_cast<std::size_t>(
            std::max(0, properties->var().undoMaxEtc.value));
    };
    if (!editorCommons) return;
    paneCommons.subscribeUndoMax =
        [editorCommons, common_lifetime,
         owner_lifetime](lib::Action callback) -> lib::Action {
            if (common_lifetime.expired() || owner_lifetime.expired() ||
                !callback)
                return {};
            const auto connection = editorCommons->refUndoMax.add(
                [callback = std::move(callback), common_lifetime,
                 owner_lifetime] {
                    if (common_lifetime.expired() || owner_lifetime.expired())
                        return;
                    callback();
                });
            return [editorCommons, common_lifetime, connection] {
                if (!common_lifetime.expired())
                    editorCommons->refUndoMax.remove(connection);
            };
        };
}

toolspane::Presentation toolsPresentation(
    dprops::Props& prop,
    const std::function<lib::ResourceId(types::MenuID)>& menu_image = {},
    commons::Commons* common = nullptr,
    std::weak_ptr<const int> common_lifetime = {},
    std::weak_ptr<int> owner_lifetime = {}) {
    const auto& messages = prop.requiredMsgs();
    toolspane::Presentation result;
    result.menuText = [&messages](const types::MenuID id) {
        return messages.menuText(id);
    };
    result.menuImage = menu_image;
    result.formatMenuSample = [](String name, String mnemonic,
                                 String hotkey, const bool escape) {
        return menu::MenuProps::buildMenuSample(
            std::move(name), std::move(mnemonic), std::move(hotkey), escape);
    };
    result.boxBgImage = messages.bgImageSettings.value;
    result.boxOuterTool = messages.outerToolsTitle.value;
    result.boxClassicEngine = messages.classicEnginesTitle.value;
    result.boxScenarioTemplate = messages.scenarioTemplatesTitle.value;
    result.boxEventTemplate = messages.eventTemplatesTitle.value;
    result.newText = messages.sNew.value;
    result.alterText = messages.sAlt.value;
    result.deleteText = messages.sDel.value;
    // DWTの ToolsPane と同様、ここでは短いボタン見
    // 出しを使う。 menuText(Up/Down) は長いコ
    // ンテキストメニュー用の説明で、二つのボタンの行では欠ける。
    result.upText = messages.upSelection.value;
    result.downText = messages.downSelection.value;
    result.referenceText = messages.reference.value;
    result.mnemonicText = messages.mnemonic.value;
    result.hotkeyText = messages.hotkey.value;
    result.typeText = messages.type.value;
    result.commandText = messages.outerToolCommand.value;
    result.outerToolDialogTitle = messages.dlgTitOuterTool.value;
    result.workDirText = messages.outerToolWorkDir.value;
    result.workDirDialogTitle = messages.toolWorkDir.value;
    result.workDirDescription = messages.toolWorkDirDesc.value;
    result.enginePathText = messages.classicEnginePath.value;
    result.applicationPath = prop.parent().appPath();
    result.dataDirText = messages.classicEngineDataDirName.value;
    result.dataDirDescription = messages.classicEngineDataDirNameDesc.value;
    result.enginePathDialogTitle = messages.dlgTitClassicEnginePath.value;
    result.engineExecuteDialogTitle = messages.dlgTitClassicEngineExecute.value;
    result.scenarioTemplateDialogTitle = messages.dlgTitScTemplate.value;
    result.executeText = messages.classicEngineExecute.value;
    result.scenarioPathText = messages.scenarioTemplatePath.value;
    result.eventScriptText = messages.eventTemplateScript.value;
    result.templateNameText = messages.eventTemplateName.value;
    result.outerToolHint1 = messages.toolsHint1.value;
    result.outerToolHint2 = messages.toolsHint2.value;
    result.outerToolHint3 = messages.toolsHint3.value;
    result.classicEngineHint = messages.classicEngineHint1.value;
    result.eventTemplateHint1 = messages.eventTemplateHint1.value;
    result.eventTemplateHint2 = messages.eventTemplateHint2.value;
    result.featureText = messages.featureName.value;
    result.featureDefaultText = messages.featureDefaultName.value;
    result.featureVariantText = messages.featureVariantName.value;
    result.featureManualText = messages.featureManualName.value;
    result.featureDefaultNameWidth = static_cast<float>(
        prop.var().featureDefaultNameWidth.value);
    result.featureVariantNameWidth = static_cast<float>(
        prop.var().featureVariantNameWidth.value);
    result.featureManualNameWidth = static_cast<float>(
        prop.var().featureManualNameWidth.value);
    result.storeFeatureColumnWidth = [&prop](const std::size_t column,
                                             const float width) {
        if (!std::isfinite(width) || width <= 0.0F) return;
        const auto saved = static_cast<std::int32_t>(std::lround(width));
        if (column == 0U) prop.var().featureDefaultNameWidth.value = saved;
        else if (column == 1U) prop.var().featureVariantNameWidth.value = saved;
        else if (column == 2U) prop.var().featureManualNameWidth.value = saved;
        (void)prop.apply();
    };
    result.undoMaxEtc = static_cast<std::size_t>(
        std::max(0, prop.var().undoMaxEtc.value));
    result.posLeftMax = static_cast<std::int32_t>(prop.var().posLeftMax.value);
    result.posTopMax = static_cast<std::int32_t>(prop.var().posTopMax.value);
    result.backWidthMax = static_cast<std::int32_t>(prop.var().backWidthMax.value);
    result.backHeightMax = static_cast<std::int32_t>(prop.var().backHeightMax.value);
    result.layerMax = std::max(types::LAYER_BACK_CELL,
                               prop.var().layerMax.value);
    result.tabs = std::max<std::size_t>(1U, prop.var().tabs.value);
    result.classicEngineRegex = prop.var().classicEngineRegex.value;
    result.classicDataDirRegex = prop.var().classicDataDirRegex.value;
    result.classicMatchKey = prop.var().classicMatchKey.value;
    const auto empty_skin = ::cwxeditor::cwx::skin::Skin::emptySkin(
        prop.parent(), {}, {});
    result.featureRowsFor = [&prop, owner_lifetime, empty_skin](
                                const structs::ClassicEngine* value) {
        std::vector<toolspane::FeatureDefinition> rows;
        if (owner_lifetime.expired() || !empty_skin) return rows;
        const auto legacy = value ? value->legacyName() : String{};
        const auto& system = prop.sys();
        rows.push_back({toolspane::FeatureDefinition::Group::Ok, {}, 0,
                        system.evtChildOK({}), system.evtChildOK(legacy)});
        for (const auto feature : empty_skin->allSexes()) {
            const auto key = system.sexName(feature, {});
            rows.push_back({toolspane::FeatureDefinition::Group::Sex, key, 0,
                            key, system.sexName(feature, legacy)});
        }
        for (const auto feature : empty_skin->allPeriods()) {
            const auto key = system.periodName(feature, {});
            rows.push_back({toolspane::FeatureDefinition::Group::Period, key, 0,
                            key, system.periodName(feature, legacy)});
        }
        for (const auto feature : empty_skin->allNatures()) {
            const auto key = system.natureName(feature, {});
            rows.push_back({toolspane::FeatureDefinition::Group::Nature, key, 0,
                            key, system.natureName(feature, legacy)});
        }
        for (const auto left : empty_skin->leftMakings()) {
            for (const auto feature : {left, empty_skin->reverseMakings(left)}) {
                const auto key = system.makingsName(feature, {});
                rows.push_back({toolspane::FeatureDefinition::Group::Makings,
                                key, 0, key,
                                system.makingsName(feature, legacy)});
            }
        }
        for (const auto type : empty_skin->actionCardTypes()) {
            rows.push_back({toolspane::FeatureDefinition::Group::ActionCard, {},
                            static_cast<std::int32_t>(type),
                            system.actionCardName(type, {}),
                            system.actionCardName(type, legacy)});
        }
        return rows;
    };
    result.skinTypes = dskin::findSkinTypes(prop);
    result.skinTypeChoices = [&prop, owner_lifetime] {
        if (owner_lifetime.expired()) return std::vector<String>{};
        return dskin::findSkinTypes(prop);
    };
    result.menuAccelerator = [&prop, owner_lifetime](const types::MenuID id) {
        return owner_lifetime.expired() ? String{} : prop.buildMenu(id);
    };
    result.undoShortcut = prop.buildMenu(types::MenuID::Undo);
    result.redoShortcut = prop.buildMenu(types::MenuID::Redo);
    const auto patterns = loader::scenarioFilter();
    const auto descriptions = loader::scenarioFilterDesc(prop);
    for (std::size_t index = 0; index < patterns.size(); ++index) {
        const auto description = index < descriptions.size()
                                     ? descriptions[index]
                                     : String{};
        result.scenarioTemplateFilters.push_back(
            {description, {patterns[index]}, {}});
    }
    result.executableFilterText = messages.exeFileDescExe.value;
    result.allFilesFilterText = messages.exeFileDescAll.value;
    result.subscribeSkinTypeChanges = [common, common_lifetime,
                                       owner_lifetime](lib::Action callback) -> lib::Action {
        if (!common || common_lifetime.expired() || owner_lifetime.expired() ||
            !callback) return lib::Action{};
        const auto invoke = [callback, common_lifetime, owner_lifetime] {
            if (!common_lifetime.expired() && !owner_lifetime.expired())
                callback();
        };
        const auto skin = common->refSkin.add(invoke);
        const auto sort = common->refSortCondition.add(invoke);
        const auto classic = common->refClassicSkin.add(invoke);
        const auto settings = common->refSettingsWithSkinTypes.add(invoke);
        return [common, common_lifetime, skin, sort, classic, settings] {
            if (common_lifetime.expired()) return;
            common->refSkin.remove(skin);
            common->refSortCondition.remove(sort);
            common->refClassicSkin.remove(classic);
            common->refSettingsWithSkinTypes.remove(settings);
        };
    };
    result.subscribeMenu = [common, common_lifetime,
                            owner_lifetime](std::function<void(types::MenuID)> callback) -> lib::Action {
        if (!common || common_lifetime.expired() || owner_lifetime.expired() ||
            !callback) return lib::Action{};
        const auto connection = common->refMenu.add(
            [callback = std::move(callback), common_lifetime,
             owner_lifetime](const types::MenuID id) {
                if (common_lifetime.expired() || owner_lifetime.expired()) return;
                if (id == types::MenuID::Undo || id == types::MenuID::Redo)
                    callback(id);
            });
        return [common, common_lifetime, connection] {
            if (!common_lifetime.expired()) common->refMenu.remove(connection);
        };
    };
    return result;
}

template <typename T>
void invokeOptionalCallback(const T& callback) {
    if (callback) callback();
}

void bindSplitWeights(const std::shared_ptr<splitpane::SplitPane>& split,
                      std::int32_t& left, std::int32_t& right,
                      lib::Action save = {}) {
    if (!split) return;
    auto* const leftValue = &left;
    auto* const rightValue = &right;
    split->on_weights_changed =
        [leftValue, rightValue, save = std::move(save)](
            const std::vector<float>& weights) {
            if (weights.size() < 2U) return;
            *leftValue = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[0])));
            *rightValue = static_cast<std::int32_t>(
                std::max(1.0F, std::round(weights[1])));
            if (save) save();
        };
}

std::shared_ptr<lib::TextEdit> textControl(String value = {}, bool multiline = false) {
    auto result = std::make_shared<lib::TextEdit>(std::move(value));
    result->multiline = multiline;
    return result;
}

std::shared_ptr<lib::CheckBox> checkControl(String title = {}, bool checked = false) {
    auto result = std::make_shared<lib::CheckBox>(std::move(title));
    result->checked = checked;
    return result;
}

std::shared_ptr<lib::Button> pushControl(String title = {}) {
    return std::make_shared<lib::Button>(std::move(title));
}

std::shared_ptr<lib::NumberEdit> numberControl(double value = 0.0,
                                               double minimum = 0.0,
                                               double maximum = 100000.0) {
    auto result = std::make_shared<lib::NumberEdit>();
    result->value = value;
    result->minimum = minimum;
    result->maximum = maximum;
    result->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                             lib::Align::Center, {60.0F, 24.0F}});
    return result;
}

std::shared_ptr<lib::ComboBox> comboControl(const lib::Array<String>& values) {
    auto result = std::make_shared<lib::ComboBox>();
    for (const auto& value : values) result->items.push_back({value, {}, 0, true});
    if (!result->items.empty()) result->selected_index = 0;
    return result;
}


bool publishComboSelection(const std::shared_ptr<lib::ComboBox>& combo,
                           const std::optional<std::size_t> selected) {
    if (!combo) return false;
    auto items = combo->items;
    return lib::replace_combo_items(combo, std::move(items), selected,
                                    combo->edit_text);
}

void appendControl(const std::shared_ptr<lib::Panel>& panel,
                   const std::shared_ptr<lib::Widget>& control) {
    if (panel && control) panel->add(control);
}

std::shared_ptr<lib::Panel> verticalPanel(const float spacing = 6.0F,
                                          const lib::Insets padding = {6, 6, 6, 6}) {
    auto result = std::make_shared<lib::Panel>();
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical);
    layout->spacing = spacing;
    layout->padding = padding;
    result->set_layout(layout);
    return result;
}

std::shared_ptr<lib::GroupBox> groupBox(String title, const std::size_t columns = 2U) {
    auto result = std::make_shared<lib::GroupBox>();
    result->title = std::move(title);
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = std::max<std::size_t>(1U, columns);
    layout->spacing = 5.0F;
    layout->padding = {18.0F, 7.0F, 7.0F, 7.0F};
    result->set_layout(layout);
    return result;
}

std::shared_ptr<lib::Label> labelControl(String text) {
    auto result = std::make_shared<lib::Label>(std::move(text));
    result->alignment = lib::TextAlignment::Start;
    return result;
}

void place(const std::shared_ptr<lib::Container>& parent,
           const std::shared_ptr<lib::Widget>& child,
           const std::size_t row, const std::size_t column,
           const std::size_t column_span = 1U, const float grow = 0.0F,
           const lib::Align horizontal = lib::Align::Stretch) {
    if (!parent || !child) return;
    const auto sizing = child->layout_item();
    child->set_layout_item({row, column, 1U, column_span, horizontal,
                            lib::Align::Center, sizing.minimum,
                            sizing.preferred, sizing.margin, grow});
    parent->add(child);
}

std::shared_ptr<lib::Panel> scrollBody(const std::shared_ptr<lib::Panel>& tab) {
    if (!tab) return {};
    tab->set_layout(std::make_shared<lib::FillLayout>());
    auto scroll = std::make_shared<lib::ScrollView>();
    scroll->vertical_scroll = true;
    scroll->horizontal_scroll = false;
    scroll->set_layout(std::make_shared<lib::FillLayout>());
    auto body = verticalPanel();
    scroll->add(body);
    tab->add(scroll);
    return body;
}

template <typename T>
void setNumber(const std::shared_ptr<lib::NumberEdit>& control, T value) {
    if (control) control->value = static_cast<double>(value);
}

template <typename T>
T numberValue(const std::shared_ptr<lib::NumberEdit>& control, T fallback = {}) {
    if (!control) return fallback;
    return static_cast<T>(std::llround(control->value));
}

template <typename T>
void setChecked(const std::shared_ptr<T>& control, bool value) {
    if (control) control->checked = value;
}

bool containsExtension(const String& path, const lib::Array<String>& extensions) {
    const auto dot = path.find_last_of('.');
    if (dot == String::npos) return false;
    String extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    for (auto candidate : extensions) {
        std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (extension == candidate) return true;
    }
    return false;
}

bool equalExecutionParty(const structs::ExecutionParty& left,
                         const structs::ExecutionParty& right) {
    return left.engineName == right.engineName && left.enginePath == right.enginePath &&
           left.isClassic == right.isClassic && left.yadoName == right.yadoName &&
           left.yadoPath == right.yadoPath && left.partyName == right.partyName &&
           left.partyPath == right.partyPath;
}

bool equalExecutionParties(const lib::Array<structs::ExecutionParty>& left,
                           const lib::Array<structs::ExecutionParty>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (!equalExecutionParty(left[i], right[i])) return false;
    }
    return true;
}

bool equalOpenHistory(const structs::OpenHistory& left,
                      const structs::OpenHistory& right) {
    return left.path == right.path && left.skinType == right.skinType &&
           left.skinName == right.skinName && left.skinEngine == right.skinEngine;
}

bool equalOpenHistories(const lib::Array<structs::OpenHistory>& left,
                        const lib::Array<structs::OpenHistory>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (!equalOpenHistory(left[i], right[i])) return false;
    }
    return true;
}

bool equalEventTemplates(const lib::Array<structs::EvTemplate>& left,
                         const lib::Array<structs::EvTemplate>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i].name != right[i].name || left[i].script != right[i].script ||
            left[i].mnemonic != right[i].mnemonic || left[i].hotkey != right[i].hotkey) {
            return false;
        }
    }
    return true;
}

bool equalClassicEngines(const lib::Array<structs::ClassicEngine>& left,
                         const lib::Array<structs::ClassicEngine>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i].name != right[i].name || left[i].enginePath != right[i].enginePath ||
            left[i].dataDirName != right[i].dataDirName || left[i].execute != right[i].execute ||
            left[i].mnemonic != right[i].mnemonic || left[i].hotkey != right[i].hotkey ||
            left[i].type != right[i].type || left[i].okText != right[i].okText ||
            left[i].sexName != right[i].sexName || left[i].periodName != right[i].periodName ||
            left[i].natureName != right[i].natureName || left[i].makingsName != right[i].makingsName ||
            left[i].actionCardName != right[i].actionCardName) {
            return false;
        }
    }
    return true;
}

String basename(const String& path) {
    return structs::pathToUtf8(structs::pathFromUtf8(path).filename());
}

std::filesystem::path initialDirectory(const String& value) {
    std::error_code error;
    const auto candidate = structs::pathFromUtf8(value);
    if (!candidate.empty() && std::filesystem::is_directory(candidate, error) && !error)
        return candidate;
    error.clear();
    if (!candidate.empty() && candidate.has_parent_path()) {
        const auto parent = candidate.parent_path();
        if (std::filesystem::is_directory(parent, error) && !error) return parent;
    }
    error.clear();
    const auto current = std::filesystem::current_path(error);
    return error ? std::filesystem::path{} : current;
}

}  // namespace

SettingsDialog::SettingsDialog(
    dprops::Props& prop, std::shared_ptr<lib::Window> parent,
    std::shared_ptr<properties::DockingFolderCTC> dock, summary::Summary* summ,
    lib::Action sendReloadProps, Callbacks callbacks, lib::Services services)
    : absdialog::AbsDialog(prop, std::move(parent), false,
                           prop.requiredMsgs().dlgTitSettings.value,
                           prop.images().load(dprops::StringView{"settings.png"}), true,
                           &prop.flex().settingsDlg, true, true, {}, false,
                           std::move(services)),
      _comm(std::move(callbacks)), _prop(&prop), _summ(summ), _dock(std::move(dock)),
      _sendReloadProps(std::move(sendReloadProps)), _menuProps(prop.flex().menu),
      _refe(this), _sele(this),
      _selt(this), _selb(this), _selbs(this), _selw(this), _clearh(this),
      _clearsh(this), _clearph(this), _selectmenu(this), _modmenu(this),
      _delmenuaccel(this), _applymenu(this) {
    if (_comm.editorCommons) {
        _editorCommonsLifetime = _comm.editorCommons->lifetimeToken();
        if (!lib::editor_window_host_options().all_mode) {
            const std::weak_ptr<int> life = _skinLifetime;
            const auto common_life = _editorCommonsLifetime;
            _initializerUndoConnection = _comm.editorCommons->refUndoMax.add(
                [this, life, common_life] {
                    if (life.expired() || common_life.expired()) return;
                    // D版と同様、現在のエディタだけでなく、各バージョンが保持する初期値編集の履歴を更新する。
                    const auto limit = static_cast<std::size_t>(std::max(0, _prop->var().undoMaxEtc.value));
                    for (const auto& manager : _initializerUndos)
                        if (manager) manager->max(limit);
                });
        }
        const std::weak_ptr<int> life = _skinLifetime;
        const auto common_life = _editorCommonsLifetime;
        _elementOverridesUndoConnection = _comm.editorCommons->refUndoMax.add(
            [this, life, common_life] {
                if (life.expired() || common_life.expired()) return;
                if (_elementOverrides) _elementOverrides->refUndoMax();
            });
    }
    closeEvent.push_back([this] {
        const std::weak_ptr<int> lifetime=_skinLifetime;
        // 保持されている同じ SettingsDialog を後
        // で開き直す場合も含め、今回の表示への応答を無効化する。
        _questionLifetime = std::make_shared<int>(0);
        _applyQuestionPending = false;
        _applyQuestionResult.reset();
        if (_skinTypeChooser && _skinTypeChooser->widget())
            _skinTypeChooser->widget()->set_enabled(true);
        if (lifetime.expired()) return;
        if (_etcSettings) _etcSettings->closeSearch();
        // 子のリセット・配置・ネイティブの非表示コールバックの後は、処理を続けない。
    });
}

SettingsDialog::~SettingsDialog() {
    if (_cEnginesView) _cEnginesView->retireFeatureWindow();
    _skinLifetime.reset(); _questionLifetime.reset();
    _menuSearchLifetime.reset();
    if (auto* common = _comm.editorCommons; common && !_editorCommonsLifetime.expired()) {
        common->refUndoMax.remove(_initializerUndoConnection);
        common->refUndoMax.remove(_elementOverridesUndoConnection);
        common->removeToolBar(_clearHist);
        common->removeToolBar(_clearSHist);
        common->removeToolBar(_clearPHist);
    }
}

void SettingsDialog::setup(Container& area) {
    const auto setup_started = std::chrono::steady_clock::now();
    const auto log_phase = [&setup_started](const char* phase) {
        lib::editor_action_log(
            String("Settings setup phase=") + phase + " elapsed_us=" +
            std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - setup_started).count()));
    };
    if (dialog()) {
        dialog()->minimum_size = lib::Size{760.0F, 650.0F};
        dialog()->icon = _prop->images().load(dprops::StringView{"settings.png"});
    }
    auto layout = std::make_shared<lib::FillLayout>();
    layout->padding = {6, 6, 6, 6};
    area.set_layout(layout);
    _tabf = std::make_shared<lib::TabView>();
    _tabf->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                            lib::Align::Stretch, {760, 560}, {}, {}, 1.0F});
    area.add(_tabf);
    construct1(*_tabf);
    log_phase("basic");
    construct2(*_tabf);
    log_phase("background");
    construct3(*_tabf);
    log_phase("template");
    construct4(*_tabf);
    log_phase("external");
    construct5(*_tabf);
    log_phase("other");
    _tabf->selected_index = 0;
    syncControlsFromProperties();
    refreshEnabled();
    refreshMenu();
    log_phase("sync");
    // 既定の行は最初のスキン固有設定ではなく、全体の配列を表す。
    // 選択欄の初期設定コールバックがすべて終わってから、各表示
    // を再反映する。 そうしないと、構築中の空の上書き値が二つ
    // の編集欄と二つの自動キーコード表を置き換える場合がある。
    if (_skinTypeChooser && _skinTypeChooser->selected() < 0) {
        if (_selections) {
            _selections->text.clear();
            for (const auto& item : _prop->var().standardSelections.value)
                _selections->text += item + "\n";
        }
        if (_keyCodes) {
            _keyCodes->text.clear();
            for (const auto& item : _prop->var().standardKeyCodes.value)
                _keyCodes->text += item + "\n";
        }
        _keyCodesByFeatures = dprops::arraySnapshot(
            _prop->var().keyCodesByFeatures.value);
        _keyCodesByMotions = dprops::arraySnapshot(
            _prop->var().keyCodesByMotions.value);
        if (_keyCodesByFeaturesView)
            _keyCodesByFeaturesView->values(_keyCodesByFeatures);
        if (_keyCodesByMotionsView)
            _keyCodesByMotionsView->values(_keyCodesByMotions);
    }
}

bool SettingsDialog::noScenario() const noexcept { return true; }

void SettingsDialog::construct1(lib::TabView& tabf) {
    _tabB = std::make_shared<lib::Panel>();
    const auto body = _tabB;
    auto bodyLayout = std::make_shared<lib::GridLayout>();
    bodyLayout->columns = 3U;
    bodyLayout->spacing = 5.0F;
    bodyLayout->padding = {6, 6, 6, 6};
    body->set_layout(bodyLayout);
    const auto& messages = _prop->requiredMsgs();
    const auto& value = _prop->var();
    const auto text = [this](Text& target, String initial = {}) {
        target = textControl(std::move(initial));
        mod(*target);
        return target;
    };
    const auto check = [this](ToggleButtonPtr& target, String title) {
        target = checkControl(std::move(title));
        mod(*target);
        return target;
    };
    const auto radio = [this](RadioButtonPtr& target, String title) {
        target = std::make_shared<lib::RadioButton>(std::move(title));
        target->group = "backup-interval";
        mod(*target);
        return target;
    };
    const auto number = [this](Spinner& target, const double initial,
                               const double minimum = 0.0,
                               const double maximum = 100000.0) {
        target = numberControl(initial, minimum, maximum);
        mod(*target);
        return target;
    };

    const bool allMode = lib::editor_window_host_options().all_mode;
    String engineTitle = messages.enginePath.value;
    if (const auto marker = engineTitle.find("%1$s"); marker != String::npos)
        engineTitle.replace(marker, 4U, value.engine.value);
    const auto top = std::make_shared<lib::Panel>();
    auto topLayout = std::make_shared<lib::GridLayout>();
    topLayout->columns = allMode ? 2U : 3U;
    topLayout->spacing = 6.0F;
    top->set_layout(topLayout);
    // フォームは内容の実測高さに保ち、余りに合わせて伸ばさない。 こ
    // こで表示領域を固定すると子の最小寸法が無視され、Webの文字
    // ・枠が指定高さを超えた際に最後の入力行がつぶれる。 全体が収
    // まらない場合は、共通ダイアログの表示領域をスクロールさせる。
    top->set_layout_item({0, 0, 1, 3, lib::Align::Stretch,
                          lib::Align::Center, {700, 104}, {}, {}, 0.0F});
    body->add(top);
    if (!allMode) {
        const auto engine = groupBox(std::move(engineTitle), 4U);
        engine->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                 lib::Align::Center, {450, 80}});
        top->add(engine);
        place(engine, text(_enginePath), 0, 0, 2U, 1.0F);
        _refEnginePath = pushControl("...");
        _refEnginePath->on_click = [this] { selectEngine(); };
        place(engine, _refEnginePath, 0, 2);
        const auto openEnginePath = pushControl("");
        openEnginePath->icon = _prop->images().load(dprops::StringView{"folder.png"});
        openEnginePath->set_tooltip(_prop->requiredMsgs().menuText(types::MenuID::OpenPlace));
        openEnginePath->on_click = [this] {
            if (!_enginePath || !_prop->images().services().shell) return;
            const auto path = dutils::openButtonDirectory(_prop, _enginePath->text);
            if (!path) return;
            String error;
            (void)_prop->images().services().shell->open_path(
                structs::pathFromUtf8(*path), error);
        };
        place(engine, openEnginePath, 0, 3);
        place(engine, check(_findEnginePath, messages.findEnginePath.value), 1, 0, 4U);
        dutils::setupDropFile(
            engine, _enginePath,
            [this](const dutils::FileNames& files) { return dropEngine(files); }, {},
            _prop->images().services());
        const auto previousEngineChanged = _enginePath->on_changed;
        _enginePath->on_changed = [this, previousEngineChanged](std::string_view value) {
            if (previousEngineChanged) previousEngineChanged(value);
            refEngineEnabled();
        };
        const auto previousFindEngineChanged = _findEnginePath->on_changed;
        _findEnginePath->on_changed = [this, previousFindEngineChanged](bool checked) {
            if (previousFindEngineChanged) previousFindEngineChanged(checked);
            refEngineEnabled();
        };
        _openButtonEnabledRefreshes.push_back(
            dutils::bindOpenButtonEnabledToNonEmptyText(openEnginePath, _enginePath));
    }

    const auto scale = groupBox(messages.previewScale.value, 2U);
    const auto language = groupBox(messages.languageSetting.value, 1U);
    scale->set_layout_item({0, allMode ? 0U : 1U, 1, 1, lib::Align::Stretch,
                            lib::Align::Stretch, {110, 104}});
    language->set_layout_item({0, allMode ? 1U : 2U, 1, 1, lib::Align::Stretch,
                               lib::Align::Stretch, {171, 80}});
    top->add(scale);
    top->add(language);
    lib::Array<String> uiScaleNames;
    for (const auto percent : {100, 125, 150, 175, 200})
        uiScaleNames.push_back(std::to_string(percent) + "%");
    _drawingScale = comboControl(uiScaleNames);
    _imageScale = comboControl(uiScaleNames);
    _uiTextScale = comboControl(uiScaleNames);
    const auto visibleItems = static_cast<std::size_t>(
        std::max(1, value.comboVisibleItemCount.value));
    _drawingScale->visible_item_count = visibleItems;
    _imageScale->visible_item_count = visibleItems;
    _uiTextScale->visible_item_count = visibleItems;
    place(scale, labelControl(messages.drawingScale.value), 0, 0);
    place(scale, _drawingScale, 0, 1, 1U, 1.0F);
    place(scale, labelControl(messages.imageScale.value), 1, 0);
    place(scale, _imageScale, 1, 1, 1U, 1.0F);
    place(scale, labelControl(messages.uiTextScale.value), 2, 0);
    place(scale, _uiTextScale, 2, 1, 1U, 1.0F);
    mod(*_drawingScale);
    mod(*_imageScale);
    mod(*_uiTextScale);

    _msgsTableIndex.clear();
    _msgsTableFile.clear();
    String defaultLocale;
    auto messageTables = _prop->parent().msgsTable(
        value.languageDir.value, _msgsTableFile, defaultLocale);
    _language = comboControl({static_cast<String>(messages.languageSystem)});
    _language->visible_item_count = visibleItems;
    std::vector<String> locales;
    for (const auto& [locale, ignored] : messageTables) {
        (void)ignored;
        locales.push_back(locale);
    }
    std::sort(locales.begin(), locales.end());
    for (const auto& locale : locales) {
        const auto found = messageTables.find(locale);
        if (found == messageTables.end() || !found->second) continue;
        const int index = static_cast<int>(_language->items.size());
        _language->items.push_back({static_cast<String>(found->second->localeName), locale});
        _msgsTableIndex[index] = locale;
    }
    place(language, _language, 0, 0, 1U, 1.0F);
    place(language, labelControl(messages.languageCaution.value), 1, 0);
    mod(*_language);
    if (allMode) {
        // ALL仕様: 基本設定の「拡大率」「言語」は表示しない。値の読み書きは
        // 通常どおり行うため、コントロールは作成して非表示にする。
        // (エンジンパスも ALL では無いので、上段の行ごと非表示になる)
        scale->set_visible(false);
        language->set_visible(false);
        top->set_visible(false);
    }

    if (!allMode) {
        const auto temporary = groupBox(messages.tempDir.value, 3U);
        temporary->set_layout_item({1, 0, 1, 3, lib::Align::Stretch,
                                    lib::Align::Center, {500, 48}, {700, 48}, {},
                                    0.0F});
        body->add(temporary);
        place(temporary, text(_tempDir), 0, 0, 1U, 1.0F);
        _tempRef = pushControl("...");
        _tempRef->on_click = [this] { selectTemp(); };
        place(temporary, _tempRef, 0, 1);
        const auto openTemp = pushControl("");
        openTemp->icon = _prop->images().load(dprops::StringView{"folder.png"});
        openTemp->set_tooltip(_prop->requiredMsgs().menuText(types::MenuID::OpenDir));
        openTemp->on_click = [this] {
            if (!_tempDir || !_prop->images().services().shell) return;
            const auto resolved = dutils::openButtonDirectory(_prop, _tempDir->text);
            if (!resolved) return;
            const auto path = structs::pathFromUtf8(*resolved);
            String error;
            (void)_prop->images().services().shell->open_path(path, error);
        };
        place(temporary, openTemp, 0, 2);
        dutils::setupDropFile(temporary, _tempDir, dutils::dropDir, {},
                              _prop->images().services());
        _openButtonEnabledRefreshes.push_back(
            dutils::bindOpenButtonEnabledToNonEmptyText(openTemp, _tempDir));
    }

    const auto backup = groupBox(messages.backupDir.value, 1U);
    backup->set_layout_item({2, 0, 1, 3, lib::Align::Stretch,
                             lib::Align::Center, {700, allMode ? 78.0F : 110.0F},
                             {700, allMode ? 78.0F : 110.0F}, {},
                             0.0F});
    body->add(backup);
    const auto compactRow = [](const std::size_t columns) {
        auto row = std::make_shared<lib::Panel>();
        auto rowLayout = std::make_shared<lib::GridLayout>();
        rowLayout->columns = columns;
        rowLayout->spacing = 5.0F;
        row->set_layout(rowLayout);
        return row;
    };
    const auto compactSpinner = [&](Spinner& target, const double initial,
                                    const double minimum, const double maximum) {
        auto control = number(target, initial, minimum, maximum);
        auto item = control->layout_item();
        item.minimum.width = 40.0F;
        control->set_layout_item(item);
        return control;
    };

    const auto backupMode = compactRow(10U);
    place(backup, backupMode, 0, 0);
    place(backupMode, check(_backupEnabled, messages.backupEnabled.value), 0, 0);
    place(backupMode, radio(_backupIntervalTypeTime, messages.backupIntervalTime.value), 0, 1);
    place(backupMode, compactSpinner(_backupIntervalTime, 15.0, 1.0, 99.0), 0, 2);
    place(backupMode, labelControl(messages.minute.value), 0, 3);
    place(backupMode, radio(_backupIntervalTypeEdit, messages.backupIntervalEdit.value), 0, 4);
    place(backupMode, compactSpinner(_backupIntervalEdit, 20.0, 1.0, 9999.0), 0, 5);
    place(backupMode, labelControl(messages.count.value), 0, 6);
    place(backupMode, labelControl(messages.backupCount.value), 0, 7);
    place(backupMode, compactSpinner(_backupCount, 10.0, 0.0, 99.0), 0, 8);
    place(backupMode, labelControl(messages.count.value), 0, 9);

    const auto backupPath = compactRow(4U);
    if (!allMode) {
        place(backup, backupPath, 1, 0);
        place(backupPath, labelControl(messages.backupPath.value), 0, 0);
        place(backupPath, text(_backupDir), 0, 1, 1U, 1.0F);
        _backupRef = pushControl("...");
        _backupRef->on_click = [this] { selectBackup(); };
        place(backupPath, _backupRef, 0, 2);
    }
    const auto backupChecks = compactRow(3U);
    place(backup, backupChecks, allMode ? 1U : 2U, 0);
    place(backupChecks, check(_backupRefAuthor, messages.backupRefAuthor.value), 0, 0);
    place(backupChecks, check(_autoSave, messages.autoSave.value), 0, 1);
    place(backupChecks, check(_backupArchived, messages.backupArchived.value), 0, 2);
    const auto refreshBackup = [this](const std::shared_ptr<lib::ToggleButton>& control) {
        const auto previous = control->on_changed;
        control->on_changed = [this, previous](bool checked) {
            if (previous) previous(checked);
            refreshEnabled();
        };
    };
    refreshBackup(_backupEnabled);
    refreshBackup(_backupIntervalTypeTime);
    refreshBackup(_backupIntervalTypeEdit);
    if (!allMode) {
        _backupDirOpen = pushControl("");
        _backupDirOpen->icon = _prop->images().load(dprops::StringView{"folder.png"});
        _backupDirOpen->set_tooltip(_prop->requiredMsgs().menuText(types::MenuID::OpenDir));
        _backupDirOpen->on_click = [this] {
            if (!_backupDir || !_prop->images().services().shell) return;
            const auto path = dutils::openButtonDirectory(_prop, _backupDir->text);
            if (!path) return;
            String error;
            (void)_prop->images().services().shell->open_path(
                structs::pathFromUtf8(*path), error);
        };
        place(backupPath, _backupDirOpen, 0, 3);
        dutils::setupDropFile(backup, _backupDir, dutils::dropDir, {},
                              _prop->images().services());
        _openButtonEnabledRefreshes.push_back(
            dutils::bindOpenButtonEnabledToNonEmptyText(_backupDirOpen, _backupDir));
    }

    const auto beforeSave = groupBox(messages.backupBeforeSaveDir.value, 5U);
    beforeSave->set_layout_item({3, 0, 1, 3, lib::Align::Stretch,
                                 lib::Align::Center, {700, allMode ? 40.0F : 72.0F},
                                 {700, allMode ? 40.0F : 72.0F}, {},
                                 0.0F});
    body->add(beforeSave);
    place(beforeSave, check(_backupBeforeSaveEnabled,
                            messages.backupBeforeSaveEnabled.value), 0, 0, 5U);
    refreshBackup(_backupBeforeSaveEnabled);
    if (!allMode) {
        place(beforeSave, labelControl(messages.backupBeforeSavePath.value), 1, 0);
        place(beforeSave, text(_backupBeforeSaveDir), 1, 1, 2U, 1.0F);
        _backupBeforeSaveRef = pushControl("...");
        _backupBeforeSaveRef->on_click = [this] { selectBackupBeforeSave(); };
        place(beforeSave, _backupBeforeSaveRef, 1, 3);
        _backupBeforeSaveDirOpen = pushControl("");
        _backupBeforeSaveDirOpen->icon =
            _prop->images().load(dprops::StringView{"folder.png"});
        _backupBeforeSaveDirOpen->set_tooltip(
            _prop->requiredMsgs().menuText(types::MenuID::OpenDir));
        _backupBeforeSaveDirOpen->on_click = [this] {
            if (!_backupBeforeSaveDir || !_prop->images().services().shell) return;
            const auto path =
                dutils::openButtonDirectory(_prop, _backupBeforeSaveDir->text);
            if (!path) return;
            String error;
            (void)_prop->images().services().shell->open_path(
                structs::pathFromUtf8(*path), error);
        };
        place(beforeSave, _backupBeforeSaveDirOpen, 1, 4);
        dutils::setupDropFile(beforeSave, _backupBeforeSaveDir, dutils::dropDir, {},
                              _prop->images().services());
        _openButtonEnabledRefreshes.push_back(
            dutils::bindOpenButtonEnabledToNonEmptyText(
                _backupBeforeSaveDirOpen, _backupBeforeSaveDir));
    }

    const auto defaults = std::make_shared<splitpane::SplitPane>(splitpane::Horizontal);
    defaults->set_layout_item({4, 0, 1, 2, lib::Align::Stretch,
                               lib::Align::Center, {480, 64}, {}, {}, 0.0F});
    body->add(defaults);
    const auto author = groupBox(messages.scenarioAuthor.value, 1U);
    const auto startArea = groupBox(messages.startAreaName.value, 1U);
    defaults->add(author);
    defaults->add(startArea);
    place(author, text(_author), 0, 0, 1U, 1.0F);
    place(startArea, text(_newAreaName), 0, 0, 1U, 1.0F);
    defaults->setWeights({value.authorNewAreaNameSashL.value,
                          value.authorNewAreaNameSashR.value});
    bindSplitWeights(defaults, _prop->var().authorNewAreaNameSashL.value,
                     _prop->var().authorNewAreaNameSashR.value,
                     [this] { saveWin(); });

    const auto wallpaper = groupBox(messages.wallpaper.value, 4U);
    wallpaper->set_layout_item({5, 0, 1, 2, lib::Align::Stretch,
                                lib::Align::Center, {480, 78}, {480, 78}, {},
                                0.0F});
    body->add(wallpaper);
    place(wallpaper, text(_wallpaper), 0, 0, 2U, 1.0F);
    _wallpaperRef = pushControl("...");
    _wallpaperRef->on_click = [this] { selectWallpaper(); };
    place(wallpaper, _wallpaperRef, 0, 2);
    const auto openWallpaper = pushControl("");
    openWallpaper->icon = _prop->images().load(dprops::StringView{"folder.png"});
    openWallpaper->set_tooltip(_prop->requiredMsgs().menuText(types::MenuID::OpenPlace));
    openWallpaper->on_click = [this] {
        if (!_wallpaper || !_prop->images().services().shell) return;
        const auto path = dutils::openButtonDirectory(_prop, _wallpaper->text);
        if (!path) return;
        String error;
        (void)_prop->images().services().shell->open_path(
            structs::pathFromUtf8(*path), error);
    };
    place(wallpaper, openWallpaper, 0, 3);
    dutils::setupDropFile(
        wallpaper, _wallpaper,
        [this](const dutils::FileNames& files) { return dropWallpaper(files); }, {},
        _prop->images().services());
    _openButtonEnabledRefreshes.push_back(
        dutils::bindOpenButtonEnabledToNonEmptyText(openWallpaper, _wallpaper));
    place(wallpaper, labelControl(messages.wallpaperStyle.value), 1, 0);
    _wallpaperStyle = comboControl({messages.wallpaperStyleNameCenter.value,
                                    messages.wallpaperStyleNameTile.value,
                                    messages.wallpaperStyleNameExpand.value,
                                    messages.wallpaperStyleNameExpandFull.value});
    _wallpaperStyle->visible_item_count = visibleItems;
    place(wallpaper, _wallpaperStyle, 1, 1, 3U, 1.0F);
    mod(*_wallpaperStyle);
    _wallpaperStyleTbl.clear();
    _wallpaperStyleTbl2.clear();
    for (int i = 0; i < 4; ++i) {
        _wallpaperStyleTbl[i] = i;
        _wallpaperStyleTbl2[i] = i;
    }
    const auto sounds = groupBox(messages.systemSounds.value, 6U);
    sounds->set_layout_item({6, 0, 1, 2, lib::Align::Stretch,
                             lib::Align::Center, {480, 52}, {480, 52}, {},
                             0.0F});
    body->add(sounds);
    place(sounds, labelControl(messages.soundSaved.value), 0, 0);
    place(sounds, text(_savedSound, value.savedSound.value), 0, 1, 1U, 1.0F);
    _savedSoundRef = pushControl("...");
    _savedSoundRef->on_click = [this] { (void)selectSysSound(_savedSound); };
    place(sounds, _savedSoundRef, 0, 2);
    const auto openSavedSound = pushControl("");
    openSavedSound->icon = _prop->images().load(dprops::StringView{"folder.png"});
    openSavedSound->set_tooltip(_prop->requiredMsgs().menuText(types::MenuID::OpenPlace));
    openSavedSound->on_click = [this] {
        if (!_savedSound || !_prop->images().services().shell) return;
        const auto path = dutils::openButtonDirectory(_prop, _savedSound->text);
        if (!path) return;
        String error;
        (void)_prop->images().services().shell->open_path(
            structs::pathFromUtf8(*path), error);
    };
    place(sounds, openSavedSound, 0, 3);
    _openButtonEnabledRefreshes.push_back(
        dutils::bindOpenButtonEnabledToNonEmptyText(openSavedSound, _savedSound));
    const auto playSavedSound = pushControl("");
    playSavedSound->icon = _comm.menuImage
                               ? _comm.menuImage(types::MenuID::PlaySE)
                               : _prop->images().load(dprops::StringView{"sound_play.png"});
    playSavedSound->on_click = [this] {
        if (!_savedSound || _savedSound->text.empty()) return;
        sound::playSE(_savedSound->text, 0, 1, sound::lastSoundType, false);
    };
    place(sounds, playSavedSound, 0, 4);
    const auto stopSavedSound = pushControl("");
    stopSavedSound->icon = _comm.menuImage
                               ? _comm.menuImage(types::MenuID::StopSE)
                               : _prop->images().load(dprops::StringView{"sound_stop.png"});
    stopSavedSound->on_click = [] { sound::stopSE(); };
    place(sounds, stopSavedSound, 0, 5);
    dutils::setupDropFile(
        sounds, _savedSound,
        [this](const dutils::FileNames& files) { return dropSysSound(files); }, {},
        _prop->images().services());
    if (allMode) {
        // ALL仕様: 基本設定の「エディタの壁紙」「システム音声」は表示しない
        // (値は保持したまま、コントロールを非表示にする)。
        wallpaper->set_visible(false);
        sounds->set_visible(false);
    }

    const auto histories = groupBox(messages.historiesSettings.value, 3U);
    histories->set_layout_item({4, 2, 1, 1, lib::Align::Stretch,
                                lib::Align::Center, {220, 108}, {220, 108}, {},
                                0.0F});
    body->add(histories);
    place(histories, labelControl(messages.openHistoryMax.value), 0, 0);
    place(histories, number(_histMax, 10.0, 0.0), 0, 1);
    _clearHist = pushControl(messages.openHistoryClear.value);
    _clearHist->on_click = [this,life=std::weak_ptr<int>(_skinLifetime)] { if(!life.expired())refHistories(); };
    place(histories, _clearHist, 0, 2);
    place(histories, labelControl(messages.searchHistoryMax.value), 1, 0);
    place(histories, number(_sHistMax, 50.0, 0.0), 1, 1);
    _clearSHist = pushControl(messages.searchHistoryClear.value);
    _clearSHist->on_click = [this,life=std::weak_ptr<int>(_skinLifetime)] { if(!life.expired())refSearchHistories(); };
    place(histories, _clearSHist, 1, 2);
    place(histories, labelControl(messages.executedPartiesMax.value), 2, 0);
    place(histories, number(_pHistMax, 10.0, 0.0), 2, 1);
    _clearPHist = pushControl(messages.executedPartiesClear.value);
    _clearPHist->on_click = [this,life=std::weak_ptr<int>(_skinLifetime)] { if(!life.expired())refExecutedParties(); };
    place(histories, _clearPHist, 2, 2);
    if (auto* common = _comm.editorCommons) {
        // D版の Commons.put と同様、通常のツールバー更新で
        // これらの条件を更新する。 登録先はダイアログより長く存続する
        // 場合があるため、所有者の失効後は Props を参照しない。
        const std::weak_ptr<int> life = _skinLifetime;
        auto* const prop = _prop;
        common->put(_clearHist, [life,prop] {
            return !life.expired() && prop && !prop->var().openHistories.value.empty();
        });
        common->put(_clearSHist, [life,prop] {
            return !life.expired() && prop &&
                (!prop->var().searchHistories.value.empty() ||
                 !prop->var().grepDirHistories.value.empty());
        });
        common->put(_clearPHist, [life,prop] {
            return !life.expired() && prop && !prop->var().executedParties.value.empty();
        });
    }


    const auto undo = groupBox(messages.undoMax.value, 2U);
    undo->set_layout_item({5, 2, 2, 1, lib::Align::Stretch,
                           lib::Align::Stretch, {220, 140}, {220, 140}, {},
                           0.0F});
    body->add(undo);
    const std::array undoRows{
        std::pair{messages.undoMaxMainView.value, &_undoMaxMainView},
        std::pair{messages.undoMaxEvent.value, &_undoMaxEvent},
        std::pair{messages.undoMaxReplace.value, &_undoMaxReplace},
        std::pair{messages.undoMaxEtc.value, &_undoMaxEtc},
    };
    for (std::size_t row = 0; row < undoRows.size(); ++row) {
        place(undo, labelControl(undoRows[row].first), row, 0);
        place(undo, number(*undoRows[row].second, row == 2U ? 16.0 : 1024.0, 0.0,
                           static_cast<double>(std::max(0, value.undoMaxLimit.value))),
              row, 1);
    }
    tabf.pages.push_back({messages.baseSettings.value, 0, _tabB, false});
}

void SettingsDialog::construct2(lib::TabView& tabf) {
    const bool compact_all = lib::editor_window_host_options().all_mode;
    _tabS = std::make_shared<lib::Panel>();
    auto tabLayout = std::make_shared<lib::GridLayout>();
    tabLayout->columns = 1U;
    tabLayout->padding = compact_all ? lib::Insets{6, 4, 6, 4}
                                    : lib::Insets{6, 6, 6, 6};
    tabLayout->spacing = compact_all ? 4.0F : 5.0F;
    _tabS->set_layout(tabLayout);
    _skinTypeChooser = std::make_unique<skintypesettings::SkinTypeChooser>(
        *_prop, *_tabS, 0, skintypesettings::SkinTypeChooser::CanSelect{},
        [this] {
            storeSkinControls();
            structs::SettingsWithSkinType value;
            value.bgImagesDefault =
                lib::SharedSlice<structs::BgImageS>{_bgImagesDefault};
            value.bgImageSettings =
                lib::SharedSlice<structs::BgImageSetting>{_bgImageSettings};
            value.standardSelections = lib::SharedSlice<String>{_standardSelections};
            value.standardKeyCodes = lib::SharedSlice<String>{_standardKeyCodes};
            value.keyCodesByFeatures =
                lib::SharedSlice<structs::KeyCodeByFeature>{_keyCodesByFeatures};
            value.keyCodesByMotions =
                lib::SharedSlice<structs::KeyCodeByMotion>{_keyCodesByMotions};
            value.overrideBgImages = value.overrideSelections = value.overrideKeyCodes = true;
            return value;
        },
        _prop->images().services(), dialog());
    _skinTypeChooser->beforeEdit = [this] { storeSkinControls(); };
    _skinTypeChooser->renamed = [this](lib::StringView before, lib::StringView after) {
        // 現在のプロファイルは名前が変わっただけで、編集対象は同じ。 並べ替えを
        // 通じて、未確定の背景設定フォームとローカルの取り消し履歴を保持する。
        if (_selectedSkinName == before) _selectedSkinName = String(after);
    };
    _skinTypeChooser->confirmSelection = [this](bool deleting, auto completion) {
        confirmSkinSelection(deleting, std::move(completion));
    };
    _skinTypeChooser->selectEvent.push_back([this, owner = std::weak_ptr<int>(_skinLifetime)] {
        if (owner.expired()) return;
        const std::weak_ptr<int> life = _questionLifetime;
        _skinType.selected = _skinTypeChooser->selected();
        const auto name = _skinType.selected < 0 ? String{} :
            skinSettingsDraft()[static_cast<std::size_t>(_skinType.selected)].type;
        // 選択がキャンセルされた挿入も未確定の内容には属
        // するが、 現在の未確定フォームを変更しない。
        if (name != _selectedSkinName && !selectSkinType()) return;
        if (!life.expired()) applyEnabled();
    });
    const auto& messages = _prop->requiredMsgs();
    auto separator = std::make_shared<lib::Separator>();
    separator->set_layout_item({1, 0, 1, 1, lib::Align::Stretch,
                                lib::Align::Center, {}, {0, 1}});
    _tabS->add(separator);
    const auto overrides = groupBox(messages.overrideDefaultSettings.value, 3U);
    overrides->set_layout_item({2, 0, 1, 1, lib::Align::Stretch,
                                lib::Align::Center});
    if (compact_all) {
        if (const auto layout = std::dynamic_pointer_cast<lib::GridLayout>(
                overrides->layout())) {
            layout->spacing = 4.0F;
            layout->padding.top = 14.0F;
            layout->padding.bottom = 5.0F;
        }
    }
    _tabS->add(overrides);
    _overrideBgImages = checkControl(messages.overrideBgImagesSettings.value);
    _overrideSelections = checkControl(messages.overrideSelectionsSettings.value);
    _overrideKeyCodes = checkControl(messages.overrideKeyCodesSettings.value);
    _defBgImg = pushControl(messages.setBgImagesDefault.value);
    _selections = textControl({}, true);
    _keyCodes = textControl({}, true);
    place(overrides, _overrideBgImages, 0, 0);
    place(overrides, _overrideSelections, 0, 1);
    place(overrides, _overrideKeyCodes, 0, 2);

    auto mainSplit = std::make_shared<splitpane::SplitPane>(splitpane::Horizontal);
    mainSplit->set_layout_item({3, 0, 1, 1, lib::Align::Stretch,
                                lib::Align::Stretch,
                                {700, compact_all ? 390.0F : 430.0F},
                                {700, compact_all ? 390.0F : 430.0F}, {},
                                1.0F, true});
    _tabS->add(mainSplit);
    auto left = verticalPanel(4.0F, {});
    auto right = verticalPanel(4.0F, {});
    mainSplit->add(left);
    mainSplit->add(right);
    mainSplit->setWeights({_prop->var().bgImageSelectionSashL.value,
                           _prop->var().bgImageSelectionSashR.value});
    bindSplitWeights(mainSplit, _prop->var().bgImageSelectionSashL.value,
                     _prop->var().bgImageSelectionSashR.value,
                     [this] { saveWin(); });

    const auto backgrounds = groupBox(messages.bgImagesDefault.value, 1U);
    backgrounds->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                  lib::Align::Center, {0, 50}, {0, 50}});
    left->add(backgrounds);
    place(backgrounds, _defBgImg, 0, 0);

    auto leftSplit = std::make_shared<splitpane::SplitPane>(splitpane::Vertical);
    leftSplit->set_layout_item({1, 0, 1, 1, lib::Align::Stretch,
                                lib::Align::Stretch,
                                {430, compact_all ? 340.0F : 370.0F}, {}, {}, 1.0F});
    left->add(leftSplit);
    toolspane::Commons paneCommons;
    paneCommons.services = _prop->images().services();
    bindToolsPaneUndoSettings(paneCommons, *_prop, _comm.editorCommons,
                             _editorCommonsLifetime, _skinLifetime);
    _bgStgsView = std::make_shared<toolspane::ToolsPane<structs::BgImageSetting>>(
        paneCommons, std::function<void(bool)>{}, std::function<bool()>{},
        [this] { storeSkinControls(); applyEnabled(); }, nullptr, 0,
        toolsPresentation(*_prop, _comm.menuImage, _comm.editorCommons,
                          _editorCommonsLifetime, _skinLifetime));
    leftSplit->add(_bgStgsView);
    _bgStgsView->setup(std::vector<structs::BgImageSetting>{},
                       _prop->var().bgImageSettingsSashL.value,
                       _prop->var().bgImageSettingsSashR.value);

    auto keyMapSplit = std::make_shared<splitpane::SplitPane>(splitpane::Horizontal);
    leftSplit->add(keyMapSplit);
    leftSplit->setWeights({_prop->var().bgImageSettingsKeyCodesSashL.value,
                           _prop->var().bgImageSettingsKeyCodesSashR.value});
    bindSplitWeights(leftSplit, _prop->var().bgImageSettingsKeyCodesSashL.value,
                     _prop->var().bgImageSettingsKeyCodesSashR.value,
                     [this] { saveWin(); });

    autokeycode::Commons keyCommons;
    keyCommons.services = _prop->images().services();
    // D版の EditableListView と同様、メニューのショートカット接尾辞なしのメッセージラベルを使う。
    keyCommons.listCommons.addText = messages.menuText(types::MenuID::AddItem);
    keyCommons.listCommons.delText = messages.menuText(types::MenuID::DelItem);
    // 設定の二つの一覧で、D版の汎用 EditableListView と同じツールバーの画像を使う。
    keyCommons.listCommons.addIcon = _prop->images().menu(types::MenuID::AddItem);
    keyCommons.listCommons.delIcon = _prop->images().menu(types::MenuID::DelItem);
    keyCommons.setupKeyCodeEditor = [prop=_prop, common=_comm.editorCommons](
        const std::shared_ptr<lib::TextEdit>& editor,
        const std::shared_ptr<lib::ComboBox>& choice,
        autokeycode::StandardKeyCodes values, autokeycode::CatchModifier catchMod,
        std::function<bool()> current) -> std::shared_ptr<void> {
        if (!choice || !editor || !current()) return {};
        dmenu::Commons menuComm;
        menuComm.services = prop->images().services();
        menuComm.presentation.menuText = [prop](auto id) { return prop->buildMenu(id); };
        menuComm.presentation.menuImage = [prop](auto id) { return prop->images().menu(id); };
        menuComm.undoMaxEtc = static_cast<std::size_t>(std::max(0, prop->var().undoMaxEtc.value));
        auto text = dmenu::createTextMenu(menuComm, dmenu::TMM(choice), catchMod);
        chooser::Commons chooserComm;
        chooserComm.services = menuComm.services;
        chooserComm.menuText = menuComm.presentation.menuText;
        chooserComm.menuImage = menuComm.presentation.menuImage;
        chooserComm.convertKeyCode = [prop](lib::StringView value, chooser::FKCKind kind) {
            return prop->sys().convFireKeyCode(value, kind);
        };
        chooser::ChooserProperties properties;
        properties.editor_commons = common;
        properties.messages.copyConvertedKeyCode = prop->buildMenu(types::MenuID::CopyTimingConvertedKeyCode);
        chooser::createKeyCodeTimingMenu(chooserComm, choice, text->menu(), false, true, properties);
        auto search = incsearch::attachComboSearch(menuComm.services, choice, editor,
            [values=std::move(values)](const incsearch::IncSearch& filter) {
                std::vector<lib::ChoiceItem> items;
                for (const auto& value : values())
                    if (filter.match(value)) items.push_back({value,value});
                return items;
            }, chooser::detail::searchSettings(properties), current,
            prop->images().menu(types::MenuID::IncSearch), {}, true, text->menu());
        struct Binding {
            dmenu::TextMenuModifyPtr text;
            std::shared_ptr<incsearch::IncSearch> search;
        };
        return std::make_shared<Binding>(Binding{std::move(text),std::move(search)});
    };

    keyCommons.featureColumnWidth = static_cast<float>(
        _prop->var().keyCodesByFeaturesFeatureColumn.value);
    keyCommons.featureKeyCodeColumnWidth = static_cast<float>(
        _prop->var().keyCodesByFeaturesKeyCodeColumn.value);
    keyCommons.motionColumnWidth = static_cast<float>(
        _prop->var().keyCodesByMotionsMotionColumn.value);
    keyCommons.motionElementColumnWidth = static_cast<float>(
        _prop->var().keyCodesByMotionsElementColumn.value);
    keyCommons.motionKeyCodeColumnWidth = static_cast<float>(
        _prop->var().keyCodesByMotionsKeyCodeColumn.value);
    keyCommons.storeFeatureColumn = [this](const std::size_t column,
                                            const float width) {
        if (!std::isfinite(width) || width <= 0.0F) return;
        const auto saved = static_cast<std::int32_t>(std::lround(width));
        if (column == 0U)
            _prop->var().keyCodesByFeaturesFeatureColumn.value = saved;
        else if (column == 1U)
            _prop->var().keyCodesByFeaturesKeyCodeColumn.value = saved;
        saveWin();
    };
    keyCommons.storeMotionColumn = [this](const std::size_t column,
                                           const float width) {
        if (!std::isfinite(width) || width <= 0.0F) return;
        const auto saved = static_cast<std::int32_t>(std::lround(width));
        if (column == 0U)
            _prop->var().keyCodesByMotionsMotionColumn.value = saved;
        else if (column == 1U)
            _prop->var().keyCodesByMotionsElementColumn.value = saved;
        else if (column == 2U)
            _prop->var().keyCodesByMotionsKeyCodeColumn.value = saved;
        saveWin();
    };
    keyCommons.messages.cardFeature = "カードの特徴";
    keyCommons.messages.keyCode = messages.keyCode.value;
    keyCommons.messages.noElement = messages.defaultSelection(messages.noElement.value);
    keyCommons.messages.cards = _prop->images().load(dprops::StringView{"cards.png"});
    keyCommons.messages.keyCodeImage =
        _prop->images().load(dprops::StringView{"key_code.png"});
    keyCommons.messages.motionName = [&messages](const types::MType type) {
        return messages.motionName(type);
    };
    keyCommons.messages.elementName = [&messages](const types::Element element) {
        return messages.elementName(element);
    };
    keyCommons.messages.motionIcon = [this](const types::MType type) {
        return _prop->images().motion(type);
    };
    keyCommons.messages.elementIcon = [this](const types::Element element) {
        return _prop->images().element(element);
    };
    keyCommons.refreshToolBar = [this] { applyEnabled(); };
    keyCommons.listCommons.undoMax=static_cast<std::size_t>(std::max(0,_prop->var().undoMaxEtc.value));
    keyCommons.listCommons.xmlCopyProvider=[prop=_prop] {return prop->var().xmlCopy.value;};
    keyCommons.listCommons.undoMaxProvider=[prop=_prop] {
        return static_cast<std::size_t>(std::max(0,prop->var().undoMaxEtc.value));
    };
    keyCommons.listCommons.presentation.menuText=[prop=_prop](types::MenuID id) {return prop->buildMenu(id);};
    keyCommons.listCommons.presentation.toolText=[prop=_prop](types::MenuID id) {return prop->buildTool(id);};
    keyCommons.listCommons.presentation.menuImage=_comm.menuImage;
    if(auto* common=_comm.editorCommons) {
        // D版のGCは購読先を保持する。ネイティブの設定用の子は MainWindow
        // より長く存続する場合があるため、 登録時も終了時も SettingsDialog
        // のデストラクターと同じ Commons の寿命を確認する。
        const auto commonLife = _editorCommonsLifetime;
        keyCommons.listCommons.subscribeUndoMax=[common, commonLife](lib::Action callback) -> lib::Action {
            if (commonLife.expired()) return {};
            const auto connection=common->refUndoMax.add(std::move(callback));
            return [common, commonLife, connection] {
                if (!commonLife.expired()) common->refUndoMax.remove(connection);
            };
        };
        keyCommons.listCommons.subscribeMenu=[common, commonLife](lib::Action callback) -> lib::Action {
            if (commonLife.expired()) return {};
            const auto connection=common->refMenu.add([callback=std::move(callback)](types::MenuID){callback();});
            return [common, commonLife, connection] {
                if (!commonLife.expired()) common->refMenu.remove(connection);
            };
        };
    }
    auto overrideCache=std::make_shared<dprops::ElementImageCache>();
    keyCommons.overrideImage=[prop=_prop,overrideCache](const structs::ElementOverride& value) {
        return prop->elementImage(value,*overrideCache);
    };

    const auto standardCodes = [this] {
        if (_skinType.selected < 0) return _standardKeyCodes;
        const auto index = static_cast<std::size_t>(_skinType.selected);
        return index < skinSettingsDraft().size()
                   ? dprops::arraySnapshot(skinSettingsDraft()[index].standardKeyCodes)
                   : lib::Array<String>{};
    };
    const auto currentOverrides = [this] {
        if (_skinType.selected < 0 || !_elementOverrides)
            return lib::Array<structs::ElementOverride>{};
        return _elementOverrides->elementOverrides();
    };
    const auto featureGroup = groupBox(messages.keyCodesByFeatures.value, 1U);
    const auto motionGroup = groupBox(messages.keyCodesByMotions.value, 1U);
    keyMapSplit->add(featureGroup);
    keyMapSplit->add(motionGroup);
    _keyCodesByFeaturesView = std::make_unique<autokeycode::KeyCodeByFeatureView>(
        keyCommons, featureGroup.get(), 0, standardCodes,
        [this] { return catchMod(); });
    _keyCodesByMotionsView = std::make_unique<autokeycode::KeyCodeByMotionView>(
        keyCommons, motionGroup.get(), 0, standardCodes, currentOverrides,
        [this] { return catchMod(); });
    if (_keyCodesByFeaturesView->widget())
        _keyCodesByFeaturesView->panel()->set_layout_item(
            {0, 0, 1, 1, lib::Align::Stretch, lib::Align::Stretch,
             {200, 130}, {}, {}, 1.0F});
    if (_keyCodesByMotionsView->widget())
        _keyCodesByMotionsView->panel()->set_layout_item(
            {0, 0, 1, 1, lib::Align::Stretch, lib::Align::Stretch,
             {200, 130}, {}, {}, 1.0F});
    _keyCodesByFeaturesView->addModificationListener([this] {
        storeSkinControls(); applyEnabled();
    });
    _keyCodesByMotionsView->addModificationListener([this] {
        storeSkinControls(); applyEnabled();
    });
    keyMapSplit->setWeights({_prop->var().keyCodesByFeaturesKeyCodesByMotionSashL.value,
                             _prop->var().keyCodesByFeaturesKeyCodesByMotionSashR.value});
    bindSplitWeights(
        keyMapSplit,
        _prop->var().keyCodesByFeaturesKeyCodesByMotionSashL.value,
        _prop->var().keyCodesByFeaturesKeyCodesByMotionSashR.value,
        [this] { saveWin(); });

    auto rightSplit = std::make_shared<splitpane::SplitPane>(splitpane::Vertical);
    rightSplit->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                 lib::Align::Stretch, {210, 430}, {}, {}, 1.0F});
    right->add(rightSplit);
    const auto selections = groupBox(messages.standardSelections.value, 1U);
    _selections->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                  lib::Align::Stretch,
                                  {},
                                  {static_cast<float>(std::max(0, _prop->var().selectionWidth.value)), 0},
                                  {}, 1.0F});
    _selections->tab_width = _prop->var().tabs.value;
    selections->add(_selections);
    rightSplit->add(selections);
    auto rightLower = std::make_shared<splitpane::SplitPane>(splitpane::Vertical);
    rightSplit->add(rightLower);
    const auto keyCodes = groupBox(messages.standardKeyCode.value, 1U);
    _keyCodes->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                lib::Align::Stretch,
                                {},
                                {static_cast<float>(std::max(0, _prop->var().keyCodeWidth.value)), 0},
                                {}, 1.0F});
    _keyCodes->tab_width = _prop->var().tabs.value;
    keyCodes->add(_keyCodes);
    rightLower->add(keyCodes);
    const auto elements = groupBox(messages.elementOverrides.value, 1U);
    rightLower->add(elements);
    _elementOverrides = std::make_unique<skintypesettings::ElementOverrideView>(
        *_prop, *elements, 0, _prop->images().services());
    if (_comm.editorCommons) if (const auto skin = _comm.editorCommons->skin())
        _elementOverrides->imageExtensions(skin->extImage());
    if (_elementOverrides->widget())
        _elementOverrides->widget()->set_layout_item(
            {0, 0, 1, 1, lib::Align::Stretch, lib::Align::Stretch,
             {210, 110}, {}, {}, 1.0F});
    _elementOverrides->modEvent.push_back([this] {
        storeSkinControls();
        if (_keyCodesByMotionsView) {
            // D版と同様、要素の上書き設定を更新したら、描画済みの全行を無効化する。
            if (!lib::editor_window_host_options().all_mode)
                _keyCodesByMotionsView->clearAll();
            else
                _keyCodesByMotionsView->clearSelection();
        }
        applyEnabled();
    });
    rightSplit->setWeights({_prop->var().selectionKeyCodeSashL.value,
                            _prop->var().selectionKeyCodeSashR.value});
    rightLower->setWeights({_prop->var().keyCodesElementSashL.value,
                            _prop->var().keyCodesElementSashR.value});
    bindSplitWeights(rightSplit, _prop->var().selectionKeyCodeSashL.value,
                     _prop->var().selectionKeyCodeSashR.value,
                     [this] { saveWin(); });
    bindSplitWeights(rightLower, _prop->var().keyCodesElementSashL.value,
                     _prop->var().keyCodesElementSashR.value,
                     [this] { saveWin(); });
    mod(*_overrideBgImages);
    mod(*_overrideSelections);
    mod(*_overrideKeyCodes);
    mod(*_selections);
    mod(*_keyCodes);
    const auto previousOverrideBg = _overrideBgImages->on_changed;
    _overrideBgImages->on_changed = [this, previousOverrideBg](bool checked) {
        if (previousOverrideBg) previousOverrideBg(checked);
        updateOverrideBgImages();
    };
    const auto previousOverrideSelections = _overrideSelections->on_changed;
    _overrideSelections->on_changed = [this, previousOverrideSelections](bool checked) {
        if (previousOverrideSelections) previousOverrideSelections(checked);
        updateOverrideSelections();
    };
    const auto previousOverrideKeyCodes = _overrideKeyCodes->on_changed;
    _overrideKeyCodes->on_changed = [this, previousOverrideKeyCodes](bool checked) {
        if (previousOverrideKeyCodes) previousOverrideKeyCodes(checked);
        updateOverrideKeyCodes();
    };
    _defBgImg->on_click = [this] { editDefaultBgImages(); };

    const auto& value = _prop->var();
    _bgImagesDefault = dprops::arraySnapshot(value.bgImagesDefault.value);
    _bgImageSettings = dprops::arraySnapshot(value.bgImageSettings.value);
    _standardSelections = dprops::arraySnapshot(value.standardSelections.value);
    _standardKeyCodes = dprops::arraySnapshot(value.standardKeyCodes.value);
    _keyCodesByFeatures = dprops::arraySnapshot(value.keyCodesByFeatures.value);
    _keyCodesByMotions = dprops::arraySnapshot(value.keyCodesByMotions.value);
    skinSettingsDraft() = dprops::arraySnapshot(value.settingsWithSkinTypes.value.values);
    if (!selectSkinType()) return;
    tabf.pages.push_back({messages.bgImageAndSelections.value, 0, _tabS, false});
}

void SettingsDialog::construct3(lib::TabView& tabf) {
    _tabC = std::make_shared<lib::Panel>();
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 2U;
    layout->spacing = 5.0F;
    layout->padding = {6, 6, 6, 6};
    _tabC->set_layout(layout);
    _scTempls.boxName = _prop->requiredMsgs().scenarioTemplatesTitle.value;
    _evTempls.boxName = _prop->requiredMsgs().eventTemplatesTitle.value;
    _initializers = _prop->var().contentInitializers.value;
    _initializerKeys.clear();
    _initializerKeys.push_back("Basic");
    _initializerKeys.push_back("Classic");
    for (const auto& version : versioninfo::VERSIONS) _initializerKeys.push_back(version);
    const auto undoMax = static_cast<std::size_t>(
        std::max(0, _prop->var().undoMaxEtc.value));
    _initializerUndos.clear();
    for (std::size_t index = 0; index < _initializerKeys.size(); ++index)
        _initializerUndos.push_back(std::make_unique<undo::UndoManager>(undoMax));

    toolspane::Commons comm;
    comm.services = _prop->images().services();
    bindToolsPaneUndoSettings(comm, *_prop, _comm.editorCommons,
                              _editorCommonsLifetime, _skinLifetime);
    auto templatesSplit = std::make_shared<splitpane::SplitPane>(splitpane::Vertical);
    // D版と同様、高さ指定なしの FILL_BOTH を使う。 500pxの最小高
    // さではタブがダイアログより高くなり、シナリオテンプレートのボタンが欠ける。
    templatesSplit->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                     lib::Align::Stretch, {520, 0}, {520, 500}, {},
                                     1.0F});
    _tabC->add(templatesSplit);
    _scTemplsView = std::make_shared<toolspane::ToolsPane<structs::ScTemplate>>(
        comm, std::function<void(bool)>{}, std::function<bool()>{},
        [this] { applyEnabled(); }, nullptr, 0,
        toolsPresentation(*_prop, _comm.menuImage, _comm.editorCommons,
                          _editorCommonsLifetime, _skinLifetime));
    _evTemplsView = std::make_shared<toolspane::ToolsPane<structs::EvTemplate>>(
        comm, std::function<void(bool)>{}, std::function<bool()>{},
        [this] { applyEnabled(); }, nullptr, 0,
        toolsPresentation(*_prop, _comm.menuImage, _comm.editorCommons,
                          _editorCommonsLifetime, _skinLifetime));
    templatesSplit->add(_evTemplsView);
    templatesSplit->add(_scTemplsView);
    templatesSplit->setWeights({_prop->var().templatesSashL.value,
                                _prop->var().templatesSashR.value});
    bindSplitWeights(templatesSplit, _prop->var().templatesSashL.value,
                     _prop->var().templatesSashR.value,
                     [this] { saveWin(); });
    _scTemplsView->setup(_prop->var().scenarioTemplates.value,
                         _prop->var().scenarioTemplatesSashL.value,
                         _prop->var().scenarioTemplatesSashR.value);
    _evTemplsView->setup(_prop->var().eventTemplates.value,
                         _prop->var().eventTemplatesSashL.value,
                         _prop->var().eventTemplatesSashR.value);
    _evTemplsView->setShortcutWeights(
        _prop->var().eventTemplateShortcutSashL.value,
        _prop->var().eventTemplateShortcutSashR.value);

    const auto initializer = groupBox(_prop->requiredMsgs().contentInitializer.value, 1U);
    // D版と同様、FILL_VERTICAL でタブの高さに合わせ、500pxの最小高さは設けない。
    initializer->set_layout_item({0, 1, 1, 1, lib::Align::Stretch,
                                  lib::Align::Stretch, {190, 0}, {190, 500}, {},
                                  0.0F});
    lib::Array<String> initializerNames;
    initializerNames.push_back(
        _prop->requiredMsgs().defaultSelection(_prop->requiredMsgs().baseInitializers));
    initializerNames.push_back(
        _prop->requiredMsgs().defaultSelection(_prop->requiredMsgs().classic));
    initializerNames.insert(initializerNames.end(), versioninfo::VERSION_NAMES.begin(),
                            versioninfo::VERSION_NAMES.end());
    _initializerChooser = comboControl(initializerNames);
    _initializerChooser->visible_item_count = static_cast<std::size_t>(
        std::max(1, _prop->var().comboVisibleItemCount.value));
    _initializerChooser->on_selection_changed = [this](std::optional<std::size_t> selected) {
        updateInitializers();
        _selectedInitializer = static_cast<int>(selected.value_or(0U));
        selectInitializers2();
        applyEnabled();
    };
    // grow は0にする。一列のグループなのでコンボはすでに幅を埋
    // めており、 行を伸ばすと余った高さの半分を取り、上下に空白が
    // できる。 D版と同様、下の一覧だけに余った高さを割り当てる。
    place(initializer, _initializerChooser, 0, 0, 1U, 0.0F);
    contentinitializer::ContentInitializerContext initializerContext(
        const_cast<::cwxeditor::cwx::props::CProps&>(_prop->parent()),
        _prop->var(), _prop->images().services());
    initializerContext.editor_props = _prop;
    initializerContext.editor_commons = _comm.editorCommons;
    initializerContext.dialog_owner = dialog();
    const auto initializerCommons = _comm.editorCommons;
    const auto initializerCommonsLifetime = _editorCommonsLifetime;
    initializerContext.stopOpen =
        [initializerCommons, initializerCommonsLifetime] {
            if (!initializerCommons) return false;
            return initializerCommonsLifetime.expired() ||
                   initializerCommons->stopOpen();
        };
    initializerContext.contentText = [prop=_prop](const auto& content,const auto* summary) {
        dutils::EditorContext context;
        context.prop=prop;
        context.summary=summary;
        context.services=prop->images().services();
        return dutils::contentText(context,&content,summary);
    };
    auto scriptDiagnostics=std::make_shared<std::vector<std::shared_ptr<scripterrordialog::ScriptErrorDialog>>>();
    // D版と同様、不正な初期化用CWXScriptは、元のソースとコ
    // ンパイル設定を添え、 変数入力と同じ診断ウィンドウで報告する。
    initializerContext.scriptErrorReporter =
        [prop=_prop,owner=std::weak_ptr<lib::Window>(dialog()),services=_prop->images().services(),scriptDiagnostics](
            const auto& error,const auto base,const auto& option) {
            const auto host=owner.lock();
            if(!host || !host->visible()) return;
            std::erase_if(*scriptDiagnostics,[](const auto& entry){return entry->closed();});
            auto diagnostic=std::make_shared<scripterrordialog::ScriptErrorDialog>(services,host,error,String(base),option,scripterrordialog::presentationFromProps(*prop));
            scriptDiagnostics->push_back(diagnostic);
            (void)diagnostic->open();
        };
    initializerContext.scriptVariableDialog =
        [prop=_prop,owner=dialog(),services=_prop->images().services(),scriptDiagnostics](
            const auto& summary,const auto& variables,const auto script,const auto base,const auto& option)
                ->std::optional<::cwxeditor::cwx::script::Contents> {
            smalldialogs::Callbacks callbacks;
            smalldialogs::ScriptVarSetDialog input(*prop,summary.get(),summary->useCounter(),
                owner,variables,String(script),String(base),option,std::move(callbacks),services);
            if (!input.open()) return std::nullopt;
            return input.contents();
        };
    initializerContext.contentName = [this](const types::CType type) {
        return _prop->requiredMsgs().contentName(type);
    };
    initializerContext.contentImage = _comm.contentImage;
    initializerContext.editedInitializer = _prop->requiredMsgs().editedInitializer.value;
    // D版と同様、一覧選択では共有ツールバーの操作可否だけを更新する。
    // 初期値編集を未適用の設定変更にするのは modEvent だけ。
    initializerContext.refreshToolBar = [comm = _comm.editorCommons] {
        if (comm) comm->refreshToolBar();
    };
    _initializer = std::make_unique<contentinitializer::ContentInitialValueEditor>(
        std::move(initializerContext), initializer);
    _initializer->bgImagesDefault(
        dprops::arraySnapshot(_prop->var().bgImagesDefault.value));
    _initializer->modEvent.push_back([this] { applyEnabled(); });
    if (_initializer->widget())
        _initializer->widget()->set_layout_item(
            // D版の heightHint=0 と同様、一覧はタブの高さを決めずス
            // クロールする。 共通モデルのゼロは全行の計測を意味するため、小さな明
            // 示的な高さを指定し、 行の grow で利用可能な高さまで伸ばす。
            {1, 0, 1, 1, lib::Align::Stretch, lib::Align::Stretch,
             {180, 60}, {180, 60}, {}, 1.0F});
    _tabC->add(initializer);
    selectInitializers2();
    tabf.pages.push_back({_prop->requiredMsgs().templates.value, 0, _tabC, false});
}

void SettingsDialog::construct4(lib::TabView& tabf) {
    const bool all_mode = lib::editor_window_host_options().all_mode;
    _tabT = std::make_shared<lib::Panel>();
    auto layout = std::make_shared<lib::FillLayout>();
    layout->padding = {6, 6, 6, 6};
    _tabT->set_layout(layout);
    _tools.setup(_prop->var().outerTools.value, _prop->requiredMsgs().outerToolsTitle.value);
    _cEngines.setup(_prop->var().classicEngines.value,
                    _prop->requiredMsgs().classicEnginesTitle.value);
    toolspane::Commons comm;
    comm.services = _prop->images().services();
    bindToolsPaneUndoSettings(comm, *_prop, _comm.editorCommons,
                              _editorCommonsLifetime, _skinLifetime);
    auto split = std::make_shared<splitpane::SplitPane>(splitpane::Vertical);
    _toolsView = std::make_shared<toolspane::ToolsPane<structs::OuterTool>>(
        comm, std::function<void(bool)>{}, std::function<bool()>{},
        [this] { applyEnabled(); }, nullptr, 0,
        toolsPresentation(*_prop, _comm.menuImage, _comm.editorCommons,
                          _editorCommonsLifetime, _skinLifetime));
    _cEnginesView = std::make_shared<toolspane::ToolsPane<structs::ClassicEngine>>(
        comm, std::function<void(bool)>{}, std::function<bool()>{},
        [this] { applyEnabled(); }, nullptr, 0,
        toolsPresentation(*_prop, _comm.menuImage, _comm.editorCommons,
                          _editorCommonsLifetime, _skinLifetime));
    if (all_mode) {
        _tabT->add(split);
        split->add(_toolsView);
        split->add(_cEnginesView);
    } else {
        // 単独版では外部ツールの設定を一時的に隠す。 設定
        // データとClassicエンジンの編集は維持する。
        _tabT->add(_cEnginesView);
    }
    split->setWeights({_prop->var().toolsClassicEnginesSashL.value,
                       _prop->var().toolsClassicEnginesSashR.value});
    bindSplitWeights(split, _prop->var().toolsClassicEnginesSashL.value,
                     _prop->var().toolsClassicEnginesSashR.value,
                     [this] { saveWin(); });
    _toolsView->setup(_prop->var().outerTools.value,
                      _prop->var().outerToolsSashL.value,
                      _prop->var().outerToolsSashR.value);
    _cEnginesView->setup(_prop->var().classicEngines.value,
                         _prop->var().classicEnginesSashL.value,
                         _prop->var().classicEnginesSashR.value);
    _toolsView->setShortcutWeights(_prop->var().outerToolShortcutSashL.value,
                                   _prop->var().outerToolShortcutSashR.value);
    _cEnginesView->setShortcutWeights(
        _prop->var().classicEngineShortcutSashL.value,
        _prop->var().classicEngineShortcutSashR.value);
    _cEnginesView->setNameTypeWeights(
        _prop->var().classicEnginesNameTypeSashL.value,
        _prop->var().classicEnginesNameTypeSashR.value);
    // ALL はページ全体を引き続き隠し、単独版はClassicエンジンだけを表示する。
    if (!all_mode) {
        const auto classicEnginesPage = tabf.pages.size();
        tabf.pages.push_back({_prop->requiredMsgs().classicEnginesTitle.value,
                              0, _tabT, false});
        const std::weak_ptr<int> alive = _questionLifetime;
        tabf.on_selection_changed =
            [this, alive, classicEnginesPage](std::optional<std::size_t> selected) {
                if (alive.expired() || selected == classicEnginesPage) return;
                if (_cEnginesView) _cEnginesView->closeFeatureWindow();
            };
    }
}

void SettingsDialog::construct5(lib::TabView& tabf) {
    _tabE = std::make_shared<lib::Panel>();
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 2U;
    layout->spacing = 6.0F;
    layout->padding = {6, 6, 6, 6};
    _tabE->set_layout(layout);
    const auto& value = _prop->var();
    const auto& messages = _prop->requiredMsgs();
    const lib::Array<String> targetVersions{"CardWirthPy", "1.50", "1.30", "1.29", "1.28"};
    lib::Array<String> targetVersionNames{messages.cardWirthPy.value};
    for (std::size_t index = 1; index < targetVersions.size(); ++index)
        targetVersionNames.push_back(::cwxeditor::cwx::utils::tryFormat(
            messages.cardWirthWithVersion.value, targetVersions[index]));
    _targetVersion = comboControl(targetVersionNames);
    _soundPlayType = comboControl({messages.soundPlayTypeDef.value,
                                   messages.soundPlayTypeSDL.value,
                                   messages.soundPlayTypeMCI.value,
                                   messages.soundPlayTypeApp.value});
    _soundEffectPlayType = comboControl({messages.soundPlaySameBGM.value,
                                         messages.soundPlayTypeDef.value,
                                         messages.soundPlayTypeSDL.value,
                                         messages.soundPlayTypeMCI.value,
                                         messages.soundPlayTypeApp.value});
    _dialogStatus = comboControl({messages.dialogStatusNameTop.value,
                                  messages.dialogStatusNameUnder.value,
                                  messages.dialogStatusNameUnderWithCoupon.value});
    _showStatusTime = comboControl({messages.showStatusTimeNameAlways.value,
                                    messages.showStatusTimeNameWithSkin.value,
                                    messages.showStatusTimeNameNo.value});
    _flagInitValue = comboControl({messages.flagOn.value, messages.flagOff.value});
    lib::Array<String> stepNames;
    const auto stepCount = std::max(1, _prop->parent().looks().stepMaxCount());
    stepNames.reserve(static_cast<std::size_t>(stepCount));
    for (int index = 0; index < stepCount; ++index) {
        stepNames.push_back(structs::parseDollarParams(
            value.stepValueName.value, {{'N', std::to_string(index)}}));
    }
    _stepInitValue = comboControl(stepNames);
    const auto visibleItems = static_cast<std::size_t>(
        std::max(1, value.comboVisibleItemCount.value));
    for (const auto& combo : {_targetVersion, _soundPlayType, _soundEffectPlayType,
                              _dialogStatus, _showStatusTime, _flagInitValue,
                              _stepInitValue})
        combo->visible_item_count = visibleItems;
    mod(*_targetVersion);
    mod(*_soundPlayType);
    mod(*_soundEffectPlayType);
    mod(*_dialogStatus);
    mod(*_showStatusTime);
    mod(*_flagInitValue);
    mod(*_stepInitValue);

    const auto details = groupBox(messages.etcSettingsTitle.value, 5U);
    details->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                              lib::Align::Stretch, {440, 570}, {440, 570}, {},
                              1.0F, true});
    const auto etcHost = std::make_shared<lib::Panel>();
    etcHost->set_layout(std::make_shared<lib::FillLayout>());
    place(details, etcHost, 0, 0, 5U, 1.0F);
    etcHost->set_layout_item({0, 0, 1, 5, lib::Align::Stretch,
                              lib::Align::Stretch, {420, 250}, {420, 250}, {},
                              1.0F, true});
    // D版と同様、初期設定時に実際のページの親を使って EtcSettings
    // を構築する。 ダイアログを開く前に、未接続のコントロールツリーを作らない。
    auto etc_presentation=etcPresentation(_prop->requiredMsgs());
    auto& search_settings=etc_presentation.searchSettings;
    search_settings.startIncrementalSearchWhenKeyDown=_prop->var().startIncrementalSearchWhenKeyDown.value;
    const auto* search_prop = _prop;
    search_settings.startIncrementalSearchWhenKeyDownProvider = [search_prop] {
        return search_prop && search_prop->var().startIncrementalSearchWhenKeyDown.value;
    };
    search_settings.incrementalSearchBoxWidth=_prop->var().incrementalSearchBoxWidth.value;
    search_settings.incrementalSearchType=_prop->var().incrementalSearchType.value;
    search_settings.containsLabel=_prop->requiredMsgs().incSearchContains.value;
    search_settings.wildcardLabel=_prop->requiredMsgs().incSearchWildcard.value;
    search_settings.regexLabel=_prop->requiredMsgs().incSearchRegex.value;
    search_settings.windowTitle=_prop->requiredMsgs().menuText(types::MenuID::IncSearch);
    search_settings.closeLabel=_prop->requiredMsgs().menuText(types::MenuID::CloseIncSearch);
    etc_presentation.searchShortcut=_menuProps.hotkey(types::MenuID::IncSearch);
    const auto etc_life=std::weak_ptr<void>(_skinLifetime);
    etc_presentation.searchTypeChanged=[prop=_prop,etc_life](auto mode){if(!etc_life.expired())prop->var().incrementalSearchType.value=static_cast<std::int32_t>(mode);};
    _etcSettings = std::make_unique<etcsettings::EtcSettings>(
        _prop->images().services(),etcHost,_prop->var(),std::move(etc_presentation));
    _etcSettings->modEvent.push_back([this] { applyEnabled(); });
    const auto detailSeparator = std::make_shared<lib::Separator>();
    place(details, detailSeparator, 1, 0, 5U);
    _tabE->add(details);

    const auto general = details;
    std::size_t row{2U};
    const auto addCombo = [&](String title, const Combo& combo) {
        place(general, labelControl(std::move(title)), row, 0);
        place(general, combo, row, 1, 2U, 1.0F);
        ++row;
    };
    addCombo(messages.targetVersion.value, _targetVersion);
    place(general, labelControl(messages.targetVersionHint.value), row++, 0, 3U);
    addCombo(messages.soundPlayType.value, _soundPlayType);
    _bgmVolume = numberControl(value.bgmVolume.value, 0.0, 100.0);
    mod(*_bgmVolume);
    place(general, labelControl(messages.soundVolume.value), row, 0);
    place(general, _bgmVolume, row, 1);
    place(general, labelControl(messages.soundVolumePer.value), row++, 2);
    addCombo(messages.soundEffectPlayType.value, _soundEffectPlayType);
    _seVolume = numberControl(value.seVolume.value, 0.0, 100.0);
    mod(*_seVolume);
    place(general, labelControl(messages.soundVolume.value), row, 0);
    place(general, _seVolume, row, 1);
    place(general, labelControl(messages.soundVolumePer.value), row++, 2);
    place(general, labelControl(messages.soundCaution.value), row++, 0, 3U);
    addCombo(messages.dialogStatus.value, _dialogStatus);
    addCombo(messages.showStatusTime.value, _showStatusTime);
    addCombo(messages.flagInitValue.value, _flagInitValue);
    addCombo(messages.stepInitValue.value, _stepInitValue);
    _stepValueName = textControl(value.stepValueName.value);
    mod(*_stepValueName);
    const auto previousStepNameChanged = _stepValueName->on_changed;
    _stepValueName->on_changed =
        [this, previousStepNameChanged](const std::string_view pattern) {
            if (previousStepNameChanged) previousStepNameChanged(pattern);
            if (!_stepInitValue) return;
            auto items = _stepInitValue->items;
            for (std::size_t index = 0; index < items.size(); ++index) {
                items[index].text = structs::parseDollarParams(
                    pattern, {{'N', std::to_string(index)}});
            }
            (void)lib::replace_combo_items(_stepInitValue, std::move(items),
                                           _stepInitValue->selected_index,
                                           _stepInitValue->edit_text);
        };
    place(general, labelControl(messages.stepValueName.value), row, 0);
    place(general, _stepValueName, row++, 1, 2U, 1.0F);

    _targetVersionTbl.clear();
    _targetVersionTbl2.clear();
    for (std::size_t i = 0; i < targetVersions.size(); ++i) {
        _targetVersionTbl[targetVersions[i]] = static_cast<int>(i);
        _targetVersionTbl2[static_cast<int>(i)] = targetVersions[i];
    }
    constexpr std::array soundTypes{
        ::cwxeditor::cwx::editor::gui::sound::SOUND_TYPE_AUTO,
        ::cwxeditor::cwx::editor::gui::sound::SOUND_TYPE_SDL,
        ::cwxeditor::cwx::editor::gui::sound::SOUND_TYPE_MCI,
        ::cwxeditor::cwx::editor::gui::sound::SOUND_TYPE_APP};
    for (std::size_t i = 0; i < soundTypes.size(); ++i) {
        _soundPlayTypeTbl[soundTypes[i]] = static_cast<int>(i);
        _soundPlayTypeTbl2[static_cast<int>(i)] = soundTypes[i];
        _soundEffectPlayTypeTbl[soundTypes[i]] = static_cast<int>(i + 1U);
        _soundEffectPlayTypeTbl2[static_cast<int>(i + 1U)] = soundTypes[i];
    }
    _soundEffectPlayTypeTbl[::cwxeditor::cwx::editor::gui::sound::SOUND_TYPE_SAME_BGM] = 0;
    _soundEffectPlayTypeTbl2[0] =
        ::cwxeditor::cwx::editor::gui::sound::SOUND_TYPE_SAME_BGM;
    for (int i = 0; i < 3; ++i) {
        _dialogStatusTbl[i] = i;
        _dialogStatusTbl2[i] = i;
        _showStatusTimeTbl[i] = i;
        _showStatusTimeTbl2[i] = i;
    }

    _mnemonic = customtext::mnemonicText(
        nullptr, customtext::STYLE_NONE, _prop->images().services().graphics.get());
    // アクセスキーの表示欄に共通のテキストメニューを保
    // 持し、その欄で全選択・コピーを使えるようにする。
    dmenu::Commons mnemonicMenu;
    mnemonicMenu.services = _prop->images().services();
    mnemonicMenu.presentation.menuText = [prop = _prop](const auto id) {
        return prop->buildMenu(id);
    };
    mnemonicMenu.presentation.menuImage = [prop = _prop](const auto id) {
        return prop->images().menu(id);
    };
    mnemonicMenu.presentation.textTabs = static_cast<std::size_t>(
        std::max(0, _prop->var().textTabs.value));
    mnemonicMenu.undoMaxEtc = static_cast<std::size_t>(
        std::max(0, _prop->var().undoMaxEtc.value));
    _mnemonicTextMenu = dmenu::createTextMenu(
        mnemonicMenu, dmenu::TMM(_mnemonic));
    _hotkey = std::make_shared<customtext::HotKeyField>(
        nullptr, customtext::STYLE_NONE, _prop->images().services().graphics.get());
    _hotkeyWidget = _hotkey->widget();
    _menu = std::make_shared<lib::TableView>();
    _menu->columns = {{"", 1.0F, lib::TextAlignment::Start, true, false}};
    _menu->header_visible = false;
    _menuApply = pushControl(messages.apply.value);
    _menuDel = pushControl(messages.del.value);
    const auto rightSplit = std::make_shared<splitpane::SplitPane>(splitpane::Vertical);
    rightSplit->set_layout_item({0, 1, 1, 1, lib::Align::Stretch,
                                 lib::Align::Stretch, {300, 570}, {300, 570}, {},
                                 1.0F, true});
    _tabE->add(rightSplit);
    const auto keys = groupBox(messages.keyBind.value, 4U);
    place(keys, labelControl(messages.mnemonic.value), 0, 0);
    place(keys, _mnemonic, 0, 1, 1U, 1.0F);
    place(keys, _menuApply, 0, 2);
    place(keys, _menuDel, 0, 3);
    place(keys, labelControl(messages.hotkey.value), 1, 0);
    place(keys, _hotkeyWidget, 1, 1, 3U, 1.0F);
    _menu->set_layout_item({2, 0, 1, 4, lib::Align::Stretch,
                            lib::Align::Stretch,
                            {},
                            {0, static_cast<float>(std::max(0, value.menuSettingsHeight.value))},
                            {}, 1.0F});
    keys->add(_menu);
    rightSplit->add(keys);
    // D版と同様、ここでは ModMenu だけを付ける。 欄の未確定の編集だけで、外側の設定全体を変更済みにしない。
    _menu->on_selection_changed = [this](const std::vector<std::size_t>&) {
        selectMenu();
    };
    const auto previousMnemonicChanged = _mnemonic->on_changed;
    _mnemonic->on_changed = [this, previousMnemonicChanged](std::string_view value) {
        if (previousMnemonicChanged) previousMnemonicChanged(value);
        _modmenu.modifyText();
    };
    const auto previousHotkeyChanged = _hotkeyWidget->on_changed;
    _hotkeyWidget->on_changed = [this, previousHotkeyChanged](std::string_view value) {
        if (previousHotkeyChanged) previousHotkeyChanged(value);
        _modmenu.modifyText();
    };
    _menuApply->on_click = [this] { applyMenu(); };
    _menuDel->on_click = [this] { _delmenuaccel.widgetSelected(); };

    incsearch::IncSearchSettings searchSettings;
    searchSettings.startIncrementalSearchWhenKeyDown =
        value.startIncrementalSearchWhenKeyDown.value;
    const auto* menu_search_prop = _prop;
    searchSettings.startIncrementalSearchWhenKeyDownProvider = [menu_search_prop] {
        return menu_search_prop && menu_search_prop->var().startIncrementalSearchWhenKeyDown.value;
    };
    searchSettings.incrementalSearchBoxWidth = value.incrementalSearchBoxWidth.value;
    searchSettings.incrementalSearchType = value.incrementalSearchType.value;
    searchSettings.containsLabel = messages.incSearchContains.value;
    searchSettings.wildcardLabel = messages.incSearchWildcard.value;
    searchSettings.regexLabel = messages.incSearchRegex.value;
    _menuIncSearch = std::make_unique<incsearch::IncSearch>(
        _prop->images().services(), keys.get(),
        [this] { return _menu && _menu->enabled(); },
        std::vector<incsearch::AdditionMatcher>{}, std::move(searchSettings));
    _menuIncSearch->modEvent.push_back([this] { refreshMenu(); });
    const std::weak_ptr<int> menu_search_lifetime = _menuSearchLifetime;
    keys->on_descendant_key_down = [this, menu_search_lifetime](lib::KeyEvent& event) {
        if (menu_search_lifetime.expired() || event.handled || !_menuIncSearch || !_prop) return;
        const auto gui = _prop->images().services().gui;
        const auto focused = gui ? gui->focused_widget() : lib::WidgetPtr{};
        // 表には上でネイティブ入力の直接経路を設定済みなので、
        // その対象に IncSearch を二重に呼ばない。
        if (!focused || focused.get() == _menu.get()) return;
        // D版の表示フィルターに合わせつつ、フォーカスのあるコントロールのキーを
        // 奪わない。 特にボタンのSpace/Enterは共通の有効化経路へ渡
        // す。 IncSearch 自体が TextEdit/NumberEdit
        // と編集可能な Combo を除外し、 ネイティブの確定文字入力の
        // 対象は、この Composite 配下の登録済み項目ビューに限る。
        (void)_menuIncSearch->handleHostKeyDown(event, focused.get());
    };
    _menu->on_key_down = [this](lib::KeyEvent& event) {
        if (_menuIncSearch && _menuIncSearch->handleHostKeyDown(event, _menu.get()))
            event.handled = true;
    };
    _menu->on_text_input = [this](lib::TextInputEvent& event) {
        if (_menuIncSearch)
            event.handled = _menuIncSearch->handleHostTextInput(event, _menu.get());
    };
    _menuPopup = std::make_shared<lib::Menu>(true);
    lib::MenuItem incrementalSearchItem;
    incrementalSearchItem.text = messages.menuText(types::MenuID::IncSearch);
    incrementalSearchItem.command = "incremental-search";
    incrementalSearchItem.icon = _comm.menuImage
                                     ? _comm.menuImage(types::MenuID::IncSearch)
                                     : _prop->images().load(
                                           dprops::StringView{"inc_search.png"});
    _menuPopup->items.push_back(std::move(incrementalSearchItem));
    _menuPopup->on_command = [this](const std::string_view command) {
        if (command == "incremental-search") menuIncSearch();
    };
    _menu->on_pointer = [this](lib::PointerEvent& event) {
        const bool popup =
            (event.action == lib::PointerAction::Up &&
             event.button == lib::PointerButton::Secondary) ||
            event.action == lib::PointerAction::LongPress;
        if (!popup || !_menuPopup || !_prop->images().services().gui) return;
        _prop->images().services().gui->show_context_menu(
            _menuPopup, _menu, event.position);
        event.handled = true;
    };
    const auto ignored = groupBox(messages.ignorePaths.value, 1U);
    _ignorePaths = textControl({}, true);
    _ignorePaths->set_layout_item({0, 0, 1, 1, lib::Align::Stretch,
                                   lib::Align::Stretch,
                                   {},
                                   {static_cast<float>(std::max(0, value.ignorePathsWidth.value)), 0},
                                   {}, 1.0F});
    _ignorePaths->tab_width = value.tabs.value;
    ignored->add(_ignorePaths);
    mod(*_ignorePaths);
    rightSplit->add(ignored);
    rightSplit->setWeights({_prop->var().ignoreMenuSashL.value,
                            _prop->var().ignoreMenuSashR.value});
    bindSplitWeights(rightSplit, _prop->var().ignoreMenuSashL.value,
                     _prop->var().ignoreMenuSashR.value,
                     [this] { saveWin(); });
    setNumber(_bgmVolume, value.bgmVolume.value);
    setNumber(_seVolume, value.seVolume.value);
    tabf.pages.push_back({messages.etcSettings.value, 0, _tabE, false});
}

void SettingsDialog::setComboItems(const Combo& combo,
                                    const lib::Array<String>& values) {
    if (!combo) return;
    std::vector<lib::ChoiceItem> items;
    items.reserve(values.size());
    for (const auto& value : values) items.push_back({value, {}, 0, true});
    const auto selected = items.empty() ? std::optional<std::size_t>{}
                                        : std::optional<std::size_t>{0U};
    (void)lib::replace_combo_items(combo, std::move(items), selected,
                                   combo->edit_text);
}

int SettingsDialog::comboIndex(const Combo& combo, int fallback) const {
    if (!combo || !combo->selected_index) return fallback;
    return static_cast<int>(*combo->selected_index);
}

String SettingsDialog::comboValue(const Combo& combo, String fallback) const {
    if (!combo || !combo->selected_index || *combo->selected_index >= combo->items.size()) {
        return fallback;
    }
    return combo->items[*combo->selected_index].value.empty()
               ? combo->items[*combo->selected_index].text
               : combo->items[*combo->selected_index].value;
}

void SettingsDialog::syncControlsFromProperties() {
    if (!_prop) return;
    const std::weak_ptr<int> life = _questionLifetime;
    struct Loading { bool& value; std::weak_ptr<int> life;
        ~Loading() { if (!life.expired()) value = false; }
    } loading{_loadingSkinControls, life};
    _loadingSkinControls = true;
    const auto& value = _prop->var();
    if (_enginePath) _enginePath->text = value.enginePath.value;
    setChecked(_findEnginePath, value.findEnginePath.value);
    if (_tempDir) _tempDir->text = value.tempPath.value;
    if (_backupDir) _backupDir->text = value.backupPath.value;
    setChecked(_backupEnabled, value.backupEnabled.value);
    setChecked(_backupIntervalTypeTime, value.backupIntervalType.value != static_cast<int>(BackupType::Edit));
    setChecked(_backupIntervalTypeEdit, value.backupIntervalType.value == static_cast<int>(BackupType::Edit));
    setNumber(_backupIntervalTime, value.backupInterval.value);
    setNumber(_backupIntervalEdit, value.backupIntervalEdit.value);
    setNumber(_backupCount, value.backupCount.value);
    setChecked(_backupRefAuthor, value.backupRefAuthor.value);
    setChecked(_autoSave, value.autoSave.value);
    setChecked(_backupArchived, value.backupArchived.value);
    if (_backupBeforeSaveDir) _backupBeforeSaveDir->text = value.backupBeforeSavePath.value;
    setChecked(_backupBeforeSaveEnabled, value.backupBeforeSaveEnabled.value);
    if (_author) _author->text = value.defaultAuthor.value;
    if (_newAreaName) _newAreaName->text = value.newAreaName.value;
    if (_wallpaper) _wallpaper->text = value.wallpaper.value;
    if (_wallpaperStyle) {
        const auto found = _wallpaperStyleTbl.find(value.wallpaperStyle.value);
        (void)publishComboSelection(
            _wallpaperStyle, found == _wallpaperStyleTbl.end()
                                 ? std::optional<std::size_t>{1U}
                                 : std::optional<std::size_t>{
                                       static_cast<std::size_t>(found->second)});
    }
    setNumber(_histMax, value.historyMax.value);
    setNumber(_sHistMax, value.searchHistoryMax.value);
    setNumber(_pHistMax, value.executedPartiesMax.value);
    setNumber(_undoMaxMainView, value.undoMaxMainView.value);
    setNumber(_undoMaxEvent, value.undoMaxEvent.value);
    setNumber(_undoMaxReplace, value.undoMaxReplace.value);
    setNumber(_undoMaxEtc, value.undoMaxEtc.value);
    if (_ignorePaths) {
        _ignorePaths->text.clear();
        for (const auto& path : value.ignorePaths.value) {
            _ignorePaths->text += path;
            _ignorePaths->text.push_back('\n');
        }
    }
    if (_drawingScale) {
        const auto percent = variables::previewScalePercent(
            value.drawingScalePercent.value, value.drawingScale.value);
        const auto index = static_cast<std::size_t>((percent - 100U + 12U) / 25U);
        (void)publishComboSelection(_drawingScale,
            std::min(index, _drawingScale->items.size() - 1U));
    }
    if (_imageScale) {
        const auto percent = variables::previewScalePercent(
            value.imageScalePercent.value, value.imageScale.value);
        const auto index = static_cast<std::size_t>((percent - 100U + 12U) / 25U);
        (void)publishComboSelection(_imageScale,
            std::min(index, _imageScale->items.size() - 1U));
    }
    if (_uiTextScale) {
        const auto percent = lib::clamp_editor_ui_text_scale_percent(value.uiTextScale.value);
        const auto index = static_cast<std::size_t>((percent - 100U + 12U) / 25U);
        (void)publishComboSelection(
            _uiTextScale, std::min(index, _uiTextScale->items.size() - 1U));
    }
    if (_language) {
        std::optional<std::size_t> selected{0U};
        if (!value.useSystemLanguage.value) {
            for (const auto& [index, locale] : _msgsTableIndex) {
                const auto file = _msgsTableFile.find(locale);
                if (file != _msgsTableFile.end() && file->second == value.languageFile.value) {
                    selected = static_cast<std::size_t>(index);
                    break;
                }
            }
        }
        (void)publishComboSelection(_language, selected);
    }
    if (_targetVersion) {
        const auto found = _targetVersionTbl.find(value.targetVersion.value);
        (void)publishComboSelection(
            _targetVersion, found == _targetVersionTbl.end()
                                ? std::optional<std::size_t>{0U}
                                : std::optional<std::size_t>{
                                      static_cast<std::size_t>(found->second)});
    }
    if (_soundPlayType) {
        const auto found = _soundPlayTypeTbl.find(value.soundPlayType.value);
        (void)publishComboSelection(
            _soundPlayType, static_cast<std::size_t>(
                                found == _soundPlayTypeTbl.end() ? 0 : found->second));
    }
    if (_soundEffectPlayType) {
        const auto found = _soundEffectPlayTypeTbl.find(value.soundEffectPlayType.value);
        (void)publishComboSelection(
            _soundEffectPlayType, static_cast<std::size_t>(
                                      found == _soundEffectPlayTypeTbl.end()
                                          ? 0 : found->second));
    }
    setNumber(_bgmVolume, value.bgmVolume.value);
    setNumber(_seVolume, value.seVolume.value);
    (void)publishComboSelection(
        _dialogStatus, static_cast<std::size_t>(std::clamp(value.dialogStatus.value, 0, 2)));
    (void)publishComboSelection(
        _showStatusTime, static_cast<std::size_t>(std::clamp(value.showStatusTime.value, 0, 2)));
    if (_savedSound) _savedSound->text = value.savedSound.value;
    (void)publishComboSelection(_flagInitValue,
                                value.flagInitValue.value ? 0U : 1U);
    const auto stepCount = std::max(1, _prop->parent().looks().stepMaxCount());
    (void)publishComboSelection(_stepInitValue, static_cast<std::size_t>(
        std::clamp(value.stepInitValue.value, 0, stepCount - 1)));
    if (_stepValueName) _stepValueName->text = value.stepValueName.value;
    _bgImagesDefault = dprops::arraySnapshot(value.bgImagesDefault.value);
    _bgImageSettings = dprops::arraySnapshot(value.bgImageSettings.value);
    _standardSelections = dprops::arraySnapshot(value.standardSelections.value);
    _standardKeyCodes = dprops::arraySnapshot(value.standardKeyCodes.value);
    _keyCodesByFeatures = dprops::arraySnapshot(value.keyCodesByFeatures.value);
    _keyCodesByMotions = dprops::arraySnapshot(value.keyCodesByMotions.value);
    skinSettingsDraft() = dprops::arraySnapshot(value.settingsWithSkinTypes.value.values);
    if (_skinTypeChooser) {
        _skinTypeChooser->settingsWithSkinTypes() = skinSettingsDraft();
        _skinTypeChooser->refresh();
        _skinTypeChooser->select(0, false);
        _skinType.selected = _skinTypeChooser->selected();
    }
    if (_toolsView) _toolsView->setArray(value.outerTools.value);
    if (_cEnginesView) _cEnginesView->setArray(value.classicEngines.value);
    if (_scTemplsView) _scTemplsView->setArray(value.scenarioTemplates.value);
    if (_evTemplsView) _evTemplsView->setArray(value.eventTemplates.value);
    if (!selectSkinType() || life.expired()) return;
    refreshEnabled();
}

void SettingsDialog::updateOverride(bool bgImages, bool selections, bool keyCodes) {
    const bool defaultSkin = _skinType.selected < 0;
    if (_skinType.selected >= 0 && static_cast<std::size_t>(_skinType.selected) <
                                      skinSettingsDraft().size()) {
        auto& setting = skinSettingsDraft()[static_cast<std::size_t>(_skinType.selected)];
        setting.overrideBgImages = bgImages;
        setting.overrideSelections = selections;
        setting.overrideKeyCodes = keyCodes;
    }
    _bgStgs.enabled = defaultSkin || bgImages;
    if (_bgStgsView) _bgStgsView->enabled(defaultSkin || bgImages);
    if (_elementOverrides) _elementOverrides->enabled(defaultSkin || keyCodes);
    if (_selections) _selections->set_enabled(defaultSkin || selections);
    if (_keyCodes) _keyCodes->set_enabled(defaultSkin || keyCodes);
    if (_keyCodesByFeaturesView) _keyCodesByFeaturesView->enabled(defaultSkin || keyCodes);
    if (_keyCodesByMotionsView) _keyCodesByMotionsView->enabled(defaultSkin || keyCodes);
}

lib::Array<structs::SettingsWithSkinType>& SettingsDialog::skinSettingsDraft() {
    return _skinTypeChooser ? _skinTypeChooser->settingsWithSkinTypes()
                            : _skinType.settingsWithSkinTypes;
}

void SettingsDialog::confirmSkinSelection(const bool deleting,
    std::function<void(bool)> completion) {
    if (_loadingSkinControls || deleting || !_bgStgsView || !_bgStgsView->noApply()) {
        storeSkinControls();
        completion(true);
        return;
    }
    lib::MessageDialogRequest request;
    request.title = _prop->requiredMsgs().dlgTitQuestion.value;
    request.message = ::cwxeditor::cwx::utils::tryFormat(
        _prop->requiredMsgs().dlgMsgForceApplySingle.value, _prop->requiredMsgs().bgImageSettings.value);
    request.severity = lib::MessageSeverity::Question;
    request.buttons = lib::MessageButtons::YesNoCancel;
    const std::weak_ptr<int> alive = _questionLifetime;
    auto finish = [this, alive, completion = std::move(completion)](lib::MessageResult answer) {
        if (alive.expired()) return;
        if (!dialog() || !dialog()->visible()) { completion(false); return; }
        if (answer != lib::MessageResult::Yes && answer != lib::MessageResult::No) {
            completion(false);
            return;
        }
        if (answer == lib::MessageResult::Yes) _bgStgsView->forceApply();
        storeSkinControls();
        completion(true);
    };
    const auto backend = _prop->images().services().dialogs;
    if (!backend) { finish(lib::MessageResult::Cancel); return; }
    if (backend->supports_message_async()) {
        if (!backend->message_async(request, finish)) finish(lib::MessageResult::Cancel);
    } else finish(backend->message(request));
}

bool SettingsDialog::applyUncommittedForms() {
    String names;
    const auto append = [&names](const auto& pane, const String& name) {
        if (pane && pane->noApply()) names += name + "\n";
    };
    append(_bgStgsView, _prop->requiredMsgs().bgImageSettings.value);
    append(_toolsView, _prop->requiredMsgs().outerToolsTitle.value);
    append(_cEnginesView, _prop->requiredMsgs().classicEnginesTitle.value);
    append(_scTemplsView, _prop->requiredMsgs().scenarioTemplatesTitle.value);
    append(_evTemplsView, _prop->requiredMsgs().eventTemplatesTitle.value);
    if (names.empty()) return true;
    if (_applyQuestionPending) return false;
    auto answer = std::exchange(_applyQuestionResult, {});
    if (!answer) {
        const auto backend = _prop->images().services().dialogs;
        if (!backend) return false;
        lib::MessageDialogRequest request;
        request.title = _prop->requiredMsgs().dlgTitQuestion.value;
        request.message = ::cwxeditor::cwx::utils::tryFormat(_prop->requiredMsgs().dlgMsgForceApply.value, names);
        request.severity = lib::MessageSeverity::Question;
        request.buttons = lib::MessageButtons::YesNoCancel;
        if (backend->supports_message_async()) {
            _applyQuestionPending = true;
            const std::weak_ptr<int> alive = _questionLifetime;
            const bool keepOpen = forceApplying();
            // SettingsDialog は常に通常のOKボタンを先頭に持つ。
            // その操作を再実行して、確定して閉じる意図を復元する。 close()
            // だけでは、保留した初回の false の結果を引き継いでしまう。
            const std::weak_ptr<lib::Button> acceptButton =
                buttons().empty() ? std::shared_ptr<lib::Button>{} : buttons().front();
            struct Delivery { bool returned{}; std::optional<lib::MessageResult> immediate; };
            const auto delivery = std::make_shared<Delivery>();
            const bool started = backend->message_async(request,
                [this, alive, keepOpen, delivery, acceptButton](lib::MessageResult result) {
                    if (alive.expired()) return;
                    if (!delivery->returned) { delivery->immediate = result; return; }
                    _applyQuestionPending = false;
                    if (!dialog() || !dialog()->visible()) return;
                    if (result != lib::MessageResult::Yes && result != lib::MessageResult::No) return;
                    _applyQuestionResult = result;
                    if (keepOpen) forceApply();
                    else if (const auto accept = acceptButton.lock(); accept && accept->on_click)
                        accept->on_click();
                });
            delivery->returned = true;
            if (delivery->immediate) {
                _applyQuestionPending = false;
                answer = delivery->immediate;
            } else {
                if (!started) _applyQuestionPending = false;
                return false;
            }
        } else answer = backend->message(request);
    }
    if (*answer == lib::MessageResult::No) return true;
    if (*answer != lib::MessageResult::Yes) return false;
    if (_bgStgsView) _bgStgsView->forceApply();
    if (_toolsView) _toolsView->forceApply();
    if (_cEnginesView) _cEnginesView->forceApply();
    if (_scTemplsView) _scTemplsView->forceApply();
    if (_evTemplsView) _evTemplsView->forceApply();
    return true;
}

void SettingsDialog::storeSkinControls() {
    if (_loadingSkinControls) return;
    const auto backgrounds = _bgStgsView ? _bgStgsView->array() : _bgStgs.array;
    const auto selections = splitLines(_selections ? _selections->text : String{});
    const auto keyCodes = splitLines(_keyCodes ? _keyCodes->text : String{});
    const auto features = _keyCodesByFeaturesView
                              ? _keyCodesByFeaturesView->values()
                              : _keyCodesByFeatures;
    const auto motions = _keyCodesByMotionsView
                             ? _keyCodesByMotionsView->values()
                             : _keyCodesByMotions;
    const auto elements = _elementOverrides
                              ? _elementOverrides->elementOverrides()
                              : lib::Array<structs::ElementOverride>{};
    if (_skinType.selected < 0 || static_cast<std::size_t>(_skinType.selected) >=
                                     skinSettingsDraft().size()) {
        _bgImageSettings = backgrounds;
        _standardSelections = selections;
        _standardKeyCodes = keyCodes;
        _keyCodesByFeatures = features;
        _keyCodesByMotions = motions;
        return;
    }
    auto& setting = skinSettingsDraft()[static_cast<std::size_t>(_skinType.selected)];
    setting.bgImageSettings = lib::SharedSlice<structs::BgImageSetting>{backgrounds};
    setting.standardSelections = lib::SharedSlice<String>{selections};
    setting.standardKeyCodes = lib::SharedSlice<String>{keyCodes};
    setting.keyCodesByFeatures = lib::SharedSlice<structs::KeyCodeByFeature>{features};
    setting.keyCodesByMotions = lib::SharedSlice<structs::KeyCodeByMotion>{motions};
    setting.elementOverrides = lib::SharedSlice<structs::ElementOverride>{elements};
}

bool SettingsDialog::selectSkinType() {
    const std::weak_ptr<int> life = _questionLifetime;
    const auto generation = ++_skinSelectionGeneration;
    // D版と同様、スキンを切り替える前にモードレスの標準背景エディタをキ
    // ャンセルする。 未確定の内容を、新しいインデックスへ適用しない。
    if (_defBgImgDlg) _defBgImgDlg->forceCancel();
    // 終了処理が同期的にこの所有者を無効化したり、別のスキンを選択したりする場合がある。
    if (life.expired() || generation != _skinSelectionGeneration) return false;
    struct Loading {
        bool& flag;
        bool previous;
        explicit Loading(bool& flag) : flag(flag), previous(flag) { flag = true; }
        ~Loading() { flag = previous; }
    } loading(_loadingSkinControls);
    _selectedSkinName = _skinType.selected < 0 ||
        static_cast<std::size_t>(_skinType.selected) >= skinSettingsDraft().size()
        ? String{} : skinSettingsDraft()[static_cast<std::size_t>(_skinType.selected)].type;
    if (_skinType.selected < 0 || static_cast<std::size_t>(_skinType.selected) >=
                                     skinSettingsDraft().size()) {
        _skinType.selected = -1;
        _bgStgs.setup(_bgImageSettings, "Background images");
        if (_bgStgsView) _bgStgsView->setArray(_bgImageSettings);
        if (_selections) _selections->text = [&] {
            String result;
            for (const auto& item : _standardSelections) result += item + "\n";
            return result;
        }();
        if (_keyCodes) _keyCodes->text = [&] {
            String result;
            for (const auto& item : _standardKeyCodes) result += item + "\n";
            return result;
        }();
        setChecked(_overrideBgImages, false);
        setChecked(_overrideSelections, false);
        setChecked(_overrideKeyCodes, false);
        if (_keyCodesByFeaturesView) _keyCodesByFeaturesView->values(_keyCodesByFeatures);
        if (_keyCodesByMotionsView) _keyCodesByMotionsView->values(_keyCodesByMotions);
        if (_elementOverrides) _elementOverrides->elementOverrides({});
        updateOverride(true, true, true);
        return true;
    }
    auto& setting = skinSettingsDraft()[static_cast<std::size_t>(_skinType.selected)];
    _bgStgs.setup(dprops::arraySnapshot(setting.bgImageSettings), "Background images");
    if (_bgStgsView) _bgStgsView->setArray(dprops::arraySnapshot(setting.bgImageSettings));
    String selections;
    for (const auto& item : setting.standardSelections) selections += item + "\n";
    String keyCodes;
    for (const auto& item : setting.standardKeyCodes) keyCodes += item + "\n";
    if (_selections) _selections->text = std::move(selections);
    if (_keyCodes) _keyCodes->text = std::move(keyCodes);
    setChecked(_overrideBgImages, setting.overrideBgImages);
    setChecked(_overrideSelections, setting.overrideSelections);
    setChecked(_overrideKeyCodes, setting.overrideKeyCodes);
    if (_keyCodesByFeaturesView)
        _keyCodesByFeaturesView->values(dprops::arraySnapshot(setting.keyCodesByFeatures));
    // 単独版の行ラベルは、その提供側が最新になってから生成する。
    const bool all_mode = lib::editor_window_host_options().all_mode;
    if (_elementOverrides && !all_mode)
        _elementOverrides->elementOverrides(dprops::arraySnapshot(setting.elementOverrides));
    if (_keyCodesByMotionsView)
        _keyCodesByMotionsView->values(dprops::arraySnapshot(setting.keyCodesByMotions));
    if (_elementOverrides && all_mode)
        _elementOverrides->elementOverrides(dprops::arraySnapshot(setting.elementOverrides));
    updateOverride(setting.overrideBgImages, setting.overrideSelections,
                   setting.overrideKeyCodes);
    return true;
}

void SettingsDialog::updateOverrideBgImages() {
    const bool checked = _overrideBgImages && _overrideBgImages->checked;
    const bool selections = _overrideSelections && _overrideSelections->checked;
    const bool keyCodes = _overrideKeyCodes && _overrideKeyCodes->checked;
    updateOverride(checked, selections, keyCodes);
}

void SettingsDialog::editDefaultBgImages() {
    if (!_prop) return;
    String type = _prop->var().defaultSkin.value;
    String name = _prop->var().defaultSkinName.value;
    if (_skinType.selected >= 0 &&
        static_cast<std::size_t>(_skinType.selected) < skinSettingsDraft().size()) {
        type = skinSettingsDraft()[static_cast<std::size_t>(_skinType.selected)].type;
        name.clear();
    }
    const auto skin = ::cwxeditor::cwx::editor::gui::dwt::dskin::findSkin2(
        *_prop, type, name);
    if (!skin) return;
    const auto selected = _skinType.selected >= 0 &&
                          static_cast<std::size_t>(_skinType.selected) <
                              skinSettingsDraft().size();
    const auto backgrounds = selected
                                 ? dprops::arraySnapshot(skinSettingsDraft()[
                                       static_cast<std::size_t>(_skinType.selected)]
                                       .bgImagesDefault)
                                 : _bgImagesDefault;
    std::function<lib::Action(lib::Action)> subscribeUndoMax;
    if (auto* common = _comm.editorCommons) {
        const auto commonLife = _editorCommonsLifetime;
        subscribeUndoMax = [common, commonLife](lib::Action callback) -> lib::Action {
            if (commonLife.expired() || !callback) return {};
            const auto connection = common->refUndoMax.add(
                [callback = std::move(callback), commonLife] {
                    if (!commonLife.expired()) callback();
                });
            return [common, commonLife, connection] {
                if (!commonLife.expired()) common->refUndoMax.remove(connection);
            };
        };
    }
    _defBgImgDlg = std::make_unique<skintypesettings::DefBgImgDialog>(
        *_prop, dialog(), backgrounds, *skin,
        _prop->images().services(), std::move(subscribeUndoMax));
    _defBgImgDlg->appliedEvent.push_back([this, selected] {
        if (!_defBgImgDlg) return;
        if (selected && _skinType.selected >= 0 &&
            static_cast<std::size_t>(_skinType.selected) <
                skinSettingsDraft().size()) {
            skinSettingsDraft()[
                static_cast<std::size_t>(_skinType.selected)]
                .bgImagesDefault = lib::SharedSlice<structs::BgImageS>{
                    _defBgImgDlg->backs()};
        } else {
            _bgImagesDefault = _defBgImgDlg->backs();
            if (_initializer) _initializer->bgImagesDefault(_bgImagesDefault);
        }
        applyEnabled();
    });
    static_cast<void>(_defBgImgDlg->open());
}

void SettingsDialog::updateOverrideSelections() {
    const bool bgImages = _overrideBgImages && _overrideBgImages->checked;
    const bool checked = _overrideSelections && _overrideSelections->checked;
    const bool keyCodes = _overrideKeyCodes && _overrideKeyCodes->checked;
    updateOverride(bgImages, checked, keyCodes);
}

void SettingsDialog::updateOverrideKeyCodes() {
    const bool bgImages = _overrideBgImages && _overrideBgImages->checked;
    const bool selections = _overrideSelections && _overrideSelections->checked;
    const bool checked = _overrideKeyCodes && _overrideKeyCodes->checked;
    updateOverride(bgImages, selections, checked);
}

void SettingsDialog::selectInitializers() {
    updateInitializers();
    _selectedInitializer = static_cast<int>(
        _initializerChooser && _initializerChooser->selected_index
            ? *_initializerChooser->selected_index
            : 0U);
    selectInitializers2();
}

void SettingsDialog::updateInitializers() {
    if (!_initializer || _initializerKeys.empty()) return;
    _selectedInitializer = std::clamp(
        _selectedInitializer, 0, static_cast<int>(_initializerKeys.size() - 1U));
    const auto& key = _initializerKeys[static_cast<std::size_t>(_selectedInitializer)];
    for (auto& initializer : _initializers) {
        if (initializer.dataVersion != key) continue;
        initializer.initializer = _initializer->getInitializer();
        return;
    }
    _initializers.push_back({key, _initializer->getInitializer()});
}

void SettingsDialog::selectInitializers2() {
    if (!_initializer || _initializerKeys.empty() || _initializerUndos.empty()) return;
    _selectedInitializer = std::clamp(
        _selectedInitializer, 0, static_cast<int>(_initializerKeys.size() - 1U));
    const auto index = static_cast<std::size_t>(_selectedInitializer);
    const auto& key = _initializerKeys[index];
    auto* undo = _initializerUndos[index].get();
    contentinitializer::ContentInitialValueEditor::Initializer empty;
    const auto found = std::find_if(
        _initializers.begin(), _initializers.end(),
        [&key](const variables::ContentInitializer& value) {
            return value.dataVersion == key;
        });
    const auto& values = found == _initializers.end() ? empty : found->initializer;
    if (key == "Basic") {
        _initializer->setInitializer(values, false,
                                     String(versioninfo::LATEST_VERSION), undo);
    } else if (key == "Classic") {
        _initializer->setInitializer(values, true, {}, undo);
    } else {
        _initializer->setInitializer(values, false, key, undo);
    }
}

void SettingsDialog::menuIncSearch() {
    if (_menuIncSearch) _menuIncSearch->startIncSearch();
}

String SettingsDialog::dropEngine(const lib::Array<String>& files) const {
    if (lib::editor_window_host_options().all_mode) return {};
    if (files.empty()) return {};
    const String file = files.front();
    const String pattern = _prop ? _prop->var().engine.value : String{};
    return _prop && ::cwxeditor::cwx::utils::cfnmatch(basename(file), pattern) ? file : String{};
}

String SettingsDialog::dropSysSound(const lib::Array<String>& files) const {
    if (files.empty()) return {};
    static const lib::Array<String> extensions{".wav", ".wave", ".mid", ".midi", ".mp3", ".ogg"};
    for (const auto& file : files) if (containsExtension(file, extensions)) return file;
    return {};
}

String SettingsDialog::dropWallpaper(const lib::Array<String>& files) const {
    if (files.empty()) return {};
    static const lib::Array<String> extensions{".bmp", ".png", ".jpg", ".jpeg", ".gif"};
    for (const auto& file : files) if (containsExtension(file, extensions)) return file;
    return {};
}

String SettingsDialog::dropEnginePath(const lib::Array<String>& files) const {
    return dropEngine(files);
}

void SettingsDialog::selectEngine() {
    if (!_prop || lib::editor_window_host_options().all_mode) return;
    const auto& messages = _prop->requiredMsgs();
    const String engineName = _prop->var().engine.value;
    const String scriptName = _prop->var().engineScript.value;
    auto pattern = [](String value) {
        if (value.find('*') != String::npos || value.find('?') != String::npos)
            return value;
        const auto extension = structs::pathFromUtf8(value).extension().string();
        return extension.empty() ? value : String("*") + extension;
    };

    lib::FilePickerRequest request;
    request.title = ::cwxeditor::cwx::utils::tryFormat(
        messages.dlgTitEnginePath.value, engineName);
    request.initial_directory = initialDirectory(
        _enginePath ? _enginePath->text : _prop->enginePath());
    request.initial_name = _enginePath
        ? basename(_enginePath->text)
        : String{};
    request.filters.push_back({
        ::cwxeditor::cwx::utils::tryFormat(messages.filterEnginePath.value,
                                            engineName + ";" + scriptName),
        {pattern(engineName), pattern(scriptName)}, {}});
    request.filters.push_back({"All files", {"*"}, {}});
    const auto picker = _prop->images().services().file_picker;
    if (!picker) return;
    const auto selected = picker->open_file(request);
    if (!selected.succeeded() || selected.selections.empty() || !_enginePath) return;
    const auto path=selected.selections.front().native_path();
    if (!path) return;
    const String value = path->string();
    if (value.empty()) return;
    (void)lib::set_text_value(*_enginePath, value);
    refEngineEnabled();
}

Text SettingsDialog::selectSysSound(Text widget) {
    if (!_prop || !widget || !_prop->images().services().dialogs) return widget;
    const auto& messages = _prop->requiredMsgs();
    static const std::vector<String> extensions{
        ".wav", ".wave", ".mid", ".midi", ".mp3", ".ogg", ".flac"};
    std::vector<String> patterns;
    String extensionList;
    for (const auto& extension : extensions) {
        patterns.push_back("*" + extension);
        if (!extensionList.empty()) extensionList += ";";
        extensionList += "*" + extension;
    }

    lib::FilePickerRequest request;
    request.title = messages.dlgTitSystemSound.value;
    request.initial_directory = initialDirectory(widget->text);
    request.initial_name = basename(widget->text);
    request.filters.push_back({
        ::cwxeditor::cwx::utils::tryFormat(messages.playableSounds.value,
                                            extensionList),
        std::move(patterns), {}});
    request.filters.push_back({"All files", {"*"}, {}});
    const auto picker = _prop->images().services().file_picker;
    if (!picker) return widget;
    const std::weak_ptr<lib::TextEdit> target = widget;
    const std::weak_ptr<int> life = _skinLifetime;
    (void)picker->open_file_async(
        std::move(request), [target, life](lib::FilePickerResult selected) {
            if (!selected.succeeded() || selected.selections.empty()) return;
            if (life.expired()) return;
            if (const auto path = selected.selections.front().native_path())
                if (const auto control = target.lock())
                    (void)lib::set_text_value(*control, path->string());
        });
    return widget;
}

void SettingsDialog::selectTemp() {
    if (!_prop || !_tempDir || !_prop->images().services().file_picker) return;
    const auto& messages = _prop->requiredMsgs();
    auto request = dutils::makeDirectoryPickerRequest(
        messages.tempDir.value, messages.tempDirDesc.value,
        initialDirectory(_tempDir->text.empty() ? _prop->tempPath()
                                                : _tempDir->text));
    const std::weak_ptr<lib::TextEdit> target = _tempDir;
    const std::weak_ptr<int> life = _skinLifetime;
    (void)_prop->images().services().file_picker->select_directory_async(
        std::move(request), [target, life](lib::FilePickerResult selected) {
            if (!selected.succeeded() || selected.selections.empty()) return;
            if (life.expired()) return;
            if (const auto path = selected.selections.front().native_path())
                if (const auto control = target.lock())
                    (void)lib::set_text_value(*control, path->string());
        });
}

void SettingsDialog::selectBackup() {
    if (!_prop || !_backupDir || !_prop->images().services().file_picker) return;
    const auto& messages = _prop->requiredMsgs();
    auto request = dutils::makeDirectoryPickerRequest(
        messages.backupDir.value, messages.backupDirDesc.value,
        initialDirectory(_backupDir->text.empty() ? _prop->backupPath()
                                                  : _backupDir->text));
    const std::weak_ptr<lib::TextEdit> target = _backupDir;
    const std::weak_ptr<int> life = _skinLifetime;
    (void)_prop->images().services().file_picker->select_directory_async(
        std::move(request), [target, life](lib::FilePickerResult selected) {
            if (!selected.succeeded() || selected.selections.empty()) return;
            if (life.expired()) return;
            if (const auto path = selected.selections.front().native_path())
                if (const auto control = target.lock())
                    (void)lib::set_text_value(*control, path->string());
        });
}

void SettingsDialog::selectBackupBeforeSave() {
    if (!_prop || !_backupBeforeSaveDir || !_prop->images().services().file_picker) return;
    const auto& messages = _prop->requiredMsgs();
    auto request = dutils::makeDirectoryPickerRequest(
        messages.backupBeforeSaveDir.value,
        messages.backupBeforeSaveDirDesc.value,
        initialDirectory(_backupBeforeSaveDir->text.empty()
                             ? _prop->backupBeforeSavePath()
                             : _backupBeforeSaveDir->text));
    const std::weak_ptr<lib::TextEdit> target = _backupBeforeSaveDir;
    const std::weak_ptr<int> life = _skinLifetime;
    (void)_prop->images().services().file_picker->select_directory_async(
        std::move(request), [target, life](lib::FilePickerResult selected) {
            if (!selected.succeeded() || selected.selections.empty()) return;
            if (life.expired()) return;
            if (const auto path = selected.selections.front().native_path())
                if (const auto control = target.lock())
                    (void)lib::set_text_value(*control, path->string());
        });
}

void SettingsDialog::selectWallpaper() {
    if (!_prop || !_wallpaper || !_prop->images().services().file_picker) return;
    const auto& messages = _prop->requiredMsgs();
    static const std::vector<String> patterns{
        "*.bmp", "*.jpg", "*.jpeg", "*.png", "*.apng", "*.webp",
        "*.tif", "*.tiff", "*.ico", "*.icon", "*.gif"};
    lib::FilePickerRequest request;
    request.title = messages.dlgTitWallpaper.value;
    // D版の selectWallpaper と同様、p に getcwd() を渡す。 selectFile
    // は現在の壁紙ファイル名とは独立して dirName(nabs(p)) を適用する。
    std::error_code directory_error;
    const auto current_directory = std::filesystem::current_path(directory_error);
    if (!directory_error) request.initial_directory = current_directory.parent_path();
    request.initial_name = basename(_wallpaper->text);
    request.filters.push_back({messages.filterImage.value, patterns, {}});
    request.filters.push_back({"All files", {"*"}, {}});
    const std::weak_ptr<lib::TextEdit> target = _wallpaper;
    const std::weak_ptr<int> life = _skinLifetime;
    (void)_prop->images().services().file_picker->open_file_async(
        std::move(request), [target, life](lib::FilePickerResult selected) {
            if (!selected.succeeded() || selected.selections.empty()) return;
            if (life.expired()) return;
            if (const auto path = selected.selections.front().native_path())
                if (const auto control = target.lock())
                    (void)lib::set_text_value(*control, path->string());
        });
}

void SettingsDialog::refHistories() {
    if (!_prop) return;
    if (!confirmClear(static_cast<String>(_prop->requiredMsgs().dlgMsgHistoryClear))) return;
    _prop->var().openHistories.value.clear();
    invokeOptionalCallback(_comm.refHistories);
    (void)_prop->apply();
    invokeOptionalCallback(_sendReloadProps);
}

void SettingsDialog::refSearchHistories() {
    if (!_prop) return;
    if (!confirmClear(static_cast<String>(_prop->requiredMsgs().dlgMsgSearchHistoryClear))) return;
    _prop->var().searchHistories.value.clear();
    _prop->var().replaceHistories.value.clear();
    _prop->var().grepDirHistories.value.clear();
    invokeOptionalCallback(_comm.refSearchHistories);
    (void)_prop->apply();
    invokeOptionalCallback(_sendReloadProps);
}

void SettingsDialog::refExecutedParties() {
    if (!_prop) return;
    if (!confirmClear(static_cast<String>(_prop->requiredMsgs().dlgMsgExecutedPartiesClear))) return;
    _prop->var().executedParties.value.clear();
    invokeOptionalCallback(_comm.refExecutedParties);
    (void)_prop->apply();
    invokeOptionalCallback(_sendReloadProps);
}

bool SettingsDialog::confirmClear(const lib::StringView message) const {
    if (!_prop || !_prop->images().services().dialogs) return false;
    lib::MessageDialogRequest request;
    request.title = static_cast<String>(_prop->requiredMsgs().dlgTitQuestion);
    request.message = String(message);
    request.severity = lib::MessageSeverity::Question;
    request.buttons = lib::MessageButtons::OKCancel;
    const auto result = _prop->images().services().dialogs->message(request);
    return result == lib::MessageResult::OK || result == lib::MessageResult::Yes;
}

void SettingsDialog::selectMenu() {
    // D版と同様、選択行を読み込む間は変更リスナーを抑止する。
    struct RestoreIgnoreMod {
        SettingsDialog& owner;
        bool previous;
        ~RestoreIgnoreMod() { owner.ignoreMod(previous); }
    } restore{*this, ignoreMod()};
    ignoreMod(true);
    if (!hasMenuSelection()) {
        if (_menuApply) _menuApply->set_enabled(false);
        if (_menuDel) _menuDel->set_enabled(false);
        return;
    }
    const auto row = _menu->selection.front();
    if (row >= _menuData.size()) return;
    if (_mnemonic) _mnemonic->text = _menuData[row].mnemonic;
    if (_hotkey) _hotkey->accelerator(_menuData[row].hotkey);
    if (_menuApply) _menuApply->set_enabled(false);
    if (_menuDel) _menuDel->set_enabled(!_menuData[row].hotkey.empty() || !_menuData[row].mnemonic.empty());
}

void SettingsDialog::applyMenu() {
    if (!hasMenuSelection()) return;
    const auto row = _menu->selection.front();
    if (row >= _menuData.size()) return;
    auto menu_data = _menuData;
    auto& data = menu_data[row];
    data.mnemonic = _mnemonic ? _mnemonic->text : String{};
    data.hotkey = _hotkey ? _hotkey->acceleratorText() : String{};
    const auto applied_id = data.id;
    const auto applied_mnemonic = data.mnemonic;
    const auto applied_hotkey = data.hotkey;
    auto table_rows = _menu ? _menu->rows : std::vector<lib::TableRow>{};
    if (_menu && row < _menu->rows.size()) {
        const String sample = menu::MenuProps::buildMenuSample(
            _prop->parent(), data.id, data.mnemonic, data.hotkey);
        if (!sample.empty()) table_rows[row].cells[0].text = sample;
    }
    if (_menu && !lib::replace_table_rows(_menu, std::move(table_rows),
                                          _menu->selection)) return;
    _menuData.swap(menu_data);
    _appliedMnemonic[applied_id] = applied_mnemonic;
    _appliedHotkey[applied_id] = applied_hotkey;
    if (_menuApply) _menuApply->set_enabled(false);
    if (_menuDel) _menuDel->set_enabled(!applied_hotkey.empty() || !applied_mnemonic.empty());
    applyEnabled();
}

void SettingsDialog::refreshMenu() {
    if (!_menu || !_prop) return;
    const auto selected = _menu->selection;
    decltype(_menuData) menu_data;
    std::vector<lib::TableRow> table_rows;
    for (int raw = 0; raw <= static_cast<int>(types::MenuID::DeleteIcon); ++raw) {
        const auto id = static_cast<types::MenuID>(raw);
        if (id == types::MenuID::None || menu::isNoKeyBindMenu(id)) continue;
        String mnemonic = _appliedMnemonic.contains(id) ? _appliedMnemonic[id] : _menuProps.mnemonic(id);
        String hotkey = _appliedHotkey.contains(id) ? _appliedHotkey[id] : _menuProps.hotkey(id);
        const String text = menu::MenuProps::buildMenuSample(_prop->parent(), id, mnemonic, hotkey);
        if (_menuIncSearch && !_menuIncSearch->match(text)) continue;
        menu_data.push_back({id, std::move(mnemonic), std::move(hotkey)});
        const auto icon = _comm.menuImage ? _comm.menuImage(id) : lib::ResourceId{};
        table_rows.push_back({{{text, {}, icon, true}}, false});
    }
    auto next_selection = !selected.empty() && selected.front() < table_rows.size()
                              ? selected : std::vector<std::size_t>{};
    if (next_selection.empty() && !table_rows.empty()) next_selection.push_back(0U);
    auto publication = lib::prepare_table_rows(_menu, std::move(table_rows),
                                               std::move(next_selection));
    if (!publication.commit()) return;
    _menuData.swap(menu_data);
    selectMenu();
}

bool SettingsDialog::hasMenuSelection() const noexcept {
    return _menu && !_menu->selection.empty() && _menu->selection.front() < _menuData.size();
}

void SettingsDialog::refreshScenario(summary::Summary* summ) { _summ = summ; }

void SettingsDialog::refreshEnabled() {
    // Commons のない差し込み用・画面なしのホストでも、初期状態を設定する。
    if (_clearHist) _clearHist->set_enabled(_prop && !_prop->var().openHistories.value.empty());
    if (_clearSHist) _clearSHist->set_enabled(_prop &&
        (!_prop->var().searchHistories.value.empty() || !_prop->var().grepDirHistories.value.empty()));
    if (_clearPHist) _clearPHist->set_enabled(_prop && !_prop->var().executedParties.value.empty());
    const bool enabled = _backupEnabled && _backupEnabled->checked;
    if (_backupDir) _backupDir->set_enabled(enabled);
    if (_backupIntervalTypeTime) _backupIntervalTypeTime->set_enabled(enabled);
    if (_backupIntervalTypeEdit) _backupIntervalTypeEdit->set_enabled(enabled);
    if (_backupIntervalTime) _backupIntervalTime->set_enabled(enabled && _backupIntervalTypeTime && _backupIntervalTypeTime->checked);
    if (_backupIntervalEdit) _backupIntervalEdit->set_enabled(enabled && _backupIntervalTypeEdit && _backupIntervalTypeEdit->checked);
    if (_backupCount) _backupCount->set_enabled(enabled);
    if (_backupRef) _backupRef->set_enabled(enabled);
    if (_backupRefAuthor) _backupRefAuthor->set_enabled(enabled);
    if (_autoSave) _autoSave->set_enabled(enabled);
    if (_backupArchived) _backupArchived->set_enabled(enabled);
    const bool beforeSave = _backupBeforeSaveEnabled && _backupBeforeSaveEnabled->checked;
    if (_backupBeforeSaveDir) _backupBeforeSaveDir->set_enabled(beforeSave);
    if (_backupBeforeSaveRef) _backupBeforeSaveRef->set_enabled(beforeSave);
    refreshOpenButtonEnabledStates();
}

void SettingsDialog::refreshOpenButtonEnabledStates() {
    const auto bindings = _openButtonEnabledRefreshes;
    for (const auto& refresh : bindings)
        if (refresh) refresh();
}

void SettingsDialog::refEngineEnabled() {
    const bool textEmpty = !_enginePath || _enginePath->text.empty();
    if (_findEnginePath) _findEnginePath->set_enabled(textEmpty);
    const bool enabled = !_findEnginePath || !_findEnginePath->checked || !textEmpty;
    if (_enginePath) _enginePath->set_enabled(enabled);
    if (_refEnginePath) _refEnginePath->set_enabled(enabled);
    refreshOpenButtonEnabledStates();
}

lib::Array<String> SettingsDialog::splitLines(String value) const {
    lib::Array<String> result;
    std::stringstream stream(std::move(value));
    String line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        result.push_back(std::move(line));
    }
    while (!result.empty() && result.back().empty()) result.pop_back();
    return result;
}

bool SettingsDialog::validEnginePath(String value) const {
    if (value.empty()) return true;
    std::error_code error;
    return std::filesystem::is_regular_file(structs::pathFromUtf8(value), error) && !error;
}

void applyHistoryLimits(dprops::Props& prop) {
    auto& value = prop.var();
    const auto scenarioHistoryMax =
        static_cast<std::size_t>(std::max(0, value.historyMax.value));
    if (value.openHistories.value.size() > scenarioHistoryMax) {
        value.openHistories.value = history::removeHistoryNonExisting(
            value.removeScenarioHistoryNonExistingWithPriority.value,
            std::move(value.openHistories.value), scenarioHistoryMax);
    }
    if (value.importHistory.value.size() > scenarioHistoryMax) {
        value.importHistory.value = history::removeHistoryNonExisting(
            value.removeScenarioHistoryNonExistingWithPriority.value,
            std::move(value.importHistory.value), scenarioHistoryMax);
    }
    const auto searchHistoryMax =
        static_cast<std::size_t>(std::max(0, value.searchHistoryMax.value));
    if (value.searchHistories.value.size() > searchHistoryMax)
        value.searchHistories.value.resize(searchHistoryMax);
    // D版と同様、ここでは replaceHistories
    // を意図的に変更しない。 検索履歴の表示上限を減らしたとき
    // ではなく、明示的な消去操作で他の検索履歴とともに消す。
    if (value.grepDirHistories.value.size() > searchHistoryMax)
        value.grepDirHistories.value.resize(searchHistoryMax);
    if (value.executedParties.value.size() > value.executedPartiesMax.value) {
        value.executedParties.value = partyhistory::removePartyHistoryNonExisting(
            partyhistory::partyHistorySettings(value, &prop.sys()),
            value.removePartyHistoryNonExistingWithPriority.value,
            std::move(value.executedParties.value), value.executedPartiesMax.value);
    }
}

bool SettingsDialog::apply() {
    if (!_prop) return false;
    const String engine = _enginePath ? _enginePath->text : String{};
    if (_enginePath && !validEnginePath(engine)) {
        // モーダルの警告がホストへ再入し、このダイアログを無効化
        // する場合がある。 表示前に必要な値をすべて取得し、そ
        // の後も確認処理の寿命が有効な場合だけ操作欄に触れる。
        const auto warningTitle = _prop->requiredMsgs().dlgTitWarning.value;
        const auto warningMessage = utils::tryFormat(
            _prop->requiredMsgs().errorEnginePath.value, _prop->var().engine.value);
        const auto dialogs = services().dialogs;
        const auto gui = services().gui;
        const auto field = _enginePath;
        const auto tabs = _tabf;
        const std::weak_ptr<int> life = _questionLifetime;
        if (dialogs) {
            lib::MessageDialogRequest request;
            request.title = warningTitle;
            request.message = warningMessage;
            request.severity = lib::MessageSeverity::Warning;
            request.buttons = lib::MessageButtons::OK;
            (void)dialogs->message(request);
        }
        if (life.expired()) return false;
        if (tabs) tabs->selected_index = 0U;
        if (gui) gui->focus(field);
        return false;
    }
    struct WorkingStateRollback final {
        dprops::Props& prop;
        properties::FlexProps flex;
        variables::FlexEtcProps var;
        bool accepted{};
        ~WorkingStateRollback() {
            if (accepted) return;
            try {
                prop.flex() = std::move(flex);
                prop.var() = std::move(var);
            } catch (...) {
                // 設定保存の失敗で、元の失敗を隠さない。
            }
        }
    } rollback{*_prop, _prop->flex(), _prop->var()};
    const String temp = _tempDir ? _tempDir->text : String{};
    const String backup = _backupDir ? _backupDir->text : String{};
    const String backupBeforeSave = _backupBeforeSaveDir ? _backupBeforeSaveDir->text : String{};
    (void)temp;
    (void)backup;
    (void)backupBeforeSave;

    if (!applyUncommittedForms()) return false;

    OldSettings old(*_prop, _prop->flex().menu);
    auto& value = _prop->var();
    // ALL は、単独版のエンジンの場所の設定を表示も上書きもしない。
    if (_enginePath) value.enginePath = engine;
    if (_findEnginePath) value.findEnginePath = _findEnginePath->checked;
    // 単独版・ALL の両方にこれらの操作欄を表示する。 どちら
    // の適用でも選択した百分率を保持する。整数の項目は元画像の品
    // 質を選ぶもので、 表示時の小数の倍率を表すものではない。
    value.drawingScalePercent = static_cast<std::uint32_t>(100 +
        25 * std::clamp(comboIndex(_drawingScale), 0, 4));
    value.imageScalePercent = static_cast<std::uint32_t>(100 +
        25 * std::clamp(comboIndex(_imageScale), 0, 4));
    value.drawingScale = (value.drawingScalePercent.value + 99U) / 100U;
    value.imageScale = (value.imageScalePercent.value + 99U) / 100U;
    value.uiTextScale = static_cast<std::uint32_t>(100 +
        25 * std::clamp(comboIndex(_uiTextScale), 0, 4));
    if (_language && _language->selected_index && *_language->selected_index > 0U) {
        const auto locale = _msgsTableIndex.find(static_cast<int>(*_language->selected_index));
        const auto file = locale == _msgsTableIndex.end()
                              ? _msgsTableFile.end()
                              : _msgsTableFile.find(locale->second);
        if (file != _msgsTableFile.end()) {
            value.languageFile = file->second;
            value.useSystemLanguage = false;
        } else {
            value.languageFile = "";
            value.useSystemLanguage = true;
        }
    } else {
        value.languageFile = "";
        value.useSystemLanguage = true;
    }
    if (_tempDir) value.tempPath = temp;
    if (_backupDir) value.backupPath = backup;
    value.backupEnabled = _backupEnabled && _backupEnabled->checked;
    value.backupIntervalType = (_backupIntervalTypeEdit && _backupIntervalTypeEdit->checked)
                                   ? static_cast<std::int32_t>(BackupType::Edit)
                                   : static_cast<std::int32_t>(BackupType::Time);
    value.backupInterval = numberValue<std::int32_t>(_backupIntervalTime);
    value.backupIntervalEdit = numberValue<std::int32_t>(_backupIntervalEdit);
    value.backupCount = numberValue<std::int32_t>(_backupCount);
    value.backupRefAuthor = _backupRefAuthor && _backupRefAuthor->checked;
    value.autoSave = _autoSave && _autoSave->checked;
    value.backupArchived = _backupArchived && _backupArchived->checked;
    if (_backupBeforeSaveDir) value.backupBeforeSavePath = backupBeforeSave;
    value.backupBeforeSaveEnabled = _backupBeforeSaveEnabled && _backupBeforeSaveEnabled->checked;
    value.defaultAuthor = _author ? _author->text : String{};
    value.newAreaName = _newAreaName ? _newAreaName->text : String{};
    value.wallpaper = _wallpaper ? _wallpaper->text : String{};
    value.wallpaperStyle = _wallpaperStyle ? comboIndex(_wallpaperStyle, static_cast<int>(structs::WallpaperStyle::Tile))
                                            : static_cast<int>(structs::WallpaperStyle::Tile);
    value.historyMax = numberValue<std::int32_t>(_histMax);
    value.searchHistoryMax = numberValue<std::int32_t>(_sHistMax);
    value.executedPartiesMax = numberValue<std::uint32_t>(_pHistMax);
    value.undoMaxMainView = numberValue<std::int32_t>(_undoMaxMainView);
    value.undoMaxEvent = numberValue<std::int32_t>(_undoMaxEvent);
    value.undoMaxReplace = numberValue<std::int32_t>(_undoMaxReplace);
    value.undoMaxEtc = numberValue<std::int32_t>(_undoMaxEtc);
    value.ignorePaths = splitLines(_ignorePaths ? _ignorePaths->text : String{});
    if (_etcSettings && !_etcSettings->apply(false)) return false;
    if (_targetVersion) {
        const auto found = _targetVersionTbl2.find(comboIndex(_targetVersion));
        if (found != _targetVersionTbl2.end()) value.targetVersion = found->second;
    }
    if (_soundPlayType) {
        const auto found = _soundPlayTypeTbl2.find(comboIndex(_soundPlayType));
        if (found != _soundPlayTypeTbl2.end()) value.soundPlayType = found->second;
    }
    value.bgmVolume = numberValue<std::int32_t>(_bgmVolume, value.bgmVolume.value);
    if (_soundEffectPlayType) {
        const auto found = _soundEffectPlayTypeTbl2.find(comboIndex(_soundEffectPlayType));
        if (found != _soundEffectPlayTypeTbl2.end()) value.soundEffectPlayType = found->second;
    }
    value.seVolume = numberValue<std::int32_t>(_seVolume, value.seVolume.value);
    value.dialogStatus = comboIndex(_dialogStatus, value.dialogStatus.value);
    value.showStatusTime = comboIndex(_showStatusTime, value.showStatusTime.value);
    value.savedSound = _savedSound ? _savedSound->text : value.savedSound.value;
    value.flagInitValue = _flagInitValue ? comboIndex(_flagInitValue) == 0 : value.flagInitValue.value;
    value.stepInitValue = comboIndex(_stepInitValue, value.stepInitValue.value);
    value.stepValueName = _stepValueName ? _stepValueName->text : value.stepValueName.value;
    applyHistoryLimits(*_prop);
    storeSkinControls();
    value.bgImagesDefault =
        lib::SharedSlice<structs::BgImageS>{_bgImagesDefault};
    value.bgImageSettings =
        lib::SharedSlice<structs::BgImageSetting>{_bgImageSettings};
    value.standardSelections = lib::SharedSlice<String>{_standardSelections};
    value.standardKeyCodes = lib::SharedSlice<String>{_standardKeyCodes};
    value.keyCodesByFeatures =
        lib::SharedSlice<structs::KeyCodeByFeature>{_keyCodesByFeatures};
    value.keyCodesByMotions =
        lib::SharedSlice<structs::KeyCodeByMotion>{_keyCodesByMotions};
    if (_skinTypeChooser) {

    }
    value.settingsWithSkinTypes.value.values =
        lib::SharedSlice<structs::SettingsWithSkinType>{skinSettingsDraft()};

    value.outerTools = _toolsView ? _toolsView->array() : _tools.array;
    value.classicEngines = _cEnginesView ? _cEnginesView->array() : _cEngines.array;
    value.eventTemplates = _evTemplsView ? _evTemplsView->array() : _evTempls.array;
    value.scenarioTemplates = _scTemplsView ? _scTemplsView->array() : _scTempls.array;
    updateInitializers();
    value.contentInitializers = _initializers;
    for (const auto& [id, mnemonic] : _appliedMnemonic) {
        _menuProps.mnemonic(id, mnemonic);
        const auto found = _appliedHotkey.find(id);
        _menuProps.hotkey(id, found == _appliedHotkey.end() ? String{} : found->second);
    }
    _appliedMnemonic.clear();
    _appliedHotkey.clear();
    if (_summ && _enginePath && dskin::findCWPy(
            *_prop, _summ->useTemp() ? _summ->zipName() : _summ->scenarioPath())) {
        _enginePath->text = value.enginePath.value;
        setChecked(_findEnginePath, value.findEnginePath.value);
        refEngineEnabled();
    }
    // ダイアログは、独立したD版互換の MenuProps の未確定値を編
    // 集する。 Props の保存処理の間だけ、実際のメニューが使う同じ
    // FlexProps に反映し、 どの失敗でも元の有効な値に戻す。
    const auto previous_menu = _prop->flex().menu;
    _prop->flex().menu = _menuProps;
    try {
        if (!_prop->apply()) {
            _prop->flex().menu = previous_menu;
            return false;
        }
    } catch (...) {
        _prop->flex().menu = previous_menu;
        throw;
    }
    rollback.accepted = true;
    if (_etcSettings) _etcSettings->commitAppliedValues();
    _menuProps = _prop->flex().menu;
    // 永続化が失敗したら、表示中のエディタは以前の倍率に保つ。
    // Props が確定したら共通の百分率の境界を使い、 動
    // 作中のウィンドウに次回起動時と同じ倍率を正確に適用する。
    const auto uiTextScale = lib::editor_ui_text_scale_from_percent(value.uiTextScale.value);
    if (_prop->images().services().gui)
        _prop->images().services().gui->set_ui_text_scale(uiTextScale);
    else
        lib::set_editor_ui_text_scale(uiTextScale);
    lib::set_number_edit_wheel_enabled(value.spinnerUpDownWithWheel.value);
    lib::set_tab_wheel_switch_enabled(value.switchTabWheel.value);
    lib::set_editor_ime_mode(value.imeMode.value);
    lib::set_combo_edit_popup_enabled(value.comboListVisible.value);
    old.raiseEvent(_comm);
    invokeOptionalCallback(_sendReloadProps);
    invokeOptionalCallback(_comm.saved);
    return true;
}

void SettingsDialog::RefE::widgetSelected() { if (owner_) owner_->refreshEnabled(); }
void SettingsDialog::RefE::modifyText() { if (owner_) owner_->refreshEnabled(); }
void SettingsDialog::SelEngine::widgetSelected() { if (owner_) owner_->selectEngine(); }
void SettingsDialog::SelTemp::widgetSelected() { if (owner_) owner_->selectTemp(); }
void SettingsDialog::SelBackup::widgetSelected() { if (owner_) owner_->selectBackup(); }
void SettingsDialog::SelBackupBeforeSave::widgetSelected() { if (owner_) owner_->selectBackupBeforeSave(); }
void SettingsDialog::SelWallpaper::widgetSelected() { if (owner_) owner_->selectWallpaper(); }
void SettingsDialog::SelSysSound::widgetSelected() { if (owner_) owner_->selectSysSound(text_); }
void SettingsDialog::ClearHist::widgetSelected() { if (owner_) owner_->refHistories(); }
void SettingsDialog::ClearSHist::widgetSelected() { if (owner_) owner_->refSearchHistories(); }
void SettingsDialog::ClearPHist::widgetSelected() { if (owner_) owner_->refExecutedParties(); }
void SettingsDialog::SelectMenu::widgetSelected() { if (owner_) owner_->selectMenu(); }
void SettingsDialog::ModMenu::modifyText() {
    if (!owner_ || owner_->ignoreMod() || !owner_->hasMenuSelection()) return;
    if (owner_->_menuApply) owner_->_menuApply->set_enabled(true);
    if (owner_->_menuDel) owner_->_menuDel->set_enabled(
        (owner_->_mnemonic && !owner_->_mnemonic->text.empty()) ||
        (owner_->_hotkey && !owner_->_hotkey->acceleratorText().empty()));
}
void SettingsDialog::DelMenuAccel::widgetSelected() {
    if (!owner_ || !owner_->hasMenuSelection()) return;
    if (owner_->_mnemonic) owner_->_mnemonic->text.clear();
    if (owner_->_hotkey) owner_->_hotkey->accelerator({});
    owner_->applyMenu();
}
void SettingsDialog::ApplyMenu::widgetSelected() { if (owner_) owner_->applyMenu(); }

OldSettings::OldSettings(dprops::Props& value, const menu::MenuProps& menuValue)
    : prop(&value), menuProps(&menuValue) {
    const auto& v = value.var();
    drawingScale = v.drawingScale.value;
    imageScale = v.imageScale.value;
    drawingScalePercent = variables::previewScalePercent(
        v.drawingScalePercent.value, v.drawingScale.value);
    imageScalePercent = variables::previewScalePercent(
        v.imageScalePercent.value, v.imageScale.value);
    uiTextScale = v.uiTextScale.value;
    targetVersion = v.targetVersion.value;
    oldEnginePath = v.enginePath.value;
    findEnginePath = v.findEnginePath.value;
    oldWallpaper = v.wallpaper.value;
    oldWallpaperStyle = v.wallpaperStyle.value;
    oldSelections = v.standardSelections.value;
    oldKeyCodes = v.standardKeyCodes.value;
    tools = v.outerTools.value;
    cEngines = v.classicEngines.value;
    eventTemplates = v.eventTemplates.value;
    oldIgnorePaths = v.ignorePaths.value;
    oldSmoothingCard = v.smoothingCard.value;
    oldLogicalSort = v.logicalSort.value;
    oldOpenHistories = v.openHistories.value;
    importHistory = v.importHistory.value;
    historyMax = static_cast<std::uint32_t>(std::max(0, v.historyMax.value));
    oldSearchHistories = v.searchHistories.value;
    oldReplaceHistories = v.replaceHistories.value;
    oldGrepDirHistories = v.grepDirHistories.value;
    oldUndoMaxMainView = v.undoMaxMainView.value;
    oldUndoMaxEvent = v.undoMaxEvent.value;
    oldUndoMaxReplace = v.undoMaxReplace.value;
    oldUndoMaxEtc = v.undoMaxEtc.value;
    oldDialogStatus = v.dialogStatus.value;
    showStatusTime = v.showStatusTime.value;
    for (int raw = 0; raw <= static_cast<int>(types::MenuID::DeleteIcon); ++raw) {
        const auto id = static_cast<types::MenuID>(raw);
        if (id == types::MenuID::None || menu::isNoKeyBindMenu(id)) continue;
        oldMnemonic[id] = menuValue.mnemonic(id);
        oldHotkey[id] = menuValue.hotkey(id);
    }
    floatMessagePreview = v.floatMessagePreview.value;
    useMessageWindowColorInTextContentDialog = v.useMessageWindowColorInTextContentDialog.value;
    bgmVolume = v.bgmVolume.value;
    seVolume = v.seVolume.value;
    soundPlayType = v.soundPlayType.value;
    soundEffectPlayType = v.soundEffectPlayType.value;
    showHandMark = v.showHandMark.value;
    showEventTreeMark = v.showEventTreeMark.value;
    showCardListHeader = v.showCardListHeader.value;
    showCardListTitle = v.showCardListTitle.value;
    showSkillCardLevel = v.showSkillCardLevel.value;
    ignoreEmptyStart = v.ignoreEmptyStart.value;
    classicStyleTree = v.classicStyleTree.value;
    radarStyleParams = v.radarStyleParams.value;
    showAptitudeOnCastCardEditor = v.showAptitudeOnCastCardEditor.value;
    showSpNature = v.showSpNature.value;
    showVariableValuesInEventText = v.showVariableValuesInEventText.value;
    useNamesAfterStandard = v.useNamesAfterStandard.value;
    selectVariableWithTree = v.selectVariableWithTree.value;
    straightEventTreeView = v.straightEventTreeView.value;
    forceIndentBranchContent = v.forceIndentBranchContent.value;
    showTerminalMark = v.showTerminalMark.value;
    showTargetStartLineNumber = v.showTargetStartLineNumber.value;
    showSummaryInAreaTable = v.showSummaryInAreaTable.value;
    showAreaDirTree = v.showAreaDirTree.value;
    showContentsGroupName = v.showContentsGroupName.value;
    showEventContentDescription = v.showEventContentDescription.value;
    contentsFloat = v.contentsFloat.value;
    contentsAutoHide = v.contentsAutoHide.value;
    showCloseButtonAllTab = v.showCloseButtonAllTab.value;
    showMotionDescription = v.showMotionDescription.value;
    xmlFileNameIsIDOnly = v.xmlFileNameIsIDOnly.value;
    replaceClassicResourceExtension = v.replaceClassicResourceExtension.value;
    saveInnerImagePath = v.saveInnerImagePath.value;
    showSummaryPreview = v.showSummaryPreview.value;
    showMessagePreview = v.showMessagePreview.value;
    mainToolBar = v.mainToolBar.value;
    stepValueName = v.stepValueName.value;
    showCurrentValueOnTopAlways = v.showCurrentValueOnTopAlways.value;
    switchFixedWithCheckBox = v.switchFixedWithCheckBox.value;
    resizingImageWithRatio = v.resizingImageWithRatio.value;
    drawXORSelectionLine = v.drawXORSelectionLine.value;
    showSceneViewSelectionFilter = v.showSceneViewSelectionFilter.value;
    saveSkinName = v.saveSkinName.value;
    showItemNumberOfSceneAndEventView = v.showItemNumberOfSceneAndEventView.value;
    lastExecutedParty = v.lastExecutedParty.value;
    executedParties = v.executedParties.value;
    executedPartiesMax = v.executedPartiesMax.value;
    useCoolBar = v.useCoolBar.value;
    keyCodesByFeatures = v.keyCodesByFeatures.value;
    // C++版エディタの拡張。D版にもこのフィールドの宣言はあるが、 OldSettings::raiseEvent
    // では初期化も比較もしていない。
    keyCodesByMotions = dprops::arraySnapshot(v.keyCodesByMotions.value);
    bgImageSettings = v.bgImageSettings.value;
    settingsWithSkinTypes = v.settingsWithSkinTypes.value.values;
}

void OldSettings::raiseEvent(const Callbacks& callbacks) {
    if (!prop) return;
    const auto& v = prop->var();
    // D版と同様、画像倍率が変わっても、プレビュー以外の作業
    // 部分の幅は維持する。 表示を更新する前に、保存された外
    // 側の寸法へプレビュー幅の差を正確に反映する必要がある。
    const auto newImagePercent = variables::previewScalePercent(
        v.imageScalePercent.value, v.imageScale.value);
    const auto newDrawingPercent = variables::previewScalePercent(
        v.drawingScalePercent.value, v.drawingScale.value);
    if (imageScalePercent != newImagePercent) {
        const auto widthDelta = [](const std::int32_t logicalWidth,
                                   const std::uint32_t oldPercent,
                                   const std::uint32_t newPercent) {
            return static_cast<std::int32_t>(std::lround(
                static_cast<double>(logicalWidth) *
                (static_cast<double>(newPercent) - oldPercent) / 100.0));
        };
        if (showSummaryPreview) {
            const auto width = static_cast<std::int32_t>(
                prop->parent().looks().summarySize().width);
            prop->flex().summaryDlg.width(
                prop->flex().summaryDlg.width() +
                widthDelta(width, imageScalePercent, newImagePercent));
        }
        const auto message = prop->parent().looks().messageBounds();
        if (showMessagePreview && !floatMessagePreview) {
            prop->flex().msgDlg.width(
                prop->flex().msgDlg.width() +
                widthDelta(message.width, imageScalePercent, newImagePercent));
        }
        if (floatMessagePreview) {
            prop->flex().msgPrev.width(
                prop->flex().msgPrev.width() +
                widthDelta(message.width, imageScalePercent, newImagePercent));
            prop->flex().msgPrev.height(
                prop->flex().msgPrev.height() +
                widthDelta(message.height, imageScalePercent, newImagePercent));
        }
    }
    // メッセージプレビューを埋め込み・独立表示で切り替えると、
    // D版の両メッセージエディタで確保するプレビュー幅が変わる。
    if (floatMessagePreview != v.floatMessagePreview.value) {
        const auto messageWidth =
            prop->s(prop->parent().looks().messageBounds().width);
        const auto delta = v.floatMessagePreview.value ? -messageWidth
                                                       : messageWidth;
        if (v.showMessagePreview.value) {
            prop->flex().msgDlg.width(
                std::max(1, prop->flex().msgDlg.width() + delta));
        }
        if (v.showDialogPreview.value) {
            prop->flex().speakDlg.width(
                std::max(1, prop->flex().speakDlg.width() + delta));
        }
    }
    if (imageScale != v.imageScale.value || drawingScale != v.drawingScale.value ||
        imageScalePercent != newImagePercent ||
        drawingScalePercent != newDrawingPercent ||
        uiTextScale != v.uiTextScale.value) invokeOptionalCallback(callbacks.refImageScale);
    if (targetVersion != v.targetVersion.value) invokeOptionalCallback(callbacks.refTargetVersion);
    if (oldWallpaper != v.wallpaper.value || oldWallpaperStyle != v.wallpaperStyle.value) invokeOptionalCallback(callbacks.refWallpaper);
    if (tools != v.outerTools.value) invokeOptionalCallback(callbacks.refOuterTools);
    if (!equalEventTemplates(eventTemplates, v.eventTemplates.value)) invokeOptionalCallback(callbacks.refEventTemplates);
    if (oldIgnorePaths != v.ignorePaths.value) invokeOptionalCallback(callbacks.refIgnorePaths);
    if (oldSmoothingCard != v.smoothingCard.value || showStatusTime != v.showStatusTime.value) invokeOptionalCallback(callbacks.refCardState);
    if (switchFixedWithCheckBox != v.switchFixedWithCheckBox.value) invokeOptionalCallback(callbacks.refSwitchFixedWithCheckBox);
    if (resizingImageWithRatio != v.resizingImageWithRatio.value || drawXORSelectionLine != v.drawXORSelectionLine.value || showSceneViewSelectionFilter != v.showSceneViewSelectionFilter.value) invokeOptionalCallback(callbacks.refImagePaneSelectionFilter);
    if (oldLogicalSort != v.logicalSort.value) invokeOptionalCallback(callbacks.refSortCondition);
    const bool classicEnginesChanged = !equalClassicEngines(cEngines, v.classicEngines.value);
    // D版と同様、エンジンパスの置換とClassicエンジン定義の変
    // 更の両方で、現在のスキンを解決し直す。 Classicの一覧だ
    // けを更新すると、現在のスキン・音声・素材ルートが古いまま残る。
    if (oldEnginePath != v.enginePath.value || classicEnginesChanged)
        invokeOptionalCallback(callbacks.refSkin);
    if (classicEnginesChanged) invokeOptionalCallback(callbacks.refClassicSkin);
    if (!equalOpenHistories(oldOpenHistories, v.openHistories.value)) invokeOptionalCallback(callbacks.refHistories);
    if (!equalOpenHistories(importHistory, v.importHistory.value)) invokeOptionalCallback(callbacks.refImportHistory);
    if (historyMax != static_cast<std::uint32_t>(std::max(0, v.historyMax.value))) invokeOptionalCallback(callbacks.refHistoryMax);
    if (oldSearchHistories != v.searchHistories.value || oldReplaceHistories != v.replaceHistories.value || oldGrepDirHistories != v.grepDirHistories.value) invokeOptionalCallback(callbacks.refSearchHistories);
    if (oldUndoMaxMainView != v.undoMaxMainView.value || oldUndoMaxEvent != v.undoMaxEvent.value || oldUndoMaxReplace != v.undoMaxReplace.value || oldUndoMaxEtc != v.undoMaxEtc.value) invokeOptionalCallback(callbacks.refUndoMax);
    if (oldDialogStatus != v.dialogStatus.value || stepValueName != v.stepValueName.value) invokeOptionalCallback(callbacks.refContentText);
    bool updateTool = false;
    for (const auto& [id, oldValue] : oldMnemonic) {
        if (!menuProps) break;
        const auto mnemonic = menuProps->mnemonic(id);
        const auto hotkey = menuProps->hotkey(id);
        const auto found = oldHotkey.find(id);
        const bool mnemonicChanged = oldValue != mnemonic;
        const bool hotkeyChanged = found == oldHotkey.end() || found->second != hotkey;
        if ((mnemonicChanged || hotkeyChanged) && callbacks.refMenu)
            callbacks.refMenu(id);
        updateTool = updateTool || mnemonicChanged || hotkeyChanged;
    }
    if (updateTool) invokeOptionalCallback(callbacks.updateToolBarText);
    if (floatMessagePreview != v.floatMessagePreview.value) invokeOptionalCallback(callbacks.refFloatMessagePreview);
    if (useMessageWindowColorInTextContentDialog != v.useMessageWindowColorInTextContentDialog.value) invokeOptionalCallback(callbacks.refUseMessageWindowColorInTextContentDialog);
    if (bgmVolume != v.bgmVolume.value)
        sound::bgmVolume(static_cast<std::uint32_t>(std::max(0, v.bgmVolume.value)));
    if (seVolume != v.seVolume.value)
        sound::seVolume(static_cast<std::uint32_t>(std::max(0, v.seVolume.value)));
    if (soundPlayType != v.soundPlayType.value || soundEffectPlayType != v.soundEffectPlayType.value) invokeOptionalCallback(callbacks.refSoundType);
    if (showHandMark != v.showHandMark.value || showEventTreeMark != v.showEventTreeMark.value || showSkillCardLevel != v.showSkillCardLevel.value || ignoreEmptyStart != v.ignoreEmptyStart.value || showStatusTime != v.showStatusTime.value) invokeOptionalCallback(callbacks.refCardImageStatus);
    if (classicStyleTree != v.classicStyleTree.value) invokeOptionalCallback(callbacks.refEventTreeStyle);
    if (radarStyleParams != v.radarStyleParams.value) invokeOptionalCallback(callbacks.refRadarStyle);
    if (showAptitudeOnCastCardEditor != v.showAptitudeOnCastCardEditor.value) invokeOptionalCallback(callbacks.refShowAptitudes);
    if (radarStyleParams != v.radarStyleParams.value || showAptitudeOnCastCardEditor != v.showAptitudeOnCastCardEditor.value) invokeOptionalCallback(callbacks.refCastCardParameterEditStyle);
    if (showSpNature != v.showSpNature.value || useNamesAfterStandard != v.useNamesAfterStandard.value || showCurrentValueOnTopAlways != v.showCurrentValueOnTopAlways.value) invokeOptionalCallback(callbacks.refCoupons);
    if (showCurrentValueOnTopAlways != v.showCurrentValueOnTopAlways.value) {
        invokeOptionalCallback(callbacks.refGossips);
        invokeOptionalCallback(callbacks.refCompleteStamps);
        invokeOptionalCallback(callbacks.refCellNames);
    }
    if (useNamesAfterStandard != v.useNamesAfterStandard.value || showCurrentValueOnTopAlways != v.showCurrentValueOnTopAlways.value) invokeOptionalCallback(callbacks.refKeyCodes);
    if (showVariableValuesInEventText != v.showVariableValuesInEventText.value) invokeOptionalCallback(callbacks.refPreviewValues);
    if (showCardListHeader != v.showCardListHeader.value) invokeOptionalCallback(callbacks.refShowCardListHeader);
    if (showCardListTitle != v.showCardListTitle.value) invokeOptionalCallback(callbacks.refShowCardListTitle);
    if (selectVariableWithTree != v.selectVariableWithTree.value) invokeOptionalCallback(callbacks.refVarSelectStyle);
    if (straightEventTreeView != v.straightEventTreeView.value || forceIndentBranchContent != v.forceIndentBranchContent.value) invokeOptionalCallback(callbacks.refEventTreeViewStyle);
    if (showTerminalMark != v.showTerminalMark.value || showTargetStartLineNumber != v.showTargetStartLineNumber.value) invokeOptionalCallback(callbacks.refEventEditorStyle);
    if (showSummaryInAreaTable != v.showSummaryInAreaTable.value) invokeOptionalCallback(callbacks.refAreaTable);
    if (showAreaDirTree != v.showAreaDirTree.value) invokeOptionalCallback(callbacks.refTableViewStyle);
    if (contentsFloat != v.contentsFloat.value || contentsAutoHide != v.contentsAutoHide.value || showContentsGroupName != v.showContentsGroupName.value || showEventContentDescription != v.showEventContentDescription.value || useCoolBar != v.useCoolBar.value) invokeOptionalCallback(callbacks.refContentsToolBoxStyle);
    if (showMotionDescription != v.showMotionDescription.value) invokeOptionalCallback(callbacks.refUpdateMotionBarStyle);
    if (showCloseButtonAllTab != v.showCloseButtonAllTab.value)
        invokeOptionalCallback(callbacks.updateCloseButtons);
    if (mainToolBar != v.mainToolBar.value || useCoolBar != v.useCoolBar.value)
        invokeOptionalCallback(callbacks.updateMainToolBar);

    // これらの設定は次回保存するバイト列を変える。Props の処理が成功してか
    // ら、現在の Summary を変更済みにする。 展開形式とClassicシ
    // ナリオで、D版の changed/changedAll の区別に合わせる。
    if (callbacks.markSummaryChanged) {
        if (xmlFileNameIsIDOnly != v.xmlFileNameIsIDOnly.value)
            callbacks.markSummaryChanged(SummaryDirtyKind::XmlFileName);
        if (saveSkinName != v.saveSkinName.value)
            callbacks.markSummaryChanged(SummaryDirtyKind::SaveSkinName);
        if (saveInnerImagePath != v.saveInnerImagePath.value)
            callbacks.markSummaryChanged(SummaryDirtyKind::LegacyInnerImagePath);
        if (replaceClassicResourceExtension != v.replaceClassicResourceExtension.value)
            callbacks.markSummaryChanged(SummaryDirtyKind::LegacyResourceExtension);
    }
    if (showItemNumberOfSceneAndEventView != v.showItemNumberOfSceneAndEventView.value) invokeOptionalCallback(callbacks.refMenuCardAndBgImageList);
    const bool lastPartyChanged = !equalExecutionParty(lastExecutedParty, v.lastExecutedParty.value);
    if (lastPartyChanged || !equalExecutionParties(executedParties, v.executedParties.value) || executedPartiesMax != v.executedPartiesMax.value) invokeOptionalCallback(callbacks.refExecutedParties);
    if (executedPartiesMax != v.executedPartiesMax.value) invokeOptionalCallback(callbacks.refPartyHistoryMax);
    const bool skinSettingsChanged = settingsWithSkinTypes != v.settingsWithSkinTypes.value.values;
    if (skinSettingsChanged || bgImageSettings != v.bgImageSettings.value)
        invokeOptionalCallback(callbacks.refBgImageSettings);
    if (skinSettingsChanged ||
        oldSelections != v.standardSelections.value ||
        showCurrentValueOnTopAlways != v.showCurrentValueOnTopAlways.value)
        invokeOptionalCallback(callbacks.refStandardSelections);
    if (skinSettingsChanged ||
        oldKeyCodes != v.standardKeyCodes.value)
        invokeOptionalCallback(callbacks.refStandardKeyCodes);
    if (skinSettingsChanged || keyCodesByFeatures != v.keyCodesByFeatures.value)
        invokeOptionalCallback(callbacks.refKeyCodesByFeatures);
    if (skinSettingsChanged) {
        invokeOptionalCallback(callbacks.refSettingsWithSkinTypes);
        invokeOptionalCallback(callbacks.refElementOverrides);
    }
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::settingsdialog
