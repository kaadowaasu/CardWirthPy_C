#include "web_storage.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <system_error>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace cardwirth::platform {
namespace detail {

enum class WebStorageOperation : std::uint8_t {
    Load,
    Save,
};

struct WebStorageSharedState {
    mutable std::mutex mutex;
    WebStorageState state{WebStorageState::Booting};
    WebStorageError error_kind{WebStorageError::None};
    std::string error;
    bool quota_error{};
    bool mounted{};
    std::size_t retries{};
    WebStorageOperation operation{WebStorageOperation::Load};
};

struct WebStorageLifetime {
    bool alive{true};
};

struct MemoryNode {
    bool directory{};
    std::vector<std::byte> data;
    std::int64_t modified{};
};

struct WebStorageMemory {
    std::map<std::string, MemoryNode> nodes;
    std::size_t used_bytes{};
    std::int64_t clock{};
};

}  // namespace detail

namespace {

using detail::MemoryNode;
using detail::WebStorageMemory;

std::string slashify(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

std::optional<std::string> normalise_path(std::string value, bool absolute) {
    value = slashify(std::move(value));
    if (absolute && (value.empty() || value.front() != '/')) value.insert(value.begin(), '/');
    if (!absolute && !value.empty() && value.front() == '/') return std::nullopt;

    const auto is_reserved_device_name = [](std::string component) {
        std::transform(component.begin(), component.end(), component.begin(),
                       [](unsigned char character) {
                           if (character >= 'A' && character <= 'Z')
                               return static_cast<char>(character - 'A' + 'a');
                           return static_cast<char>(character);
                       });
        const auto dot = component.find('.');
        const auto stem = component.substr(0, dot);
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return true;
        if (stem.size() == 4 &&
            (stem.starts_with("com") || stem.starts_with("lpt")) &&
            stem.back() >= '1' && stem.back() <= '9')
            return true;
        return false;
    };

    const auto valid_component = [&](const std::string& component) {
        if (component.empty() || component == "." || component == "..") return true;
        if (component.back() == '.' || component.back() == ' ') return false;
        if (is_reserved_device_name(component)) return false;
        for (const auto character : component) {
            const auto value = static_cast<unsigned char>(character);
            if (value < 0x20U || character == '\0' ||
                std::string_view(":*?\"<>|").find(character) != std::string_view::npos)
                return false;
        }
        return true;
    };

    std::vector<std::string> components;
    std::size_t begin = absolute ? 1 : 0;
    while (begin <= value.size()) {
        const auto end = value.find('/', begin);
        const auto length = end == std::string::npos ? value.size() - begin : end - begin;
        const std::string component = value.substr(begin, length);
        if (!valid_component(component)) return std::nullopt;
        if (!component.empty() && component != ".") {
            if (component == "..") return std::nullopt;
            components.push_back(component);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }

    std::string result;
    if (absolute) result = "/";
    for (std::size_t index = 0; index < components.size(); ++index) {
        if (index != 0) result.push_back('/');
        result += components[index];
    }
    if (absolute && result.size() > 1 && result.back() == '/') result.pop_back();
    return result;
}

std::optional<std::string> normalise_root(const StoragePath& root) {
    auto value = slashify(root.generic_string());
    if (value.empty() || value == ".") value = "/user";
    const bool absolute = !value.empty() && value.front() == '/';
    if (!absolute) value.insert(value.begin(), '/');
    return normalise_path(std::move(value), true);
}

std::string join_key(const std::string& left, const std::string& right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    return left + '/' + right;
}

std::string parent_key(const std::string& key) {
    const auto separator = key.rfind('/');
    return separator == std::string::npos ? std::string{} : key.substr(0, separator);
}

bool is_descendant(const std::string& key, const std::string& parent) {
    if (parent.empty()) return !key.empty();
    return key.size() > parent.size() && key.compare(0, parent.size(), parent) == 0 &&
           key[parent.size()] == '/';
}

std::string path_text(const StoragePath& path) {
    return slashify(path.generic_string());
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        if (character >= 'A' && character <= 'Z')
            return static_cast<char>(character - 'A' + 'a');
        return static_cast<char>(character);
    });
    return value;
}

bool looks_like_quota(std::string_view message) {
    const auto lower = lower_copy(std::string(message));
    return lower.find("quota") != std::string::npos ||
           lower.find("enospc") != std::string::npos ||
           lower.find("no space") != std::string::npos ||
           lower.find("storage full") != std::string::npos ||
           lower.find("disk full") != std::string::npos ||
           lower.find("edquot") != std::string::npos;
}

std::uint64_t next_temporary_id() noexcept {
    static std::atomic<std::uint64_t> sequence{0};
    return ++sequence;
}

std::string error_text(const std::error_code& error) {
    return error ? error.message() : std::string("unknown I/O error");
}

bool looks_like_cross_device(std::string_view message) {
    const auto lower = lower_copy(std::string(message));
    return lower.find("cross-device") != std::string::npos ||
           lower.find("cross device") != std::string::npos ||
           lower.find("exdev") != std::string::npos ||
           lower.find("異なるデバイス") != std::string::npos;
}

StorageStat make_memory_stat(const MemoryNode& node) {
    StorageStat result;
    result.type = node.directory ? StorageEntryType::Directory : StorageEntryType::File;
    result.size = node.directory ? 0 : node.data.size();
    result.modified = node.modified;
    result.modified_valid = true;
    return result;
}

std::int64_t next_memory_timestamp(WebStorageMemory& memory) {
    const auto now = storage_timestamp_now();
    memory.clock = std::max(now, memory.clock + 1);
    return memory.clock;
}

#ifdef __EMSCRIPTEN__

// ブラウザー固有のJavaScriptは、この翻訳単位にまとめる。 C++
// 側には、マウントの結果と非同期同期処理の完了コードだけを見せる。
#if defined(CARDWIRTH_WEB_IO_SERVER) && CARDWIRTH_WEB_IO_SERVER

int cwxeditor_web_storage_mount_idbfs(const char* root) {
    return MAIN_THREAD_EM_ASM_INT({
        const path = UTF8ToString($0);
        try {
            if (Module['cardwirthVirtualStorageReady']) return 1;
            if (!FS || !FS.mount || !FS.syncfs) return -1;
            FS.mkdirTree(path);
            const existing = FS.lookupPath(path).node;
            if (existing && existing.mount) return 1;
            const filesystem = (typeof IDBFS !== 'undefined') ? IDBFS :
                (FS.filesystems && FS.filesystems.IDBFS);
            if (!filesystem) return -1;
            FS.mount(filesystem, {}, path);
            return 1;
        } catch (error) {
            const message = String((error && (error.name || error.message)) || error || '');
            const lower = message.toLowerCase();
            return (lower.indexOf('quota') >= 0 || lower.indexOf('no space') >= 0) ? 2 : -1;
        }
    }, root);
}

void cwxeditor_web_storage_syncfs(int populate, std::uintptr_t token) {
    MAIN_THREAD_ASYNC_EM_ASM({
        const token = $1;
        const invoke = function(code, message) {
            const callback = Module['_cwxeditor_web_storage_sync_complete'];
            if (typeof callback === 'function') {
                let pointer = 0;
                try {
                    if (message) pointer = stringToNewUTF8(String(message));
                    callback(token, code, pointer);
                } finally {
                    if (pointer) _free(pointer);
                }
                return;
            }
            if (typeof Module.ccall === 'function') {
                Module.ccall('cwxeditor_web_storage_sync_complete', 'void',
                    ['number', 'number', 'string'], [token, code, message || '']);
            }
        };
        try {
            if (Module['cardwirthVirtualStorageReady']) {
                const flush = Module['cardwirthFlushUserStorage'];
                Promise.resolve($0 || typeof flush !== 'function' ? null : flush())
                    .then(function() { invoke(0, ''); })
                    .catch(function(error) {
                        invoke(1, String((error && (error.message || error.name)) || error ||
                            'virtual storage flush failed'));
                    });
                return;
            }
            if (!FS || !FS.syncfs) {
                invoke(1, 'IDBFS syncfs is unavailable');
                return;
            }
            FS.syncfs(!!$0, function(error) {
                if (!error) {
                    invoke(0, '');
                    return;
                }
                const message = String((error && (error.name || error.message || error.code)) || error);
                const lower = message.toLowerCase();
                const quota = lower.indexOf('quota') >= 0 || lower.indexOf('no space') >= 0 ||
                    lower.indexOf('enospc') >= 0 || lower.indexOf('edquot') >= 0;
                invoke(quota ? 2 : 1, message);
            });
        } catch (error) {
            const message = String((error && (error.name || error.message)) || error ||
                'IDBFS sync failed');
            const lower = message.toLowerCase();
            invoke((lower.indexOf('quota') >= 0 || lower.indexOf('no space') >= 0) ? 2 : 1,
                message);
        }
    }, populate, token);
}

#else

EM_JS(int, cwxeditor_web_storage_mount_idbfs, (const char* root), {
    const path = UTF8ToString(root);
    try {
        // Web ALL はエンジン・エディタ・Explorerで共通の遅延ファイ
        // ルシステムを使い、すでに /user を管理する。 エディタはその上に
        // IDBFSをマウントしない。 保存の実装詳細はC++側から完全に隠す。
        if (Module['cardwirthVirtualStorageReady']) return 1;
        if (!FS || !FS.mount || !FS.syncfs) return -1;
        FS.mkdirTree(path);
        const existing = FS.lookupPath(path).node;
        if (existing && existing.mount) return 1;
        const filesystem = (typeof IDBFS !== 'undefined') ? IDBFS :
            (FS.filesystems && FS.filesystems.IDBFS);
        if (!filesystem) return -1;
        FS.mount(filesystem, {}, path);
        return 1;
    } catch (error) {
        const message = String((error && (error.name || error.message)) || error || '');
        const lower = message.toLowerCase();
        return (lower.indexOf('quota') >= 0 || lower.indexOf('no space') >= 0) ? 2 : -1;
    }
});

EM_JS(void, cwxeditor_web_storage_syncfs,
      (int populate, std::uintptr_t token), {
    const invoke = function(code, message) {
        const callback = Module['_cwxeditor_web_storage_sync_complete'];
        if (typeof callback === 'function') {
            // 旧Asyncify構成も、Pthreadの入出力サーバー構成と同じC
            // ABIを使う。 C++へはUTF-8のポインターを渡し、JavaScript
            // 文字列をWebAssemblyの整数引数へ暗黙変換しない。
            let pointer = 0;
            try {
                if (message) pointer = stringToNewUTF8(String(message));
                callback(token, code, pointer);
            } finally {
                if (pointer) _free(pointer);
            }
            return;
        }
        // モジュール化した構成によっては、アンダースコア付きの直接エクスポートがなく
        // ccall だけがある。 この代替経路でも同じコールバックの規則を維持する。
        if (typeof Module.ccall === 'function') {
            Module.ccall('cwxeditor_web_storage_sync_complete', 'void',
                ['number', 'number', 'string'], [token, code, message || '']);
        }
    };
    try {
        if (Module['cardwirthVirtualStorageReady']) {
            const flush = Module['cardwirthFlushUserStorage'];
            Promise.resolve(populate || typeof flush !== 'function' ? null : flush())
                .then(function() { invoke(0, ''); })
                .catch(function(error) {
                    invoke(1, String((error && (error.message || error.name)) || error ||
                        'virtual storage flush failed'));
                });
            return;
        }
        if (!FS || !FS.syncfs) {
            invoke(1, 'IDBFS syncfs is unavailable');
            return;
        }
        FS.syncfs(!!populate, function(error) {
            if (!error) {
                invoke(0, '');
                return;
            }
            const message = String((error && (error.name || error.message || error.code)) || error);
            const lower = message.toLowerCase();
            const quota = lower.indexOf('quota') >= 0 || lower.indexOf('no space') >= 0 ||
                lower.indexOf('enospc') >= 0 || lower.indexOf('edquot') >= 0;
            invoke(quota ? 2 : 1, message);
        });
    } catch (error) {
        const message = String((error && (error.name || error.message)) || error || 'IDBFS sync failed');
        const lower = message.toLowerCase();
        invoke((lower.indexOf('quota') >= 0 || lower.indexOf('no space') >= 0) ? 2 : 1,
            message);
    }
});

#endif

struct SyncContext {
    std::shared_ptr<detail::WebStorageSharedState> shared;
    detail::WebStorageOperation operation{detail::WebStorageOperation::Load};
};

extern "C" EMSCRIPTEN_KEEPALIVE void cwxeditor_web_storage_sync_complete(
    std::uintptr_t token, int result, const char* message) noexcept {
    auto* context = reinterpret_cast<SyncContext*>(token);
    if (context == nullptr) return;
    auto shared = std::move(context->shared);
    const auto operation = context->operation;
    delete context;

    try {
        std::lock_guard lock(shared->mutex);
        shared->operation = operation;
        if (result == 0) {
            shared->state = WebStorageState::Ready;
            shared->error_kind = WebStorageError::None;
            shared->error.clear();
            shared->quota_error = false;
        } else {
            shared->state = WebStorageState::StorageError;
            shared->error_kind = result == 2 ? WebStorageError::QuotaExceeded
                                             : WebStorageError::SyncFailed;
            shared->quota_error = result == 2;
            shared->error = message != nullptr && *message != '\0'
                ? std::string(message)
                : (result == 2 ? std::string("IndexedDBの容量が不足しています")
                               : std::string("IndexedDBの同期に失敗しました"));
        }
    } catch (...) {
        // 診断用の確保失敗を、JavaScriptのコールバックの外へ伝えない
        // 。 安全側の観測可能な状態として StorageError を残す。
        try {
            std::lock_guard lock(shared->mutex);
            shared->state = WebStorageState::StorageError;
            shared->error_kind = WebStorageError::SyncFailed;
            shared->quota_error = false;
            shared->error = "IndexedDBの同期に失敗しました";
        } catch (...) {
        }
    }
}

#endif  // __EMSCRIPTEN__

class MemoryReadHandle final : public StorageReadHandle {
public:
    explicit MemoryReadHandle(std::vector<std::byte> data) : data_(std::move(data)) {}

