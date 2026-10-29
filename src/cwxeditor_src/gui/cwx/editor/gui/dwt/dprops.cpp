#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/lib/runtime_context.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/editor_diagnostics.hpp"

#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/filesync.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "platform/storage/storage.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::dprops {
namespace {

using Path = std::filesystem::path;
using ::cwxeditor::cwx::structs::pathFromUtf8;
using ::cwxeditor::cwx::structs::pathToUtf8;

[[nodiscard]] String managedSessionPath(cardwirth::platform::Storage* storage,
                                        const StringView relative) {
    if (storage == nullptr || storage->root().empty())
        throw std::runtime_error("managed UserData session requires scoped Editor storage");
    return pathToUtf8(storage->root() / pathFromUtf8(relative));
}

[[nodiscard]] String configuredStoragePath(cardwirth::platform::Storage* storage,
                                           const StringView configured) {
    const auto path = pathFromUtf8(configured);
    if (storage && !storage->root().empty() && !path.is_absolute())
        return pathToUtf8(storage->root() / path);
    return {};
}

Path normalizedPath(Path value) {
    std::error_code error;
    if (value.is_relative()) {
        value = std::filesystem::absolute(value, error);
        if (error) error.clear();
    }
    return value.lexically_normal();
}

String lowerAscii(String value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const char character) {
        if (character >= 'A' && character <= 'Z') return static_cast<char>(character - 'A' + 'a');
        return character;
    });
    return value;
}

String currentLocale(const lib::Services& services) {
    if (services.system) {
        auto locale = services.system->locale_name();
        if (!locale.empty()) return lowerAscii(std::move(locale));
    }
    const auto environmentValue = [&services](const StringView name) {
        if (!services.system) return String{};
        const auto value = services.system->environment(name);
        return value ? *value : String{};
    };
    String locale = environmentValue("LC_ALL");
    if (locale.empty()) locale = environmentValue("LC_MESSAGES");
    if (locale.empty()) locale = environmentValue("LANG");
    if (locale.empty()) return String{};
    const auto encoding = locale.find('.');
    if (encoding != String::npos) locale.erase(encoding);
    const auto modifier = locale.find('@');
    if (modifier != String::npos) locale.erase(modifier);
    return lowerAscii(std::move(locale));
}

std::int32_t scaledSignedPercent(const std::int32_t value,
                                 const std::uint32_t percent) noexcept {
    const auto product = static_cast<std::int64_t>(value) * percent;
    const auto scaled = product >= 0 ? (product + 50) / 100 : (product - 50) / 100;
    if (scaled > std::numeric_limits<std::int32_t>::max()) {
        return std::numeric_limits<std::int32_t>::max();
    }
    if (scaled < std::numeric_limits<std::int32_t>::min()) {
        return std::numeric_limits<std::int32_t>::min();
    }
    return static_cast<std::int32_t>(scaled);
}

std::uint32_t scaledUnsignedPercent(const std::uint32_t value,
                                    const std::uint32_t percent) noexcept {
    const auto scaled = (static_cast<std::uint64_t>(value) * percent + 50U) / 100U;
    if (scaled > std::numeric_limits<std::uint32_t>::max()) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(scaled);
}

std::uint8_t channel(const std::uint32_t value) noexcept {
    return static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, value));
}

bool startsWith(const StringView value, const StringView prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool hasTextKey(const StringView key) noexcept {
    return key == "languageDir" || key == "languageFile" || key == "targetVersion" ||
           key == "engine" || key == "engineScript" || key == "enginePath" ||
           key == "pythonCommand" || key == "dataDir" || key == "defaultSkin" ||
           key == "defaultSkinName" || key == "tempPath" || key == "backupPath" ||
           key == "backupBeforeSavePath" || key == "newScenarioName" || key == "newAreaName";
}

bool hasBoolKey(const StringView key) noexcept {
    return key == "useSystemLanguage" || key == "backupEnabled" || key == "autoSave" ||
           key == "backupRefAuthor" || key == "backupBeforeSaveEnabled" ||
           key == "backupArchived" || key == "findEnginePath" || key == "saveSkinName" ||
           key == "viewCards" || key == "viewBgImages" || key == "showGrid";
}

bool hasNumberKey(const StringView key) noexcept {
    return key == "imageScale" || key == "drawingScale" ||
           key == "imageScalePercent" || key == "drawingScalePercent" || key == "tabs" ||
           key == "backupInterval" || key == "backupIntervalEdit" || key == "backupCount" ||
           key == "messageAlpha" || key == "partyCardAlpha" || key == "comboVisibleItemCount";
}

}  // namespace

Images::Images(String appPath, lib::Services services)
    : app_path_(std::move(appPath)),
      services_(std::move(services)),
      standard_images_(app_path_, services_) {}

Images::~Images() { disposeImages(); }

String Images::defaultPath(const StringView filename) const {
    return pathToUtf8(pathFromUtf8(app_path_).parent_path() / pathFromUtf8("resource") /
                      pathFromUtf8(filename));
}

Image Images::load(StringView path) {
    Path value = pathFromUtf8(path);
    if (value.is_relative()) {
        value = pathFromUtf8(app_path_).parent_path() / pathFromUtf8("resource") / value;
    }
    return loadPath(value);
}

Image Images::load(const Path& path) { return loadPath(path); }

void Images::requestApplicationIcon(const Path& executable,
                                    const lib::Size requested_size,
                                    std::function<void(Image)> completed,
                                    std::function<bool()> still_needed) {
    standard_images_.requestApplicationIcon(executable, requested_size,
                                            std::move(completed),
                                            std::move(still_needed));
}

Image Images::handWith(const std::uint32_t scale, const bool empty) {
    return empty ? standard_images_.handEmptyWith(scale) : standard_images_.handWith(scale);
}

Image Images::eventTreeWith(const std::uint32_t scale, const bool empty) {
    return empty ? standard_images_.eventTreeEmptyWith(scale) : standard_images_.eventTreeWith(scale);
}

