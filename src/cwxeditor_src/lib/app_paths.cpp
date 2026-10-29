#include "app_paths.hpp"

#include <string>
#include <utility>

namespace cwxeditor::lib::paths::detail {

AppPath normalize_root(AppPath value) {
    try { return ::cardwirth::platform::normalize_explicit_root(std::move(value)); }
    catch (const std::invalid_argument& error) { throw AppPathError(error.what()); }
}

AppPath normalize_relative(AppPath value) {
    try { return ::cardwirth::platform::normalize_relative_path(std::move(value)); }
    catch (const std::invalid_argument& error) { throw AppPathError(error.what()); }
}

bool contains_path(const AppPath& root, const AppPath& candidate) noexcept {
    return ::cardwirth::platform::path_contains(root, candidate);
}

}  // namespace cwxeditor::lib::paths::detail

namespace cwxeditor::lib::paths {
namespace {

AppPath web_app_root() {
    return AppPath("/app");
}

AppPath web_user_root() {
    return AppPath("/user");
}

}  // namespace

AppPaths::AppPaths(AppRoot app, UserRoot user)
    : AppPaths(::cardwirth::platform::AppPaths::from_explicit_roots(app.path(), user.path())) {}

AppPaths::AppPaths(const ::cardwirth::platform::AppPaths& paths)
    : app_root(paths.app_root), user_root(paths.user_root), data_root(paths.data_root),
      skin_root(paths.skin_root), scenario_root(paths.scenario_root), yado_root(paths.yado_root),
      config_root(paths.config_root), database_root(paths.database_root), temp_root(paths.temp_root),
      cache_root(paths.cache_root), trash_root(paths.trash_root) {}

AppPaths AppPaths::from_root(AppPath root) {
    // 引数の評価順は未規定なので、ムーブするコンス
    // トラクターへ渡す前に、両方の値を保持する。
    return from_roots(root, root);
}

AppPaths AppPaths::from_roots(AppPath app_root_value, AppPath user_root_value) {
    if (user_root_value.empty()) user_root_value = app_root_value;
    return AppPaths(AppRoot(std::move(app_root_value)),
                    UserRoot(std::move(user_root_value)));
}

AppPaths AppPaths::native_defaults(AppPath app_root_value) {
    return from_roots(app_root_value, app_root_value);
}

AppPaths AppPaths::native_defaults(AppPath app_root_value,
                                   AppPath user_root_value) {
    return from_roots(std::move(app_root_value), std::move(user_root_value));
}

AppPaths AppPaths::web_defaults() {
    return from_roots(web_app_root(), web_user_root());
}

AppPaths AppPaths::web_defaults(AppPath app_root_value) {
    if (app_root_value.empty()) app_root_value = web_app_root();
    return from_roots(std::move(app_root_value), web_user_root());
}

AppPaths AppPaths::web_defaults(AppPath app_root_value,
                                AppPath user_root_value) {
    if (app_root_value.empty()) app_root_value = web_app_root();
    if (user_root_value.empty()) user_root_value = web_user_root();
    return from_roots(std::move(app_root_value), std::move(user_root_value));
}

AppPaths AppPaths::defaults(const RuntimeKind kind,
                            AppPath app_root_value,
                            AppPath user_root_value) {
    if (kind == RuntimeKind::Web) {
        return web_defaults(std::move(app_root_value), std::move(user_root_value));
    }
    if (app_root_value.empty()) {
        throw AppPathError("native logical roots must be injected by the runtime host");
    }
    if (user_root_value.empty()) return native_defaults(std::move(app_root_value));
    return native_defaults(std::move(app_root_value), std::move(user_root_value));
}

AppPaths AppPaths::from_runtime(const RuntimeContext& context) {
    return context.paths;
}

AppPaths AppPaths::native(AppPath app_root_value,
                          AppPath user_root_value) {
    return user_root_value.empty()
        ? native_defaults(std::move(app_root_value))
        : native_defaults(std::move(app_root_value), std::move(user_root_value));
}

AppPaths AppPaths::web() {
    return web_defaults();
}

AppPaths AppPaths::web(AppPath app_root_value,
                       AppPath user_root_value) {
    return user_root_value.empty()
        ? web_defaults(std::move(app_root_value))
        : web_defaults(std::move(app_root_value), std::move(user_root_value));
}

const AppPath& AppPaths::root(const LogicalRoot value) const {
    switch (value) {
        case LogicalRoot::App: return app_root.path();
        case LogicalRoot::User: return user_root.path();
        case LogicalRoot::Data: return data_root.path();
        case LogicalRoot::Skin: return skin_root.path();
        case LogicalRoot::Scenario: return scenario_root.path();
        case LogicalRoot::Yado: return yado_root.path();
        case LogicalRoot::Config: return config_root.path();
        case LogicalRoot::Database: return database_root.path();
        case LogicalRoot::Temp: return temp_root.path();
        case LogicalRoot::Cache: return cache_root.path();
        case LogicalRoot::Trash: return trash_root.path();
    }
    throw AppPathError("unknown logical root");
}

AppPath AppPaths::resolve(const LogicalRoot value, AppPath relative) const {
    switch (value) {
        case LogicalRoot::App: return app_root.resolve(relative);
        case LogicalRoot::User: return user_root.resolve(relative);
        case LogicalRoot::Data: return data_root.resolve(relative);
        case LogicalRoot::Skin: return skin_root.resolve(relative);
        case LogicalRoot::Scenario: return scenario_root.resolve(relative);
        case LogicalRoot::Yado: return yado_root.resolve(relative);
        case LogicalRoot::Config: return config_root.resolve(relative);
        case LogicalRoot::Database: return database_root.resolve(relative);
        case LogicalRoot::Temp: return temp_root.resolve(relative);
        case LogicalRoot::Cache: return cache_root.resolve(relative);
        case LogicalRoot::Trash: return trash_root.resolve(relative);
    }
    throw AppPathError("unknown logical root");
}

RuntimeContext RuntimeContext::native(AppPath app_root_value) {
    return RuntimeContext(AppPaths::native_defaults(std::move(app_root_value)),
                          RuntimeKind::Native);
}

RuntimeContext RuntimeContext::native(AppPath app_root_value,
                                      AppPath user_root_value) {
    return RuntimeContext(AppPaths::native_defaults(std::move(app_root_value),
                                                    std::move(user_root_value)),
                          RuntimeKind::Native);
}

RuntimeContext RuntimeContext::web() {
    return RuntimeContext(AppPaths::web_defaults(), RuntimeKind::Web);
}

RuntimeContext RuntimeContext::web(AppPath app_root_value) {
    return RuntimeContext(AppPaths::web_defaults(std::move(app_root_value)),
                          RuntimeKind::Web);
}

RuntimeContext RuntimeContext::web(AppPath app_root_value,
                                   AppPath user_root_value) {
    return RuntimeContext(AppPaths::web_defaults(std::move(app_root_value),
                                                 std::move(user_root_value)),
                          RuntimeKind::Web);
}

AppPaths app_paths_from_runtime(const RuntimeContext& context) {
    return context.paths;
}

RuntimeContext make_native_runtime_context(AppPath app_root_value) {
    return RuntimeContext::native(std::move(app_root_value));
}

RuntimeContext make_native_runtime_context(AppPath app_root_value,
                                            AppPath user_root_value) {
    return RuntimeContext::native(std::move(app_root_value),
                                  std::move(user_root_value));
}

RuntimeContext make_web_runtime_context() {
    return RuntimeContext::web();
}

RuntimeContext make_web_runtime_context(AppPath app_root_value) {
    return RuntimeContext::web(std::move(app_root_value));
}

RuntimeContext make_web_runtime_context(AppPath app_root_value,
                                        AppPath user_root_value) {
    return RuntimeContext::web(std::move(app_root_value),
                               std::move(user_root_value));
}

// フロントエンドが使い始める前でも、具体的なエディタの
// ルート型について すべての公開ルート操作を実体化し、
// この実装単位だけで型の整合を検査できるようにする。
template class RootPath<AppRootTag>;
template class RootPath<UserRootTag>;
template class RootPath<DataRootTag>;
template class RootPath<SkinRootTag>;
template class RootPath<ScenarioRootTag>;
template class RootPath<YadoRootTag>;
template class RootPath<ConfigRootTag>;
template class RootPath<DatabaseRootTag>;
template class RootPath<TempRootTag>;
template class RootPath<CacheRootTag>;
template class RootPath<TrashRootTag>;

}  // namespace cwxeditor::lib::paths
