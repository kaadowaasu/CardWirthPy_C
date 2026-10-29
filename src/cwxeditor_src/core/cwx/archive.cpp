#include "cwxeditor_src/core/cwx/archive.hpp"
#include "cwxeditor_src/core/cwx/sjis.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/lib/filesystem.hpp"

#include "cw/lib/archive.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <limits>
#include <new>
#include <random>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace cwxeditor::cwx::archive {
namespace {

namespace fs = std::filesystem;
using CoreEntry = cw::lib::archive::Entry;
using CoreReader = cw::lib::archive::Reader;
using CoreBytes = std::vector<std::byte>;

constexpr std::size_t kNoSource = std::numeric_limits<std::size_t>::max();

std::string utf8(const fs::path& value)
{
    const auto encoded = value.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

String native_utf8(fs::path value)
{
    value.make_preferred();
    const auto encoded = value.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

fs::path from_utf8(StringView value)
{
#if defined(__cpp_char8_t)
    const auto* first = reinterpret_cast<const char8_t*>(value.data());
    return fs::path(std::u8string(first, first + value.size()));
#else
    return fs::path(value);
#endif
}

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

Bytes read_file(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) fail("アーカイブ入力を開けません: " + utf8(path));
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0) fail("ファイルサイズを取得できません: " + utf8(path));
    const auto size = static_cast<std::uintmax_t>(end);
    // Dはファイル全体を読み込む。独自の512MiB制限は設けず、C++
    // のメモリ確保・読み込みAPIで表現できる範囲だけを制限する。
    if (size > std::numeric_limits<std::size_t>::max() ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        fail("ファイルサイズを表現できません: " + utf8(path));
    stream.seekg(0, std::ios::beg);
    Bytes result(static_cast<std::size_t>(size));
    if (!result.empty()) {
        stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
        if (!stream) fail("ファイルを読み込めません: " + utf8(path));
    }
    return result;
}

void write_file(const fs::path& path, ByteView data)
{
    if (const auto parent = path.parent_path(); !parent.empty()) {
        std::error_code error;
        fs::create_directories(parent, error);
        if (error) fail("展開先ディレクトリを作成できません: " + utf8(parent));
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) fail("ファイルを書き込めません: " + utf8(path));
    if (!data.empty()) {
        stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!stream) fail("ファイルを書き込めません: " + utf8(path));
    }
}

CoreBytes to_core(ByteView data)
{
    CoreBytes result;
    result.reserve(data.size());
    for (const auto byte : data) result.push_back(static_cast<std::byte>(byte));
    return result;
}

Bytes from_core(const CoreBytes& data)
{
    Bytes result;
    result.reserve(data.size());
    for (const auto byte : data) result.push_back(static_cast<Byte>(byte));
    return result;
}

std::uint64_t now_unix()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::uint64_t file_time_unix(const fs::path& path)
{
    std::error_code error;
    const auto value = fs::last_write_time(path, error);
    if (error) fail("更新日時を取得できません: " + utf8(path));
    // ファイル時計を直接変換する。別々に取得した現在時刻を使うと、秒
    // ちょうどの値がDOSの2秒単位の境界を越えてずれる場合がある。
    const auto system = lib::fileTimeToSystem(value);
    const auto seconds = std::chrono::floor<std::chrono::seconds>(system).time_since_epoch().count();
    return seconds < 0 ? 0 : static_cast<std::uint64_t>(seconds);
}

std::uint32_t zip_dos_time(std::uint64_t seconds)
{
    if (seconds > static_cast<std::uint64_t>((std::numeric_limits<std::time_t>::max)()))
        fail("DOS File Time is outside the native calendar range.");
    const auto value = static_cast<std::time_t>(seconds);
    std::tm calendar{};
#if defined(_WIN32)
    if (localtime_s(&calendar, &value) != 0) fail("Unable to convert ZIP local time.");
#else
    if (!localtime_r(&value, &calendar)) fail("Unable to convert ZIP local time.");
#endif
    const auto year = calendar.tm_year + 1900;
    if (year < 1980) fail("DOS File Times cannot hold dates prior to 1980.");
    if (year > 2107) fail("DOS File Times cannot hold dates past 2107.");
    return (static_cast<std::uint32_t>(year - 1980) << 25) |
        (static_cast<std::uint32_t>(calendar.tm_mon + 1) << 21) |
        (static_cast<std::uint32_t>(calendar.tm_mday) << 16) |
        (static_cast<std::uint32_t>(calendar.tm_hour) << 11) |
        (static_cast<std::uint32_t>(calendar.tm_min) << 5) |
        static_cast<std::uint32_t>(calendar.tm_sec >> 1);
}

void set_zip_time(ArchiveMember& member, std::uint64_t seconds)
{
    const auto calendar = zip_dos_time(seconds);
    member.time = seconds;
    member.dosTimeSource = seconds;
    member.dosTime = calendar;
}

void set_zip_name(ArchiveMember& member, String name, bool use_system_encoding)
{
    member.name = std::move(name);
    member.flags = use_system_encoding ? 0U : 0x800U;
    if (use_system_encoding) {
#if defined(_WIN32)
        member.rawName = sjis::tosjis(member.name);
#endif
    }
    if (member.rawName.empty()) member.rawName = member.name;
}

struct TempFile {
    fs::path path;

    ~TempFile()
    {
        if (!path.empty()) {
            std::error_code error;
            fs::remove(path, error);
        }
    }

    TempFile() = default;
    explicit TempFile(fs::path value) : path(std::move(value)) {}
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&& other) noexcept : path(std::move(other.path)) { other.path.clear(); }
    TempFile& operator=(TempFile&& other) noexcept
    {
        if (this != &other) {
            std::error_code error;
            fs::remove(path, error);
            path = std::move(other.path);
            other.path.clear();
        }
        return *this;
    }
};

