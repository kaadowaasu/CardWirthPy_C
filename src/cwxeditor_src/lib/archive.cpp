#include "cwxeditor_src/lib/archive.hpp"
#include "cwxeditor_src/lib/filesystem.hpp"

#include "cw/lib/archive.hpp"
#include "cw/lib/encoding.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cwxeditor::lib {
namespace {

using Byte = std::byte;
using Bytes = std::vector<Byte>;
using CoreFormat = cw::lib::archive::Format;
using CoreEntry = cw::lib::archive::Entry;

constexpr std::uint64_t kMaxFileSize = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxTotalSize = 2ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxEntries = 100000;

std::string utf8(const std::filesystem::path& value)
{
    const auto encoded = value.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::filesystem::path from_utf8(std::string_view value)
{
#if defined(__cpp_char8_t)
    const auto* first = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path(std::u8string(first, first + value.size()));
#else
    return std::filesystem::path(value);
#endif
}

std::string path_text(const std::filesystem::path& value)
{
    return utf8(value);
}

[[noreturn]] void fail(std::string message)
{
    throw std::runtime_error(std::move(message));
}

void report_exception(std::string& error)
{
    try {
        throw;
    } catch (const std::exception& exception) {
        error = exception.what();
    } catch (...) {
        error = "アーカイブ処理中に不明なエラーが発生しました。";
    }
}

void check_stream(const std::ofstream& stream, const std::filesystem::path& path)
{
    if (!stream) fail("アーカイブを書き込めません: " + path_text(path));
}

enum class FileReadShareMode { standard, cab_fci_source };

Bytes read_file(const std::filesystem::path& path,
                const FileReadShareMode share_mode = FileReadShareMode::standard)
{
#if defined(_WIN32)
    if (share_mode == FileReadShareMode::cab_fci_source) {
        // FCIAddFile は、D版と同じ FNFCIGETOPENINFO コールバック
        // から入力ハンドルを得る。 元ファイルは GENERIC_READ、共有なしで開く。
        const HANDLE raw_handle = ::CreateFileW(
            path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (raw_handle == INVALID_HANDLE_VALUE)
            fail("ファイルを開けません: " + path_text(path));
        const std::unique_ptr<void, decltype(&::CloseHandle)> handle(
            raw_handle, &::CloseHandle);

        LARGE_INTEGER file_size{};
        if (!::GetFileSizeEx(raw_handle, &file_size) || file_size.QuadPart < 0)
            fail("ファイルサイズを取得できません: " + path_text(path));
        const auto size = static_cast<std::uint64_t>(file_size.QuadPart);
        if (size > std::numeric_limits<std::size_t>::max())
            fail("ファイルがプロセスのアドレス空間に収まりません: " + path_text(path));
        if (size > kMaxFileSize)
            fail("ファイルサイズが上限を超えています: " + path_text(path));

        Bytes result(static_cast<std::size_t>(size));
        std::size_t offset = 0;
        while (offset < result.size()) {
            const auto remaining = result.size() - offset;
            const auto request_size = std::min<std::size_t>(
                remaining, std::numeric_limits<DWORD>::max());
            DWORD bytes_read = 0;
            if (!::ReadFile(raw_handle, result.data() + offset,
                            static_cast<DWORD>(request_size), &bytes_read, nullptr) ||
                bytes_read == 0)
                fail("ファイルを読み込めません: " + path_text(path));
            offset += bytes_read;
        }
        return result;
    }
#else
    (void)share_mode;
#endif

    std::ifstream stream(path, std::ios::binary);
    if (!stream) fail("ファイルを開けません: " + path_text(path));
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0) fail("ファイルサイズを取得できません: " + path_text(path));
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > std::numeric_limits<std::size_t>::max())
        fail("ファイルがプロセスのアドレス空間に収まりません: " + path_text(path));
    if (size > kMaxFileSize) fail("ファイルサイズが上限を超えています: " + path_text(path));
    stream.seekg(0, std::ios::beg);
    Bytes result(static_cast<std::size_t>(size));
    if (!result.empty()) {
        stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
        if (!stream) fail("ファイルを読み込めません: " + path_text(path));
    }
    return result;
}

void write_file(const std::filesystem::path& path, const Bytes& data)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) fail("アーカイブ出力を作成できません: " + path_text(path));
    if (!data.empty()) stream.write(reinterpret_cast<const char*>(data.data()),
                                    static_cast<std::streamsize>(data.size()));
    check_stream(stream, path);
}

void write_cab_file_create_always(const std::filesystem::path& path, const Bytes& data)
{
#if defined(_WIN32)
    // D版の FNFCIOPEN と同様、最終CABパスを GENERIC_WRITE、FILE_SHARE_READ
    // 、CREATE_ALWAYS で開く。 Windows専用の処理を共通の
    // アーカイブ境界に置き、すべてのCAB呼び出し元で同じ書き込み処理を使う。
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const auto code = GetLastError();
        fail("CAB出力を作成できません: " + path_text(path) + ": " +
             std::system_category().message(static_cast<int>(code)));
    }
    struct HandleGuard {
        HANDLE value;
        ~HandleGuard() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    } guard{file};

    std::size_t offset = 0;
    while (offset < data.size()) {
        const auto remaining = data.size() - offset;
        const auto requested = static_cast<DWORD>(std::min<std::size_t>(
            remaining, std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!WriteFile(file, data.data() + offset, requested, &written, nullptr)) {
            const auto code = GetLastError();
            fail("CAB出力を書き込めません: " + path_text(path) + ": " +
                 std::system_category().message(static_cast<int>(code)));
        }
        if (written == 0) fail("CAB出力を書き込めません: " + path_text(path));
        offset += written;
    }
#else
    write_file(path, data);
#endif
}

std::string lower_ascii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

