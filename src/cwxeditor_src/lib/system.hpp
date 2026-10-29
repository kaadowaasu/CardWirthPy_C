#pragma once

#include "types.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::lib {

enum class SpecialDirectory { Application, ApplicationData, UserData, Documents, Temporary, Home };
enum class ThemeMode { System, Light, Dark, HighContrast };
inline constexpr std::chrono::milliseconds kDefaultDoubleClickTime{500};

class DynamicLibrary {
public:
    virtual ~DynamicLibrary() = default;
    [[nodiscard]] virtual void* symbol(std::string_view name) const = 0;
};

class SystemBackend {
public:
    virtual ~SystemBackend() = default;
    [[nodiscard]] virtual Platform platform() const noexcept = 0;
    [[nodiscard]] virtual std::filesystem::path executable_path() const = 0;
    [[nodiscard]] virtual std::filesystem::path special_directory(SpecialDirectory directory) const = 0;
    [[nodiscard]] virtual std::string locale_name() const = 0;
    [[nodiscard]] virtual ThemeMode theme_mode() const = 0;
    [[nodiscard]] virtual float scale_factor() const = 0;
    [[nodiscard]] virtual std::size_t working_memory() const = 0;
    // DのDisplay.getDoubleClickTimeは、Slow設定で選択行の編集を始
    // めるまでの遅延を決める。プラットフォームアダプターは、この時間値だけを共通処理へ提供する。
    [[nodiscard]] virtual std::chrono::milliseconds double_click_time() const noexcept {
        return kDefaultDoubleClickTime;
    }
    [[nodiscard]] virtual std::unique_ptr<DynamicLibrary> load_library(
        const std::filesystem::path& path,
        std::string& error) = 0;
    [[nodiscard]] virtual std::vector<std::byte> executable_resource(
        const std::filesystem::path& executable,
        std::string_view type,
        std::string_view name,
        std::string& error) = 0;
    virtual void set_environment(std::string name, std::string value) = 0;
    [[nodiscard]] virtual std::optional<std::string> environment(std::string_view name) const = 0;

    // 実時刻を、共通のスレッドセーフな規則で変換する。DWTやプラット
    // フォームアダプターで個別のlocaltime状態管理を持たない。
    [[nodiscard]] std::optional<std::string> format_local_time(
        std::chrono::system_clock::time_point instant,
        std::string_view format) const;
};

}  // namespace cwxeditor::lib
