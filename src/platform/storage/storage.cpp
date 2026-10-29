#include "storage.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <system_error>

#ifdef __EMSCRIPTEN__
extern "C" int cardwirth_rename_no_replace(const char*, const char*);
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <stdio.h>
#endif
#endif

namespace cardwirth::platform {

bool rename_storage_path_no_replace(const std::filesystem::path& source,
                       const std::filesystem::path& destination,
                       std::error_code& error) noexcept {
    error.clear();
#ifdef _WIN32
    // Windowsのスキャナーが、新しい一時ディレクトリをFILE_SHARE_DELETE
    // なしで短時間保持する場合がある。 既存項目を上書きしない配置規則を
    // 保ちつつ、この一時的な失敗は再試行する。継続する拒否は失敗とする。
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(500);
    for (;;) {
        if (MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) {
            error.clear();
            return true;
        }
        const auto failure = GetLastError();
        error = std::error_code(static_cast<int>(failure), std::system_category());
        if ((failure != ERROR_ACCESS_DENIED && failure != ERROR_SHARING_VIOLATION) ||
            std::chrono::steady_clock::now() >= deadline) break;
        Sleep(10);
    }
#elif defined(__EMSCRIPTEN__)
    const int result = cardwirth_rename_no_replace(source.c_str(), destination.c_str());
    if (result == 0) return true;
    error = std::error_code(result, std::generic_category());
#elif defined(__linux__) && defined(SYS_renameat2)
    constexpr unsigned int kRenameNoReplace = 1U;
    if (::syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD,
                  destination.c_str(), kRenameNoReplace) == 0)
        return true;
    error = std::error_code(errno, std::generic_category());
#elif defined(__APPLE__)
    if (::renamex_np(source.c_str(), destination.c_str(), RENAME_EXCL) == 0)
        return true;
    error = std::error_code(errno, std::generic_category());
#else
    error = std::make_error_code(std::errc::operation_not_supported);
#endif
    return false;
}

namespace {

std::string path_text(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

std::filesystem::path normalize_root(std::filesystem::path root) {
    if (root.empty()) root = std::filesystem::current_path();
    std::error_code error;
    root = std::filesystem::absolute(std::move(root), error).lexically_normal();
    if (error) throw std::invalid_argument("Storage rootを解決できません: " + error.message());
    // AndroidのSDL_GetPrefPath()
    // は末尾に区切り文字を付けるため、パス要素ごとの
    // 包含判定の前に除く。 空の末尾要素があると、root/Trash
    // がルート外と誤判定される。
    while (root.has_relative_path() && root.filename().empty())
        root = root.parent_path();
    return root;
}

StoragePath normalize_logical_path(const StoragePath& value) {
    if (value.empty() || value == ".") return {};
    // 文字列上の正規化を行う前に、親を表す要素を拒否す
    // る。 a/../bを許すとネイティブの解析方法に
    // 依存し、シンボリックリンクの検査も曖昧になる。
    for (const auto& component : value) {
        if (component == "..")
            throw std::invalid_argument("Storage pathに親ディレクトリ参照は使用できません");
    }
    if (value.has_root_name() || value.has_root_directory())
        return value.lexically_normal();
    const auto normalized = value.lexically_normal();
    return normalized == "." ? StoragePath{} : normalized;
}

bool portable_device_name(std::string component) {
    const auto dot = component.find('.');
    if (dot != std::string::npos) component.resize(dot);
    std::transform(component.begin(), component.end(), component.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::toupper(value));
                   });
    if (component == "CON" || component == "PRN" || component == "AUX" ||
        component == "NUL") {
        return true;
    }
    if (component.size() != 4) return false;
    const bool numbered_device = component.starts_with("COM") || component.starts_with("LPT");
    return numbered_device && component[3] >= '1' && component[3] <= '9';
}

void validate_portable_relative_path(const StoragePath& relative) {
    for (const auto& part : relative) {
        const auto component = path_text(part);
        if (component.empty() || component == ".") continue;
        if (component == "..")
            throw std::invalid_argument("Storage pathに親ディレクトリ参照は使用できません");
        if (component.back() == '.' || component.back() == ' ')
            throw std::invalid_argument("Storage pathの末尾にdotまたは空白は使用できません");
        if (portable_device_name(component))
            throw std::invalid_argument("Storage pathに予約済みデバイス名は使用できません");
        for (const unsigned char value : component) {
            if (value < 0x20 || value == '<' || value == '>' || value == ':' ||
                value == '"' || value == '/' || value == '\\' || value == '|' ||
                value == '?' || value == '*') {
                throw std::invalid_argument("Storage pathにportableでない文字は使用できません");
            }
        }
    }
}

bool path_component_equal(const std::filesystem::path& left,
                          const std::filesystem::path& right) {
#ifdef _WIN32
    return _wcsicmp(left.native().c_str(), right.native().c_str()) == 0;
#else
    return left == right;
#endif
}

bool path_is_same_or_descendant(const std::filesystem::path& candidate,
                                const std::filesystem::path& root) {
    // パス要素を比較し、異なるWindowsドライブやUNC共有でlexically_relative
    // が 空を返す場合を区別する。空の相対パスが同じ場
    // 所を表すのは、ルートの全要素が一致した場合だけ。
    auto candidate_iterator = candidate.begin();
    auto root_iterator = root.begin();
    for (; root_iterator != root.end(); ++root_iterator, ++candidate_iterator) {
        if (candidate_iterator == candidate.end() ||
            !path_component_equal(*candidate_iterator, *root_iterator)) {
            return false;
        }
    }
    return true;
}

bool path_is_inside(const std::filesystem::path& candidate,
                    const std::filesystem::path& root) {
    if (root.empty() || candidate.empty()) return false;
    return path_is_same_or_descendant(candidate, root);
}

bool paths_are_same(const std::filesystem::path& left,
                    const std::filesystem::path& right) {
    if (path_is_same_or_descendant(left, right) &&
        path_is_same_or_descendant(right, left)) {
        return true;
    }
    std::error_code error;
    if (!std::filesystem::exists(left, error) || error) return false;
    error.clear();
    if (!std::filesystem::exists(right, error) || error) return false;
    error.clear();
    return std::filesystem::equivalent(left, right, error) && !error;
}

bool paths_overlap(const std::filesystem::path& left,
                   const std::filesystem::path& right) {
    return path_is_same_or_descendant(left, right) ||
           path_is_same_or_descendant(right, left);
}

std::filesystem::path path_suffix(const std::filesystem::path& candidate,
                                   const std::filesystem::path& root) {
    auto candidate_iterator = candidate.begin();
    auto root_iterator = root.begin();
    for (; root_iterator != root.end(); ++root_iterator, ++candidate_iterator) {
        if (candidate_iterator == candidate.end() ||
            !path_component_equal(*candidate_iterator, *root_iterator)) {
            return {};
        }
    }
    std::filesystem::path result;
    for (; candidate_iterator != candidate.end(); ++candidate_iterator)
        result /= *candidate_iterator;
    return result;
}

