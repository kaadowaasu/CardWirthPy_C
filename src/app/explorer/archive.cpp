#include "archive.hpp"

#include "cw/lib/archive.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#if defined(__linux__)
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <stdio.h>
#endif
#include <unistd.h>
#endif

#if defined(__EMSCRIPTEN__)
extern "C" int cardwirth_rename_no_replace(const char*, const char*);
#endif

namespace cardwirth::explorer {
namespace {

std::string normalized_extension(const std::filesystem::path& path) {
    auto encoded = path.extension().u8string();
    std::string result(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (!result.empty() && result.front() != '.') result.insert(result.begin(), '.');
    return result;
}

std::string path_text(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

std::filesystem::path make_temporary_directory() {
    static std::atomic_uint64_t sequence{0};
    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error);
    if (error) throw std::runtime_error("アーカイブ一時領域を取得できません: " + error.message());
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        const auto directory = root / ("cardwirth-archive-" +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        error.clear();
        if (std::filesystem::create_directory(directory, error)) return directory;
        // 規格に沿ったcreate_directoryは、既存のディレク
        // トリに対してfalseと空のerror_codeを返す。 いず
        // れの表現も、他プロセスとの競合や残存名の衝突として再試行する。
        if (error && error != std::errc::file_exists)
            throw std::runtime_error("アーカイブ一時領域を作成できません: " + error.message());
    }
    throw std::runtime_error("アーカイブ一時領域名を確保できません");
}

std::filesystem::path make_sibling_staging_directory(
    const std::filesystem::path& destination) {
    static std::atomic_uint64_t sequence{0};
    std::error_code error;
    const auto parent = destination.parent_path().empty()
        ? std::filesystem::current_path(error) : destination.parent_path();
    if (error || parent.empty())
        throw std::runtime_error("展開先の親ディレクトリを解決できません");
    std::filesystem::create_directories(parent, error);
    if (error)
        throw std::runtime_error("展開先の親ディレクトリを作成できません: " + error.message());
    const auto epoch = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        const auto suffix = ".cw-extract-tmp-" + std::to_string(epoch) + "-" +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        const auto candidate = parent /
            (destination.filename().native() + std::filesystem::path(suffix).native());
        error.clear();
        if (std::filesystem::create_directory(candidate, error)) return candidate;
        if (error && error != std::errc::file_exists)
            throw std::runtime_error("安全な一時展開先を確保できません: " + error.message());
    }
    throw std::runtime_error("安全な一時展開先名を確保できません");
}

bool publish_directory_no_replace(const std::filesystem::path& source,
                                  const std::filesystem::path& destination,
                                  std::error_code& error) noexcept {
    error.clear();
#if defined(_WIN32)
    if (MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#elif defined(__linux__) && defined(SYS_renameat2)
    constexpr unsigned int kRenameNoReplace = 1U;
    if (::syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD,
                  destination.c_str(), kRenameNoReplace) == 0) return true;
    error = std::error_code(errno, std::generic_category());
#elif defined(__APPLE__)
    if (::renamex_np(source.c_str(), destination.c_str(), RENAME_EXCL) == 0) return true;
    error = std::error_code(errno, std::generic_category());
#elif defined(__EMSCRIPTEN__)
    const int result = cardwirth_rename_no_replace(source.c_str(), destination.c_str());
    if (result == 0) return true;
    error = std::error_code(result, std::generic_category());
#else
    error = std::make_error_code(std::errc::operation_not_supported);
#endif
    return false;
}

cardwirth::platform::StoragePath make_storage_staging_path(
    cardwirth::platform::Storage& storage,
    const cardwirth::platform::StoragePath& destination) {
    static std::atomic_uint64_t sequence{0};
    const auto parent = destination.parent_path();
    const auto epoch = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        const auto name = std::filesystem::path(
            ".cw-extract-tmp-" + std::to_string(epoch) + "-" +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        const auto staging = parent / name;
        if (!storage.exists(staging)) return staging;
    }
    throw std::runtime_error("Storage上の安全な一時展開先を確保できません");
}

std::filesystem::path materialize_source(const ArchiveSource& source,
                                          const std::filesystem::path& directory) {
    auto name = std::filesystem::u8path(source.display_name()).filename();
    if (name.empty() || name == "." || name == "..") name = "archive.bin";
    const auto destination = directory / name;
    auto input = source.open_read();
    if (!input) throw std::runtime_error("アーカイブ入力を開けません");
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("アーカイブ入力の一時保存先を開けません");
    std::array<std::byte, 64U * 1024U> buffer{};
    for (;;) {
        const auto count = input->read(std::span<std::byte>(buffer));
        if (count != 0) {
            output.write(reinterpret_cast<const char*>(buffer.data()),
                         static_cast<std::streamsize>(count));
            if (!output) throw std::runtime_error("アーカイブ入力を一時保存できません");
        }
        if (!input->good()) throw std::runtime_error("アーカイブ入力を読み取れません");
        if (count == 0) break;
    }
    output.flush();
    if (!output) throw std::runtime_error("アーカイブ入力を確定できません");
    output.close();
    if (!output) throw std::runtime_error("アーカイブ入力を閉じられません");
    return destination;
}

void copy_native_file_to_storage(const std::filesystem::path& source,
                                 cardwirth::platform::Storage& storage,
                                 const cardwirth::platform::StoragePath& destination) {
    std::ifstream input(source, std::ios::binary);
    if (!input) throw std::runtime_error("アーカイブ展開結果を開けません");
    auto output = storage.open_write(destination, true, true);
    if (!output) {
        const auto error = storage.last_error();
        throw std::runtime_error(error.empty() ? "アーカイブ展開結果を保存できません" : error);
    }
    std::array<std::byte, 64U * 1024U> buffer{};
    for (;;) {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count != 0 && !output->write(std::span<const std::byte>(
                buffer.data(), static_cast<std::size_t>(count))))
            throw std::runtime_error("アーカイブ展開結果を書き込めません");
        if (input.eof()) break;
        if (!input) throw std::runtime_error("アーカイブ展開結果を読み取れません");
    }
    if (!output->flush()) throw std::runtime_error("アーカイブ展開結果を確定できません");
}

void publish_native_tree_to_storage(const std::filesystem::path& source_root,
                                    cardwirth::platform::Storage& storage,
                                    const cardwirth::platform::StoragePath& destination) {
    if (destination.empty() || storage.exists(destination))
        throw std::invalid_argument("Storage展開先が既に存在します");
    if (!storage.mkdir(destination, true)) {
        const auto error = storage.last_error();
        throw std::runtime_error(error.empty() ? "Storage展開先を作成できません" : error);
    }
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(source_root, error), end;
         iterator != end; iterator.increment(error)) {
        if (error) throw std::runtime_error("アーカイブ展開結果を列挙できません: " +
                                            error.message());
        const auto status = iterator->symlink_status(error);
        if (error) throw std::runtime_error("アーカイブ展開結果を検査できません: " +
                                            error.message());
        const auto relative = std::filesystem::relative(iterator->path(), source_root, error);
        if (error || relative.empty() || relative.is_absolute())
            throw std::runtime_error("アーカイブ展開結果の相対パスが不正です");
        const auto target = destination / relative;
        if (std::filesystem::is_symlink(status) ||
            (!std::filesystem::is_directory(status) &&
             !std::filesystem::is_regular_file(status)))
            throw std::runtime_error("アーカイブ展開結果に未対応の項目があります");
        if (std::filesystem::is_directory(status)) {
            if (!storage.mkdir(target, true)) {
                const auto detail = storage.last_error();
                throw std::runtime_error(detail.empty() ? "Storage展開先を作成できません" : detail);
            }
        } else {
            copy_native_file_to_storage(iterator->path(), storage, target);
        }
    }
    if (error) throw std::runtime_error("アーカイブ展開結果を列挙できません: " +
                                        error.message());
}