    bool seek(std::uint64_t offset) override {
        if (!good_ || offset > data_.size()) return false;
        offset_ = static_cast<std::size_t>(offset);
        return true;
    }

    std::size_t read(std::span<std::byte> destination) override {
        if (!good_ || destination.empty()) return 0;
        const auto remaining = data_.size() - offset_;
        const auto count = std::min(remaining, destination.size());
        if (count != 0)
            std::copy_n(data_.data() + offset_, count, destination.data());
        offset_ += count;
        return count;
    }

    [[nodiscard]] bool good() const noexcept override { return good_; }

private:
    std::vector<std::byte> data_;
    std::size_t offset_{};
    bool good_{true};
};

class MemoryWriteHandle final : public StorageWriteHandle {
public:
    MemoryWriteHandle(WebStorage* owner,
                      std::shared_ptr<detail::WebStorageLifetime> lifetime,
                      StoragePath path,
                      std::vector<std::byte> data)
        : owner_(owner), lifetime_(std::move(lifetime)), path_(std::move(path)),
          data_(std::move(data)) {}

    ~MemoryWriteHandle() override {
        if (good_ && !flushed_) (void)flush();
    }

    bool write(std::span<const std::byte> source) override {
        if (!good_) return false;
        if (source.size() > std::numeric_limits<std::size_t>::max() - data_.size()) {
            good_ = false;
            return false;
        }
        data_.insert(data_.end(), source.begin(), source.end());
        flushed_ = false;
        return true;
    }

    bool flush() override {
        if (!good_ || owner_ == nullptr || lifetime_ == nullptr || !lifetime_->alive) {
            good_ = false;
            return false;
        }
        flushed_ = true;
        if (!owner_->write(path_, std::span<const std::byte>(data_.data(), data_.size()))) {
            good_ = false;
            return false;
        }
        return true;
    }

