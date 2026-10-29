#pragma once

// EditorはEngineやExplorerと同じStorage
// 契約を通じてこのバックエンドを使う。Emscriptenでは/user
// をIDBFSにマウントし、syncfsで永続化する。ほかのビ
// ルドでは小さなメモリ内ツリーを使い、ブラウザやホスト固有のフ
// ァイルシステムなしでEditorをテストできるようにする。
#include "../../platform/storage/storage.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cardwirth::platform {

enum class WebStorageState : std::uint8_t {
    Booting,
    LoadingStorage,
    Ready,
    SavingStorage,
    StorageError,
};

// StorageStateは別名とし、ホストが具体的
// なバックエンド名を使わずに状態を扱えるようにする。
using StorageState = WebStorageState;

enum class WebStorageError : std::uint8_t {
    None,
    NotReady,
    MountFailed,
    SyncFailed,
    QuotaExceeded,
    InvalidPath,
    IoError,
};

struct WebStorageOptions {
    // WebビルドではここにIDBFSをマウントする。Storageの相対
    // パスも常にここを基準とする。/userはアプリケーション全体の契約。
    StoragePath root{StoragePath("/user")};

    // コンストラクターで初回読み込みを開始する。ホストが自身の起動処理を
    // 済ませてからstart()を呼ぶ必要がある場合はfalseにする。
    bool auto_start{true};

    // Emscriptenビルドは既定でIDBFSを使う。無効化すると、
    // 結果が一定のメモリ専用テストビルドに使える。ブラウザがIDBFS
    // に対応しない場合はfallback_to_memoryも使える。
    bool use_idbfs{true};
    bool fallback_to_memory{false};

    // 0以外の上限を指定すると、メモリバックエンドは無制限に拡張せずQuotaExceeded
    // を報告する。容量制限の処理を一定の条件でテストする場合にも使
    // える。ブラウザバックエンドの実際の上限はIDBFSに任せる。
    std::size_t max_memory_bytes{};

    // 0は無制限の再試行。それ以外ではretry()はこの回数で停
    // 止し、バックエンドをStorageErrorのままにする。
    std::size_t max_retries{3};
};

namespace detail {
struct WebStorageSharedState;
struct WebStorageLifetime;
struct WebStorageMemory;
}  // namespace detail

class WebStorage final : public Storage {
public:
    WebStorage();
    explicit WebStorage(WebStorageOptions options);
    explicit WebStorage(StoragePath root, WebStorageOptions options = {});
    ~WebStorage() override;

    WebStorage(const WebStorage&) = delete;
    WebStorage& operator=(const WebStorage&) = delete;
    WebStorage(WebStorage&&) = delete;
    WebStorage& operator=(WebStorage&&) = delete;

    // IDBFSのマウントと初回読み込みを開始する。Emscripten
    // 以外のビルドでは直接Readyへ遷移する。
    [[nodiscard]] bool start();
    [[nodiscard]] bool begin_load();
    [[nodiscard]] bool begin_save();
    [[nodiscard]] bool sync(bool populate) {
        return populate ? begin_load() : begin_save();
    }
    [[nodiscard]] bool sync_from_persistent();
    bool sync() override;
    [[nodiscard]] bool sync_to_persistent();

    // 最後に失敗した読み込み・保存操作を再試行する。UIの再試行ボ
    // タンから安全に呼べ、すでにReadyなら何もせず成功する。
    [[nodiscard]] bool retry();

    // ネイティブ/Web共通ループを使うホスト向けに残すポーリングフック
    // 。Emscriptenの同期はIDBFSコールバックで完了するため
    // ポーリング不要。ネイティブのメモリ保存領域にも進める処理はない。
    void poll() noexcept;

    // IDBFSを明示的にマウントする。有効な場合はstart()が自動的に呼ぶ。
    [[nodiscard]] bool mount_idbfs();

    [[nodiscard]] WebStorageState state() const noexcept;
    using State = WebStorageState;
    [[nodiscard]] StorageState storage_state() const noexcept { return state(); }
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept { return ready(); }
    [[nodiscard]] bool mounted() const noexcept;
    [[nodiscard]] bool using_idbfs() const noexcept;
    [[nodiscard]] bool capacity_error() const noexcept;
    [[nodiscard]] bool quota_exceeded() const noexcept { return capacity_error(); }
    [[nodiscard]] WebStorageError error_kind() const noexcept;
    [[nodiscard]] std::size_t retry_count() const noexcept;
    [[nodiscard]] std::size_t bytes_used() const noexcept;
    [[nodiscard]] std::size_t memory_limit() const noexcept;