TempFile temporary_file(const char* extension)
{
    static std::atomic<std::uint64_t> serial{0};
    const auto directory = fs::temp_directory_path();
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device random;
    for (std::uint32_t attempt = 0; attempt != 128; ++attempt) {
        const auto id = serial.fetch_add(1, std::memory_order_relaxed);
        const auto name = std::string("cwxeditor-") + std::to_string(tick) + "-" +
                          std::to_string(random()) + "-" + std::to_string(id) + extension;
        const auto path = directory / name;
        std::error_code error;
        if (!fs::exists(path, error)) return TempFile{path};
    }
    fail("一時アーカイブ名を作成できません。");
}

std::shared_ptr<CoreReader> reader_from_bytes(ByteView data, const char* extension,
                                               cw::lib::archive::Format expected)
{
    auto temp = temporary_file(extension);
    write_file(temp.path, data);
    auto reader = std::make_shared<CoreReader>(CoreReader::open(temp.path,
        cw::lib::archive::ReaderProfile::Editor));
    if (reader->format() != expected) fail("アーカイブ形式が一致しません: " + utf8(temp.path));
    return reader;
}

String normalized_name(String name)
{
    for (auto& character : name) {
        if (character == '\\') character = '/';
    }
    return name;
}

StringView member_key(const ArchiveMember& member)
{
    return member.rawName.empty() ? StringView(member.name) : StringView(member.rawName);
}

void store_member(std::vector<ArchiveMemberPtr>& directory, ArchiveMemberPtr member)
{
    const auto key = member_key(*member);
    const auto found = std::find_if(directory.begin(), directory.end(), [&](const auto& existing) {
        return existing && member_key(*existing) == key;
    });
    // Dの一覧はメンバー名をキーとし最後の代入を優先する。Dの連想配列の反復順は未
    // 規定。C++側は新規キーの追加順を保ち、同じキーの置換では位置を変えない。
    if (found == directory.end()) directory.push_back(std::move(member));
    else *found = std::move(member);
}

bool valid_utf8(StringView value)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t index = 0;
    while (index < value.size()) {
        const auto first = bytes[index++];
        if (first <= 0x7f) continue;
        std::size_t count = 0;
        std::uint32_t codepoint = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            count = 1;
            codepoint = first & 0x1f;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 2;
            codepoint = first & 0x0f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 3;
            codepoint = first & 0x07;
        } else {
            return false;
        }
        if (index + count > value.size()) return false;
        for (std::size_t i = 0; i < count; ++i) {
            const auto continuation = bytes[index++];
            if ((continuation & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (continuation & 0x3f);
        }
        if ((count == 1 && codepoint < 0x80) ||
            (count == 2 && codepoint < 0x800) ||
            (count == 3 && codepoint < 0x10000) ||
            codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
            return false;
        }
    }
    return true;
}

bool has_parent_component(StringView value)
{
    const auto name = normalized_name(String(value));
    std::size_t begin = 0;
    while (begin <= name.size()) {
        const auto end = name.find('/', begin);
        const auto component = name.substr(begin, end == String::npos ? String::npos : end - begin);
        if (component == "..") return true;
        if (end == String::npos) break;
        begin = end + 1;
    }
    return false;
}

bool is_outer_path(StringView value)
{
    const auto name = normalized_name(String(value));
    if (name.empty() || name == ".") return true;
    if (name.front() == '/' || name.front() == '\\') return true;
    if (name.size() >= 2 && std::isalpha(static_cast<unsigned char>(name[0])) && name[1] == ':') return true;

    std::int64_t depth = 0;
    std::size_t begin = 0;
    while (begin <= name.size()) {
        const auto end = name.find('/', begin);
        const auto component = name.substr(begin, end == String::npos ? String::npos : end - begin);
        if (!component.empty() && component != ".") {
            if (component == "..") {
                if (depth == 0) return true;
                --depth;
            } else {
                ++depth;
            }
        }
        if (end == String::npos) break;
        begin = end + 1;
    }
    return depth == 0;
}

bool glob_match(StringView text, StringView pattern)
{
#if defined(_WIN32)
    constexpr bool insensitive = true;
#else
    constexpr bool insensitive = false;
#endif
    const auto equal = [](char left, char right) {
#if defined(_WIN32)
        return std::tolower(static_cast<unsigned char>(left)) ==
               std::tolower(static_cast<unsigned char>(right));
#else
        return left == right;
#endif
    };
    const auto match = [&](auto&& self, std::size_t ti, std::size_t pi) -> bool {
        while (pi < pattern.size()) {
            const char token = pattern[pi];
            if (token == '*') {
                while (pi < pattern.size() && pattern[pi] == '*') ++pi;
                if (pi == pattern.size()) return true;
                for (std::size_t candidate = ti; candidate <= text.size(); ++candidate) {
                    if (self(self, candidate, pi)) return true;
                }
                return false;
            }
            if (ti >= text.size()) return false;
            if (token == '?') {
                ++ti;
                ++pi;
                continue;
            }
            if (token == '[') {
                std::size_t cursor = pi + 1;
                bool negated = false;
                if (cursor < pattern.size() && (pattern[cursor] == '!' || pattern[cursor] == '^')) {
                    negated = true;
                    ++cursor;
                }
                bool matched = false;
                bool hadItem = false;
                char previous = 0;
                while (cursor < pattern.size() && pattern[cursor] != ']') {
                    char current = pattern[cursor++];
                    if (current == '-' && hadItem && cursor < pattern.size() && pattern[cursor] != ']') {
                        const char rangeEnd = pattern[cursor++];
                        const auto value = insensitive ? std::tolower(static_cast<unsigned char>(text[ti])) : text[ti];
                        const auto rangeBegin = insensitive ? std::tolower(static_cast<unsigned char>(previous)) : previous;
                        const auto rangeLast = insensitive ? std::tolower(static_cast<unsigned char>(rangeEnd)) : rangeEnd;
                        matched = matched || (rangeBegin <= value && value <= rangeLast);
                        previous = rangeEnd;
                    } else {
                        matched = matched || equal(text[ti], current);
                        previous = current;
                        hadItem = true;
                    }
                }
                if (cursor >= pattern.size()) {
                    if (!equal(text[ti], '[')) return false;
                    ++ti;
                    ++pi;
                    continue;
                }
                if (negated) matched = !matched;
                if (!matched) return false;
                ++ti;
                pi = cursor + 1;
                continue;
            }
            if (!equal(text[ti], token)) return false;
            ++ti;
            ++pi;
        }
        return ti == text.size();
    };
    return match(match, 0, 0);
}