    [[nodiscard]] bool good() const noexcept override { return good_; }

private:
    WebStorage* owner_{};
    std::shared_ptr<detail::WebStorageLifetime> lifetime_;
    StoragePath path_;
    std::vector<std::byte> data_;
    bool flushed_{};
    bool good_{true};
};

#ifdef __EMSCRIPTEN__

class FileReadHandle final : public StorageReadHandle {
public:
    explicit FileReadHandle(const std::filesystem::path& path)
        : stream_(path, std::ios::binary) {}

    bool seek(std::uint64_t offset) override {
        if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) return false;
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        return static_cast<bool>(stream_);
    }

    std::size_t read(std::span<std::byte> destination) override {
        if (!stream_ || destination.empty()) return 0;
        stream_.read(reinterpret_cast<char*>(destination.data()),
                     static_cast<std::streamsize>(destination.size()));
        return static_cast<std::size_t>(stream_.gcount());
    }

    [[nodiscard]] bool good() const noexcept override {
        return stream_.good() || stream_.eof();
    }

private:
    std::ifstream stream_;
};

class FileWriteHandle final : public StorageWriteHandle {
public:
    FileWriteHandle(const std::filesystem::path& path, bool truncate)
        : stream_(path, std::ios::binary | (truncate ? std::ios::trunc : std::ios::app)) {}

    bool write(std::span<const std::byte> source) override {
        if (!stream_) return false;
        stream_.write(reinterpret_cast<const char*>(source.data()),
                      static_cast<std::streamsize>(source.size()));
        return static_cast<bool>(stream_);
    }

    bool flush() override {
        stream_.flush();
        return static_cast<bool>(stream_);
    }

    [[nodiscard]] bool good() const noexcept override { return static_cast<bool>(stream_); }

private:
    std::ofstream stream_;
};

StorageStat make_filesystem_stat(const std::filesystem::path& path,
                                 const std::filesystem::file_status& status) {
    StorageStat result;
    if (std::filesystem::is_regular_file(status)) {
        result.type = StorageEntryType::File;
        std::error_code error;
        result.size = std::filesystem::file_size(path, error);
        if (error) result.size = 0;
    } else if (std::filesystem::is_directory(status)) {
        result.type = StorageEntryType::Directory;
    } else if (std::filesystem::is_symlink(status)) {
        result.type = StorageEntryType::Symlink;
    } else if (status.type() != std::filesystem::file_type::none) {
        result.type = StorageEntryType::Other;
    }
    if (result.type == StorageEntryType::Symlink || result.type == StorageEntryType::Other) {
        // MSVCはジャンクションをOtherと返すため、対象がディレクトリかだ
        // けをファイルシステムへ問い合わせる。 走査の方針は共通のままにする。
        std::error_code target_error;
        result.directory_link = std::filesystem::is_directory(path, target_error);
    }
    std::error_code error;
    result.modified = storage_timestamp_from_file_time(
        std::filesystem::last_write_time(path, error));
    result.modified_valid = !error;
    if (error) result.modified = 0;
    return result;
}

#endif  // __EMSCRIPTEN__

