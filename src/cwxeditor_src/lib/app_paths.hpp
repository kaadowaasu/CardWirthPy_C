#pragma once

#include <filesystem>
#include <stdexcept>
#include <utility>
#include "platform/storage/app_paths.hpp"

namespace cwxeditor::lib::paths {

// 絶対パス、または明示的
// にルートを持つ仮想パス
// を受け付け、作業ディレ
// クトリは参照しない。
using AppPath = std::filesystem::path;

struct AppRootTag {};
struct UserRootTag {};
struct DataRootTag {};
struct SkinRootTag {};
struct ScenarioRootTag {};
struct YadoRootTag {};
struct ConfigRootTag {};
struct DatabaseRootTag {};
struct TempRootTag {};
struct CacheRootTag {};
struct TrashRootTag {};

class AppPathError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

namespace detail {

[[nodiscard]] AppPath normalize_root(AppPath value);
[[nodiscard]] AppPath normalize_relative(AppPath value);
[[nodiscard]] bool contains_path(const AppPath& root,
                                 const AppPath& candidate) noexcept;

}  // namespace detail

/// 絶対パス、または
/// 明示的にルート
/// を持つ仮想パス
/// を受け付け、作
/// 業ディレクトリ
/// は参照しない。
template <typename Tag>
class RootPath final {
public:
    using tag_type = Tag;
    using path_type = AppPath;

    explicit RootPath(AppPath value) : value_(detail::normalize_root(std::move(value))) {}

    [[nodiscard]] const AppPath& path() const noexcept { return value_; }
    [[nodiscard]] const AppPath& value() const noexcept { return value_; }
    [[nodiscard]] const AppPath& get() const noexcept { return value_; }

    /// このルートを基準に相対パスを解決す
    /// る。 空と.はルート自身を表す。絶
    /// 対パスやa/../../outside
    /// などルート外へ出る相対パスはAppPathError
    /// で拒否する。
    [[nodiscard]] AppPath resolve(const AppPath& relative) const;
    [[nodiscard]] AppPath join(const AppPath& relative) const {
        return resolve(relative);
    }

    [[nodiscard]] bool contains(const AppPath& candidate) const noexcept {
        return detail::contains_path(value_, candidate);
    }

    /// このルート配下の相対パスを返し、外側なら例外を投げ
    /// る。 戻り値は正規化済みで、絶対パスにはしない。
    [[nodiscard]] AppPath relative(const AppPath& candidate) const;

    friend bool operator==(const RootPath&, const RootPath&) = default;

private:
    AppPath value_;
};

using AppRoot = RootPath<AppRootTag>;
using UserRoot = RootPath<UserRootTag>;
using DataRoot = RootPath<DataRootTag>;
using SkinRoot = RootPath<SkinRootTag>;
using ScenarioRoot = RootPath<ScenarioRootTag>;
using YadoRoot = RootPath<YadoRootTag>;
using ConfigRoot = RootPath<ConfigRootTag>;
using DatabaseRoot = RootPath<DatabaseRootTag>;
using TempRoot = RootPath<TempRootTag>;
using CacheRoot = RootPath<CacheRootTag>;
using TrashRoot = RootPath<TrashRootTag>;

// 上のルート型の区別を保ちつつ、呼び出し
// 箇所でパス値だと分かる別名を用意する。
using AppRootPath = AppRoot;
using UserRootPath = UserRoot;
using DataRootPath = DataRoot;
using SkinRootPath = SkinRoot;
using ScenarioRootPath = ScenarioRoot;
using YadoRootPath = YadoRoot;
using ConfigRootPath = ConfigRoot;
using DatabaseRootPath = DatabaseRoot;
using TempRootPath = TempRoot;
using CacheRootPath = CacheRoot;
using TrashRootPath = TrashRoot;

enum class LogicalRoot {
    App,
    User,
    Data,
    Skin,
    Scenario,
    Yado,
    Config,
    Database,
    Temp,
    Cache,
    Trash,

    // 仕様上のルート名を使うコード向けの、読みやすい別名。
    AppRoot = App,
    UserRoot = User,
    DataRoot = Data,
    SkinRoot = Skin,
    ScenarioRoot = Scenario,
    YadoRoot = Yado,
    ConfigRoot = Config,
    DatabaseRoot = Database,
    TempRoot = Temp,
    CacheRoot = Cache,
    TrashRoot = Trash,
};

enum class RuntimeKind {
    Native,
    Web,
};

using RuntimeMode = RuntimeKind;
using RootKind = LogicalRoot;

struct RuntimeContext;

/// 単独起動Editorが使う全論理ルート。 既
/// 定コンストラクターは設けず、ホストがネイティ
/// ブ・Webのファクトリーを選ぶか、 from_roots()
/// で明示的なルートを渡す。
struct AppPaths {
    AppRoot app_root;
    UserRoot user_root;
    DataRoot data_root;
    SkinRoot skin_root;
    ScenarioRoot scenario_root;
    YadoRoot yado_root;
    ConfigRoot config_root;
    DatabaseRoot database_root;
    TempRoot temp_root;
    CacheRoot cache_root;
    TrashRoot trash_root;

    explicit AppPaths(AppRoot app, UserRoot user);
    explicit AppPaths(const ::cardwirth::platform::AppPaths& paths);

    [[nodiscard]] static AppPaths from_root(AppPath root);
    [[nodiscard]] static AppPaths from_roots(AppPath app_root,
                                             AppPath user_root);