std::filesystem::path topology_path(const std::filesystem::path& value,
                                    std::error_code& error) {
    auto result = std::filesystem::weakly_canonical(value, error);
    if (!error) return result;
    if (error != std::errc::no_such_file_or_directory &&
        error != std::errc::not_a_directory) {
        // パス構造の検査は安全性の事前確認であり、I/O自体ではな
        // い。 未作成の末端を正規化できない提供元もあるが、正式な
        // ルート・リンクの検査はresolve()で済んでいる。
        error.clear();
        return value.lexically_normal();
    }

    // Windowsのファイル提供元によっては、新しい末端パスに
    // ERROR_PATH_NOT_FOUNDを返す。 最も近い
    // 既存の親を正規化し、リンクによる別名も構造検査に含める。
    error.clear();
    const auto parent = std::filesystem::weakly_canonical(value.parent_path(), error);
    if (!error) return parent / value.filename();
    error.clear();
    return value.lexically_normal();
}

StorageStat make_stat(const std::filesystem::path& path,
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
    // 通常ファイル以外の境界項目だけ、内部を走査
    // せずにリンク先がディレクトリか確認する。
    if (result.type == StorageEntryType::Symlink || result.type == StorageEntryType::Other) {
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

bool flush_file_to_disk(const std::filesystem::path& path, std::error_code& error) {
#ifdef _WIN32
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return false;
    }
    const bool result = FlushFileBuffers(file) != FALSE;
    if (!result) error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    CloseHandle(file);
    return result;
#else
    int file;
    do { file = ::open(path.c_str(), O_WRONLY); } while (file < 0 && errno == EINTR);
    if (file < 0) {
        error = std::error_code(errno, std::generic_category());
        return false;
    }
    int sync_result;
    do { sync_result = ::fsync(file); } while (sync_result < 0 && errno == EINTR);
    const bool result = sync_result == 0;
    if (!result) error = std::error_code(errno, std::generic_category());
    ::close(file);
    return result;
#endif
}

bool flush_parent_directory(const std::filesystem::path& path, std::error_code& error,
                            bool& supported) {
    supported = true;
#ifndef _WIN32
    int directory;
    do { directory = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY); }
    while (directory < 0 && errno == EINTR);
    if (directory < 0) { error = std::error_code(errno, std::generic_category()); return false; }
    int result;
    do { result = ::fsync(directory); } while (result < 0 && errno == EINTR);
    const int code = result < 0 ? errno : 0;
    ::close(directory);
    if (code == EINVAL || code == ENOTSUP || code == ENOSYS) {
        supported = false; // ディレクトリのfsyncに未対応のファイル提供元もある。
        return true;
    }
    if (code) { error = std::error_code(code, std::generic_category()); return false; }
#else
    (void)path; (void)error;
#endif
    return true;
}

bool create_exclusive_file(const std::filesystem::path& path,
                           std::error_code& error) {
#ifdef _WIN32
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const auto code = GetLastError();
        error = std::error_code(static_cast<int>(code), std::system_category());
        return false;
    }
    CloseHandle(file);
    return true;
#else
    const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0) {
        error = std::error_code(errno, std::generic_category());
        return false;
    }
    ::close(file);
    return true;
#endif
}

std::filesystem::path create_exclusive_temporary(
    const std::filesystem::path& destination, std::error_code& error) {
    static std::atomic_uint64_t sequence{0};
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        const auto suffix = ".cardwirth-tmp-" +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        const auto candidate = destination.parent_path() /
            (destination.filename().native() + std::filesystem::path(suffix).native());
        error.clear();
        if (create_exclusive_file(candidate, error)) return candidate;
        if (error != std::errc::file_exists) return {};
    }
    error = std::make_error_code(std::errc::file_exists);
    return {};
}

std::filesystem::path unique_sibling_path(const std::filesystem::path& destination,
                                          const char* tag,
                                          std::error_code& error) {
    static std::atomic_uint64_t sequence{0};
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        const auto suffix = std::string(tag) +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        const auto candidate = destination.parent_path() /
            (destination.filename().native() + std::filesystem::path(suffix).native());
        error.clear();
        if (!std::filesystem::exists(candidate, error)) {
            if (!error) return candidate;
            return {};
        }
        if (error) return {};
    }
    error = std::make_error_code(std::errc::file_exists);
    return {};
}

bool is_cross_device_error(const std::error_code& error) {
    if (error == std::errc::cross_device_link) return true;
#ifdef _WIN32
    return error.value() == ERROR_NOT_SAME_DEVICE;
#else
    return error.value() == EXDEV;
#endif
}

class NativeReadHandle final : public StorageReadHandle {
public:
    explicit NativeReadHandle(const std::filesystem::path& path) : stream_(path, std::ios::binary) {}

    bool seek(std::uint64_t offset) override {
        if (offset > static_cast<std::uint64_t>((std::numeric_limits<std::streamoff>::max)())) return false;
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
        return !stream_.bad() && (stream_.good() || stream_.eof());
    }

private:
    std::ifstream stream_;
};

class NativeWriteHandle final : public StorageWriteHandle {
public:
    using Callback = std::function<void(bool, std::string)>;
    using DirtyCallback = std::function<void()>;

    NativeWriteHandle(const std::filesystem::path& path,
                      bool truncate,
                      DirtyCallback dirty_callback,
                      Callback completion)
        : path_(path),
          stream_(path, std::ios::binary | (truncate ? std::ios::trunc : std::ios::app)),
          dirty_callback_(std::move(dirty_callback)),
          completion_(std::move(completion)) {}

    ~NativeWriteHandle() override {
        if (!flushed_ && completion_)
            completion_(false, "open_write: flushされないままハンドルが破棄されました");
    }

    bool write(std::span<const std::byte> source) override {
        if (!stream_) return false;
        flushed_ = false;
        stream_.write(reinterpret_cast<const char*>(source.data()),
                      static_cast<std::streamsize>(source.size()));
        if (!stream_) {
            if (completion_) completion_(false, "open_write: ストリームへ書き込めません");
            return false;
        }
        if (stream_ && dirty_callback_) dirty_callback_();
        return static_cast<bool>(stream_);
    }

    bool flush() override {
        stream_.flush();
        if (!stream_) {
            flushed_ = false;
            if (completion_) completion_(false, "open_write: ストリームをflushできません");
            return false;
        }
        std::error_code error;
        if (!flush_file_to_disk(path_, error)) {
            flushed_ = false;
            if (completion_) completion_(false, "open_write flush: " + error.message());
            return false;
        }
        flushed_ = true;
        if (completion_) completion_(true, {});
        return true;
    }

    [[nodiscard]] bool good() const noexcept override { return static_cast<bool>(stream_); }

private:
    std::filesystem::path path_;
    std::ofstream stream_;
    DirtyCallback dirty_callback_;
    Callback completion_;
    bool flushed_{false};
};

}  // namespace

bool sync_storage_directory(const StoragePath& directory, std::error_code& error) {
    bool supported = true;
    return flush_parent_directory(directory / ".storage-sync", error, supported);
}