bool ensure_memory_parent(WebStorageMemory& memory,
                          const std::string& parent,
                          bool recursive,
                          std::string& error) {
    if (parent.empty()) return true;
    if (!recursive) {
        const auto found = memory.nodes.find(parent);
        if (found == memory.nodes.end() || !found->second.directory) {
            error = "親ディレクトリがありません: " + parent;
            return false;
        }
        return true;
    }

    std::string current;
    std::size_t begin = 0;
    while (begin <= parent.size()) {
        const auto end = parent.find('/', begin);
        const auto component = parent.substr(
            begin, end == std::string::npos ? parent.size() - begin : end - begin);
        if (!component.empty()) {
            current = join_key(current, component);
            const auto found = memory.nodes.find(current);
            if (found == memory.nodes.end()) {
                memory.nodes.emplace(current, MemoryNode{true, {},
                                                          next_memory_timestamp(memory)});
            } else if (!found->second.directory) {
                error = "親パスがファイルです: " + current;
                return false;
            }
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return true;
}

bool memory_has_children(const WebStorageMemory& memory, const std::string& key) {
    const auto prefix = key.empty() ? std::string{} : key + '/';
    for (const auto& [candidate, node] : memory.nodes) {
        (void)node;
        if (candidate.size() > prefix.size() &&
            candidate.compare(0, prefix.size(), prefix) == 0)
            return true;
    }
    return false;
}

std::size_t memory_subtree_bytes(const WebStorageMemory& memory, const std::string& key) {
    std::size_t total = 0;
    const auto prefix = key.empty() ? std::string{} : key + '/';
    for (const auto& [candidate, node] : memory.nodes) {
        if (candidate != key &&
            (candidate.size() <= prefix.size() ||
             candidate.compare(0, prefix.size(), prefix) != 0))
            continue;
        if (node.directory || node.data.size() > std::numeric_limits<std::size_t>::max() - total)
            continue;
        total += node.data.size();
    }
    return total;
}

void memory_remove_subtree(WebStorageMemory& memory, const std::string& key) {
    const auto first = memory.nodes.find(key);
    if (first != memory.nodes.end()) {
        if (!first->second.directory) memory.used_bytes -= first->second.data.size();
        memory.nodes.erase(first);
    }
    const auto prefix = key.empty() ? std::string{} : key + '/';
    auto iterator = memory.nodes.lower_bound(prefix);
    while (iterator != memory.nodes.end() &&
           iterator->first.compare(0, prefix.size(), prefix) == 0) {
        if (!iterator->second.directory) memory.used_bytes -= iterator->second.data.size();
        iterator = memory.nodes.erase(iterator);
    }
}

}  // namespace

WebStorage::WebStorage() : WebStorage(WebStorageOptions{}) {}

WebStorage::WebStorage(WebStorageOptions options)
    : WebStorage(options.root, options) {}

WebStorage::WebStorage(StoragePath root, WebStorageOptions options)
    : root_(root.empty() ? StoragePath("/user") : std::move(root)),
      options_(std::move(options)),
      shared_(std::make_shared<detail::WebStorageSharedState>()),
      lifetime_(std::make_shared<detail::WebStorageLifetime>()),
      memory_(std::make_shared<detail::WebStorageMemory>()) {
    try {
        const auto normalised = normalise_root(root_);
        if (!normalised) {
            root_valid_ = false;
            root_.clear();
        } else {
            root_ = StoragePath(*normalised);
        }
    } catch (...) {
        root_valid_ = false;
        root_.clear();
    }
    options_.root = root_;
    memory_->clock = storage_timestamp_now();
    memory_->nodes.emplace(std::string{}, MemoryNode{true, {}, memory_->clock});
#ifdef __EMSCRIPTEN__
    memory_backend_ = !options_.use_idbfs;
#else
    memory_backend_ = true;
#endif
    if (!root_valid_) {
        std::lock_guard lock(shared_->mutex);
        shared_->state = WebStorageState::StorageError;
        shared_->error_kind = WebStorageError::InvalidPath;
        shared_->error = "Storage rootが不正です";
    } else if (options_.auto_start) {
        (void)start();
    }
}

WebStorage::~WebStorage() {
    if (lifetime_ != nullptr) lifetime_->alive = false;
}

WebStorageState WebStorage::state() const noexcept {
    if (shared_ == nullptr) return WebStorageState::StorageError;
    std::lock_guard lock(shared_->mutex);
    return shared_->state;
}

bool WebStorage::ready() const noexcept { return state() == WebStorageState::Ready; }

bool WebStorage::mounted() const noexcept {
    if (shared_ == nullptr) return false;
    std::lock_guard lock(shared_->mutex);
    return shared_->mounted;
}

bool WebStorage::using_idbfs() const noexcept {
#ifdef __EMSCRIPTEN__
    return !memory_backend_ && options_.use_idbfs;
#else
    return false;
#endif
}

bool WebStorage::capacity_error() const noexcept {
    if (shared_ == nullptr) return false;
    std::lock_guard lock(shared_->mutex);
    return shared_->quota_error;
}

WebStorageError WebStorage::error_kind() const noexcept {
    if (shared_ == nullptr) return WebStorageError::IoError;
    std::lock_guard lock(shared_->mutex);
    return shared_->error_kind;
}

std::size_t WebStorage::retry_count() const noexcept {
    if (shared_ == nullptr) return 0;
    std::lock_guard lock(shared_->mutex);
    return shared_->retries;
}

std::size_t WebStorage::bytes_used() const noexcept {
    if (memory_backend_ && memory_ != nullptr) return memory_->used_bytes;
#ifdef __EMSCRIPTEN__
    std::size_t total = 0;
    try {
        std::error_code error;
        if (!std::filesystem::is_directory(root_, error) || error) return 0;
        for (std::filesystem::recursive_directory_iterator iterator(
                 root_, std::filesystem::directory_options::skip_permission_denied, error),
             end;
             iterator != end; iterator.increment(error)) {
            if (error) break;
            if (!iterator->is_regular_file(error) || error) continue;
            const auto size = iterator->file_size(error);
            if (error || size > std::numeric_limits<std::size_t>::max() - total) return total;
            total += static_cast<std::size_t>(size);
        }
    } catch (...) {
    }
    return total;
#else
    return 0;
#endif
}

std::size_t WebStorage::memory_limit() const noexcept { return options_.max_memory_bytes; }

void WebStorage::clear_error() const {
    if (shared_ == nullptr) return;
    std::lock_guard lock(shared_->mutex);
    shared_->error.clear();
    shared_->error_kind = WebStorageError::None;
    shared_->quota_error = false;
}

void WebStorage::set_error(std::string message, WebStorageError kind) const {
    if (shared_ == nullptr) return;
    std::lock_guard lock(shared_->mutex);
    shared_->error = std::move(message);
    shared_->error_kind = kind;
    shared_->quota_error = kind == WebStorageError::QuotaExceeded;
}

bool WebStorage::fail(std::string message, WebStorageError kind) const {
    set_error(std::move(message), kind);
    return false;
}

bool WebStorage::fail_quota(std::string message) const {
    if (shared_ != nullptr) {
        std::lock_guard lock(shared_->mutex);
        shared_->state = WebStorageState::StorageError;
        shared_->operation = detail::WebStorageOperation::Save;
        shared_->error = std::move(message);
        shared_->error_kind = WebStorageError::QuotaExceeded;
        shared_->quota_error = true;
    }
    return false;
}

std::optional<std::string> WebStorage::key_for(const StoragePath& path) const {
    try {
        if (!root_valid_) {
            (void)fail("Storage rootが不正です", WebStorageError::InvalidPath);
            return std::nullopt;
        }
        const auto raw = path_text(path);
        if (raw.empty() || raw == ".") return std::string{};

        const auto root = normalise_root(root_);
        if (!root) {
            (void)fail("Storage rootが不正です", WebStorageError::InvalidPath);
            return std::nullopt;
        }

        if (!raw.empty() && raw.front() == '/') {
            const auto absolute = normalise_path(raw, true);
            if (!absolute) {
                (void)fail("Storage pathが不正です: " + raw, WebStorageError::InvalidPath);
                return std::nullopt;
            }
            if (*absolute == *root) return std::string{};
            const auto prefix = *root + '/';
            if (absolute->compare(0, prefix.size(), prefix) != 0) {
                (void)fail("Storage pathがrootの外側を指しています: " + raw,
                           WebStorageError::InvalidPath);
                return std::nullopt;
            }
            return absolute->substr(prefix.size());
        }

        const auto relative = normalise_path(raw, false);
        if (!relative) {
            (void)fail("Storage pathが不正です: " + raw, WebStorageError::InvalidPath);
            return std::nullopt;
        }
        return *relative;
    } catch (const std::exception& error) {
        (void)fail(std::string("Storage pathが不正です: ") + error.what(),
                   WebStorageError::InvalidPath);
        return std::nullopt;
    } catch (...) {
        (void)fail("Storage pathが不正です", WebStorageError::InvalidPath);
        return std::nullopt;
    }
}

StoragePath WebStorage::path_for_key(const std::string& key) const {
    if (key.empty()) return root_;
    return root_ / StoragePath(key);
}

bool WebStorage::ready_for_io() const {
    if (!root_valid_)
        return fail("Storage rootが不正です", WebStorageError::InvalidPath);
    if (state() == WebStorageState::Ready) return true;
    return fail("StorageがReadyではありません", WebStorageError::NotReady);
}

bool WebStorage::mount_idbfs() {
    clear_error();
    if (!root_valid_)
        return fail("Storage rootが不正です", WebStorageError::InvalidPath);
    if (memory_backend_) {
        if (shared_ != nullptr) {
            std::lock_guard lock(shared_->mutex);
            shared_->mounted = true;
        }
        return true;
    }
#ifdef __EMSCRIPTEN__
    const auto root = path_text(root_);
    const auto result = cwxeditor_web_storage_mount_idbfs(root.c_str());
    if (result == 1) {
        if (shared_ != nullptr) {
            std::lock_guard lock(shared_->mutex);
            shared_->mounted = true;
        }
        return true;
    }
    if (options_.fallback_to_memory) {
        memory_backend_ = true;
        if (shared_ != nullptr) {
            std::lock_guard lock(shared_->mutex);
            shared_->mounted = true;
        }
        return true;
    }
    if (result == 2) return fail_quota("IndexedDBの容量が不足しているためマウントできません");
    if (shared_ != nullptr) {
        std::lock_guard lock(shared_->mutex);
        shared_->state = WebStorageState::StorageError;
    }
    return fail("IDBFSをマウントできません", WebStorageError::MountFailed);
#else
    return fail("IDBFSはEmscripten以外では利用できません", WebStorageError::MountFailed);
#endif
}

bool WebStorage::start() {
    if (!root_valid_)
        return fail("Storage rootが不正です", WebStorageError::InvalidPath);
    const auto current = state();
    if (current == WebStorageState::Ready || current == WebStorageState::LoadingStorage ||
        current == WebStorageState::SavingStorage)
        return true;

    clear_error();
    if (shared_ != nullptr) {
        std::lock_guard lock(shared_->mutex);
        shared_->state = WebStorageState::Booting;
        shared_->operation = detail::WebStorageOperation::Load;
        shared_->retries = 0;
    }
    if (memory_backend_) {
        if (shared_ != nullptr) {
            std::lock_guard lock(shared_->mutex);
            shared_->mounted = true;
            shared_->state = WebStorageState::Ready;
        }
        return true;
    }
    if (!mount_idbfs()) return false;
    return begin_sync(true);
}

bool WebStorage::begin_load() {
    if (!root_valid_)
        return fail("Storage rootが不正です", WebStorageError::InvalidPath);
    if (memory_backend_) {
        if (shared_ != nullptr) {
            std::lock_guard lock(shared_->mutex);
            shared_->state = WebStorageState::Ready;
            shared_->error.clear();
            shared_->error_kind = WebStorageError::None;
            shared_->quota_error = false;
        }
        return true;
    }
    if (!mounted() && !mount_idbfs()) return false;
    return begin_sync(true);
}

bool WebStorage::begin_save() { return begin_sync(false); }

bool WebStorage::sync() {
    if (!ready_for_io()) return false;
    if (memory_backend_) return true;
    std::error_code error;
    if (!sync_storage_directory(root_, error))
        return fail("UserDataの永続保存に失敗しました: " + error.message(), WebStorageError::IoError);
    return true;
}

bool WebStorage::sync_from_persistent() { return begin_load(); }

bool WebStorage::sync_to_persistent() { return begin_save(); }

bool WebStorage::retry() {
    const auto current = state();
    if (current == WebStorageState::Ready) return true;
    if (current != WebStorageState::StorageError) return false;

    detail::WebStorageOperation operation = detail::WebStorageOperation::Load;
    std::size_t attempt = 0;
    if (shared_ != nullptr) {
        std::lock_guard lock(shared_->mutex);
        operation = shared_->operation;
        attempt = shared_->retries + 1;
        if (options_.max_retries != 0 && attempt > options_.max_retries) {
            shared_->error = "IndexedDB同期の再試行回数を超えました";
            shared_->error_kind = WebStorageError::SyncFailed;
            shared_->quota_error = false;
            return false;
        }
        shared_->retries = attempt;
    }
    if (operation == detail::WebStorageOperation::Save) return begin_sync(false, true);
    return begin_sync(true, true);
}

void WebStorage::poll() noexcept {}

bool WebStorage::begin_sync(bool populate, bool from_retry) {
    if (!root_valid_)
        return fail("Storage rootが不正です", WebStorageError::InvalidPath);
    if (memory_backend_) {
        if (!populate && state() != WebStorageState::Ready && !from_retry)
            return fail("StorageがReadyではありません", WebStorageError::NotReady);
        if (shared_ != nullptr) {
            std::lock_guard lock(shared_->mutex);
            shared_->state = WebStorageState::Ready;
            shared_->operation = populate ? detail::WebStorageOperation::Load
                                           : detail::WebStorageOperation::Save;
            shared_->error.clear();
            shared_->error_kind = WebStorageError::None;
            shared_->quota_error = false;
        }
        return true;
    }

#ifndef __EMSCRIPTEN__
    return fail("WebStorageの永続化backendがありません", WebStorageError::MountFailed);
#else
    {
        if (shared_ == nullptr) return false;
        std::lock_guard lock(shared_->mutex);
        if (populate) {
            if (shared_->state == WebStorageState::LoadingStorage) return true;
            if (shared_->state == WebStorageState::SavingStorage && !from_retry) {
                shared_->error = "保存中は読み込みできません";
                shared_->error_kind = WebStorageError::NotReady;
                return false;
            }
            shared_->state = WebStorageState::LoadingStorage;
            shared_->operation = detail::WebStorageOperation::Load;
        } else {
            if (shared_->state == WebStorageState::SavingStorage) return true;
            if (shared_->state != WebStorageState::Ready && !from_retry) {
                shared_->error = "StorageがReadyではありません";
                shared_->error_kind = WebStorageError::NotReady;
                return false;
            }
            shared_->state = WebStorageState::SavingStorage;
            shared_->operation = detail::WebStorageOperation::Save;
        }
        shared_->error.clear();
        shared_->error_kind = WebStorageError::None;
        shared_->quota_error = false;
    }

    auto* context = new (std::nothrow) SyncContext{
        shared_, populate ? detail::WebStorageOperation::Load : detail::WebStorageOperation::Save};
    if (context == nullptr) {
        if (shared_ != nullptr) {
            std::lock_guard lock(shared_->mutex);
            shared_->state = WebStorageState::StorageError;
            shared_->error_kind = WebStorageError::SyncFailed;
            shared_->error = "IndexedDB同期コンテキストを作成できません";
        }
        return false;
    }
    cwxeditor_web_storage_syncfs(populate ? 1 : 0,
                                 reinterpret_cast<std::uintptr_t>(context));
    return true;
#endif
}

bool WebStorage::exists(const StoragePath& path) const {
    clear_error();
    if (!ready_for_io()) return false;
    const auto key = key_for(path);
    if (!key) return false;
    if (memory_backend_) return memory_->nodes.find(*key) != memory_->nodes.end();
#ifdef __EMSCRIPTEN__
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path_for_key(*key), error);
    if (error && error != std::errc::no_such_file_or_directory) {
        (void)fail("exists: " + error_text(error));
        return false;
    }
    return std::filesystem::exists(status);
#else
    return false;
#endif
}