String basename(StringView name)
{
    auto normalized = native_utf8(from_utf8(name));
    const auto separator = static_cast<char>(fs::path::preferred_separator);
    while (normalized.size() > 1 && normalized.back() == separator) normalized.pop_back();
    const auto slash = normalized.find_last_of(separator);
    return slash == String::npos ? normalized : normalized.substr(slash + 1);
}

bool path_equal_or_below(const fs::path& value, const fs::path& parent)
{
    const auto a = normalized_name(utf8(value.lexically_normal()));
    const auto b = normalized_name(utf8(parent.lexically_normal()));
#if defined(_WIN32)
    String left = a;
    String right = b;
    std::transform(left.begin(), left.end(), left.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(right.begin(), right.end(), right.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#else
    const String& left = a;
    const String& right = b;
#endif
    return left == right || (left.size() > right.size() && left.compare(0, right.size(), right) == 0 && left[right.size()] == '/');
}

fs::path safe_destination(const fs::path& parent, StringView archivePath)
{
    const String name(archivePath);
    if (name.find('\0') != String::npos) fail("NULを含むアーカイブパスです。");
    if (utils::isOuterPath(name)) fail("安全でないアーカイブパスです: " + name);
    // ルートは呼び出し側が選ぶため、既存の別名は信頼する。一度だけ解決
    // し、メンバー側が指定する各構成要素をそのルートに対して検証する。
    const auto root = fs::weakly_canonical(utils::nabs(parent));
    const auto relative = from_utf8(name);
    const auto within = [&](const fs::path& path) {
        return utils::isSubDirectory(path, root);
    };
    fs::path current = root;
    for (const auto& component : relative) {
        current = fs::weakly_canonical(current / component);
        // Dはアーカイブ名の字句上の確認だけを行う。しかし既存のリンクか
        // らディレクトリ外へ出られるため、この危険な場合はルート内への制
        // 限を維持する。選択したルート内へ解決されるリンクは許可する。
        if (!within(current)) fail("展開先がルート外です: " + name);
    }
    // DのbuildPathと同様に、mkdir/writeではa/../bも含めた表記を維
    // 持する。POSIXのバックスラッシュを含むファイル名を、サブディレクトリへ変換しない。
    return root / relative;
}
bool should_exclude(const fs::path& path, const std::vector<fs::path>& exact,
                    const std::vector<String>& patterns)
{
    for (const auto& candidate : exact) {
        if (path_equal_or_below(path, candidate)) return true;
    }
    const auto text = normalized_name(utf8(path));
    const auto base = basename(text);
    for (const auto& pattern : patterns) {
        const auto normalized = normalized_name(pattern);
        if (glob_match(text, normalized) || glob_match(base, normalized)) return true;
    }
    return false;
}

std::vector<fs::path> normalized_exclusions(const std::vector<String>& values,
                                            std::vector<String>& patterns)
{
    std::vector<fs::path> exact;
    for (const auto& value : values) {
        const auto normalized = normalized_name(value);
        if (normalized.find_first_of("*?[") != String::npos) {
            patterns.push_back(normalized);
            continue;
        }
        std::error_code error;
        auto path = fs::absolute(from_utf8(normalized), error).lexically_normal();
        if (!error) exact.push_back(std::move(path));
    }
    return exact;
}

void write_extracted(const fs::path& parent, String path, const Bytes& data, bool isDir)
{
    const auto target = safe_destination(parent, path);
    if (isDir) {
        if (!data.empty()) fail("ディレクトリメンバにデータがあります: " + path);
        std::error_code error;
        fs::create_directories(target, error);
        if (error) fail("展開先ディレクトリを作成できません: " + utf8(target));
        return;
    }
    write_file(target, data);
}

String lha_member_path(const CoreEntry& entry, bool& is_directory)
{
    constexpr char separator = static_cast<char>(fs::path::preferred_separator);
    const auto& raw_directory = entry.lha_raw_directory;
    String directory;
    std::size_t start = 0;
    for (;;) {
        const auto end = raw_directory.find(static_cast<char>(0xff), start);
        if (start) directory += separator;
        directory += memberName(raw_directory.substr(start,
            end == String::npos ? String::npos : end - start));
        if (end == String::npos) break;
        start = end + 1;
    }
    auto filename = memberName(*entry.lha_raw_filename);
    std::replace(directory.begin(), directory.end(), '\\', separator);
    std::replace(filename.begin(), filename.end(), '\\', separator);
    is_directory = filename.empty() || filename.back() == separator;
    auto result = raw_directory.empty() ? filename : utils::buildPath(directory, filename);
    // Dは埋め込まれたファイル名コメントを分離してから、末尾のネイティブ区切
    // り文字をちょうど一つ除く。ディレクトリかどうかは、それ以前に判定済み。
    if (const auto nul = result.find('\0'); nul != String::npos) result.resize(nul);
    if (!result.empty() && result.back() == separator) result.pop_back();
    return result;
}

std::uint32_t progress_number(std::size_t value)
{
    // Dのcast(uint)は飽和させず、下位32ビットを保持する。
    return static_cast<std::uint32_t>(value);
}

void write_archive(const fs::path& path, ByteView data,
                   const lib::Owner<filesync::FileSync>& sync)
{
    if (const auto parent = path.parent_path(); !parent.empty()) {
        std::error_code error;
        fs::create_directories(parent, error);
        if (error) fail("アーカイブ出力先ディレクトリを作成できません: " + utf8(parent));
    }
    if (sync) {
        sync->push(path, data, {});
    } else if (!filesync::writeFileAndSync(path, data)) {
        fail("アーカイブを書き込めません: " + utf8(path));
    }
}

void write_archive(const fs::path& path, ByteView data, filesync::FileSync* sync)
{
    if (const auto parent = path.parent_path(); !parent.empty()) {
        std::error_code error;
        fs::create_directories(parent, error);
        if (error) fail("アーカイブ出力先ディレクトリを作成できません: " + utf8(parent));
    }
    if (sync) {
        sync->push(path, data, {});
    } else if (!filesync::writeFileAndSync(path, data)) {
        fail("アーカイブを書き込めません: " + utf8(path));
    }
}

} // namespace

struct ZipArchive::Impl {
    std::shared_ptr<CoreReader> reader;
};

struct LhaFile::Impl {
    std::shared_ptr<CoreReader> reader;
};

bool ArchiveMember::isDirectory() const noexcept
{
    // Dの不具合を意図的に修正する。固定のDOS 0x10判定はUnixの権限
    // ビットにも一致するため、作成元OSの実際のディレクトリ種別を解読する。グ
    // ループ書き込み可能な空のUnixファイルをディレクトリ扱いしないため。
    return expandedSize == 0 && expandedData.empty() &&
        (((madeVersion >> 8) == 3 ? ((fileAttributes >> 16) & 0170000U) == 0040000U : (fileAttributes & 0x10U) != 0) || (!name.empty() && name.back() == '/'));
}

ZipArchive::ZipArchive() : impl_(std::make_shared<Impl>()) {}

ZipArchive::ZipArchive(const fs::path& path) : impl_(std::make_shared<Impl>())
{
    impl_->reader = std::make_shared<CoreReader>(CoreReader::open(path, cw::lib::archive::ReaderProfile::Editor));
    if (impl_->reader->format() != cw::lib::archive::Format::Zip) {
        fail("ZIPアーカイブではありません: " + utf8(path));
    }
    loadFromReader();
}

ZipArchive::ZipArchive(const Bytes& data) : ZipArchive(ByteView(data)) {}

ZipArchive::ZipArchive(ByteView data) : impl_(std::make_shared<Impl>())
{
    impl_->reader = reader_from_bytes(data, ".zip", cw::lib::archive::Format::Zip);
    loadFromReader();
}

ZipArchive::ZipArchive(ZipArchive&&) noexcept = default;
ZipArchive& ZipArchive::operator=(ZipArchive&&) noexcept = default;
ZipArchive::~ZipArchive() = default;

void ZipArchive::loadFromReader()
{
    directory.clear();
    if (!impl_ || !impl_->reader) return;
    const auto& entries = impl_->reader->entries();
    directory.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        auto member = std::make_shared<ArchiveMember>();
        member->name = memberName(entry.zip_raw_name);
        member->rawName = entry.zip_raw_name;
        member->flags = entry.utf8_name ? 0x800U : 0U;
        member->fileAttributes = entry.zip_external_attributes;
        member->internalAttributes = entry.zip_internal_attributes;
        member->madeVersion = entry.zip_made_by;
        member->time = entry.mtime_unix;
        member->dosTimeSource = member->time;
        member->dosTime = (static_cast<std::uint32_t>(entry.zip_dos_date) << 16) |
                          entry.zip_dos_time;
        member->compressionMethod = entry.compression == cw::lib::archive::Compression::Stored
            ? CompressionMethod::stored : CompressionMethod::deflate;
        member->compressedSize = entry.compressed_size;
        member->expandedSize = entry.uncompressed_size;
        member->crc32 = entry.crc32;
        member->sourceIndex = index;
        store_member(directory, std::move(member));
    }
}

