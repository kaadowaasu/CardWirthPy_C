#include "storage_mount.hpp"

#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace cardwirth::platform {
namespace {

struct MountState {
    Storage* storage{};
    std::filesystem::path root{};
    std::uint64_t generation{};
    std::size_t active_users{};
    bool closing{};
    std::condition_variable idle;
};
std::mutex g_mount_mutex;
MountState g_mount;

bool belongs(const std::filesystem::path& path, const std::filesystem::path& root) {
    // このマウントは暗号化専用Engineのセッション中だけ存
    // 在する。 Data・Scenario・Yado・Config
    // などの実行時ツリー全体はUserData.dataに入
    // るが、 移植済みEngineはData/SkinBase/Skin.xml
    // など作業ディレクトリ基準のパスをまだ多く使う。
    // これらを論理マウントのルート基準で解決し、実行フ
    // ァイルの隣の平文ディレクトリへ処理を流さない。
    if (path.empty()) return false;
    const auto normalized = path.is_absolute()
        ? path.lexically_normal() : (root / path).lexically_normal();
    const auto base = root.lexically_normal();
    if (normalized == base) return true;
    const auto relative = normalized.lexically_relative(base);
    if (relative.empty() || relative.is_absolute()) return false;
    for (const auto& part : relative)
        if (part == "..") return false;
    return true;
}

void release_mount_lease(std::uint64_t generation) noexcept {
    std::scoped_lock lock(g_mount_mutex);
    if (generation == 0 || generation != g_mount.generation ||
        g_mount.active_users == 0) return;
    --g_mount.active_users;
    if (g_mount.active_users == 0) g_mount.idle.notify_all();
}

class MountLease final {
public:
    MountLease() = default;
    MountLease(Storage* storage, std::filesystem::path root,
               std::uint64_t generation)
        : storage(storage), root(std::move(root)), generation_(generation) {}
    ~MountLease() { release(); }
    MountLease(const MountLease&) = delete;
    MountLease& operator=(const MountLease&) = delete;
    MountLease(MountLease&& other) noexcept
        : storage(other.storage), root(std::move(other.root)),
          generation_(other.generation_) {
        other.storage = nullptr;
        other.generation_ = 0;
    }
    MountLease& operator=(MountLease&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept {
        return storage != nullptr;
    }
    [[nodiscard]] std::uint64_t detach() noexcept {
        const auto result = generation_;
        generation_ = 0;
        return result;
    }
    Storage* storage{};
    std::filesystem::path root{};

private:
    void release() noexcept {
        release_mount_lease(generation_);
        generation_ = 0;
        storage = nullptr;
    }
    std::uint64_t generation_{};
};

MountLease lease_for(const std::filesystem::path& path) {
    std::scoped_lock lock(g_mount_mutex);
    if (!g_mount.storage || g_mount.closing || !belongs(path, g_mount.root))
        return {};
    ++g_mount.active_users;
    return {g_mount.storage, g_mount.root, g_mount.generation};
}

}  // namespace

ScopedUserStorageMount::ScopedUserStorageMount(
    Storage& storage, std::filesystem::path logical_root) {
    std::scoped_lock lock(g_mount_mutex);
    if (g_mount.storage) throw std::runtime_error("an encrypted UserData mount is already active");
    generation_ = ++g_mount.generation;
    g_mount.storage = &storage;
    g_mount.root = std::move(logical_root);
    g_mount.closing = false;
}

ScopedUserStorageMount::~ScopedUserStorageMount() {
    std::unique_lock lock(g_mount_mutex);
    if (g_mount.generation == generation_) {
        // 先に新規操作を止め、取得済みのすべての操作・トランザク
        // ションが終わるまで 参照中のStorageを保持する。
        g_mount.closing = true;
        g_mount.storage = nullptr;
        g_mount.idle.wait(lock, [] { return g_mount.active_users == 0; });
        g_mount.root.clear();
        g_mount.closing = false;
    }
}

ScopedMountedStorageTransaction::ScopedMountedStorageTransaction(
    const std::filesystem::path& path) {
    auto mount = lease_for(path);
    storage_ = mount.storage;
    if (storage_) {
        good_ = storage_->begin_transaction();
        if (!good_) error_ = storage_->last_error();
        active_ = good_;
        if (good_) mount_generation_ = mount.detach();
    }
}

ScopedMountedStorageTransaction::~ScopedMountedStorageTransaction() {
    if (active_ && storage_) storage_->rollback_transaction();
    release_mount();
}

bool ScopedMountedStorageTransaction::commit() {
    if (!good_) return false;
    if (!active_ || !storage_) return true;
    const bool result = storage_->commit_transaction();
    if (!result) {
        error_ = storage_->last_error();
        // 確定の失敗は、トランザクションの正常終了ではない。 マウン
        // ト解放後はデストラクターで戻せないため、解放前に巻き戻す。
        storage_->rollback_transaction();
    }
    active_ = false;
    good_ = result;
    release_mount();
    return result;
}

void ScopedMountedStorageTransaction::release_mount() noexcept {
    release_mount_lease(mount_generation_);
    mount_generation_ = 0;
    storage_ = nullptr;
}

bool is_mounted_user_path(const std::filesystem::path& path) {
    return static_cast<bool>(lease_for(path));
}

std::optional<StorageCapabilities> mounted_user_storage_capabilities(
    const std::filesystem::path& path) {
    const auto mount = lease_for(path);
    return mount.storage
        ? std::optional<StorageCapabilities>(mount.storage->capabilities())
        : std::nullopt;
}

std::optional<std::filesystem::path> resolve_mounted_user_path(
    const std::filesystem::path& path) {
    std::scoped_lock lock(g_mount_mutex);
    if (!g_mount.storage || path.empty()) return std::nullopt;
    const auto resolved = path.is_absolute()
        ? path.lexically_normal() : (g_mount.root / path).lexically_normal();
    return belongs(resolved, g_mount.root)
        ? std::optional<std::filesystem::path>(resolved) : std::nullopt;
}

std::optional<std::vector<std::byte>> read_mounted_user_file(
    const std::filesystem::path& path) {
    const auto mount = lease_for(path);
    return mount.storage ? mount.storage->read(path) : std::nullopt;
}

std::unique_ptr<StorageReadHandle> open_mounted_user_file(
    const std::filesystem::path& path) {
    auto mount = lease_for(path);
    if (!mount.storage) return nullptr;
    auto input = mount.storage->open_read(path);
    if (!input) return nullptr;
    // すべてのランダム読み込み中に既存の利用権を保持し、 アーカイブが入力を
    // 使っている間にアンマウントで暗号化の提供元が破棄されないようにする。
    class LeasedInput final : public StorageReadHandle {
    public:
        LeasedInput(MountLease lease, std::unique_ptr<StorageReadHandle> input)
            : lease_(std::move(lease)), input_(std::move(input)) {}
        std::size_t read(std::span<std::byte> destination) override { return input_->read(destination); }
        bool seek(std::uint64_t offset) override { return input_->seek(offset); }
        bool good() const noexcept override { return input_->good(); }
    private:
        MountLease lease_;
        std::unique_ptr<StorageReadHandle> input_;
    };
    return std::make_unique<LeasedInput>(std::move(mount), std::move(input));
}

std::unique_ptr<StorageWriteHandle> open_mounted_user_write_file(
    const std::filesystem::path& path, bool truncate, bool create_parent) {
    auto mount = lease_for(path);
    if (!mount.storage) return nullptr;
    auto output = mount.storage->open_write(path, truncate, create_parent);
    if (!output) return nullptr;
    class LeasedOutput final : public StorageWriteHandle {
    public:
        LeasedOutput(MountLease lease, std::unique_ptr<StorageWriteHandle> output)
            : lease_(std::move(lease)), output_(std::move(output)) {}
        bool write(std::span<const std::byte> bytes) override { return output_->write(bytes); }
        bool flush() override { return output_->flush(); }
        bool good() const noexcept override { return output_->good(); }
    private:
        MountLease lease_;
        std::unique_ptr<StorageWriteHandle> output_;
    };
    return std::make_unique<LeasedOutput>(std::move(mount), std::move(output));
}

std::optional<std::vector<std::byte>> read_mounted_user_file_prefix(
    const std::filesystem::path& path, std::size_t limit) {
    const auto mount = lease_for(path);
    if (!mount.storage) return std::nullopt;
    auto input = mount.storage->open_read(path);
    if (!input) return std::nullopt;
    std::vector<std::byte> result(limit);
    std::size_t used = 0;
    while (used < limit) {
        const auto count = input->read(std::span<std::byte>(result).subspan(used));
        if (!input->good()) return std::nullopt;
        if (!count) break;
        used += count;
    }
    result.resize(used);
    return result;
}

std::optional<StorageStat> stat_mounted_user_path(const std::filesystem::path& path, bool throw_on_error) {
    const auto mount = lease_for(path);
    if (!mount.storage) return std::nullopt;
    auto result = mount.storage->stat(path);
    if (throw_on_error && !mount.storage->last_error().empty())
        throw std::runtime_error(mount.storage->last_error());
    return result;
}

std::optional<std::vector<StorageEntry>> list_mounted_user_path(
    const std::filesystem::path& path, bool throw_on_error) {
    const auto mount = lease_for(path);
    if (!mount.storage) return std::nullopt;
    // ディレクトリの検証はStorage::listが行う。 先にstat()
    // も呼ぶと、暗号化UserDataの走査で同じ行を毎回二度復号してしまう。
    auto result = mount.storage->list(path);
    if (throw_on_error && !mount.storage->last_error().empty())
        throw std::runtime_error(mount.storage->last_error());
    return result;
}

bool write_mounted_user_file(const std::filesystem::path& path,
                             std::span<const std::byte> data,
                             bool atomic) {
    const auto mount = lease_for(path);
    if (!mount.storage) return false;
    return atomic ? mount.storage->write_atomic(path, data, true)
                  : mount.storage->write(path, data, true);
}

bool set_modified_mounted_user_path(const std::filesystem::path& path,
                                    std::int64_t timestamp) {
    const auto mount = lease_for(path);
    return mount.storage && mount.storage->set_modified(path, timestamp);
}

bool make_mounted_user_directory(const std::filesystem::path& path,
                                 bool recursive) {
    const auto mount = lease_for(path);
    return mount.storage && mount.storage->mkdir(path, recursive);
}

bool reserve_mounted_user_directory(const std::filesystem::path& path) {
    const auto mount = lease_for(path);
    return mount.storage && mount.storage->mkdir_exclusive(path);
}

bool remove_mounted_user_path(const std::filesystem::path& path,
                              bool recursive) {
    const auto mount = lease_for(path);
    return mount.storage && mount.storage->remove(path, recursive);
}

bool rename_mounted_user_path(const std::filesystem::path& source,
                              const std::filesystem::path& destination,
                              bool overwrite) {
    const auto mount = lease_for(source);
    return mount.storage && belongs(destination, mount.root) &&
           mount.storage->rename(source, destination, overwrite);
}

bool copy_mounted_user_path(const std::filesystem::path& source,
                            const std::filesystem::path& destination,
                            bool overwrite) {
    const auto mount = lease_for(source);
    return mount.storage && belongs(destination, mount.root) &&
           mount.storage->copy(source, destination, overwrite);
}

bool move_mounted_user_path(const std::filesystem::path& source,
                            const std::filesystem::path& destination,
                            bool overwrite) {
    const auto mount = lease_for(source);
    return mount.storage && belongs(destination, mount.root) &&
           mount.storage->move(source, destination, overwrite);
}

}  // namespace cardwirth::platform
