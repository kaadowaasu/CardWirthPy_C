#pragma once

// 任意で読み込む実行時ライブラリは実行ファイルのディレクトリへ直接置かない。 デス
// クトップはlib/、Appleのバンドルは標準のFrameworksを使う。 Android
// ではAPK内のlib/<ABI>へネイティブライブラリが配置される。
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <mutex>

namespace cardwirth::app {

inline void configure_runtime_library_search()
{
    static std::once_flag once;
    std::call_once(once, [] {
        std::wstring executable(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
                                                static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size()) return;
        executable.resize(length);
        const auto library_directory = std::filesystem::path(executable).parent_path() / L"lib";
        if (!std::filesystem::is_directory(library_directory)) return;

        // 通常のLoadLibraryW（Editorの任意のBASS実装を含む）
        // でも、 作業ディレクトリに依存しない、このプロセス共通の検索先を使う。
        if (SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
                                     LOAD_LIBRARY_SEARCH_USER_DIRS)) {
            (void)AddDllDirectory(library_directory.c_str());
        }
    });
}

}  // namespace cardwirth::app
#else
namespace cardwirth::app {
inline void configure_runtime_library_search() {}
}  // namespace cardwirth::app
#endif
