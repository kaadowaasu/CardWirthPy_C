#pragma once

#include <cstddef>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace cwxeditor::lib {

// 共通APIのファイルパスはUTF-8で受け渡す。generic_string()はWindows
// の現在のコードページを使うことがあり、その範囲外の有効なパス文字を扱えない。
[[nodiscard]] inline std::string generic_path_to_utf8(
    const std::filesystem::path& path) {
    const auto encoded = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()),
                       encoded.size());
}

// C++20のファイル時計はシステム時計またはUTC時計への
// 変換を提供する。変換にはここで標準ライブラリの機能を使い
// 、別々に取得した現在時刻の差からエポック差を推定しない。
template <typename Duration>
[[nodiscard]] auto fileTimeToSystem(
    std::chrono::time_point<std::filesystem::file_time_type::clock, Duration> value) {
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
    return std::chrono::clock_cast<std::chrono::system_clock>(value);
#else
    return std::filesystem::file_time_type::clock::to_sys(value);
#endif
}

template <typename Duration>
[[nodiscard]] auto fileTimeFromSystem(
    std::chrono::time_point<std::chrono::system_clock, Duration> value) {
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
    return std::chrono::clock_cast<std::filesystem::file_time_type::clock>(value);
#else
    return std::filesystem::file_time_type::clock::from_sys(value);
#endif
}

enum class FileChangeKind { Created, Modified, Removed, Renamed, Attributes, Overflow };

struct FileChange {
    FileChangeKind kind{FileChangeKind::Modified};
    std::filesystem::path path;
    std::filesystem::path old_path;
};

struct WatchRequest {
    std::filesystem::path directory;
    bool recursive{};
    std::function<void(const std::vector<FileChange>&)> on_changed;
    std::function<void(std::string)> on_error;
    // ポーリング用アダプターだけで使用する。ネイティブの変更通知は待機不要。
    std::chrono::milliseconds poll_interval{500};
};

class FileWatchHandle {
public:
    virtual ~FileWatchHandle() = default;
    // 破棄も含め、所有元のUIスレッドで直列に実行する。通知
    // 内からも呼び出せ、その場合は待機中の通知を無効にする。
    virtual void stop() noexcept = 0;
};

class FileSystemBackend {
public:
    virtual ~FileSystemBackend() = default;
    // 監視は自身の要求を所有する。通知はファイル操作用ワーカーではなく、所有
    // 元のUIディスパッチャーで実行する。stop()は待機中の通知も無効に
    // する。未対応または設定失敗時はnullを返すか例外を送出し、何もしない
    // まま成功扱いにしない。ハンドルの作成と停止も同じUIスレッドで行う。
    [[nodiscard]] virtual std::unique_ptr<FileWatchHandle> watch(WatchRequest request) = 0;
    // ネイティブの一時ファイル書き込みは、対象と同じディレクトリに準備し、フ
    // ラッシュしてから置換する。置換前に失敗した場合は既存の出力先を維持する
    // 。プロバイダーへの公開と後処理はDialogBackendの別操作で
    // 行う。この保証を満たせない実装はfalseを返す（ヘッドレスなど）。
    virtual bool durable_write(const std::filesystem::path& target,
                               std::span<const std::byte> data,
                               std::string& error) = 0;
    virtual bool atomic_replace(const std::filesystem::path& temporary,
                                const std::filesystem::path& target,
                                std::string& error) = 0;
    virtual bool move_to_trash(const std::vector<std::filesystem::path>& paths,
                               std::string& error) = 0;
    [[nodiscard]] virtual bool valid_filename_character(char32_t value) const = 0;
    // filenames_equal()と同じパス規則を使うマップや集
    // 合向けの安定した比較キー。区切り文字や大文字小文字の正規化は実
    // ファイルシステムのアダプターに集約し、製品側に重複実装しない。
    [[nodiscard]] virtual std::string filename_identity(
        const std::filesystem::path& path) const {
        return generic_path_to_utf8(path.lexically_normal());
    }
    [[nodiscard]] virtual bool filenames_equal(
        const std::filesystem::path& left,
        const std::filesystem::path& right) const {
        return filename_identity(left) == filename_identity(right);
    }
};

}  // namespace cwxeditor::lib