void ZipArchive::addMember(ArchiveMemberPtr member)
{
    if (!member) fail("nullアーカイブメンバです。");
    // 既存の呼び出し側は生の名前を持つメンバーを作る場合がある。論理的なUTF-8
    // 表現を復号しつつ、元のバイト列はDと同じ正確な一覧キーとして保持する。
    if (member->rawName.empty() && (member->flags & 0x800U) == 0) {
        member->rawName = member->name;
        member->name = memberName(member->name);
    }
    if (member->isDirectory() && !member->expandedData.empty()) fail("ディレクトリにデータがあります。");
    if (member->expandedSize == 0 && !member->isDirectory()) member->expandedSize = member->expandedData.size();
    member->sourceIndex = kNoSource;
    store_member(directory, std::move(member));
}

void ZipArchive::deleteMember(const ArchiveMemberPtr& member)
{
    if (!member) return;
    const auto found = std::find_if(directory.begin(), directory.end(), [&](const auto& candidate) {
        return candidate.get() == member.get();
    });
    if (found != directory.end()) directory.erase(found);
}

bool ZipArchive::contains(StringView name) const
{
    return std::any_of(directory.begin(), directory.end(), [&](const auto& member) {
        return member && member->name == name;
    });
}

Bytes ZipArchive::expand(const ArchiveMemberPtr& member)
{
    if (!member) return {};
    // Dはディレクトリメンバーも展開する。unzipがファイルのコールバッ
    // クを呼ぶ前に、ローカルヘッダーや圧縮のエラーを検出する必要がある。
    if (impl_ && impl_->reader && member->sourceIndex != kNoSource &&
        member->sourceIndex < impl_->reader->entries().size()) {
        const auto data = from_core(impl_->reader->read(impl_->reader->entries()[member->sourceIndex]));
        member->expandedData = data;
        member->expandedSize = data.size();
        member->crc32 = cw::lib::archive::crc32(to_core(data));
        return data;
    }
    return member->expandedData;
}