namespace {
// file_clockの起点は実装によって異なるため、標準の時計変
// 換を優先する。 古い標準ライブラリでは共有オフセットを一度測定し
// 、statや復元のたびに測定誤差を加えない。 これは言語・ライブ
// ラリの境界であり、別のプラットフォーム用のファイル規則ではない。
template<class Clock> struct StorageFileClockBridge {
    using Duration = typename Clock::duration;
    static Duration offset() noexcept {
        static const Duration value =
            std::chrono::duration_cast<Duration>(std::chrono::system_clock::now().time_since_epoch())
            - Clock::now().time_since_epoch();
        return value;
    }
    static auto to_system(typename Clock::time_point value) noexcept {
        if constexpr (requires { Clock::to_sys(value); }) {
            return Clock::to_sys(value);
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
        } else if constexpr (requires { Clock::to_utc(value); }) {
            return std::chrono::utc_clock::to_sys(Clock::to_utc(value));
#endif
        } else {
            return std::chrono::sys_time<Duration>{value.time_since_epoch() + offset()};
        }
    }
    template<class SystemDuration>
    static typename Clock::time_point from_system(std::chrono::sys_time<SystemDuration> value) noexcept {
        // 起点を変更する前に、変換先の時計の精度へ合わせる。 MSVCの1601年
        // 起点は100ns刻みなら収まるが、int64のナノ秒では範囲を超える。
        const auto native_system = std::chrono::time_point_cast<Duration>(value);
        if constexpr (requires { Clock::from_sys(native_system); }) {
            return std::chrono::time_point_cast<Duration>(Clock::from_sys(native_system));
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
        } else if constexpr (requires { Clock::from_utc(std::chrono::utc_clock::from_sys(native_system)); }) {
            return std::chrono::time_point_cast<Duration>(Clock::from_utc(std::chrono::utc_clock::from_sys(native_system)));
#endif
        } else {
            return typename Clock::time_point{
                std::chrono::duration_cast<Duration>(value.time_since_epoch()) - offset()};
        }
    }
};
} // namespace

StorageSystemFileTime storage_file_time_to_system(
    std::filesystem::file_time_type value) noexcept {
    return std::chrono::time_point_cast<std::filesystem::file_time_type::duration>(
        StorageFileClockBridge<std::filesystem::file_time_type::clock>::to_system(value));
}

std::filesystem::file_time_type storage_file_time_from_system(
    StorageSystemFileTime value) noexcept {
    return StorageFileClockBridge<std::filesystem::file_time_type::clock>::from_system(value);
}

std::int64_t storage_timestamp_from_file_time(
    std::filesystem::file_time_type value) noexcept {
    using Nanoseconds = std::chrono::nanoseconds;
    const auto system_value = std::chrono::time_point_cast<Nanoseconds>(
        storage_file_time_to_system(value));
    return system_value.time_since_epoch().count();
}

std::optional<std::filesystem::file_time_type> storage_timestamp_to_file_time(
    std::int64_t value) noexcept {
    if (value == 0) return std::nullopt;
    using Nanoseconds = std::chrono::nanoseconds;
    const std::chrono::sys_time<Nanoseconds> system_value{Nanoseconds{value}};
    return storage_file_time_from_system(
        std::chrono::time_point_cast<std::filesystem::file_time_type::duration>(system_value));
}

std::int64_t storage_timestamp_now() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

NativeStorage::NativeStorage(std::filesystem::path root, bool create_root)
    : root_(normalize_root(std::move(root))) {
    if (!create_root) return;
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error) throw std::runtime_error("Storage rootを作成できません: " + path_text(root_) +
                                        " (" + error.message() + ")");
}

NativeStorage::NativeStorage(std::string root_utf8, bool create_root)
    : NativeStorage([&] {
          try {
#if defined(__cpp_char8_t)
              std::u8string encoded;
              encoded.reserve(root_utf8.size());
              for (const auto character : root_utf8)
                  encoded.push_back(static_cast<char8_t>(
                      static_cast<unsigned char>(character)));
              return std::filesystem::path(encoded);
#else
              return std::filesystem::path(root_utf8);
#endif
          } catch (const std::exception& error) {
              throw std::invalid_argument("Storage rootのUTF-8変換に失敗しました: " +
                                          std::string(error.what()));
          }
      }(),
      create_root) {}

std::filesystem::path NativeStorage::resolve(const StoragePath& path) const {
    const auto normalized = normalize_logical_path(path);
    const auto candidate = normalized.is_absolute()
        ? normalized
        : (root_ / normalized).lexically_normal();
    if (!path_is_inside(candidate, root_))
        throw std::invalid_argument("Storage pathがrootの外側を指しています: " + path_text(path));

    // 既存のディレクトリ要素がシンボリックリンクなら、文字列上の
    // 包含判定だけでは不十分。 既存の各接頭パスを解決して正規化
    // したルートと照合しつつ、最後のファイルは新規作成を許す。
    std::error_code error;
    const auto canonical_root = std::filesystem::weakly_canonical(root_, error);
    if (error) throw std::invalid_argument("Storage rootを解決できません: " + error.message());
    const auto relative = path_suffix(candidate, root_);
    validate_portable_relative_path(relative);
    if (!path_is_inside(candidate, root_))
        throw std::invalid_argument("Storage pathがrootの外側を指しています: " + path_text(path));

    auto canonical_candidate = canonical_root;
    for (const auto& component : relative) {
        const auto next = canonical_candidate / component;
        error.clear();
        const auto entry_status = std::filesystem::symlink_status(next, error);
        if (error && error != std::errc::no_such_file_or_directory &&
            error != std::errc::not_a_directory) {
            throw std::invalid_argument("Storage pathを検査できません: " + error.message());
        }
        if (error) error.clear();
        const bool present = std::filesystem::exists(entry_status);
        if (present) {
            error.clear();
            // weakly_canonicalは参照先がないリンクを未解決
            // のパス要素として扱う。 実際のリンクはcanonical()
            // で対象を解決し、後から作られるディレクトリへのリンクによって
            // Storageのルート外へ作成先が変わらないようにする。
            canonical_candidate = std::filesystem::is_symlink(entry_status)
                ? std::filesystem::canonical(next, error)
                : std::filesystem::weakly_canonical(next, error);
            if (error || canonical_candidate.empty())
                throw std::invalid_argument("Storage pathを解決できません: " + error.message());
        } else {
            canonical_candidate = next.lexically_normal();
        }
        if (!path_is_inside(canonical_candidate, canonical_root))
            throw std::invalid_argument("Storage pathがsymlink経由でrootの外側を指しています: " +
                                        path_text(path));
    }
    return candidate;
}

bool NativeStorage::fail(const std::error_code& error,
                         const std::string& operation) const {
    std::lock_guard lock(error_mutex_);
    last_error_ = operation + ": " + error.message();
    last_error_code_ = error;
    return false;
}

