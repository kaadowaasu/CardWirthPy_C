#pragma once

#include "storage.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace cardwirth::platform {

// 従来のEngineを作業ディレクトリ基準のファイルAPIからStorage
// へ移す間の互換接続。 暗号化Engineのセッションだけに設
// 定し、通常のパスと資産のパスは既存のネイティブ処理を使う。
class ScopedUserStorageMount final {
public:
    ScopedUserStorageMount(Storage& storage, std::filesystem::path logical_root);
    ~ScopedUserStorageMount();
    ScopedUserStorageMount(const ScopedUserStorageMount&) = delete;
    ScopedUserStorageMount& operator=(const ScopedUserStorageMount&) = delete;
private:
    std::uint64_t generation_{};
};

// pathが有効なUserDataマウントに属する場合だけ、トランザクショ
// ンを始める。 commit()が成功していなければ、破棄時に巻き戻す。
class ScopedMountedStorageTransaction final {
public:
    explicit ScopedMountedStorageTransaction(const std::filesystem::path& path);
    ~ScopedMountedStorageTransaction();
    ScopedMountedStorageTransaction(const ScopedMountedStorageTransaction&) = delete;
    ScopedMountedStorageTransaction& operator=(const ScopedMountedStorageTransaction&) = delete;
    [[nodiscard]] bool good() const noexcept { return good_; }
    [[nodiscard]] bool commit();
    [[nodiscard]] const std::string& last_error() const noexcept { return error_; }
private:
    void release_mount() noexcept;
    Storage* storage_{};
    std::uint64_t mount_generation_{};
    bool active_{};
    bool good_{true};
    std::string error_;
};

[[nodiscard]] bool is_mounted_user_path(const std::filesystem::path& path);
[[nodiscard]] std::optional<StorageCapabilities> mounted_user_storage_capabilities(
    const std::filesystem::path& path);
// 相対パスがUserDataを表すとAPIの契約で分かっている従来コ
// ード向けの、明示的な解決処理。 汎用の資産I/Oからは使わない。
[[nodiscard]] std::optional<std::filesystem::path> resolve_mounted_user_path(
    const std::filesystem::path& path);
[[nodiscard]] std::optional<std::vector<std::byte>> read_mounted_user_file(
    const std::filesystem::path& path);
// 返した入力が閉じられるまで、マウントを保持する。
[[nodiscard]] std::unique_ptr<StorageReadHandle> open_mounted_user_file(
    const std::filesystem::path& path);
// 提供元の既存の順次書き込みを使い、マウントの利用権を保持する。 open_read
// と同様、不可分な配置やパスの規則は呼び出し元が管理する。
[[nodiscard]] std::unique_ptr<StorageWriteHandle> open_mounted_user_write_file(
    const std::filesystem::path& path, bool truncate = true, bool create_parent = true);
[[nodiscard]] std::optional<std::vector<std::byte>> read_mounted_user_file_prefix(
    const std::filesystem::path& path, std::size_t limit);
[[nodiscard]] std::optional<StorageStat> stat_mounted_user_path(
    const std::filesystem::path& path, bool throw_on_error = false);
[[nodiscard]] std::optional<std::vector<StorageEntry>> list_mounted_user_path(
    const std::filesystem::path& path, bool throw_on_error = false);
[[nodiscard]] bool write_mounted_user_file(
    const std::filesystem::path& path, std::span<const std::byte> data,
    bool atomic = true);
[[nodiscard]] bool set_modified_mounted_user_path(const std::filesystem::path& path,
                                    std::int64_t timestamp);

bool make_mounted_user_directory(
    const std::filesystem::path& path, bool recursive = true);
// 成功した場合は新しいディレクトリを確保する。既存のパスなら必ず失敗する。
[[nodiscard]] bool reserve_mounted_user_directory(const std::filesystem::path& path);
[[nodiscard]] bool remove_mounted_user_path(
    const std::filesystem::path& path, bool recursive = false);
[[nodiscard]] bool rename_mounted_user_path(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    bool overwrite = false);
[[nodiscard]] bool copy_mounted_user_path(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    bool overwrite = false);
[[nodiscard]] bool move_mounted_user_path(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    bool overwrite = false);

}  // namespace cardwirth::platform
