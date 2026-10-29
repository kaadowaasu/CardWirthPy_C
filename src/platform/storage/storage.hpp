#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <mutex>
#include <utility>
#include <unordered_map>
#include <vector>

namespace cardwirth::platform {

// Storageへ渡すパスはAppPathsで構成する。NativeStorage
// は相対パスをルート基準で解決し、 WebStorageなどは
// 同じ値をメモリ上のツリー・IDBFS・その他の永続保存先へ対
// 応付けられる。 ネイティブ実装は、設定されたルート内であれば
// AppPathsの絶対パスも受け付ける。 アプリ境界での変
// 換を明示し、コアへファイルシステム呼び出しを持ち込まない。
using StoragePath = std::filesystem::path;
// ファイルシステムを使うすべての保存先で共用する、OS側の永続化操作。
[[nodiscard]] bool sync_storage_directory(const StoragePath& directory,
                                          std::error_code& error);
// 一時作成した項目を、内容のコピーや既存項目の置き換えなしで配置する。
// ここではOSの基本操作だけを行い、ルート内のパス検証と永続化の完
// 了確認は呼び出し元が行う。 未対応の環境では安全のため失敗とする。
[[nodiscard]] bool rename_storage_path_no_replace(const StoragePath& source,
                                                 const StoragePath& destination,
                                                 std::error_code& error) noexcept;

enum class StorageEntryType {
    Missing,
    File,
    Directory,
    Symlink,
    Other,
};

struct StorageStat {
    StorageEntryType type{StorageEntryType::Missing};
    std::uintmax_t size{};
    // Unix時刻の起点からのナノ秒。modified_validが
    // なく値が0なら取得不能とするが、 実際に観測した起点の時刻は
    // 有効。ネイティブ・Android・Webで共通の表現を使う。
    std::int64_t modified{};
    // 非公開の暗号化実装を含め、従来の{type,size,modified}
    // の集成体契約を保つ。 リンクは走査の境界として扱う。
    bool directory_link{};
    // 実際に観測したUnix時刻の起点と、取得できない時刻を区別する。 従来の
    // 提供元でmodifiedが0以外なら、このフラグがなくても有効とする。
    bool modified_valid{};

    [[nodiscard]] bool exists() const noexcept {
        return type != StorageEntryType::Missing;
    }
    [[nodiscard]] bool is_file() const noexcept {
        return type == StorageEntryType::File;
    }
    [[nodiscard]] bool is_directory() const noexcept {
        return type == StorageEntryType::Directory;
    }
};

struct StorageEntry {
    StoragePath path;
    std::string name;
    StorageStat stat;
};

class StorageReadHandle {
public:
    virtual ~StorageReadHandle() = default;

    // 読み込んだバイト数を返す。0はEOFまたはI/O
    // の失敗を表し、good()で区別する。
    virtual std::size_t read(std::span<std::byte> destination) = 0;
    // Python互換のZIP読み込みなどで使う、任意対応のランダムアクセス。
    // 順次読み込み専用の提供元は、ファイルを実体化せず未対応として拒否する。
    virtual bool seek(std::uint64_t /*offset*/) { return false; }
    [[nodiscard]] virtual bool good() const noexcept = 0;
};

class StorageWriteHandle {
public:
    virtual ~StorageWriteHandle() = default;

    virtual bool write(std::span<const std::byte> source) = 0;
    virtual bool flush() = 0;
    [[nodiscard]] virtual bool good() const noexcept = 0;
};

// EditorとEngineが同じ保存ライフサイクルを参照でき
// るよう、永続化状態を共通の契約に含める。 文書モデルを保持し
// ない実装は、下のStorageの何もしない既定実装を使える。
enum class PersistenceState : std::uint8_t {
    Clean,
    MemoryDirty,
    FsDirty,
    Syncing,
    Persisted,
    SyncFailed,

    // Editorの従来のストレージ窓口で使う互換名。
    Dirty = MemoryDirty,
    Saving = Syncing,
    Failed = SyncFailed,
};

struct PersistenceInfo {
    PersistenceState state{PersistenceState::Clean};
    std::uint64_t revision{};
    std::uint64_t persisted_revision{};
    std::string error;
    // 保存成功後に提供元が報告する、致命的ではない制約。
    std::string durability_note;

    [[nodiscard]] bool dirty() const noexcept {
        const bool pending = state == PersistenceState::MemoryDirty ||
                             state == PersistenceState::FsDirty ||
                             state == PersistenceState::Syncing ||
                             state == PersistenceState::SyncFailed;
        return pending || revision != persisted_revision;
    }
    [[nodiscard]] bool is_dirty() const noexcept { return dirty(); }
};

// 上位の保存処理が具体的なStorage実装を識別
// せずに最適化できるよう、永続化の特性を示す。 ネ
// イティブと文書の実装では、安全側の既定値を使う。
struct StorageCapabilities {
    bool atomic_multi_operation_transactions{};
    bool requires_native_secondary_indexes{true};
};

class Storage {
public:
    virtual ~Storage() = default;
    // 操作全体の成功を返す前に、キューに積まれた永続化書き込みを完了させ
    // る。 同期実装やメモリ上の実装には、追加で処理するキューはない。
    virtual bool sync() { return true; }