bool NativeStorage::fail(const std::string& message) const {
    std::lock_guard lock(error_mutex_);
    last_error_ = message;
    last_error_code_.clear();
    return false;
}

void NativeStorage::clear_error() const {
    std::lock_guard lock(error_mutex_);
    last_error_.clear();
    last_error_code_.clear();
}

std::string NativeStorage::last_error() const {
    std::lock_guard lock(error_mutex_);
    return last_error_;
}

std::error_code NativeStorage::last_error_code() const {
    std::lock_guard lock(error_mutex_);
    return last_error_code_;
}

std::pair<std::string, std::error_code> NativeStorage::error_snapshot() const {
    std::lock_guard lock(error_mutex_);
    return {last_error_, last_error_code_};
}

StoragePath NativeStorage::persistence_key_for_resolved(
    const StoragePath& resolved) const {
    const auto relative = path_suffix(resolved, root_);
    if (relative.empty() || relative == ".") return {};
    return relative.lexically_normal();
}

StoragePath NativeStorage::persistence_key(const StoragePath& path) const {
    return persistence_key_for_resolved(resolve(path));
}

std::uint64_t NativeStorage::begin_persistence(const StoragePath& path) const {
    std::lock_guard lock(persistence_mutex_);
    auto& info = persistence_[path];
    info.state = PersistenceState::Syncing;
    info.error.clear();
    info.durability_note.clear();
    return info.revision;
}

void NativeStorage::finish_persistence(const StoragePath& path,
                                       std::uint64_t revision,
                                       bool success,
                                       std::string error,
                                       std::string durability_note) const {
    std::lock_guard lock(persistence_mutex_);
    auto& info = persistence_[path];
    info.durability_note = std::move(durability_note);
    if (success) {
        info.persisted_revision = revision;
        info.error.clear();
        info.state = info.revision == revision ? PersistenceState::Persisted
                                               : PersistenceState::MemoryDirty;
    } else {
        info.error = std::move(error);
        info.state = info.revision == revision ? PersistenceState::SyncFailed
                                               : PersistenceState::MemoryDirty;
    }
}

void NativeStorage::record_persisted(const StoragePath& path) const {
    std::lock_guard lock(persistence_mutex_);
    auto& info = persistence_[path];
    info.persisted_revision = info.revision;
    info.state = PersistenceState::Persisted;
    info.error.clear();
    info.durability_note.clear();
}

void NativeStorage::erase_persistence(const StoragePath& path) const {
    std::lock_guard lock(persistence_mutex_);
    for (auto iterator = persistence_.begin(); iterator != persistence_.end();) {
        if (path_is_same_or_descendant(iterator->first, path))
            iterator = persistence_.erase(iterator);
        else
            ++iterator;
    }
}

void NativeStorage::move_persistence(const StoragePath& source,
                                     const StoragePath& destination) const {
    std::lock_guard lock(persistence_mutex_);
    std::vector<std::pair<StoragePath, PersistenceInfo>> moved;
    for (const auto& [key, info] : persistence_) {
        if (!path_is_same_or_descendant(key, source)) continue;
        const auto suffix = path_suffix(key, source);
        moved.emplace_back(suffix.empty() ? destination : destination / suffix, info);
    }
    for (auto iterator = persistence_.begin(); iterator != persistence_.end();) {
        if (path_is_same_or_descendant(iterator->first, source) ||
            path_is_same_or_descendant(iterator->first, destination))
            iterator = persistence_.erase(iterator);
        else
            ++iterator;
    }
    for (auto& [key, info] : moved) persistence_[std::move(key)] = std::move(info);
}

void NativeStorage::copy_persistence(const StoragePath& source,
                                     const StoragePath& destination) const {
    std::lock_guard lock(persistence_mutex_);
    std::vector<std::pair<StoragePath, PersistenceInfo>> copied;
    for (const auto& [key, info] : persistence_) {
        if (!path_is_same_or_descendant(key, source)) continue;
        const auto suffix = path_suffix(key, source);
        auto copy = info;
        copy.state = PersistenceState::Persisted;
        copy.persisted_revision = copy.revision;
        copy.error.clear();
        copied.emplace_back(suffix.empty() ? destination : destination / suffix,
                            std::move(copy));
    }
    for (auto iterator = persistence_.begin(); iterator != persistence_.end();) {
        if (path_is_same_or_descendant(iterator->first, destination))
            iterator = persistence_.erase(iterator);
        else
            ++iterator;
    }
    for (auto& [key, info] : copied) persistence_[std::move(key)] = std::move(info);
}

void NativeStorage::stream_write_started(const StoragePath& path,
                                         std::uint64_t revision) const {
    std::lock_guard lock(persistence_mutex_);
    auto& info = persistence_[path];
    if (info.revision == revision) info.state = PersistenceState::FsDirty;
    info.error.clear();
}

bool NativeStorage::exists(const StoragePath& path) const {
    clear_error();
    try {
        const auto resolved = resolve(path);
        std::error_code error;
        const auto status = std::filesystem::symlink_status(resolved, error);
        if (error && error != std::errc::no_such_file_or_directory) {
            (void)fail(error, "exists");
            return false;
        }
        return std::filesystem::exists(status);
    } catch (const std::exception& error) {
        return fail(std::string("exists: ") + error.what());
    }
}

std::optional<StorageStat> NativeStorage::stat(const StoragePath& path) const {
    clear_error();
    try {
        const auto resolved = resolve(path);
        std::error_code error;
        const auto status = std::filesystem::symlink_status(resolved, error);
        if (error == std::errc::no_such_file_or_directory) return std::nullopt;
        if (error) {
            (void)fail(error, "stat");
            return std::nullopt;
        }
        return make_stat(resolved, status);
    } catch (const std::exception& error) {
        (void)fail(std::string("stat: ") + error.what());
        return std::nullopt;
    }
}

std::vector<StorageEntry> NativeStorage::list(const StoragePath& directory) const {
    clear_error();
    try {
        const auto resolved = resolve(directory);
        std::error_code error;
        if (!std::filesystem::is_directory(resolved, error) || error) {
            (void)fail(error ? error : std::make_error_code(std::errc::not_a_directory), "list");
            return {};
        }

        std::vector<StorageEntry> result;
        for (std::filesystem::directory_iterator iterator(
                 resolved, std::filesystem::directory_options::none, error),
             end;
             iterator != end; iterator.increment(error)) {
            if (error) {
                (void)fail(error, "list");
                return {};
            }
            const auto status = iterator->symlink_status(error);
            if (error) { (void)fail(error, "list"); return {}; }
            StorageEntry entry;
            entry.path = directory / iterator->path().filename();
            entry.name = path_text(iterator->path().filename());
            entry.stat = make_stat(iterator->path(), status);
            result.push_back(std::move(entry));
        }
        std::sort(result.begin(), result.end(), [](const StorageEntry& left,
                                                   const StorageEntry& right) {
            return left.name < right.name;
        });
        if (error) { (void)fail(error, "list"); return {}; }
        return result;
    } catch (const std::exception& error) {
        (void)fail(std::string("list: ") + error.what());
        return {};
    }
}