class BuiltinArchiveExtractor final : public ArchiveExtractor {
public:
    void extract(const std::filesystem::path& archive,
                 const std::filesystem::path& destination,
                 const ArchiveExtractLimits& limits) const override {
        cw::lib::archive::ExtractOptions options;
        options.overwrite = false;
        options.reject_symlinks = true;
        options.max_file_size = limits.max_file_size;
        options.max_total_size = limits.max_total_size;
        options.max_files = limits.max_files;
        cw::lib::archive::Reader::open(archive).extract(destination, options);
    }
};

}  // namespace

std::string StorageArchiveSource::display_name() const {
    return path_text(path_.filename());
}

std::unique_ptr<cardwirth::platform::StorageReadHandle>
StorageArchiveSource::open_read() const {
    auto result = storage_.open_read(path_);
    if (!result) {
        const auto error = storage_.last_error();
        throw std::runtime_error(error.empty() ? "アーカイブ入力を開けません" : error);
    }
    return result;
}

void ArchiveRegistry::register_extension(std::string extension, ExtractorPtr extractor) {
    if (!extractor) throw std::invalid_argument("ArchiveExtractorがnullです");
    if (!extension.empty() && extension.front() == '.') {
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    } else {
        std::filesystem::path extension_path = std::filesystem::u8path(extension);
        extension = normalized_extension(extension_path);
    }
    if (extension.empty()) throw std::invalid_argument("空のアーカイブ拡張子です");
    extractors_[std::move(extension)] = std::move(extractor);
}