ArchiveFormat format_from_extension(const std::filesystem::path& path)
{
    const auto extension = lower_ascii(utf8(path.extension()));
    if (extension == ".cab") return ArchiveFormat::Cab;
    if (extension == ".lha" || extension == ".lzh") return ArchiveFormat::Lha;
    // WSN は、CardWirthのZIP形式シナリオに従来使われている拡張子。
    if (extension == ".zip" || extension == ".wsn") return ArchiveFormat::Zip;
    return ArchiveFormat::Zip;
}

CoreFormat core_format(ArchiveFormat format)
{
    switch (format) {
    case ArchiveFormat::Cab: return CoreFormat::Cab;
    case ArchiveFormat::Lha: return CoreFormat::Lha;
    case ArchiveFormat::Zip: return CoreFormat::Zip;
    case ArchiveFormat::Automatic: break;
    }
    return CoreFormat::Unknown;
}

void require_format(CoreFormat actual, ArchiveFormat requested,
                    const std::filesystem::path& archive)
{
    if (requested == ArchiveFormat::Automatic) return;
    if (actual != core_format(requested)) {
        fail("アーカイブ形式が指定と一致しません: " + path_text(archive));
    }
}

bool reserved_windows_name(std::string_view component)
{
    const auto dot = component.find('.');
    auto stem = std::string(component.substr(0, dot));
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return true;
    return stem.size() == 4 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) &&
        stem[3] >= '1' && stem[3] <= '9';
}