std::optional<std::vector<std::byte>> NativeStorage::read(const StoragePath& path) const {
    clear_error();
    try {
        const auto resolved = resolve(path);
        std::error_code error;
        const auto size = std::filesystem::file_size(resolved, error);
        if (error) {
            (void)fail(error, "read");
            return std::nullopt;
        }
        // Windowsのヘッダーはmaxを関数形式のマクロとして定義する。
        if (size > static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)())) {
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
    } catch (const std::exception& error) {
        (void)fail(std::string("read: ") + error.what());
        return std::nullopt;
    }
}

bool NativeStorage::write(const StoragePath& path,
                          std::span<const std::byte> data,
                          bool create_parent) {
    clear_error();
    StoragePath key;
    std::uint64_t revision{};
    bool tracking = false;
    try {
        const auto resolved = resolve(path);
        if (paths_are_same(resolved, root_))
            return fail("write: Storage rootへは書き込めません");
        key = persistence_key_for_resolved(resolved);
        revision = begin_persistence(key);
        tracking = true;
        const auto failed = [&](const std::string& message) {
            (void)fail(message);
            finish_persistence(key, revision, false, message);
            tracking = false;
            return false;
        };
        if (data.size() > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)()))
            return failed("write: ファイルが大きすぎます");
        std::error_code error;
        if (create_parent && !resolved.parent_path().empty()) {
            std::filesystem::create_directories(resolved.parent_path(), error);
            if (error) return failed("write: " + error.message());
        }
        {
            std::ofstream output(resolved, std::ios::binary | std::ios::trunc);
            if (!output) return failed("write: ファイルを開けません: " + path_text(path));
            if (!data.empty()) {
                output.write(reinterpret_cast<const char*>(data.data()),
                             static_cast<std::streamsize>(data.size()));
            }
            output.flush();
            if (!output) return failed("write: ファイルを書き込めません: " + path_text(path));
        }
        if (!flush_file_to_disk(resolved, error))
            return failed("write flush: " + error.message());
        finish_persistence(key, revision, true, {});
        tracking = false;
        return true;
    } catch (const std::exception& error) {
        const std::string message = std::string("write: ") + error.what();
        (void)fail(message);
        if (tracking) finish_persistence(key, revision, false, message);
        return false;
    }
}

bool NativeStorage::write_atomic(const StoragePath& path,
                                 std::span<const std::byte> data,
                                 bool create_parent) {
    clear_error();
    std::lock_guard mutation_lock(mutation_mutex_);
    StoragePath key;
    std::uint64_t revision{};
    bool tracking = false;
    try {
        const auto destination = resolve(path);
        if (paths_are_same(destination, root_))
            return fail("write_atomic: Storage rootへは書き込めません");
        key = persistence_key_for_resolved(destination);
        revision = begin_persistence(key);
        tracking = true;
        const auto failed = [&](const std::string& message) {
            (void)fail(message);
            finish_persistence(key, revision, false, message);
            tracking = false;
            return false;
        };
        if (data.size() > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)()))
            return failed("write_atomic: ファイルが大きすぎます");

        std::error_code error;
        if (create_parent && !destination.parent_path().empty()) {
            std::filesystem::create_directories(destination.parent_path(), error);
            if (error) return failed("write_atomic: " + error.message());
        }
        const auto temporary = create_exclusive_temporary(destination, error);
        if (error || temporary.empty())
            return failed("write_atomic: " +
                          (error ? error.message() :
                                   std::make_error_code(std::errc::file_exists).message()));

        const auto discard_temporary = [&] {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        };
        {
            // 排他的に作成した空の一時ファ
            // イルを、書き込み用に開く。
            std::ofstream output(temporary, std::ios::binary | std::ios::out);
            if (!output) {
                output.close();
                discard_temporary();
                return failed("write_atomic: 一時ファイルを開けません: " + path_text(path));
            }
            if (!data.empty()) {
                output.write(reinterpret_cast<const char*>(data.data()),
                             static_cast<std::streamsize>(data.size()));
            }
            output.flush();
            if (!output) {
                output.close();
                discard_temporary();
                return failed("write_atomic: 一時ファイルを書き込めません: " + path_text(path));
            }
        }
        if (!flush_file_to_disk(temporary, error)) {
            discard_temporary();
            return failed("write_atomic flush: " + error.message());
        }

#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
            discard_temporary();
            return failed("write_atomic replace: " + error.message());
        }
#else
        std::filesystem::rename(temporary, destination, error);
        if (error) {
            discard_temporary();
            return failed("write_atomic replace: " + error.message());
        }
#endif
        bool directory_sync_supported = true;
        if (!flush_parent_directory(destination, error, directory_sync_supported))
            return failed("write_atomic: ファイルは置換済みですが、親フォルダの永続化を確認できません: " + error.message());
        finish_persistence(key, revision, true, {}, directory_sync_supported ? std::string{} :
            "この保存先は親フォルダの同期に非対応です。ファイル同期・置換まで完了しました。");
        tracking = false;
        return true;
    } catch (const std::exception& error) {
        const std::string message = std::string("write_atomic: ") + error.what();
        (void)fail(message);
        if (tracking) finish_persistence(key, revision, false, message);
        return false;
    }
}

bool NativeStorage::set_modified(const StoragePath& path, std::int64_t timestamp) {
    clear_error();
    std::lock_guard mutation_lock(mutation_mutex_);
    StoragePath key;
    std::uint64_t revision{};
    bool tracking = false;
    try {
        const auto resolved = resolve(path);
        key = persistence_key_for_resolved(resolved);
        revision = begin_persistence(key);
        tracking = true;
        std::error_code error;
        // このメタデータ設定関数は既知の時刻を受け取るので、起点の0も有効と
        // する。 別の箇所の従来の任意変換では、0を不明として使っている。
        const auto value = timestamp == 0 ? std::optional<std::filesystem::file_time_type>(
            storage_file_time_from_system(StorageSystemFileTime{})) : storage_timestamp_to_file_time(timestamp);
        if (!value) throw std::out_of_range("timestamp is outside the file clock range");
        std::filesystem::last_write_time(resolved, *value, error);
        const bool ok = !error;
        const auto message = ok ? std::string{} : std::string("set_modified: ") + error.message();
        if (!ok) (void)fail(message);
        finish_persistence(key, revision, ok, message);
        return ok;
    } catch (const std::exception& error) {
        const std::string message = std::string("set_modified: ") + error.what();
        (void)fail(message);
        if (tracking) finish_persistence(key, revision, false, message);
        return false;
    }
}

bool NativeStorage::sync() {
    clear_error();
    std::error_code error;
    // 親だけを開き、マーカーファイルは作らない。Webではこのfsyncが mkdir
    // ・unlink・取り込みを含む、提供元共通の永続化待ちになる。
    if (!sync_storage_directory(root_, error))
        return fail(error, "UserDataの永続保存に失敗しました");
    return true;
}