lib::Size Images::imageSize(const Image image) const noexcept {
    return standard_images_.imageSize(image);
}

Image Images::motion(const types::MType type) { return standard_images_.motion(type); }

Image Images::msnDelete() { return standard_images_.msnDelete(); }

Image Images::menu(const types::MenuID id) { return standard_images_.menu(id); }

Image Images::flag() { return standard_images_.flag(); }

Image Images::step() { return standard_images_.step(); }

Image Images::variant() { return standard_images_.variant(); }

Image Images::variantRef() { return standard_images_.variantRef(); }

Image Images::functions() { return standard_images_.functions(); }
Image Images::setBeast() const { return standard_images_.setBeast(); }

Image Images::warning() const { return standard_images_.warning(); }

Image Images::script() const { return standard_images_.script(); }

Image Images::talker(const types::Talker talker) { return standard_images_.talker(talker); }

Image Images::scTalker(const types::Talker talker) { return standard_images_.scTalker(talker); }

Image Images::playerCardName() { return standard_images_.playerCardName(); }

Image Images::selectedPlayerCardNumber() { return standard_images_.selectedPlayerCardNumber(); }

Image Images::color(const char32_t code) { return standard_images_.color(code); }

Image Images::scRef() { return standard_images_.scRef(); }

Image Images::scTeam() { return standard_images_.scTeam(); }

Image Images::scYado() { return standard_images_.scYado(); }

Image Images::cards() { return standard_images_.cards(); }

Image Images::backs() { return standard_images_.backs(); }

Image Images::bgm() { return standard_images_.bgm(); }

Image Images::se() { return standard_images_.se(); }

Image Images::folder() { return standard_images_.folder(); }

Image Images::unknown() { return standard_images_.unknown(); }

Image Images::text() { return standard_images_.text(); }

Image Images::flagDir() { return standard_images_.flagDir(); }

Image Images::casts() { return standard_images_.casts(); }

Image Images::skill() { return standard_images_.skill(); }

Image Images::item() { return standard_images_.item(); }

Image Images::beast() { return standard_images_.beast(); }

Image Images::info() { return standard_images_.info(); }

Image Images::area() { return standard_images_.area(); }

Image Images::battle() { return standard_images_.battle(); }

Image Images::packages() { return standard_images_.packages(); }

Image Images::coupon(const ::cwxeditor::cwx::features::CouponType type,
                     const std::int32_t value) {
    using CouponType = ::cwxeditor::cwx::features::CouponType;
    switch (type) {
    case CouponType::System: return standard_images_.couponSystem();
    case CouponType::Dur: return standard_images_.couponDur();
    case CouponType::DurBattle: return standard_images_.couponDurBattle();
    default: break;
    }
    return value > 1 ? standard_images_.couponHigh()
        : value > 0 ? standard_images_.couponPlus()
        : value < 0 ? standard_images_.couponMinus() : standard_images_.couponNormal();
}

Image Images::loadPath(const Path& input) {
    // 組み込み・独自画像とも、一つの読み込み処理で倍率別画像の選択、埋め込
    // みへの代替、透過色、アップロード、成功時だけのキャッシュを管理する。
    return standard_images_.load(pathToUtf8(normalizedPath(input)));
}

Image Images::element(const types::Element element) {
    String path;
    const auto configured = element_paths_.find(element);
    if (configured != element_paths_.end() && !configured->second.empty()) {
        path = configured->second;
    } else {
        switch (element) {
        case types::Element::All: path = defaultPath("elm_all.png"); break;
        case types::Element::Health: path = defaultPath("elm_health.png"); break;
        case types::Element::Mind: path = defaultPath("elm_mind.png"); break;
        case types::Element::Miracle: path = defaultPath("elm_miracle.png"); break;
        case types::Element::Magic: path = defaultPath("elm_magic.png"); break;
        case types::Element::Fire: path = defaultPath("elm_fire.png"); break;
        case types::Element::Ice: path = defaultPath("elm_ice.png"); break;
        }
    }
    return load(StringView{path});
}

Image Images::elementHearing() {
    const String path = hearing_path_.empty() ? defaultPath("elm_hearing.png") : hearing_path_;
    return load(StringView{path});
}

Image Images::elementVision() {
    const String path = vision_path_.empty() ? defaultPath("elm_vision.png") : vision_path_;
    return load(StringView{path});
}

Image Images::elementElectronic() {
    const String path = electronic_path_.empty() ? defaultPath("elm_electronic.png") : electronic_path_;
    return load(StringView{path});
}

void Images::setElementPath(const types::Element element, String path) {
    // Dの非公開SWT Imageとは意図的に異なるRAII共有。ID
    // は、設定を考慮する一つの登録表から、その破棄まで借用する。
    element_paths_[element] = std::move(path);
}
void Images::setElementHearingPath(String path) { hearing_path_=std::move(path); }
void Images::setElementVisionPath(String path) { vision_path_=std::move(path); }
void Images::setElementElectronicPath(String path) {
    electronic_path_=std::move(path);
}

void Images::disposeImages() noexcept {
    standard_images_.disposeImages();
}

Props::Props(String confFilePath, props::CProps& parent, lib::Services services,
             cardwirth::platform::Storage* storage)
    : parent_(&parent),
      config_file_path_(std::move(confFilePath)),
      services_(std::move(services)),
      storage_(storage),
      images_(parent.appPath(), services_),
      flex_(parent.appPath(), services_, config_file_path_, storage_),
      committed_flex_(flex_),
      var_(flex_.etc) {
    initialize(config_file_path_);
}

Props::Props(props::CProps& parent, lib::Services services)
    : Props(String{}, parent, std::move(services)) {}

