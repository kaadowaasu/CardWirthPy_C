#include "cwxeditor_src/source/cwxeditor.hpp"
#include "cwxeditor_src/lib/editor_diagnostics.hpp"
#include "cwxeditor_src/lib/runtime_context.hpp"
#include "runtime_library.hpp"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::filesystem::path executable_directory(
        std::filesystem::path executable,
        const std::filesystem::path& fallback) {
    if (executable.empty()) return fallback;
    std::error_code error;
    if (!executable.is_absolute()) executable = std::filesystem::absolute(executable, error);
    if (error || executable.empty()) return fallback;
    const auto directory = executable.lexically_normal().parent_path();
    return directory.empty() ? fallback : directory;
}

}  // namespace

#if !defined(_WIN32)
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_stdinc.h>
#endif

#if defined(_WIN32)

#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>

namespace {

std::string utf8(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                        result.data(), length, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::filesystem::path managed_diagnostics_directory(
        const std::filesystem::path& current_directory) {
    constexpr wchar_t marker[] = L"CARDWIRTHC_MANAGED_USERDATA_SESSION";
    wchar_t marker_value[2]{};
    const bool managed = GetEnvironmentVariableW(
        marker, marker_value, 2U) == 1U &&
        marker_value[0] == L'1';
    constexpr wchar_t variable[] = L"CARDWIRTHC_USER_ROOT";
    const DWORD required = GetEnvironmentVariableW(variable, nullptr, 0);
    if (required == 0) {
        if (managed)
            throw std::runtime_error(
                "managed Editor session has no CARDWIRTHC_USER_ROOT");
        return current_directory /
               ::cwxeditor::lib::editor_data_directory_name() /
               "Log";
    }
    std::vector<wchar_t> buffer(static_cast<std::size_t>(required), L'\0');
    const DWORD copied = GetEnvironmentVariableW(variable, buffer.data(), required);
    if (copied == 0 || copied >= required) {
        if (managed)
            throw std::runtime_error(
                "managed Editor session cannot read CARDWIRTHC_USER_ROOT");
        return current_directory /
               ::cwxeditor::lib::editor_data_directory_name() /
               "Log";
    }

    std::filesystem::path result(buffer.data());
    std::error_code error;
    if (!result.is_absolute()) {
        if (managed)
            throw std::runtime_error(
                "managed Editor CARDWIRTHC_USER_ROOT is not absolute");
        result = std::filesystem::absolute(result, error);
    }
    if (error || result.empty()) {
        if (managed)
            throw std::runtime_error(
                "managed Editor CARDWIRTHC_USER_ROOT is invalid");
        return current_directory /
               ::cwxeditor::lib::editor_data_directory_name() /
               "Log";
    }
    // CARDWIRTHC_USER_ROOTは製品のUserDataルートを表
    // す。 Editorの診断記録は設定・一時データ・バックアップと同じ<root>/editor
    // 配下へ置き、 UserData/Logへは書き出さない。
    return (result.lexically_normal() /
            ::cwxeditor::lib::editor_data_directory_name() / "Log");
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, wchar_t*, int) {
    ::cardwirth::app::configure_runtime_library_search();
    int argc = 0;
    wchar_t** wide_argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (wide_argv == nullptr) return -1;

    const std::filesystem::path current_directory = std::filesystem::current_path();
    const auto application_directory = executable_directory(
        argc > 0 && wide_argv[0] ? std::filesystem::path(wide_argv[0])
                                 : std::filesystem::path{},
        current_directory);
    try {
        ::cwxeditor::lib::initialize_editor_diagnostics(
            managed_diagnostics_directory(application_directory));
    } catch (const std::exception& error) {
        LocalFree(wide_argv);
        MessageBoxA(nullptr, error.what(), "CWXEditor_C", MB_OK | MB_ICONERROR);
        return 2;
    }
    ::cwxeditor::lib::editor_action_log("CWXEditor_C process started");

    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) arguments.push_back(utf8(wide_argv[index]));
    LocalFree(wide_argv);

    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (std::string& argument : arguments) argv.push_back(argument.data());
    const int result = ::cwxeditor::cwxeditor::run(static_cast<int>(argv.size()), argv.data());
    ::cwxeditor::lib::editor_action_log("CWXEditor_C process stopped result=" +
                                        std::to_string(result));
    ::cwxeditor::lib::shutdown_editor_diagnostics();
    return result;
}

#else

namespace {

std::filesystem::path diagnostics_directory(
        const std::filesystem::path& current_directory) {
    const char* marker = std::getenv("CARDWIRTHC_MANAGED_USERDATA_SESSION");
    const bool managed = marker != nullptr && marker[0] == '1' && marker[1] == '\0';
    if (const char* value = std::getenv("CARDWIRTHC_USER_ROOT");
        value != nullptr && *value != '\0') {
        const auto requested = std::filesystem::u8path(value);
        if (requested.is_absolute())
            return requested.lexically_normal() /
                   ::cwxeditor::lib::editor_data_directory_name() / "Log";
        if (managed)
            throw std::runtime_error(
                "managed Editor CARDWIRTHC_USER_ROOT is not absolute");
        std::error_code error;
        const auto absolute = std::filesystem::absolute(requested, error);
        if (error || absolute.empty())
            throw std::runtime_error("Editor CARDWIRTHC_USER_ROOT is invalid");
        return absolute.lexically_normal() /
               ::cwxeditor::lib::editor_data_directory_name() / "Log";
    }
    if (managed)
        throw std::runtime_error(
            "managed Editor session has no CARDWIRTHC_USER_ROOT");
#if defined(__EMSCRIPTEN__)
    // Webでは永続化するアプリ状態を/userへマウントする。 起動
    // ログもここへ置き、EditorRuntimeContextの初期
    // 化前の診断記録が 後で管理される保存領域の外へ出ないようにする。
    return std::filesystem::path("/user") /
           ::cwxeditor::lib::editor_data_directory_name() / "Log";
#elif defined(__APPLE__) || defined(__ANDROID__)
    // アプリバンドルやモバイルの作業ディレクトリは書き込み可能とは限らない。 Editor
    // の実行環境が整うまではSDLのアプリデータディレクトリを使う。 後続の
    // EditorRuntimeContextも同じ基準ディレクトリを選ぶ。
    if (char* preference = SDL_GetPrefPath("CardWirthC", "CWXEditor")) {
        const std::filesystem::path root(preference);
        SDL_free(preference);
        if (!root.empty())
            return root / ::cwxeditor::lib::editor_data_directory_name() / "Log";
    }
    throw std::runtime_error("XEditor writable diagnostics directory is unavailable");
#endif
    return current_directory /
           ::cwxeditor::lib::editor_data_directory_name() /
           "Log";
}

}  // namespace

int main(const int argc, char** argv) {
    ::cardwirth::app::configure_runtime_library_search();
    const auto current_directory = std::filesystem::current_path();
    const auto application_directory = executable_directory(
        argc > 0 && argv && argv[0] ? std::filesystem::u8path(argv[0])
                                    : std::filesystem::path{},
        current_directory);
    ::cwxeditor::lib::initialize_editor_diagnostics(
        diagnostics_directory(application_directory));
    ::cwxeditor::lib::editor_action_log("CWXEditor_C process started");
    const int result = ::cwxeditor::cwxeditor::run(argc, argv);
    ::cwxeditor::lib::editor_action_log("CWXEditor_C process stopped result=" +
                                        std::to_string(result));
    ::cwxeditor::lib::shutdown_editor_diagnostics();
    return result;
}

#endif
