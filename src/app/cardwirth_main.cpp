#define SDL_MAIN_HANDLED 1

#include "engine_runtime.hpp"
#include "cw/argparser.hpp"

#include <string_view>

#if !defined(_WIN32)
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>

#include "app_shell.hpp"
#endif

#if defined(__APPLE__)
#include "platform_apple.hpp"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    ::cardwirth::app::AppLaunchOptions options;
    options.host_mode = ::cardwirth::app::EngineHostMode::Standalone;
    return ::cardwirth::app::run_cardwirth_engine(options);
}
#else
int main(int argc, char** argv) {
    ::cw::argparser::set_app_launch_options(argc, argv);
    ::cardwirth::app::AppLaunchOptions options;
    options.host_mode = ::cardwirth::app::EngineHostMode::Standalone;
    if (argc > 0 && argv && argv[0]) options.executable_path = argv[0];
    for (int index = 1; index < argc; ++index)
        if (argv[index]) options.arguments.emplace_back(argv[index]);
    std::filesystem::path requested_user_root;
    for (std::size_t index = 0; index < options.arguments.size(); ++index) {
        const auto& argument = options.arguments[index];
        std::string value;
        if (argument == "--user-data") {
            if (index + 1 >= options.arguments.size()) {
                SDL_Log("CardWirthPy_C: --user-data has no value");
                return 1;
            }
            value = options.arguments[++index];
        } else {
            constexpr std::string_view prefix = "--user-data=";
            if (!argument.starts_with(prefix)) continue;
            value = argument.substr(prefix.size());
        }
        if (value.empty()) {
            SDL_Log("CardWirthPy_C: --user-data has an empty value");
            return 1;
        }
        requested_user_root = std::filesystem::u8path(value);
    }
#if defined(__APPLE__)
    // Appleのアプリバンドルでは、配布資産を読み取り専用のResources
    // に置く。 UserDataは署名されたバンドルの外へ保存する。
    options.application_directory = ::cardwirth::app::apple::bundle_resource_root();
    if (options.application_directory.empty()) {
        if (const char* base = SDL_GetBasePath(); base && *base)
            options.application_directory = std::filesystem::path(base);
    }
    if (options.application_directory.empty()) {
        SDL_Log("CardWirthPy_C: Apple application directory is unavailable");
        return 1;
    }
    const auto writable_root = requested_user_root.empty()
        ? ::cardwirth::app::apple::writable_user_root() : requested_user_root;
    if (writable_root.empty()) {
        SDL_Log("CardWirthPy_C: Apple UserData root is unavailable");
        return 1;
    }
    const auto user_data = ::cardwirth::app::UserDataLayout::from_root(writable_root);
    std::string setup_error;
    if (!user_data.ensure(setup_error, options.application_directory)) {
        SDL_Log("CardWirthPy_C: %s", setup_error.c_str());
        return 1;
    }
    const auto exclude_from_backup = [](const std::filesystem::path& path,
                                        const char* label) {
        if (!::cardwirth::app::apple::exclude_from_backup(path))
            SDL_Log("CardWirthPy_C: failed to exclude %s from Apple backup", label);
    };
    // Dataは再取得できる配布データで、CacheとTempは破棄可能。 Scenario
    // ・Yado・Config・Databaseはバックアップ対象として残す。
    exclude_from_backup(user_data.data, "managed UserData/Data");
    exclude_from_backup(user_data.cache, "UserData/Cache");
    exclude_from_backup(user_data.temp, "UserData/Temp");
    options.user_data_root = user_data.root;
#else
    // Linuxの単独起動では、--user-dataまたはCARDWIRTH_USER_DATA
    // により、 ユーザーデータをインストール先の外へ配置できる。従来のEngine
    // はその下のData/Skinを 参照するため、資産が隣にある
    // と仮定せず、ALLと同じ管理付きの互換用コピーを用意する。
    std::error_code path_error;
    if (const char* base = SDL_GetBasePath(); base && *base)
        options.application_directory = std::filesystem::path(base);
    else
        options.application_directory = options.executable_path.empty()
            ? std::filesystem::current_path(path_error)
            : std::filesystem::absolute(options.executable_path, path_error).parent_path();
    if (path_error || options.application_directory.empty()) {
        SDL_Log("CardWirthPy_C: Linux application directory is unavailable");
        return 1;
    }
    const auto writable_root = requested_user_root.empty()
        ? options.application_directory : requested_user_root;
    const auto user_data = ::cardwirth::app::UserDataLayout::from_root(writable_root);
    std::string setup_error;
    if (!user_data.ensure(setup_error, options.application_directory)) {
        SDL_Log("CardWirthPy_C: %s", setup_error.c_str());
        return 1;
    }
    options.user_data_root = user_data.root;
#endif
    return ::cardwirth::app::run_cardwirth_engine(options);
}
#endif
