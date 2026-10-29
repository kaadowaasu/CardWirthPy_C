#include "cwxeditor_src/gui/cwx/editor/gui/dwt/properties.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dockingfolder.hpp"
#include "cwxeditor_src/lib/runtime_context.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include "platform/storage/storage.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace cwxeditor::cwx::editor::gui::dwt::properties {

namespace {

using Storage = cardwirth::platform::Storage;

[[nodiscard]] String lowerAscii(StringView value) {
    String result(value);
    for (char& ch : result) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
    }
    return result;
}

[[nodiscard]] Path toPath(StringView value) {
    return structs::pathFromUtf8(value);
}

[[nodiscard]] String pathString(const Path& value) {
    return structs::pathToUtf8(value);
}

[[nodiscard]] String managedSettingsFileName(const StringView configured) {
    constexpr StringView fallback = "cwxeditor.xml";
    if (configured.empty() || configured == "." || configured == ".." ||
        configured.find('/') != StringView::npos ||
        configured.find('\\') != StringView::npos ||
        configured.find(':') != StringView::npos) {
        return String(fallback);
    }
    const Path candidate = toPath(configured);
    if (candidate.empty() || candidate.has_root_path() || candidate != candidate.filename())
        return String(fallback);
    return String(configured);
}

void applyAllDirectoryDefaults(variables::FlexEtcProps& properties) {
    if (!lib::editor_window_host_options().all_mode) return;
    properties.tempPath.value = "editor/Temp";
    properties.backupPath.value = "editor/Backup";
    properties.backupBeforeSavePath.value = "editor/BackupBeforeSave";
}

