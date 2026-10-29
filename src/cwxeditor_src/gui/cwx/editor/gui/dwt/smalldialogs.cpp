#include "cwxeditor_src/gui/cwx/editor/gui/dwt/smalldialogs.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/filesync.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <type_traits>

namespace cwxeditor::cwx::editor::gui::dwt::smalldialogs {
namespace {

// 単独エディタでは、D版のビルド情報の項目を維持する。 提供元・バ
// ージョン・構成はDコンパイラーではなく、このC++版のビルドを
// 示す。 ALL は既存の APP_BUILD 表示を維持する。
String standaloneBuildInformation() {
    const String date = __DATE__;
    constexpr const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    String month = "??";
    for (std::size_t i = 0; i < 12U; ++i) {
        if (date.compare(0U, 3U, months[i]) == 0) {
            month = (i < 9U ? "0" : "") + std::to_string(i + 1U);
            break;
        }
    }
    String day = date.substr(4U, 2U);
    if (day[0] == ' ') day[0] = '0';
    String compiler;
#if defined(__clang__)
    compiler = "Clang " + std::to_string(__clang_major__) + "." +
               std::to_string(__clang_minor__) + "." + std::to_string(__clang_patchlevel__);
#elif defined(_MSC_VER)
    compiler = "Microsoft MSVC " + std::to_string(_MSC_VER);
#elif defined(__GNUC__)
    compiler = "GCC " + std::to_string(__GNUC__) + "." +
               std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__);
#else
    compiler = "Unknown C++ compiler";
#endif
    return "Build: " + date.substr(7U) + "-" + month + "-" + day + " " + __TIME__ +
           " " + ::cwxeditor::cwx::utils::DR + " (" +
           std::to_string(sizeof(void*) * 8U) + "-bit)\nCompiled by " + compiler;
}

Services chooseServices(const Props& prop, Services supplied,
                        const Services& callbacks) {
    auto hasGui = [](const Services& value) {
        return value.gui || value.dialogs || value.filesystem || value.shell;
    };
    if (hasGui(supplied)) return supplied;
    if (hasGui(callbacks)) return callbacks;
    return prop.images().services();
}

structs::DSize* smallDialogSavedSize(structs::DSize& saved) noexcept {
    // 単独版ではD版と同様にダイアログのサイズを保存する。 ALL は現在の推奨
    // サイズ・部品配置を維持し、両方とも同じ AbsDialog を共有する。
    return lib::editor_window_host_options().all_mode ? nullptr : &saved;
}

std::shared_ptr<lib::TextEdit> textControl(String value = {}, bool multiline = false) {
    auto control = std::make_shared<lib::TextEdit>(std::move(value));
    control->multiline = multiline;
    return control;
}

std::shared_ptr<lib::Label> labelControl(String value) {
    return std::make_shared<lib::Label>(std::move(value));
}

std::shared_ptr<lib::Button> buttonControl(String value) {
    return std::make_shared<lib::Button>(std::move(value));
}

Toggle checkControl(String value, bool checked = false) {
    auto control = std::make_shared<lib::CheckBox>(std::move(value));
    control->checked = checked;
    return control;
}

Combo comboControl() {
    return std::make_shared<lib::ComboBox>();
}

void addControl(lib::Container& area,
                const std::shared_ptr<lib::Widget>& control) {
    if (control) area.add(control);
}

template <typename IconProvider>
void addImportOptionLabel(lib::Container& area, Props& prop, String text,
                          IconProvider&& icon_provider) {
    // D版と同様、これらのラベルに分類アイコンを付ける。 す
    // べてのホストで同じ行ラベルの経路を使い、ALL では
    // 装飾画像だけを隠して、既存の行寸法と配置を維持する。
    const bool all_mode = lib::editor_window_host_options().all_mode;
    const auto icon = all_mode ? lib::ResourceId{} : icon_provider();
    const bool show_icon = !all_mode && icon != 0U;
    auto row = std::make_shared<lib::Panel>();
    auto layout = std::make_shared<lib::LinearLayout>(lib::Orientation::Horizontal);
    layout->spacing = show_icon ? 4.0F : 0.0F;
    row->set_layout(layout);

    auto image = std::make_shared<lib::ImageView>();
    image->border = false;
    image->image = show_icon ? icon : 0U;
    image->set_visible(show_icon);
    if (show_icon) image->design_size = prop.images().imageSize(icon);
    row->add(image);
    row->add(labelControl(std::move(text)));
    area.add(row);
}

String pathText(const std::filesystem::path& path) {
    return structs::pathToUtf8(path);
}

String fileName(StringView value) {
    return pathText(structs::pathFromUtf8(value).filename());
}

bool hasSummaryFile(StringView value,
                    const std::shared_ptr<lib::ArchiveBackend>& archives = {}) {
    std::error_code error;
    const auto path = structs::pathFromUtf8(value);
    if (std::filesystem::is_directory(path, error)) {
        return std::filesystem::exists(path / "Summary.wsm", error);
    }
    if (!std::filesystem::is_regular_file(path, error)) return false;
    if (fileName(value) == "Summary.wsm" ||
        (!lib::editor_window_host_options().all_mode &&
         utils::cfnmatch(fileName(value), "Summary.wsm")))
        return true;
    String ext = structs::pathToUtf8(path.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (archives && (ext == ".cab" || ext == ".lzh" || ext == ".zip" ||
                     ext == ".wsn")) {
        std::string archive_error;
        for (const auto& entry :
             archives->list(path, lib::ArchiveFormat::Automatic, archive_error)) {
            String filename = structs::pathToUtf8(entry.path.filename());
            std::transform(filename.begin(), filename.end(), filename.begin(),
                           [](const unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            if (!entry.directory && filename == "summary.wsm") return true;
        }
    }
    return false;
}

String displayNumber(std::uint64_t value) {
    return std::to_string(value);
}

struct ImportResourcePresentation {
    String text;
    lib::ResourceId icon{};
};

ImportResourcePresentation importResourcePresentation(
    Props& prop, const Summary* summary,
    const ::cwxeditor::cwx::path::CWXPath* source) {
    ImportResourcePresentation presentation;
    dutils::EditorContext context;
    context.prop = &prop;
    context.summary = summary;
    dutils::getSymbols(context, summary, source, true,
                       presentation.text, presentation.icon);
    return presentation;
}

std::size_t selectedIndex(const Combo& combo) {
    return combo && combo->selected_index ? *combo->selected_index
                                          : std::numeric_limits<std::size_t>::max();
}

void setSelected(const Combo& combo, std::size_t index) {
    if (combo && index < combo->items.size())
        (void)lib::select_combo_item(*combo, index, false);
}

template <typename T>
std::vector<std::uint64_t> sortedKeys(const T& table) {
    std::vector<std::uint64_t> keys;
    keys.reserve(table.size());
    for (const auto& [key, value] : table) {
        (void)value;
        keys.push_back(static_cast<std::uint64_t>(key));
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

template <typename Enum>
std::vector<Enum> enumValues(bool includeImages) {
    if constexpr (std::is_same_v<Enum, types::ImportTypeReference1>) {
        return {Enum::Rename, Enum::NoOverwrite, Enum::Overwrite, Enum::NoImport};
    } else if constexpr (std::is_same_v<Enum, types::ImportTypeReference2>) {
        return {Enum::Rename, Enum::NoImport};
    } else {
        if (includeImages)
            return {Enum::Exclude, Enum::Include, Enum::AsIs};
        return {Enum::Exclude};
    }
}

template <typename Enum>
String enumText(const Props& prop, Enum value) {
    if constexpr (std::is_same_v<Enum, types::ImportTypeReference1>) {
        return prop.requiredMsgs().importTypeReference1Name(value);
    } else if constexpr (std::is_same_v<Enum, types::ImportTypeReference2>) {
        return prop.requiredMsgs().importTypeReference2Name(value);
    } else {
        return prop.requiredMsgs().importTypeIncludedName(value);
    }
    return {};
}

template <typename T>
String objectName(const std::shared_ptr<T>& object) {
    if (!object) return {};
    if constexpr (requires { object->id(); object->name(); }) {
        return displayNumber(static_cast<std::uint64_t>(object->id())) +
               "." + object->name();
    } else if constexpr (requires { object->name(); }) {
        return object->name();
    } else {
        return {};
    }
}

void addChoice(const Combo& combo, String text, String value = {}) {
    if (!combo) return;
    combo->items.push_back({std::move(text), std::move(value), 0, true});
}

lib::Array<EvTemplate> copyEventTemplates(
        const lib::SharedSlice<EvTemplate>& templates) {
    // D版の setup での項目ごとの複製と同様、ToolsPane が編集用配列を所有する。
    lib::Array<EvTemplate> result;
    result.reserve(templates.size());
    for (const auto& value : templates) result.push_back(value);
    return result;
}

}  // namespace

CreateScenarioDialog::CreateScenarioDialog(Props& prop, Parent parent,
                                           const bool current_window,
                                           Callbacks callbacks,
                                           Services services)
    : absdialog::AbsDialog(
          prop, std::move(parent), true,
          current_window ? String(prop.requiredMsgs().dlgTitNewScenario.value)
                         : String(prop.requiredMsgs().dlgTitNewScenarioAtNewWin.value),
          current_window ? prop.images().load(StringView{"new.png"})
                         : prop.images().load(StringView{"new_new_win.png"}),
          true, &prop.flex().newScDlg, false, true, {}, false,
          chooseServices(prop, std::move(services), callbacks.services)),
      callbacks_(std::move(callbacks)), prop_(&prop), use_template_(current_window) {
    enterClose(true);
    if (lib::editor_window_host_options().all_mode)
        dialog()->focus_first_control_on_open = false;
}

void CreateScenarioDialog::enabledClassicDir() {
    const bool enabled = legacy();
    if (directory_) directory_->set_enabled(enabled);
    if (directory_reference_) directory_reference_->set_enabled(enabled);
    if (create_scenario_directory_) create_scenario_directory_->set_enabled(enabled);
    // ALL ではClassic形式（旧形式の出力先）が選択されている
    // 場合、 ファイル管理ソフトで開くフォルダーボタンを表示しない。
    if (directory_open_)
        directory_open_->set_visible(
            !(lib::editor_window_host_options().all_mode && enabled));
}

void CreateScenarioDialog::refSkinVal() {
    const auto index = selectedIndex(skin_combo_);
    if (index >= skin_types_.size()) {
        selected_skin_ = {};
        return;
    }
    selected_skin_ = skin_types_[index];
}

void CreateScenarioDialog::refClassic() {
    refSkinVal();
    const bool useSkin = base_skin_ && base_skin_->checked;
    if (skin_combo_) skin_combo_->set_enabled(useSkin);
    if (template_combo_)
        template_combo_->set_enabled(base_template_ &&
                                     base_template_->checked &&
                                     use_template_ && !template_table_.empty());
    classic_ = useSkin && selected_skin_.path.empty() &&
               selected_skin_.type.empty();
    if (base_template_ && base_template_->checked) {
        const auto index = selectedIndex(template_combo_);
        if (const auto found = template_table_.find(index);
            found != template_table_.end()) {
            const auto path = found->second.path;
            classic_ = hasSummaryFile(path, services().archives);
            if (!path.empty()) classic_templates_[path] = classic_;
        } else {
            classic_ = false;
        }
    }
    enabledClassicDir();
}

void CreateScenarioDialog::refreshSkins() {
    if (!skin_combo_) return;
    const auto order_revision = skin_combo_->item_order_revision;
    std::vector<SkinChoice> skin_types;
    std::vector<lib::ChoiceItem> items;
    skin_types.reserve(all_skin_types_.size());
    items.reserve(all_skin_types_.size() + 1U);
    std::optional<std::size_t> selected;
    for (const auto& choice : all_skin_types_) {
        if (skin_inc_search_ && !skin_inc_search_->match(choice.name)) continue;
        const auto index = skin_types.size();
        skin_types.push_back(choice);
        items.push_back({choice.name + "(" + choice.type + ")", {}, 0, true});
        if (choice.type == selected_skin_.type &&
            choice.name == selected_skin_.name &&
            choice.path == selected_skin_.path)
            selected = index;
    }
    items.push_back({prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().classic.value),
                     {}, 0, true});
    if (classic_) selected = items.size() - 1U;
    const std::weak_ptr<lib::ComboBox> weak = skin_combo_;
    if (!lib::replace_combo_items(skin_combo_, std::move(items), selected, {},
            [weak, order_revision] {
                const auto target = weak.lock();
                return target && target->item_order_revision == order_revision;
            })) return;
    skin_types_ = std::move(skin_types);
}

void CreateScenarioDialog::selectDirectory() {
    std::optional<String> selected;
    const auto current = directory_ ? directory_->text : directory_value_;
    if (callbacks_.selectDirectory) {
        selected = callbacks_.selectDirectory("New classic scenario", current);
    } else if (services().file_picker) {
        const auto title = prop_ ? String(prop_->requiredMsgs().newClassicDir.value)
                                 : String{"New classic scenario"};
        const auto description = prop_
                                     ? String(prop_->requiredMsgs().newClassicDirDesc.value)
                                     : String{};
        auto request = dutils::makeDirectoryPickerRequest(
            title, description, structs::pathFromUtf8(current));
        const auto picked = services().file_picker->select_directory(request);
        if (picked.succeeded() && !picked.selections.empty())
            if (const auto path=picked.selections.front().native_path()) selected = pathText(*path);
    }
    if (!selected) return;
    directory_value_ = *selected;
    if (directory_) directory_->text = directory_value_;
}

void CreateScenarioDialog::selectDirectoryAsync() {
    if (callbacks_.selectDirectory) {
        selectDirectory();
        return;
    }
    const auto file_picker = services().file_picker;
    if (!file_picker) return;
    const auto current = directory_ ? directory_->text : directory_value_;
    const auto title = prop_ ? String(prop_->requiredMsgs().newClassicDir.value)
                             : String{"New classic scenario"};
    const auto description = prop_
                                 ? String(prop_->requiredMsgs().newClassicDirDesc.value)
                                 : String{};
    auto request = dutils::makeDirectoryPickerRequest(
        title, description, structs::pathFromUtf8(current));
    const std::weak_ptr<lib::TextEdit> directory = directory_;
    (void)file_picker->select_directory_async(
        std::move(request), [directory](lib::FilePickerResult picked) {
            if (!picked.succeeded() || picked.selections.empty()) return;
            const auto path = picked.selections.front().native_path();
            const auto control = directory.lock();
            if (path && control) control->text = pathText(*path);
        });
}

String CreateScenarioDialog::name() const {
    if (name_ && !name_->text.empty()) return name_->text;
    if (!name_value_.empty()) return name_value_;
    return prop_ ? String(prop_->var().newScenarioName.value) : String{};
}

String CreateScenarioDialog::skinType() const {
    return selected_skin_.type;
}

String CreateScenarioDialog::skinName() const {
    return selected_skin_.name;
}

String CreateScenarioDialog::classicDir() const {
    if (!prop_) return {};
    auto path = structs::pathFromUtf8(
        prop_->toAppAbs(directory_ ? StringView(directory_->text)
                                  : StringView(directory_value_)));
    const bool createDirectory = create_scenario_directory_
                                     ? create_scenario_directory_->checked
                                     : static_cast<bool>(prop_->var().createScenarioDir.value);
    if (createDirectory) {
        auto component = name();
        // プラットフォームの選択はホストのサービスに任せ、OS固有のGUI実装の
        // 分岐では行わない。 CMakeの単一GUIの検査でもこの制約を守る。
        const auto platform = services().system ? services().system->platform()
            : (services().gui ? services().gui->platform() : lib::Platform::Headless);
        if (platform != lib::Platform::Windows &&
            platform != lib::Platform::Headless &&
            platform != lib::Platform::Automatic) {
            // D版のPOSIX構成と同様、NULと / だけを置き換える。 ALL
            // が変えるのは表示であり、ネイティブのファイル名規則ではない。
            for (auto& character : component)
                if (character == '\0' || character == '/') character = '_';
        } else {
            // D版のWindowsの規則と同様、末尾のドットも置き換える。
            component = utils::toFileName(std::move(component));
        }
        path /= component;
        path = utils::createNewFileName(path, true);
    }
    return pathText(path.lexically_normal());
}

String CreateScenarioDialog::createClassicDir(Props& prop, Parent parent,
                                               Callbacks callbacks) {
    (void)parent;
    // D版が一つの DirectoryDialog を再利用するのと同様、最後に拒否した選択を選択画面に残す。
    String retry_directory = prop.var().scenarioPath.value;
    for (;;) {
        std::optional<String> selected;
        if (callbacks.selectDirectory) {
            selected = callbacks.selectDirectory(
                prop.requiredMsgs().newClassicDir.value, retry_directory);
        } else {
            const auto services = chooseServices(prop, {}, callbacks.services);
            if (services.file_picker) {
                auto request = dutils::makeDirectoryPickerRequest(
                    prop.requiredMsgs().newClassicDir.value,
                    prop.requiredMsgs().newClassicDirDesc.value,
                    structs::pathFromUtf8(retry_directory));
                const auto picked = services.file_picker->select_directory(request);
                if (picked.succeeded() && !picked.selections.empty())
                    if (const auto path=picked.selections.front().native_path()) selected = pathText(*path);
            }
        }
        if (!selected || selected->empty()) return {};
        const auto directory = structs::pathFromUtf8(*selected);
        std::error_code error;
        const bool notEmpty = std::filesystem::is_directory(directory, error) &&
                              std::filesystem::directory_iterator(directory, error) !=
                                  std::filesystem::directory_iterator{};
        if (notEmpty) {
            const auto services = chooseServices(prop, {}, callbacks.services);
            if (!services.dialogs) return {};
            lib::MessageDialogRequest request;
            request.title = prop.requiredMsgs().dlgTitQuestion.value;
            request.message = utils::tryFormat(prop.requiredMsgs().notEmptyDir.value, *selected);
            request.severity = lib::MessageSeverity::Question;
            request.buttons = lib::MessageButtons::OKCancel;
            if (services.dialogs->message(request) != lib::MessageResult::OK) {
                retry_directory = *selected;
                continue;
            }
        }
        prop.var().scenarioPath = pathText(directory.parent_path());
        return *selected;
    }
}

void CreateScenarioDialog::setup(Container& area) {
    auto rootLayout = std::make_shared<lib::GridLayout>();
    rootLayout->columns = 1;
    rootLayout->spacing = 6.0F;
    rootLayout->padding = {8.0F, 5.0F, 5.0F, 4.0F};
    area.set_layout(rootLayout);

    const auto makeGroup = [](String title, std::size_t row,
                              std::size_t columns) {
        auto group = std::make_shared<lib::GroupBox>();
        group->title = std::move(title);
        auto layout = std::make_shared<lib::GridLayout>();
        layout->columns = columns;
        layout->spacing = 6.0F;
        layout->padding = {8.0F, 12.0F, 8.0F, 6.0F};
        group->set_layout(layout);
        group->set_layout_item({.row = row, .column = 0});
        return group;
    };

    auto nameGroup = makeGroup(prop_->requiredMsgs().scenarioName.value, 0, 1);
    nameGroup->set_layout_item({.row = 0, .column = 0,
                                .preferred = {300.0F, 48.0F}});
    // D版と同様、名前欄は空で開始し、名前がないままOKを押
    // したときだけ newScenarioName を補う。
    name_ = textControl(String{});
    name_->set_layout_item({.row = 0, .column = 0,
                            .preferred = {300.0F, 22.0F}, .grow = 1.0F});
    nameGroup->add(name_);
    area.add(nameGroup);

    auto initialGroup = makeGroup(prop_->requiredMsgs().initialize.value, 1, 2);
    initialGroup->set_layout_item({.row = 1, .column = 0,
                                   .preferred = {300.0F, 100.0F}});
    base_skin_ = std::make_shared<lib::RadioButton>(prop_->requiredMsgs().type.value);
    base_skin_->group = "scenario-base";
    base_skin_->checked = true;
    base_template_ =
        std::make_shared<lib::RadioButton>(prop_->requiredMsgs().scenarioTemplate.value);
    base_template_->group = "scenario-base";
    skin_combo_ = comboControl();
    template_combo_ = comboControl();
    base_skin_->set_layout_item({.row = 0, .column = 0,
                                 .preferred = {72.0F, 22.0F}});
    skin_combo_->set_layout_item({.row = 0, .column = 1,
                                  .preferred = {222.0F, 24.0F}, .grow = 1.0F});
    base_template_->set_layout_item({.row = 1, .column = 0,
                                     .preferred = {72.0F, 22.0F}});
    template_combo_->set_layout_item({.row = 1, .column = 1,
                                      .preferred = {222.0F, 24.0F}, .grow = 1.0F});
    initialGroup->add(base_skin_);
    initialGroup->add(skin_combo_);
    initialGroup->add(base_template_);
    initialGroup->add(template_combo_);

    for (const auto& [path, candidate] : dskin::skinTable(*prop_)) {
        if (!candidate) continue;
        skin_types_.push_back({candidate->type(), candidate->name(), path});
    }
    std::sort(skin_types_.begin(), skin_types_.end(),
              [&](const SkinChoice& left, const SkinChoice& right) {
                  if (prop_->var().logicalSort.value) {
                      const auto byName = utils::ncmp(left.name, right.name);
                      if (byName != 0) return byName < 0;
                      const auto byType = utils::ncmp(left.type, right.type);
                      if (byType != 0) return byType < 0;
                  } else {
                      if (left.name != right.name) return left.name < right.name;
                      if (left.type != right.type) return left.type < right.type;
                  }
                  return utils::fncmp(left.path, right.path) < 0;
              });
    std::optional<std::size_t> lastIndex;
    const auto lastType = String(prop_->var().lastSkinType.value);
    const auto lastName = String(prop_->var().lastSkinName.value);
    for (std::size_t index = 0; index < skin_types_.size(); ++index) {
        const auto& choice = skin_types_[index];
        addChoice(skin_combo_, choice.name + "(" + choice.type + ")");
        if (choice.type == lastType && choice.name == lastName)
            lastIndex = index;
    }
    const auto defaultType = String(prop_->var().defaultSkin.value);
    const auto defaultName = String(prop_->var().defaultSkinName.value);
    std::optional<std::size_t> defaultIndex;
    for (std::size_t index = 0; index < skin_types_.size(); ++index) {
        if (skin_types_[index].type == defaultType &&
            skin_types_[index].name == defaultName) {
            defaultIndex = index;
            break;
        }
    }
    if (skin_types_.empty()) {
        skin_types_.push_back({defaultType, defaultName, {}});
        addChoice(skin_combo_, defaultName + "(" + defaultType + ")");
        defaultIndex = 0U;
    }
    addChoice(skin_combo_,
              prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().classic.value));
    const auto classicIndex = skin_combo_->items.size() - 1U;
    const auto medievalClassic = std::find_if(skin_types_.begin(), skin_types_.end(),
        [](const SkinChoice& choice) {
            return choice.type == "MedievalFantasy" && choice.name == "Classic";
        });
    std::size_t skinIndex{};
    // 利用可能な設定済みの選択を、組み込み先の代替値より優先する
    // 。 未設定時・代替選択の方針は、ここでは意図的に変えない。
    if (prop_->var().targetVersion.value == "CardWirthPy" && lastIndex)
        skinIndex = *lastIndex;
    else if (defaultIndex)
        skinIndex = *defaultIndex;
    else if (medievalClassic != skin_types_.end())
        skinIndex = static_cast<std::size_t>(medievalClassic - skin_types_.begin());
    else if (prop_->var().targetVersion.value != "CardWirthPy")
        skinIndex = defaultIndex.value_or(classicIndex);
    else if (lastIndex)
        skinIndex = *lastIndex;
    else
        skinIndex = defaultIndex.value_or(0U);
    setSelected(skin_combo_, skinIndex);
    selected_skin_ = skinIndex < skin_types_.size()
                         ? skin_types_[skinIndex]
                         : SkinChoice{};
    classic_ = skinIndex == classicIndex;
    all_skin_types_ = skin_types_;

    incsearch::IncSearchSettings searchSettings;
    searchSettings.startIncrementalSearchWhenKeyDown =
        prop_->var().startIncrementalSearchWhenKeyDown.value;
    const auto* search_prop = prop_;
    searchSettings.startIncrementalSearchWhenKeyDownProvider = [search_prop] {
        return search_prop && search_prop->var().startIncrementalSearchWhenKeyDown.value;
    };
    searchSettings.incrementalSearchBoxWidth =
        prop_->var().incrementalSearchBoxWidth.value;
    searchSettings.incrementalSearchType =
        prop_->var().incrementalSearchType.value;
    searchSettings.containsLabel = prop_->requiredMsgs().incSearchContains.value;
    searchSettings.wildcardLabel = prop_->requiredMsgs().incSearchWildcard.value;
    searchSettings.regexLabel = prop_->requiredMsgs().incSearchRegex.value;
    skin_inc_search_ = std::make_shared<incsearch::IncSearch>(
        services(), skin_combo_.get(), incsearch::IncSearch::Enabled{},
        std::vector<incsearch::AdditionMatcher>{}, std::move(searchSettings));
    skin_inc_search_->modEvent.push_back([this] { refreshSkins(); });
    skin_menu_ = std::make_shared<lib::Menu>(true);
    skin_menu_->items.push_back(lib::MenuItem{
        .text = prop_->requiredMsgs().menuText(types::MenuID::IncSearch),
        .command = "incremental-search",
        .icon = prop_->images().load(StringView{"inc_search.png"}),
    });
    skin_menu_->on_command = [this](StringView command) {
        if (command == "incremental-search" && skin_inc_search_)
            skin_inc_search_->startIncSearch();
    };
    skin_combo_->on_pointer = [this](lib::PointerEvent& event) {
        if (event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up || !services().gui)
            return;
        services().gui->show_context_menu(skin_menu_, skin_combo_, event.position);
        event.handled = true;
    };
    skin_combo_->on_key_down = [this](lib::KeyEvent& event) {
        if (skin_inc_search_ &&
            skin_inc_search_->handleHostKeyDown(event, skin_combo_.get()))
            event.handled = true;
    };
    skin_combo_->on_text_input = [this](lib::TextInputEvent& event) {
        if (skin_inc_search_)
            event.handled = skin_inc_search_->handleHostTextInput(event, skin_combo_.get());
    };

    for (std::size_t index = 0; index < prop_->var().scenarioTemplates.value.size(); ++index) {
        const auto& value = prop_->var().scenarioTemplates.value[index];
        template_table_[index] = value;
        addChoice(template_combo_,
                  utils::tryFormat(prop_->requiredMsgs().templateDesc.value,
                                   value.name, value.path),
                  value.path);
    }
    if (template_combo_->items.empty())
        addChoice(template_combo_,
                  prop_->requiredMsgs().defaultSelection(prop_->requiredMsgs().noTemplate.value));
    std::size_t templateIndex = 0;
    for (const auto& [index, value] : template_table_) {
        if (utils::cfnmatch(value.path, prop_->var().defaultScenarioTemplate.value)) {
            templateIndex = index;
            break;
        }
    }
    setSelected(template_combo_, templateIndex);
    base_template_->checked = use_template_ && !template_table_.empty() &&
                              static_cast<bool>(prop_->var().defaultIsTemplate.value);
    base_skin_->checked = !base_template_->checked;
    base_template_->set_enabled(use_template_ && !template_table_.empty());

    auto warning = labelControl(prop_->requiredMsgs().notClassicWarning.value);
    warning->alignment = lib::TextAlignment::End;
    warning->set_layout_item({.row = 2, .column = 0, .column_span = 2,
                              .preferred = {300.0F, 18.0F}});
    initialGroup->add(warning);
    area.add(initialGroup);

    auto directoryGroup = makeGroup(prop_->requiredMsgs().createClassicDir.value, 2, 3);
    directoryGroup->set_layout_item({.row = 2, .column = 0,
                                     .preferred = {300.0F, 70.0F}});
    directory_ = textControl(String(prop_->var().scenarioPath.value));
    directory_value_ = directory_->text;
    create_scenario_directory_ = checkControl(
        prop_->requiredMsgs().createScenarioNameDir.value,
        static_cast<bool>(prop_->var().createScenarioDir.value));
    directory_reference_ = buttonControl("...");
    directory_open_ = buttonControl({});
    directory_open_->icon =
        prop_->images().load(lib::StringView{"folder.png"});
    directory_open_->set_tooltip(
        prop_->requiredMsgs().menuText(types::MenuID::OpenDir));
    directory_->set_layout_item({.row = 0, .column = 0,
                                 .preferred = {234.0F, 22.0F}, .grow = 1.0F});
    directory_reference_->set_layout_item({.row = 0, .column = 1,
                                           .preferred = {26.0F, 23.0F}});
    directory_open_->set_layout_item({.row = 0, .column = 2,
                                      .preferred = {28.0F, 23.0F}});
    create_scenario_directory_->set_layout_item(
        {.row = 1, .column = 0, .column_span = 3,
         .preferred = {300.0F, 20.0F}});
    directoryGroup->add(directory_);
    directoryGroup->add(directory_reference_);
    directoryGroup->add(directory_open_);
    directoryGroup->add(create_scenario_directory_);
    // D版のClassic出力グループと同様、有効なパス欄にフォルダー
    // のドロップを受け付ける。 ALL の別のGUI方針は変えない。
    if (!lib::editor_window_host_options().all_mode)
        dutils::setupDropFile(directoryGroup, directory_, dutils::dropDir, {}, services());
    area.add(directoryGroup);

    base_skin_->on_changed = [this](bool checked) {
        if (checked && base_template_) base_template_->checked = false;
        refClassic();
    };
    base_template_->on_changed = [this](bool checked) {
        if (checked && base_skin_) base_skin_->checked = false;
        refClassic();
    };
    skin_combo_->on_selection_changed = [this](std::optional<std::size_t>) {
        refClassic();
    };
    template_combo_->on_selection_changed = [this](std::optional<std::size_t>) {
        refClassic();
    };
    directory_->on_changed = [this](StringView value) {
        directory_value_.assign(value.data(), value.size());
    };
    create_scenario_directory_->on_changed = [this](const bool value) {
        if (prop_) prop_->var().createScenarioDir = value;
    };
    directory_reference_->on_click = [this] {
        const auto gui = services().gui;
        // Webだけでなく、すべての ALL ホストで同じ非同期の選択画面を使う。
        if (gui && lib::editor_window_host_options().all_mode)
            selectDirectoryAsync();
        else
            selectDirectory();
    };
    directory_open_->on_click = [this] {
        const StringView raw = directory_ ? StringView(directory_->text)
                                          : StringView(directory_value_);
        const auto path = dutils::openButtonDirectory(prop_, raw);
        if (!path) return;
        if (callbacks_.openPath) callbacks_.openPath(*path);
        else if (services().shell) {
            std::string error;
            (void)services().shell->open_path(
                structs::pathFromUtf8(*path), error);
        }
    };
    // AbsDialog はウィンドウの有効化時にネイティブの操作部品を構築するため、デ
    // スクトップではその時点でフォーカスを設定する。 モバイルブラウザーは実際のタップで
    // しかソフトキーボードを開かず、有効化時のフォーカスはユーザー操作とは見なされない。
    dialog()->on_activated = [this] {
        if (initial_focus_done_) return;
        initial_focus_done_ = true;
        if (lib::editor_window_host_options().all_mode)
            return;
        if (name_ && services().gui) services().gui->focus(name_);
    };
    refClassic();
    // D版・SWTのコンパクトなクライアントサイズは、子モデル全体と
    // 所有者が揃うまで設定を遅らせる。 派生クラスの構築中に境界を通
    // 知すると、構築完了前にネイティブ側の同期が始まる場合がある。
    dialog()->set_bounds({0.0F, 0.0F, 323.0F, 273.0F});
}

bool CreateScenarioDialog::close(bool ok, bool& cancel) {
    cancel = false;
    if (!ok || !prop_) return ok;
    name_value_ = name_ ? name_->text : String{};
    if (name_value_.empty()) name_value_ = prop_->var().newScenarioName.value;
    refSkinVal();
    if (!legacy()) {
        prop_->var().lastSkinType = selected_skin_.type;
        prop_->var().lastSkinName = selected_skin_.name;
    }
    directory_value_ = directory_ ? directory_->text : directory_value_;
    // 入力した保存先は、シナリオの反映に成功するまでローカルで保持する。
    // ディレクトリ作成のチェック設定だけは、意図的に即時反映のままにする。
    if (create_scenario_directory_)
        prop_->var().createScenarioDir = create_scenario_directory_->checked;

    const auto destination = classicDir();
    if (legacy()) {
        std::error_code error;
        const auto directory = structs::pathFromUtf8(destination);
        const bool notEmpty = std::filesystem::is_directory(directory, error) &&
            std::filesystem::directory_iterator(directory, error) !=
                std::filesystem::directory_iterator{};
        if (notEmpty) {
            if (!services().dialogs) {
                cancel = true;
                return false;
            }
            lib::MessageDialogRequest request;
            request.title = prop_->requiredMsgs().dlgTitQuestion.value;
            request.message = utils::tryFormat(prop_->requiredMsgs().notEmptyDir.value,
                                               destination);
            request.severity = lib::MessageSeverity::Question;
            request.buttons = lib::MessageButtons::OKCancel;
            if (services().dialogs->message(request) != lib::MessageResult::OK) {
                cancel = true;
                return false;
            }
        }
    }

    if (base_template_ && base_template_->checked) {
        const auto index = selectedIndex(template_combo_);
        const auto found = template_table_.find(index);
        if (found != template_table_.end()) {
            const auto& templateValue = found->second;
            std::error_code error;
            const auto templatePath = structs::pathFromUtf8(templateValue.path);
            const bool templateExists = !templateValue.path.empty() &&
                std::filesystem::exists(templatePath, error);
            std::unique_ptr<summary::FileSync> localSync;
            auto* sync = callbacks_.sync;
            if (!sync) {
                localSync = std::make_unique<summary::FileSync>(prop_->storage());
                localSync->start();
                sync = localSync.get();
            }
            if (!templateExists) {
                std::filesystem::create_directories(structs::pathFromUtf8(destination), error);
                from_template_ = std::make_shared<Summary>(
                    name_value_, skinType(), skinName(), destination, false, true,
                    prop_->storage());
            } else if (std::filesystem::is_directory(templatePath, error) &&
                       !std::filesystem::exists(templatePath / "Summary.wsm", error) &&
                       !std::filesystem::exists(templatePath / "Summary.xml", error)) {
                // D版と同様、コピー中は setWaitCursors(topShell(getShell())) を適用する。
                dutils::WaitCursorScope wait_cursors(dutils::topShell(dialog()));
                const auto selectedSkin = dskin::findSkin2(*prop_, skinType(), skinName());
                if (selectedSkin && sync) {
                    const auto& newArea = prop_->var().newAreaName.value;
                    from_template_ = Summary::createScenario(
                        prop_->parent(), prop_->tempPath(), name_value_, *selectedSkin,
                        !newArea.empty(), newArea,
                        ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                            prop_->bgImagesDefault(selectedSkin->type())),
                        prop_->var().saveSkinName.value, *sync);
                    if (from_template_) {
                        const auto target = structs::pathFromUtf8(
                            from_template_->scenarioPath());
                        String cyclePath;
                        String protectivePath;
                        // ALL/standalone共通で、DのcopyAllと同じくディレクトリ参照先を
                        // テンプレート外も含めて要求された別名ごとにコピーする。
                        // 祖先だけを追跡し、同じ参照先を持つ兄弟の別名も両方コピーする。
                        // 意図的な差: Dの無制限再帰に対する既存の循環・コピー先再帰の
                        // 防止を、この共有経路でも維持する。
                        const auto isWithin = [](const std::filesystem::path& child,
                                                 const std::filesystem::path& parent) {
                            const auto relative = child.lexically_relative(parent);
                            if (relative.empty()) return child == parent;
                            const auto first = relative.begin();
                            return !relative.is_absolute() && first != relative.end() &&
                                   *first != "..";
                        };
                        const auto sourceRoot = std::filesystem::canonical(templatePath, error);
                        const auto destinationRoot = error
                            ? std::filesystem::path{}
                            : std::filesystem::canonical(target, error);
                        std::vector<std::filesystem::path> ancestors;
                        const auto copyTree = [&](const auto& self,
                                                  const std::filesystem::path& source,
                                                  const std::filesystem::path& destination) -> void {
                            if (error) return;
                            const bool sourceExists = std::filesystem::exists(source, error);
                            if (error || !sourceExists) return;
                            const auto sourceStatus = std::filesystem::status(source, error);
                            if (error) return;
                            if (std::filesystem::is_directory(sourceStatus)) {
                                const auto resolved = std::filesystem::canonical(source, error);
                                if (error) return;
                                if (isWithin(resolved, destinationRoot)) {
                                    // 意図的な差: 参照先が出力先へ戻る場合、Dのように
                                    // 出力先を再帰列挙し続けず、既存の保護エラーにする。
                                    protectivePath = pathText(source);
                                    error = std::make_error_code(std::errc::invalid_argument);
                                    return;
                                }
                                for (const auto& ancestor : ancestors) {
                                    if (std::filesystem::equivalent(resolved, ancestor, error)) {
                                        cyclePath = pathText(source);
                                        error = std::make_error_code(
                                            std::errc::too_many_symbolic_link_levels);
                                        return;
                                    }
                                    if (error) return;
                                }
                                ancestors.push_back(resolved);
                                std::filesystem::create_directories(destination, error);
                                if (error) {
                                    ancestors.pop_back();
                                    return;
                                }
                                std::filesystem::directory_iterator current(source, error), end;
                                if (error) {
                                    ancestors.pop_back();
                                    return;
                                }
                                while (!error && current != end) {
                                    const auto entry = *current;
                                    self(self, entry.path(),
                                         destination / entry.path().filename());
                                    if (error) break;
                                    current.increment(error);
                                }
                                ancestors.pop_back();
                                return;
                            }
                            if (!std::filesystem::is_regular_file(sourceStatus)) return;
                            std::filesystem::create_directories(destination.parent_path(), error);
                            if (error) return;
                            std::filesystem::copy_file(
                                source, destination,
                                std::filesystem::copy_options::overwrite_existing, error);
                        };
                        if (!error && sourceRoot == destinationRoot) {
                            // Dと同じくコピー元とコピー先が同一なら何もしない。
                        } else if (!error && isWithin(destinationRoot, sourceRoot)) {
                            // テンプレート内に作成した出力先の再帰列挙も防ぐ。
                            // 既存の保護エラーを通知し、出力先を無限に入れ子にしない。
                            protectivePath = pathText(target);
                            error = std::make_error_code(std::errc::invalid_argument);
                        } else if (!error) {
                            copyTree(copyTree, templatePath, target);
                        }
                        if (error) {
                            const auto diagnostic = error.message() +
                                (cyclePath.empty() ? String{} :
                                    "\nDirectory symlink cycle: " + cyclePath) +
                                (protectivePath.empty() ? String{} :
                                    "\nCopy stopped to prevent recursive source/destination overlap: " + protectivePath);
                            from_template_.reset();
                            // 素材コピーの失敗は、シナリオローダーの代替処理とは
                            // 別に扱う。 要求されたファイルをコピーできなかった
                            // テンプレートの代わりに、空のシナリオを反映しない。
                            if (services().dialogs) {
                                lib::MessageDialogRequest request;
                                request.title = prop_->requiredMsgs().dlgTitError.value;
                                request.message = utils::tryFormat(
                                    prop_->requiredMsgs().fileCopyError.value, templateValue.path) +
                                    "\n" + diagnostic;
                                request.severity = lib::MessageSeverity::Error;
                                request.buttons = lib::MessageButtons::OK;
                                (void)services().dialogs->message(request);
                            }
                            cancel = true;
                            return false;
                        }
                    }
                }
            } else {
                // D版と同様、読み込み中は setWaitCursors(topShell(getShell())) を適用する。
                dutils::WaitCursorScope wait_cursors(dutils::topShell(dialog()));
                try {
                    summary::LoadOption option;
                    option.cardOnly = false;
                    option.textOnly = false;
                    option.doubleIO = prop_->var().doubleIO.value;
                    option.expandXMLs = prop_->var().expandXMLs.value;
                    option.numberStepToVariantThreshold =
                        prop_->var().numberStepToVariantThreshold.value;
                    const auto defaultSkin = dskin::findSkin2(
                        *prop_, prop_->var().defaultSkin.value,
                        prop_->var().defaultSkinName.value);
                    lib::Array<String> errorFiles;
                    from_template_ = Summary::loadScenarioFromFile(
                        prop_->parent(), option, errorFiles, templateValue.path,
                        prop_->tempPath(), defaultSkin.get(),
                        [destination] { return destination; }, {}, {}, {},
                        name_value_, prop_->storage());
                } catch (const std::exception&) {
                    // C++版ローダーは、D版の SummaryException 以
                    // 外の標準ファイルシステム・XML例外を返す場合がある。 このUI
                    // 境界では、同じ復旧可能な新規シナリオへの代替動作を維持する。
                    from_template_.reset();
                }
            }
            if (!from_template_) {
                const auto selectedSkin = dskin::findSkin2(*prop_, skinType(), skinName());
                if (selectedSkin && sync) {
                    const auto& newArea = prop_->var().newAreaName.value;
                    from_template_ = Summary::createScenario(
                        prop_->parent(), prop_->tempPath(), name_value_, *selectedSkin,
                        !newArea.empty(), newArea,
                        ::cwxeditor::cwx::editor::gui::dwt::dprops::arraySnapshot(
                            prop_->bgImagesDefault(selectedSkin->type())),
                        prop_->var().saveSkinName.value, *sync);
                }
            }
            if (from_template_)
                from_template_->setBaseParams(
                    name_value_, String(prop_->var().defaultAuthor.value));
            prop_->var().defaultScenarioTemplate = templateValue.path;
            prop_->var().defaultIsTemplate = true;
        }
    } else {
        prop_->var().defaultIsTemplate = false;
    }
    return true;
}

VersionDialog::VersionDialog(Props& prop, Parent parent, Callbacks callbacks,
                             Services services)
    : absdialog::AbsDialog(prop, std::move(parent), true, prop.requiredMsgs().dlgTitVersion.value,
                           prop.images().menu(types::MenuID::VersionInfo),
                           false, nullptr, false, false, {}, false,
                           chooseServices(prop, std::move(services), callbacks.services)),
      callbacks_(std::move(callbacks)), prop_(&prop) {
    enterClose(true);
    firstFocusIsOK(true);
}

void VersionDialog::setup(Container& area) {
    auto icon = std::make_shared<lib::ImageView>();
    icon->image = prop_ ? prop_->images().standardRegistry().largeIcon() : 0;
    addControl(area, icon);
    addControl(area, labelControl(prop_->requiredMsgs().application.value + " / " + versioninfo::APP_VERSION));
    // 既存の行位置を維持する。D版のWebサイトへのリン
    // クは、 新しい行を挿入せず、説明と同じ行に置く。
    auto description = std::make_shared<lib::Panel>();
    auto row = std::make_shared<lib::LinearLayout>();
    row->orientation = lib::Orientation::Horizontal;
    description->set_layout(row);
    addControl(*description, labelControl(prop_->requiredMsgs().appDesc.value));
    auto site = std::make_shared<lib::Link>(versioninfo::APP_WEB_SITE_URI);
    site->target = versioninfo::APP_WEB_SITE_URI;
    site->on_activate = [this] {
        if (callbacks_.openLink) callbacks_.openLink(versioninfo::APP_WEB_SITE_URI);
        else if (services().shell) {
            std::string error;
            (void)services().shell->open_url(versioninfo::APP_WEB_SITE_URI, error);
        }
    };
    addControl(*description, site);
    addControl(area, description);
    auto build = textControl(lib::editor_window_host_options().all_mode
                                 ? ::cwxeditor::cwx::utils::APP_BUILD
                                 : standaloneBuildInformation(), true);
    build->read_only = true;
    build->tab_width = prop_->var().tabs.value;
    dmenu::Commons common;
    common.services = services();
    common.presentation.textTabs = prop_->var().tabs.value;
    common.presentation.menuText = [prop = prop_](auto id) { return prop->buildMenu(id); };
    common.presentation.menuImage = [prop = prop_](auto id) { return prop->images().menu(id); };
    text_menu_ = dmenu::createTextMenu(common, dmenu::TMM(build));
    addControl(area, build);
}

ErrorDialog::ErrorDialog(Props& prop, Parent parent, String description,
                         Callbacks callbacks, Services services)
    : absdialog::AbsDialog(
          prop, std::move(parent), false, prop.requiredMsgs().dlgTitError.value, 0, true, nullptr, false,
          false,
          {absdialog::ButtonInfo{
              prop.requiredMsgs().shutdown.value, [action = callbacks.shutdown] {
                  if (action) action();
              }}},
          false, chooseServices(prop, std::move(services), callbacks.services)),
      callbacks_(std::move(callbacks)), prop_(&prop),
      description_(std::move(description)) {
    auto bounds = dialog()->bounds();
    bounds.width = 600.0F;
    bounds.height = 400.0F;
    dialog()->set_bounds(bounds);
}

void ErrorDialog::setup(Container& area) {
    const auto logPath = ::cwxeditor::cwx::utils::debugLog;
    auto heading = std::make_shared<lib::Label>(::cwxeditor::cwx::utils::tryFormat(
        prop_->requiredMsgs().unknownError.value, logPath));
    heading->wrap = true;
    addControl(area, heading);
    auto link = std::make_shared<lib::Link>(logPath);
    link->target = logPath;
    link->on_activate = [this, logPath] {
        if (callbacks_.openPath) callbacks_.openPath(logPath);
    };
    addControl(area, link);
    auto message = textControl(description_, true);
    message->read_only = true;
    dmenu::Commons common;
    common.services = services();
    common.presentation.menuText = [prop = prop_](auto id) {
        return prop->buildMenu(id);
    };
    common.presentation.menuImage = [prop = prop_](auto id) {
        return prop->images().menu(id);
    };
    // D版と同様、詳細欄の選択・コピー用に共通のテキストメニューを作る。
    text_menu_ = dmenu::createTextMenu(common, dmenu::TMM(message));
    addControl(area, message);
}

ScriptVarSetDialog::ScriptVarSetDialog(
    Props& prop, Summary* summary, std::shared_ptr<usecounter::UseCounter> use_counter,
    Parent parent, const lib::Array<String>& variables, String script_text,
    String base_text, const script::CompileOption& option, Callbacks callbacks,
    Services services, SkinPtr skin)
    : absdialog::AbsDialog(prop, std::move(parent), true,
                           prop.requiredMsgs().dlgTitScriptVarSet.value,
                           prop.images().menu(types::MenuID::EvTemplates),
                           true, smallDialogSavedSize(prop.flex().scriptVarSetDlg),
                           false, true, {}, false,
                           chooseServices(prop, std::move(services), callbacks.services)),
      callbacks_(std::move(callbacks)), prop_(&prop), summary_(summary),
      use_counter_(std::move(use_counter)), skin_(std::move(skin)), variables_(variables),
      script_text_(std::move(script_text)), base_text_(std::move(base_text)),
      values_(variables_.size()), option_(option) {
    enterClose(false);
    const std::weak_ptr<int> life = editor_lifetime_;
    openedEvent.push_back([this, life] {
        if (life.expired()) return;
        ++editor_generation_;
        bindEditors();
    });
    closeEvent.push_back([this, life] {
        if (!life.expired()) { ++editor_generation_; closeDiagnostics(); }
    });
}

ScriptVarSetDialog::~ScriptVarSetDialog() {
    editor_lifetime_.reset();
    closeDiagnostics();
}

void ScriptVarSetDialog::closeDiagnostics() {
    auto diagnostics=std::move(diagnostics_);
    diagnostics_.clear();
    for (const auto& diagnostic:diagnostics) if (diagnostic) (void)diagnostic->close();
}

void ScriptVarSetDialog::editEnd(std::size_t row, std::size_t column, String text) {
    if (column != 1U || row >= values_.size()) return;
    values_[row] = std::move(text);
    if (table_ && row < table_->rows.size() && table_->rows[row].cells.size() > 1U)
        (void)lib::publish_table_cell_text(*table_, row, 1U, values_[row]);
}

void ScriptVarSetDialog::bindEditors() {
    if (!table_) return;
    const std::weak_ptr<int> life = editor_lifetime_;
    const auto generation = editor_generation_;
    const std::weak_ptr<lib::Window> owner = dialog();
    const auto current = [this, life, generation, owner] {
        if (life.expired()) return false;
        const auto window = owner.lock();
        return generation == editor_generation_ && window && window->visible();
    };
    table_->on_cell_edited = [this, current](std::size_t row, std::size_t column, String text) {
        if (current()) editEnd(row, column, std::move(text));
    };
    std::vector<lib::ChoiceItem> candidates;
    for (const auto& [label, expression] : editor_table_)
        candidates.push_back({label, expression});
    incsearch::IncSearchSettings settings;
    if (prop_) {
        settings.startIncrementalSearchWhenKeyDown = prop_->var().startIncrementalSearchWhenKeyDown.value;
        const auto* search_prop = prop_;
        settings.startIncrementalSearchWhenKeyDownProvider = [search_prop] {
            return search_prop && search_prop->var().startIncrementalSearchWhenKeyDown.value;
        };
        settings.incrementalSearchBoxWidth = prop_->var().incrementalSearchBoxWidth.value;
        settings.incrementalSearchType = prop_->var().incrementalSearchType.value;
        settings.containsLabel = prop_->requiredMsgs().incSearchContains.value;
        settings.wildcardLabel = prop_->requiredMsgs().incSearchWildcard.value;
        settings.regexLabel = prop_->requiredMsgs().incSearchRegex.value;
    }
    // D版の TableTextEdit の createEditor と同様、共通の検索可能・編集可能な
    // コンボを使う。 実際のUIはネイティブのインライン編集側が一つだけ持ち、候補値はリテラルとして扱う。
    const std::weak_ptr<lib::TableView> weak_table = table_;
    const auto edit_revision = std::make_shared<std::uint64_t>(0);
    table_->on_inline_editor_created =
        [this, current, candidates, settings, weak_table, edit_revision, service = services()](std::size_t row, std::size_t column,
            const std::shared_ptr<lib::TextEdit>& projection, const Combo& combo) {
        if (column != 1U || !combo || !current()) return;
        const auto table = weak_table.lock();
        if (!table) return;
        const auto revision = ++*edit_revision;
        // D版と同様、明示的に選んだ候補はインデックスで対応付け、自
        // 由入力は式として扱う。 確定時の終了処理をまたいでこのエ
        // ディタを保持し、無関係な編集可能コンボの動作は変えない。
        table->on_cell_edited = [this, current, edit_revision, revision, combo, row](
            std::size_t edited_row, std::size_t edited_column, String text) {
            if (!current() || revision != *edit_revision || edited_row != row || edited_column != 1U) return;
            if (combo->selected_index && *combo->selected_index < combo->items.size())
                text = combo->items[*combo->selected_index].value;
            editEnd(edited_row, edited_column, std::move(text));
        };
        (void)incsearch::attachComboSearch(service, combo, projection,
            [candidates](const incsearch::IncSearch& search) {
                std::vector<lib::ChoiceItem> result;
                for (const auto& candidate : candidates)
                    if (search.match(candidate.text)) result.push_back(candidate);
                return result;
            }, settings, current);
    };
}

void ScriptVarSetDialog::setup(Container& area) {
    const auto& messages = prop_->requiredMsgs();
    table_ = std::make_shared<lib::TableView>();
    if (lib::editor_window_host_options().all_mode) {
        // ALL の既存の列幅、サイズ変更用のつまみ、列順を維持する。
        table_->columns = {{"Variable", 180.0F, lib::TextAlignment::Start, true, false},
                           {"Value", 300.0F, lib::TextAlignment::Start, true, false}};
    } else {
        // 単独エディタではD版と同様、変数名列を内容
        // に合わせ、値の列で表の残りの幅を埋める。
        table_->columns = {{messages.scriptVarNameColumn.value, 180.0F,
                            lib::TextAlignment::Start, false, false},
                           {messages.scriptVarValueColumn.value, 50.0F,
                            lib::TextAlignment::Start, false, false, 1.0F}};
    }
    table_->rows.reserve(variables_.size());
    for (std::size_t index = 0; index < variables_.size(); ++index) {
        table_->rows.push_back({{{variables_[index], variables_[index], 0, true},
                                 {values_[index], values_[index], 0, true}}, false});
    }
    editor_table_.clear();
    const auto add_value = [this](String label, String value) {
        editor_table_.emplace_back(std::move(label), std::move(value));
    };
    if (summary_) {
        const auto root = summary_->flagDirRoot();
        const auto logical_sort = prop_ ? prop_->var().logicalSort.value : false;
        const auto add_variables = [&](const auto& values, const StringView kind) {
            ::cwxeditor::cwx::flag::sortedWithPath(
                values, logical_sort, [&](const auto& value) {
                    if (!value) return;
                    add_value(String(kind) + " - " + value->path(),
                              script::CWXScript::createString(value->path()));
                });
        };
        // D版の ScriptVarSetDialog と同様、三つの一覧を allVars(root,
        // _uc) に任せ、 現在のイベント・カードのローカル宣言を先頭に加える。
        add_variables(::cwxeditor::cwx::flag::allVars<
                          ::cwxeditor::cwx::flag::Flag>(root, use_counter_),
                      "Flag");
        add_variables(::cwxeditor::cwx::flag::allVars<
                          ::cwxeditor::cwx::flag::Step>(root, use_counter_),
                      "Step");
        add_variables(::cwxeditor::cwx::flag::allVars<
                          ::cwxeditor::cwx::flag::Variant>(root, use_counter_),
                      "Variant");
        const auto add_id = [&add_value](const auto& value, StringView kind) {
            if (value) add_value(String(kind) + " - " + std::to_string(value->id()) +
                                 "." + value->name(), std::to_string(value->id()));
        };
        for (const auto& value : summary_->areas()) add_id(value, "Area");
        for (const auto& value : summary_->battles()) add_id(value, "Battle");
        for (const auto& value : summary_->packages()) add_id(value, "Package");
        for (const auto& value : summary_->casts()) add_id(value, "Cast");
        for (const auto& value : summary_->skills()) add_id(value, "Skill");
        for (const auto& value : summary_->items()) add_id(value, "Item");
        for (const auto& value : summary_->beasts()) add_id(value, "Beast");
        for (const auto& value : summary_->infos()) add_id(value, "Info");
    }
    // D版と同様、上の allVars 用の _uc がカードのローカルカウンタ
    // ーでも、 文字列IDの補完には _summ.useCounter を使う。
    const auto scenario_counter = summary_ ? summary_->useCounter() : nullptr;
    if (scenario_counter) {
        const auto add_string_ids = [&add_value, this](const auto& ids, StringView kind) {
            auto sorted = ids;
            std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
                return left.id < right.id;
            });
            for (const auto& id : sorted)
                add_value(String(kind) + " - " + id.id,
                          script::CWXScript::createString(id.id));
        };
        add_string_ids(scenario_counter->template keys<usecounter::CouponId>(), "Coupon");
        add_string_ids(scenario_counter->template keys<usecounter::GossipId>(), "Gossip");
        add_string_ids(scenario_counter->template keys<usecounter::CompleteStampId>(), "Complete stamp");
        add_string_ids(scenario_counter->template keys<usecounter::KeyCodeId>(), "Key code");
        add_string_ids(scenario_counter->template keys<usecounter::CellNameId>(), "Cell name");
        add_string_ids(scenario_counter->template keys<usecounter::CardGroupId>(), "Card group");
    }
    if (summary_ && skin_ && prop_) {
        for (const auto& path : summary_->allMaterials(
                 *skin_, prop_->var().ignorePaths.value,
                 prop_->var().logicalSort.value, false)) {
            add_value("Material - " + path,
                      script::CWXScript::createString(path));
        }
    }
    const auto keyword_overrides = prop_ ? prop_->elementOverrides(skin_ ? skin_->type() : String{}) : dprops::ElementOverrideTable{};
    for (const auto& keyword : script::keywordInfos(prop_ ? &prop_->parent() : nullptr, keyword_overrides)) {
        if (!keyword.keyword.empty()) {
            const auto label = keyword.name.empty() ? keyword.keyword
                                                    : keyword.name;
            add_value(keyword.category.empty() ? label : keyword.category + " - " + label, keyword.keyword);
        }
    }
    // C++版では呼び出し元が渡した未解決のプレースホルダー
    // も、追加の編集可能な選択肢として残す。 既知のリテラル
    // については、D版が生成する補完一覧を引き続き正とする。
    for (const auto& value : variables_) add_value(value, value);
    for (auto& row : table_->rows) {
        if (row.cells.size() < 2U) continue;
        row.cells[0].inline_editable = false;
        auto& cell = row.cells[1];
        cell.editor = lib::CellEditorKind::EditableChoice;
        for (const auto& [label, expression] : editor_table_) {
            cell.editor_choices.push_back(label);
            cell.editor_choice_values.push_back(expression);
        }
    }
    const auto table_height = prop_
        ? static_cast<float>(std::max<std::int32_t>(
              1, prop_->var().scriptVarTableHeight.value))
        : 300.0F;