    // Storageの契約。
    [[nodiscard]] StoragePath root() const override { return root_; }
    [[nodiscard]] bool exists(const StoragePath& path) const override;
    [[nodiscard]] std::optional<StorageStat> stat(
        const StoragePath& path) const override;
    [[nodiscard]] std::vector<StorageEntry> list(
        const StoragePath& directory) const override;
    [[nodiscard]] std::optional<std::vector<std::byte>> read(
        const StoragePath& path) const override;
    bool write(const StoragePath& path,
               std::span<const std::byte> data,
               bool create_parent = true) override;
    bool set_modified(const StoragePath& path, std::int64_t timestamp) override;
    bool write_atomic(const StoragePath& path,
                      std::span<const std::byte> data,
                      bool create_parent = true) override;
    bool mkdir(const StoragePath& path, bool recursive = true) override;
    bool mkdir_exclusive(const StoragePath& path) override;
    bool remove(const StoragePath& path, bool recursive = false) override;
    bool rename(const StoragePath& source,
                const StoragePath& destination,
                bool overwrite = false) override;
    // overwrite=trueは出力先の部分木全体
    // を置き換える。NativeStorageと同じ契
    // 約で、既存ディレクトリの古い子とは結合しない。
    bool copy(const StoragePath& source,
              const StoragePath& destination,
              bool overwrite = false) override;
    bool move(const StoragePath& source,
              const StoragePath& destination,
              bool overwrite = false) override;
    [[nodiscard]] std::unique_ptr<StorageReadHandle> open_read(
        const StoragePath& path) const override;
    [[nodiscard]] std::unique_ptr<StorageWriteHandle> open_write(
        const StoragePath& path,
        bool truncate = true,
        bool create_parent = true) override;
    [[nodiscard]] std::string last_error() const override;

private:
    bool mkdir_impl(const StoragePath& path, bool recursive, bool exclusive);
    [[nodiscard]] std::optional<std::string> key_for(
        const StoragePath& path) const;
    [[nodiscard]] StoragePath path_for_key(const std::string& key) const;
    [[nodiscard]] bool ready_for_io() const;
    [[nodiscard]] bool begin_sync(bool populate, bool from_retry = false);
    [[nodiscard]] bool fail(std::string message,
                            WebStorageError kind = WebStorageError::IoError) const;
    [[nodiscard]] bool fail_quota(std::string message) const;
    void clear_error() const;
    void set_error(std::string message, WebStorageError kind) const;

    StoragePath root_;
    WebStorageOptions options_;
    std::shared_ptr<detail::WebStorageSharedState> shared_;
    std::shared_ptr<detail::WebStorageLifetime> lifetime_;
    std::shared_ptr<detail::WebStorageMemory> memory_;
    bool memory_backend_{true};
    // 不正なルートでは、すべての操作を安全側に失敗させる。オブジェクト
    // 自体は構築可能にし、WebやC ABIの境界から例外を送出せず、
    // bool/optionalを使うStorage契約でerror_kind()/last_error()
    // からエラーを報告する。
    bool root_valid_{true};
};

[[nodiscard]] std::string_view web_storage_state_name(
    WebStorageState state) noexcept;
[[nodiscard]] std::string_view web_storage_error_name(
    WebStorageError error) noexcept;

// 具体的な戻り値型によりEditorホストで状態を調べられる
// 。起動時にバックエンドを選ぶ際は、std::unique_ptr<Storage>
// へムーブして使うこともできる。
[[nodiscard]] std::unique_ptr<WebStorage> make_web_storage(
    WebStorageOptions options = {});
[[nodiscard]] std::unique_ptr<WebStorage> make_memory_storage(
    WebStorageOptions options = {});

}  // namespace cardwirth::platform

// Editorのライブラリでは、プラットフォーム部品にcwxeditor::lib
// を使う。ここに別名を置き、共通platform名前空間の表記へヘッダーを直
// 接依存させずに、Editor側からWebStorageを選べるようにする。
namespace cwxeditor::lib {
using WebStorage = ::cardwirth::platform::WebStorage;
using WebStorageError = ::cardwirth::platform::WebStorageError;
using WebStorageOptions = ::cardwirth::platform::WebStorageOptions;
using WebStorageState = ::cardwirth::platform::WebStorageState;
using StorageState = ::cardwirth::platform::StorageState;
using ::cardwirth::platform::make_memory_storage;
using ::cardwirth::platform::make_web_storage;
using ::cardwirth::platform::web_storage_error_name;
using ::cardwirth::platform::web_storage_state_name;
}  // namespace cwxeditor::lib