Props::Props(String appPath, const system::System& sys, String confFilePath,
             lib::Services services)
    : owned_parent_(std::make_unique<props::CProps>(std::move(appPath), sys)),
      parent_(owned_parent_.get()),
      config_file_path_(std::move(confFilePath)),
      services_(std::move(services)),
      storage_(nullptr),
      images_(parent_->appPath(), services_),
      flex_(parent_->appPath(), services_, config_file_path_, storage_),
      committed_flex_(flex_),
      var_(flex_.etc) {
    initialize(config_file_path_);
}

void Props::initialize(const StringView) {
    // 設定全体の読み込みはFlexPropsが管理する。ここでFlexEtcProps
    // だけを読むと、FlexPropsが扱うメニュー、ツールバー
    // 、座標、ドックの設定を黙って失うため、一か所に集約する。
    selectLanguage();
    committed_flex_ = flex_;
    committed_ = var_;
    committed_serialized_ = serializeProperties();
}

void Props::loadConfiguration() {
    // 旧呼び出し側向けの非公開の互換フックとして残す。完全なFlexProps
    // はinitialize()前に構築・読み込み済み。
}

void Props::selectLanguage() {
    if (!parent_) return;
    try {
        lib::Dictionary<String, String> tableFiles;
        String defaultLocale;
        auto table = parent_->msgsTable(var_.languageDir.value, tableFiles, defaultLocale);
        String locale = currentLocale(services_);
        auto selected = table.find(locale);
        // Dはカルチャ名が完全一致する場合だけ設定する。一覧が
        // なければ現在の親のメッセージ一覧を維持し、ホストの
        // ロケール取得や明示的な言語ファイル選択は変えない。
        if (selected != table.end() && selected->second) parent_->msgs(std::move(selected->second));
    } catch (...) {
        // 意図的な追加機能。Dの起動失敗とは異なり、システム
        // ロケールや言語一覧の探索が失敗しても、既存・既定の
        // CPropsのメッセージ一覧を保持して継続する。
    }

    if (!var_.useSystemLanguage.value && !var_.languageFile.value.empty()) {
        try {
            const Path directory = pathFromUtf8(parent_->toAppAbs(var_.languageDir.value));
            const Path language = directory / pathFromUtf8(var_.languageFile.value);
            std::error_code error;
            if (std::filesystem::is_regular_file(language, error) && !error) {
                parent_->loadMsgs(pathToUtf8(language));
            }
        } catch (...) {
            // CProps::loadMsgsも、独自言語ファイ
            // ルを読めない場合は意図的に以前の一覧を維持する。
        }
    }
}

String Props::enginePath() const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d85(__FILE__, __LINE__);
    if (lib::editor_window_host_options().all_mode) return parent_->appPath();
    if (!var_.enginePath.value.empty()) return parent_->toAppAbs(var_.enginePath.value);

    // 管理対象・ポータブルのUserDataは、EditorのenginePath
    // がなくても有効。単体配布はXEditor_Cの隣にEngine
    // を置くため、空スキンへ切り替える前にその配置を解決
    // する。カード・背景のスキン資源に加え、「Engine
    // を実行」の参照先も実際の実行ファイルへ向ける。
    const Path application = pathFromUtf8(parent_->appPath());
    const Path directory = application.has_filename() ? application.parent_path()
                                                      : application;
    const bool windows = services_.system &&
                         services_.system->platform() == lib::Platform::Windows;
    const std::array<StringView, 2> candidates = windows
        ? std::array<StringView, 2>{"CardWirthPy_C.exe", "CardWirthPy.exe"}
        : std::array<StringView, 2>{"CardWirthPy_C", "CardWirthPy"};
    std::error_code error;
    for (const auto candidate_name : candidates) {
        const Path candidate = directory / pathFromUtf8(candidate_name);
        if (std::filesystem::is_regular_file(candidate, error) && !error)
            return pathToUtf8(candidate);
        error.clear();
    }
    return {};
}

String Props::tempPath() const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d95(__FILE__, __LINE__);
    if (lib::editor_window_host_options().all_mode ||
        lib::is_managed_user_data_session(services_))
        return managedSessionPath(storage_, "editor/Temp");
    if (var_.tempPath.value.empty()) return String{};
    if (const auto scoped = configuredStoragePath(storage_, var_.tempPath.value);
        !scoped.empty()) return scoped;
    return parent_->toAppAbs(var_.tempPath.value);
}

String Props::backupPath() const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d105(__FILE__, __LINE__);
    if (lib::editor_window_host_options().all_mode ||
        lib::is_managed_user_data_session(services_))
        return managedSessionPath(storage_, "editor/Backup");
    if (var_.backupPath.value.empty()) return String{};
    if (const auto scoped = configuredStoragePath(storage_, var_.backupPath.value);
        !scoped.empty()) return scoped;
    return parent_->toAppAbs(var_.backupPath.value);
}

String Props::backupBeforeSavePath() const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d115(__FILE__, __LINE__);
    if (lib::editor_window_host_options().all_mode ||
        lib::is_managed_user_data_session(services_))
        return managedSessionPath(storage_, "editor/BackupBeforeSave");
    if (var_.backupBeforeSavePath.value.empty()) return String{};
    if (const auto scoped = configuredStoragePath(
            storage_, var_.backupBeforeSavePath.value); !scoped.empty()) return scoped;
    return parent_->toAppAbs(var_.backupBeforeSavePath.value);
}

String Props::toAppAbs(const StringView path) const { return parent_->toAppAbs(path); }

String Props::buildTool(const types::MenuID id) const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d147(__FILE__, __LINE__);
    if (tool_builder_) return tool_builder_(parent(), id);
    return flex_.menu.buildTool(parent(), id);
}

String Props::buildMenu(const types::MenuID id) const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d151(__FILE__, __LINE__);
    if (menu_builder_) return menu_builder_(parent(), id);
    return flex_.menu.buildMenu(parent(), id);
}

bool Props::targetVersion(const summary::Summary* summ, const StringView ver) const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d157(__FILE__, __LINE__);
    if (summ && !summ->legacy()) return true;
    return parent_->targetVersion(ver, var_.targetVersion.value);
}