    table_->set_layout_item({.horizontal = lib::Align::Stretch,
                             .vertical = lib::Align::Stretch,
                             .preferred = {480.0F, table_height},
                             .grow = 1.0F});
    if (lib::editor_window_host_options().all_mode) {
        // ALL は既存の直接のラベル・表の子構造とラベル文字
        // 列を維持し、 計測される位置・幅・高さを変えない。
        addControl(area, labelControl("Script variable values"));
        addControl(area, table_);
    } else {
        // 単独エディタではD版と同様、Group の中に変数の表を入れる。
        auto group = std::make_shared<lib::GroupBox>();
        group->title = messages.scriptVarSet.value;
        auto group_layout = std::make_shared<lib::GridLayout>();
        group_layout->columns = 1U;
        group->set_layout(group_layout);
        group->set_layout_item({.horizontal = lib::Align::Stretch,
                                .vertical = lib::Align::Stretch,
                                .grow = 1.0F});
        group->add(table_);
        addControl(area, group);
    }
}

bool ScriptVarSetDialog::close(bool ok, bool& cancel) {
    cancel = false;
    if (!ok || !prop_) return ok;
    std::vector<script::VarSet> variableTable;
    variableTable.reserve(variables_.size());
    for (std::size_t index = 0; index < variables_.size(); ++index)
        variableTable.push_back({variables_[index], values_[index]});
    auto option = option_;
    const auto source = script::CWXScript::pushVars(script_text_, variableTable, option);
    try {
        contents_ = script::compile(&prop_->parent(), summary_, source, option);
    } catch (const script::CWXScriptException& error) {
        cancel = true;
        const std::weak_ptr<int> life=editor_lifetime_;
        const auto report=callbacks_.scriptError;
        if (report) report(error);
        if (life.expired()) return false;
        std::erase_if(diagnostics_,[](const auto& value){return value->closed();});
        auto diagnostic=std::make_shared<scripterrordialog::ScriptErrorDialog>(
            services(),dialog(),error,base_text_,option,scripterrordialog::presentationFromProps(*prop_));
        diagnostics_.push_back(diagnostic);
        (void)diagnostic->open();
        return false;
    } catch (const std::exception&) {
        cancel = true;
        return false;
    }
    return true;
}