Bytes ZipArchive::expand(const ArchiveMember& member)
{
    if (const auto found = std::find_if(directory.begin(), directory.end(), [&](const auto& candidate) {
            return candidate.get() == &member;
        }); found != directory.end()) {
        return expand(*found);
    }
    if (member.isDirectory()) return {};
    return member.expandedData;
}

Bytes ZipArchive::build()
{
    auto temp = temporary_file(".zip");
    cw::lib::archive::WriterOptions options;
    // Dのstd.zipは従来形式の論理配置でサイズ・位置のオーバーフローを
    // 拒否し、空でない各メンバーの検査でZIP64 EOCD用の76バイトを
    // 確保する。メンバー数が65535を超えた場合だけZIP64へ切り替える
    // 。共通Writerを維持しつつ、このEditorの契約を明示的に選ぶ。
    options.d_zip_logical_layout_limit = (std::numeric_limits<std::uint32_t>::max)();
    options.emit_extended_timestamps = false;
    const auto emit = [&](cw::lib::archive::Writer& writer, bool forceStored) {
        for (const auto& member : directory) {
            if (!member) continue;
            const auto name = encodeArchiveName(member->name);
            const auto& raw = member->rawName.empty() ? name : member->rawName;
            const bool utf8_name = (member->flags & 0x800U) != 0;
            const auto data = member->isDirectory() ? Bytes{} : expand(member);
            const auto compression = !forceStored && member->compressionMethod == CompressionMethod::deflate
                ? cw::lib::archive::Compression::Deflated : cw::lib::archive::Compression::Stored;
            const auto calendar = member->dosTime && member->dosTimeSource == member->time
                ? *member->dosTime : (member->time == 0 ? 0U : zip_dos_time(member->time));
            writer.add_zip_member(name, raw, to_core(data), member->isDirectory(),
                compression, utf8_name, {member->time, member->fileAttributes, member->internalAttributes,
                                        member->madeVersion, calendar});
        }
    };
    try {
        auto writer = cw::lib::archive::Writer::create(temp.path, options);
        emit(writer, false);
        writer.close();
    } catch (const cw::lib::archive::BackendUnavailable&) {
        // 下位部品ではDeflateは省略可能。実行時のzlib
        // バックエンドがなくても、無圧縮ZIPは有効で移植性の
        // あるアーカイブであり、全メンバーの内容を保持する。
        std::error_code error;
        fs::remove(temp.path, error);
        auto writer = cw::lib::archive::Writer::create(temp.path, options);
        emit(writer, true);
        writer.close();
    }
    const auto result = read_file(temp.path);

    // 可能な範囲でサイズとチェックサムを更新する。新しいアーカイブを
    // 構築した後も、呼び出し側がメタデータを利用できるようにする。
    try {
        const auto built = CoreReader::open(temp.path);
        const auto& entries = built.entries();
        for (std::size_t index = 0; index < directory.size() && index < entries.size(); ++index) {
            if (!directory[index]) continue;
            directory[index]->compressedSize = entries[index].compressed_size;
            directory[index]->expandedSize = entries[index].uncompressed_size;
            directory[index]->crc32 = entries[index].crc32;
        }
    } catch (...) {
        // バイト列は生成済み。メタデータの更新は可能な
        // 範囲で行い、その失敗で構築の成功を隠さない。
    }
    return result;
}

LhaFile::LhaFile(const fs::path& path) : LhaFile(read_file(path)) {}

LhaFile::LhaFile(const Bytes& data) : LhaFile(ByteView(data)) {}

LhaFile::LhaFile(ByteView data) : impl_(std::make_shared<Impl>())
{
    // Dは末尾の一バイトを検査せずに受け入れ、空の一覧とする。汎用Reader
    // はレコードを必要とするため、デコーダーを開かずに空の窓口を
    // 維持する。ファイル入力とメモリ入力はこの経路を共有する。
    if (data.size() == 1) return;
    impl_->reader = reader_from_bytes(data, ".lzh", cw::lib::archive::Format::Lha);
    loadFromReader();
}

LhaFile::LhaFile(LhaFile&&) noexcept = default;
LhaFile& LhaFile::operator=(LhaFile&&) noexcept = default;
LhaFile::~LhaFile() = default;