bool Props::targetVersion(const bool legacy, const StringView ver) const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d163(__FILE__, __LINE__);
    if (!legacy) return true;
    return parent_->targetVersion(ver, var_.targetVersion.value);
}

bool Props::isTargetVersion(const summary::Summary* summ, const StringView ver) const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d170(__FILE__, __LINE__);
    if (summ) return props::detail::summaryIsTargetVersion(*summ, ver);
    return var_.targetVersion.value == "CardWirthPy";
}

bool Props::isTargetVersion(const bool legacy, const StringView dataVersion,
                            const StringView wsnVer) const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d174(__FILE__, __LINE__);
    return props::detail::summaryIsTargetVersionWith(legacy, dataVersion, wsnVer);
}

std::int32_t Props::s(const std::int32_t value) const noexcept {
    return scaledSignedPercent(value, imageScalePercent());
}

std::uint32_t Props::imageScalePercent() const noexcept {
    return variables::previewScalePercent(
        var_.imageScalePercent.value, var_.imageScale.value);
}

std::uint32_t Props::drawingScalePercent() const noexcept {
    return std::min(imageScalePercent(), variables::previewScalePercent(
        var_.drawingScalePercent.value, var_.drawingScale.value));
}

float Props::imageScaleFactor() const noexcept {
    return static_cast<float>(imageScalePercent()) / 100.0F;
}

float Props::drawingScaleFactor() const noexcept {
    return static_cast<float>(drawingScalePercent()) / 100.0F;
}

structs::CInsets Props::s(structs::CInsets insets) const noexcept {
    insets.n = s(insets.n);
    insets.e = s(insets.e);
    insets.s = s(insets.s);
    insets.w = s(insets.w);
    return insets;
}

std::uint32_t Props::drawingScale() const noexcept {
    // 百分率と従来の設定は、プレビューの飽和処理を共有する。追
    // 加の百分率モードでは、素材選択の倍率を整数へ切り上げる。
    return (drawingScalePercent() + 99U) / 100U;
}

std::uint32_t Props::drawingScaleForImage(const summary::Summary* summ) const {
    if (!summ || summ->loadScaledImage()) return drawingScale();
    return 1U;
}

std::int32_t Props::ds(const std::int32_t value) const noexcept {
    return scaledSignedPercent(value, drawingScalePercent());
}

structs::CPoint Props::ds(structs::CPoint point) const noexcept {
    point.x = ds(point.x);
    point.y = ds(point.y);
    return point;
}

structs::CSize Props::ds(structs::CSize size) const noexcept {
    size.width = scaledUnsignedPercent(size.width, drawingScalePercent());
    size.height = scaledUnsignedPercent(size.height, drawingScalePercent());
    return size;
}

structs::CRect Props::ds(structs::CRect rect) const noexcept {
    rect.x = ds(rect.x);
    rect.y = ds(rect.y);
    rect.width = ds(rect.width);
    rect.height = ds(rect.height);
    return rect;
}

structs::CInsets Props::ds(structs::CInsets insets) const noexcept {
    insets.n = ds(insets.n);
    insets.e = ds(insets.e);
    insets.s = ds(insets.s);
    insets.w = ds(insets.w);
    return insets;
}

structs::CFont Props::ds(structs::CFont font) const noexcept {
    font.point = scaledUnsignedPercent(font.point, drawingScalePercent());
    return font;
}

structs::CFont Props::adjustFont(structs::CFont font) const {
    font.name = adjustFont(font.name);
    return font;
}

String Props::adjustFont(const StringView face) const {
    if (services_.graphics) {
        const auto families = services_.graphics->font_families();
        if (std::find(families.begin(), families.end(), face) != families.end()) {
            return String(face);
        }
    }
    return parent_->looks().alternativeFont(String(face));
}

lib::SharedSlice<structs::BgImageS> Props::bgImagesDefault(const StringView type) const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d262(__FILE__, __LINE__);
    for (const auto& skin : var_.settingsWithSkinTypes.value.values) {
        ::cwxeditor::cwx::perf::TraceScope trace_dprops_d263(__FILE__, __LINE__);
        if (skin.type == type && skin.overrideBgImages) return skin.bgImagesDefault;
    }
    return var_.bgImagesDefault.value;
}

lib::SharedSlice<structs::BgImageSetting> Props::bgImageSettings(const StringView type) const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d269(__FILE__, __LINE__);
    for (const auto& skin : var_.settingsWithSkinTypes.value.values) {
        ::cwxeditor::cwx::perf::TraceScope trace_dprops_d270(__FILE__, __LINE__);
        if (skin.type == type && skin.overrideBgImages) return skin.bgImageSettings;
    }
    return var_.bgImageSettings.value;
}

lib::SharedSlice<String> Props::standardSelections(const StringView type) const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d276(__FILE__, __LINE__);
    for (const auto& skin : var_.settingsWithSkinTypes.value.values) {
        ::cwxeditor::cwx::perf::TraceScope trace_dprops_d277(__FILE__, __LINE__);
        if (skin.type == type && skin.overrideSelections) return skin.standardSelections;
    }
    return var_.standardSelections.value;
}

lib::SharedSlice<String> Props::standardKeyCodes(const StringView type) const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d283(__FILE__, __LINE__);
    for (const auto& skin : var_.settingsWithSkinTypes.value.values) {
        ::cwxeditor::cwx::perf::TraceScope trace_dprops_d284(__FILE__, __LINE__);
        if (skin.type == type && skin.overrideKeyCodes) return skin.standardKeyCodes;
    }
    return var_.standardKeyCodes.value;
}

lib::SharedSlice<structs::KeyCodeByFeature> Props::keyCodesByFeatures(const StringView type) const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d290(__FILE__, __LINE__);
    for (const auto& skin : var_.settingsWithSkinTypes.value.values) {
        ::cwxeditor::cwx::perf::TraceScope trace_dprops_d291(__FILE__, __LINE__);
        if (skin.type == type && skin.overrideKeyCodes) return skin.keyCodesByFeatures;
    }
    return var_.keyCodesByFeatures.value;
}