EventTemplateDialog::EventTemplateDialog(Props& prop, Summary* summary,
                                         Parent parent,
                                         lib::SharedSlice<EvTemplate> templates,
                                         Callbacks callbacks, Services services)
    : absdialog::AbsDialog(prop, std::move(parent), false,
                           summary
                               ? ::cwxeditor::cwx::utils::tryFormat(
                                     prop.requiredMsgs().dlgTitEvTemplates.value,
                                     summary->scenarioName())
                               : prop.requiredMsgs().dlgTitEvTemplates.value,
                           prop.images().load(StringView{"ev_tmpl.png"}), true,
                           &prop.flex().evTemplDlg, true, true, {}, false,
                           chooseServices(prop, std::move(services), callbacks.services)),
      callbacks_(std::move(callbacks)), prop_(&prop), summary_(summary),
      templates_(std::move(templates)) {
    if (callbacks_.subscribeScenarioName)
        disconnect_scenario_name_ = callbacks_.subscribeScenarioName(
            [this] { refScenarioName(); });
    if (callbacks_.subscribeScenario)
        disconnect_scenario_ = callbacks_.subscribeScenario(
            [this](SummaryPtr value) { refScenario(value.get()); });
}

EventTemplateDialog::~EventTemplateDialog() {
    if (disconnect_scenario_name_) disconnect_scenario_name_();
    if (disconnect_scenario_) disconnect_scenario_();
}