void LhaFile::loadFromReader()
{
    infoList.clear();
    nameList.clear();
    if (!impl_ || !impl_->reader) return;
    const auto& entries = impl_->reader->entries();
    infoList.reserve(entries.size());
    nameList.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        LhaInfo info;
        if (entry.lha_raw_filename) {
            info.fileName = lha_member_path(entry, info.isDirectory);
        } else {
            // Dの0/1/2以外のネイティブバックエンドのレベルは、バックエンド
            // のメタデータを維持する。D形式のヘッダーは必ず上記の経路を使う。
            info.isDirectory = entry.directory;
            info.fileName = native_utf8(from_utf8(entry.name));
            const auto separator = static_cast<char>(fs::path::preferred_separator);
            if (!info.fileName.empty() && info.fileName.back() == separator)
                info.fileName.pop_back();
        }
        info.compressedSize = entry.lha_raw_filename ? entry.lha_compressed_size : entry.compressed_size;
        info.fileSize = entry.lha_raw_filename ? entry.lha_original_size : entry.uncompressed_size;
        info.crc = entry.lha_raw_filename ? entry.lha_crc : entry.crc32;
        info.compressType = entry.lha_method.empty() ? "-unknown-" : entry.lha_method;
        if (entry.lha_raw_filename && info.compressType != "-lhd-" &&
            info.compressType != "-lh0-" && info.compressType != "-lh5-" &&
            info.compressType != "-lh6-" && info.compressType != "-lh7-") {
            // Dは一覧の構築中に未対応の圧縮を拒否する。setProgressNum
            // や展開コールバックの実行より前に判定する。
            fail("Unsupported file is contained " + info.compressType);
        }
        info.sourceIndex = index;
        nameList.push_back(info.fileName);
        infoList.push_back(std::move(info));
    }
}

Bytes LhaFile::read(StringView name) const
{
    if (!impl_) fail("LHAリーダーが初期化されていません。");
    if (!impl_->reader) fail("LHA内にファイルがありません: " + String(name));
    // DのnameToInfoはネイティブ変換と末尾除去後の名前をキーとし、最後の
    // 代入を優先する。その窓口のキーを解決してから、対応する正確なレコードを読む。
    const auto found = std::find_if(infoList.rbegin(), infoList.rend(), [&](const auto& info) {
        return info.fileName == name;
    });
    if (found == infoList.rend()) fail("LHA内にファイルがありません: " + String(name));
    // Dは一覧に-lhd-を受け入れるが、read(name)が要求されるとLZHDecodeSession
    // で拒否する。黙って空の内容を返してはならない。
    if (found->compressType == "-lhd-") fail("Unsupported compress type");
    // Dのread(name)はisDirectoryを検査せず、対応する内容を復
    // 号する。ディレクトリを読む必要があるかは、展開の呼び出し側だけが判断する。
    return from_core(impl_->reader->read_payload(impl_->reader->entries().at(found->sourceIndex)));
}

String encodeArchiveName(String name)
{
    const auto separator = static_cast<char>(fs::path::preferred_separator);
    for (auto& character : name) {
        if (character == separator) character = '/';
    }
    return name;
}

void unzip(const fs::path& parent, const fs::path& zipPath,
           ExpandCallback expand, ProgressCallback setProgressNum,
           ProgressCallback progress)
{
    ZipArchive arc(zipPath);
    unzip(parent, arc, std::move(expand), std::move(setProgressNum), std::move(progress));
}

void unzip(const fs::path& parent, ZipArchive& arc,
           ExpandCallback expand, ProgressCallback setProgressNum,
           ProgressCallback progress)
{
    unzip(arc,
          [parent, expand = std::move(expand)](String path, const Bytes& data, bool isDir) mutable {
              if (expand) {
                  path = expand(path, isDir);
                  if (path.empty()) return;
              }
              write_extracted(parent, std::move(path), data, isDir);
          },
          std::move(setProgressNum), std::move(progress));
}

void unzip(ZipArchive& arc, FileCallback fileProc,
           ProgressCallback setProgressNum, ProgressCallback progress)
{
    if (setProgressNum) setProgressNum(progress_number(arc.directory.size()));
    std::uint32_t count = 1;
    for (const auto& member : arc.directory) {
        if (!member) continue;
        const auto name = memberName(member->rawName.empty() ? member->name : member->rawName);
        auto native = name;
        std::replace(native.begin(), native.end(), '/',
                     static_cast<char>(fs::path::preferred_separator));
        if (utils::isOuterPath(native)) continue;
        if (!name.empty() && !utils::hasParDir(native)) {
            const bool isDir = ((member->fileAttributes & 0x10U) != 0 ||
                                (!native.empty() && native.back() == static_cast<char>(fs::path::preferred_separator))) &&
                               member->expandedSize == 0;
            const auto data = arc.expand(member);
            fileProc(native, data, isDir);
        }
        if (progress) progress(count++);
    }
}

void unzip(const fs::path& parent, const ZipArchiveOwner& arc,
           ExpandCallback expand, ProgressCallback setProgressNum,
           ProgressCallback progress)
{
    if (!arc) fail("null ZIPアーカイブです。");
    unzip(parent, *arc, std::move(expand), std::move(setProgressNum), std::move(progress));
}

void unzip(const ZipArchiveOwner& arc, FileCallback fileProc,
           ProgressCallback setProgressNum, ProgressCallback progress)
{
    if (!arc) fail("null ZIPアーカイブです。");
    unzip(*arc, std::move(fileProc), std::move(setProgressNum), std::move(progress));
}

void unzip(const String& parent, const String& zipPath,
           ExpandCallback expand, ProgressCallback setProgressNum,
           ProgressCallback progress)
{
    unzip(from_utf8(parent), from_utf8(zipPath), std::move(expand),
          std::move(setProgressNum), std::move(progress));
}

