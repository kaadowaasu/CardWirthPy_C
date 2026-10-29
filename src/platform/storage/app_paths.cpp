#include "app_paths.hpp"

#include <stdexcept>
#include <utility>

namespace cardwirth::platform {
StoragePath normalize_explicit_root(StoragePath root) {
    if (root.empty() || (!root.is_absolute() && !root.has_root_directory()))
        throw std::invalid_argument("logical root must be explicitly rooted");
    return root.lexically_normal();
}

StoragePath normalize_relative_path(StoragePath relative) {
    if (relative.empty() || relative == ".") return {};
    if (relative.has_root_name() || relative.has_root_directory())
        throw std::invalid_argument("absolute path cannot be resolved below a logical root");
    // 共通APIでは型付きEditorの契約に合わせ、a/../fileは
    // fileへ正規化する。 ルートの外へ出る../は引き続き拒否する。
    relative = relative.lexically_normal();
    for (const auto& part : relative)
        if (part == "..") throw std::invalid_argument("logical root escape is not allowed");
    return relative == "." ? StoragePath{} : relative;
}

bool path_contains(const StoragePath& root, const StoragePath& candidate) noexcept {
    if ((!root.is_absolute() && !root.has_root_directory()) ||
        (!candidate.is_absolute() && !candidate.has_root_directory())) return false;
    try {
        const auto relative = candidate.lexically_normal().lexically_relative(root.lexically_normal());
        if (relative.empty()) return candidate.lexically_normal() == root.lexically_normal();
        if (relative.has_root_name() || relative.has_root_directory()) return false;
        for (const auto& part : relative) if (part == "..") return false;
        return true;
    } catch (...) { return false; }
}

StoragePath resolve_below_root(const StoragePath& root, StoragePath relative) {
    relative = normalize_relative_path(std::move(relative));
    const auto candidate = (root / relative).lexically_normal();
    if (!path_contains(root, candidate)) throw std::invalid_argument("logical root escape is not allowed");
    return candidate;
}

AppPaths AppPaths::from_root(StoragePath root) {
    if (root.empty()) root = ".";
    root = std::filesystem::absolute(std::move(root)).lexically_normal();
    return from_roots(root, root);
}

AppPaths AppPaths::from_roots(StoragePath app_root, StoragePath user_root) {
    if (app_root.empty()) app_root = ".";
    if (user_root.empty()) user_root = app_root;
    app_root = std::filesystem::absolute(std::move(app_root)).lexically_normal();
    user_root = std::filesystem::absolute(std::move(user_root)).lexically_normal();
    return from_explicit_roots(std::move(app_root), std::move(user_root));
}

AppPaths AppPaths::from_explicit_roots(StoragePath app_root, StoragePath user_root) {
    app_root = normalize_explicit_root(std::move(app_root));
    if (user_root.empty()) user_root = app_root;
    user_root = normalize_explicit_root(std::move(user_root));
    AppPaths result;
    result.app_root = app_root;
    result.user_root = user_root;
    result.data_root = app_root / "Data";
    result.user_data_root = user_root / "Data";
    result.skin_root = result.data_root / "Skin";
    result.scenario_root = user_root / "Scenario";
    result.yado_root = user_root / "Yado";
    result.config_root = user_root / "Config";
    result.backup_root = user_root / "Backup";
    result.database_root = user_root / "Database";
    result.temp_root = user_root / "Temp";
    result.cache_root = user_root / "Cache";
    result.trash_root = user_root / "Trash";
    return result;
}

AppPaths AppPaths::defaults() {
    return from_root(".");
}

const StoragePath& AppPaths::root(LogicalRoot value) const {
    switch (value) {
        case LogicalRoot::App: return app_root;
        case LogicalRoot::User: return user_root;
        case LogicalRoot::Data: return data_root;
        case LogicalRoot::UserData: return user_data_root;
        case LogicalRoot::Skin: return skin_root;
        case LogicalRoot::Scenario: return scenario_root;
        case LogicalRoot::Yado: return yado_root;
        case LogicalRoot::Config: return config_root;
        case LogicalRoot::Backup: return backup_root;
        case LogicalRoot::Database: return database_root;
        case LogicalRoot::Temp: return temp_root;
        case LogicalRoot::Cache: return cache_root;
        case LogicalRoot::Trash: return trash_root;
    }
    throw std::invalid_argument("不明な論理rootです");
}

StoragePath AppPaths::resolve(LogicalRoot value, StoragePath relative) const {
    return resolve_below_root(root(value), std::move(relative));
}

}  // namespace cardwirth::platform