bool NativeStorage::mkdir(const StoragePath& path, bool recursive) {
    return mkdir_impl(path, recursive, false);
}

bool NativeStorage::mkdir_exclusive(const StoragePath& path) {
    return mkdir_impl(path, false, true);
}

bool NativeStorage::mkdir_impl(const StoragePath& path, bool recursive, bool exclusive) {
    clear_error();
    try {
        const auto resolved = resolve(path);
        std::error_code error;
        const bool created = recursive ? std::filesystem::create_directories(resolved, error)
                                       : std::filesystem::create_directory(resolved, error);
        if (exclusive && !created)
            return fail(error ? error : std::make_error_code(std::errc::file_exists), "mkdir_exclusive");
        if (error && error != std::errc::file_exists) return fail(error, "mkdir");
        error.clear();
        if (!std::filesystem::is_directory(resolved, error))
            return fail(error ? error : std::make_error_code(std::errc::not_a_directory), "mkdir");
        return true;
    } catch (const std::exception& error) {
        return fail(std::string("mkdir: ") + error.what());
    }
}

bool NativeStorage::remove(const StoragePath& path, bool recursive) {
    clear_error();
    try {
        const auto resolved = resolve(path);
        if (paths_are_same(resolved, root_))
            return fail("remove: Storage root自体は削除できません");
        const auto key = persistence_key_for_resolved(resolved);
        std::error_code error;
        const auto initial_status = std::filesystem::symlink_status(resolved, error);
        if (!std::filesystem::exists(initial_status)) {
            if (error && error != std::errc::no_such_file_or_directory)
                return fail(error, "remove");
            erase_persistence(key);
            return true;
        }
        const auto status = initial_status;
        if (std::filesystem::is_directory(status) && recursive)
            std::filesystem::remove_all(resolved, error);
        else
            std::filesystem::remove(resolved, error);
        if (error) return fail(error, "remove");
        erase_persistence(key);
        return true;
    } catch (const std::exception& error) {
        return fail(std::string("remove: ") + error.what());
    }
}

bool NativeStorage::rename(const StoragePath& source,
                           const StoragePath& destination,
                           bool overwrite) {
    clear_error();
    try {
        const auto source_path = resolve(source);
        const auto destination_path = resolve(destination);
        if (source_path == destination_path) return true;
        const bool same_object = paths_are_same(source_path, destination_path);
        const auto same_case_spelling = [](const std::filesystem::path& a,
                                           const std::filesystem::path& b) {
#ifdef _WIN32
            return path_component_equal(a, b);
#else
            auto left = path_text(a), right = path_text(b);
            const auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
            std::transform(left.begin(), left.end(), left.begin(), lower);
            std::transform(right.begin(), right.end(), right.begin(), lower);
            return left == right;
#endif
        };
        bool change_case = same_object && source_path.filename() != destination_path.filename() &&
            paths_are_same(source_path.parent_path(), destination_path.parent_path()) &&
            same_case_spelling(source_path.filename(), destination_path.filename());
        if (change_case) {
            bool source_entry = false, destination_entry = false;
            for (const auto& entry : std::filesystem::directory_iterator(source_path.parent_path())) {
                source_entry |= entry.path().filename() == source_path.filename();
                destination_entry |= entry.path().filename() == destination_path.filename();
            }
            // 大文字・小文字を区別する環境では、実在する二つの項目が 相互
            // のリンクなどで同じ対象を指す場合がある。一方を置き換えない。
            if (source_entry && destination_entry) change_case = false;
        }
        // 別名・ハードリンクに対する無操作と、大文字・小文字を区別
        // しない環境で 項目名の表記自体を変える要求を区別する。
        if (same_object && !change_case) return true;
        if (paths_are_same(source_path, root_) || paths_are_same(destination_path, root_))
            return fail("rename: Storage rootは操作できません");

        std::error_code error;
        const auto source_status = std::filesystem::symlink_status(source_path, error);
        if (error || !std::filesystem::exists(source_status))
            return fail(error ? error : std::make_error_code(std::errc::no_such_file_or_directory),
                        "rename source");
        if (change_case) {
            // 移動先はコピー元自身なので、バックアップへ移さない。 ネイティブのrename
            // で、大文字・小文字だけを変えても元のデータを削除せず処理できる。
            std::filesystem::rename(source_path, destination_path, error);
            if (error) return fail(error, "rename spelling");
            bool supported = true;
            if (!flush_parent_directory(destination_path, error, supported))
                return fail(error, "rename spelling: 移動済みですが永続保存に失敗しました");
            move_persistence(persistence_key_for_resolved(source_path),
                             persistence_key_for_resolved(destination_path));
            return true;
        }
        const auto followed_source_status = std::filesystem::status(source_path, error);
        if (error && error != std::errc::no_such_file_or_directory)
            return fail(error, "rename source");
        error.clear();
        const auto topology_source = topology_path(source_path, error);
        if (error) return fail(error, "rename source");
        error.clear();
        const auto topology_destination = topology_path(destination_path, error);
        if (error) return fail(error, "rename destination");

        // 自身のツリー内への再帰的な名前変更には有効な結果が
        // なく、 実装によってはデータを失うため拒否する。
        if ((std::filesystem::is_directory(source_status) ||
             std::filesystem::is_directory(followed_source_status)) &&
            paths_overlap(topology_source, topology_destination))
            return fail("rename: directoryの祖先/子孫への移動はできません");

        const auto source_key = persistence_key_for_resolved(source_path);
        const auto destination_key = persistence_key_for_resolved(destination_path);
        const auto destination_status = std::filesystem::symlink_status(destination_path, error);
        const bool destination_exists = std::filesystem::exists(destination_status);
        if (error && error != std::errc::no_such_file_or_directory)
            return fail(error, "rename");
        if (destination_exists && !overwrite)
            return fail(std::make_error_code(std::errc::file_exists), "rename");

        if (!destination_path.parent_path().empty()) {
            std::filesystem::create_directories(destination_path.parent_path(), error);
            if (error) return fail(error, "rename");
        }

        if (!overwrite) {
            if (!rename_storage_path_no_replace(source_path, destination_path, error))
                return fail(error, "rename no-replace");
            // ディレクトリエントリーの保存まで終えてから、配置完了とする
            // 。 Webのfsyncは通常の書き込みと同じ提供元を通り
            // 、 一時展開ツリーの書き出しも成功を返す前に完了させる。
            bool supported = true;
            if (!flush_parent_directory(destination_path, error, supported))
                return fail(error, "rename: 移動済みですが永続保存に失敗しました");
            if (source_path.parent_path() != destination_path.parent_path() &&
                !flush_parent_directory(source_path, error, supported))
                return fail(error, "rename: 移動元の永続保存に失敗しました");
            move_persistence(source_key, destination_key);
            return true;
        }

        std::filesystem::path backup;
        bool has_backup = false;
        if (destination_exists) {
            bool moved = false;
            for (unsigned attempt = 0; attempt < 128 && !moved; ++attempt) {
                backup = unique_sibling_path(destination_path, ".cardwirth-backup-", error);
                if (error || backup.empty()) return fail(error, "rename backup");
                std::filesystem::rename(destination_path, backup, error);
                if (!error) {
                    moved = true;
                    has_backup = true;
                } else if (error != std::errc::file_exists) {
                    return fail(error, "rename backup");
                }
            }
            if (!has_backup)
                return fail(std::make_error_code(std::errc::file_exists), "rename backup");
        }

        std::filesystem::rename(source_path, destination_path, error);
        if (error) {
            if (has_backup) {
                std::error_code restore_error;
                std::filesystem::rename(backup, destination_path, restore_error);
                if (restore_error)
                    return fail(error, "rename (destination restore failed: " +
                                      restore_error.message() + ")");
            }
            return fail(error, "rename");
        }
        if (has_backup) {
            std::error_code cleanup_error;
            std::filesystem::remove_all(backup, cleanup_error);
            if (cleanup_error) {
                // 配置の確定は既に成功しているため、 追加の
                // 破壊的な操作を避け、バックアップを残す。
                (void)fail(cleanup_error, "rename backup cleanup");
            }
        }
        bool supported = true;
        if (!flush_parent_directory(destination_path, error, supported))
            return fail(error, "rename: 移動済みですが永続保存に失敗しました");
        if (source_path.parent_path() != destination_path.parent_path() &&
            !flush_parent_directory(source_path, error, supported))
            return fail(error, "rename: 移動元の永続保存に失敗しました");
        move_persistence(source_key, destination_key);
        return true;
    } catch (const std::exception& error) {
        return fail(std::string("rename: ") + error.what());
    }
}