bool ArchiveRegistry::supports(const std::filesystem::path& path) const {
    return extractors_.contains(normalized_extension(path));
}

bool ArchiveRegistry::supports(const ArchiveSource& source) const {
    return supports(std::filesystem::u8path(source.display_name()));
}

void ArchiveRegistry::extract(const std::filesystem::path& archive,
                              const std::filesystem::path& destination,
                              const ArchiveExtractLimits& limits) const {
    const auto extension = normalized_extension(archive);
    const auto found = extractors_.find(extension);
    if (found == extractors_.end())
        throw std::invalid_argument("このアーカイブ形式には対応していません: " + extension);
    std::error_code error;
    if (std::filesystem::exists(destination, error))
        throw std::invalid_argument("展開先が既に存在します");
    const auto temporary = make_sibling_staging_directory(destination);
    try {
        found->second->extract(archive, temporary, limits);
        if (!publish_directory_no_replace(temporary, destination, error))
            throw std::runtime_error("展開結果を確定できません: " + error.message());
    } catch (...) {
        std::filesystem::remove_all(temporary, error);
        throw;
    }
}

void ArchiveRegistry::extract(const ArchiveSource& archive,
                              const std::filesystem::path& destination,
                              const ArchiveExtractLimits& limits) const {
    if (!supports(archive)) {
        const auto extension = normalized_extension(std::filesystem::u8path(
            archive.display_name()));
        throw std::invalid_argument("このアーカイブ形式には対応していません: " + extension);
    }
    const auto temporary_directory = make_temporary_directory();
    try {
        const auto temporary_archive = materialize_source(archive, temporary_directory);
        extract(temporary_archive, destination, limits);
        std::error_code error;
        std::filesystem::remove_all(temporary_directory, error);
        if (error) throw std::runtime_error("アーカイブ一時領域を削除できません: " +
                                            error.message());
    } catch (...) {
        std::error_code error;
        std::filesystem::remove_all(temporary_directory, error);
        throw;
    }
}

void ArchiveRegistry::extract(cardwirth::platform::Storage& storage,
                              const cardwirth::platform::StoragePath& archive,
                              const std::filesystem::path& destination,
                              const ArchiveExtractLimits& limits) const {
    extract(StorageArchiveSource(storage, archive), destination, limits);
}

void ArchiveRegistry::extract_to_storage(cardwirth::platform::Storage& storage,
                                         const cardwirth::platform::StoragePath& archive,
                                         const cardwirth::platform::StoragePath& destination,
                                         const ArchiveExtractLimits& limits) const {
    if (destination.empty()) throw std::invalid_argument("Storage rootへは展開できません");
    if (storage.exists(destination)) throw std::invalid_argument("展開先が既に存在します");
    const auto temporary_directory = make_temporary_directory();
    const auto storage_staging = make_storage_staging_path(storage, destination);
    try {
        const auto temporary_destination = temporary_directory / "result";
        extract(storage, archive, temporary_destination, limits);
        try {
            publish_native_tree_to_storage(temporary_destination, storage, storage_staging);
            if (!storage.rename(storage_staging, destination, false)) {
                const auto detail = storage.last_error();
                throw std::runtime_error(detail.empty()
                    ? "Storage展開結果を確定できません" : detail);
            }
        } catch (...) {
            std::error_code ignored;
            (void)storage.remove(storage_staging, true);
            throw;
        }
        std::error_code error;
        std::filesystem::remove_all(temporary_directory, error);
        if (error) throw std::runtime_error("アーカイブ一時領域を削除できません: " +
                                            error.message());
    } catch (...) {
        std::error_code error;
        std::filesystem::remove_all(temporary_directory, error);
        throw;
    }
}

ArchiveRegistry ArchiveRegistry::with_builtin_formats() {
    ArchiveRegistry result;
    auto builtin = std::make_shared<BuiltinArchiveExtractor>();
    for (const auto* extension : {".zip", ".wsn", ".lzh", ".7z"})
        result.register_extension(extension, builtin);
    return result;
}

}  // namespace cardwirth::explorer