ArchiveMemberPtr archive(String name, const Bytes& data, bool isDir, bool useSysEnc)
{
    if (isDir && !data.empty()) fail("not directory");
    name = encodeArchiveName(std::move(name));
    auto member = std::make_shared<ArchiveMember>();
    set_zip_time(*member, now_unix());
    // Dのarchive(name, data, isDir)は符号化済みのメンバー名を
    // 保持する。ディレクトリエントリーの命名規則はZIPコンテナーへの挿入処理で扱う。
    member->compressionMethod = CompressionMethod::deflate;
    // Dの不具合を意図的に修正する。これらはDOSのリテラルだが、DのPOSIX版fileAttributes
    // プロパティはUnixモードとして記録する。移植性のあるZIP読み込
    // みでもisDirの意味を保つため、DOSの作成元タグを維持する。
    member->fileAttributes = isDir ? 0x10U : 0x20U;
    member->internalAttributes = 1;
    set_zip_name(*member, std::move(name), useSysEnc);
    if (!isDir) {
        member->expandedData = data;
        member->expandedSize = data.size();
        member->crc32 = cw::lib::archive::crc32(to_core(data));
    }
    return member;
}

namespace {
bool source_is_directory(const fs::path& path)
{
    std::error_code error;
    const auto status = fs::status(path, error);
    if (error || !fs::exists(status)) fail("圧縮対象を確認できません: " + utf8(path));
    return fs::is_directory(status);
}

void source_attributes(ArchiveMember& member, const fs::path& path)
{
#if defined(_WIN32)
    const auto attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) fail("Unable to read source attributes: " + utf8(path));
    member.fileAttributes = attrs;
    // Dのstd.zipのfileAttributes設定に合わせ、呼び出し側のバー
    // ジョンバイトを保持し、作成元バイトでDOS/Windowsの属性形式を示す。
    member.madeVersion &= 0x00FFU;
#else
    struct stat status{};
    if (::stat(path.c_str(), &status) != 0)
        fail("Unable to read source attributes: " + utf8(path));
    // DのgetAttributesはstatに従い、特殊ファイ
    // ル種別も含む全モードビットを保持する。権限だけから通常ファ
    // イル・ディレクトリの種別を推測せず、この基本処理を使う。
    member.fileAttributes = (static_cast<std::uint32_t>(status.st_mode) & 0xffffU) << 16;
    // Dのstd.zipのfileAttributes設定に合わせ、呼び出
    // し側のバージョンバイトを保持し、作成元バイトでUnix属性を示す。
    member.madeVersion = (member.madeVersion & 0x00FFU) | 0x0300U;
#endif
}
} // namespace

ZipArchiveOwner zip(const fs::path& target, bool top, IgnorePathCallback ignorePath,
                    bool useSysEnc, std::vector<Bytes>& data)
{
    auto arc = std::make_shared<ZipArchive>();
    const auto path = utils::nabs(target);
    const bool includeTarget = top || !source_is_directory(path);
    const auto pathText = native_utf8(path);
    constexpr char separator = static_cast<char>(fs::path::preferred_separator);
    std::size_t cut;
    if (includeTarget) {
        auto parentText = native_utf8(path.parent_path());
        if (!parentText.empty() && parentText.back() != separator) parentText += separator;
        cut = parentText.size();
    } else {
        cut = pathText.size() + 1;
    }

    std::function<void(const fs::path&)> addPath;
    std::set<fs::path> activeDirectories;
    addPath = [&](const fs::path& current) {
        if (ignorePath && ignorePath(native_utf8(current))) return;
        if (source_is_directory(current)) {
            // DのisDir/getAttributesはシンボリックリンクを
            // たどるため、ここでもたどる。ただしDの無制限の再帰を繰り返さず、
            // 祖先への循環は拒否する。同じツリーへの独立したリンクは許可する。
            const auto identity = fs::canonical(current);
            if (!activeDirectories.insert(identity).second)
                fail("循環するディレクトリリンクです: " + utf8(current));
            struct LeaveDirectory {
                std::set<fs::path>& active;
                fs::path identity;
                ~LeaveDirectory() { active.erase(identity); }
            } leave{activeDirectories, identity};
            const auto children = utils::clistdir(current);
            if (!children.empty()) {
                for (const auto& child : children)
                    addPath(from_utf8(utils::buildPath(native_utf8(current), child)));
                return;
            }
        }

        auto member = std::make_shared<ArchiveMember>();
        set_zip_time(*member, file_time_unix(current));
        member->compressionMethod = CompressionMethod::deflate;
        auto name = native_utf8(current);
        if (source_is_directory(current)) name += separator;
        source_attributes(*member, current);
        member->internalAttributes = 1;
        // Dはファイルシステムのルートであっても、一度計算した切断位置で元の表記
        // を切り出す。ここをlexically_relativeで置き換えない。
        if (cut > name.size()) throw std::out_of_range("ZIP member name cut");
        set_zip_name(*member, encodeArchiveName(name.substr(cut)), useSysEnc);
        // Dはファイル内容の確保・追加より先にメタデータを読む。そのため
        // メタデータの取得失敗時は、呼び出し側のデータ集合を変更しない。
        if (!source_is_directory(current)) {
            Byte* allocation = nullptr;
            auto bytes = utils::readBinaryFrom<Byte>(current, allocation);
            data.push_back(std::move(bytes));
            // Dのデータ一覧は、管理外の読み込みバッファを後で解放するためにあ
            // る。ネイティブAPIでは所有するベクターとメンバー所有のバイト列
            // を使い、解放用一覧の消失後に内容が無効参照にならないようにする。
            member->expandedData = data.back();
            member->expandedSize = member->expandedData.size();
            member->crc32 = cw::lib::archive::crc32(to_core(member->expandedData));
        }
        arc->addMember(std::move(member));
    };

    if (includeTarget) {
        addPath(path);
    } else {
        for (const auto& child : utils::clistdir(path))
            addPath(from_utf8(utils::buildPath(pathText, child)));
    }
    return arc;
}