bool NativeStorage::copy(const StoragePath& source,
                         const StoragePath& destination,
                         bool overwrite) {
    clear_error();
    try {
        const auto source_path = resolve(source);
        const auto destination_path = resolve(destination);
        if (paths_are_same(source_path, destination_path)) return true;
        if (paths_are_same(source_path, root_) || paths_are_same(destination_path, root_))
            return fail("copy: Storage rootは操作できません");

        std::error_code error;
        const auto source_status = std::filesystem::symlink_status(source_path, error);
        if (error || !std::filesystem::exists(source_status))
            return fail(error ? error : std::make_error_code(std::errc::no_such_file_or_directory),
                        "copy source");
        const auto followed_source_status = std::filesystem::status(source_path, error);
        if (error && error != std::errc::no_such_file_or_directory)
            return fail(error, "copy source");
        error.clear();
        const auto topology_source = topology_path(source_path, error);
        if (error) return fail(error, "copy source");
        error.clear();
        const auto topology_destination = topology_path(destination_path, error);
        if (error) return fail(error, "copy destination");
        if ((std::filesystem::is_directory(source_status) ||
             std::filesystem::is_directory(followed_source_status)) &&
            paths_overlap(topology_source, topology_destination))
            return fail("copy: directoryの祖先/子孫へのコピーはできません");
        if (std::filesystem::is_symlink(source_status))
            return fail("copy: symbolic linkはStorage境界を越える可能性があるためコピーできません");
        if (std::filesystem::is_directory(source_status)) {
            std::filesystem::recursive_directory_iterator it(
                source_path, std::filesystem::directory_options::none, error), end;
            if (error) return fail(error, "copy source traversal");
            for (; it != end; it.increment(error)) {
                if (error) return fail(error, "copy source traversal");
                const auto child_status=it->symlink_status(error);
                if (error) return fail(error, "copy source traversal");
                if (std::filesystem::is_symlink(child_status))
                    return fail("copy: source subtree内のsymbolic linkはコピーできません");
            }
            if (error) return fail(error, "copy source traversal");
        }

        const auto source_key = persistence_key_for_resolved(source_path);
        const auto destination_key = persistence_key_for_resolved(destination_path);
        const auto destination_status = std::filesystem::symlink_status(destination_path, error);
        const bool destination_exists = std::filesystem::exists(destination_status);
        if (error && error != std::errc::no_such_file_or_directory)
            return fail(error, "copy");
        if (destination_exists && !overwrite)
            return fail(std::make_error_code(std::errc::file_exists), "copy");

        if (!destination_path.parent_path().empty()) {
            std::filesystem::create_directories(destination_path.parent_path(), error);
            if (error) return fail(error, "copy");
        }

        std::filesystem::path backup;
        bool has_backup = false;
        if (destination_exists) {
            bool moved = false;
            for (unsigned attempt = 0; attempt < 128 && !moved; ++attempt) {
                backup = unique_sibling_path(destination_path, ".cardwirth-backup-", error);
                if (error || backup.empty()) return fail(error, "copy backup");
                std::filesystem::rename(destination_path, backup, error);
                if (!error) {
                    moved = true;
                    has_backup = true;
                } else if (error != std::errc::file_exists) {
                    return fail(error, "copy backup");
                }
            }
            if (!has_backup)
                return fail(std::make_error_code(std::errc::file_exists), "copy backup");
        }

        std::filesystem::copy(source_path, destination_path,
                              std::filesystem::copy_options::recursive, error);
        if (error) {
            std::error_code cleanup_error;
            if (std::filesystem::exists(destination_path, cleanup_error))
                std::filesystem::remove_all(destination_path, cleanup_error);
            if (has_backup) {
                std::error_code restore_error;
                std::filesystem::rename(backup, destination_path, restore_error);
                if (restore_error)
                    return fail(error, "copy (destination restore failed: " +
                                      restore_error.message() + ")");
            }
            return fail(error, "copy");
        }

        if (has_backup) {
            std::error_code cleanup_error;
            std::filesystem::remove_all(backup, cleanup_error);
            if (cleanup_error) (void)fail(cleanup_error, "copy backup cleanup");
        }
        copy_persistence(source_key, destination_key);
        return true;
    } catch (const std::exception& error) {
        return fail(std::string("copy: ") + error.what());
    }
}

bool NativeStorage::move(const StoragePath& source,
                         const StoragePath& destination,
                         bool overwrite) {
    clear_error();
    if (rename(source, destination, overwrite)) return true;
    // コピー・削除による代替処理を許すのはEXDEV
    // だけ。 不正なパス、コピー元の欠落、権限不足、
    // 移動先の競合は、対象へ触れずにエラーを返す。
    const auto [rename_error, rename_error_code] = error_snapshot();
    if (!is_cross_device_error(rename_error_code)) return false;
    if (!copy(source, destination, overwrite)) {
        const auto copy_error = last_error();
        (void)fail(rename_error + "; move fallback: " + copy_error);
        return false;
    }
    if (!remove(source, true)) {
        const auto remove_error = last_error();
        (void)fail("move fallback: destination committed but source removal failed: " +
                   remove_error);
        return false;
    }
    return true;
}