void EventTemplateDialog::retainWhileOpen() {
    if (lifetime_hooks_installed_) return;
    const auto self = weak_from_this().lock();
    if (!self || !dialog() || !dialog()->visible()) return;
    // D版はウィンドウ表示中のモードレスダイアログをGCが保持する。 C++
    // 版は明示的に保持し、共通の表示側がウィンドウを隠した後に解放する。
    // 表示ごとにコールバックを置き換える AbsDialog::open
    // の後で登録し、 ネイティブの終了・ボタンの方針を二重に実装しない。
    self_keep_alive_ = self;
    const std::weak_ptr<EventTemplateDialog> weak = self;
    const auto shell = dialog();
    auto previous = std::move(shell->on_visibility_changed);
    shell->on_visibility_changed =
        [weak, previous = std::move(previous)](bool visible) {
            const auto hold = weak.lock();
            if (previous) previous(visible);
            if (hold && !visible) hold->self_keep_alive_.reset();
        };
    lifetime_hooks_installed_ = true;
}

bool EventTemplateDialog::open() {
    const auto hold = weak_from_this().lock();
    const bool was_visible = dialog() && dialog()->visible();
    if (!was_visible) lifetime_hooks_installed_ = false;
    const bool result = absdialog::AbsDialog::open();
    if (dialog() && dialog()->visible()) {
        if (!was_visible) retainWhileOpen();
    }
    else self_keep_alive_.reset();
    return result;
}