std::string archive_name(const std::filesystem::path& path, bool directory)
{
    auto name = utf8(path);
    std::replace(name.begin(), name.end(), '\\', '/');
    while (!name.empty() && name.back() == '/') name.pop_back();
    if (name.empty()) fail("空のアーカイブ名です。");
    if (name.front() == '/' || (name.size() >= 2 && name[1] == ':'))
        fail("絶対パスをアーカイブ名にできません: " + name);
    if (name.find('\0') != std::string::npos)
        fail("NULを含むアーカイブ名です: " + name);

    std::size_t begin = 0;
    while (begin <= name.size()) {
        const auto end = name.find('/', begin);
        const auto part = name.substr(begin, end == std::string::npos ? end : end - begin);
        if (part.empty() || part == "." || part == "..")
            fail("安全でないアーカイブ名です: " + name);
        if (part.find(':') != std::string::npos || reserved_windows_name(part) ||
            std::any_of(part.begin(), part.end(), [](unsigned char character) {
                return character < 0x20;
            }))
            fail("特殊文字を含むアーカイブ名です: " + name);
        if (part.back() == ' ' || part.back() == '.')
            fail("末尾が空白またはドットのアーカイブ名です: " + name);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    if (directory) name.push_back('/');
    return name;
}

// コールバックの結果を、展開ルートに対する相対パスへ変換する。 両方の
// スラッシュを区切りとして扱い、POSIX・Windows間でエディタ
// を移しても、 危険なアーカイブを安全なものと誤判定しないようにする。
std::string safe_relative_name(const std::filesystem::path& value, bool directory)
{
    auto name = utf8(value);
    std::replace(name.begin(), name.end(), '\\', '/');
    while (!name.empty() && name.back() == '/') name.pop_back();
    if (name.empty()) fail("展開先のアーカイブ名が空です。");
    if (name.front() == '/' || (name.size() >= 2 && name[1] == ':'))
        fail("絶対パスへ展開できません: " + name);
    std::size_t begin = 0;
    while (begin <= name.size()) {
        const auto end = name.find('/', begin);
        const auto part = name.substr(begin, end == std::string::npos ? end : end - begin);
        if (part.empty() || part == "." || part == "..")
            fail("親参照を含む展開先です: " + name);
        if (part.find(':') != std::string::npos || reserved_windows_name(part) ||
            std::any_of(part.begin(), part.end(), [](unsigned char character) {
                return character < 0x20;
            }))
            fail("安全でない展開先です: " + name);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    if (directory) name.push_back('/');
    return name;
}

std::string comparison_key(std::string value)
{
    while (!value.empty() && value.back() == '/') value.pop_back();
#if defined(_WIN32)
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
#endif
    return value;
}

struct SourceEntry {
    std::filesystem::path source;
    std::string name;
    bool directory{};
    const Bytes* memory{};
};

std::vector<SourceEntry> collect_source(const ArchiveCreateRequest& request)
{
    if (request.source_directory.empty()) fail("アーカイブ元ディレクトリが指定されていません。");
    if (request.archive_path.empty()) fail("アーカイブ出力先が指定されていません。");
    std::error_code error;
    const auto status = std::filesystem::symlink_status(request.source_directory, error);
    if (error || !std::filesystem::exists(status))
        fail("アーカイブ元がありません: " + path_text(request.source_directory));
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
        fail("アーカイブ元は通常ディレクトリでなければなりません: " +
             path_text(request.source_directory));

    std::vector<SourceEntry> result;
    std::map<std::string, std::size_t> names;
    std::filesystem::recursive_directory_iterator iterator(
        request.source_directory, std::filesystem::directory_options::none, error);
    if (error) fail("アーカイブ元を走査できません: " + path_text(request.source_directory));
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(error)) {
        if (error) fail("アーカイブ元を走査できません: " + error.message());
        const auto source = iterator->path();
        const auto child_status = iterator->symlink_status(error);
        if (error) fail("アーカイブ元の状態を取得できません: " + path_text(source));
        if (std::filesystem::is_symlink(child_status)) {
            if (!request.include || request.include(source))
                fail("symlinkをアーカイブへ追加できません: " + path_text(source));
            continue;
        }
        const bool directory = std::filesystem::is_directory(child_status);
        const bool regular = std::filesystem::is_regular_file(child_status);
        const bool selected = !request.include || request.include(source);
        if (!selected) {
            if (directory) iterator.disable_recursion_pending();
            continue;
        }
        if (!directory && !regular)
            fail("通常ファイルまたはディレクトリではありません: " + path_text(source));
        const auto relative = source.lexically_relative(request.source_directory);
        const auto name = archive_name(relative, directory);
        const auto key = comparison_key(name);
        if (!names.emplace(key, result.size()).second) fail("重複するアーカイブ名です: " + name);
        result.push_back(SourceEntry{source, name, directory});
        if (result.size() > kMaxEntries) fail("アーカイブ内ファイル数が上限を超えています。");
    }
    std::set<std::string> memory_names;
    std::set<std::string> selected_memory_names;
    for (const auto& file : request.memory_files) {
        const auto name = archive_name(file.path, false);
        const auto key = comparison_key(name);
        if (!memory_names.insert(key).second) fail("重複するアーカイブ名です: " + name);
        // 含めるかの検査とディスク検索の前に、両方のスラッシュ表記を正規化する。
        const auto relative = from_utf8(name);
        auto source = request.source_directory;
        bool selected = true;
        for (auto part = relative.begin(); part != relative.end(); ++part) {
            source /= *part;
            if (request.include && !request.include(source)) { selected = false; break; }
            const auto state = std::filesystem::symlink_status(source, error);
            if (error && error != std::errc::no_such_file_or_directory)
                fail("アーカイブ元の状態を取得できません: " + path_text(source));
            error.clear();
            if (!std::filesystem::exists(state)) continue;
            const bool last = std::next(part) == relative.end();
            if (std::filesystem::is_symlink(state) ||
                (last ? !std::filesystem::is_regular_file(state) : !std::filesystem::is_directory(state)))
                fail("通常のアーカイブ元ではありません: " + path_text(source));
        }
        if (!selected) continue; // 除外されたディレクトリは、仮想的な子も除外する。
        selected_memory_names.insert(key);
        const auto existing = names.find(key);
        if (existing != names.end()) {
            auto& entry = result[existing->second];
            if (entry.directory) fail("重複するアーカイブ名です: " + name);
            entry.memory = &file.data;
        } else {
            names.emplace(key, result.size());
            result.push_back(SourceEntry{source, name, false, &file.data});
            if (result.size() > kMaxEntries) fail("アーカイブ内ファイル数が上限を超えています。");
        }
    }
    // 仮想ファイルを、ほかの選択項目の祖先として扱うことはできない。
    for (const auto& entry : result) {
        auto parent = from_utf8(entry.name).parent_path();
        if (entry.directory) parent = parent.parent_path(); // 名前は / で終わる。
        for (; !parent.empty(); parent = parent.parent_path()) {
            if (selected_memory_names.contains(comparison_key(utf8(parent))))
                fail("ファイルとディレクトリが競合するアーカイブ名です: " + entry.name);
        }
    }
    std::sort(result.begin(), result.end(), [](const SourceEntry& left, const SourceEntry& right) {
        if (left.name != right.name) return left.name < right.name;
        return left.directory && !right.directory;
    });
    return result;
}

std::uint64_t source_total_size(const std::vector<SourceEntry>& entries)
{
    std::uint64_t total = 0;
    for (const auto& entry : entries) {
        if (entry.directory) continue;
        std::error_code error;
        const auto size = entry.memory ? entry.memory->size()
                                       : std::filesystem::file_size(entry.source, error);
        if (error) fail("ファイルサイズを取得できません: " + path_text(entry.source));
        if (size > kMaxFileSize || total > kMaxTotalSize - size)
            fail("アーカイブ元の合計サイズが上限を超えています。");
        total += size;
    }
    return total;
}

std::filesystem::path temporary_path(const std::filesystem::path& target)
{
    static std::atomic<std::uint64_t> sequence{};
    const auto parent = target.parent_path();
    const auto stem = utf8(target.filename());
    const auto tick = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return parent / from_utf8(stem + ".cwx-archive-tmp-" + std::to_string(tick) +
                             "-" + std::to_string(sequence.fetch_add(1)));
}

void commit_file(const std::filesystem::path& temporary, const std::filesystem::path& target)
{
    std::error_code error;
    const auto target_status = std::filesystem::symlink_status(target, error);
    if (error && error != std::errc::no_such_file_or_directory)
        fail("アーカイブ出力先を確認できません: " + path_text(target));
    if (!error && std::filesystem::is_directory(target_status))
        fail("アーカイブ出力先がディレクトリです: " + path_text(target));
    error.clear();
    if (std::filesystem::exists(target_status) && !std::filesystem::remove(target, error))
        fail("既存のアーカイブを置き換えられません: " + path_text(target));
    error.clear();
    std::filesystem::rename(temporary, target, error);
    if (error) fail("アーカイブ出力を確定できません: " + error.message());
}

std::pair<std::uint16_t, std::uint16_t> dos_datetime(const std::filesystem::path& path,
                                                   bool local_time = false)
{
    std::error_code error;
    const auto file_time = std::filesystem::last_write_time(path, error);
    if (error) return {};
    const auto system_time = fileTimeToSystem(file_time);
    const auto seconds = std::chrono::floor<std::chrono::seconds>(system_time).time_since_epoch().count();
    if (seconds < 0) return {};
    const auto value = static_cast<std::time_t>(seconds);
    std::tm fields{};
#if defined(_WIN32)
    if ((local_time ? localtime_s(&fields, &value) : gmtime_s(&fields, &value)) != 0) return {};
#else
    if ((local_time ? localtime_r(&value, &fields) : gmtime_r(&value, &fields)) == nullptr) return {};
#endif
    const int year = std::clamp(fields.tm_year + 1900, 1980, 2107);
    const auto date = static_cast<std::uint16_t>(((year - 1980) << 9) |
        ((fields.tm_mon + 1) << 5) | std::clamp(fields.tm_mday, 1, 31));
    const auto time = static_cast<std::uint16_t>((std::clamp(fields.tm_hour, 0, 23) << 11) |
        (std::clamp(fields.tm_min, 0, 59) << 5) | (std::clamp(fields.tm_sec, 0, 59) / 2));
    return {date, time};
}

std::uint16_t cab_attributes(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        fail("CAB元ファイルの属性を取得できません: " + utf8(path));
    return static_cast<std::uint16_t>(attributes & 0x27U);
#else
    // D版のCAB書き込みはWindows専用。他のシステムではアーカイブ・読み取り専用の
    // 情報を保持するが、 DOSの隠し・システム属性に相当するファイルシステム属性はない。
    const auto permissions = std::filesystem::status(path).permissions();
    constexpr auto writable = std::filesystem::perms::owner_write |
        std::filesystem::perms::group_write | std::filesystem::perms::others_write;
    return static_cast<std::uint16_t>(0x20U | ((permissions & writable) == std::filesystem::perms::none ? 1U : 0U));
#endif
}

void restore_cab_metadata(const std::filesystem::path& path, const CoreEntry& entry) {
#if defined(_WIN32)
    if (entry.mtime_valid) {
        FILETIME dos_time{};
        FILETIME creation_time{};
        if (!DosDateTimeToFileTime(entry.cab_dos_date, entry.cab_dos_time, &dos_time))
            fail("CAB日時をFILETIMEへ変換できません: " + utf8(path));
        if (!LocalFileTimeToFileTime(&dos_time, &creation_time))
            fail("CAB作成日時をUTCへ変換できません: " + utf8(path));
        HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            fail("CAB展開ファイルの作成日時を復元するため開けません: " + utf8(path));
        // D版と同様、作成日時には変換後の FILETIME を使うが、最終更新日時には
        // DosDateTimeToFileTime の値を意図的にそのまま渡す。 UTC
        // 以外の環境でも同じ結果になるよう、このWindows APIの順序を維持する。
        const BOOL restored = SetFileTime(file, &creation_time, nullptr, &dos_time);
        const DWORD error = restored ? ERROR_SUCCESS : GetLastError();
        (void)CloseHandle(file);
        if (!restored)
            fail("CAB展開ファイルの日時を復元できません (Win32 " +
                 std::to_string(error) + "): " + utf8(path));
    }
#else
    if (entry.mtime_valid) {
        const auto instant = std::chrono::system_clock::time_point(
            std::chrono::seconds(entry.mtime_unix));
        std::filesystem::last_write_time(path,
            fileTimeFromSystem(instant));
    }
#endif
#if defined(_WIN32)
    const DWORD attributes = entry.cab_dos_attributes & 0x27U;
    if (!SetFileAttributesW(path.c_str(), attributes ? attributes : FILE_ATTRIBUTE_NORMAL))
        fail("CAB展開ファイルの属性を復元できません: " + utf8(path));
#else
    // DOSの隠し・システム属性はWindowsのファイルシステム情報。
    // 他のホストでは、共通の読み書き処理が読み取り専用の権限を保持する。
    if (entry.cab_dos_attributes & 1U)
        std::filesystem::permissions(path,
            std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
            std::filesystem::perms::others_write, std::filesystem::perm_options::remove);
#endif
}

void remove_extracted_file(const std::filesystem::path& path) noexcept {
    std::error_code ignored;
    // 復元済みのCAB項目や古いバックアップは、読み取り専用の場合がある。
    std::filesystem::permissions(path, std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add, ignored);
    std::filesystem::remove(path, ignored);
}

void put16(Bytes& output, std::uint16_t value)
{
    output.push_back(static_cast<Byte>(value & 0xffU));
    output.push_back(static_cast<Byte>((value >> 8) & 0xffU));
}

void put32(Bytes& output, std::uint32_t value)
{
    put16(output, static_cast<std::uint16_t>(value));
    put16(output, static_cast<std::uint16_t>(value >> 16));
}

void patch16(Bytes& output, std::size_t offset, std::uint16_t value)
{
    if (offset + 2 > output.size()) fail("アーカイブ内部サイズが不正です。");
    output[offset] = static_cast<Byte>(value & 0xffU);
    output[offset + 1] = static_cast<Byte>((value >> 8) & 0xffU);
}

void patch32(Bytes& output, std::size_t offset, std::uint32_t value)
{
    patch16(output, offset, static_cast<std::uint16_t>(value));
    patch16(output, offset + 2, static_cast<std::uint16_t>(value >> 16));
}

std::uint16_t lha_crc16(const Bytes& data)
{
    std::uint16_t crc = 0;
    for (const auto byte : data) {
        crc ^= static_cast<std::uint16_t>(static_cast<std::uint8_t>(byte));
        for (int bit = 0; bit < 8; ++bit)
            crc = static_cast<std::uint16_t>((crc >> 1) ^ ((crc & 1U) ? 0xa001U : 0U));
    }
    return crc;
}

Bytes cab_compressed_block(const Bytes& data, std::size_t offset, std::size_t amount)
{
#if defined(_WIN32)
    // SetupIterateCabinetW はMSZIPのフォルダーを安定して受け付けるが、
    // Windowsのexpand/FDIの一部は、有効なtype-NONEのフォルダーも拒否す
    // る。 非圧縮のDEFLATE格納ブロックを使い、書き込み処理に依存ライブラリを増やさない。
    if (amount > 32768) fail("CAB MSZIPブロックが大きすぎます。");
    Bytes result{Byte{'C'}, Byte{'K'}, Byte{0x01}};
    put16(result, static_cast<std::uint16_t>(amount));
    put16(result, static_cast<std::uint16_t>(~static_cast<std::uint16_t>(amount)));
    result.insert(result.end(), data.begin() + static_cast<std::ptrdiff_t>(offset),
                  data.begin() + static_cast<std::ptrdiff_t>(offset + amount));
    return result;
#else
    // 共通形式のフォルダーは tcompTYPE_NONE とし、CFDATA に生のバ
    // イト列を入れる。 後続の全ファイルの位置をずらすDEFLATEヘッダーは付けない。
    return Bytes(data.begin() + static_cast<std::ptrdiff_t>(offset),
                 data.begin() + static_cast<std::ptrdiff_t>(offset + amount));
#endif
}

void append_name(Bytes& output, std::string_view name)
{
    if (name.find('\0') != std::string_view::npos) fail("NULを含むファイル名です。");
    output.insert(output.end(), reinterpret_cast<const Byte*>(name.data()),
                  reinterpret_cast<const Byte*>(name.data() + name.size()));
    output.push_back(Byte{0});
}

Bytes make_lha(const std::vector<SourceEntry>& entries)
{
    Bytes output;
    output.reserve(1 + entries.size() * 64);
    for (const auto& entry : entries) {
        const auto name = entry.name.substr(0, entry.name.size() - (entry.directory ? 1U : 0U));
        if (name.size() > 255) fail("LHAファイル名が255バイトを超えています: " + name);
        const auto data = entry.directory ? Bytes{} : read_file(entry.source);
        if (data.size() > std::numeric_limits<std::uint32_t>::max())
            fail("LHAファイルが4GiBを超えています: " + name);
        const auto [date, time] = dos_datetime(entry.source);
        const auto header_start = output.size();
        output.push_back(Byte{0}); // ヘッダーサイズ。
        output.push_back(Byte{0}); // ヘッダーのチェックサム。
        const std::string_view method = entry.directory ? "-lhd-" : "-lh0-";
        output.insert(output.end(), reinterpret_cast<const Byte*>(method.data()),
                      reinterpret_cast<const Byte*>(method.data() + method.size()));
        put32(output, static_cast<std::uint32_t>(entry.directory ? 0 : data.size()));
        put32(output, static_cast<std::uint32_t>(entry.directory ? 0 : data.size()));
        put16(output, time);
        put16(output, date);
        output.push_back(static_cast<Byte>(entry.directory ? 0x10 : 0x20));
        output.push_back(Byte{0}); // レベル 0。
        output.push_back(static_cast<Byte>(name.size()));
        output.insert(output.end(), reinterpret_cast<const Byte*>(name.data()),
                      reinterpret_cast<const Byte*>(name.data() + name.size()));
        put16(output, entry.directory ? 0 : lha_crc16(data));
        const auto header_size = output.size() - header_start - 2;
        if (header_size > 255) fail("LHAヘッダが255バイトを超えています: " + name);
        output[header_start] = static_cast<Byte>(header_size);
        std::uint8_t checksum = 0;
        for (std::size_t offset = header_start + 2; offset < output.size(); ++offset)
            checksum = static_cast<std::uint8_t>(checksum + static_cast<std::uint8_t>(output[offset]));
        output[header_start + 1] = static_cast<Byte>(checksum);
        output.insert(output.end(), data.begin(), data.end());
        if (output.size() > kMaxTotalSize) fail("LHA出力サイズが上限を超えています。");
    }
    output.push_back(Byte{0});
    return output;
}

Bytes make_cab(const std::vector<SourceEntry>& entries)
{
    if (entries.size() > std::numeric_limits<std::uint16_t>::max())
        fail("CAB内ファイル数が65535を超えています。");
    struct FileData {
        const SourceEntry* entry{};
        Bytes data;
        std::uint32_t offset{};
        std::string cabinet_name;
        std::uint16_t attributes{0x20U};
    };
    std::vector<FileData> files;
    files.reserve(entries.size());
    std::uint64_t total = 0;
    for (const auto& entry : entries) {
        FileData file{
            &entry,
            entry.directory ? Bytes{}
                            : (entry.memory ? *entry.memory
                                            : read_file(entry.source,
                                                        FileReadShareMode::cab_fci_source)),
            0};
        // メモリ内の置換データには、物理的な元ファイルがない場合がある。 新規ファイルの既
        // 定属性は ARCHIVE とし、実ファイルはD版のDOS属性マスクを保持する。
        if (std::filesystem::exists(entry.source)) file.attributes = cab_attributes(entry.source);
        file.cabinet_name = entry.name;
        std::replace(file.cabinet_name.begin(), file.cabinet_name.end(), '/', '\\');
#if defined(_WIN32)
        // D版と同様、CAB内の名前は toMBSz/FCIAddFile に対応する形式で渡す
        // 。 SetupIterateCabinetW 側は、UTF-8と _A_NAME_IS_UTF
        // ではなく、 WindowsのANSI（CP932）のバイト列として扱う。
        const auto encoded = cw::lib::encoding::utf8_to_cp932(file.cabinet_name);
        file.cabinet_name.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
#else
        // Windows以外の共通読み取り処理には現在のANSIコード
        // ページがないため、 名前がUTF-8であることを明示する。
        file.attributes |= 0x80U;
#endif
        if (file.data.size() > std::numeric_limits<std::uint32_t>::max())
            fail("CABファイルが4GiBを超えています: " + entry.name);
        if (total > std::numeric_limits<std::uint32_t>::max() - file.data.size())
            fail("CABフォルダが4GiBを超えています。");
        file.offset = static_cast<std::uint32_t>(total);
        total += file.data.size();
        files.push_back(std::move(file));
    }

    constexpr std::size_t kCabBlockSize =
#if defined(_WIN32)
        32768;
#else
        65535;
#endif
    std::uint64_t block_count = 0;
    for (const auto& file : files)
        block_count += (file.data.size() + (kCabBlockSize - 1)) / kCabBlockSize;
    if (block_count > std::numeric_limits<std::uint16_t>::max())
        fail("CAB CFDATAブロック数が65535を超えています。");

    Bytes output;
    output.reserve(44 + entries.size() * 32 + static_cast<std::size_t>(total));
    output.insert(output.end(), {Byte{'M'}, Byte{'S'}, Byte{'C'}, Byte{'F'}});
    put32(output, 0); // reserved1
    put32(output, 0); // cbCabinet は下で書き換える。
    put32(output, 0); // reserved2
    put32(output, 44); // coffFiles はヘッダーと一つのフォルダーの後。
    put32(output, 0); // reserved3
    output.push_back(Byte{3}); // マイナーバージョン。
    output.push_back(Byte{1}); // メジャーバージョン。
    put16(output, 1); // cFolders
    put16(output, static_cast<std::uint16_t>(entries.size()));
    put16(output, 0); // フラグ: 予約領域なし、複数巻なし。
    put16(output, 0); // setID
    put16(output, 0); // iCabinet

    // 格納データのフォルダーは一つ。 可変長のファイル表を追
    // 加した後で coffCabStart を書き換える。
    put32(output, 0);
    put16(output, static_cast<std::uint16_t>(block_count));
#if defined(_WIN32)
    put16(output, 1); // tcompTYPE_MSZIP
#else
    put16(output, 0); // tcompTYPE_NONE
#endif

    std::uint64_t file_offset = 0;
    for (const auto& file : files) {
        if (file.data.size() > std::numeric_limits<std::uint32_t>::max())
            fail("CABファイルサイズが不正です。");
        put32(output, static_cast<std::uint32_t>(file.data.size()));
        if (file_offset > std::numeric_limits<std::uint32_t>::max())
            fail("CABファイルオフセットが4GiBを超えています。");
        put32(output, static_cast<std::uint32_t>(file_offset));
        put16(output, 0); // フォルダーのインデックス。
        const auto [date, time] = dos_datetime(file.entry->source, true);
        put16(output, date);
        put16(output, time);
        put16(output, file.attributes);
        append_name(output, file.cabinet_name);
        file_offset += file.data.size();
    }

    if (output.size() > std::numeric_limits<std::uint32_t>::max())
        fail("CABファイルが4GiBを超えています。");
    patch32(output, 36, static_cast<std::uint32_t>(output.size()));
    for (const auto& file : files) {
        std::size_t offset = 0;
        while (offset < file.data.size()) {
            const auto amount = std::min<std::size_t>(kCabBlockSize, file.data.size() - offset);
            const auto compressed = cab_compressed_block(file.data, offset, amount);
            if (compressed.size() > std::numeric_limits<std::uint16_t>::max())
                fail("CAB圧縮ブロックが65535バイトを超えています。");
            // CFDATAのチェックサムは任意。CABではゼロは未指定
            // を意味する。 奇数サイズのブロックのパディングについて
            // SetupAPI とこの小さな書き込み処理の解釈が違っ
            // ても、 有効なMSZIPを拒否しないよう、ゼロを使う。
            put32(output, 0);
            put16(output, static_cast<std::uint16_t>(compressed.size()));
            put16(output, static_cast<std::uint16_t>(amount));
            output.insert(output.end(), compressed.begin(), compressed.end());
            offset += amount;
        }
    }
    if (output.size() > std::numeric_limits<std::uint32_t>::max())
        fail("CAB出力サイズが4GiBを超えています。");
    patch32(output, 8, static_cast<std::uint32_t>(output.size()));
    patch32(output, 36, static_cast<std::uint32_t>(output.size()));
    // フォルダーの coffCabStart はオフセット36にあり、
    // CABの末尾ではなく最初のCFDATAブロックを指す必要がある。
    std::size_t data_start = 44;
    for (const auto& file : files)
        data_start += 16 + file.cabinet_name.size() + 1;
    if (data_start > std::numeric_limits<std::uint32_t>::max())
        fail("CABデータオフセットが4GiBを超えています。");
    patch32(output, 36, static_cast<std::uint32_t>(data_start));
    return output;
}

void ensure_directory_chain(const std::filesystem::path& root,
                            const std::filesystem::path& relative,
                            std::vector<std::filesystem::path>& created)
{
    std::filesystem::path current = root;
    for (const auto& component : relative) {
        current /= component;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        if (!error && std::filesystem::exists(status)) {
            if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
                fail("展開先に安全でないパスがあります: " + path_text(current));
            continue;
        }
        if (error && error != std::errc::no_such_file_or_directory)
            fail("展開先を確認できません: " + path_text(current));
        error.clear();
        if (!std::filesystem::create_directory(current, error) && error)
            fail("展開先ディレクトリを作成できません: " + path_text(current));
        created.push_back(current);
    }
}

void ensure_directory_path(const std::filesystem::path& path,
                           std::vector<std::filesystem::path>& created)
{
    const auto root = path.root_path();
    const auto relative = root.empty() ? path : path.lexically_relative(root);
    ensure_directory_chain(root.empty() ? std::filesystem::path{} : root, relative, created);
}

std::filesystem::path absolute_path(const std::filesystem::path& path)
{
    std::error_code error;
    auto result = std::filesystem::absolute(path, error);
    if (error) fail("パスを絶対化できません: " + path_text(path));
    return result.lexically_normal();
}

std::vector<ArchiveEntry> public_entries(const std::vector<CoreEntry>& entries)
{
    std::vector<ArchiveEntry> result;
    result.reserve(entries.size());
    for (const auto& entry : entries)
        result.push_back(ArchiveEntry{from_utf8(entry.name), entry.uncompressed_size, entry.directory});
    return result;
}

void create_zip(const ArchiveCreateRequest& request,
                const std::vector<SourceEntry>& entries,
                const std::filesystem::path& temporary)
{
    auto writer = cw::lib::archive::Writer::create(temporary);
    // CardWirthのシナリオでは通常deflate
    // を使う。低水準のアーカイブ処理は実行時にzlib
    // を解決し、 利用できなければ明示的に失敗する
    // 。黙って大きな非圧縮アーカイブを作らない。
    for (const auto& entry : entries) {
        if (entry.directory) {
            writer.add_directory(entry.name);
        } else {
            const auto data = read_file(entry.source);
            writer.add_file(entry.name, data, cw::lib::archive::Compression::Deflated);
        }
    }
    writer.close();
    (void)request;
}

struct MappedEntry {
    const CoreEntry* source{};
    ArchiveEntry public_entry;
    std::filesystem::path relative;
    std::filesystem::path output;
    std::string key;
};

std::vector<MappedEntry> map_entries(const ArchiveExtractRequest& request,
                                     const std::vector<CoreEntry>& entries,
                                     const std::filesystem::path& destination)
{
    if (entries.size() > kMaxEntries) fail("アーカイブ内ファイル数が上限を超えています。");
    std::vector<MappedEntry> result;
    result.reserve(entries.size());
    std::set<std::string> keys;
    std::uint64_t total = 0;
    for (const auto& entry : entries) {
        ArchiveEntry public_entry{from_utf8(entry.name), entry.uncompressed_size, entry.directory};
        const auto mapped = request.map_entry ? request.map_entry(public_entry) : public_entry.path;
        if (mapped.empty()) continue;
        const auto name = safe_relative_name(mapped, entry.directory);
        const auto key = comparison_key(name);
        if (!keys.insert(key).second) fail("展開先でパスが重複しています: " + name);
        if (!entry.directory) {
            if (entry.uncompressed_size > kMaxFileSize ||
                total > kMaxTotalSize - entry.uncompressed_size)
                fail("アーカイブ展開サイズが上限を超えています。");
            total += entry.uncompressed_size;
        }
        result.push_back(MappedEntry{&entry, std::move(public_entry), from_utf8(name),
                                     destination / from_utf8(name), key});
    }
    std::sort(result.begin(), result.end(), [](const MappedEntry& left, const MappedEntry& right) {
        if (left.public_entry.directory != right.public_entry.directory)
            return left.public_entry.directory;
        return left.key < right.key;
    });
    // ファイルを別の対応先パスの祖先にはできない。 上でディ
    // レクトリを先に並べるため、どちらの挿入順でも検査する。
    std::set<std::string> all_paths;
    std::set<std::string> files;
    for (const auto& mapped : result) {
        all_paths.insert(mapped.key);
        if (!mapped.public_entry.directory) files.insert(mapped.key);
    }
    for (const auto& mapped : result) {
        auto separator = mapped.key.find('/');
        while (separator != std::string::npos) {
            const auto parent = mapped.key.substr(0, separator);
            if (files.contains(parent)) fail("ファイルとディレクトリが衝突しています: " + mapped.key);
            separator = mapped.key.find('/', separator + 1);
        }
        if (!mapped.public_entry.directory) {
            const auto prefix = mapped.key + "/";
            for (const auto& candidate : all_paths)
                if (candidate.rfind(prefix, 0) == 0)
                    fail("ファイルとディレクトリが衝突しています: " + mapped.key);
        }
    }
    return result;
}

} // namespace