lib::SharedSlice<structs::KeyCodeByMotion> Props::keyCodesByMotions(const StringView type) const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d297(__FILE__, __LINE__);
    for (const auto& skin : var_.settingsWithSkinTypes.value.values) {
        ::cwxeditor::cwx::perf::TraceScope trace_dprops_d298(__FILE__, __LINE__);
        if (skin.type == type && skin.overrideKeyCodes) return skin.keyCodesByMotions;
    }
    return var_.keyCodesByMotions.value;
}

ElementOverrideTable Props::elementOverrides(const StringView type) const {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d304(__FILE__, __LINE__);
    ElementOverrideTable result;
    for (const auto& skin : var_.settingsWithSkinTypes.value.values) {
        ::cwxeditor::cwx::perf::TraceScope trace_dprops_d305(__FILE__, __LINE__);
        if (skin.type != type) continue;
        {
            ::cwxeditor::cwx::perf::TraceScope trace_dprops_d306(__FILE__, __LINE__);
            for (const auto& value : skin.elementOverrides) result[value.element] = value;
            return result;
        }
    }
    return result;
}

Image Props::elementImage(const ElementOverrideTable& table, const types::Element element,
                          ElementImageCache& imageCache) {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d316(__FILE__, __LINE__);
    const auto found = table.find(element);
    if (found != table.end()) return elementImage(found->second, imageCache);
    return images_.element(element);
}

Image Props::elementImage(const structs::ElementOverride& elementOverride,
                          ElementImageCache& imageCache) {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d321(__FILE__, __LINE__);
    const auto release = [&imageCache, element = elementOverride.element]() {
        ::cwxeditor::cwx::perf::TraceScope trace_dprops_d322(__FILE__, __LINE__);
        imageCache.erase(element);
    };
    const auto fallback = [this, element = elementOverride.element]() {
        return images_.element(element);
    };

    if (elementOverride.icon.empty()) {
        release();
        return fallback();
    }

    {
        ::cwxeditor::cwx::perf::TraceScope trace_dprops_d330(__FILE__, __LINE__);
        const StringView icon = elementOverride.icon;
        if (startsWith(icon, PRESET_IMAGE_SCHEMA)) {
            ::cwxeditor::cwx::perf::TraceScope trace_dprops_d331(__FILE__, __LINE__);
            release();
            const StringView preset = icon.substr(PRESET_IMAGE_SCHEMA.size());
            if (preset == "elm_all.png") return images_.element(types::Element::All);
            if (preset == "elm_health.png") return images_.element(types::Element::Health);
            if (preset == "elm_mind.png") return images_.element(types::Element::Mind);
            if (preset == "elm_miracle.png") return images_.element(types::Element::Miracle);
            if (preset == "elm_magic.png") return images_.element(types::Element::Magic);
            if (preset == "elm_fire.png") return images_.element(types::Element::Fire);
            if (preset == "elm_ice.png") return images_.element(types::Element::Ice);
            if (preset == "elm_hearing.png") return images_.elementHearing();
            if (preset == "elm_vision.png") return images_.elementVision();
            if (preset == "elm_electronic.png") return images_.elementElectronic();
            return fallback();
        }

        {
            ::cwxeditor::cwx::perf::TraceScope trace_dprops_d357(__FILE__, __LINE__);
            release();

            try {
                const Image image = images_.load(pathFromUtf8(parent_->toAppAbs(elementOverride.icon)));
                if (image != 0) {
                    imageCache[elementOverride.element] = ElementImageCacheEntry{image, elementOverride.icon};
                    return image;
                }
            } catch (...) {
                // 上書き指定が不正なら、組み込みの属性アイコンを使う。
            }
        }
    }
    return fallback();
}

bool Props::modified() const { return serializeProperties() != committed_serialized_; }

bool Props::reload() {
    if (!flex_.reload()) return false;
    committed_flex_ = flex_;
    committed_ = var_;
    committed_serialized_ = serializeProperties();
    if(!visitBindings(true))return false;
    notifyChanged();
    return true;
}

String Props::serializeProperties() const {
    auto node = flex_.toNode();
    return node.serialize();
}

bool Props::validate(lib::Array<String>* errors) const {
    if (errors) errors->clear();
    const auto addError = [errors](String message) {
        if (errors) errors->push_back(std::move(message));
    };
    bool valid = true;
    if (var_.imageScale.value == 0) {
        addError("imageScale must be at least 1");
        valid = false;
    }
    if (var_.drawingScale.value == 0) {
        addError("drawingScale must be at least 1");
        valid = false;
    }
    return valid;
}

bool Props::apply() {
    const std::weak_ptr<void> life=lifetime_;
    syncBindings();
    if(life.expired())return false;
    const bool managed_values_adjusted =
        lib::is_managed_user_data_session(services_) &&
        properties::applyManagedSessionDefaults(var_, storage_);
    if (!validate()) return false;
    const bool wasModified = managed_values_adjusted || modified();
    const auto serialized = serializeProperties();
    if (wasModified) {
        try {
            // FlexPropsは解決済みの設定保存先（標準・ローカル・コピー・保存
            // なし）を知っている。config_file_path_へ直接書くと、
            // Editor設定を保存する代わりに、保存先の記述子を置換してしまう。
            flex_.save(std::shared_ptr<properties::DockingFolderCTC>{});
        } catch (...) {
            return false;
        }
    }
    committed_flex_ = flex_;
    committed_ = var_;
    committed_serialized_ = serialized;
    if (wasModified) notifyChanged(true);
    return true;
}

void Props::cancel() {
    if (!modified()) return;
    flex_ = committed_flex_;
    var_ = committed_;
    if(!visitBindings(true))return;
    notifyChanged();
}

void Props::addChangeListener(ChangeListener listener) {
    if (listener) change_listeners_.push_back(std::move(listener));
}