// ALL の共通 Storage が所有するのは、書き込み可能なユーザールートだけ。 エ
// ディタのリソースや旧式のローカル cwxeditor.xml は実行ファイルの隣にあり
// 、 Webでは別の読み取り専用ルート /program に属する場合がある。 それらを
// ユーザー用 Storage に渡すのは意味上も誤りで、範囲外参照の検査にも拒否される。
[[nodiscard]] bool usesStorage(const Path& value, Storage* storage) {
    if (storage == nullptr) return false;
    if (value.is_relative()) return true;
    const Path root = storage->root().lexically_normal();
    const Path relative = value.lexically_normal().lexically_relative(root);
    if (relative.empty()) return value.lexically_normal() == root;
    if (relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first == relative.end() || *first != "..";
}

[[nodiscard]] bool pathExists(const Path& value) {
    std::error_code error;
    return std::filesystem::exists(value, error);
}

[[nodiscard]] bool pathExists(const Path& value, Storage* storage) {
    if (usesStorage(value, storage)) {
        try {
            return storage->exists(value);
        } catch (...) {
            return false;
        }
    }
    return pathExists(value);
}

[[nodiscard]] String readText(const Path& value) {
    std::ifstream stream(value, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open settings file: " + value.string());
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

[[nodiscard]] String readText(const Path& value, Storage* storage) {
    if (usesStorage(value, storage)) {
        const auto data = storage->read(value);
        if (!data) throw std::runtime_error("Cannot read settings file: " + pathString(value));
        return String(reinterpret_cast<const char*>(data->data()), data->size());
    }
    return readText(value);
}

void ensureParent(const Path& value) {
    const auto parent = value.parent_path();
    if (parent.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error) throw std::filesystem::filesystem_error("create_directories", parent, error);
}

void ensureParent(const Path& value, Storage* storage) {
    const auto parent = value.parent_path();
    if (parent.empty()) return;
    if (usesStorage(parent, storage)) {
        if (!storage->mkdir(parent, true)) {
            // ネイティブの Storage アダプターに相当するものはD版にない
            // 。 exists() が診断を置き換える前に、失敗理由を保存する。
            const auto reason = storage->last_error();
            if (!storage->exists(parent))
                throw std::runtime_error("Cannot create settings directory: " + pathString(parent) +
                                         (reason.empty() ? String{} : ": " + reason));
        }
        return;
    }
    ensureParent(value);
}

template <typename Property>
void scaleLayout(Property& property, double dpi_multiplier,
                 double initial_multiplier) {
    if constexpr (Property::LAYOUT_VALUE) {
        if (property.value != DEFAULT) {
            const double multiplier = property.readValue ? dpi_multiplier : initial_multiplier;
            property.value = static_cast<std::int32_t>(
                static_cast<double>(property.value) * multiplier);
        }
        if (property.INIT != DEFAULT) {
            property.INIT = static_cast<std::int32_t>(
                static_cast<double>(property.INIT) * initial_multiplier);
        }
    }
}

template <typename Window>
void scaleWindow(Window& window, double dpi_multiplier, double initial_multiplier) {
    scaleLayout(window._x, dpi_multiplier, initial_multiplier);
    scaleLayout(window._y, dpi_multiplier, initial_multiplier);
    scaleLayout(window._width, dpi_multiplier, initial_multiplier);
    scaleLayout(window._height, dpi_multiplier, initial_multiplier);
}

template <typename Dialog>
void scaleDialog(Dialog& dialog, double dpi_multiplier, double initial_multiplier) {
    scaleLayout(dialog._width, dpi_multiplier, initial_multiplier);
    scaleLayout(dialog._height, dpi_multiplier, initial_multiplier);
}

void scaleContents(ContWin& window, double dpi_multiplier,
                   double initial_multiplier) {
    scaleLayout(window._x, dpi_multiplier, initial_multiplier);
    scaleLayout(window._y, dpi_multiplier, initial_multiplier);
}

void scaleEtc(variables::FlexEtcProps& etc, double dpi_multiplier,
              double initial_multiplier) {
    // FlexEtcProps はプロパティの走査処理を非公開に
    // している。 公開されている配置型のプロパティをここで明示的
    // に全列挙し、 共通APIを広げずにDPI変換の漏れを防ぐ。
#define CWX_SCALE(name) scaleLayout(etc.name, dpi_multiplier, initial_multiplier)
    CWX_SCALE(fileNameColumn); CWX_SCALE(fileExtColumn); CWX_SCALE(fileCountColumn);
    CWX_SCALE(areaIdColumn); CWX_SCALE(areaNameColumn); CWX_SCALE(areaCountColumn);
    CWX_SCALE(importAreaIdColumn); CWX_SCALE(importAreaNameColumn); CWX_SCALE(importAreaCountColumn);
    CWX_SCALE(flagsWidth); CWX_SCALE(flagsHeight);
    CWX_SCALE(bgImageSampleWidth); CWX_SCALE(bgImageSampleHeight);
    CWX_SCALE(textCellPreviewWidth); CWX_SCALE(textCellPreviewHeight);
    CWX_SCALE(textCellBoxWidth); CWX_SCALE(textCellBoxHeight);
    CWX_SCALE(cardIdColumn); CWX_SCALE(cardNameColumn); CWX_SCALE(cardNumberColumn);
    CWX_SCALE(cardDescriptionColumn); CWX_SCALE(cardCountColumn);
    CWX_SCALE(handCardIdColumn); CWX_SCALE(handCardNameColumn); CWX_SCALE(handCardNumberColumn);
    CWX_SCALE(handCardDescriptionColumn); CWX_SCALE(importCardIdColumn);
    CWX_SCALE(importCardNameColumn); CWX_SCALE(importCardNumberColumn);
    CWX_SCALE(importCardDescriptionColumn); CWX_SCALE(importCardCountColumn);
    CWX_SCALE(importHandCardIdColumn); CWX_SCALE(importHandCardNameColumn);
    CWX_SCALE(importHandCardNumberColumn); CWX_SCALE(importHandCardDescriptionColumn);
    CWX_SCALE(couponWidth); CWX_SCALE(couponHeight); CWX_SCALE(couponValueColumn);
    CWX_SCALE(idColumn); CWX_SCALE(valueNumberColumn); CWX_SCALE(nameTableWidth);
    CWX_SCALE(nameTableHeight); CWX_SCALE(nameWidth); CWX_SCALE(firesWidth);
    CWX_SCALE(flagNameWidth); CWX_SCALE(flagInitWidth); CWX_SCALE(flagValueWidth);
    CWX_SCALE(flagNameColumn); CWX_SCALE(flagInitColumn);
    CWX_SCALE(variableInitializationColumn); CWX_SCALE(flagCountColumn);
    CWX_SCALE(localFlagNameColumn); CWX_SCALE(localFlagInitColumn);
    CWX_SCALE(localVariableInitializationColumn); CWX_SCALE(localFlagCountColumn);
    CWX_SCALE(filesWidth); CWX_SCALE(filesHeight); CWX_SCALE(talkersWidth);
    CWX_SCALE(motionsWidth); CWX_SCALE(incrementalSearchBoxWidth);
    CWX_SCALE(toolTipWidth); CWX_SCALE(colorExampleWidth); CWX_SCALE(buttonWidth);
    CWX_SCALE(radioGroupSeparatorWidth); CWX_SCALE(couponValueColumnWidth);
    CWX_SCALE(idNameColumn); CWX_SCALE(idCountColumn);
    CWX_SCALE(historyScenarioNameColumn); CWX_SCALE(historyScenarioPathColumn);
    CWX_SCALE(executionPartyNameColumn); CWX_SCALE(executionPartyYadoColumn);
    CWX_SCALE(executionPartyEngineColumn); CWX_SCALE(functionCallEditorWidth);
    CWX_SCALE(functionCallEditorHeight); CWX_SCALE(functionArgumentNameColumn);
    CWX_SCALE(functionArgumentTypeColumn); CWX_SCALE(functionArgumentValueColumn);
    CWX_SCALE(keyCodesByFeaturesFeatureColumn); CWX_SCALE(keyCodesByFeaturesKeyCodeColumn);
    CWX_SCALE(keyCodesByMotionsMotionColumn); CWX_SCALE(keyCodesByMotionsElementColumn);
    CWX_SCALE(keyCodesByMotionsKeyCodeColumn); CWX_SCALE(elementOverrideNameColumn);
    CWX_SCALE(elementOverrideTargetTypeColumn); CWX_SCALE(imageListWidth);
    CWX_SCALE(imageListHeight); CWX_SCALE(imageListForBgImagesWidth);
    CWX_SCALE(imageListForBgImagesHeight); CWX_SCALE(layerListWidth);
    CWX_SCALE(layerListHeight); CWX_SCALE(cardsMarginX); CWX_SCALE(cardsSpaceX);
    CWX_SCALE(cardsMarginY); CWX_SCALE(cardsSpaceY); CWX_SCALE(cardsTitleSpace);
    CWX_SCALE(cardsFocusLinePadding); CWX_SCALE(ignorePathsWidth);
    CWX_SCALE(menuSettingsHeight); CWX_SCALE(settingListWidth); CWX_SCALE(settingListHeight);
    CWX_SCALE(featureDefaultNameWidth); CWX_SCALE(featureVariantNameWidth);
    CWX_SCALE(featureManualNameWidth); CWX_SCALE(selectionWidth); CWX_SCALE(keyCodeWidth);
    CWX_SCALE(roundWidth); CWX_SCALE(searchResultTableWidth); CWX_SCALE(searchResultTableHeight);
    CWX_SCALE(detailAreaWidth); CWX_SCALE(searchResultColumnMain);
    CWX_SCALE(searchResultColumnParent); CWX_SCALE(searchResultColumnCouponCount);
    CWX_SCALE(searchResultColumnErrorDesc); CWX_SCALE(searchResultColumnScenario);
    CWX_SCALE(commentBoxDistance); CWX_SCALE(warningImageWidth);
    CWX_SCALE(warningImageWidthForTable); CWX_SCALE(warningImageWidthForTree);
    CWX_SCALE(warningImageWidthForCardList); CWX_SCALE(previewMaxWidth);
    CWX_SCALE(previewMaxHeight); CWX_SCALE(messageVarKindColumn);
    CWX_SCALE(messageVarValueColumn); CWX_SCALE(textVarKindColumn);
    CWX_SCALE(textVarValueColumn); CWX_SCALE(messageVarTableHeight);
    CWX_SCALE(scriptVarTableHeight);
#undef CWX_SCALE
}

[[nodiscard]] bool excluded(StringView name, const lib::Array<String>& names) {
    for (const auto& prefix : names) {
        if (name.size() >= prefix.size() && name.substr(0, prefix.size()) == prefix)
            return true;
    }
    return false;
}

[[nodiscard]] XNode copyElements(const XNode& source,
                                 const lib::Array<String>& exclude) {
    auto result = XNode::create(source.name(), source.value());
    for (const auto& attribute : source.attributes())
        result.newAttr(attribute.name, attribute.value);
    for (const auto& child : source.element_children()) {
        if (excluded(child.name(), exclude)) continue;
        result.append(copyElements(child, exclude));
    }
    return result;
}

void writeBytes(const Path& file, StringView text, Storage* storage = nullptr) {
    lib::Bytes bytes;
    bytes.reserve(text.size());
    for (const char value : text)
        bytes.push_back(static_cast<lib::Byte>(static_cast<unsigned char>(value)));
    if (!filesync::writeFileAndSync(file, lib::ByteView(bytes.data(), bytes.size()), storage))
        throw std::runtime_error("Cannot write settings file: " + file.string());
}

}  // namespace

bool applyManagedSessionDefaults(variables::FlexEtcProps& properties,
                                 cardwirth::platform::Storage* storage) {
    const auto previous = properties.lastScenario.value;
    String scenario;
    if (!previous.empty() && storage && !storage->root().empty()) {
        const auto path = toPath(previous);
        const auto candidate = (path.is_absolute() ? path : storage->root() / path)
                                   .lexically_normal();
        if (usesStorage(candidate, storage)) scenario = structs::pathToUtf8(candidate);
    }
    const bool changed = properties.scenarioPath.value != "Scenario" ||
        properties.tempPath.value != "editor/Temp" ||
        properties.backupPath.value != "editor/Backup" ||
        properties.backupBeforeSavePath.value != "editor/BackupBeforeSave" ||
        previous != scenario;
    properties.scenarioPath.value = "Scenario";
    properties.tempPath.value = "editor/Temp";
    properties.backupPath.value = "editor/Backup";
    properties.backupBeforeSavePath.value = "editor/BackupBeforeSave";
    // 以前の平文セッションへの参照は拒否するが、 ウィンドウ状態を
    // 保存・読み込みするたびに、現在開いているシナリオを消さない。
    properties.lastScenario.value = std::move(scenario);
    return changed;
}

void MainWin::toNode(XNode& parent, bool write_all) const {
    auto element = parent.newElement(XML_NAME, String{});
    detail::writeProperty(element, _x, write_all);
    detail::writeProperty(element, _y, write_all);
    detail::writeProperty(element, _width, write_all);
    detail::writeProperty(element, _height, write_all);
    detail::writeProperty(element, _maximized, write_all);
}

XNode MainWin::toNode(bool write_all) const {
    auto element = XNode::create(XML_NAME, String{});
    detail::writeProperty(element, _x, write_all);
    detail::writeProperty(element, _y, write_all);
    detail::writeProperty(element, _width, write_all);
    detail::writeProperty(element, _height, write_all);
    detail::writeProperty(element, _maximized, write_all);
    return element;
}

MainWin MainWin::fromNode(const XNode& parent, std::uint64_t data_version) {
    MainWin result;
    const auto element = detail::elementFor(parent, XML_NAME);
    if (!element.valid()) return result;
    detail::readProperty(element, result._x, data_version);
    detail::readProperty(element, result._y, data_version);
    detail::readProperty(element, result._width, data_version);
    detail::readProperty(element, result._height, data_version);
    detail::readProperty(element, result._maximized, data_version);
    return result;
}

void ContWin::toNode(XNode& parent, bool write_all) const {
    auto element = parent.newElement(XML_NAME, String{});
    detail::writeProperty(element, _x, write_all);
    detail::writeProperty(element, _y, write_all);
}

XNode ContWin::toNode(bool write_all) const {
    auto element = XNode::create(XML_NAME, String{});
    detail::writeProperty(element, _x, write_all);
    detail::writeProperty(element, _y, write_all);
    return element;
}

ContWin ContWin::fromNode(const XNode& parent, std::uint64_t data_version) {
    ContWin result;
    const auto element = detail::elementFor(parent, XML_NAME);
    if (!element.valid()) return result;
    detail::readProperty(element, result._x, data_version);
    detail::readProperty(element, result._y, data_version);
    return result;
}

void DockingFolderCTC::toNode(XNode& parent,
                              const lib::Array<String>& exclude) const {
    if (!layout.valid() || excluded(layout.name(), exclude)) return;
    if (implementation_) {
        std::vector<String> excluded_tabs;
        excluded_tabs.reserve(exclude.size());
        excluded_tabs.insert(excluded_tabs.end(), exclude.begin(), exclude.end());
        parent.append(implementation_->toNode(excluded_tabs));
        return;
    }
    parent.append(copyElements(layout, exclude));
}

std::shared_ptr<DockingFolderCTC> DockingFolderCTC::fromNode(
    const XNode& node, ContainerPtr parent, const std::int32_t style,
    HasCloseButton has_close_button, CanVanish can_vanish,
    CreateControl create, FirstResize first_resize) {
    const XNode element = node.name() == "dockingFolder"
                              ? node
                              : node.child("dockingFolder", false);
    if (!element.valid()) return {};

    auto result = std::make_shared<DockingFolderCTC>(element.clone());
    std::function<XNode(const XNode&)> find_first_tabs =
        [&](const XNode& current) -> XNode {
        if (current.name() == "tabs") return current;
        for (const auto& child : current.element_children()) {
            if (auto found = find_first_tabs(child); found.valid()) return found;
        }
        return {};
    };
    if (const auto first = find_first_tabs(element); first.valid())
        result->first_pane = first.attr("key", true);

    // 旧コールバックはDTOを受け取り、最初のペインや、方
    // 針コールバック未指定時のペインを許可しない。 ラッ
    // パーを循環参照で保持せず、共通モデルへ適合させる。
    const std::weak_ptr<DockingFolderCTC> legacy = result;
    auto vanish = [legacy, can_vanish = std::move(can_vanish)](
                      const ::cwxeditor::cwx::editor::gui::dwt::dockingfolder::DockingFolder& folder,
                      const StringView key) {
        const auto owner = legacy.lock();
        return owner && can_vanish && key != folder.firstKey() &&
               can_vanish(*owner, key);
    };
    result->implementation_ =
        ::cwxeditor::cwx::editor::gui::dwt::dockingfolder::DockingFolder::fromNode(
            element, std::move(parent), style, std::move(has_close_button),
            std::move(vanish), std::move(create), std::move(first_resize));
    return result;
}

FlexProps::FlexProps(String app_path, const lib::Services& services,
                     String conf_file_name, Storage* storage)
    : app_path_(std::move(app_path)), services_(&services), storage_(storage) {
    const bool managed_session = lib::is_managed_user_data_session(services);
    const bool scoped_settings = storage_ && !storage_->root().empty();
    const bool all_settings =
        (lib::editor_window_host_options().all_mode || managed_session) &&
        scoped_settings;
    if (managed_session && (storage_ == nullptr || storage_->root().empty()))
        throw std::runtime_error("managed UserData session requires scoped Editor storage");
    const Path app_file = toPath(app_path_);
    IniLocation location = IniLocation::Standard;
    String ini_file_name = "cwxeditor.xml";
    const Path app_directory = app_file.parent_path();
    const Path legacy_config = app_directory / ini_file_name;
    if (pathExists(legacy_config, storage_)) location = IniLocation::Local;

    if (!conf_file_name.empty()) {
        try {
            const Path config_path = toPath(conf_file_name);
            if (pathExists(config_path, storage_)) {
                const auto config = XNode::parse(readText(config_path, storage_));
                std::function<void(const XNode&)> visit = [&](const XNode& current) {
                    const auto name = lowerAscii(current.name());
                    if (name == "location") {
                        const auto value = lowerAscii(current.value());
                        if (value == "standard") location = IniLocation::Standard;
                        else if (value == "local") location = IniLocation::Local;
                        else if (value == "copy") location = IniLocation::Copy;
                        else if (value == "nothing") location = IniLocation::Nothing;
                    } else if (name == "file") {
                        ini_file_name = current.value();
                    }
                    for (const auto& child : current.element_children()) visit(child);
                };
                visit(config);
            }
        } catch (const std::exception& error) {
            utils::debugln("settings configuration: ", error.what());
        }
    }
    if (managed_session) {
        // 起動側が認証したセッションのルートを正とする。 XEditor_C.exe
        // の隣の旧設定ファイルや、移行した場所・ファイルの指定に
        // よって、 自動書き込み先をそのルートの外へ変更させない。
        location = IniLocation::Standard;
        ini_file_name = managedSettingsFileName(ini_file_name);
    }
    if (scoped_settings) {
        // 実行時に接続された Storage を、共通エディタの保存ディレ
        // クトリの正とする。 旧式のローカル・コピー先の指定で、実行ファイ
        // ルの隣やホストの UserData 直下の任意の場所へ書かない。
        location = IniLocation::Standard;
        ini_file_name = managedSettingsFileName(ini_file_name);
    }
    if (all_settings) {
        // 組み込み・管理対象のエディタセッションは、一つの永続的
        // な設定ディレクトリを持つ。 単独版の場所指定によって変
        // 更したり、nothing で保存を無効にしたりしない。
        location = IniLocation::Standard;
        ini_file_name = "cwxeditor.xml";
    }
    location_ = location;

    Path directory;
    switch (location_) {
    case IniLocation::Standard:
        // RuntimeContext は、アプリの範囲に限定した書き込み
        // 可能な Storage のルートを提供する。 単独版の設定もその
        // 中に置き、プロセスユーザーの共通 AppData へ黙って戻さな
        // い。 非表示・制御サーバー経由の実行も分離でき、再現可能になる。
        directory = storage_ && !storage_->root().empty()
            ? storage_->root() / (all_settings ? "editor/Config" : "Config")
            : utils::appDataDir(app_file) /
                  toPath(lib::editor_settings_directory_name(services));
        break;
    case IniLocation::Local:
        directory = app_directory;
        break;
    case IniLocation::Copy: {
        directory = storage_ && !storage_->root().empty()
            ? storage_->root() / "Config"
            : utils::appDataDir(app_file) /
                  toPath(lib::editor_settings_directory_name(services));
        const Path source = app_directory / ini_file_name;
        const Path destination = directory / ini_file_name;
        if (pathExists(source, storage_) && !pathExists(destination, storage_)) {
            try {
                ensureParent(destination, storage_);
                if (!usesStorage(source, storage_) ||
                    !usesStorage(destination, storage_) ||
                    !storage_->copy(source, destination, false)) {
                    std::filesystem::copy_file(source, destination);
                }
            } catch (const std::exception& error) {
                utils::debugln("settings copy: ", error.what());
            }
        }
        break;
    }
    case IniLocation::Nothing:
        no_file_ = true;
        directory = utils::appDataDir(app_file) /
                    toPath(lib::editor_settings_directory_name(services, true));
        break;
    }

    if (all_settings) {
        if (!storage_->mkdir(directory, true))
            throw std::runtime_error("エディタの設定フォルダーを作成できません: " +
                                     pathString(directory));
        // 不足している設定だけをコピーし、古い設定と保存済みの ALL 設
        // 定の両方を維持する。 ネイティブのデスクトップだけでなく、Web
        // と管理対象 UserData でも Storage を使う。
        for (const char* name : {"cwxeditor.xml", "cwxeditor.config.dock"}) {
            const Path destination = directory / name;
            const Path source = storage_->root() / "Config" / name;
            if (storage_->exists(destination) || !storage_->exists(source)) continue;
            const auto contents = storage_->read(source);
            if (!contents || !storage_->write_atomic(destination, *contents, true))
                throw std::runtime_error("以前のエディタ設定を引き継げません: " +
                                         pathString(source));
        }
    } else if (scoped_settings) {
        // 既存の単独版の設定を保ちつつ、<exe>/editor/Config
        // へ移行する。 元のファイルは変更しない。
        const Path destination = directory / ini_file_name;
        const Path source = app_directory / ini_file_name;
        if (!pathExists(destination, storage_) && pathExists(source, storage_)) {
            try {
                ensureParent(destination, storage_);
                if (!usesStorage(source, storage_) ||
                    !storage_->copy(source, destination, false)) {
                    std::filesystem::copy_file(source, destination);
                }
            } catch (const std::exception& error) {
                utils::debugln("standalone settings migration: ", error.what());
            }
        }
    }
    cwx_dir_ = pathString(directory);
    path_ = pathString(directory / ini_file_name);
    String diagnostics;
    if (!no_file_ && pathExists(toPath(path_), storage_)) {
        if (!reloadImpl(true, diagnostics)) createBackup();
    } else {
        const double dpi_multiplier = static_cast<double>(dpi_) / 96.0;
        etc.imageScale.value = static_cast<std::uint32_t>(
            std::max(1.0, dpi_multiplier));
        etc.eventTreeSlope.value = static_cast<std::int32_t>(
            static_cast<double>(etc.eventTreeSlope.value) * dpi_multiplier);
        switch (location_) {
        case IniLocation::Standard:
        case IniLocation::Copy:
            if (storage_ && !storage_->root().empty()) {
                etc.tempPath.value = "Temp";
                etc.backupPath.value = "Backup";
            } else {
                etc.tempPath.value = pathString(directory / "temp");
                etc.backupPath.value = pathString(directory / "backup");
            }
            break;
        case IniLocation::Nothing:
            no_file_temp_ = pathString(utils::createNewFileName(directory, true));
            etc.tempPath.value = pathString(toPath(no_file_temp_) / "temp");
            etc.backupPath.value = pathString(toPath(no_file_temp_) / "backup");
            break;
        case IniLocation::Local:
            etc.tempPath.value = "temp";
            etc.backupPath.value = "backup";
            break;
        }
        etc.backupBeforeSavePath.value = etc.backupPath.value;
    }
    if (managed_session) applyManagedSessionDefaults(etc, storage_);
    applyAllDirectoryDefaults(etc);
    if (all_settings) {
        for (const auto folder : {"editor/Temp", "editor/Backup",
                                  "editor/BackupBeforeSave"}) {
            if (!storage_->mkdir(folder, true) && !storage_->exists(folder))
                throw std::runtime_error("エディタの作業フォルダを作成できません: " + String(folder));
        }
    }
    // reloadImpl は、配置とともに保存されたDPIを dpi_ に読み込んだ直
    // 後。 D版の properties.d と同様、ここで現在の画面DPIへ変換する。
    // dpi_ をそのまま updatePPIs に返すと、倍率が常に1対1になる。
    (void)updatePPIs(currentDisplayDpi());
}

void FlexProps::cleanup() noexcept {
    if (!no_file_ || no_file_temp_.empty()) return;
    try {
        const auto path = toPath(no_file_temp_);
        if (!usesStorage(path, storage_) || !storage_->remove(path, true))
            utils::delAll(path);
    } catch (const std::exception& error) {
        utils::debugln("settings cleanup: ", error.what());
    }
}

bool FlexProps::reload() {
    if (no_file_) return true;
    String diagnostics;
    if (!reloadImpl(true, diagnostics)) return false;
    if (lib::is_managed_user_data_session(*services_))
        applyManagedSessionDefaults(etc, storage_);
    applyAllDirectoryDefaults(etc);
    return updatePPIs(currentDisplayDpi());
}

std::shared_ptr<DockingFolderCTC> FlexProps::loadDock(
    ContainerPtr parent, std::int32_t style, HasCloseButton has_close_button,
    CanVanish can_vanish, CreateControl create, FirstResize first_resize) {
    if (no_file_ || !pathExists(toPath(path_), storage_)) return {};
    constexpr int max_attempts = 100;
    for (int attempt = 0; attempt <= max_attempts; ++attempt) {
        try {
            XNode root = node_.valid() ? node_ :
                XNode::parse(readText(toPath(path_), storage_));
            return DockingFolderCTC::fromNode(root, parent, style, has_close_button,
                                              can_vanish, create, first_resize);
        } catch (const std::exception& error) {
            if (attempt == max_attempts) {
                utils::debugln("docking settings: ", error.what());
                createBackup();
                return {};
            }
        }
    }
    return {};
}

void FlexProps::save(const std::shared_ptr<DockingFolderCTC>& dock) {
    save(path_, dock);
}

void FlexProps::save(String xml_file_name,
                     const std::shared_ptr<DockingFolderCTC>& dock) {
    if (no_file_) return;
    if (lib::is_managed_user_data_session(*services_)) {
        if (storage_ == nullptr || storage_->root().empty() ||
            !usesStorage(toPath(xml_file_name), storage_)) {
            throw std::runtime_error(
                "managed UserData settings must remain inside the session root");
        }
        applyManagedSessionDefaults(etc, storage_);
    }
    applyAllDirectoryDefaults(etc);
    auto root = XNode::create("cwxeditor", String{});
    root.newAttr("version", versioninfo::APP_VERSION_NUM);
    root.newAttr("dpi", dpi_);
    writeNode(root);
    if (dock) dock->toNode(root, {"work"});
    const Path target = toPath(xml_file_name);
    ensureParent(target, storage_);
    writeBytes(target, root.text(), storage_);
}

std::int32_t FlexProps::currentDisplayDpi() const noexcept {
    constexpr std::int32_t baseline_dpi = 96;
    if (services_ == nullptr || !services_->system) return baseline_dpi;
    try {
        const float scale = services_->system->scale_factor();
        const double dpi = static_cast<double>(scale) * baseline_dpi;
        if (!std::isfinite(dpi) || dpi <= 0.0 ||
            dpi > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
            return baseline_dpi;
        }
        const auto rounded = static_cast<std::int32_t>(std::lround(dpi));
        return rounded > 0 ? rounded : baseline_dpi;
    } catch (...) {
        return baseline_dpi;
    }
}

bool FlexProps::updatePPIs(std::int32_t dpi) {
    if (dpi <= 0) return false;
    // 有効なDPIが保存されていない旧式・不正な設定は
    // 、分母ゼロではなく96DPIの配置として扱う。
    if (dpi_ <= 0) dpi_ = 96;
    if (dpi_ == dpi && dpi_ == 96) return true;
    const double dpi_multiplier = static_cast<double>(dpi) / static_cast<double>(dpi_);
    const double initial_multiplier = static_cast<double>(dpi) / 96.0;
    dpi_ = dpi;

    scaleWindow(mainWin, dpi_multiplier, initial_multiplier);
    scaleContents(contentsWin, dpi_multiplier, initial_multiplier);
    scaleDialog(settingsDlg, dpi_multiplier, initial_multiplier);
    scaleWindow(featuresWin, dpi_multiplier, initial_multiplier);
    scaleWindow(replaceDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(summaryDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(menuCardDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(areaBackgroundDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(areaBackgroundNFDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(areaTextCellDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(areaTextCellNFDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(areaColorCellDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(areaColorCellNFDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(areaPCCellDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(areaPCCellNFDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(enemyCardDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(castCardDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(skillCardDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(itemCardDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(beastCardDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(infoCardDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(bgImagesDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(defBgImagesDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(flagDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(stepDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(variantDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(newScDlg, dpi_multiplier, initial_multiplier);
    scaleWindow(speakDlg, dpi_multiplier, initial_multiplier);
    scaleWindow(msgDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(brMemberDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(cardEvtDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(flagEvtDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(effEvtDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(soundEvtDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(couponEvtDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(multiCouponEvtDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(inputEvtDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(gossipEvtDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(selEvtDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(loseBgImageEvtDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(scriptDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(commentDlg, dpi_multiplier, initial_multiplier);
    scaleWindow(dlgPrev, dpi_multiplier, initial_multiplier);
    scaleWindow(msgPrev, dpi_multiplier, initial_multiplier);
    scaleWindow(scriptVarSetDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(flagCombiDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(evTemplDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(toolBarCustomDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(importResultDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(scenarioLoadErrorsDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(eventIgnitionsDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(eventIgnitionsWithRoundsDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(scenarioHistoryDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(executedPartyHistoryDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(expressionDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(expressionWithTargetDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(xmlContentDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(selectFeaturesDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(addKeyCodesErrorDlg, dpi_multiplier, initial_multiplier);
    scaleDialog(editSkinTypeDlg, dpi_multiplier, initial_multiplier);
    scaleEtc(etc, dpi_multiplier, initial_multiplier);
    return true;
}

bool FlexProps::reloadImpl(bool force, String& diagnostics) {
    diagnostics.clear();
    detail::PropertyReadDiagnostics field_diagnostics(diagnostics);
    if (no_file_) return true;
    try {
        if (!pathExists(toPath(path_), storage_)) return true;
        node_ = XNode::parse(readText(toPath(path_), storage_));
        if (node_.name() != "cwxeditor" && node_.name() != "CWXEditor") {
            diagnostics = "設定XMLのルート要素がcwxeditorではありません: " +
                          node_.name();
            // D版の再読み込みは無関係なルートを受け付け、設定を読
            // み込まないまま後の保存で別文書を上書きする場合がある
            // 。 ここでは意図的に拒否し、バックアップ・復旧の保護
            // を維持する。 構文が正しくても無関係なXMLは不正な
            // 設定ファイルと同じに扱い、 コンストラクターの既存
            // の復旧経路で、既定値の保存前に元の文書を保護する。
            node_ = XNode{};
            return false;
        }
        const auto data_version = node_.attr<std::uint64_t>("version", false, 0);
        if (force) mainWin = MainWin::fromNode(node_, data_version);
        if (force) areaSceneWin = AreaSceneWin::fromNode(node_, data_version);
        if (force) areaEventWin = AreaEventWin::fromNode(node_, data_version);
        if (force) battleSceneWin = BattleSceneWin::fromNode(node_, data_version);
        if (force) battleEventWin = BattleEventWin::fromNode(node_, data_version);
        if (force) packageWin = PackageWin::fromNode(node_, data_version);
        if (force) cardEventWin = CardEventWin::fromNode(node_, data_version);
        if (force) contentsWin = ContWin::fromNode(node_, data_version);
        if (force) settingsDlg = decltype(settingsDlg)::fromNode(node_, data_version);
        if (force) featuresWin = decltype(featuresWin)::fromNode(node_, data_version);
        if (force) replaceDlg = decltype(replaceDlg)::fromNode(node_, data_version);
        if (force) summaryDlg = decltype(summaryDlg)::fromNode(node_, data_version);
        if (force) menuCardDlg = decltype(menuCardDlg)::fromNode(node_, data_version);
        if (force) areaBackgroundDlg = decltype(areaBackgroundDlg)::fromNode(node_, data_version);
        if (force) areaBackgroundNFDlg = decltype(areaBackgroundNFDlg)::fromNode(node_, data_version);
        if (force) areaTextCellDlg = decltype(areaTextCellDlg)::fromNode(node_, data_version);
        if (force) areaTextCellNFDlg = decltype(areaTextCellNFDlg)::fromNode(node_, data_version);
        if (force) areaColorCellDlg = decltype(areaColorCellDlg)::fromNode(node_, data_version);
        if (force) areaColorCellNFDlg = decltype(areaColorCellNFDlg)::fromNode(node_, data_version);
        if (force) areaPCCellDlg = decltype(areaPCCellDlg)::fromNode(node_, data_version);
        if (force) areaPCCellNFDlg = decltype(areaPCCellNFDlg)::fromNode(node_, data_version);
        if (force) enemyCardDlg = decltype(enemyCardDlg)::fromNode(node_, data_version);
        if (force) castCardDlg = decltype(castCardDlg)::fromNode(node_, data_version);
        if (force) skillCardDlg = decltype(skillCardDlg)::fromNode(node_, data_version);
        if (force) itemCardDlg = decltype(itemCardDlg)::fromNode(node_, data_version);
        if (force) beastCardDlg = decltype(beastCardDlg)::fromNode(node_, data_version);
        if (force) infoCardDlg = decltype(infoCardDlg)::fromNode(node_, data_version);
        if (force) bgImagesDlg = decltype(bgImagesDlg)::fromNode(node_, data_version);
        if (force) defBgImagesDlg = decltype(defBgImagesDlg)::fromNode(node_, data_version);
        if (force) flagDlg = decltype(flagDlg)::fromNode(node_, data_version);
        if (force) stepDlg = decltype(stepDlg)::fromNode(node_, data_version);
        if (force) variantDlg = decltype(variantDlg)::fromNode(node_, data_version);
        if (force) newScDlg = decltype(newScDlg)::fromNode(node_, data_version);
        if (force) speakDlg = decltype(speakDlg)::fromNode(node_, data_version);
        if (force) msgDlg = decltype(msgDlg)::fromNode(node_, data_version);
        if (force) brMemberDlg = decltype(brMemberDlg)::fromNode(node_, data_version);
        if (force) cardEvtDlg = decltype(cardEvtDlg)::fromNode(node_, data_version);
        if (force) flagEvtDlg = decltype(flagEvtDlg)::fromNode(node_, data_version);
        if (force) effEvtDlg = decltype(effEvtDlg)::fromNode(node_, data_version);
        if (force) soundEvtDlg = decltype(soundEvtDlg)::fromNode(node_, data_version);
        if (force) couponEvtDlg = decltype(couponEvtDlg)::fromNode(node_, data_version);
        if (force) multiCouponEvtDlg = decltype(multiCouponEvtDlg)::fromNode(node_, data_version);
        if (force) inputEvtDlg = decltype(inputEvtDlg)::fromNode(node_, data_version);
        if (force) gossipEvtDlg = decltype(gossipEvtDlg)::fromNode(node_, data_version);
        if (force) selEvtDlg = decltype(selEvtDlg)::fromNode(node_, data_version);
        if (force) loseBgImageEvtDlg = decltype(loseBgImageEvtDlg)::fromNode(node_, data_version);
        if (force) scriptDlg = decltype(scriptDlg)::fromNode(node_, data_version);
        if (force) commentDlg = decltype(commentDlg)::fromNode(node_, data_version);
        if (force) dlgPrev = decltype(dlgPrev)::fromNode(node_, data_version);
        if (force) msgPrev = decltype(msgPrev)::fromNode(node_, data_version);
        if (force) scriptVarSetDlg = decltype(scriptVarSetDlg)::fromNode(node_, data_version);
        if (force) flagCombiDlg = decltype(flagCombiDlg)::fromNode(node_, data_version);
        if (force) evTemplDlg = decltype(evTemplDlg)::fromNode(node_, data_version);
        if (force) toolBarCustomDlg = decltype(toolBarCustomDlg)::fromNode(node_, data_version);
        if (force) importResultDlg = decltype(importResultDlg)::fromNode(node_, data_version);
        if (force) scenarioLoadErrorsDlg = decltype(scenarioLoadErrorsDlg)::fromNode(node_, data_version);
        if (force) eventIgnitionsDlg = decltype(eventIgnitionsDlg)::fromNode(node_, data_version);
        if (force) eventIgnitionsWithRoundsDlg = decltype(eventIgnitionsWithRoundsDlg)::fromNode(node_, data_version);
        if (force) scenarioHistoryDlg = decltype(scenarioHistoryDlg)::fromNode(node_, data_version);
        if (force) executedPartyHistoryDlg = decltype(executedPartyHistoryDlg)::fromNode(node_, data_version);
        if (force) expressionDlg = decltype(expressionDlg)::fromNode(node_, data_version);
        if (force) expressionWithTargetDlg = decltype(expressionWithTargetDlg)::fromNode(node_, data_version);
        if (force) xmlContentDlg = decltype(xmlContentDlg)::fromNode(node_, data_version);
        if (force) selectFeaturesDlg = decltype(selectFeaturesDlg)::fromNode(node_, data_version);
        if (force) addKeyCodesErrorDlg = decltype(addKeyCodesErrorDlg)::fromNode(node_, data_version);
        if (force) editSkinTypeDlg = decltype(editSkinTypeDlg)::fromNode(node_, data_version);
        if (force) menu = menu::MenuProps::fromNode(node_);
        if (force) etc = variables::FlexEtcProps::fromNode(node_, data_version);
        dpi_ = node_.attr<std::int32_t>("dpi", false, 96);

        if (data_version < 2012072700)
            etc.backupBeforeSavePath.value = etc.backupPath.value;
        if (data_version < 2014100400) {
            if (etc.straightEventTreeView.value && etc.gentleAngleEventTree.value)
                etc.eventTreeSlope.value = static_cast<std::int32_t>(8.0 * dpi_ / 96.0);
            else
                etc.eventTreeSlope.value = 0;
        }
        if (data_version < 2014102400) {
            bool has_with_party = false;
            for (const auto& group : etc.mainToolBar.value.tools) {
                for (const auto& tool : group) {
                    if (tool.menu == types::MenuID::ExecEngineWithParty) {
                        has_with_party = true;
                        break;
                    }
                }
                if (has_with_party) break;
            }
            if (!has_with_party) {
                for (auto& group : etc.mainToolBar.value.tools) {
                    const auto found = std::find_if(
                        group.begin(), group.end(), [](const structs::Tool& tool) {
                            return tool.menu == types::MenuID::ExecEngine;
                        });
                    if (found != group.end()) {
                        group.insert(found + 1, structs::Tool(types::MenuID::ExecEngineWithParty));
                        break;
                    }
                }
            }
        }
        if (data_version < 2016091800) {
            etc.imageScale.value = static_cast<std::uint32_t>(
                std::max(1.0, static_cast<double>(dpi_) / 96.0));
            etc.eventTreeSlope.value = static_cast<std::int32_t>(
                static_cast<double>(etc.eventTreeSlope.value) * dpi_ / 96.0);
        }
        etc.eventTreeSlope.INIT = std::max(etc.eventTreeSlope.INIT,
                                           etc.eventTreeSlope.value);
        if (data_version < versioninfo::APP_VERSION_NUM) {
            for (auto& engine : etc.classicEngines.value) {
                if (engine.type.empty()) {
                    engine.type = skin::skinTypeByClassicEngine(
                        etc.skinTypesByClassicEngines.value, engine.enginePath,
                        etc.defaultSkin.value);
                }
            }
        }
        // すべてのグループを処理するが、エラー時にバックアップする既存の経路は維持する。
        return diagnostics.empty();
    } catch (const std::exception& error) {
        diagnostics = error.what();
        utils::debugln("settings reload: ", diagnostics);
        return false;
    }
}

void FlexProps::createBackup() noexcept {
    if (no_file_ || !pathExists(toPath(path_), storage_)) return;
    try {
        if (services_ == nullptr || !services_->system) return;
        const auto suffix = services_->system->format_local_time(
            std::chrono::system_clock::now(), "%Y%m%d%H%M%S");
        if (!suffix) return;
        const Path backup = toPath(path_ + ".bak." + *suffix);
        ensureParent(backup, storage_);
        if (usesStorage(toPath(path_), storage_) && usesStorage(backup, storage_)) {
            if (!storage_->copy(toPath(path_), backup, true))
                utils::debugln("settings backup: storage copy failed");
        } else {
            std::error_code error;
            std::filesystem::copy_file(toPath(path_), backup,
                                       std::filesystem::copy_options::overwrite_existing,
                                       error);
            if (error) utils::debugln("settings backup: ", error.message());
        }
    } catch (const std::exception& error) {
        utils::debugln("settings backup: ", error.what());
    }
}

void FlexProps::writeNode(XNode& node) const {
    mainWin.toNode(node); areaSceneWin.toNode(node); areaEventWin.toNode(node);
    battleSceneWin.toNode(node); battleEventWin.toNode(node); packageWin.toNode(node);
    cardEventWin.toNode(node); contentsWin.toNode(node); settingsDlg.toNode(node);
    featuresWin.toNode(node); replaceDlg.toNode(node); summaryDlg.toNode(node);
    menuCardDlg.toNode(node); areaBackgroundDlg.toNode(node);
    areaBackgroundNFDlg.toNode(node); areaTextCellDlg.toNode(node);
    areaTextCellNFDlg.toNode(node); areaColorCellDlg.toNode(node);
    areaColorCellNFDlg.toNode(node); areaPCCellDlg.toNode(node);
    areaPCCellNFDlg.toNode(node); enemyCardDlg.toNode(node); castCardDlg.toNode(node);
    skillCardDlg.toNode(node); itemCardDlg.toNode(node); beastCardDlg.toNode(node);
    infoCardDlg.toNode(node); bgImagesDlg.toNode(node); defBgImagesDlg.toNode(node);
    flagDlg.toNode(node); stepDlg.toNode(node); variantDlg.toNode(node);
    newScDlg.toNode(node); speakDlg.toNode(node); msgDlg.toNode(node);
    brMemberDlg.toNode(node); cardEvtDlg.toNode(node); flagEvtDlg.toNode(node);
    effEvtDlg.toNode(node); soundEvtDlg.toNode(node); couponEvtDlg.toNode(node);
    multiCouponEvtDlg.toNode(node); inputEvtDlg.toNode(node); gossipEvtDlg.toNode(node);
    selEvtDlg.toNode(node); loseBgImageEvtDlg.toNode(node); scriptDlg.toNode(node);
    commentDlg.toNode(node); dlgPrev.toNode(node); msgPrev.toNode(node);
    scriptVarSetDlg.toNode(node); flagCombiDlg.toNode(node); evTemplDlg.toNode(node);
    toolBarCustomDlg.toNode(node); importResultDlg.toNode(node);
    scenarioLoadErrorsDlg.toNode(node); eventIgnitionsDlg.toNode(node);
    eventIgnitionsWithRoundsDlg.toNode(node); scenarioHistoryDlg.toNode(node);
    executedPartyHistoryDlg.toNode(node); expressionDlg.toNode(node);
    expressionWithTargetDlg.toNode(node); xmlContentDlg.toNode(node);
    selectFeaturesDlg.toNode(node); addKeyCodesErrorDlg.toNode(node);
    editSkinTypeDlg.toNode(node); menu.toNode(node); etc.toNode(node);
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::properties