std::optional<StorageStat> WebStorage::stat(const StoragePath& path) const {
    clear_error();
    if (!ready_for_io()) return std::nullopt;
    const auto key = key_for(path);
    if (!key) return std::nullopt;
    if (memory_backend_) {
        const auto found = memory_->nodes.find(*key);
        if (found == memory_->nodes.end()) return std::nullopt;
        return make_memory_stat(found->second);
    }
#ifdef __EMSCRIPTEN__
    std::error_code error;
    const auto resolved = path_for_key(*key);
    const auto status = std::filesystem::symlink_status(resolved, error);
    if (error == std::errc::no_such_file_or_directory) return std::nullopt;
    if (error) {
        (void)fail("stat: " + error_text(error));
        return std::nullopt;
    }
    return make_filesystem_stat(resolved, status);
#else
    return std::nullopt;
#endif
}

std::vector<StorageEntry> WebStorage::list(const StoragePath& directory) const {
    clear_error();
    if (!ready_for_io()) return {};
    const auto key = key_for(directory);
    if (!key) return {};
    if (memory_backend_) {
        const auto found = memory_->nodes.find(*key);
        if (found == memory_->nodes.end() || !found->second.directory) {
            (void)fail("list: ディレクトリではありません", WebStorageError::IoError);
            return {};
        }
        const auto prefix = key->empty() ? std::string{} : *key + '/';
        std::map<std::string, StorageEntry> entries;
        for (const auto& [candidate, node] : memory_->nodes) {
            if (candidate.empty() || candidate.size() <= prefix.size() ||
                candidate.compare(0, prefix.size(), prefix) != 0)
                continue;
            const auto rest = candidate.substr(prefix.size());
            if (rest.find('/') != std::string::npos) continue;
            StorageEntry entry;
            entry.name = rest;
            entry.path = path_for_key(join_key(*key, rest));
            entry.stat = make_memory_stat(node);
            entries.emplace(entry.name, std::move(entry));
        }
        std::vector<StorageEntry> result;
        result.reserve(entries.size());
        for (auto& [name, entry] : entries) {
            (void)name;
            result.push_back(std::move(entry));
        }
        return result;
    }
#ifdef __EMSCRIPTEN__
    const auto resolved = path_for_key(*key);
    std::error_code error;
    if (!std::filesystem::is_directory(resolved, error) || error) {
        (void)fail("list: " + (error ? error_text(error) : std::string("not a directory")));
        return {};
    }
    std::vector<StorageEntry> result;
    for (std::filesystem::directory_iterator iterator(
             resolved, std::filesystem::directory_options::skip_permission_denied, error),
         end;
         iterator != end; iterator.increment(error)) {
        if (error) {
            (void)fail("list: " + error_text(error));
            return {};
        }
        const auto status = iterator->symlink_status(error);
        if (error) continue;
        const auto name_path = iterator->path().filename();
        StorageEntry entry;
        entry.name = name_path.generic_string();
        entry.path = path_for_key(join_key(*key, entry.name));
        entry.stat = make_filesystem_stat(iterator->path(), status);
        result.push_back(std::move(entry));
    }
    std::sort(result.begin(), result.end(), [](const StorageEntry& left,
                                               const StorageEntry& right) {
        return left.name < right.name;
    });
    return result;
#else
    return {};
#endif
}

std::optional<std::vector<std::byte>> WebStorage::read(const StoragePath& path) const {
    clear_error();
    if (!ready_for_io()) return std::nullopt;
    const auto key = key_for(path);
    if (!key) return std::nullopt;
    if (memory_backend_) {
        const auto found = memory_->nodes.find(*key);
        if (found == memory_->nodes.end() || found->second.directory) {
            (void)fail("read: ファイルがありません", WebStorageError::IoError);
            return std::nullopt;
        }
        return found->second.data;
    }
#ifdef __EMSCRIPTEN__
    const auto resolved = path_for_key(*key);
    std::error_code error;
    const auto size = std::filesystem::file_size(resolved, error);
    if (error) {
        (void)fail("read: " + error_text(error));
        return std::nullopt;
    }
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        (void)fail("read: ファイルが大きすぎます");
        return std::nullopt;
    }
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    std::ifstream input(resolved, std::ios::binary);
    if (!input) {
        (void)fail("read: ファイルを開けません: " + path_text(path));
        return std::nullopt;
    }
    if (!result.empty()) {
        input.read(reinterpret_cast<char*>(result.data()),
                   static_cast<std::streamsize>(result.size()));
        if (!input) {
            (void)fail("read: ファイルを読み込めません: " + path_text(path));
            return std::nullopt;
        }
    }
    return result;