std::unique_ptr<StorageReadHandle> NativeStorage::open_read(const StoragePath& path) const {
    clear_error();
    try {
        const auto resolved = resolve(path);
        auto result = std::make_unique<NativeReadHandle>(resolved);
        if (!result->good()) {
            (void)fail("open_read: ファイルを開けません: " + path_text(path));
            return nullptr;
        }
        return result;
    } catch (const std::exception& error) {
        (void)fail(std::string("open_read: ") + error.what());
        return nullptr;
    }
}

std::unique_ptr<StorageWriteHandle> NativeStorage::open_write(const StoragePath& path,
                                                              bool truncate,
                                                              bool create_parent) {
    clear_error();
    try {
        const auto resolved = resolve(path);
        if (paths_are_same(resolved, root_)) {
            (void)fail("open_write: Storage rootへは書き込めません");
            return nullptr;
        }
        std::error_code error;
        if (create_parent && !resolved.parent_path().empty()) {
            std::filesystem::create_directories(resolved.parent_path(), error);
            if (error) {
                (void)fail(error, "open_write");
                return nullptr;
            }
        }
        const auto key = persistence_key_for_resolved(resolved);
        const auto revision = begin_persistence(key);
        const auto lifetime = std::weak_ptr<std::uint8_t>(lifetime_token_);
        auto dirty_callback = [this, lifetime, key, revision] {
            if (lifetime.lock()) stream_write_started(key, revision);
        };
        auto completion = [this, lifetime, key, revision](bool success,
                                                           std::string error_message) {
            if (lifetime.lock())
                finish_persistence(key, revision, success, std::move(error_message));
        };
        auto result = std::make_unique<NativeWriteHandle>(
            resolved, truncate, std::move(dirty_callback), std::move(completion));
        if (!result->good()) {
            (void)fail("open_write: ファイルを開けません: " + path_text(path));
            finish_persistence(key, revision, false,
                               "open_write: ファイルを開けません: " + path_text(path));
            return nullptr;
        }
        return result;
    } catch (const std::exception& error) {
        (void)fail(std::string("open_write: ") + error.what());
        return nullptr;
    }
}

PersistenceInfo NativeStorage::persistence(const StoragePath& path) const {
    clear_error();
    try {
        const auto key = persistence_key(path);
        std::lock_guard lock(persistence_mutex_);
        const auto found = persistence_.find(key);
        return found == persistence_.end() ? PersistenceInfo{} : found->second;
    } catch (const std::exception& error) {
        (void)fail(std::string("persistence: ") + error.what());
        return {};
    }
}

bool NativeStorage::dirty(const StoragePath& path) const {
    return persistence(path).dirty();
}

bool NativeStorage::mark_dirty(const StoragePath& path) {
    clear_error();
    try {
        const auto key = persistence_key(path);
        std::lock_guard lock(persistence_mutex_);
        auto& info = persistence_[key];
        if (info.revision != (std::numeric_limits<std::uint64_t>::max)()) ++info.revision;
        info.state = PersistenceState::MemoryDirty;
        info.error.clear();
        return true;
    } catch (const std::exception& error) {
        return fail(std::string("mark_dirty: ") + error.what());
    }
}

bool NativeStorage::mark_clean(const StoragePath& path) {
    clear_error();
    try {
        const auto key = persistence_key(path);
        std::lock_guard lock(persistence_mutex_);
        auto& info = persistence_[key];
        info.state = PersistenceState::Clean;
        info.persisted_revision = info.revision;
        info.error.clear();
        return true;
    } catch (const std::exception& error) {
        return fail(std::string("mark_clean: ") + error.what());
    }
}

std::unique_ptr<Storage> make_native_storage(std::filesystem::path root,
                                             bool create_root) {
    return std::make_unique<NativeStorage>(std::move(root), create_root);
}

namespace {

class ReadOnlyStorage final : public Storage {
public:
    explicit ReadOnlyStorage(std::unique_ptr<Storage> backing)
        : backing_(std::move(backing)) {
        if (!backing_) throw std::invalid_argument("ReadOnlyStorage backing is null");
    }

    [[nodiscard]] StorageCapabilities capabilities() const noexcept override {
        auto result = backing_->capabilities();
        result.atomic_multi_operation_transactions = false;
        return result;
    }
    [[nodiscard]] StoragePath root() const override { return backing_->root(); }
    [[nodiscard]] bool exists(const StoragePath& path) const override {
        clear_error();
        return backing_->exists(path);
    }
    [[nodiscard]] std::optional<StorageStat> stat(
        const StoragePath& path) const override {
        clear_error();
        return backing_->stat(path);
    }
    [[nodiscard]] std::vector<StorageEntry> list(
        const StoragePath& directory) const override {
        clear_error();
        return backing_->list(directory);
    }
    [[nodiscard]] std::optional<std::vector<std::byte>> read(
        const StoragePath& path) const override {
        clear_error();
        return backing_->read(path);
    }
    bool write(const StoragePath&, std::span<const std::byte>, bool) override {
        return reject("write");
    }
    bool write_atomic(const StoragePath&, std::span<const std::byte>, bool) override {
        return reject("write_atomic");
    }
    [[nodiscard]] PersistenceInfo persistence(
        const StoragePath& path) const override {
        clear_error();
        return backing_->persistence(path);
    }
    [[nodiscard]] bool dirty(const StoragePath& path) const override {
        clear_error();
        return backing_->dirty(path);
    }
    bool mark_dirty(const StoragePath&) override { return reject("mark_dirty"); }
    bool mark_clean(const StoragePath&) override { return reject("mark_clean"); }
    bool mkdir(const StoragePath&, bool) override { return reject("mkdir"); }
    bool remove(const StoragePath&, bool) override { return reject("remove"); }
    bool rename(const StoragePath&, const StoragePath&, bool) override {
        return reject("rename");
    }
    bool copy(const StoragePath&, const StoragePath&, bool) override {
        return reject("copy");
    }
    bool move(const StoragePath&, const StoragePath&, bool) override {
        return reject("move");
    }
    [[nodiscard]] std::unique_ptr<StorageReadHandle> open_read(
        const StoragePath& path) const override {
        clear_error();
        return backing_->open_read(path);
    }
    [[nodiscard]] std::unique_ptr<StorageWriteHandle> open_write(
        const StoragePath&, bool, bool) override {
        (void)reject("open_write");
        return nullptr;
    }
    [[nodiscard]] std::string last_error() const override {
        {
            std::lock_guard lock(error_mutex_);
            if (!last_error_.empty()) return last_error_;
        }
        return backing_->last_error();
    }

private:
    bool reject(std::string_view operation) const {
        std::lock_guard lock(error_mutex_);
        last_error_ = std::string(operation) + ": read-only storage";
        return false;
    }
    void clear_error() const {
        std::lock_guard lock(error_mutex_);
        last_error_.clear();
    }

    std::unique_ptr<Storage> backing_;
    mutable std::mutex error_mutex_;
    mutable std::string last_error_;
};

}  // namespace

std::unique_ptr<Storage> make_read_only_storage(
    std::unique_ptr<Storage> backing) {
    return std::make_unique<ReadOnlyStorage>(std::move(backing));
}

}  // namespace cardwirth::platform