    [[nodiscard]] virtual StorageCapabilities capabilities() const noexcept {
        return {};
    }

    [[nodiscard]] virtual StoragePath root() const = 0;
    [[nodiscard]] virtual bool exists(const StoragePath& path) const = 0;
    [[nodiscard]] virtual std::optional<StorageStat> stat(
        const StoragePath& path) const = 0;
    [[nodiscard]] virtual std::vector<StorageEntry> list(
        const StoragePath& directory) const = 0;

    [[nodiscard]] virtual std::optional<std::vector<std::byte>> read(
        const StoragePath& path) const = 0;
    virtual bool write(const StoragePath& path,
                       std::span<const std::byte> data,
                       bool create_parent = true) = 0;
    // 保存先と同じ親へ一時ファイルを書き、書き出しを確定してか
    // ら一度のファイル操作で置き換える。 置き換え前の失敗なら
    // 旧ファイルを残す。置き換え後に親の同期が失敗した場合は、
    // 新しいファイルを残したまま永続化未確認として報告する
    // ため、呼び出し元はlast_error()を確認する。
    virtual bool write_atomic(const StoragePath& path,
                              std::span<const std::byte> data,
                              bool create_parent = true) = 0;

    // メタデータの設定だけを行い、内容の読み書きや置き換え方針は扱わない。
    // 読み取り専用・独自の提供元では省略でき、未対応の更新は失敗とする。
    virtual bool set_modified(const StoragePath&, std::int64_t) { return false; }

    // Editorの窓口で使う互換名。仮想の書き込み操
    // 作を正式な入口として保ち、 既存のEngine・
    // Web実装へ別の純粋仮想関数の実装を要求しない。
    virtual bool atomic_save(const StoragePath& path,
                             std::span<const std::byte> data,
                             bool create_parent = true) {
        return write_atomic(path, data, create_parent);
    }
    bool save_atomic(const StoragePath& path,
                     std::span<const std::byte> data,
                     bool create_parent = true) {
        return atomic_save(path, data, create_parent);
    }

    // 軽量な仮想実装では、永続化状態の追跡は
    // 任意。 必要ならこれらを上書きし、既定
    // では既存実装とのソース互換性を保つ。
    [[nodiscard]] virtual PersistenceInfo persistence(const StoragePath&) const {
        return {};
    }
    [[nodiscard]] virtual bool dirty(const StoragePath& path) const {
        return persistence(path).dirty();
    }
    virtual bool mark_dirty(const StoragePath&) { return false; }
    virtual bool mark_clean(const StoragePath&) { return false; }
    bool clear_dirty(const StoragePath& path) { return mark_clean(path); }

    // 任意対応の複数操作トランザクション。ネイティブ・文書の
    // 実装は、何もしない既定処理で従来の動作を保つ。 DB型
    // のUserDataはこれを上書きし、一つの論理保存でXML
    // やカードの変更ごとにfsyncしないようにする。
    virtual bool begin_transaction() { return true; }
    virtual bool commit_transaction() { return true; }
    virtual void rollback_transaction() noexcept {}

    virtual bool mkdir(const StoragePath& path, bool recursive = true) = 0;
    // 所有権を確認して巻き戻すためのネイティブ拡張。繰り返し可能なmkdir
    // と異なり、 成功はこの呼び出しが新しいディレクトリを作ったことを保
    // 証する。 未対応の独自実装は、データ消失の危険を避けて失敗とする。
    virtual bool mkdir_exclusive(const StoragePath&) { return false; }
    // removeは対象が存在しなくても成功とする。 中身のあ
    // るディレクトリにはrecursive=trueが必要。
    virtual bool remove(const StoragePath& path, bool recursive = false) = 0;
    virtual bool rename(const StoragePath& source,
                        const StoragePath& destination,
                        bool overwrite = false) = 0;
    // overwrite=trueでは置き換えとして扱う。既存の移動先ツリーは
    // コピー元の移動・コピーの確定が成功するまで保持し、その後に削除する。
    virtual bool copy(const StoragePath& source,
                      const StoragePath& destination,
                      bool overwrite = false) = 0;
    virtual bool move(const StoragePath& source,
                      const StoragePath& destination,
                      bool overwrite = false) = 0;

    [[nodiscard]] virtual std::unique_ptr<StorageReadHandle> open_read(
        const StoragePath& path) const = 0;
    [[nodiscard]] virtual std::unique_ptr<StorageWriteHandle> open_write(
        const StoragePath& path,
        bool truncate = true,
        bool create_parent = true) = 0;