void EventTemplateDialog::forceCancel() {
    const auto hold = self_keep_alive_;
    absdialog::AbsDialog::forceCancel();
    if (!dialog() || !dialog()->visible()) self_keep_alive_.reset();
}

String EventTemplateDialog::title() const {
    const auto base = prop_ ? prop_->requiredMsgs().dlgTitEvTemplates.value
                            : String{"イベントテンプレートの設定"};
    if (!summary_) return base;
    return ::cwxeditor::cwx::utils::tryFormat(base, summary_->scenarioName());
}

void EventTemplateDialog::refScenarioName() {
    if (dialog()) absdialog::AbsDialog::title(title());
}

void EventTemplateDialog::refScenario(Summary* summary) {
    if (summary_ != summary) forceCancel();
}

void EventTemplateDialog::setup(Container& area) {
    auto area_layout = std::make_shared<lib::GridLayout>();
    area_layout->columns = 1U;
    area_layout->spacing = 0.0F;
    area_layout->padding = {};
    area.set_layout(area_layout);

    toolspane::Commons common;
    common.services = services();
    if (prop_) common.undoMaxEtc = static_cast<std::size_t>(
        std::max(1, prop_->var().undoMaxEtc.value));
    toolspane::Callbacks callbacks;
    callbacks.ignoreMod = [this](bool value) { ignoreMod(value); };
    callbacks.catchMod = [this] { return catchMod(); };
    callbacks.applyEnabled = [this] {
        pane_needs_apply_ = true;
        applyEnabled();
    };
    toolspane::Presentation presentation;
    if (prop_) {
        auto& messages = prop_->requiredMsgs();
        presentation.menuText = [&messages](const types::MenuID id) {
            return messages.menuText(id);
        };
        presentation.menuImage = [this](const types::MenuID id) {
            if (!prop_) return lib::ResourceId{};
            switch (id) {
            case types::MenuID::Up:
                return prop_->images().load(lib::StringView{"up.png"});
            case types::MenuID::Down:
                return prop_->images().load(lib::StringView{"down.png"});
            case types::MenuID::Undo:
                return prop_->images().load(lib::StringView{"undo.png"});
            case types::MenuID::Redo:
                return prop_->images().load(lib::StringView{"redo.png"});
            case types::MenuID::Cut:
                return prop_->images().load(lib::StringView{"cut.png"});
            case types::MenuID::Copy:
                return prop_->images().load(lib::StringView{"copy.png"});
            case types::MenuID::Paste:
                return prop_->images().load(lib::StringView{"paste.png"});
            case types::MenuID::Delete:
                return prop_->images().load(lib::StringView{"delete.png"});
            default: return lib::ResourceId{};
            }
        };
        presentation.boxEventTemplate = messages.eventTemplatesTitle.value;
        presentation.noName = messages.noNameData.value;
        presentation.newText = messages.sNew.value;
        presentation.alterText = messages.sAlt.value;
        presentation.deleteText = messages.sDel.value;
        presentation.upText = messages.upSelection.value;
        presentation.downText = messages.downSelection.value;
        presentation.nameText = messages.eventTemplateName.value;
        presentation.mnemonicText = messages.mnemonic.value;
        presentation.hotkeyText = messages.hotkey.value;
        presentation.eventScriptText = messages.eventTemplateScript.value;
        presentation.eventTemplateHint1 = messages.eventTemplateHint1.value;
        presentation.eventTemplateHint2 = messages.eventTemplateHint2.value;
        presentation.questionTitle = messages.dlgTitQuestion.value;
        presentation.selectionApplyMessage =
            messages.dlgMsgForceApplySelection.value;
        presentation.tabs = prop_->var().tabs.value;
        presentation.undoMaxEtc = static_cast<std::size_t>(
            std::max(0, prop_->var().undoMaxEtc.value));
        presentation.comboVisibleItemCount = static_cast<std::size_t>(
            std::max(1, prop_->var().comboVisibleItemCount.value));
        presentation.leftWidth = prop_->var().settingListWidth.value;
        presentation.leftHeight = prop_->var().settingListHeight.value;
    }
    tools_pane_ = std::make_shared<toolspane::ToolsPane<EvTemplate>>(
        std::move(common), std::move(callbacks), &area,
        std::move(presentation));
    area.add(tools_pane_);
    tools_pane_->set_layout_item({.row = 0U, .column = 0U,
                                  .horizontal = lib::Align::Stretch,
                                  .vertical = lib::Align::Stretch,
                                  .minimum = {575.0F, 320.0F}, .grow = 1.0F});
    if (prop_) {
        tools_pane_->setup(copyEventTemplates(templates_),
            prop_->var().eventTemplatesOfScenarioSashL.value,
            prop_->var().eventTemplatesOfScenarioSashR.value);
        tools_pane_->setShortcutWeights(
            prop_->var().eventTemplateOfScenarioShortcutSashL.value,
            prop_->var().eventTemplateOfScenarioShortcutSashR.value);
    } else {
        std::vector<std::int32_t> weights{1, 2};
        tools_pane_->setup(copyEventTemplates(templates_), weights);
    }
    refScenarioName();
}