void Props::notifyChanged(const bool committed) {
    const std::weak_ptr<void> life=lifetime_;
    const auto listeners = change_listeners_;
    for (const auto& listener : listeners) {
        if (!listener) continue;
        try { listener(); }
        catch (...) {
            if (!committed) throw;
            // Dの直接通知と異なり、共通の設定トランザクションは
            // ここですでに永続化・確定済み。不正な監視処理が、呼
            // び出し側に現在の値だけを巻き戻させないようにする。
            lib::editor_action_log("Settings saved; a change observer failed.");
        }
        if(life.expired())return;
    }
}

Props::~Props() {
    lifetime_.reset();
    for(auto& binding:bindings_)if(binding.active)*binding.active=false;
}

std::size_t Props::bindProperty(PropertyBinding binding) {
    if (!binding.pull && !binding.push) return std::numeric_limits<std::size_t>::max();
    if(!binding.active)binding.active=std::make_shared<bool>(true);
    const auto id=bindings_.size();
    const auto initialPush=binding.push;
    const auto state=binding.active;
    bindings_.push_back(std::move(binding));
    if(initialPush && *state)initialPush();
    // IDは末尾追加だけの項目とし、前の接続を削除しても移動させない。
    return id;
}

bool Props::bindTextProperty(String key, const std::shared_ptr<lib::TextEdit>& widget) {
    if(!widget || !hasTextKey(key))return false;
    const std::weak_ptr<void> life=lifetime_;
    const std::weak_ptr<lib::TextEdit> weak=widget;
    const String bindingKey=std::move(key);
    auto state=std::make_shared<bool>(true);
    const std::weak_ptr<bool> bindingLife=state;
    const auto live=[life,bindingLife] {auto active=bindingLife.lock();return !life.expired() && active && *active;};
    (void)bindProperty(PropertyBinding{bindingKey,
        [this,weak,bindingKey,live] {if(!live())return;if(auto target=weak.lock())if(setTextValue(bindingKey,target->text))notifyChanged();},
        [this,weak,bindingKey,live] {if(!live())return;if(auto target=weak.lock()){target->text = textValue(bindingKey);}},state});
    if(!live())return false;
    widget->on_changed=[this,weak,bindingKey,live](StringView value) {if(live() && weak.lock() && setTextValue(bindingKey,value))notifyChanged();};
    return true;
}

bool Props::bindCheckProperty(String key, const std::shared_ptr<lib::CheckBox>& widget) {
    if(!widget || !hasBoolKey(key))return false;
    const std::weak_ptr<void> life=lifetime_;
    const std::weak_ptr<lib::CheckBox> weak=widget;
    const String bindingKey=std::move(key);
    auto state=std::make_shared<bool>(true);
    const std::weak_ptr<bool> bindingLife=state;
    const auto live=[life,bindingLife] {auto active=bindingLife.lock();return !life.expired() && active && *active;};
    (void)bindProperty(PropertyBinding{bindingKey,
        [this,weak,bindingKey,live] {if(!live())return;if(auto target=weak.lock())if(setBoolValue(bindingKey,target->checked))notifyChanged();},
        [this,weak,bindingKey,live] {if(!live())return;if(auto target=weak.lock()){bool value=false; if(boolValue(bindingKey,value))target->checked=value;}},state});
    if(!live())return false;
    widget->on_changed=[this,weak,bindingKey,live](bool value) {if(live() && weak.lock() && setBoolValue(bindingKey,value))notifyChanged();};
    return true;
}

bool Props::bindNumberProperty(String key, const std::shared_ptr<lib::NumberEdit>& widget) {
    if(!widget || !hasNumberKey(key))return false;
    const std::weak_ptr<void> life=lifetime_;
    const std::weak_ptr<lib::NumberEdit> weak=widget;
    const String bindingKey=std::move(key);
    auto state=std::make_shared<bool>(true);
    const std::weak_ptr<bool> bindingLife=state;
    const auto live=[life,bindingLife] {auto active=bindingLife.lock();return !life.expired() && active && *active;};
    (void)bindProperty(PropertyBinding{bindingKey,
        [this,weak,bindingKey,live] {if(!live())return;if(auto target=weak.lock())if(setNumberValue(bindingKey,target->value))notifyChanged();},
        [this,weak,bindingKey,live] {if(!live())return;if(auto target=weak.lock()){double value=0; if(numberValue(bindingKey,value))target->value=value;}},state});
    if(!live())return false;
    widget->on_changed=[this,weak,bindingKey,live](double value) {if(live() && weak.lock() && setNumberValue(bindingKey,value))notifyChanged();};
    return true;
}

void Props::removeBinding(const std::size_t bindingId) {
    if(bindingId>=bindings_.size())return;
    auto& binding=bindings_[bindingId];
    if(binding.active)*binding.active=false;
    binding.pull={};binding.push={};binding.key.clear();
}

bool Props::visitBindings(const bool push) {
    const std::weak_ptr<void> life=lifetime_;
    const auto snapshot=bindings_;
    for(const auto& binding:snapshot) {
        if(binding.active && *binding.active) {
            const auto& callback=push?binding.push:binding.pull;
            if(callback)callback();
        }
        if(life.expired())return false;
    }
    return true;
}

void Props::syncBindings() {(void)visitBindings(false);}