#else
    return std::nullopt;
#endif
}

bool WebStorage::write(const StoragePath& path,
                       std::span<const std::byte> data,
                       bool create_parent) {
    clear_error();
    if (!ready_for_io()) return false;
    const auto key = key_for(path);
    if (!key) return false;
    if (key->empty()) return fail("write: Storage rootには書き込めません");
    if (memory_backend_) {
        auto found = memory_->nodes.find(*key);
        if (found != memory_->nodes.end() && found->second.directory)
            return fail("write: ディレクトリには書き込めません");
        std::string parent_error;
        if (!ensure_memory_parent(*memory_, parent_key(*key), create_parent, parent_error))
            return fail("write: " + parent_error);
        const auto old_size = found == memory_->nodes.end() ? 0 : found->second.data.size();
        const auto used_without_old = memory_->used_bytes - old_size;
        if (options_.max_memory_bytes != 0 &&
            (data.size() > options_.max_memory_bytes ||
             used_without_old > options_.max_memory_bytes - data.size()))
            return fail_quota("Storageの容量が不足しています");
        MemoryNode node{false, std::vector<std::byte>(data.begin(), data.end()),
                        next_memory_timestamp(*memory_)};
        if (found != memory_->nodes.end()) {
            found->second = std::move(node);
        } else {
            memory_->nodes.emplace(*key, std::move(node));
        }
        memory_->used_bytes = used_without_old + data.size();
        return true;
    }
#ifdef __EMSCRIPTEN__
    const auto resolved = path_for_key(*key);
    // 先に兄弟の一時ファイルへ書く。IDBFSが永続化する
    // のは syncfs 後なので、 特に容量制限などの書
    // き込み失敗でも、以前のエディタ文書を残す必要がある。
    auto temporary = resolved;
    temporary += ".cwxeditor-write-" + std::to_string(next_temporary_id()) + ".tmp";
    std::error_code error;
    if (create_parent && !resolved.parent_path().empty()) {
        std::filesystem::create_directories(resolved.parent_path(), error);
        if (error) {
            if (looks_like_quota(error_text(error))) return fail_quota("Storageの容量が不足しています");
            return fail("write: " + error_text(error));
        }
    }
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        const auto message = std::string("write: ファイルを開けません: ") + path_text(path);
        if (looks_like_quota(message) || errno == ENOSPC || errno == EDQUOT)
            return fail_quota("Storageの容量が不足しています");
        return fail(message);
    }
    if (!data.empty())
        output.write(reinterpret_cast<const char*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
    if (!output) {
        output.close();
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        const auto message = std::string("write: ファイルを書き込めません: ") + path_text(path);
        if (looks_like_quota(message) || errno == ENOSPC || errno == EDQUOT)
            return fail_quota("Storageの容量が不足しています");
        return fail(message);
    }
    output.close();
    if (!output) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        return fail("write: 一時ファイルを閉じられません");
    }
    std::filesystem::rename(temporary, resolved, error);
    if (error) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        if (looks_like_quota(error_text(error))) return fail_quota("Storageの容量が不足しています");
        return fail("write: 原子的な置換に失敗しました: " + error_text(error));
    }
    return true;
#else
    return fail("WebStorageの永続化backendがありません", WebStorageError::MountFailed);
#endif
}

bool WebStorage::write_atomic(const StoragePath& path,
                              std::span<const std::byte> data,
                              const bool create_parent) {
    // write() の両実装は、すでに完全な値を置き換える。
    // メモリ版は構築済みのバイトベクターを交換し、 IDBFS
    // 版は兄弟の一時ファイルへ書いてから保存先へ改名する。
    return write(path, data, create_parent);
}

bool WebStorage::set_modified(const StoragePath& path, std::int64_t timestamp) {
    clear_error();
    if (!ready_for_io()) return false;
    const auto key = key_for(path);
    if (!key) return false;
    if (memory_backend_) {
        const auto found = memory_->nodes.find(*key);
        if (found == memory_->nodes.end()) return fail("set_modified: path does not exist");
        found->second.modified = timestamp;
        return true;
    }
#ifdef __EMSCRIPTEN__
    // IDBFSは既存のファイルシステム情報を syncfs で保存する。
    // 日時の変更で、ファイルの再取得・再保存や別の保存領域の作成を行わない。
    std::error_code error;
    // この設定関数は既知の日時を受け取り、epochのゼロも有効とする
    // 。 別の旧式のoptional変換では、ゼロを不明として使う。
    const auto value = timestamp == 0 ? std::optional<std::filesystem::file_time_type>(
        storage_file_time_from_system(StorageSystemFileTime{})) : storage_timestamp_to_file_time(timestamp);
    if (!value) return fail("set_modified: timestamp is outside the file clock range");
    std::filesystem::last_write_time(path_for_key(*key), *value, error);
    return error ? fail("set_modified: " + error_text(error)) : true;
#else
    return fail("WebStorageの永続化backendがありません", WebStorageError::MountFailed);
#endif
}

bool WebStorage::mkdir(const StoragePath& path, bool recursive) {
    return mkdir_impl(path, recursive, false);
}

bool WebStorage::mkdir_exclusive(const StoragePath& path) {
    return mkdir_impl(path, false, true);
}

bool WebStorage::mkdir_impl(const StoragePath& path, bool recursive, bool exclusive) {
    clear_error();
    if (!ready_for_io()) return false;
    const auto key = key_for(path);
    if (!key) return false;
    if (key->empty()) return exclusive ? fail("mkdir_exclusive: directory already exists") : true;
    if (memory_backend_) {
        const auto found = memory_->nodes.find(*key);
        if (found != memory_->nodes.end()) {
            if (exclusive) return fail("mkdir_exclusive: directory already exists");
            if (found->second.directory) return true;
            return fail("mkdir: ファイルが既に存在します");
        }
        std::string parent_error;
        if (!ensure_memory_parent(*memory_, parent_key(*key), recursive, parent_error))
            return fail("mkdir: " + parent_error);
        memory_->nodes.emplace(*key, MemoryNode{true, {},
                                                next_memory_timestamp(*memory_)});
        return true;
    }
#ifdef __EMSCRIPTEN__
    const auto resolved = path_for_key(*key);
    std::error_code error;
    const bool created = recursive ? std::filesystem::create_directories(resolved, error)
                                   : std::filesystem::create_directory(resolved, error);
    if (exclusive && !created)
        return fail("mkdir_exclusive: " + error_text(error ? error : std::make_error_code(std::errc::file_exists)));
    if (error && error != std::errc::file_exists)
        return fail("mkdir: " + error_text(error));
    return true;
#else
    return fail("WebStorageの永続化backendがありません", WebStorageError::MountFailed);
#endif
}

bool WebStorage::remove(const StoragePath& path, bool recursive) {
    clear_error();
    if (!ready_for_io()) return false;
    const auto key = key_for(path);
    if (!key) return false;
    if (key->empty()) return fail("remove: Storage root自体は削除できません");
    if (memory_backend_) {
        const auto found = memory_->nodes.find(*key);
        if (found == memory_->nodes.end()) return true;
        if (found->second.directory && !recursive && memory_has_children(*memory_, *key))
            return fail("remove: ディレクトリに子があります");
        memory_remove_subtree(*memory_, *key);
        return true;
    }
#ifdef __EMSCRIPTEN__
    const auto resolved = path_for_key(*key);
    std::error_code error;
    const auto status = std::filesystem::symlink_status(resolved, error);
    if (error == std::errc::no_such_file_or_directory) return true;
    if (error) return fail("remove: " + error_text(error));
    if (std::filesystem::is_directory(status) && recursive)
        std::filesystem::remove_all(resolved, error);
    else
        std::filesystem::remove(resolved, error);
    if (error) return fail("remove: " + error_text(error));
    return true;
#else
    return fail("WebStorageの永続化backendがありません", WebStorageError::MountFailed);
#endif
}