std::vector<ArchiveEntry> PortableArchiveBackend::list(const std::filesystem::path& archive,
                                                       ArchiveFormat format,
                                                       std::string& error)
{
    try {
        auto reader = cw::lib::archive::Reader::open(archive);
        require_format(reader.format(), format, archive);
        if (reader.format() == CoreFormat::SevenZip)
            fail("7zはeditor ArchiveBackendの対応形式ではありません。");
        error.clear();
        return public_entries(reader.entries());
    } catch (...) {
        report_exception(error);
        return {};
    }
}

PortableArchiveBackend::PortableArchiveBackend(std::shared_ptr<FileSystemBackend> filesystem)
    : filesystem_(std::move(filesystem)) {}

bool PortableArchiveBackend::create(const ArchiveCreateRequest& request, std::string& error)
{
    std::filesystem::path temporary;
    try {
        auto format = request.format == ArchiveFormat::Automatic
            ? format_from_extension(request.archive_path) : request.format;
        if (format != ArchiveFormat::Zip && format != ArchiveFormat::Lha && format != ArchiveFormat::Cab)
            fail("未対応のアーカイブ形式です。");
        if (!request.memory_files.empty() && format != ArchiveFormat::Cab)
            fail("メモリ上の入力はCAB作成専用です。");
        const auto entries = collect_source(request);
        (void)source_total_size(entries);
        const auto parent = request.archive_path.parent_path();
        std::error_code filesystem_error;
        if (!parent.empty()) std::filesystem::create_directories(parent, filesystem_error);
        if (filesystem_error) fail("アーカイブ出力先を作成できません: " + path_text(parent));
        if (format != ArchiveFormat::Cab) {
            temporary = temporary_path(request.archive_path);
            std::filesystem::remove(temporary, filesystem_error);
        }
        if (format == ArchiveFormat::Zip) {
            create_zip(request, entries, temporary);
        } else if (format == ArchiveFormat::Lha) {
            const auto data = make_lha(entries);
            std::string backend_error;
            if (filesystem_ && !filesystem_->durable_write(temporary, data, backend_error))
                fail(backend_error.empty() ? "LHA出力を書き込めません。" : backend_error);
            if (!filesystem_) write_file(temporary, data);
        } else {
            // FCIには SetupIterateCabinetW が確
            // 実に受け付けるディレクトリ記録がないため、省略して展開時に
            // 必要な親を作る。 出力コールバックは、Windowsでも
            // CREATE_ALWAYS の動作で最終パスへ直接書く。
            std::vector<SourceEntry> files;
            files.reserve(entries.size());
            std::copy_if(entries.begin(), entries.end(), std::back_inserter(files),
                         [](const SourceEntry& entry) { return !entry.directory; });
            const auto data = make_cab(files);
            write_cab_file_create_always(request.archive_path, data);
        }
        if (format != ArchiveFormat::Cab) {
            if (filesystem_) {
                std::string backend_error;
                if (!filesystem_->atomic_replace(temporary, request.archive_path, backend_error))
                    fail(backend_error.empty() ? "アーカイブ出力を確定できません。" : backend_error);
            } else {
                commit_file(temporary, request.archive_path);
            }
        }
        error.clear();
        return true;
    } catch (...) {
        if (!temporary.empty()) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        report_exception(error);
        return false;
    }
}