bool Props::setTextValue(const StringView key, const StringView value) {
    if (lib::editor_window_host_options().all_mode &&
        (key == "tempPath" || key == "backupPath" ||
         key == "backupBeforeSavePath" || key == "enginePath")) return false;
    if (key == "languageDir") var_.languageDir.value = String(value);
    else if (key == "languageFile") var_.languageFile.value = String(value);
    else if (key == "targetVersion") var_.targetVersion.value = String(value);
    else if (key == "engine") var_.engine.value = String(value);
    else if (key == "engineScript") var_.engineScript.value = String(value);
    else if (key == "enginePath") var_.enginePath.value = String(value);
    else if (key == "pythonCommand") var_.pythonCommand.value = String(value);
    else if (key == "dataDir") var_.dataDir.value = String(value);
    else if (key == "defaultSkin") var_.defaultSkin.value = String(value);
    else if (key == "defaultSkinName") var_.defaultSkinName.value = String(value);
    else if (key == "tempPath") var_.tempPath.value = String(value);
    else if (key == "backupPath") var_.backupPath.value = String(value);
    else if (key == "backupBeforeSavePath") var_.backupBeforeSavePath.value = String(value);
    else if (key == "newScenarioName") var_.newScenarioName.value = String(value);
    else if (key == "newAreaName") var_.newAreaName.value = String(value);
    else return false;
    return true;
}

bool Props::setBoolValue(const StringView key, const bool value) {
    if (key == "useSystemLanguage") var_.useSystemLanguage.value = value;
    else if (key == "backupEnabled") var_.backupEnabled.value = value;
    else if (key == "autoSave") var_.autoSave.value = value;
    else if (key == "backupRefAuthor") var_.backupRefAuthor.value = value;
    else if (key == "backupBeforeSaveEnabled") var_.backupBeforeSaveEnabled.value = value;
    else if (key == "backupArchived") var_.backupArchived.value = value;
    else if (key == "findEnginePath") var_.findEnginePath.value = value;
    else if (key == "saveSkinName") var_.saveSkinName.value = value;
    else if (key == "viewCards") var_.viewCards.value = value;
    else if (key == "viewBgImages") var_.viewBgImages.value = value;
    else if (key == "showGrid") var_.showGrid.value = value;
    else return false;
    return true;
}

bool Props::setNumberValue(const StringView key, const double value) {
    if (!std::isfinite(value)) return false;
    // 追加の数値接続APIでは、丸め・縮小変換の前に表現不能な値を
    // 拒否する。Dの通常のコントロールはすでに範囲を制限している。
    const auto rounded=std::round(value);
    if(rounded<static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
       rounded>static_cast<double>(std::numeric_limits<std::uint32_t>::max()))return false;
    const auto integer=static_cast<std::int64_t>(rounded);
    const bool unsignedValue=key=="imageScale" || key=="drawingScale" || key=="imageScalePercent" || key=="drawingScalePercent" || key=="tabs";
    if(!unsignedValue && integer>std::numeric_limits<std::int32_t>::max())return false;
    if (key == "imageScale" && integer >= 0) {
        var_.imageScale.value = static_cast<std::uint32_t>(integer);
        var_.imageScalePercent.value = 0U;
    } else if (key == "drawingScale" && integer >= 0) {
        var_.drawingScale.value = static_cast<std::uint32_t>(integer);
        var_.drawingScalePercent.value = 0U;
    } else if (key == "imageScalePercent" && integer >= 100 && integer <= 200) {
        var_.imageScalePercent.value = static_cast<std::uint32_t>(integer);
        var_.imageScale.value = static_cast<std::uint32_t>((integer + 99) / 100);
    } else if (key == "drawingScalePercent" && integer >= 100 && integer <= 200) {
        var_.drawingScalePercent.value = static_cast<std::uint32_t>(integer);
        var_.drawingScale.value = static_cast<std::uint32_t>((integer + 99) / 100);
    }
    else if (key == "tabs") var_.tabs.value = static_cast<std::uint32_t>(std::max<std::int64_t>(0, integer));
    else if (key == "backupInterval") var_.backupInterval.value = static_cast<std::int32_t>(integer);
    else if (key == "backupIntervalEdit") var_.backupIntervalEdit.value = static_cast<std::int32_t>(integer);
    else if (key == "backupCount") var_.backupCount.value = static_cast<std::int32_t>(integer);
    else if (key == "messageAlpha") var_.messageAlpha.value = static_cast<std::int32_t>(integer);
    else if (key == "partyCardAlpha") var_.partyCardAlpha.value = static_cast<std::int32_t>(integer);
    else if (key == "comboVisibleItemCount") var_.comboVisibleItemCount.value = static_cast<std::int32_t>(integer);
    else return false;
    return true;
}

String Props::textValue(const StringView key) const {
    if (key == "languageDir") return var_.languageDir.value;
    if (key == "languageFile") return var_.languageFile.value;
    if (key == "targetVersion") return var_.targetVersion.value;
    if (key == "engine") return var_.engine.value;
    if (key == "engineScript") return var_.engineScript.value;
    if (key == "enginePath") return var_.enginePath.value;
    if (key == "pythonCommand") return var_.pythonCommand.value;
    if (key == "dataDir") return var_.dataDir.value;
    if (key == "defaultSkin") return var_.defaultSkin.value;
    if (key == "defaultSkinName") return var_.defaultSkinName.value;
    if (key == "tempPath") return var_.tempPath.value;
    if (key == "backupPath") return var_.backupPath.value;
    if (key == "backupBeforeSavePath") return var_.backupBeforeSavePath.value;
    if (key == "newScenarioName") return var_.newScenarioName.value;
    if (key == "newAreaName") return var_.newAreaName.value;
    return String{};
}

bool Props::boolValue(const StringView key, bool& value) const {
    if (key == "useSystemLanguage") value = var_.useSystemLanguage.value;
    else if (key == "backupEnabled") value = var_.backupEnabled.value;
    else if (key == "autoSave") value = var_.autoSave.value;
    else if (key == "backupRefAuthor") value = var_.backupRefAuthor.value;
    else if (key == "backupBeforeSaveEnabled") value = var_.backupBeforeSaveEnabled.value;
    else if (key == "backupArchived") value = var_.backupArchived.value;
    else if (key == "findEnginePath") value = var_.findEnginePath.value;
    else if (key == "saveSkinName") value = var_.saveSkinName.value;
    else if (key == "viewCards") value = var_.viewCards.value;
    else if (key == "viewBgImages") value = var_.viewBgImages.value;
    else if (key == "showGrid") value = var_.showGrid.value;
    else return false;
    return true;
}