    // Web・Androidの実装へ例外を強制しないよう、共通の
    // I/O APIはoptional/boolで結果を返す。
    // 診断情報は操作の開始時に消去し、補助情報としてのみ使う。
    [[nodiscard]] virtual std::string last_error() const = 0;
};

// 起点を変える際も、ファイル時計自身の精度を保つ。 ファイルの時刻は、3000
// 年などUnix時刻のint64ナノ秒の範囲を超える場合がある。
using StorageSystemFileTime = std::chrono::sys_time<std::filesystem::file_time_type::duration>;
[[nodiscard]] StorageSystemFileTime storage_file_time_to_system(
    std::filesystem::file_time_type value) noexcept;
[[nodiscard]] std::filesystem::file_time_type storage_file_time_from_system(
    StorageSystemFileTime value) noexcept;
[[nodiscard]] std::int64_t storage_timestamp_from_file_time(
    std::filesystem::file_time_type value) noexcept;
[[nodiscard]] std::optional<std::filesystem::file_time_type>
storage_timestamp_to_file_time(std::int64_t value) noexcept;
[[nodiscard]] std::int64_t storage_timestamp_now() noexcept;

class NativeStorage final : public Storage {
public:
    explicit NativeStorage(std::filesystem::path root,
                            bool create_root = true);
    // Editorの従来の文字列ルートAPI向けに残すUTF-8変換アダプター。
    explicit NativeStorage(std::string root_utf8,
                           bool create_root = true);

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
    bool sync() override;
    bool set_modified(const StoragePath& path, std::int64_t timestamp) override;
    bool write_atomic(const StoragePath& path,
                      std::span<const std::byte> data,
                      bool create_parent = true) override;
    bool atomic_save(const StoragePath& path,
                     std::span<const std::byte> data,
                     bool create_parent = true) override {
        return write_atomic(path, data, create_parent);
    }
    [[nodiscard]] PersistenceInfo persistence(
        const StoragePath& path) const override;
    [[nodiscard]] bool dirty(const StoragePath& path) const override;
    bool mark_dirty(const StoragePath& path) override;
    bool mark_clean(const StoragePath& path) override;
    bool mkdir(const StoragePath& path, bool recursive = true) override;
    bool mkdir_exclusive(const StoragePath& path) override;
    bool remove(const StoragePath& path, bool recursive = false) override;
    bool rename(const StoragePath& source,
                const StoragePath& destination,
                bool overwrite = false) override;
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
    // 未対応・異なるデバイス間の操作と、永続化の失敗を区別できる
    // ようにする。 コピー・削除による代替を許すのは前者だけ。
    [[nodiscard]] std::error_code last_error_code() const;

private:
    [[nodiscard]] std::filesystem::path resolve(const StoragePath& path) const;
    bool mkdir_impl(const StoragePath& path, bool recursive, bool exclusive);
    [[nodiscard]] bool fail(const std::error_code& error,
                            const std::string& operation) const;
    [[nodiscard]] bool fail(const std::string& message) const;
    [[nodiscard]] std::pair<std::string, std::error_code> error_snapshot() const;
    void clear_error() const;
    [[nodiscard]] StoragePath persistence_key(const StoragePath& path) const;
    [[nodiscard]] StoragePath persistence_key_for_resolved(
        const StoragePath& resolved) const;
    std::uint64_t begin_persistence(const StoragePath& path) const;
    void finish_persistence(const StoragePath& path,
                            std::uint64_t revision,
                            bool success,
                            std::string error,
                            std::string durability_note = {}) const;
    void record_persisted(const StoragePath& path) const;
    void erase_persistence(const StoragePath& path) const;
    void move_persistence(const StoragePath& source,
                          const StoragePath& destination) const;
    void copy_persistence(const StoragePath& source,
                          const StoragePath& destination) const;
    void stream_write_started(const StoragePath& path,
                              std::uint64_t revision) const;

    std::filesystem::path root_;
    mutable std::mutex error_mutex_;
    mutable std::string last_error_;
    mutable std::error_code last_error_code_;
    // NativeStorageのインスタンス内で、不可分な配置
    // を直列化する。 一時名はプロセス内で一意だが、複数ワーカ
    // ーが同じ保存先を使う場合は置き換え順も決める必要がある。
    mutable std::mutex mutation_mutex_;
    mutable std::mutex persistence_mutex_;
    mutable std::unordered_map<StoragePath, PersistenceInfo> persistence_;
    // ストリームはStorage本体より長く生きる場合がある。 完了
    // コールバックは所有者へ触れる前にこのトークンの弱参照を確認し
    // 、 通常の終了時に破棄済みの所有者を呼び出さないようにする。
    std::shared_ptr<std::uint8_t> lifetime_token_{
        std::make_shared<std::uint8_t>(std::uint8_t{0})};
};

[[nodiscard]] std::unique_ptr<Storage> make_native_storage(
    std::filesystem::path root,
    bool create_root = true);

// ストレージの実装を所有し、読み取りだけを公開してすべての変更を
// 拒否する。 アプリ資産の境界で使い、誤った保存操作で配布済みの
// Data/Skin/Scenarioを書き換えないようにする。
[[nodiscard]] std::unique_ptr<Storage> make_read_only_storage(
    std::unique_ptr<Storage> backing);

}  // namespace cardwirth::platform