ZipArchiveOwner zip(const fs::path& target, bool top, const std::vector<String>& excludePath,
                    bool useSysEnc, std::vector<Bytes>& data)
{
    std::vector<String> patterns;
    const auto exact = normalized_exclusions(excludePath, patterns);
    return zip(target, top,
               [exact, patterns = std::move(patterns)](String path) {
                   return should_exclude(from_utf8(path), exact, patterns);
               }, useSysEnc, data);
}

void detail::normalizeZipExclusions(std::vector<String>& paths) {
    for (auto& path : paths) path = utils::nabs(StringView(path));
}

String memberName(String name)
{
    if (valid_utf8(name)) return name;
    // 生のメンバー名が有効なUTF-8ならそのまま返
    // す。無効な場合だけShift-JISとして復号
    // し、不正なコード単位を「__」へ置き換える。
    // 汎用バックエンドのCP437代替とは異なる。
    return sjis::touni(name, false, "__");
}

String zipHasFile(const fs::path& zipPath, StringView fileName)
{
    std::error_code error;
    if (!fs::exists(zipPath, error) || error) return {};
    try {
        ZipArchive arc(zipPath);
        for (const auto& member : arc.directory) {
            if (!member) continue;
            const auto name = memberName(member->rawName.empty() ? member->name : member->rawName);
            if (utils::cfnmatch(basename(name), fileName)) return name;
        }
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& error) {
        utils::fdebugln(error.what());
    }
    return {};
}

String zipHasFile(const String& zipPath, StringView fileName)
{
    return zipHasFile(from_utf8(zipPath), fileName);
}

void zip(const fs::path& target, const fs::path& zipPath, bool top,
         IgnorePathCallback ignorePath, bool useSysEnc,
         const lib::Owner<filesync::FileSync>& sync)
{
    std::vector<Bytes> data;
    auto arc = zip(target, top, std::move(ignorePath), useSysEnc, data);
    const auto bytes = arc->build();
    write_archive(zipPath, bytes, sync);
}

void zip(const fs::path& target, const fs::path& zipPath, bool top,
         const std::vector<String>& excludePath, bool useSysEnc,
         const lib::Owner<filesync::FileSync>& sync)
{
    std::vector<Bytes> data;
    auto arc = zip(target, top, excludePath, useSysEnc, data);
    const auto bytes = arc->build();
    write_archive(zipPath, bytes, sync);
}

void zip(const fs::path& target, const fs::path& zipPath, bool top,
         IgnorePathCallback ignorePath, bool useSysEnc, filesync::FileSync* sync)
{
    std::vector<Bytes> data;
    auto arc = zip(target, top, std::move(ignorePath), useSysEnc, data);
    const auto bytes = arc->build();
    write_archive(zipPath, bytes, sync);
}

void zip(const fs::path& target, const fs::path& zipPath, bool top,
         const std::vector<String>& excludePath, bool useSysEnc,
         filesync::FileSync* sync)
{
    std::vector<Bytes> data;
    auto arc = zip(target, top, excludePath, useSysEnc, data);
    const auto bytes = arc->build();
    write_archive(zipPath, bytes, sync);
}

void unlha(const fs::path& parent, LhaFile& arc,
           ExpandCallback expand, ProgressCallback setProgressNum,
           ProgressCallback progress)
{
    unlha(arc,
          [parent, expand = std::move(expand)](String path, const Bytes& data, bool isDir) mutable {
              if (expand) {
                  path = expand(path, isDir);
                  if (path.empty()) return;
              }
              write_extracted(parent, std::move(path), data, isDir);
          },
          std::move(setProgressNum), std::move(progress));
}

void unlha(LhaFile& arc, FileCallback fileProc,
           ProgressCallback setProgressNum, ProgressCallback progress)
{
    // Dの引数はクラス参照の値で、infoListは読み取り専用。呼
    // び出し側の可変なネイティブハンドルやベクターのイテレーターを借
    // 用せず、全コールバックの間、同じアーカイブと一覧を生存させる。
    const LhaFile retained(arc);
    if (setProgressNum) setProgressNum(progress_number(retained.infoList.size()));
    std::uint32_t count = 1;
    for (const auto& info : retained.infoList) {
        const auto& name = info.fileName;
        if (utils::isOuterPath(name)) continue;
        if (!name.empty() && !utils::hasParDir(name)) {
            const auto data = info.isDirectory ? Bytes{} : retained.read(name);
            fileProc(name, data, info.isDirectory);
        }
        if (progress) {
            progress(count);
            ++count;
        }
    }
}

void unlha(const String& parent, LhaFile& arc,
           ExpandCallback expand, ProgressCallback setProgressNum,
           ProgressCallback progress)
{
    unlha(from_utf8(parent), arc, std::move(expand), std::move(setProgressNum), std::move(progress));
}

String lhaHasFile(const fs::path& lhaPath, StringView fileName)
{
    std::error_code error;
    if (!fs::exists(lhaPath, error) || error) return {};
    try {
        LhaFile arc(lhaPath);
        for (const auto& name : arc.nameList) {
            if (utils::cfnmatch(basename(name), fileName)) return name;
        }
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& error) {
        utils::fdebugln(error.what());
    }
    return {};
}

String lhaHasFile(const String& lhaPath, StringView fileName)
{
    return lhaHasFile(from_utf8(lhaPath), fileName);
}

} // namespace cwxeditor::cwx::archive