bool WebStorage::rename(const StoragePath& source,
                        const StoragePath& destination,
                        bool overwrite) {
    clear_error();
    if (!ready_for_io()) return false;
    const auto source_key = key_for(source);
    const auto destination_key = key_for(destination);
    if (!source_key || !destination_key) return false;
    if (source_key->empty() || destination_key->empty())
        return fail("rename: Storage rootは移動できません");
    if (*source_key == *destination_key) return true;
    if (is_descendant(*destination_key, *source_key) ||
        is_descendant(*source_key, *destination_key))
        return fail("rename: 移動先が元パスの内側です");
    if (memory_backend_) {
        const auto source_found = memory_->nodes.find(*source_key);
        if (source_found == memory_->nodes.end()) return fail("rename: 元ファイルがありません");
        const auto destination_found = memory_->nodes.find(*destination_key);
        if (destination_found != memory_->nodes.end()) {
            if (!overwrite) return fail("rename: 移動先が既に存在します");
            memory_remove_subtree(*memory_, *destination_key);
        }
        std::string parent_error;
        if (!ensure_memory_parent(*memory_, parent_key(*destination_key), true, parent_error))
            return fail("rename: " + parent_error);
        std::vector<std::pair<std::string, MemoryNode>> moved;
        const auto prefix = *source_key + '/';
        for (auto iterator = memory_->nodes.lower_bound(*source_key);
             iterator != memory_->nodes.end(); ++iterator) {
            if (iterator->first != *source_key &&
                iterator->first.compare(0, prefix.size(), prefix) != 0)
                break;
            const auto suffix = iterator->first == *source_key
                ? std::string{}
                : iterator->first.substr(prefix.size());
            moved.emplace_back(join_key(*destination_key, suffix), std::move(iterator->second));
        }
        memory_remove_subtree(*memory_, *source_key);
        for (auto& [key, node] : moved) memory_->nodes.emplace(std::move(key), std::move(node));
        return true;
    }
#ifdef __EMSCRIPTEN__
    const auto source_path = path_for_key(*source_key);
    const auto destination_path = path_for_key(*destination_key);
    std::error_code error;
    const auto source_status = std::filesystem::symlink_status(source_path, error);
    if (error == std::errc::no_such_file_or_directory)
        return fail("rename: 元ファイルがありません");
    if (error) return fail("rename: " + error_text(error));
    const auto destination_status = std::filesystem::symlink_status(destination_path, error);
    const bool destination_exists = !error &&
        destination_status.type() != std::filesystem::file_type::not_found;
    if (error && error != std::errc::no_such_file_or_directory)
        return fail("rename: " + error_text(error));
    if (destination_exists) {
        if (!overwrite) return fail("rename: 移動先が既に存在します");
    }

    if (!destination_path.parent_path().empty()) {
        std::filesystem::create_directories(destination_path.parent_path(), error);
        if (error) return fail("rename: " + error_text(error));
    }

    // 元の改名が確定するまで、上書きする保存先を保持す
    // る。 remove_all(destination)
    // 成功後の改名失敗によるデータ損失を防ぎ、 move()
    // が失敗を返した場合も安全にする。
    std::filesystem::path backup;
    if (destination_exists && overwrite) {
        for (unsigned attempt = 0; attempt != 128; ++attempt) {
            backup = destination_path.parent_path() /
                (destination_path.filename().string() + ".cwxeditor-rename-" +
                 std::to_string(next_temporary_id()) + ".bak");
            if (!std::filesystem::exists(backup, error)) {
                if (error) return fail("rename: " + error_text(error));
                break;
            }
            if (error) return fail("rename: " + error_text(error));
            backup.clear();
        }
        if (backup.empty()) return fail("rename: 一時退避先を作成できません");
        std::filesystem::rename(destination_path, backup, error);
        if (error) return fail("rename: " + error_text(error));
    }

    std::filesystem::rename(source_path, destination_path, error);
    if (error) {
        if (!backup.empty()) {
            std::error_code restore_error;
            std::filesystem::rename(backup, destination_path, restore_error);
        }
        return fail("rename: " + error_text(error));
    }
    if (!backup.empty()) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(backup, cleanup_error);
        if (cleanup_error) return fail("rename: " + error_text(cleanup_error));
    }
    (void)source_status;
    return true;
#else
    return fail("WebStorageの永続化backendがありません", WebStorageError::MountFailed);
#endif
}

bool WebStorage::copy(const StoragePath& source,
                      const StoragePath& destination,
                      bool overwrite) {
    clear_error();
    if (!ready_for_io()) return false;
    const auto source_key = key_for(source);
    const auto destination_key = key_for(destination);
    if (!source_key || !destination_key) return false;
    if (source_key->empty() || destination_key->empty())
        return fail("copy: Storage rootはコピーできません");
    if (*source_key == *destination_key) return true;
    if (is_descendant(*destination_key, *source_key) ||
        is_descendant(*source_key, *destination_key))
        return fail("copy: コピー先が元パスの内側です");
    if (memory_backend_) {
        const auto source_found = memory_->nodes.find(*source_key);
        if (source_found == memory_->nodes.end()) return fail("copy: 元ファイルがありません");
        const auto destination_found = memory_->nodes.find(*destination_key);
        const auto source_bytes = memory_subtree_bytes(*memory_, *source_key);
        const auto destination_bytes = destination_found == memory_->nodes.end()
            ? 0
            : memory_subtree_bytes(*memory_, *destination_key);
        const auto used_without_destination = memory_->used_bytes - destination_bytes;
        if (options_.max_memory_bytes != 0 &&
             (source_bytes > options_.max_memory_bytes ||
              used_without_destination > options_.max_memory_bytes - source_bytes))
            return fail_quota("Storageの容量が不足しています");
        std::string parent_error;
        if (!ensure_memory_parent(*memory_, parent_key(*destination_key), true, parent_error))
            return fail("copy: " + parent_error);

        // 保存先を置き換える前に、元の状態を取得する。
        // 置換の意味を明示するとともに、確保・元の走査
        // の失敗で既存のツリーを削除しないようにする。
        const auto prefix = *source_key + '/';
        std::vector<std::pair<std::string, MemoryNode>> copied;
        for (auto iterator = memory_->nodes.lower_bound(*source_key);
             iterator != memory_->nodes.end(); ++iterator) {
            if (iterator->first != *source_key &&
                iterator->first.compare(0, prefix.size(), prefix) != 0)
                break;
            const auto suffix = iterator->first == *source_key
                ? std::string{}
                : iterator->first.substr(prefix.size());
            copied.emplace_back(join_key(*destination_key, suffix), iterator->second);
        }
        if (destination_found != memory_->nodes.end()) {
            if (!overwrite) return fail("copy: コピー先が既に存在します");
            memory_remove_subtree(*memory_, *destination_key);
        }
        for (auto& [key, node] : copied) memory_->nodes.emplace(std::move(key), std::move(node));
        memory_->used_bytes += source_bytes;
        return true;
    }
#ifdef __EMSCRIPTEN__
    const auto source_path = path_for_key(*source_key);
    const auto destination_path = path_for_key(*destination_key);
    std::error_code error;
    const auto source_status = std::filesystem::symlink_status(source_path, error);
    if (error == std::errc::no_such_file_or_directory)
        return fail("copy: 元ファイルがありません");
    if (error) return fail("copy: " + error_text(error));

    const auto destination_status = std::filesystem::symlink_status(destination_path, error);
    const bool destination_exists = !error &&
        destination_status.type() != std::filesystem::file_type::not_found;
    if (error && error != std::errc::no_such_file_or_directory)
        return fail("copy: " + error_text(error));
    if (destination_exists && !overwrite)
        return fail("copy: コピー先が既に存在します");

    if (!destination_path.parent_path().empty()) {
        std::filesystem::create_directories(destination_path.parent_path(), error);
        if (error) return fail("copy: " + error_text(error));
    }

    // 兄弟の部分ツリーへコピーし、全体が成功
    // してから確定する。 既存ディレクトリの
    // 置換もメモリ版と同じく古い子を残さず、
    // コピー失敗時は以前の保存先を保つ。
    std::filesystem::path temporary;
    for (unsigned attempt = 0; attempt != 128; ++attempt) {
        temporary = destination_path.parent_path() /
            (destination_path.filename().string() + ".cwxeditor-copy-" +
             std::to_string(next_temporary_id()) + ".tmp");
        if (!std::filesystem::exists(temporary, error)) {
            if (error) return fail("copy: " + error_text(error));
            break;
        }
        if (error) return fail("copy: " + error_text(error));
        temporary.clear();
    }
    if (temporary.empty()) return fail("copy: 一時コピー先を作成できません");

    auto copy_options = std::filesystem::copy_options::none;
    if (std::filesystem::is_directory(source_status))
        copy_options |= std::filesystem::copy_options::recursive;
    std::filesystem::copy(source_path, temporary, copy_options, error);
    if (error) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(temporary, cleanup_error);
        return fail("copy: " + error_text(error));
    }

    std::filesystem::path backup;
    if (destination_exists) {
        for (unsigned attempt = 0; attempt != 128; ++attempt) {
            backup = destination_path.parent_path() /
                (destination_path.filename().string() + ".cwxeditor-copy-backup-" +
                 std::to_string(next_temporary_id()) + ".tmp");
            if (!std::filesystem::exists(backup, error)) {
                if (error) {
                    std::error_code cleanup_error;
                    std::filesystem::remove_all(temporary, cleanup_error);
                    return fail("copy: " + error_text(error));
                }
                break;
            }
            if (error) {
                std::error_code cleanup_error;
                std::filesystem::remove_all(temporary, cleanup_error);
                return fail("copy: " + error_text(error));
            }
            backup.clear();
        }
        if (backup.empty()) {
            std::error_code cleanup_error;
            std::filesystem::remove_all(temporary, cleanup_error);
            return fail("copy: 一時退避先を作成できません");
        }
        std::filesystem::rename(destination_path, backup, error);
        if (error) {
            std::error_code cleanup_error;
            std::filesystem::remove_all(temporary, cleanup_error);
            return fail("copy: " + error_text(error));
        }
    }

    std::filesystem::rename(temporary, destination_path, error);
    if (error) {
        if (!backup.empty()) {
            std::error_code restore_error;
            std::filesystem::rename(backup, destination_path, restore_error);
        }
        std::error_code cleanup_error;
        std::filesystem::remove_all(temporary, cleanup_error);
        return fail("copy: " + error_text(error));
    }
    if (!backup.empty()) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(backup, cleanup_error);
        if (cleanup_error) return fail("copy: " + error_text(cleanup_error));
    }
    return true;