bool EventTemplateDialog::apply() {
    if (tools_pane_) {
        if (tools_pane_->noApply()) {
            auto result = lib::MessageResult::Yes;
            if (services().dialogs) {
                lib::MessageDialogRequest request;
                request.title = prop_ ? prop_->requiredMsgs().dlgTitQuestion.value
                                      : String{"確認"};
                request.message = prop_
                    ? ::cwxeditor::cwx::utils::tryFormat(
                          prop_->requiredMsgs().dlgMsgForceApplySingle.value,
                          tools_pane_->boxName())
                    : String{"編集中の項目を適用しますか？"};
                request.severity = lib::MessageSeverity::Question;
                request.buttons = lib::MessageButtons::YesNoCancel;
                result = services().dialogs->message(request);
            }
            if (result == lib::MessageResult::Cancel ||
                result == lib::MessageResult::None)
                return false;
            if (result == lib::MessageResult::Yes) tools_pane_->forceApply();
        }
        // ToolsPane::array() は、D版の取得関数と同様、所有権を持つ値の複製を返す。
        templates_ = lib::SharedSlice<EvTemplate>(tools_pane_->array());
    }
    pane_needs_apply_ = false;
    if (summary_) summary_->eventTemplates(templates_);
    if (callbacks_.scenarioNameChanged) callbacks_.scenarioNameChanged();
    return true;
}