bool PortableArchiveBackend::extract(const ArchiveExtractRequest& request, std::string& error)
{
    std::vector<std::filesystem::path> created;
    std::vector<std::filesystem::path> temporary_files;
    struct OutputChange {
        std::filesystem::path output;
        std::filesystem::path backup;
        bool remove_output{};
    };
    std::vector<OutputChange> output_changes;
    try {
        if (request.destination.empty()) fail("アーカイブ展開先が指定されていません。");
        auto reader = cw::lib::archive::Reader::open(request.archive_path);
        require_format(reader.format(), request.format, request.archive_path);
        if (reader.format() == CoreFormat::SevenZip)
            fail("7zはeditor ArchiveBackendの対応形式ではありません。");
        const auto destination = absolute_path(request.destination);
        std::error_code filesystem_error;
        const auto status = std::filesystem::symlink_status(destination, filesystem_error);
        if (!filesystem_error && std::filesystem::exists(status)) {
            if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
                fail("展開先がディレクトリではありません: " + path_text(destination));
        } else if (filesystem_error && filesystem_error != std::errc::no_such_file_or_directory) {
            fail("展開先を確認できません: " + path_text(destination));
        } else {
            ensure_directory_path(destination, created);
        }

        const auto mapped = map_entries(request, reader.entries(), destination);
        for (const auto& item : mapped) {
            const auto relative_parent = item.output.parent_path().lexically_relative(destination);
            ensure_directory_chain(destination, relative_parent, created);
            if (item.public_entry.directory) {
                ensure_directory_chain(destination, item.output.lexically_relative(destination), created);
                continue;
            }
            if (item.source->symlink || item.source->encrypted)
                fail("symlinkまたは暗号化されたエントリは展開できません: " + item.source->name);
            std::error_code output_error;
            const auto output_status = std::filesystem::symlink_status(item.output, output_error);
            const bool existed = !output_error && std::filesystem::exists(output_status);
            if (existed && (!request.overwrite || !std::filesystem::is_regular_file(output_status)))
                fail("展開先ファイルが既に存在します: " + path_text(item.output));
            if (output_error && output_error != std::errc::no_such_file_or_directory)
                fail("展開先ファイルを確認できません: " + path_text(item.output));
            const auto data = reader.read(*item.source);
            if (data.size() != item.source->uncompressed_size)
                fail("展開後サイズが一致しません: " + item.source->name);
            const auto temporary = temporary_path(item.output);
            temporary_files.push_back(temporary);
            if (filesystem_) {
                std::string backend_error;
                if (!filesystem_->durable_write(temporary, data, backend_error))
                    fail(backend_error.empty() ? "展開先一時ファイルを書き込めません。" : backend_error);
            } else {
                write_file(temporary, data);
            }
            // D版と同様、CABの出力先は直ちに上書きする。 成功時の結果は
            // 保ちつつ、展開全体が成功するまで以前の各ファイルを保持し、 後
            // の復号・書き込み失敗でユーザーデータが失われないようにする。
            output_changes.push_back({item.output, {}, false});
            auto& change = output_changes.back();
            if (existed) {
                const auto backup = temporary_path(item.output);
                std::filesystem::rename(item.output, backup);
                change.backup = backup;
            }
            output_error.clear();
            if (std::filesystem::exists(item.output, output_error))
                fail("展開先ファイルが既に存在します: " + path_text(item.output));
            change.remove_output = true;
            if (filesystem_) {
                std::string backend_error;
                if (!filesystem_->atomic_replace(temporary, item.output, backend_error))
                    fail(backend_error.empty() ? "展開先ファイルを確定できません。" : backend_error);
            } else {
                std::filesystem::rename(temporary, item.output, output_error);
                if (output_error) {
                    fail("展開先ファイルを確定できません: " + output_error.message());
                }
            }
            if (reader.format() == CoreFormat::Cab) restore_cab_metadata(item.output, *item.source);
        }
        for (const auto& change : output_changes)
            if (!change.backup.empty()) remove_extracted_file(change.backup);
        error.clear();
        return true;
    } catch (...) {
        std::error_code ignored;
        for (const auto& temporary : temporary_files) remove_extracted_file(temporary);
        // 各書き込みを、それが退避したファイルと一緒に元へ戻す。 全出力を
        // 先に削除すると、同じ出力先が繰り返される場合に元の内容を失う。
        for (auto iterator = output_changes.rbegin(); iterator != output_changes.rend(); ++iterator) {
            if (iterator->remove_output) remove_extracted_file(iterator->output);
            if (!iterator->backup.empty() && std::filesystem::exists(iterator->backup, ignored))
                std::filesystem::rename(iterator->backup, iterator->output, ignored);
        }
        for (auto iterator = created.rbegin(); iterator != created.rend(); ++iterator)
            std::filesystem::remove(*iterator, ignored);
        report_exception(error);
        return false;
    }
}

std::shared_ptr<ArchiveBackend> make_archive_backend()
{
    return std::make_shared<PortableArchiveBackend>();
}

std::shared_ptr<ArchiveBackend> make_archive_backend(
    std::shared_ptr<FileSystemBackend> filesystem)
{
    return std::make_shared<PortableArchiveBackend>(std::move(filesystem));
}

} // namespace cwxeditor::lib
