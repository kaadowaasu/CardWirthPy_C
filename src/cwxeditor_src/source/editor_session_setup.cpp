#include "editor_session_setup.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cw/lib/archive.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
namespace cwxeditor::cwxeditor {
namespace {
using Path = std::filesystem::path;
using String = ::cwxeditor::lib::String;
using Services = ::cwxeditor::lib::Services;
using Props = ::cwxeditor::cwx::editor::gui::dwt::dprops::Props;
namespace lib = ::cwxeditor::lib;
String pathText(const Path& path) {
    const auto value = path.u8string();
    return String(reinterpret_cast<const char*>(value.data()), value.size());
}
Path pathFromText(const String& value) { return ::cwxeditor::cwx::structs::pathFromUtf8(value); }
struct LegacyEditorData {
    Path root;
    Path config;
    Path settings;
    Path dock;
    Path error_log;
};

bool isWithin(const Path& root, const Path& candidate) {
    return ::cardwirth::platform::path_contains(root, candidate);
}

std::vector<LegacyEditorData> legacyEditorDataLocations(const Services& services) {
    std::vector<LegacyEditorData> result;
    if (!services.system) return result;
    const auto append = [&result](Path root, const bool shared_cardwirth_root) {
        if (root.empty()) return;
        root = root.lexically_normal();
        const Path config = shared_cardwirth_root ? root / "Config" : root;
        result.push_back({
            root,
            config,
            config / "cwxeditor.xml",
            config / "cwxeditor.config.dock",
            config / "cwxeditor_error.log",
        });
    };
    try {
        const auto local = services.system->special_directory(lib::SpecialDirectory::UserData);
        append(local.empty() ? Path{} : local / "CardWirthC", true);
        const auto roaming = services.system->special_directory(
            lib::SpecialDirectory::ApplicationData);
        append(roaming.empty() ? Path{} : roaming /
            Path(lib::editor_settings_directory_name(services)), false);
    } catch (...) {
        // ネイティブの既知フォルダーがないプラットフォームでは、移行対象もない。
    }
    return result;
}

bool copyFileIfMissing(const Path& source, const Path& destination, lib::Storage& storage) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error) return false;
    if (storage.exists(destination)) return false;
    // 旧ファイルはインストール先・既知フォルダーの実ファイルで、その読み
    // 取りだけをネイティブ境界にする。 同じパスのマウント済みユーザー
    // ツリーも含め、全保存先は指定された Storage を正とする。
    std::ifstream input(source, std::ios::binary);
    if (!input) return false;
    const std::vector<char> bytes{std::istreambuf_iterator<char>{input}, {}};
    if (input.bad()) return false;
    return storage.write_atomic(destination, std::as_bytes(std::span{bytes}));
}

std::vector<LegacyEditorData> migrateLegacyEditorData(
    const Services& services, const Path& destination_root, lib::Storage& storage) {
    auto locations = legacyEditorDataLocations(services);
    const Path config = destination_root / "Config";
    const Path log = destination_root / "Log";
    for (const auto& legacy : locations) {
        if (legacy.root == destination_root) continue;
        (void)copyFileIfMissing(legacy.settings, config / "cwxeditor.xml", storage);
        (void)copyFileIfMissing(legacy.dock, config / "cwxeditor.config.dock", storage);
        (void)copyFileIfMissing(legacy.error_log, log / "cwxeditor_error.legacy.log", storage);
    }
    return locations;
}

bool relocateLegacyAutomaticPaths(Props& properties,
                                  const std::vector<LegacyEditorData>& locations) {
    const auto is_legacy = [&locations](const String& value) {
        if (value.empty()) return false;
        const auto path = pathFromText(value);
        if (!path.is_absolute()) return false;
        return std::any_of(locations.begin(), locations.end(), [&path](const auto& location) {
            return isWithin(location.root, path) || isWithin(location.config, path);
        });
    };
    bool changed{};
    if (is_legacy(properties.var().tempPath.value)) {
        properties.var().tempPath = "Temp";
        changed = true;
    }
    if (is_legacy(properties.var().backupPath.value)) {
        properties.var().backupPath = "Backup";
        changed = true;
    }
    if (is_legacy(properties.var().backupBeforeSavePath.value)) {
        properties.var().backupBeforeSavePath = "BackupBeforeSave";
        changed = true;
    }
    return changed;
}

}
void prepare_editor_resources() {
#if defined(__EMSCRIPTEN__)
    // Webのパッケージにはアーカイブを使う。生成するWASMシンボルでは
    // 全リソース名を表せないため。 読み込んだリソースとモデルは共有する。
    const Path root = "/program/resource";
    if (!std::filesystem::is_directory(root)) {
        ::cw::lib::archive::ExtractOptions options;
        options.overwrite = true;
        ::cw::lib::archive::Reader::open("/program/cwxeditor_resource.zip").extract("/program", options);
    }
#endif
}
EditorModelSession make_editor_model_session(
    lib::EditorRuntimeContext& runtime,
    const ::cwxeditor::cwx::system::System& system,
    String app_path, String config, String default_skin) {
    const auto& services = runtime.services();
    const auto& user_root = runtime.paths().user_root;
    const auto legacy = migrateLegacyEditorData(services, user_root, runtime.storage());
    ::cwxeditor::cwx::utils::debugLog = pathText(user_root / "Log" / "cwxeditor_error.log");
    if (config.empty()) config = pathText(runtime.storage_path(lib::LogicalRoot::Config, "cwxeditor.config"));
    // 単独版・組み込み版は同じ旧設定を移行する。 組み込み
    // 側が渡すのは実行時のルートと任意のスキンの場所だけ。
    (void)copyFileIfMissing(runtime.paths().app_root / "cwxeditor.config", pathFromText(config), runtime.storage());
    EditorModelSession result;
    result.core = std::make_unique<::cwxeditor::cwx::props::CProps>(std::move(app_path), system);
    if (!default_skin.empty()) result.core->defaultSkinPath(std::move(default_skin));
    result.properties = std::make_unique<Props>(config, *result.core, services, runtime.storage_ptr());
    if (relocateLegacyAutomaticPaths(*result.properties, legacy)) (void)result.properties->apply();
    const auto scale = lib::editor_ui_text_scale_from_percent(result.properties->var().uiTextScale.value);
    if (services.gui) services.gui->set_ui_text_scale(scale);
    else lib::set_editor_ui_text_scale(scale);
    return result;
}
}