template <typename Enum>
void ImportOptionDialog::addEnumItems(const Combo& combo, const Enum selected,
                                      const bool include_images) {
    if (!combo) return;
    const auto values = enumValues<Enum>(include_images);
    std::size_t selectedIndexValue = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto value = values[index];
        addChoice(combo, enumText(*prop_, value),
                  std::to_string(static_cast<int>(value)));
        if (value == selected) selectedIndexValue = index;
    }
    // D版と同様、絞り込んだコンボに保存値がなければ未選択のままにする。
    if (selectedIndexValue >= values.size())
        combo->selected_index.reset();
    else
        setSelected(combo, selectedIndexValue);
}

template <typename Enum>
Enum ImportOptionDialog::selectedEnum(const Combo& combo, const Enum fallback,
                                      const bool include_images) {
    const auto values = enumValues<Enum>(include_images);
    const auto index = selectedIndex(combo);
    return index < values.size() ? values[index] : fallback;
}

ImportOptionDialog::ImportOptionDialog(Props& prop, Parent parent,
                                       const bool can_include_image,
                                       Callbacks callbacks, Services services)
    : absdialog::AbsDialog(prop, std::move(parent), true,
                           prop.requiredMsgs().dlgTitImportOption.value,
                           prop.images().menu(types::MenuID::Import),
                           false, nullptr, false, true, {}, false,
                           chooseServices(prop, std::move(services), callbacks.services)),
      callbacks_(std::move(callbacks)), prop_(&prop),
      can_include_image_(can_include_image) {
    option_.materials = static_cast<types::ImportTypeReference1>(
        prop.var().importOptionMaterials.value);
    option_.variables = static_cast<types::ImportTypeReference1>(
        prop.var().importOptionVariables.value);
    option_.casts = static_cast<types::ImportTypeReference2>(
        prop.var().importOptionCasts.value);
    option_.skills = static_cast<types::ImportTypeReference2>(
        prop.var().importOptionSkills.value);
    option_.items = static_cast<types::ImportTypeReference2>(
        prop.var().importOptionItems.value);
    option_.beasts = static_cast<types::ImportTypeReference2>(
        prop.var().importOptionBeasts.value);
    option_.infos = static_cast<types::ImportTypeReference2>(
        prop.var().importOptionInfos.value);
    option_.areas = static_cast<types::ImportTypeReference2>(
        prop.var().importOptionAreas.value);
    option_.battles = static_cast<types::ImportTypeReference2>(
        prop.var().importOptionBattles.value);
    option_.packages = static_cast<types::ImportTypeReference2>(
        prop.var().importOptionPackages.value);
    option_.includedFiles = static_cast<types::ImportTypeIncluded>(
        can_include_image ? prop.var().importOptionIncludedFiles.value
                          : prop.var().importOptionIncludedFilesWithoutIncluding.value);
    option_.hands = static_cast<types::ImportTypeIncluded>(
        prop.var().importOptionHands.value);
    option_.beastsInMotions = static_cast<types::ImportTypeIncluded>(
        prop.var().importOptionBeastsInMotions.value);
    option_.overwriteScenarioInfo = prop.var().importOptionOverwriteScenarioInfo.value;
    // D版の create() と同様、保存値が範囲外なら各 ImportOption
    // の既定値を使う。 両ホストに同じデータ方針を適用し、ALL の部品配置は変えない。
    const decltype(option_) defaults{};
    const auto normalize = []<typename Enum>(Enum value, Enum fallback) {
        const auto values = enumValues<Enum>(true);
        return std::find(values.begin(), values.end(), value) != values.end()
            ? value : fallback;
    };
    option_.materials = normalize(option_.materials, defaults.materials);
    option_.variables = normalize(option_.variables, defaults.variables);
    option_.casts = normalize(option_.casts, defaults.casts);
    option_.skills = normalize(option_.skills, defaults.skills);
    option_.items = normalize(option_.items, defaults.items);
    option_.beasts = normalize(option_.beasts, defaults.beasts);
    option_.infos = normalize(option_.infos, defaults.infos);
    option_.areas = normalize(option_.areas, defaults.areas);
    option_.battles = normalize(option_.battles, defaults.battles);
    option_.packages = normalize(option_.packages, defaults.packages);
    option_.includedFiles = normalize(option_.includedFiles, defaults.includedFiles);
    option_.hands = normalize(option_.hands, defaults.hands);
    option_.beastsInMotions = normalize(option_.beastsInMotions, defaults.beastsInMotions);
}

ImportOptionDialog::ImportOptionDialog(Callbacks callbacks, Parent parent,
                                       const bool can_include_image,
                                       Services services)
    : ImportOptionDialog(*callbacks.prop, std::move(parent), can_include_image,
                         std::move(callbacks), std::move(services)) {}

void ImportOptionDialog::setup(Container& area) {
    materials_ = comboControl();
    variables_ = comboControl();
    casts_ = comboControl();
    skills_ = comboControl();
    items_ = comboControl();
    beasts_ = comboControl();
    infos_ = comboControl();
    areas_ = comboControl();
    battles_ = comboControl();
    packages_ = comboControl();
    included_files_ = comboControl();
    hands_ = comboControl();
    beasts_in_motions_ = comboControl();
    addEnumItems(materials_, option_.materials, true);
    addEnumItems(variables_, option_.variables, true);
    addEnumItems(casts_, option_.casts, true);
    addEnumItems(skills_, option_.skills, true);
    addEnumItems(items_, option_.items, true);
    addEnumItems(beasts_, option_.beasts, true);
    addEnumItems(infos_, option_.infos, true);
    addEnumItems(areas_, option_.areas, true);
    addEnumItems(battles_, option_.battles, true);
    addEnumItems(packages_, option_.packages, true);
    addEnumItems(included_files_, option_.includedFiles, can_include_image_);
    addEnumItems(hands_, option_.hands, true);
    addEnumItems(beasts_in_motions_, option_.beastsInMotions, true);
    const auto& messages = prop_->requiredMsgs();
    addImportOptionLabel(area, *prop_, messages.importOptionMaterials.value,
                         [this] { return prop_->images().text(); });
    addControl(area, materials_);
    addImportOptionLabel(area, *prop_, messages.importOptionVariables.value,
                         [this] { return prop_->images().flagDir(); });
    addControl(area, variables_);
    addImportOptionLabel(area, *prop_, messages.importOptionCasts.value,
                         [this] { return prop_->images().casts(); });
    addControl(area, casts_);
    addImportOptionLabel(area, *prop_, messages.importOptionSkills.value,
                         [this] { return prop_->images().skill(); });
    addControl(area, skills_);
    addImportOptionLabel(area, *prop_, messages.importOptionItems.value,
                         [this] { return prop_->images().item(); });
    addControl(area, items_);
    addImportOptionLabel(area, *prop_, messages.importOptionBeasts.value,
                         [this] { return prop_->images().beast(); });
    addControl(area, beasts_);
    addImportOptionLabel(area, *prop_, messages.importOptionInfos.value,
                         [this] { return prop_->images().info(); });
    addControl(area, infos_);
    addImportOptionLabel(area, *prop_, messages.importOptionAreas.value,
                         [this] { return prop_->images().area(); });
    addControl(area, areas_);
    addImportOptionLabel(area, *prop_, messages.importOptionBattles.value,
                         [this] { return prop_->images().battle(); });
    addControl(area, battles_);
    addImportOptionLabel(area, *prop_, messages.importOptionPackages.value,
                         [this] { return prop_->images().packages(); });
    addControl(area, packages_);
    addImportOptionLabel(area, *prop_, messages.importOptionIncludedFiles.value,
                         [this] { return prop_->images().cards(); });
    addControl(area, included_files_);
    addImportOptionLabel(area, *prop_, messages.importOptionHands.value,
                         [this] { return prop_->images().menu(types::MenuID::OpenHand); });
    addControl(area, hands_);
    addImportOptionLabel(area, *prop_, messages.importOptionBeastsInMotions.value,
                         [this] { return prop_->images().beast(); });
    addControl(area, beasts_in_motions_);
    overwrite_scenario_info_ = checkControl(
        messages.importOverwriteScenarioInfo.value,
        option_.overwriteScenarioInfo);
    addControl(area, overwrite_scenario_info_);
}

bool ImportOptionDialog::close(bool ok) {
    if (!ok || !prop_) return ok;
    option_.materials = selectedEnum(materials_, option_.materials, true);
    option_.variables = selectedEnum(variables_, option_.variables, true);
    option_.casts = selectedEnum(casts_, option_.casts, true);
    option_.skills = selectedEnum(skills_, option_.skills, true);
    option_.items = selectedEnum(items_, option_.items, true);
    option_.beasts = selectedEnum(beasts_, option_.beasts, true);
    option_.infos = selectedEnum(infos_, option_.infos, true);
    option_.areas = selectedEnum(areas_, option_.areas, true);
    option_.battles = selectedEnum(battles_, option_.battles, true);
    option_.packages = selectedEnum(packages_, option_.packages, true);
    option_.includedFiles = selectedEnum(
        included_files_, can_include_image_ ? option_.includedFiles
                                            : types::ImportTypeIncluded::AsIs,
        can_include_image_);
    option_.hands = selectedEnum(hands_, option_.hands, true);
    option_.beastsInMotions = selectedEnum(beasts_in_motions_, option_.beastsInMotions,
                                           true);
    option_.overwriteScenarioInfo = overwrite_scenario_info_ &&
                                     overwrite_scenario_info_->checked;
    prop_->var().importOptionMaterials = static_cast<std::int32_t>(option_.materials);
    prop_->var().importOptionVariables = static_cast<std::int32_t>(option_.variables);
    prop_->var().importOptionCasts = static_cast<std::int32_t>(option_.casts);
    prop_->var().importOptionSkills = static_cast<std::int32_t>(option_.skills);
    prop_->var().importOptionItems = static_cast<std::int32_t>(option_.items);
    prop_->var().importOptionBeasts = static_cast<std::int32_t>(option_.beasts);
    prop_->var().importOptionInfos = static_cast<std::int32_t>(option_.infos);
    prop_->var().importOptionAreas = static_cast<std::int32_t>(option_.areas);
    prop_->var().importOptionBattles = static_cast<std::int32_t>(option_.battles);
    prop_->var().importOptionPackages = static_cast<std::int32_t>(option_.packages);
    // D版の put() と同様、コンボが未選択なら保存済みの設定を維持する。
    if (selectedIndex(included_files_) <
        enumValues<types::ImportTypeIncluded>(can_include_image_).size()) {
        if (can_include_image_)
            prop_->var().importOptionIncludedFiles = static_cast<std::int32_t>(option_.includedFiles);
        else
            prop_->var().importOptionIncludedFilesWithoutIncluding =
                static_cast<std::int32_t>(option_.includedFiles);
    }
    prop_->var().importOptionHands = static_cast<std::int32_t>(option_.hands);
    prop_->var().importOptionBeastsInMotions = static_cast<std::int32_t>(option_.beastsInMotions);
    prop_->var().importOptionOverwriteScenarioInfo = option_.overwriteScenarioInfo;
    return true;
}

