#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cw::lib::managed_userdata {

inline bool active() noexcept
{
#if defined(_WIN32)
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"CARDWIRTHC_MANAGED_USERDATA_SESSION",
                                   value, 2U) == 1U &&
           value[0] == L'1';
#else
    const char* value = std::getenv("CARDWIRTHC_MANAGED_USERDATA_SESSION");
    return value != nullptr && value[0] == '1' && value[1] == '\0';
#endif
}

namespace detail {

inline std::filesystem::path path_from_utf8(std::string_view value)
{
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(value.data()), value.size()));
#else
    return std::filesystem::path(std::string(value));
#endif
}

inline std::optional<std::filesystem::path> session_root()
{
#if defined(_WIN32)
    const DWORD required = GetEnvironmentVariableW(L"CARDWIRTHC_USER_ROOT", nullptr, 0);
    if (required == 0U) return std::nullopt;
    std::wstring value(static_cast<std::size_t>(required), L'\0');
    const DWORD copied = GetEnvironmentVariableW(
        L"CARDWIRTHC_USER_ROOT", value.data(), required);
    if (copied == 0U || copied >= required) return std::nullopt;
    value.resize(copied);

    auto root = std::filesystem::path(std::move(value)).lexically_normal();
    if (root.empty() || !root.is_absolute()) return std::nullopt;

    // 専用ランチャーもセッションの認証・展開前に同じロ
    // ーカルボリューム制限を適用する。ここでも名前空間
    // を確認し、不正な管理対象環境によって出力の代替先
    // が UNC やデバイスへ向けられないようにする。
    const auto native = root.native();
    const bool ordinary_unc = native.rfind(L"\\\\", 0) == 0 &&
                              native.rfind(L"\\\\?\\", 0) != 0;
    const bool extended_unc = native.rfind(L"\\\\?\\UNC\\", 0) == 0;
    const bool device_namespace = native.rfind(L"\\\\.\\", 0) == 0;
    const bool unsupported_extended = native.rfind(L"\\\\?\\", 0) == 0 &&
        !(native.size() >= 7U &&
          ((native[4] >= L'A' && native[4] <= L'Z') ||
           (native[4] >= L'a' && native[4] <= L'z')) &&
          native[5] == L':' && native[6] == L'\\');
    auto drive_probe = root.root_path().native();
    if (native.rfind(L"\\\\?\\", 0) == 0 && native.size() >= 7U &&
        native[5] == L':' && native[6] == L'\\')
        drive_probe = native.substr(4U, 3U);
    const UINT drive_type = drive_probe.empty() ? DRIVE_NO_ROOT_DIR
                                                : GetDriveTypeW(drive_probe.c_str());
    if (ordinary_unc || extended_unc || device_namespace || unsupported_extended ||
        drive_type == DRIVE_REMOTE || drive_type == DRIVE_UNKNOWN ||
        drive_type == DRIVE_NO_ROOT_DIR)
        return std::nullopt;
    return root;
#else
    const char* value = std::getenv("CARDWIRTHC_USER_ROOT");
    if (value == nullptr || *value == '\0') return std::nullopt;
    try {
        auto root = path_from_utf8(value).lexically_normal();
        if (root.empty() || !root.is_absolute()) return std::nullopt;
        return root;
    } catch (...) {
        return std::nullopt;
    }
#endif
}

inline bool reserved_device_component(std::string value)
{
    const auto dot = value.find('.');
    if (dot != std::string::npos) value.resize(dot);
    for (auto& character : value) {
        if (character >= 'a' && character <= 'z')
            character = static_cast<char>(character - 'a' + 'A');
    }
    if (value == "CON" || value == "PRN" || value == "AUX" ||
        value == "NUL" || value == "CLOCK$")
        return true;
    return value.size() == 4U && value[3] >= '1' && value[3] <= '9' &&
           (value.rfind("COM", 0) == 0 || value.rfind("LPT", 0) == 0);
}

inline std::filesystem::path relative_path(std::string_view value)
{
    std::string portable(value);
#if !defined(_WIN32)
    // CardWirth のデータは両方の区切り文字を使う。ここでも Windows
    // と同様にバックスラッシュを区切りとし、POSIX での管理対象
    // 実行中に、Windows で開くと範囲外へ出るパスを保存しない。
    std::replace(portable.begin(), portable.end(), '\\', '/');
#endif
    return path_from_utf8(portable);
}

inline std::string component_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

inline bool safe_relative_output_path(std::string_view value)
{
    if (value.empty() || value.find('\0') != std::string_view::npos ||
        value.back() == '/' || value.back() == '\\')
        return false;
    try {
        const auto path = relative_path(value);
        if (path.empty() || path.is_absolute() || path.has_root_name() ||
            path.has_root_directory())
            return false;
        for (const auto& component : path) {
            const auto text = component_utf8(component);
            if (text.empty() || text == ".." ||
                text.find(':') != std::string::npos ||
                text.back() == '.' || text.back() == ' ' ||
                reserved_device_component(text))
                return false;
        }
        const auto normalized = path.lexically_normal();
        if (normalized.empty() || normalized == std::filesystem::path(".")) return false;
        for (const auto& component : normalized)
            if (component_utf8(component) == "..") return false;
        return true;
    } catch (...) {
        return false;
    }
}

inline std::string path_to_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

inline std::string select_relative(std::string_view requested,
                                   std::string_view fallback)
{
    if (!session_root()) return {};
    if (safe_relative_output_path(requested)) return std::string(requested);
    if (safe_relative_output_path(fallback)) return std::string(fallback);
    return {};
}

} // namespace detail

// 管理対象の保管庫以外では、パス文字列をバイト単位で互換に保つ。
// 管理対象では、認証済み UserData ルートから出られない
// 相対パスだけを ScenarioLog.xml へ保存する。
inline std::string select_output_relative_path(std::string_view requested,
                                               std::string_view fallback)
{
    if (!active()) return std::string(requested);
    return detail::select_relative(requested, fallback);
}

// 非同期の書き込み側へ渡す前に、選択した管理対象パスを絶対セッ
// ションパスに解決する。Engine ループの終了開始後、作
// 業ディレクトリーが戻った先へ遅れて書き込まれることを防ぐ。
inline std::string resolve_output_path(std::string_view requested,
                                       std::string_view fallback)
{
    if (!active()) return std::string(requested);
    const auto root = detail::session_root();
    if (!root) return {};
    const auto relative = detail::safe_relative_output_path(requested)
        ? std::string(requested)
        : detail::safe_relative_output_path(fallback) ? std::string(fallback)
                                                      : std::string{};
    if (relative.empty()) return {};
    const auto candidate = (*root / detail::relative_path(relative)).lexically_normal();
    const auto below_root = candidate.lexically_relative(*root).lexically_normal();
    if (below_root.empty() || below_root == std::filesystem::path(".") || below_root.is_absolute() ||
        below_root.has_root_name() || below_root.has_root_directory())
        return {};
    for (const auto& component : below_root)
        if (detail::component_utf8(component) == "..") return {};
    return detail::path_to_utf8(candidate);
}

} // namespace cw::lib::managed_userdata
