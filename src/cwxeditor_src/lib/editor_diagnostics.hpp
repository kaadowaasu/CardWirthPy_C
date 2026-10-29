#pragma once

#include <filesystem>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace cwxeditor::lib {

// 単独Editorの診断。ReleaseはCWXEDITOR_LOG_IN_RELEASE
// で有効化し、Debugは常に同じ経路を使う。
void initialize_editor_diagnostics(const std::filesystem::path& application_directory,
                                   bool show_console = true);
void shutdown_editor_diagnostics() noexcept;
void editor_action_log(std::string_view message) noexcept;
[[nodiscard]] bool editor_diagnostics_enabled() noexcept;
// 単独・埋め込みEditorで共有し、既存のログセッションを保持する。
void ensure_editor_diagnostics(const std::filesystem::path& directory);

class EditorPerformanceScope final {
public:
    explicit EditorPerformanceScope(std::string_view operation,
                                    std::uint64_t threshold_us = 5000) noexcept;
    ~EditorPerformanceScope() noexcept;
    EditorPerformanceScope(const EditorPerformanceScope&) = delete;
    EditorPerformanceScope& operator=(const EditorPerformanceScope&) = delete;
private:
    std::string_view operation_;
    std::chrono::steady_clock::time_point started_{};
    std::uint64_t threshold_us_{};
    bool enabled_{};
};

}  // namespace cwxeditor::lib