String ImportResultDialog::resourceName(
    const lib::Owner<area::AbstractArea>& value) {
    return objectName(value);
}

String ImportResultDialog::cardName(const lib::Owner<card::Card>& value) {
    return objectName(value);
}

ImportResultDialog::ImportResultDialog(Props& prop, Summary* summary, Parent parent,
                                       importutils::ImportResult result,
                                       Callbacks callbacks, Services services,
                                       SkinPtr skin)
    : absdialog::AbsDialog(prop, std::move(parent), true,
                           prop.requiredMsgs().dlgTitImportResult.value,
                           prop.images().menu(types::MenuID::Import),
                           true, smallDialogSavedSize(prop.flex().importResultDlg),
                           false, true, {}, false,
                           chooseServices(prop, std::move(services), callbacks.services)),
      callbacks_(std::move(callbacks)), prop_(&prop), summary_(summary),
      skin_(std::move(skin)),
      result_(std::move(result)) {}

ImportResultDialog::ImportResultDialog(Callbacks callbacks, Summary* summary,
                                       Parent parent,
                                       importutils::ImportResult result,
                                       Services services, SkinPtr skin)
    : ImportResultDialog(*callbacks.prop, summary, std::move(parent),
                         std::move(result), std::move(callbacks),
                         std::move(services), std::move(skin)) {}

void ImportResultDialog::appendRow(String text, RowReference reference,
                                   const lib::ResourceId icon) {
    if (!list_) return;
    list_->rows.push_back({{{std::move(text), {}, icon, true}}, true});
    row_references_.push_back(std::move(reference));
}

void ImportResultDialog::setup(Container& area) {
    list_ = std::make_shared<lib::TableView>();
    // D版と同様に MULTI|CHECK を使い、checkedResult には受け付けた行だけを残す。
    list_->multi_select = true;
    list_->checkable = true;
    list_->check_selected_rows = true;
    list_->columns = {{"Imported resources", 500.0F, lib::TextAlignment::Start,
                       true, false}};
    row_references_.clear();
    std::size_t index = 0;
    for (const auto& material : result_.materials) {
        String text = material.dst.empty() ? material.src : material.dst;
        if (summary_ && !material.dst.empty()) {
            const auto destination = structs::pathFromUtf8(material.dst);
            const auto scenario = structs::pathFromUtf8(summary_->scenarioPath());
            const auto relative = destination.lexically_normal().lexically_relative(
                scenario.lexically_normal());
            if (!relative.empty())
                text = utils::encodePath(structs::pathToUtf8(relative));
        }
        if (material.overwrite)
            text = utils::tryFormat(prop_->requiredMsgs().overwriteMark.value, text);
        appendRow(std::move(text), {RowKind::Material, {}, 0, index++},
                  dutils::fimage(prop_, material.src, skin_.get(), services()));
    }
    auto select_all_menu = std::make_shared<lib::Menu>(true);
    select_all_menu->items.push_back({.text = prop_->buildMenu(types::MenuID::SelectAll),
                                      .command = "select-all"});
    const std::weak_ptr<lib::TableView> weak_list = list_;
    const std::weak_ptr<lib::Menu> weak_select_all_menu = select_all_menu;
    select_all_menu->on_open = [weak_list, weak_select_all_menu] {
        const auto table = weak_list.lock();
        const auto menu = weak_select_all_menu.lock();
        if (!table || !menu || menu->items.empty()) return;
        menu->items.front().enabled = !table->rows.empty() &&
            table->selection.size() != table->rows.size();
    };
    select_all_menu->on_command = [weak_list](std::string_view command) {
        if (command != "select-all") return;
        const auto table = weak_list.lock();
        if (!table) return;
        std::vector<std::size_t> rows;
        rows.reserve(table->rows.size());
        for (std::size_t row = 0; row < table->rows.size(); ++row) rows.push_back(row);
        (void)lib::select_table_rows(*table, std::move(rows));
    };
    const std::weak_ptr<lib::GuiBackend> weak_gui = services().gui;
    // 表のコールバックがポップアップを所有し、ポップアップのコールバックは表への弱参照だけを持つ。
    list_->on_pointer = [weak_list, select_all_menu, weak_gui](lib::PointerEvent& event) {
        if (event.action != lib::PointerAction::Up ||
            event.button != lib::PointerButton::Secondary) return;
        const auto table = weak_list.lock();
        const auto gui = weak_gui.lock();
        if (!table || !gui) return;
        if (select_all_menu->on_open) select_all_menu->on_open();
        gui->show_context_menu(select_all_menu, table, event.position);
        event.handled = true;
    };
    auto appendVariables = [this](const auto& groups, const RowKind kind) {
        const auto less = [this](const String& left, const String& right) {
            return prop_->var().logicalSort.value ? utils::ncmp(left, right) < 0
                                                  : left < right;
        };
        std::vector<String> directories;
        for (const auto& [group, values] : groups) directories.push_back(group);
        std::sort(directories.begin(), directories.end(), less);
        for (const auto& group : directories) {
            const auto& values = groups.at(group);
            std::vector<std::size_t> indices;
            for (std::size_t i = 0; i < values.size(); ++i) indices.push_back(i);
            std::stable_sort(indices.begin(), indices.end(), [&](auto left, auto right) {
                return less(values[left].name(), values[right].name());
            });
            for (const auto sourceIndex : indices) {
                const auto& value = values[sourceIndex];
                // D版と同様、ここではすべての変数種類に searchResultFlag
                // を使い、 完成したラベルに翻訳済みの上書きの印を付ける。
                const auto path = ::cwxeditor::cwx::flag::FlagDir::join(group, value.name());
                String text = utils::tryFormat(
                    prop_->requiredMsgs().searchResultFlag.value, path);
                if (value.overwrite)
                    text = utils::tryFormat(prop_->requiredMsgs().overwriteMark.value, text);
                // 表示用の並びを整列し、元の結果の識別情報を保持する。
                const auto icon = kind == RowKind::Flag
                    ? prop_->images().flag()
                    : kind == RowKind::Step ? prop_->images().step()
                                            : prop_->images().variant();
                appendRow(std::move(text), {kind, group, 0, sourceIndex}, icon);
            }
        }
    };
    appendVariables(result_.flags, RowKind::Flag);
    appendVariables(result_.steps, RowKind::Step);
    appendVariables(result_.variants, RowKind::Variant);
    auto appendResources = [this](const auto& values, const RowKind kind) {
        for (const auto id : sortedKeys(values)) {
            const auto& value = values.at(id);
            const auto presentation = importResourcePresentation(
                *prop_, summary_, value.get());
            appendRow(presentation.text.empty() ? displayNumber(id) : presentation.text,
                      {kind, {}, id, 0}, presentation.icon);
        }
    };
    appendResources(result_.casts, RowKind::Cast);
    appendResources(result_.skills, RowKind::Skill);
    appendResources(result_.items, RowKind::Item);
    appendResources(result_.beasts, RowKind::Beast);
    appendResources(result_.infos, RowKind::Info);
    appendResources(result_.areas, RowKind::Area);
    appendResources(result_.battles, RowKind::Battle);
    appendResources(result_.packages, RowKind::Package);
    addControl(area, labelControl("Select imported resources"));
    addControl(area, list_);
}

void ImportResultDialog::retainCheckedRows(importutils::ImportResult& destination) const {
    if (!list_) {
        destination = result_;
        return;
    }
    for (std::size_t row = 0; row < row_references_.size(); ++row) {
        if (row >= list_->rows.size() || !list_->rows[row].checked) continue;
        const auto& reference = row_references_[row];
        switch (reference.kind) {
        case RowKind::Material:
            if (reference.source_index < result_.materials.size())
                destination.materials.push_back(result_.materials[reference.source_index]);
            break;
        case RowKind::Flag:
            if (const auto found = result_.flags.find(reference.group);
                found != result_.flags.end() && reference.source_index < found->second.size())
                destination.flags[reference.group].push_back(found->second[reference.source_index]);
            break;
        case RowKind::Step:
            if (const auto found = result_.steps.find(reference.group);
                found != result_.steps.end() && reference.source_index < found->second.size())
                destination.steps[reference.group].push_back(found->second[reference.source_index]);
            break;
        case RowKind::Variant:
            if (const auto found = result_.variants.find(reference.group);
                found != result_.variants.end() && reference.source_index < found->second.size())
                destination.variants[reference.group].push_back(found->second[reference.source_index]);
            break;
        case RowKind::Cast:
            if (const auto found = result_.casts.find(reference.id); found != result_.casts.end())
                destination.casts.emplace(found->first, found->second);
            break;
        case RowKind::Skill:
            if (const auto found = result_.skills.find(reference.id); found != result_.skills.end())
                destination.skills.emplace(found->first, found->second);
            break;
        case RowKind::Item:
            if (const auto found = result_.items.find(reference.id); found != result_.items.end())
                destination.items.emplace(found->first, found->second);
            break;
        case RowKind::Beast:
            if (const auto found = result_.beasts.find(reference.id); found != result_.beasts.end())
                destination.beasts.emplace(found->first, found->second);
            break;
        case RowKind::Info:
            if (const auto found = result_.infos.find(reference.id); found != result_.infos.end())
                destination.infos.emplace(found->first, found->second);
            break;
        case RowKind::Area:
            if (const auto found = result_.areas.find(reference.id); found != result_.areas.end())
                destination.areas.emplace(found->first, found->second);
            break;
        case RowKind::Battle:
            if (const auto found = result_.battles.find(reference.id); found != result_.battles.end())
                destination.battles.emplace(found->first, found->second);
            break;
        case RowKind::Package:
            if (const auto found = result_.packages.find(reference.id); found != result_.packages.end())
                destination.packages.emplace(found->first, found->second);
            break;
        }
    }
}

bool ImportResultDialog::close(bool ok) {
    if (!ok) return false;
    importutils::ImportResult selected;
    retainCheckedRows(selected);
    result_ = std::move(selected);
    return true;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::smalldialogs
