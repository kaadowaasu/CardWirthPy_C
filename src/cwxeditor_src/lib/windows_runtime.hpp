#pragma once

#include "audio.hpp"
#include "filesystem.hpp"
#include "process.hpp"
#include "system.hpp"
#include "threading.hpp"

#include <memory>
#include <optional>

namespace cwxeditor::lib::windows_runtime {

[[nodiscard]] bool supports_native_shell_paths() noexcept;

// Windows実行環境を利用できないプラットフォームでは、ファクトリーはnullptr
// を返す。このインターフェースにWindowsのヘッダーは不要。
[[nodiscard]] std::shared_ptr<FileSystemBackend> make_filesystem();
/// 省略可能なUIディスパッチャーを指定して、Windowsのファイルシステ
/// ムアダプターを作る。指定された場合、ディレクトリ通知は必ずこのディスパッ
/// チャーを経由し、Editorの状態コールバックを所有スレッドで実行する。
[[nodiscard]] std::shared_ptr<FileSystemBackend> make_filesystem(
    std::shared_ptr<MainDispatcher> dispatcher);
[[nodiscard]] std::shared_ptr<ProcessBackend> make_process();
[[nodiscard]] std::shared_ptr<SystemBackend> make_system();
[[nodiscard]] std::shared_ptr<AudioBackend> make_audio();
[[nodiscard]] std::shared_ptr<FileSystemBackend> make_filesystem_backend();
[[nodiscard]] std::shared_ptr<FileSystemBackend> make_filesystem_backend(
    std::shared_ptr<MainDispatcher> dispatcher);
[[nodiscard]] std::shared_ptr<ProcessBackend> make_process_backend();
[[nodiscard]] std::shared_ptr<SystemBackend> make_system_backend();
[[nodiscard]] std::shared_ptr<AudioBackend> make_audio_backend();
[[nodiscard]] std::optional<std::uint32_t> process_handle_count() noexcept;

}  // namespace cwxeditor::lib::windows_runtime

namespace cwxeditor::lib {

[[nodiscard]] std::shared_ptr<FileSystemBackend> make_windows_filesystem();
[[nodiscard]] std::shared_ptr<FileSystemBackend> make_windows_filesystem(
    std::shared_ptr<MainDispatcher> dispatcher);
[[nodiscard]] std::shared_ptr<ProcessBackend> make_windows_process();
[[nodiscard]] std::shared_ptr<SystemBackend> make_windows_system();
[[nodiscard]] std::shared_ptr<AudioBackend> make_windows_audio();
[[nodiscard]] std::shared_ptr<FileSystemBackend> make_windows_filesystem_backend();
[[nodiscard]] std::shared_ptr<FileSystemBackend> make_windows_filesystem_backend(
    std::shared_ptr<MainDispatcher> dispatcher);
[[nodiscard]] std::shared_ptr<ProcessBackend> make_windows_process_backend();
[[nodiscard]] std::shared_ptr<SystemBackend> make_windows_system_backend();
[[nodiscard]] std::shared_ptr<AudioBackend> make_windows_audio_backend();

}  // namespace cwxeditor::lib