bool Props::numberValue(const StringView key, double& value) const {
    if (key == "imageScale") value = var_.imageScale.value;
    else if (key == "drawingScale") value = var_.drawingScale.value;
    else if (key == "imageScalePercent") value = imageScalePercent();
    else if (key == "drawingScalePercent") value = drawingScalePercent();
    else if (key == "tabs") value = var_.tabs.value;
    else if (key == "backupInterval") value = var_.backupInterval.value;
    else if (key == "backupIntervalEdit") value = var_.backupIntervalEdit.value;
    else if (key == "backupCount") value = var_.backupCount.value;
    else if (key == "messageAlpha") value = var_.messageAlpha.value;
    else if (key == "partyCardAlpha") value = var_.partyCardAlpha.value;
    else if (key == "comboVisibleItemCount") value = var_.comboVisibleItemCount.value;
    else return false;
    return true;
}

lib::Array<PresetImageInfo> presetIcons(Props& prop) {
    ::cwxeditor::cwx::perf::TraceScope trace_dprops_d393(__FILE__, __LINE__);
    const auto& message = prop.requiredMsgs();
    return {
        {"elm_all.png", message.elementName(types::Element::All), prop.images().element(types::Element::All)},
        {"elm_health.png", message.elementName(types::Element::Health), prop.images().element(types::Element::Health)},
        {"elm_mind.png", message.elementName(types::Element::Mind), prop.images().element(types::Element::Mind)},
        {"elm_miracle.png", message.elementName(types::Element::Miracle), prop.images().element(types::Element::Miracle)},
        {"elm_magic.png", message.elementName(types::Element::Magic), prop.images().element(types::Element::Magic)},
        {"elm_fire.png", message.elementName(types::Element::Fire), prop.images().element(types::Element::Fire)},
        {"elm_ice.png", message.elementName(types::Element::Ice), prop.images().element(types::Element::Ice)},
        {"elm_hearing.png", message.elementNameHearing.value, prop.images().elementHearing()},
        {"elm_vision.png", message.elementNameVision.value, prop.images().elementVision()},
        {"elm_electronic.png", message.elementNameElectronic.value, prop.images().elementElectronic()},
    };
}

DwtPoint dwtData(const structs::CPoint value) noexcept { return {value.x, value.y}; }
DwtPoint dwtData(const structs::CSize value) noexcept {
    return {static_cast<std::int32_t>(std::min<std::uint32_t>(value.width,
                                                               std::numeric_limits<std::int32_t>::max())),
            static_cast<std::int32_t>(std::min<std::uint32_t>(value.height,
                                                               std::numeric_limits<std::int32_t>::max()))};
}

DwtRgbResult dwtData(const structs::CRGB value) noexcept {
    return {DwtRgb{channel(value.r), channel(value.g), channel(value.b)},
            static_cast<std::int32_t>(std::min<std::uint32_t>(value.a,
                                                               std::numeric_limits<std::int32_t>::max()))};
}

DwtRgb dwtData(const structs::CRGB value, std::int32_t& alpha) noexcept {
    const auto result = dwtData(value);
    alpha = result.alpha;
    return result.color;
}

DwtFontData dwtData(const structs::CFont value) {
    std::int32_t style = static_cast<std::int32_t>(DwtFontStyle::Normal);
    if (value.bold) style |= static_cast<std::int32_t>(DwtFontStyle::Bold);
    if (value.italic) style |= static_cast<std::int32_t>(DwtFontStyle::Italic);
    return {value.name, static_cast<std::int32_t>(std::min<std::uint32_t>(
                         value.point, std::numeric_limits<std::int32_t>::max())),
            static_cast<DwtFontStyle>(style)};
}

std::function<void()> Props::bindDialogSize(
    const std::shared_ptr<lib::Window>& window, structs::DSize& size) {
    if (!window || lib::editor_window_host_options().all_mode) return {};
    const std::weak_ptr<void> life = lifetime_;
    const std::weak_ptr<lib::Window> weak = window;
    const auto active = std::make_shared<bool>(true);
    auto bounds = window->bounds();
    const auto preferred = lib::window_content_preferred_size(*window);
    bounds.width = size.width() > 0 ? static_cast<float>(size.width()) : preferred.width;
    bounds.height = size.height() > 0 ? static_cast<float>(size.height()) : preferred.height;
    if (window->minimum_size) {
        bounds.width = std::max(bounds.width, window->minimum_size->width);
        bounds.height = std::max(bounds.height, window->minimum_size->height);
    }
    bounds.width = std::max(1.0F, bounds.width);
    bounds.height = std::max(1.0F, bounds.height);
    window->bounds_ui_text_scale = 1.0F;
    window->set_bounds(bounds);
    const auto prior = window->on_bounds_changed;
    const auto save = [weak, life, active, target = &size] {
        if (!*active || life.expired()) return;
        const auto current = weak.lock();
        if (!current || current->maximized || current->minimized) return;
        const float scale = std::max(1.0F, current->bounds_ui_text_scale);
        const auto value = current->bounds();
        const double width = value.width / scale, height = value.height / scale;
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
            width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max()) return;
        target->width(static_cast<int>(std::lround(width)));
        target->height(static_cast<int>(std::lround(height)));
    };
    struct Listener {
        std::shared_ptr<bool> active;
        std::function<void(lib::Rect)> prior;
        std::function<void()> save;
        void operator()(lib::Rect bounds) const {
            if (prior) prior(bounds);
            if (*active) save();
        }
    };
    window->on_bounds_changed = Listener{active, prior, save};
    return [weak, active, prior, save] {
        if (!*active) return;
        save();
        *active = false;
        if (const auto current = weak.lock()) {
            const auto own = current->on_bounds_changed.target<Listener>();
            if (own && own->active == active) current->on_bounds_changed = prior;
        }
    };
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::dprops