    /// 明示的なネイティブのルート。一引数版はインスト
    /// ール先とユーザーデータに同じディレクトリを使い
    /// 、 ポータブルなEditor配布に対応する。
    [[nodiscard]] static AppPaths native_defaults(AppPath app_root);
    [[nodiscard]] static AppPaths native_defaults(AppPath app_root,
                                                  AppPath user_root);

    /// Emscriptenの標準的な永続名前空間。 片方ま
    /// たは両方の引数で、独自の絶対名前空間を指定できる。
    [[nodiscard]] static AppPaths web_defaults();
    [[nodiscard]] static AppPaths web_defaults(AppPath app_root);
    [[nodiscard]] static AppPaths web_defaults(AppPath app_root,
                                                AppPath user_root);

    [[nodiscard]] static AppPaths defaults(RuntimeKind kind,
                                           AppPath app_root,
                                           AppPath user_root = {});
    [[nodiscard]] static AppPaths from_runtime(const RuntimeContext& context);

    // アプリとの接続では短いファクトリー名を使え、 設定コ
    // ードでは意味の分かる*_defaults名も残す。
    [[nodiscard]] static AppPaths native(AppPath app_root,
                                         AppPath user_root = {});
    [[nodiscard]] static AppPaths web();
    [[nodiscard]] static AppPaths web(AppPath app_root,
                                      AppPath user_root = {});

    [[nodiscard]] const AppPath& root(LogicalRoot value) const;
    [[nodiscard]] AppPath resolve(LogicalRoot value,
                                  AppPath relative = {}) const;

    [[nodiscard]] const AppRoot& app() const noexcept { return app_root; }
    [[nodiscard]] const UserRoot& user() const noexcept { return user_root; }
    [[nodiscard]] const DataRoot& data() const noexcept { return data_root; }
    [[nodiscard]] const SkinRoot& skin() const noexcept { return skin_root; }
    [[nodiscard]] const ScenarioRoot& scenario() const noexcept { return scenario_root; }
    [[nodiscard]] const YadoRoot& yado() const noexcept { return yado_root; }
    [[nodiscard]] const ConfigRoot& config() const noexcept { return config_root; }
    [[nodiscard]] const DatabaseRoot& database() const noexcept { return database_root; }
    [[nodiscard]] const TempRoot& temp() const noexcept { return temp_root; }
    [[nodiscard]] const CacheRoot& cache() const noexcept { return cache_root; }
    [[nodiscard]] const TrashRoot& trash() const noexcept { return trash_root; }
};

/// Editorホストから渡す小さな値型の実行環境
/// 。 Editorはconst RuntimeContext&
/// を受け取るかパスをコピーし、プロセスの
/// 作業ディレクトリからルートを探さない。
struct RuntimeContext {
    RuntimeKind kind;
    AppPaths paths;

    explicit RuntimeContext(AppPaths value, RuntimeKind runtime = RuntimeKind::Native)
        : kind(runtime), paths(std::move(value)) {}

    [[nodiscard]] static RuntimeContext native(AppPath app_root);
    [[nodiscard]] static RuntimeContext native(AppPath app_root, AppPath user_root);
    [[nodiscard]] static RuntimeContext web();
    [[nodiscard]] static RuntimeContext web(AppPath app_root);
    [[nodiscard]] static RuntimeContext web(AppPath app_root, AppPath user_root);

    [[nodiscard]] const AppPaths& app_paths() const noexcept { return paths; }
};

using EditorRuntimeContext = RuntimeContext;
using AppRuntimeContext = RuntimeContext;

[[nodiscard]] AppPaths app_paths_from_runtime(const RuntimeContext& context);
[[nodiscard]] RuntimeContext make_native_runtime_context(AppPath app_root);
[[nodiscard]] RuntimeContext make_native_runtime_context(AppPath app_root,
                                                          AppPath user_root);
[[nodiscard]] RuntimeContext make_web_runtime_context();
[[nodiscard]] RuntimeContext make_web_runtime_context(AppPath app_root);
[[nodiscard]] RuntimeContext make_web_runtime_context(AppPath app_root,
                                                       AppPath user_root);

template <typename Tag>
AppPath RootPath<Tag>::resolve(const AppPath& relative) const {
    try { return ::cardwirth::platform::resolve_below_root(value_, relative); }
    catch (const std::invalid_argument& error) { throw AppPathError(error.what()); }
}

template <typename Tag>
AppPath RootPath<Tag>::relative(const AppPath& candidate) const {
    // Windowsのstd::filesystemでは、/appのよ
    // うなEmscriptenの仮想パスはドライブ名がなく、 絶対では
    // なくルート付きと判定される。それでも明示指定された名前空間なの
    // で、 プロセスの作業ディレクトリからの相対パスとして扱わない。
    if (!(candidate.is_absolute() || candidate.has_root_directory()) ||
        !detail::contains_path(value_, candidate)) {
        throw AppPathError("path is outside the logical root: " + candidate.string());
    }
    const auto result = candidate.lexically_relative(value_).lexically_normal();
    return result == "." ? AppPath{} : result;
}

}  // namespace cwxeditor::lib::paths

namespace cwxeditor::lib {
namespace editor_paths = ::cwxeditor::lib::paths;
using EditorAppPaths = ::cwxeditor::lib::paths::AppPaths;
using EditorPathRuntimeContext = ::cwxeditor::lib::paths::RuntimeContext;
}  // namespace cwxeditor::lib