#else
    return fail("WebStorageの永続化backendがありません", WebStorageError::MountFailed);
#endif
}

bool WebStorage::move(const StoragePath& source,
                      const StoragePath& destination,
                      bool overwrite) {
    clear_error();
    if (!ready_for_io()) return false;
    if (rename(source, destination, overwrite)) return true;
    const auto rename_error = last_error();
    // WebStorageのメモリツリーとIDBFSは単一機器
    // の保存領域。 任意の改名失敗をコピー＋削除へ変えると、不
    // 正パス・権限・元ファイルのエラーを隠し、 置換失敗後に既
    // 存の保存先を壊す場合がある。 代替経路は、将来のWeb
    // マウントで明示された機器間移動のエラーだけに限定する。
    if (!looks_like_cross_device(rename_error)) return false;
    if (!copy(source, destination, overwrite)) {
        const auto copy_error = last_error();
        return fail(rename_error + "; move fallback: " + copy_error);
    }
    if (!remove(source, true)) return false;
    return true;
}

std::unique_ptr<StorageReadHandle> WebStorage::open_read(const StoragePath& path) const {
    clear_error();
    if (!ready_for_io()) return nullptr;
    const auto key = key_for(path);
    if (!key) return nullptr;
    if (memory_backend_) {
        const auto found = memory_->nodes.find(*key);
        if (found == memory_->nodes.end() || found->second.directory) {
            (void)fail("open_read: ファイルがありません", WebStorageError::IoError);
            return nullptr;
        }
        return std::make_unique<MemoryReadHandle>(found->second.data);
    }
#ifdef __EMSCRIPTEN__
    auto result = std::make_unique<FileReadHandle>(path_for_key(*key));
    if (!result->good()) {
        (void)fail("open_read: ファイルを開けません: " + path_text(path));
        return nullptr;
    }
    return result;
#else
    return nullptr;
#endif
}

std::unique_ptr<StorageWriteHandle> WebStorage::open_write(const StoragePath& path,
                                                           bool truncate,
                                                           bool create_parent) {
    clear_error();
    if (!ready_for_io()) return nullptr;
    const auto key = key_for(path);
    if (!key || key->empty()) return nullptr;
    if (memory_backend_) {
        const auto found = memory_->nodes.find(*key);
        if (found != memory_->nodes.end() && found->second.directory) {
            (void)fail("open_write: ディレクトリには書き込めません");
            return nullptr;
        }
        std::string parent_error;
        if (!ensure_memory_parent(*memory_, parent_key(*key), create_parent, parent_error)) {
            (void)fail("open_write: 親ディレクトリを作成できません");
            return nullptr;
        }
        std::vector<std::byte> initial;
        if (!truncate && found != memory_->nodes.end()) initial = found->second.data;
        return std::make_unique<MemoryWriteHandle>(this, lifetime_, path,
                                                   std::move(initial));
    }
#ifdef __EMSCRIPTEN__
    const auto resolved = path_for_key(*key);
    std::error_code error;
    if (create_parent && !resolved.parent_path().empty()) {
        std::filesystem::create_directories(resolved.parent_path(), error);
        if (error) {
            (void)fail("open_write: " + error_text(error));
            return nullptr;
        }
    }
    auto result = std::make_unique<FileWriteHandle>(resolved, truncate);
    if (!result->good()) {
        const auto message = std::string("open_write: ファイルを開けません: ") + path_text(path);
        if (looks_like_quota(message) || errno == ENOSPC || errno == EDQUOT)
            (void)fail_quota("Storageの容量が不足しています");
        else
            (void)fail(message);
        return nullptr;
    }
    return result;
#else
    return nullptr;
#endif
}

std::string WebStorage::last_error() const {
    if (shared_ == nullptr) return "WebStorage state is unavailable";
    std::lock_guard lock(shared_->mutex);
    return shared_->error;
}

std::string_view web_storage_state_name(WebStorageState state) noexcept {
    switch (state) {
    case WebStorageState::Booting: return "Booting";
    case WebStorageState::LoadingStorage: return "LoadingStorage";
    case WebStorageState::Ready: return "Ready";
    case WebStorageState::SavingStorage: return "SavingStorage";
    case WebStorageState::StorageError: return "StorageError";
    }
    return "StorageError";
}

std::string_view web_storage_error_name(WebStorageError error) noexcept {
    switch (error) {
    case WebStorageError::None: return "None";
    case WebStorageError::NotReady: return "NotReady";
    case WebStorageError::MountFailed: return "MountFailed";
    case WebStorageError::SyncFailed: return "SyncFailed";
    case WebStorageError::QuotaExceeded: return "QuotaExceeded";
    case WebStorageError::InvalidPath: return "InvalidPath";
    case WebStorageError::IoError: return "IoError";
    }
    return "IoError";
}

std::unique_ptr<WebStorage> make_web_storage(WebStorageOptions options) {
    return std::make_unique<WebStorage>(std::move(options));
}

std::unique_ptr<WebStorage> make_memory_storage(WebStorageOptions options) {
    options.use_idbfs = false;
    options.fallback_to_memory = true;
    options.auto_start = true;
    return std::make_unique<WebStorage>(std::move(options));
}

}  // namespace cardwirth::platform
