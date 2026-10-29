#include "cw/lib/exception_origin.hpp"
#include "cw/lib/archive.hpp"
#include "cw/lib/encoding.hpp"
#include "platform/storage/storage_mount.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <limits>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <system_error>
#include <sys/stat.h>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include <SDL3/SDL_filesystem.h>

#if defined(CARDWIRTH_STATIC_LHA_BACKEND)
#include <archive.h>
#include <archive_entry.h>
#endif

#if defined(__EMSCRIPTEN__)
#include <zlib.h>
extern "C" int cardwirth_rename_no_replace(const char*, const char*);
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#if !defined(__ANDROID__) || __ANDROID_API__ >= 28
#include <dlfcn.h>
#include <iconv.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace cw::lib::archive {

struct InternalEntry {
    Entry public_entry;
    Format format{Format::Unknown};
    std::size_t data_offset{};
    std::size_t lha_crc_offset{};
    std::optional<std::size_t> backend_index{};
    std::uint16_t zip_method{};
    std::uint16_t zip_flags{};
    std::uint64_t zip_end_offset{};
    std::size_t cab_folder{};
    std::uint64_t cab_folder_offset{};
};

struct CabFolder {
    std::uint32_t data_offset{};
    std::uint16_t type{};
    std::vector<std::vector<std::byte>> blocks;
    std::vector<std::uint16_t> block_uncompressed;
};

// Python の ZipFile と同様、圧縮アーカイブ全体を複製せず、開いたシー
// ク可能な入力を使う。 ネイティブ・MEMFS・マウント先のプロバイダーで同じ上限付
// き範囲読み取りを提供し、 下のパーサーにはプラットフォーム固有の規則を持たせない。
class ZipInput final {
public:
    explicit ZipInput(const std::filesystem::path& path) {
        if (::cardwirth::platform::is_mounted_user_path(path)) {
            const auto stat = ::cardwirth::platform::stat_mounted_user_path(path);
            if (!stat || !stat->is_file()) throw ArchiveError("アーカイブを開けません。");
            length_ = stat->size;
            mounted_ = ::cardwirth::platform::open_mounted_user_file(path);
            if (!mounted_ || !mounted_->seek(0)) throw ArchiveError("アーカイブ入力をseekできません。");
        } else {
            native_.open(path, std::ios::binary);
            if (!native_) throw ArchiveError("アーカイブを開けません。");
            native_.seekg(0, std::ios::end);
            const auto end = native_.tellg();
            if (end < 0) throw ArchiveError("アーカイブのサイズを取得できません。");
            length_ = static_cast<std::uint64_t>(end);
        }
    }
    std::uint64_t size() const noexcept { return length_; }
    std::vector<std::byte> read(std::uint64_t offset, std::uint64_t count) const {
        if (offset > length_ || count > length_ - offset ||
            count > (std::numeric_limits<std::size_t>::max)())
            throw InvalidArchive("アーカイブ入力が範囲外です。");
        std::scoped_lock lock(mutex_); // Python の _SharedFile と同様、シークと読み取りをまとめてロックする。
        if (mounted_) {
            if (!mounted_->seek(offset)) throw ArchiveError("アーカイブ入力をseekできません。");
        } else {
            if (offset > static_cast<std::uint64_t>((std::numeric_limits<std::streamoff>::max)()))
                throw InvalidArchive("アーカイブ位置が大きすぎます。");
            native_.clear();
            native_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            if (!native_) throw ArchiveError("アーカイブ入力をseekできません。");
        }
        std::vector<std::byte> result(static_cast<std::size_t>(count));
        std::size_t used = 0;
        while (used < result.size()) {
            const auto wanted = std::min<std::size_t>(65536, result.size() - used);
            std::size_t got;
            if (mounted_) {
                got = mounted_->read(std::span<std::byte>(result).subspan(used, wanted));
                if (!mounted_->good()) throw ArchiveError("アーカイブ入力を読み取れません。");
            } else {
                native_.read(reinterpret_cast<char*>(result.data() + used),
                             static_cast<std::streamsize>(wanted));
                got = static_cast<std::size_t>(native_.gcount());
                if (native_.bad()) throw ArchiveError("アーカイブ入力を読み取れません。");
            }
            if (!got || got > wanted) throw InvalidArchive("アーカイブ入力が途中で切れています。");
            used += got;
        }
        return result;
    }
private:
    std::uint64_t length_{};
    mutable std::ifstream native_;
    std::unique_ptr<::cardwirth::platform::StorageReadHandle> mounted_;
    mutable std::mutex mutex_;
};

#if defined(_WIN32)
struct CabinetDecodeSession {
    std::filesystem::path root;
    std::vector<std::filesystem::path> outputs;
    bool complete{};
    ::cw::lib::ExceptionPtr failure;
    std::mutex mutex;
    ~CabinetDecodeSession() {
        if (!root.empty()) {
            // SetupAPI は専用の展開ファイルを読み取り専用にする場合がある。 そ
            // の属性を解除するのは、自身が作った番号付きの一時展開ファイルだけとする。
            for (const auto& path : outputs) {
                const auto attributes = GetFileAttributesW(path.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES)
                    SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
            }
            std::error_code error; std::filesystem::remove_all(root, error);
        }
    }
};
#endif

struct Reader::Impl {
    std::filesystem::path source_path;
    std::vector<std::byte> data;
    std::shared_ptr<ZipInput> zip_input;
    bool exact_member_lookup{};
    bool python_duplicate_extraction{};
    std::vector<InternalEntry> records;
    std::vector<CabFolder> cab_folders;
#if defined(_WIN32)
    std::shared_ptr<CabinetDecodeSession> cabinet = std::make_shared<CabinetDecodeSession>();
#endif
};

namespace {

using Byte = std::byte;
using Bytes = std::vector<Byte>;
constexpr std::uint64_t kMaxDecodedBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxArchiveEntries = 100000;

std::vector<std::filesystem::path> runtime_library_candidates(const std::filesystem::path& name)
{
    std::vector<std::filesystem::path> result;
    if (const char* base = SDL_GetBasePath(); base && *base) {
        const std::filesystem::path executable_directory(base);
#if defined(__APPLE__)
        result.emplace_back(executable_directory / "../Frameworks" / name); // macOS のバンドル。
        result.emplace_back(executable_directory / "Frameworks" / name);    // iOS のバンドル。
#else
        result.emplace_back(executable_directory / "lib" / name);
#endif
    }
    return result;
}

#if defined(_WIN32)
void* load_runtime_library(const wchar_t* name)
{
    for (const auto& candidate : runtime_library_candidates(name)) {
        if (HMODULE module = LoadLibraryExW(candidate.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
            return reinterpret_cast<void*>(module);
    }
    return reinterpret_cast<void*>(LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
}
#else
void* load_runtime_library(const char* name)
{
    for (const auto& candidate : runtime_library_candidates(name)) {
        if (void* module = dlopen(candidate.c_str(), RTLD_LAZY | RTLD_LOCAL)) return module;
    }
    return dlopen(name, RTLD_LAZY | RTLD_LOCAL);
}
#endif

std::uint8_t u8(const Bytes& b, std::size_t p)
{
    if (p >= b.size()) throw InvalidArchive("アーカイブが途中で切れています。");
    return static_cast<std::uint8_t>(b[p]);
}

std::uint16_t le16(const Bytes& b, std::size_t p)
{
    if (p + 2 > b.size()) throw InvalidArchive("アーカイブの16ビット値が途中で切れています。");
    return static_cast<std::uint16_t>(u8(b, p) | (static_cast<std::uint16_t>(u8(b, p + 1)) << 8));
}

std::uint32_t le32(const Bytes& b, std::size_t p)
{
    if (p + 4 > b.size()) throw InvalidArchive("アーカイブの32ビット値が途中で切れています。");
    return static_cast<std::uint32_t>(u8(b, p)) |
        (static_cast<std::uint32_t>(u8(b, p + 1)) << 8) |
        (static_cast<std::uint32_t>(u8(b, p + 2)) << 16) |
        (static_cast<std::uint32_t>(u8(b, p + 3)) << 24);
}

std::uint64_t le64(const Bytes& b, std::size_t p)
{
    return static_cast<std::uint64_t>(le32(b, p)) |
        (static_cast<std::uint64_t>(le32(b, p + 4)) << 32);
}

void put16(std::ostream& out, std::uint16_t v)
{
    const char d[2]{static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff)};
    out.write(d, 2);
}

void put32(std::ostream& out, std::uint32_t v)
{
    const char d[4]{static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff),
                    static_cast<char>((v >> 16) & 0xff), static_cast<char>((v >> 24) & 0xff)};
    out.write(d, 4);
}

void put64(std::ostream& out, std::uint64_t v)
{
    put32(out, static_cast<std::uint32_t>(v));
    put32(out, static_cast<std::uint32_t>(v >> 32));
}

std::string bytes_string(const Bytes& b, std::size_t p, std::size_t n)
{
    if (p + n > b.size()) throw InvalidArchive("アーカイブの文字列が途中で切れています。");
    std::string result(n, '\0');
    for (std::size_t i = 0; i < n; ++i) result[i] = static_cast<char>(u8(b, p + i));
    return result;
}

std::string path_text(const std::filesystem::path& path)
{
    // ZIP の名前とアーカイブ API の文字列は、Windows の ANSI コードページではなく UTF-8 とする。
    const auto utf8 = path.generic_u8string();
    return std::string(utf8.begin(), utf8.end());
}

Bytes read_file(const std::filesystem::path& path)
{
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        const auto mounted = ::cardwirth::platform::read_mounted_user_file(path);
        if (!mounted) throw ArchiveError("暗号化アーカイブを開けません: " + path_text(path));
        return *mounted;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ArchiveError("アーカイブを開けません: " + path_text(path));
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < 0) throw ArchiveError("アーカイブのサイズを取得できません: " + path_text(path));
    if (static_cast<unsigned long long>(end) > static_cast<unsigned long long>((std::numeric_limits<std::size_t>::max)()))
        throw ArchiveError("アーカイブがプロセスのアドレス空間に収まりません: " + path_text(path));
    in.seekg(0, std::ios::beg);
    Bytes data(static_cast<std::size_t>(end));
    if (!data.empty()) in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!in && !data.empty()) throw ArchiveError("アーカイブを読み込めません: " + path_text(path));
    return data;
}

bool starts_with(const std::string& s, const char* p)
{
    return s.rfind(p, 0) == 0;
}

bool valid_utf8(const std::string& s)
{
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i++]);
        std::size_t continuation = 0;
        std::uint32_t value = 0;
        if (c <= 0x7f) continue;
        if ((c & 0xe0) == 0xc0) { continuation = 1; value = c & 0x1f; if (value == 0) return false; }
        else if ((c & 0xf0) == 0xe0) { continuation = 2; value = c & 0x0f; }
        else if ((c & 0xf8) == 0xf0) { continuation = 3; value = c & 0x07; }
        else return false;
        if (i + continuation > s.size()) return false;
        for (std::size_t j = 0; j < continuation; ++j) {
            const auto d = static_cast<unsigned char>(s[i++]);
            if ((d & 0xc0) != 0x80) return false;
            value = (value << 6) | (d & 0x3f);
        }
        if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ||
            (continuation == 2 && value < 0x800) || (continuation == 3 && value < 0x10000)) return false;
    }
    return true;
}

void append_utf8(std::string& out, std::uint32_t c)
{
    if (c <= 0x7f) out.push_back(static_cast<char>(c));
    else if (c <= 0x7ff) { out.push_back(static_cast<char>(0xc0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 0x3f))); }
    else if (c <= 0xffff) { out.push_back(static_cast<char>(0xe0 | (c >> 12))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3f))); out.push_back(static_cast<char>(0x80 | (c & 0x3f))); }
    else { out.push_back(static_cast<char>(0xf0 | (c >> 18))); out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3f))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3f))); out.push_back(static_cast<char>(0x80 | (c & 0x3f))); }
}

std::string cp437(const std::string& raw)
{
    static constexpr std::array<std::uint16_t, 128> table{
        0x00c7,0x00fc,0x00e9,0x00e2,0x00e4,0x00e0,0x00e5,0x00e7,0x00ea,0x00eb,0x00e8,0x00ef,0x00ee,0x00ec,0x00c4,0x00c5,
        0x00c9,0x00e6,0x00c6,0x00f4,0x00f6,0x00f2,0x00fb,0x00f9,0x00ff,0x00d6,0x00dc,0x00a2,0x00a3,0x00a5,0x20a7,0x0192,
        0x00e1,0x00ed,0x00f3,0x00fa,0x00f1,0x00d1,0x00aa,0x00ba,0x00bf,0x2310,0x00ac,0x00bd,0x00bc,0x00a1,0x00ab,0x00bb,
        0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255d,0x255c,0x255b,0x2510,
        0x2514,0x2534,0x252c,0x251c,0x2500,0x253c,0x255e,0x255f,0x255a,0x2554,0x2569,0x2566,0x2560,0x2550,0x256c,0x2567,
        0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256b,0x256a,0x2518,0x250c,0x2588,0x2584,0x258c,0x2590,0x2580,
        0x03b1,0x00df,0x0393,0x03c0,0x03a3,0x03c3,0x00b5,0x03c4,0x03a6,0x0398,0x03a9,0x03b4,0x221e,0x03c6,0x03b5,0x2229,
        0x2261,0x00b1,0x2265,0x2264,0x2320,0x2321,0x00f7,0x2248,0x00b0,0x2219,0x00b7,0x221a,0x207f,0x00b2,0x25a0,0x00a0};
    std::string out;
    for (const unsigned char c : raw) append_utf8(out, c < 0x80 ? c : table[c - 0x80]);
    return out;
}

std::string decode_name_bytes(const std::string& raw)
{
    const ::cw::lib::Bytes bytes(raw.begin(), raw.end());
    return ::cw::lib::encoding::decode_legacy_archive_name(bytes);
}

std::string normalize_name(const std::string& input, bool directory_ok)
{
    if (input.empty()) throw UnsafePath("空のアーカイブ名です。");
    if (input.find('\0') != std::string::npos) throw UnsafePath("NULを含むアーカイブ名です。");
    if (!valid_utf8(input)) throw UnsafePath("UTF-8として不正なアーカイブ名です。");
    std::string name = input;
    std::replace(name.begin(), name.end(), '\\', '/');
    const bool trailing = !name.empty() && name.back() == '/';
    while (name.size() > 1 && name.back() == '/') name.pop_back();
    if (name.empty() || name.front() == '/' || (name.size() >= 2 && name[1] == ':')) throw UnsafePath("絶対パスのアーカイブ名です: " + input);
    std::size_t begin = 0;
    while (begin <= name.size()) {
        const std::size_t end = name.find('/', begin);
        const std::string part = name.substr(begin, end == std::string::npos ? end : end - begin);
        if (part.empty() || part == "." || part == "..") throw UnsafePath("親参照を含むアーカイブ名です: " + input);
        if (part.find(':') != std::string::npos ||
            std::any_of(part.begin(), part.end(), [](unsigned char c) { return c < 0x20; }))
            throw UnsafePath("Windowsで特殊な意味を持つアーカイブ名です: " + input);
        if (part.back() == ' ' || part.back() == '.')
            throw UnsafePath("末尾が空白またはドットのアーカイブ名です: " + input);
        std::string stem = part.substr(0, part.find('.'));
        std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        const bool numbered_device = stem.size() == 4 &&
            (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) && stem[3] >= '1' && stem[3] <= '9';
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || numbered_device)
            throw UnsafePath("Windows予約デバイス名を含むアーカイブ名です: " + input);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    if (directory_ok && trailing) name.push_back('/');
    return name;
}

std::string lha_member_name(const std::string& raw, ReaderProfile profile)
{
    auto name = decode_name_bytes(raw);
    if (profile == ReaderProfile::Default) return normalize_name(name, true);
    // D Editor と Py は、出力先を選別する前に LHA の読
    // み取り専用メンバー表を調べる。 既存の LHA のスラッシュ規則
    // でその表記を維持し、一覧の名前を検証済みの出力パスとは扱わない。
    std::replace(name.begin(), name.end(), '\\', '/');
    return name;
}

// ZIP のビット 11 が未設定なら、名前は UTF-8 文字列ではなくエンコ
// ード済みのバイト列である。 デコードしたパスを同じディレクトリー逸脱・デバイス
// 名の規則で検証しつつ、書き込み用に元のバイト列を保持する。 CardWirth
// の Windows アーカイブは、CP932 のメンバー名にこの形式を使う。
std::string normalize_legacy_name(const std::string& input, const bool directory_ok)
{
    auto name = input;
    std::replace(name.begin(), name.end(), '\\', '/');
    const auto decoded = decode_name_bytes(name);
    const auto validated = normalize_name(decoded, directory_ok);
    const bool trailing = !validated.empty() && validated.back() == '/';
    while (name.size() > 1 && name.back() == '/') name.pop_back();
    if (directory_ok && trailing) name.push_back('/');
    return name;
}

// add_path() は std::filesystem から UTF-8 の名
// 前を取得する。 ZIP の言語エンコードフラグを外す場合は、単に UTF-8
// のままフラグを消さず、旧形式のバイト列にする。 明示的な add_file(...,
// false) は引き続き、呼び出し側の生の旧形式バイト列を受け付ける。
std::string encode_legacy_path_name(const std::string& utf8_name, const bool directory_ok)
{
    const auto normalized = normalize_name(utf8_name, directory_ok);
    try {
        const auto encoded = encoding::utf8_to_cp932(normalized, encoding::ErrorPolicy::Strict);
        return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    } catch (const encoding::EncodingError& error) {
        throw ArchiveError("ZIPファイル名をCP932へ変換できません: " + normalized + " (" + error.what() + ")");
    }
}

std::uint64_t dos_time(std::uint16_t date, std::uint16_t time)
{
    const unsigned year = 1980U + ((date >> 9) & 0x7fU);
    const unsigned month = (date >> 5) & 0x0fU;
    const unsigned day = date & 0x1fU;
    const unsigned hour = (time >> 11) & 0x1fU;
    const unsigned minute = (time >> 5) & 0x3fU;
    const unsigned second = (time & 0x1fU) * 2U;
    if (month == 0 || month > 12 || day == 0 || hour > 23 || minute > 59 || second > 59) return 0;
    // プラットフォームのタイムゾーン API を使わず、先発グレゴリオ暦で 1970-01-01 からの日数を求める。
    const auto y = static_cast<long long>(year) - (month <= 2 ? 1 : 0);
    const auto era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = y - era * 400;
    const auto doy = (153 * (static_cast<long long>(month) + (month > 2 ? -3 : 9)) + 2) / 5 + static_cast<long long>(day) - 1;
    const auto doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const auto days = era * 146097 + doe - 719468;
    return days >= 0 ? static_cast<std::uint64_t>(days * 86400LL + hour * 3600U + minute * 60U + second) : 0;
}

std::pair<std::uint16_t, std::uint16_t> unix_to_dos_time(std::uint64_t seconds)
{
    if (seconds == 0 || seconds > static_cast<std::uint64_t>((std::numeric_limits<std::time_t>::max)())) return {};
    const std::time_t value = static_cast<std::time_t>(seconds);
    std::tm fields{};
#if defined(_WIN32)
    if (localtime_s(&fields, &value) != 0) return {};
#else
    if (!localtime_r(&value, &fields)) return {};
#endif
    // ZIP の DOS 日時は、Python の ZipInfo.from_file
    // と compress_zip のディレクトリー用 ZipInfo
    // と同じローカル時刻を使う。 UT 追加情報は絶対 Unix 時刻な
    // ので、このタイムゾーン変換を適用しない。 Py は 1980 年よ
    // り前では ZipInfo の構築時、2107 年より後では日付項目
    // の格納時に失敗する。 ネイティブの書き込みはファイル内容を維持し、
    // 年だけを 7 ビットの 1980～2107 に収め、折り返さない。
    const int year = std::clamp(fields.tm_year + 1900, 1980, 2107);
    const auto date = static_cast<std::uint16_t>(((year - 1980) << 9) |
        ((fields.tm_mon + 1) << 5) | fields.tm_mday);
    const auto time = static_cast<std::uint16_t>((fields.tm_hour << 11) |
        (fields.tm_min << 5) | (fields.tm_sec / 2));
    return {date, time};
}

std::optional<std::int64_t> lha_dos_time(std::uint16_t date, std::uint16_t time)
{
    int year = 1980 + ((date >> 9) & 0x7f);
    int month = (date >> 5) & 15, day = date & 31;
    int hour = (time >> 11) & 31, minute = (time >> 5) & 63, second = (time & 31) * 2;
    // 参照する Py の lhafile 0.3.0 は、DOS の 6 ビ
    // ットの分を使う。 旧検証データには 0.2.2 の誤った 0x2f マ
    // スクがあるが、現在の Py が開くファイルにその旧不具合を適用しない。
    const auto calendar = std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month)} /
        std::chrono::day{static_cast<unsigned>(day)};
    if (!calendar.ok() || hour > 23 || minute > 59 || second > 59) {
        year = 1970; month = 1; day = 1; hour = minute = second = 0;
    }
    // LhaInfo.date_time はタイムゾーン情報なしのローカル時刻で、Py はそ
    // の後 timestamp() を呼ぶ。 Windows や基準時刻前の変換失敗を維持
    // し、UTC の代替値を作らない。Unix ではエラーなしで -1 を返す場合もある。
    std::tm local{};
    local.tm_year = year - 1900; local.tm_mon = month - 1; local.tm_mday = day;
    local.tm_hour = hour; local.tm_min = minute; local.tm_sec = second; local.tm_isdst = -1;
    errno = 0;
    const auto result = std::mktime(&local);
    if (result == static_cast<std::time_t>(-1) && errno != 0) return std::nullopt;
    return static_cast<std::int64_t>(result);
}

std::uint64_t current_unix_time()
{
    const auto now = std::time(nullptr);
    return now > 0 ? static_cast<std::uint64_t>(now) : 0;
}

std::uint64_t file_unix_time(const std::filesystem::path& path)
{
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        const auto status = ::cardwirth::platform::stat_mounted_user_path(path);
        if (status && status->modified > 0)
            return static_cast<std::uint64_t>(status->modified / 1000000000LL);
        return current_unix_time();
    }
    std::error_code ec;
    const auto value = std::filesystem::last_write_time(path, ec);
    if (ec) return current_unix_time();
    const auto system_value = ::cardwirth::platform::storage_file_time_to_system(value);
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(system_value.time_since_epoch()).count();
    return seconds > 0 ? static_cast<std::uint64_t>(seconds) : 0;
}

// ZipInfo.from_file と同様、ファイルのモードを維持する。 OS ごとに変えるのは
// stat の基本処理と ZIP の作成 OS 識別子だけとし、方針は全呼び出し側で共有する。
std::uint16_t zip_creator_version()
{
#if defined(_WIN32)
    return 20; // Python と同様、Windows の ZipInfo.create_system は 0 とする。
#else
    return (3U << 8) | 20U;
#endif
}

std::uint32_t zip_source_file_mode(const std::filesystem::path& path)
{
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        // 暗号化された StorageStat には権限がない。 ホストの属
        // 性を作り出さず、仮想的な通常ファイルの読み書きモードを維持する。
        return 0100666U;
    }
#if defined(_WIN32)
    struct _stat64 status{};
    const auto result = _wstat64(path.c_str(), &status);
#else
    struct stat status{};
    const auto result = ::stat(path.c_str(), &status);
#endif
    if (result != 0)
        throw std::filesystem::filesystem_error("ZIP input stat", path,
            std::error_code(errno, std::generic_category()));
    return static_cast<std::uint32_t>(status.st_mode);
}

std::uint16_t crc16_lha(const Bytes& data)
{
    std::uint16_t crc = 0;
    for (const auto byte : data) {
        crc ^= static_cast<std::uint16_t>(static_cast<std::uint8_t>(byte));
        for (int i = 0; i < 8; ++i) {
            crc = static_cast<std::uint16_t>((crc >> 1) ^ ((crc & 1U) ? 0xa001U : 0U));
        }
    }
    return crc;
}

// zlib は任意の共通バックエンドである。 実行時に解決して archive.cpp
// を自己完結させ、バックエンドがなければ BackendUnavailable とする。
#if defined(__EMSCRIPTEN__)
using ZStream = z_stream;
#else
struct ZStream {
    unsigned char* next_in{}; unsigned int avail_in{}; unsigned long total_in{};
    unsigned char* next_out{}; unsigned int avail_out{}; unsigned long total_out{};
    char* msg{}; void* state{}; void* zalloc{}; void* zfree{}; void* opaque{};
    int data_type{}; unsigned long adler{}; unsigned long reserved{};
};
#endif
using ZInflateInit = int (*)(ZStream*, int, const char*, int);
using ZDeflateInit = int (*)(ZStream*, int, int, int, int, int, const char*, int);
using ZStep = int (*)(ZStream*, int);
using ZEnd = int (*)(ZStream*);

struct ZlibApi {
    void* module{};
    ZInflateInit inflate_init2{}; ZStep inflate{}; ZEnd inflate_end{};
    ZDeflateInit deflate_init2{}; ZStep deflate{}; ZEnd deflate_end{};
    bool available{};
};

void* zsym(void* module, const char* name)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
#else
    return dlsym(module, name);
#endif
}

ZlibApi load_zlib()
{
    ZlibApi z;
#if defined(__EMSCRIPTEN__)
    // WebAssembly はホストの zlib を dlopen
    // できない。 Emscripten の zlib をモジュールへリ
    // ンクし、デスクトップの実行時読み込みと同じ C ABI を使う。
    z.inflate_init2 = &::inflateInit2_;
    z.inflate = &::inflate;
    z.inflate_end = &::inflateEnd;
    z.deflate_init2 = &::deflateInit2_;
    z.deflate = &::deflate;
    z.deflate_end = &::deflateEnd;
    z.available = true;
    return z;
#elif defined(_WIN32)
    const wchar_t* names[]{L"zlib1.dll", L"zlibwapi.dll", L"zlib.dll"};
    for (const auto name : names) if ((z.module = load_runtime_library(name))) break;
#elif defined(__APPLE__)
    const char* names[]{"libz.1.dylib", "libz.dylib"};
    for (const auto name : names) if ((z.module = load_runtime_library(name))) break;
#else
    const char* names[]{"libz.so.1", "libz.so"};
    for (const auto name : names) if ((z.module = load_runtime_library(name))) break;
#endif
    if (!z.module) return z;
    z.inflate_init2 = reinterpret_cast<ZInflateInit>(zsym(z.module, "inflateInit2_"));
    z.inflate = reinterpret_cast<ZStep>(zsym(z.module, "inflate"));
    z.inflate_end = reinterpret_cast<ZEnd>(zsym(z.module, "inflateEnd"));
    z.deflate_init2 = reinterpret_cast<ZDeflateInit>(zsym(z.module, "deflateInit2_"));
    z.deflate = reinterpret_cast<ZStep>(zsym(z.module, "deflate"));
    z.deflate_end = reinterpret_cast<ZEnd>(zsym(z.module, "deflateEnd"));
    z.available = z.inflate_init2 && z.inflate && z.inflate_end && z.deflate_init2 && z.deflate && z.deflate_end;
    return z;
}

const ZlibApi& zlib()
{
    static const ZlibApi api = load_zlib();
    return api;
}

Bytes inflate_raw(const Bytes& input, std::size_t expected, std::size_t skip = 0)
{
    const auto& z = zlib();
    if (!z.available) throw BackendUnavailable("deflate展開には実行時 zlib backend が必要です。");
    if (skip > input.size() || input.size() - skip > (std::numeric_limits<unsigned int>::max)()) throw InvalidArchive("deflate入力が大きすぎます。");
    ZStream stream{};
    if (z.inflate_init2(&stream, -15, "1.2.11", static_cast<int>(sizeof(ZStream))) != 0) throw ArchiveError("inflate初期化に失敗しました。");
    Bytes output; output.reserve(expected);
    std::array<unsigned char, 65536> chunk{};
    stream.next_in = reinterpret_cast<unsigned char*>(const_cast<Byte*>(input.data() + skip));
    stream.avail_in = static_cast<unsigned int>(input.size() - skip);
    int result = 0;
    for (;;) {
        stream.next_out = chunk.data(); stream.avail_out = static_cast<unsigned int>(chunk.size());
        // Python の zipfile と同様、Z_STREAM_END まで deflate
        // の出力を順に読む。 固定 64KiB の出力断片では、出力が残っている時の Z_FINISH
        // は正常でも Z_BUF_ERROR を返し得る。 破損と誤判定して大きい Party.wsl
        // の項目を読めなくしないよう、Z_NO_FLUSH で通常のストリーム処理を行う。
        result = z.inflate(&stream, 0 /* Z_NO_FLUSH */);
        const std::size_t produced = chunk.size() - stream.avail_out;
        output.insert(output.end(), reinterpret_cast<Byte*>(chunk.data()), reinterpret_cast<Byte*>(chunk.data() + produced));
        if (output.size() > expected + 1 || output.size() > kMaxDecodedBytes) { z.inflate_end(&stream); throw InvalidArchive("展開後サイズが上限を超えました。"); }
        if (result == 1) break; // Z_STREAM_END
        if (result != 0 || (stream.avail_in == 0 && stream.avail_out != 0)) {
            z.inflate_end(&stream);
            throw InvalidArchive("deflateデータが不完全または壊れています。");
        }
    }
    z.inflate_end(&stream);
    if (result != 1 || output.size() != expected) throw InvalidArchive("deflateデータを完全に展開できません。");
    return output;
}

std::uint32_t update_crc32(std::uint32_t crc, std::span<const Byte> bytes) noexcept
{
    for (const auto byte : bytes) {
        crc ^= static_cast<std::uint8_t>(byte);
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xedb88320U & static_cast<std::uint32_t>(-(static_cast<int>(crc & 1U))));
    }
    return crc;
}

struct ArchiveApi {
    void* module{};
    void* (*read_new)(){};
    int (*support_filter_all)(void*){};
    int (*support_format_all)(void*){};
    int (*set_options)(void*, const char*){};
    int (*open_memory)(void*, const void*, std::size_t){};
    int (*next_header)(void*, void**){};
    std::ptrdiff_t (*read_data)(void*, void*, std::size_t){};
    int (*read_free)(void*){};
    const char* (*error_string)(void*){};
    const char* (*entry_pathname)(void*){};
    long long (*entry_size)(void*){};
    long long (*entry_mtime)(void*){};
    unsigned int (*entry_filetype)(void*){};
    const char* (*entry_symlink)(void*){};
    bool available{};
};

#if defined(CARDWIRTH_STATIC_LHA_BACKEND)
std::string lha_wide_utf8(const wchar_t* wide)
{
    ::cw::lib::Bytes bytes;
    for (; *wide; ++wide) {
        const auto unit = static_cast<std::uint32_t>(*wide);
        for (std::size_t byte = 0; byte < sizeof(wchar_t); ++byte)
            bytes.push_back(static_cast<std::uint8_t>((unit >> (byte * 8)) & 0xffU));
    }
    // wchar_t はライブラリー ABI の単位であり、暗黙の OS ロケールではない。
    return encoding::decode(bytes, sizeof(wchar_t) == 2 ?
        encoding::TextEncoding::Utf16LE : encoding::TextEncoding::Utf32LE,
        {encoding::ErrorPolicy::Strict, false});
}
#endif

ArchiveApi load_archive_api()
{
    ArchiveApi api;
#if defined(_WIN32)
    const wchar_t* names[]{L"archive.dll", L"libarchive.dll"};
    for (const auto name : names) if ((api.module = load_runtime_library(name))) break;
#elif defined(__APPLE__)
    const char* names[]{"libarchive.13.dylib", "libarchive.dylib"};
    for (const auto name : names) if ((api.module = load_runtime_library(name))) break;
#else
    const char* names[]{"libarchive.so.13", "libarchive.so"};
    for (const auto name : names) if ((api.module = load_runtime_library(name))) break;
#endif
    if (!api.module) return api;
#define CW_ARCHIVE_SYMBOL(member, name) api.member = reinterpret_cast<decltype(api.member)>(zsym(api.module, name))
    CW_ARCHIVE_SYMBOL(read_new, "archive_read_new");
    CW_ARCHIVE_SYMBOL(support_filter_all, "archive_read_support_filter_all");
    CW_ARCHIVE_SYMBOL(support_format_all, "archive_read_support_format_all");
    CW_ARCHIVE_SYMBOL(set_options, "archive_read_set_options");
    CW_ARCHIVE_SYMBOL(open_memory, "archive_read_open_memory");
    CW_ARCHIVE_SYMBOL(next_header, "archive_read_next_header");
    CW_ARCHIVE_SYMBOL(read_data, "archive_read_data");
    CW_ARCHIVE_SYMBOL(read_free, "archive_read_free");
    CW_ARCHIVE_SYMBOL(error_string, "archive_error_string");
    CW_ARCHIVE_SYMBOL(entry_pathname, "archive_entry_pathname");
    CW_ARCHIVE_SYMBOL(entry_size, "archive_entry_size");
    CW_ARCHIVE_SYMBOL(entry_mtime, "archive_entry_mtime");
    CW_ARCHIVE_SYMBOL(entry_filetype, "archive_entry_filetype");
    CW_ARCHIVE_SYMBOL(entry_symlink, "archive_entry_symlink");
#undef CW_ARCHIVE_SYMBOL
    api.available = api.read_new && api.support_filter_all && api.support_format_all && api.open_memory &&
        api.next_header && api.read_data && api.read_free && api.error_string && api.entry_pathname &&
        api.entry_size && api.entry_mtime && api.entry_filetype && api.entry_symlink;
    return api;
}

const ArchiveApi& archive_api(Format format)
{
#if defined(CARDWIRTH_STATIC_LHA_BACKEND)
    if (format == Format::Lha || format == Format::Zip) {
        // Py が提供する LH5/LH6/LH7 と ZIP BZip2/LZMA
        // のデコードは、共通の固定版の静的 libarchive
        // から解決する。 ホスト DLL を使えない Web も同じ
        // とする。ZIP の索引と項目識別は Py 互換パーサーで
        // 維持し、 追加の他形式は既存の実行時バックエンドを使う。
        const auto make_static_api = [](bool zip) {
            ArchiveApi api;
            api.read_new = +[]() -> void* { return ::archive_read_new(); };
            api.support_filter_all = +[](void* a) { return ::archive_read_support_filter_none(static_cast<::archive*>(a)); };
            api.support_format_all = zip
                ? +[](void* a) { return ::archive_read_support_format_zip(static_cast<::archive*>(a)); }
                : +[](void* a) { return ::archive_read_support_format_lha(static_cast<::archive*>(a)); };
            // 固定版の LHA 接続は、Py と同様、生の名前を共通の文字コード
            // API へ渡す。Android の JNI 処理も含める。 libarchive
            // が独自に CP932 と再解釈しないようにする。
            api.set_options = nullptr;
            api.open_memory = +[](void* a, const void* bytes, std::size_t size) { return ::archive_read_open_memory(static_cast<::archive*>(a), bytes, size); };
            api.next_header = +[](void* a, void** entry) {
                ::archive_entry* native_entry = nullptr;
                const auto status = ::archive_read_next_header(static_cast<::archive*>(a), &native_entry);
                *entry = native_entry;
                return status;
            };
            api.read_data = +[](void* a, void* bytes, std::size_t size) -> std::ptrdiff_t { return ::archive_read_data(static_cast<::archive*>(a), bytes, size); };
            api.read_free = +[](void* a) { return ::archive_read_free(static_cast<::archive*>(a)); };
            api.error_string = +[](void* a) { return ::archive_error_string(static_cast<::archive*>(a)); };
            api.entry_pathname = +[](void* e) -> const char* {
                const auto* wide = ::archive_entry_pathname_w(static_cast<::archive_entry*>(e));
                if (!wide) return nullptr;
                thread_local std::string utf8;
                utf8 = lha_wide_utf8(wide);
                return utf8.c_str();
            };
            api.entry_size = +[](void* e) -> long long { return ::archive_entry_size(static_cast<::archive_entry*>(e)); };
            api.entry_mtime = +[](void* e) -> long long { return ::archive_entry_mtime(static_cast<::archive_entry*>(e)); };
            api.entry_filetype = +[](void* e) -> unsigned int { return ::archive_entry_filetype(static_cast<::archive_entry*>(e)); };
            api.entry_symlink = +[](void* e) -> const char* {
                const auto* wide = ::archive_entry_symlink_w(static_cast<::archive_entry*>(e));
                if (!wide) return nullptr;
                thread_local std::string utf8;
                utf8 = lha_wide_utf8(wide);
                return utf8.c_str();
            };
            api.available = true;
            return api;
        };
        // 同じ固定版バックエンドで、Python の ZIP BZIP2/LZMA 相当
        // の機能を提供する。 圧縮方式の依存関係は、共通のビルド接続で一度だけ設定する。
        static const ArchiveApi lha = make_static_api(false);
        static const ArchiveApi zip = make_static_api(true);
        return format == Format::Zip ? zip : lha;
    }
#else
    (void)format; // 外部の単体テスト用ホストでは、従来の接続も引き続き検証できる。
#endif
    static const ArchiveApi api = load_archive_api();
    return api;
}

class ArchiveHandle {
public:
    ArchiveHandle(const ArchiveApi& api, const Bytes& data, std::size_t skip) : m_api(api)
    {
        if (!api.available) throw BackendUnavailable("このアーカイブ形式にはlibarchive backendが必要です。");
        m_handle = api.read_new();
        if (!m_handle) throw BackendUnavailable("libarchive readerを作成できません。");
        api.support_filter_all(m_handle); api.support_format_all(m_handle);
        if (api.set_options) api.set_options(m_handle, "hdrcharset=CP932");
        if (skip > data.size() || api.open_memory(m_handle, data.data() + skip, data.size() - skip) != 0) {
            const std::string reason = api.error_string(m_handle) ? api.error_string(m_handle) : "unknown";
            api.read_free(m_handle); m_handle = nullptr;
            throw InvalidArchive("libarchiveがアーカイブを開けません: " + reason);
        }
    }
    ~ArchiveHandle() { if (m_handle) m_api.read_free(m_handle); }
    ArchiveHandle(const ArchiveHandle&) = delete;
    ArchiveHandle& operator=(const ArchiveHandle&) = delete;
    void* get() const noexcept { return m_handle; }
private:
    const ArchiveApi& m_api;
    void* m_handle{};
};

std::string unicode_extra(const Bytes& data, std::size_t p, std::size_t n, const std::string& raw)
{
    const std::size_t end = p + n;
    while (p + 4 <= end) {
        const auto id = le16(data, p), size = le16(data, p + 2); p += 4;
        if (p + size > end) throw InvalidArchive("ZIP extra fieldが途中で切れています。");
        if (id == 0x7075 && size >= 5 && u8(data, p) == 1 && le32(data, p + 1) == crc32(Bytes(reinterpret_cast<const Byte*>(raw.data()), reinterpret_cast<const Byte*>(raw.data()) + raw.size()))) {
            const auto candidate = bytes_string(data, p + 5, size - 5);
            if (valid_utf8(candidate)) return candidate;
        }
        p += size;
    }
    return {};
}

struct Zip64Values {
    std::optional<std::uint64_t> uncompressed;
    std::optional<std::uint64_t> compressed;
    std::optional<std::uint64_t> local_offset;
    std::optional<std::uint32_t> disk;
};

// Py は必須の ZIP64 サイズ・位置項目がなくても項目を公開し、読み
// 取り時だけ失敗する場合がある。 ネイティブの記録構築時には必須項目を要
// 求し、仮の 0xffffffff を有効なメタデータとして使わせない。
Zip64Values zip64_extra(const Bytes& data, std::size_t p, std::size_t n,
                        bool need_uncompressed, bool need_compressed,
                        bool need_offset, bool need_disk)
{
    const std::size_t end = p + n;
    while (p + 4 <= end) {
        const auto id = le16(data, p), size = le16(data, p + 2);
        p += 4;
        if (p + size > end) throw InvalidArchive("ZIP extra fieldが途中で切れています。");
        if (id == 0x0001) {
            const std::size_t field_end = p + size;
            Zip64Values result;
            if (need_uncompressed) { if (p + 8 > field_end) throw InvalidArchive("ZIP64非圧縮サイズがありません。"); result.uncompressed = le64(data, p); p += 8; }
            if (need_compressed) { if (p + 8 > field_end) throw InvalidArchive("ZIP64圧縮サイズがありません。"); result.compressed = le64(data, p); p += 8; }
            if (need_offset) { if (p + 8 > field_end) throw InvalidArchive("ZIP64 local offsetがありません。"); result.local_offset = le64(data, p); p += 8; }
            if (need_disk) { if (p + 4 > field_end) throw InvalidArchive("ZIP64 disk番号がありません。"); result.disk = le32(data, p); }
            return result;
        }
        p += size;
    }
    if (need_uncompressed || need_compressed || need_offset || need_disk)
        throw InvalidArchive("ZIP64 extra fieldがありません。");
    return {};
}

std::uint64_t timestamp_extra(const Bytes& data, std::size_t p, std::size_t n)
{
    const std::size_t end = p + n;
    while (p + 4 <= end) {
        const auto id = le16(data, p), size = le16(data, p + 2);
        p += 4;
        if (p + size > end) throw InvalidArchive("ZIP extra fieldが途中で切れています。");
        if (id == 0x5455 && size >= 5 && (u8(data, p) & 1U) != 0)
            return le32(data, p + 1);
        p += size;
    }
    return 0;
}

// 形式検出と共有する。Python の zipfile と同様、制限したコメント領域
// の最後の EOCD を探す。 実行ファイルの後ろに結合された ZIP も含める。
std::optional<std::size_t> find_zip_eocd(const Bytes& tail, std::size_t begin)
{
    if (tail.size() < 4) return {};
    for (std::size_t p = tail.size() - 4;; --p) {
        if (le32(tail, p) == 0x06054b50U) {
            if (tail.size() - p < 22) throw InvalidArchive("ZIP EOCD is truncated");
            return p;
        }
        if (p == begin) break;
    }
    return {};
}

std::shared_ptr<Reader::Impl> parse_zip(const std::filesystem::path& path, std::vector<Entry>& entries,
                                      const std::shared_ptr<ZipInput>& input, ReaderProfile profile)
{
    auto impl = std::make_shared<Reader::Impl>();
    impl->source_path = path;
    impl->zip_input = input;
    impl->exact_member_lookup = true;
    const auto size = input->size();
    if (size < 22) throw InvalidArchive("ZIPの終端レコードがありません。");
    // ファイル全体ではなく、EOCD の検索領域と、存在し得る ZIP64 の位置情報だけを読む。
    const auto search_size = std::min<std::uint64_t>(size, 0x10000U + 22U);
    const auto tail_size = std::min<std::uint64_t>(size, search_size + 20U);
    const auto tail = input->read(size - tail_size, tail_size);
    const auto found = find_zip_eocd(tail, static_cast<std::size_t>(tail_size - search_size));
    if (!found) throw InvalidArchive("ZIP EOCD was not found");
    const auto eocd = *found;
    // Py は単一ファイルの ZIP でもゼロ以外のディスク番号を無視す
    // る場合がある。 この Reader は一つの入力しか持たず別巻へ位
    // 置を振り分けられないため、その矛盾したヘッダーは開く時に拒否する。
    if (le16(tail, eocd + 4) != 0 || le16(tail, eocd + 6) != 0) throw InvalidArchive("分割ZIPはサポートしていません。");
    std::uint64_t count = le16(tail, eocd + 10);
    std::uint64_t cd_size = le32(tail, eocd + 12);
    std::uint64_t cd_offset = le32(tail, eocd + 16);
    // Python の zipfile と同様、65535 項目だけで ZIP64 と判断
    // せず、実際の ZIP64 位置情報を検出する。 65535 は通常 ZIP の有効な
    // 最大件数でもあり、この Writer はその件数を ZIP64 情報なしで出力する。
    const bool has_zip64_locator = eocd >= 20 && le32(tail, eocd - 20) == 0x07064b50U;
    if (has_zip64_locator) {
        if (le32(tail, eocd - 16) != 0 || le32(tail, eocd - 4) != 1)
            throw InvalidArchive("分割ZIP64はサポートしていません。");
        // zipfile._EndRecData64 と同様、位置情報の直前の固定レ
        // コードを読む。 連結・自己解凍アーカイブの未補正のオフセット値は使わない。
        const auto eocd_offset = size - tail_size + eocd;
        if (eocd_offset < 76) throw InvalidArchive("ZIP64 EOCD is truncated");
        const auto zip64_end = input->read(eocd_offset - 76, 56);
        if (le32(zip64_end, 0) != 0x06064b50U)
            throw InvalidArchive("ZIP64終端レコードが壊れています。");
        if (le32(zip64_end, 16) != 0 || le32(zip64_end, 20) != 0)
            throw InvalidArchive("分割ZIP64はサポートしていません。");
        count = le64(zip64_end, 32); cd_size = le64(zip64_end, 40); cd_offset = le64(zip64_end, 48);
    } else if (cd_size == 0xffffffffU || cd_offset == 0xffffffffU) {
        throw InvalidArchive("ZIP64 locatorがありません。");
    }
    const auto end_offset = size - tail_size + eocd;
    const auto zip64_prefix_size = has_zip64_locator ? 76ULL : 0ULL;
    if (zip64_prefix_size > end_offset || cd_size > end_offset - zip64_prefix_size)
        throw InvalidArchive("ZIP central directoryが範囲外です。");
    // zipfile と同様、ディレクトリーのバイト数と EOCD の位置から
    // 場所を求め、連結された先頭部分に応じてローカル位置を補正する。 古い件数
    // を持つ読めるアーカイブもあるため、報告された件数をループの上限にしない。
    const auto cd_begin = end_offset - zip64_prefix_size - cd_size;
    const auto data = input->read(cd_begin, cd_size);
    std::size_t p = 0;
    while (p < data.size()) {
        // 既存のネイティブのメタデータ確保上限を維持する。 信頼でき
        // ない報告件数ではなく、実際に存在するレコード数に基づく。
        if (entries.size() >= 10000000ULL) throw InvalidArchive("ZIPエントリ数が上限を超えました。");
        if (p + 46 > data.size() || le32(data, p) != 0x02014b50U) throw InvalidArchive("ZIP central directoryが壊れています。");
        // Py は宣言された展開バージョンが全体の上限を超える
        // と拒否する。 この Reader は実際の圧縮方式・
        // 暗号化・必須 ZIP64 項目で判断し、バージョンだ
        // けが高い対応済み記録は読み取れるようにする。 未対
        // 応の圧縮方式は、共通の項目読み取り境界で失敗する。
        const auto flags = le16(data, p + 8);
        const auto method = le16(data, p + 10);
        const auto crc = le32(data, p + 16);
        const auto compressed32 = le32(data, p + 20), uncompressed32 = le32(data, p + 24);
        const auto name_size = le16(data, p + 28), extra_size = le16(data, p + 30), comment_size = le16(data, p + 32);
        const auto disk32 = le16(data, p + 34);
        const auto external = le32(data, p + 38), local32 = le32(data, p + 42);
        if (p + 46ULL + name_size + extra_size + comment_size > data.size()) throw InvalidArchive("ZIP central entryが途中で切れています。");
        const std::string raw_name = bytes_string(data, p + 46, name_size);
        std::string name = (flags & 0x800) ? raw_name : decode_name_bytes(raw_name);
        // Py 3.11 は Info-ZIP 0x7075 があっても旧形式
        // の名前を維持する。 ここでは CRC 検証済みの UTF-8 パス
        // を優先し、この拡張で作られた日本語名の表記と共通の検索キーを保つ。
        if (const auto path_extra = unicode_extra(data, p + 46 + name_size, extra_size, raw_name); !path_extra.empty()) name = path_extra;
        const auto zip64 = zip64_extra(data, p + 46 + name_size, extra_size,
            uncompressed32 == 0xffffffffU, compressed32 == 0xffffffffU,
            local32 == 0xffffffffU, disk32 == 0xffffU);
        const std::uint64_t compressed = zip64.compressed.value_or(compressed32);
        const std::uint64_t uncompressed = zip64.uncompressed.value_or(uncompressed32);
        std::uint64_t local = zip64.local_offset.value_or(local32);
        if (cd_begin >= cd_offset) {
            const auto prefix = cd_begin - cd_offset;
            if (local > size || prefix > size - local) throw InvalidArchive("ZIP local offsetが範囲外です。");
            local += prefix;
        } else {
            const auto prefix = cd_offset - cd_begin;
            if (local < prefix) throw InvalidArchive("ZIP local offsetが範囲外です。");
            local -= prefix;
        }
        if (zip64.disk.value_or(disk32) != 0 || local > (std::numeric_limits<std::size_t>::max)())
            throw InvalidArchive("分割ZIPまたは大きすぎるlocal offsetです。");
        // ZipInfo と同様、空・親参照・絶対パスも読み取り専用の
        // キーとして保持する。危険な出力先は validate_extraction()
        // でだけ拒否する。 上の CW 向けの旧形式デコード・
        // Unicode 追加情報の表記も維持する。 D の Editor
        // は展開先を決める前に元の名前を調べるため、それ
        // らも読み取り用に残し、出力先検証は展開側で行う。
        std::string read_name = name;
        if (profile != ReaderProfile::Editor) {
            if (const auto nul = read_name.find('\0'); nul != std::string::npos)
                read_name.resize(nul); // ZipInfo.filename に相当する。orig_filename は元のバイト列を保持する。
            if (std::filesystem::path::preferred_separator == '\\')
                std::replace(read_name.begin(), read_name.end(), '\\', '/');
        }
        Entry entry; entry.name = std::move(read_name); entry.compressed_size = compressed; entry.uncompressed_size = uncompressed; entry.crc32 = crc;
        entry.compression = method == 0 ? Compression::Stored : (method == 8 ? Compression::Deflated : Compression::Unsupported);
        entry.encrypted = (flags & 1) != 0; entry.utf8_name = (flags & 0x800) != 0;
        const bool unix_mode = (le16(data, p + 4) >> 8) == 3;
        entry.symlink = unix_mode && ((external >> 16) & 0170000U) == 0120000U;
        entry.directory = !entry.name.empty() && entry.name.back() == '/';
        if (profile == ReaderProfile::Editor) {
            // D のディレクトリーメタデータ・サイズゼロの規則は既存の Editor 向けと
            // し、 Engine は名前に基づく ZipInfo.is_dir() に従う。
            entry.directory = (entry.directory || (unix_mode ? ((external >> 16) & 0040000U) != 0 : (external & 0x10U) != 0)) && uncompressed == 0;
        }
        entry.zip_raw_name = raw_name;
        entry.zip_external_attributes = external;
        entry.zip_internal_attributes = le16(data, p + 36);
        entry.zip_made_by = le16(data, p + 4);
        if (profile != ReaderProfile::Editor && entry.directory &&
            !entry.name.empty() && entry.name.back() != '/') entry.name.push_back('/');
        entry.mtime_unix = timestamp_extra(data, p + 46 + name_size, extra_size);
        entry.zip_dos_date = le16(data, p + 14);
        entry.zip_dos_time = le16(data, p + 12);
        if (entry.mtime_unix == 0) entry.mtime_unix = dos_time(le16(data, p + 14), le16(data, p + 12));
        InternalEntry internal{entry}; internal.format = Format::Zip; internal.data_offset = static_cast<std::size_t>(local);
        internal.zip_method = method; internal.zip_flags = flags;
        impl->records.push_back(internal); entries.push_back(entry); p += 46 + name_size + extra_size + comment_size;
    }
    // ZipFile._RealGetContents と同様、ディレクトリー項目が未整
    // 列でも、 各メンバーの終端は次のローカルヘッダーまたは中央ディレクトリーとする。
    std::vector<InternalEntry*> by_offset;
    by_offset.reserve(impl->records.size());
    for (auto& record : impl->records) by_offset.push_back(&record);
    std::stable_sort(by_offset.begin(), by_offset.end(), [](const auto* left, const auto* right) {
        return left->data_offset > right->data_offset;
    });
    auto member_end = cd_begin;
    for (auto* record : by_offset) {
        record->zip_end_offset = member_end;
        member_end = record->data_offset;
    }
    return impl;
}

std::size_t lha_start(const Bytes& data)
{
    if (data.size() >= 2 && std::isxdigit(static_cast<unsigned char>(u8(data, 0))) && std::isxdigit(static_cast<unsigned char>(u8(data, 1)))) {
        std::size_t p = 0; while (p < data.size() && std::isxdigit(static_cast<unsigned char>(u8(data, p)))) ++p;
        if (p + 2 <= data.size() && u8(data, p) == '\r' && u8(data, p + 1) == '\n') return p + 2;
    }
    return 0;
}

struct LhaHeaderMetadata {
    std::string filename;
    std::string directory;
    std::string method;
    std::uint16_t crc{};
    std::uint64_t compressed_size{};
    std::uint64_t original_size{};
};

std::vector<LhaHeaderMetadata> normalize_lha_header_checks(Bytes& data, std::size_t start,
                                                         ReaderProfile profile)
{
    std::vector<LhaHeaderMetadata> metadata;
    // lhafile はレベル 0/1 の加算値とレベル 2 の共通ヘッダ
    // ー CRC を無視するが、libarchive は不一致で項目公開前
    // に失敗する。 専用のデコード用バッファー内の、その冗長な項目だけを調
    // 整する。元ファイル、内容の CRC、サイズ、圧縮バイトは変更しない。
    // Editor の名前要素は下で先に保持し、D の NUL コメン
    // トを扱えないバックエンド向けに専用コピー内の NUL を調整する。
    const auto adapt_name = [&](std::size_t begin, std::size_t size) {
        if (profile == ReaderProfile::Editor) {
            for (std::size_t index = begin; index < begin + size; ++index)
                if (data[index] == Byte{0}) data[index] = static_cast<Byte>('_');
        }
    };
    for (std::size_t p = start; p < data.size() &&
         (profile == ReaderProfile::Editor || u8(data, p) != 0);) {
        // 元の二つの LHA 読み取り処理と同様、最
        // 後の一バイトは値を調べず終端として扱う。
        if (data.size() - p == 1) break;
        // D の is_read(26) は厳密であり、最後の一バイトだけを終端とす
        // る。 汎用・Py のゼロヘッダーによる短絡を Editor へ適用しない。
        if (profile == ReaderProfile::Editor && data.size() - p <= 26)
            throw InvalidArchive("LHA header is broken");
        if (data.size() - p < 21) throw InvalidArchive("LHA header truncated");
        const auto level = u8(data, p + 20);
        if (level > 2) return metadata; // 既存の追加バックエンドの形式を維持する。
        const std::size_t header = level == 2 ? le16(data, p) : u8(data, p) + 2;
        if (header < (level == 2 ? 26U : 22U) || header > data.size() - p)
            throw InvalidArchive("LHA header bounds");
        const std::size_t packed = le32(data, p + 7);
        LhaHeaderMetadata member;
        member.method = bytes_string(data, p + 2, 5);
        member.compressed_size = packed;
        member.original_size = le32(data, p + 11);
        if (level < 2) {
            const auto length = u8(data, p + 21);
            if (header < 24U || length > header - 24U)
                throw InvalidArchive("LHA filename bounds");
            member.filename = bytes_string(data, p + 22, length);
            member.crc = le16(data, p + 22 + length);
            adapt_name(p + 22, length);
        } else {
            member.crc = le16(data, p + 21);
        }
        // チェックサム調整と同じヘッダーを走査しながら、元のファ
        // イル名拡張情報を保持する。 ディレクトリー名の拡張情報
        // によって、空のファイル名を通常ファイルと判定しない。
        std::vector<std::size_t> crcs;
        std::size_t extension_bytes = 0;
        if (level != 0) {
            std::size_t at = level == 2 ? p + 26 : p + header;
            std::size_t extension = le16(data, level == 2 ? p + 24 : p + header - 2);
            const auto limit = level == 2 ? p + header : data.size();
            while (extension != 0) {
                if (extension < 3 || at > limit || extension > limit - at)
                    throw InvalidArchive("LHA filename extension bounds");
                if (u8(data, at) == 1) {
                    member.filename = bytes_string(data, at + 1, extension - 3);
                    adapt_name(at + 1, extension - 3);
                } else if (u8(data, at) == 2) {
                    member.directory = bytes_string(data, at + 1, extension - 3);
                    adapt_name(at + 1, extension - 3);
                }
                if (level == 2 && u8(data, at) == 0 && extension >= 5) {
                    crcs.push_back(at + 1);
                    data[at + 1] = data[at + 2] = Byte{0};
                }
                const auto next = le16(data, at + extension - 2);
                at += extension;
                extension_bytes += extension;
                extension = next;
            }
        }
        if (level == 1) {
            if (extension_bytes > packed) throw InvalidArchive("LHA extension size exceeds packed size");
            member.compressed_size -= extension_bytes;
        }
        metadata.push_back(std::move(member));
        if (level < 2) {
            unsigned sum = 0;
            for (std::size_t i = p + 2; i < p + header; ++i) sum += u8(data, i);
            data[p + 1] = static_cast<Byte>(sum & 255);
        } else {
            const auto crc = crc16_lha(Bytes(data.begin() + p, data.begin() + p + header));
            for (const auto at_crc : crcs) {
                data[at_crc] = static_cast<Byte>(crc & 255);
                data[at_crc + 1] = static_cast<Byte>(crc >> 8);
            }
        }
        // レベル 1 の圧縮サイズ項目は既に拡張ヘッダーを含む。 Python/D
        // は次のヘッダー確認に圧縮データの後の一バイトを必要とするため、内容
        // の末尾でちょうど終わる read(num) は切り詰めとして扱う。
        if (packed >= data.size() - p - header) throw InvalidArchive("LHA payload bounds");
        p += header + packed;
    }
    return metadata;
}

std::shared_ptr<Reader::Impl> parse_archive_backend(const std::filesystem::path& path,
                                                    const Bytes& source,
                                                    std::size_t start,
                                                    Format format,
                                                    std::vector<Entry>& entries,
                                                    ReaderProfile profile = ReaderProfile::Default)
{
    const auto& api = archive_api(format);
    ArchiveHandle archive(api, source, start);
    auto impl = std::make_shared<Reader::Impl>();
    impl->source_path = path;
    impl->data = source;
    for (;;) {
        void* backend_entry = nullptr;
        const int status = api.next_header(archive.get(), &backend_entry);
        if (status == 1) break;
        // Engine のシナリオインストールは Py の上限なしのメンバー一覧に
        // 従う。 汎用・Editor の索引作成は既存の安全上の上限を維持する。
        if (profile != ReaderProfile::EngineScenario && entries.size() >= kMaxArchiveEntries)
            throw InvalidArchive("アーカイブ内ファイル数が安全上限を超えました。");
        if (status < 0 || !backend_entry) {
            const std::string reason = api.error_string(archive.get()) ? api.error_string(archive.get()) : "unknown";
            throw InvalidArchive("libarchiveがheaderを読めません: " + reason);
        }
        const char* raw_path = api.entry_pathname(backend_entry);
        if (!raw_path) throw InvalidArchive("アーカイブエントリにファイル名がありません。");
        Entry entry;
        entry.name = format == Format::Lha ? lha_member_name(raw_path, profile)
                                          : normalize_name(decode_name_bytes(raw_path), true);
        const auto size = api.entry_size(backend_entry);
        if (size < 0) throw InvalidArchive("アーカイブエントリサイズが不正です: " + entry.name);
        entry.uncompressed_size = static_cast<std::uint64_t>(size);
        entry.mtime_unix = api.entry_mtime(backend_entry) > 0 ? static_cast<std::uint64_t>(api.entry_mtime(backend_entry)) : 0;
        const auto filetype = api.entry_filetype(backend_entry) & 0170000U;
        entry.directory = filetype == 0040000U;
        // Py の lhafile は UNIX リンクを「|」を含む -lhd-
        // の名前として公開する。 ここではバックエンドのリンク情報を保持し、
        // 共通の展開検証でフォルダーと誤認せずリンクを拒否できるようにする。
        entry.symlink = filetype == 0120000U || api.entry_symlink(backend_entry) != nullptr;
        if (entry.directory && entry.name.back() != '/') entry.name.push_back('/');
        entry.compression = Compression::Unsupported;
        InternalEntry internal{entry}; internal.format = format;
        internal.backend_index = impl->records.size();
        impl->records.push_back(internal); entries.push_back(entry);
    }
    if (entries.empty()) throw InvalidArchive("アーカイブエントリがありません。");
    return impl;
}

Bytes read_archive_backend(const Reader::Impl& impl, const InternalEntry& record)
{
    const auto& api = archive_api(record.format);
    ArchiveHandle archive(api, impl.data,
                          record.format == Format::Lha ? lha_start(impl.data) : 0);
    std::size_t backend_index = 0;
    for (;;) {
        void* backend_entry = nullptr;
        const int status = api.next_header(archive.get(), &backend_entry);
        if (status == 1) break;
        if (status < 0 || !backend_entry) throw InvalidArchive("libarchiveがheaderを読めません。");
        const auto current_index = backend_index++;
        if (record.backend_index) {
            // Reader::read(Entry) は、重複名も含め個
            // 別の記録を識別する。 バックエンドを開き直してもその識別
            // を保ち、同じ正規化パスの先頭メンバーを代わりに読まない。
            if (current_index != *record.backend_index) continue;
        } else {
            const char* raw_path = api.entry_pathname(backend_entry);
            if (!raw_path) continue;
            std::string candidate;
            try { candidate = normalize_name(decode_name_bytes(raw_path), true); } catch (...) { continue; }
            if (candidate != record.public_entry.name) continue;
        }
        Bytes result;
        result.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(record.public_entry.uncompressed_size, 16ULL * 1024ULL * 1024ULL)));
        std::array<Byte, 65536> buffer{};
        for (;;) {
            const auto amount = api.read_data(archive.get(), buffer.data(), buffer.size());
            if (amount == 0) break;
            if (amount < 0) {
                const std::string reason = api.error_string(archive.get()) ? api.error_string(archive.get()) : "unknown";
                throw InvalidArchive("libarchiveがdataを展開できません: " + reason);
            }
            if (static_cast<std::uint64_t>(amount) > kMaxDecodedBytes - result.size()) throw InvalidArchive("展開後サイズが上限を超えました。");
            result.insert(result.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(amount));
        }
        if (result.size() != record.public_entry.uncompressed_size) throw InvalidArchive("展開後サイズが一致しません。");
        return result;
    }
    throw InvalidArchive("libarchive内にエントリがありません: " + record.public_entry.name);
}

std::shared_ptr<Reader::Impl> parse_lha(const std::filesystem::path& path, std::vector<Entry>& entries,
                                        ReaderProfile profile)
{
    auto impl = std::make_shared<Reader::Impl>();
    impl->source_path = path;
    impl->data = read_file(path);
    const std::size_t start = lha_start(impl->data);
    if (start != 0) {
        std::size_t declared = 0;
        const auto read_limit = static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)());
        for (std::size_t i = 0; i < start - 2; ++i) {
            const auto c = static_cast<unsigned char>(u8(impl->data, i));
            const auto digit = static_cast<std::size_t>(c <= '9' ? c - '0' : std::tolower(c) - 'a' + 10);
            // Python の int は任意長の接頭辞を読めるが、file.read(num)
            // は Py_ssize_t 外のバイト数を拒否する。 乗算前に確認
            // し、長い十六進の接頭辞が小さな長さへ折り返さないようにする。
            if (declared > (read_limit - digit) / 16U)
                throw InvalidArchive("LHA preambleの長さがreadの範囲を超えました。");
            declared = declared * 16U + digit;
        }
        // Python の _LhafileWrapper と同様、最大 num
        // バイトまで読み、EOF は短い読み取りとして扱う。 ヘッダー正規
        // 化と共通デコーダーの前に実際のバッファーを制限し、Python
        // が BytesIO から除いた末尾をどちらも読まないようにする。
        impl->data.resize(start + (std::min)(declared, impl->data.size() - start));
    }
    const auto header_metadata = normalize_lha_header_checks(impl->data, start, profile);
    const auto attach_metadata = [&](auto& target_entries, auto& target_impl) {
        const auto count = (std::min)(header_metadata.size(), target_entries.size());
        for (std::size_t index = 0; index < count; ++index) {
            target_entries[index].lha_raw_filename = header_metadata[index].filename;
            target_entries[index].lha_raw_directory = header_metadata[index].directory;
            target_entries[index].lha_method = header_metadata[index].method;
            target_entries[index].lha_crc = header_metadata[index].crc;
            target_entries[index].lha_compressed_size = header_metadata[index].compressed_size;
            target_entries[index].lha_original_size = header_metadata[index].original_size;
            target_impl.records.at(index).public_entry = target_entries[index];
        }
    };
    const auto& data = impl->data;
    std::size_t p = start;
    const std::size_t end = data.size();
    while (p < end) {
        if (end - p == 1) break;
        // ゼロの終端記号の後に末尾データがある形式を受け付ける。Py の lhafile
        // 0.2.2 は拒否するが、 この共通 Reader は内容のバイト
        // を変えず末尾を無視して旧アーカイブを扱う。すべての Python/D
        // のライブラリー版との一致を意味するものではない。 十六進の枠付き
        // 入力は、上で元の read(num) の範囲へ切り詰め済みである。
        if (u8(data, p) == 0 && profile != ReaderProfile::Editor) break;
        if (p + 21 > end) throw InvalidArchive("LHAヘッダが途中で切れています。");
        if (u8(data, p + 20) != 0) {
            // 混在レベルのアーカイブではバックエンドが先頭から読み直す。
            // 部分的なレベル 0 の索引は二重に追加せず置き換える。
            std::vector<Entry> backend_entries;
            auto backend = parse_archive_backend(path, data, start, Format::Lha, backend_entries, profile);
            entries = std::move(backend_entries);
            attach_metadata(entries, *backend);
            return backend;
        }
        // レベル 0 はサイズ・加算値を除き、固定 22 バイトとファイル名からなる。
        // Py と同様、一バイトの名前、つまり 23 バイトのヘッダーも受け付ける。
        const auto header_size = u8(data, p);
        // D は空のファイル名要素を許す。 ヘッダー全体の存在を
        // 厳密に確認する処理は、上の共通走査で適用済みである。
        if (header_size < (profile == ReaderProfile::Editor ? 22 : 23) ||
            p + 2ULL + header_size > end) throw InvalidArchive("LHAヘッダが壊れています。");
        // lhafile と同様、レベル 0 のチェックサムは読み取るが検証し
        // ない。 ヘッダーの範囲と別途確認する内容 CRC は厳密に検証する。
        const std::size_t h = p + 2, method_pos = h, compressed_pos = h + 5, original_pos = h + 9, name_len_pos = h + 19;
        const auto method = bytes_string(data, method_pos, 5); const auto compressed = le32(data, compressed_pos), original = le32(data, original_pos);
        const auto name_len = u8(data, name_len_pos);
        if (h + 20ULL + name_len + 2 > p + 2ULL + header_size) throw InvalidArchive("LHAファイル名が範囲外です。");
        const std::string raw_name = bytes_string(data, h + 20, name_len); Entry entry; entry.name = lha_member_name(raw_name, profile);
        entry.compressed_size = compressed; entry.uncompressed_size = original; entry.compression = method == "-lhd-" ? Compression::Stored : (method == "-lh0-" ? Compression::LhaStored : Compression::Unsupported); entry.directory = method == "-lhd-"; entry.utf8_name = valid_utf8(raw_name);
        const auto timestamp = lha_dos_time(le16(data, h + 15), le16(data, h + 13));
        entry.mtime_unix = timestamp.value_or(0);
        entry.mtime_valid = timestamp.has_value();
        entry.mtime_conversion_failed = !timestamp;
        const std::size_t data_offset = p + 2 + header_size; if (data_offset + compressed > end) throw InvalidArchive("LHAデータが範囲外です。");
        InternalEntry internal{entry}; internal.format = Format::Lha; internal.data_offset = data_offset; internal.lha_crc_offset = h + 20 + name_len; impl->records.push_back(internal); entries.push_back(entry); p = data_offset + compressed;
    }
    // Py の lhafile は終端記号だけの LHA を開け
    // るが、後の利用側はメンバー一覧を前提とする。 項目のないア
    // ーカイブを公開せず、共通 Reader の境界で拒否する。
    if (impl->records.empty()) throw InvalidArchive("LHAエントリがありません。");
    attach_metadata(entries, *impl);
    return impl;
}

std::shared_ptr<Reader::Impl> parse_seven_zip(const std::filesystem::path& path,
                                              std::vector<Entry>& entries)
{
    const auto data = read_file(path);
    return parse_archive_backend(path, data, 0, Format::SevenZip, entries);
}

// Py の展開は、不正な DOS 日時の適用で失敗する場合がある。
// 不正・ゼロの日時はメタデータなしと扱い、展開したバイト列を利
// 用可能にする。 呼び出し側は日時を作り出さず、その設定を省く。
std::optional<std::int64_t> local_dos_time(std::uint16_t date, std::uint16_t clock)
{
    const int year = 1980 + ((date >> 9) & 127), month = (date >> 5) & 15, day = date & 31;
    const int hour = (clock >> 11) & 31, minute = (clock >> 5) & 63, second = (clock & 31) * 2;
    const auto calendar = std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month)} /
        std::chrono::day{static_cast<unsigned>(day)};
    if (!calendar.ok() || hour > 23 || minute > 59 || second > 59) return std::nullopt;
    std::tm value{}; value.tm_year = year - 1900; value.tm_mon = month - 1; value.tm_mday = day;
    value.tm_hour = hour; value.tm_min = minute; value.tm_sec = second; value.tm_isdst = -1;
    errno = 0;
    const auto result = std::mktime(&value);
    if (result == static_cast<std::time_t>(-1) && errno != 0) return std::nullopt;
    return static_cast<std::int64_t>(result);
}

std::string decode_cab_name(const std::string& raw, bool utf8)
{
    const ::cw::lib::Bytes bytes(raw.begin(), raw.end());
    return encoding::decode(bytes, utf8 ? encoding::TextEncoding::Utf8 : encoding::TextEncoding::Cp932,
                            {encoding::ErrorPolicy::Strict, false});
}

std::uint16_t cab_type(std::uint16_t type) { return type & 0x000fU; }

std::uint32_t cab_checksum(const Bytes& bytes, std::size_t offset, std::size_t size)
{
    if (offset > bytes.size() || size > bytes.size() - offset)
        throw InvalidArchive("CAB checksum範囲が不正です。");
    std::uint32_t result = 0;
    while (size >= 4) { result ^= le32(bytes, offset); offset += 4; size -= 4; }
    std::uint32_t tail = 0;
    for (std::size_t i = 0; i < size; ++i) tail |= static_cast<std::uint32_t>(u8(bytes, offset + i)) << (8 * i);
    return result ^ tail;
}

std::shared_ptr<Reader::Impl> parse_cab(const std::filesystem::path& path, std::vector<Entry>& entries)
{
    auto impl = std::make_shared<Reader::Impl>(); impl->source_path = path; impl->data = read_file(path); const auto& data = impl->data;
    if (data.size() < 36 || bytes_string(data, 0, 4) != "MSCF") throw InvalidArchive("CABヘッダが壊れています。");
    const auto cabinet_size = le32(data, 8);
    const auto files_offset = le32(data, 16);
    const auto folders = le16(data, 26);
    const auto files = le16(data, 28);
    const auto flags = le16(data, 30);
    // 空の CFFILE 表は EOF の位置から始まってもよい。 空でない表には読
    // み取り可能なバイトを要求しつつ、有効なゼロファイルの CAB は拒否しない。
    if (cabinet_size > data.size() || files_offset > data.size() ||
        (files != 0 && files_offset == data.size()) || (flags & 1) || (flags & 2))
        throw InvalidArchive("分割CABはサポートしていません。");
    std::size_t p = 36; std::uint8_t reserve_folder = 0, reserve_data = 0;
    if (flags & 4) { const auto reserve_header = le16(data, p); reserve_folder = u8(data, p + 2); reserve_data = u8(data, p + 3); p += 4 + reserve_header; }
    auto read_cstring = [&](std::size_t& at) { const auto start = at; while (at < data.size() && u8(data, at) != 0) ++at; if (at >= data.size()) throw InvalidArchive("CAB文字列が終端していません。"); const auto value = bytes_string(data, start, at - start); ++at; return value; };
    impl->cab_folders.reserve(folders);
    for (std::size_t i = 0; i < folders; ++i) { if (p + 8 > data.size()) throw InvalidArchive("CAB folderが壊れています。"); CabFolder folder; folder.data_offset = le32(data, p); folder.type = le16(data, p + 6); impl->cab_folders.push_back(folder); p += 8 + reserve_folder; }
    if (p != files_offset && p > files_offset) throw InvalidArchive("CAB file tableが不正です。");
    p = files_offset; struct CabFile { std::uint32_t size, offset; std::uint16_t folder, date, time, attr; std::string name; };
    std::vector<CabFile> cab_files;
    for (std::size_t i = 0; i < files; ++i) { if (p + 16 > data.size()) throw InvalidArchive("CAB file tableが壊れています。"); CabFile file{le32(data, p), le32(data, p + 4), le16(data, p + 8), le16(data, p + 10), le16(data, p + 12), le16(data, p + 14), {}}; p += 16; file.name = read_cstring(p); if (file.folder >= folders || file.folder >= 0xfffdu) throw InvalidArchive("CABの分割ファイルはサポートしていません。"); cab_files.push_back(std::move(file)); }
    for (std::size_t i = 0; i < folders; ++i) {
        auto& folder = impl->cab_folders[i]; std::uint64_t need = 0; for (const auto& f : cab_files) if (f.folder == i) { if (f.offset > (std::numeric_limits<std::uint64_t>::max)() - f.size) throw InvalidArchive("CAB file offsetがoverflowしました。"); need = (std::max)(need, static_cast<std::uint64_t>(f.offset) + f.size); }
        std::size_t at = folder.data_offset; std::uint64_t got = 0; while (got < need) { if (at + 8 > data.size()) throw InvalidArchive("CAB CFDATAが壊れています。"); const auto checksum = le32(data, at); const auto cb = le16(data, at + 4), ub = le16(data, at + 6); const auto checked_offset = at + 4; at += 8 + reserve_data; if (at + cb > data.size()) throw InvalidArchive("CAB CFDATAが範囲外です。"); if (checksum != 0 && cab_checksum(data, checked_offset, 4ULL + reserve_data + cb) != checksum) throw InvalidArchive("CAB CFDATA checksumが一致しません。"); folder.blocks.emplace_back(data.begin() + static_cast<std::ptrdiff_t>(at), data.begin() + static_cast<std::ptrdiff_t>(at + cb)); folder.block_uncompressed.push_back(ub); at += cb; got += ub; if (folder.blocks.size() > 100000) throw InvalidArchive("CAB block数が上限を超えました。"); }
    }
    // CFFILE の記録は DOS 属性に 0x10 があってもファイルとして扱う。
    // Python の展開処理と同様、ゼロバイトの記録も通常ファイルとして保存する。
    for (const auto& file : cab_files) { Entry entry; entry.name = normalize_name(decode_cab_name(file.name, (file.attr & 0x80U) != 0), true); entry.compressed_size = 0; entry.uncompressed_size = file.size; entry.directory = false; entry.cab_dos_attributes = file.attr; entry.cab_dos_date = file.date; entry.cab_dos_time = file.time; entry.compression = cab_type(impl->cab_folders[file.folder].type) == 0 ? Compression::Stored : (cab_type(impl->cab_folders[file.folder].type) == 1 ? Compression::MsZip : Compression::Unsupported); const auto timestamp = local_dos_time(file.date, file.time); entry.mtime_unix = timestamp.value_or(0); entry.mtime_valid = timestamp.has_value(); InternalEntry internal{entry}; internal.format = Format::Cab; internal.cab_folder = file.folder; internal.cab_folder_offset = file.offset; impl->records.push_back(internal); entries.push_back(entry); }
    return impl;
}

#if defined(_WIN32)
std::string utf8_from_wide(const wchar_t* value)
{
    if (!value || !*value) return {};
    const int length = static_cast<int>(wcslen(value));
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw InvalidArchive("CABファイル名をUTF-8へ変換できません。");
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, length, result.data(), size, nullptr, nullptr) != size)
        throw InvalidArchive("CABファイル名をUTF-8へ変換できません。");
    return result;
}

struct CabinetExtractContext {
    const Reader::Impl* impl;
    CabinetDecodeSession* session;
    std::vector<bool> selected;
    bool callback_error{};
};

UINT CALLBACK cabinet_extract_callback(void* opaque, UINT notification, UINT_PTR parameter1, UINT_PTR)
{
    auto& context = *static_cast<CabinetExtractContext*>(opaque);
    if (notification != SPFILENOTIFY_FILEINCABINET) return NO_ERROR;
    auto* info = reinterpret_cast<FILE_IN_CABINET_INFO_W*>(parameter1);
    try {
        const auto candidate = normalize_name(utf8_from_wide(info->NameInCabinet), true);
        for (std::size_t i = 0; i < context.impl->records.size(); ++i) {
            if (context.selected[i] || context.impl->records[i].public_entry.name != candidate) continue;
            // 別の選択メンバーが CAB 全体のセッションを開始した場合も、
            // read_record の共通デコーダーの確保制限を維持する。
            if (context.impl->records[i].public_entry.uncompressed_size > kMaxDecodedBytes)
                return FILEOP_SKIP;
            const auto& output = context.session->outputs[i].native();
            if (output.size() >= MAX_PATH || wcsncpy_s(info->FullTargetName, MAX_PATH, output.c_str(), _TRUNCATE) != 0)
                throw InvalidArchive("CAB temporary path is too long");
            context.selected[i] = true;
            return FILEOP_DOIT;
        }
        return FILEOP_SKIP;
    } catch (...) { context.callback_error = true; return FILEOP_ABORT; }
}

Bytes read_cab_windows(const Reader::Impl& impl, const InternalEntry& record)
{
    // SetupAPI は CAB 全体を展開する基本処理として使い、専用の番号付き一時
    // 名へ一度だけ展開する。 メンバー・パス・上書きの規則は extract で管理する
    // 。 最後の共有 Reader が破棄されるまで、セッションがファイルを所有する。
    auto& session = *impl.cabinet;
    std::lock_guard lock(session.mutex);
    if (session.failure) ::cw::lib::rethrow_exception(session.failure);
    if (!session.complete) {
        HMODULE setup{};
        try {
            using IterateCabinet = BOOL (WINAPI*)(PCWSTR, DWORD, PSP_FILE_CALLBACK_W, PVOID);
            setup = LoadLibraryExW(L"setupapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!setup) throw BackendUnavailable("setupapi.dll is unavailable");
            const auto iterate = reinterpret_cast<IterateCabinet>(GetProcAddress(setup, "SetupIterateCabinetW"));
            if (!iterate) throw BackendUnavailable("SetupIterateCabinetW is unavailable");
            wchar_t directory[MAX_PATH]{}, unique[MAX_PATH]{};
            if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"cwa", 0, unique))
                throw ArchiveError("CAB temporary directory could not be allocated");
            DeleteFileW(unique);
            session.root = unique;
            std::filesystem::create_directory(session.root);
            for (std::size_t i = 0; i < impl.records.size(); ++i)
                session.outputs.push_back(session.root / std::to_string(i));
            CabinetExtractContext context{&impl, &session, std::vector<bool>(impl.records.size())};
            const BOOL ok = iterate(impl.source_path.c_str(), 0, cabinet_extract_callback, &context);
            const auto error = GetLastError();
            FreeLibrary(setup); setup = nullptr;
            if (!ok || context.callback_error)
                throw InvalidArchive("Windows CAB decode failed (error=" + std::to_string(error) + ")");
            session.complete = true;
        } catch (...) {
            if (setup) FreeLibrary(setup);
            session.failure = ::cw::lib::capture_exception();
            throw;
        }
    }
    const auto found = std::find_if(impl.records.begin(), impl.records.end(), [&](const auto& candidate) { return &candidate == &record; });
    if (found == impl.records.end()) throw InvalidArchive("CAB member identity is invalid");
    const auto index = static_cast<std::size_t>(found - impl.records.begin());
    Bytes result = read_file(session.outputs[index]);
    if (result.size() != record.public_entry.uncompressed_size)
        throw InvalidArchive("CAB decoded size differs: " + record.public_entry.name);
    return result;
}

#endif

std::uint64_t zip_member_data_offset(const Reader::Impl& impl, const InternalEntry& record)
{
    const auto header = impl.zip_input->read(record.data_offset, 30);
    if (le32(header, 0) != 0x04034b50U) throw InvalidArchive("ZIP local header is invalid");
    const auto offset = static_cast<std::uint64_t>(record.data_offset) + 30ULL +
        le16(header, 26) + le16(header, 28);
    if (offset > impl.zip_input->size() ||
        record.public_entry.compressed_size > impl.zip_input->size() - offset)
        throw InvalidArchive("ZIP member bytes are out of bounds");
    // ZipFile.open と同様、デコード前に圧縮範囲の重なりを拒否す
    // る。 位置を確認してから減算し、符号なしの加算による桁あふれも避ける。
    if (offset > record.zip_end_offset ||
        record.public_entry.compressed_size > record.zip_end_offset - offset)
        throw InvalidArchive("ZIP member overlaps the next header or central directory");
    // ZipFile.open と同様、デコーダー構築前に全方式を検証する
    // 。 元のヘッダー名はそれぞれの CP437/UTF-8 表記で比較し
    // 、CW の公開用旧形式デコード、スラッシュ正規化、Unicode パ
    // ス追加情報から独立させる。 正規化した代替名でこの検証を回避しない。
    if (record.zip_flags & 0x60U) throw UnsupportedCompression("ZIP patched data/strong encryption is unsupported");
    const auto local_bytes = impl.zip_input->read(static_cast<std::uint64_t>(record.data_offset) + 30, le16(header, 26));
    const std::string local_raw(reinterpret_cast<const char*>(local_bytes.data()), local_bytes.size());
    const auto original_name = [](const std::string& raw, std::uint16_t flags) {
        if (flags & 0x800U) {
            if (!valid_utf8(raw)) throw InvalidArchive("ZIP local name is not UTF8");
            return raw;
        }
        return cp437(raw);
    };
    if (original_name(local_raw, le16(header, 6)) != original_name(record.public_entry.zip_raw_name, record.zip_flags))
        throw InvalidArchive("ZIP local and central names differ");
    return offset;
}

Bytes read_zip_backend(const Reader::Impl& impl, const InternalEntry& record, std::uint64_t offset)
{
    if (record.public_entry.compressed_size > kMaxDecodedBytes)
        throw InvalidArchive("ZIP backend input exceeds the native allocation limit");
    const auto compressed = impl.zip_input->read(offset, record.public_entry.compressed_size);
    // Python の header_offset による ZipInfo の選択に合わせ
    // る。 libarchive には正規の ASCII 名を付けたその項目だけを渡し、元
    // の名前で再検索しない。 重複名・CP932・Unicode 追加情報で別の記録を選
    // ばないようにし、ZipInput の開いた入力と Entry の弱所有は維持する。
    const std::string name = "member";
    const auto flags = static_cast<std::uint16_t>(record.zip_flags & ~8U);
    std::ostringstream zip(std::ios::binary);
    zip.write("PK\x03\x04", 4); put16(zip, 63); put16(zip, flags); put16(zip, record.zip_method);
    put16(zip, 0); put16(zip, 0x21); put32(zip, record.public_entry.crc32);
    put32(zip, static_cast<std::uint32_t>(compressed.size()));
    put32(zip, static_cast<std::uint32_t>(record.public_entry.uncompressed_size));
    put16(zip, static_cast<std::uint16_t>(name.size())); put16(zip, 0); zip.write(name.data(), name.size());
    if (!compressed.empty()) zip.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
    const auto central = static_cast<std::uint32_t>(zip.tellp());
    zip.write("PK\x01\x02", 4); put16(zip, 63); put16(zip, 63); put16(zip, flags); put16(zip, record.zip_method);
    put16(zip, 0); put16(zip, 0x21); put32(zip, record.public_entry.crc32);
    put32(zip, static_cast<std::uint32_t>(compressed.size()));
    put32(zip, static_cast<std::uint32_t>(record.public_entry.uncompressed_size));
    put16(zip, static_cast<std::uint16_t>(name.size())); put16(zip, 0); put16(zip, 0);
    put16(zip, 0); put16(zip, 0); put32(zip, 0); put32(zip, 0); zip.write(name.data(), name.size());
    const auto central_size = static_cast<std::uint32_t>(zip.tellp()) - central;
    zip.write("PK\x05\x06", 4); put16(zip, 0); put16(zip, 0); put16(zip, 1); put16(zip, 1);
    put32(zip, central_size); put32(zip, central); put16(zip, 0);
    const auto bytes = zip.str();
    Reader::Impl selected;
    selected.data.assign(reinterpret_cast<const Byte*>(bytes.data()),
                         reinterpret_cast<const Byte*>(bytes.data() + bytes.size()));
    auto selected_record = record; selected_record.public_entry.name = name;
    // 不正な ZIP の宣言に対しても、ネイティブの正確な展開サイズ・CRC の検証を維持する。
    // Py の ZipExtFile は切り詰めたり、短いストリームを返したりする場合がある。
    auto result = read_archive_backend(selected, selected_record);
    if (crc32(result) != record.public_entry.crc32) throw InvalidArchive("ZIP CRC mismatch");
    return result;
}

Bytes read_record(const Reader::Impl& impl, const InternalEntry& record,
                  bool directory_payload = false)
{
    if (record.public_entry.encrypted && record.format != Format::Zip)
        throw UnsupportedCompression("暗号化アーカイブエントリはサポートしていません。");
    if (record.public_entry.uncompressed_size > kMaxDecodedBytes) throw InvalidArchive("展開後サイズが上限を超えています。");
    // ZipFile.read と同様、名前が「/」で終わっていても内容のバイト列
    // を読み取る。 展開時は引き続き、ディレクトリー情報に従ってフォルダーを作る。
    if (record.public_entry.directory && record.format != Format::Zip && !directory_payload) return {};
    if (record.format == Format::Cab) {
#if defined(_WIN32)
        return read_cab_windows(impl, record);
#else
        if (record.public_entry.compression != Compression::Stored && record.public_entry.compression != Compression::MsZip) {
            throw BackendUnavailable("Quantum/LZX CABにはportable CAB backendが必要です。");
        }
        const auto& folder = impl.cab_folders.at(record.cab_folder); Bytes all;
        for (std::size_t i = 0; i < folder.blocks.size(); ++i) {
            if (folder.type == 1 && (folder.blocks[i].size() < 2 || bytes_string(folder.blocks[i], 0, 2) != "CK"))
                throw InvalidArchive("CAB MSZIPブロックのCK署名がありません。");
            Bytes part = folder.type == 0 ? folder.blocks[i] : inflate_raw(folder.blocks[i], folder.block_uncompressed[i], folder.type == 1 ? 2 : 0);
            all.insert(all.end(), part.begin(), part.end());
            if (all.size() > kMaxDecodedBytes) throw InvalidArchive("CAB展開後サイズが上限を超えました。");
        }
        if (record.cab_folder_offset + record.public_entry.uncompressed_size > all.size()) throw InvalidArchive("CABファイルが範囲外です。");
        return Bytes(all.begin() + static_cast<std::ptrdiff_t>(record.cab_folder_offset), all.begin() + static_cast<std::ptrdiff_t>(record.cab_folder_offset + record.public_entry.uncompressed_size));
#endif
    }
    if (record.format == Format::Lha) {
        if (record.public_entry.compression != Compression::Stored && record.public_entry.compression != Compression::LhaStored)
            return read_archive_backend(impl, record);
        if (record.data_offset > impl.data.size() ||
            record.public_entry.compressed_size > impl.data.size() - record.data_offset)
            throw InvalidArchive("LHAエントリが範囲外です。");
        Bytes out(impl.data.begin() + static_cast<std::ptrdiff_t>(record.data_offset), impl.data.begin() + static_cast<std::ptrdiff_t>(record.data_offset + record.public_entry.compressed_size));
        if (out.size() != record.public_entry.uncompressed_size) throw InvalidArchive("LHAのサイズが一致しません。");
        if (record.lha_crc_offset + 2 <= impl.data.size() &&
            (!record.public_entry.directory || directory_payload) &&
            le16(impl.data, record.lha_crc_offset) != crc16_lha(out))
            throw InvalidArchive("LHA CRCが一致しません。");
        return out;
    }
    if (record.format == Format::SevenZip) return read_archive_backend(impl, record);
    if (record.format == Format::Zip) {
        const auto data_offset = zip_member_data_offset(impl, record);
        // ZipFile.open と同じ順序で、中央の未対応フラグ、デコードし
        // たローカル名の一致、 パスワード・暗号化の拒否、圧縮方式の選択を行う。
        if (record.public_entry.encrypted)
            throw UnsupportedCompression("暗号化アーカイブエントリはサポートしていません。");
        if (record.zip_method == 12 || record.zip_method == 14)
            return read_zip_backend(impl, record, data_offset);
        if (record.public_entry.compression == Compression::Stored || record.public_entry.compression == Compression::Deflated) {
            Bytes compressed = impl.zip_input->read(data_offset, record.public_entry.compressed_size);
            Bytes out = record.public_entry.compression == Compression::Stored ? std::move(compressed) : inflate_raw(compressed, static_cast<std::size_t>(record.public_entry.uncompressed_size));
            if (out.size() != record.public_entry.uncompressed_size || crc32(out) != record.public_entry.crc32) throw InvalidArchive("ZIP CRCまたはサイズが一致しません。");
            return out;
        }
    }
    if (record.public_entry.compression == Compression::Unsupported) throw UnsupportedCompression("このアーカイブ圧縮方式はサポートされていません。");
    throw UnsupportedCompression("アーカイブエントリを展開できません。");
}

std::filesystem::path safe_destination(const std::filesystem::path& root, const std::string& name)
{
    const auto normalized = normalize_name(name, false);
    std::error_code ec;
    const auto absolute_root = std::filesystem::weakly_canonical(root, ec);
    if (ec) throw ArchiveError("展開先rootを正規化できません: " + path_text(root));
    const auto destination = (absolute_root / std::filesystem::u8path(normalized)).lexically_normal();
    const auto relative = destination.lexically_relative(absolute_root);
    if (relative.empty() || *relative.begin() == std::filesystem::path(".."))
        throw UnsafePath("展開先rootの外を指すパスです: " + name);
    auto current = absolute_root;
    for (const auto& component : std::filesystem::u8path(normalized)) {
        current /= component;
        const auto status = std::filesystem::symlink_status(current, ec);
        if (!ec && std::filesystem::exists(status) && std::filesystem::is_symlink(status)) throw UnsafePath("展開先にsymlinkがあります: " + path_text(current));
#if defined(_WIN32)
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            throw UnsafePath("展開先にreparse pointがあります: " + path_text(current));
#endif
        ec.clear();
    }
    return destination;
}

std::string extraction_key(const std::string& name)
{
    std::string key = name.back() == '/' ? name.substr(0, name.size() - 1) : name;
#if defined(_WIN32)
    const int wide_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, key.data(), static_cast<int>(key.size()), nullptr, 0);
    if (wide_size <= 0) throw UnsafePath("展開パスを比較用に変換できません: " + name);
    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, key.data(), static_cast<int>(key.size()), wide.data(), wide_size);
    CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
    const int byte_size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_size, nullptr, 0, nullptr, nullptr);
    key.assign(static_cast<std::size_t>(byte_size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_size, key.data(), byte_size, nullptr, nullptr);
#endif
    return key;
}

void validate_path_topology(const std::vector<Entry>& entries)
{
    // ディレクトリーは自身の項目と子孫を持てるが、通常フ
    // ァイルは別項目の親にはなれない。 ZIP のメタデ
    // ータとしては有効でもファイルシステムに表せないため
    // 、展開開始前に拒否し、先行メンバーだけを残さない。
    std::unordered_set<std::string> file_keys;
    file_keys.reserve(entries.size());
    for (const auto& entry : entries) {
        if (!entry.directory) file_keys.insert(extraction_key(entry.name));
    }
    for (const auto& entry : entries) {
        const auto key = extraction_key(entry.name);
        auto separator = key.find('/');
        while (separator != std::string::npos) {
            if (file_keys.contains(key.substr(0, separator)))
                throw UnsafePath("ファイルを親に持つ展開パスです: " + entry.name);
            separator = key.find('/', separator + 1);
        }
    }
}

void set_mtime(const std::filesystem::path& path, std::int64_t unix_seconds, bool valid = false)
{
    if ((unix_seconds == 0 && !valid) || unix_seconds > (std::numeric_limits<std::time_t>::max)() ||
        unix_seconds < (std::numeric_limits<std::time_t>::min)()) return;
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        constexpr auto unit = 1000000000LL;
        if (unix_seconds > (std::numeric_limits<std::int64_t>::max)() / unit ||
            unix_seconds < (std::numeric_limits<std::int64_t>::min)() / unit ||
            !::cardwirth::platform::set_modified_mounted_user_path(path, unix_seconds * unit))
            throw ArchiveError("展開先Storageの時刻を設定できません: " + path_text(path));
        return;
    }
    const auto target_system = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(unix_seconds));
    const auto target_file = ::cardwirth::platform::storage_file_time_from_system(
        std::chrono::time_point_cast<std::filesystem::file_time_type::duration>(target_system));
    std::error_code ec;
    std::filesystem::last_write_time(path, target_file, ec);
    if (ec) throw ArchiveError("展開先ファイルの時刻を設定できません: " + path_text(path));
}

Entry extraction_time(Entry entry, const ExtractOptions& options)
{
    if (options.local_zip_time && !entry.zip_raw_name.empty()) {
        const auto timestamp = local_dos_time(entry.zip_dos_date, entry.zip_dos_time);
        entry.mtime_unix = timestamp.value_or(0);
        entry.mtime_valid = timestamp.has_value();
    }
    return entry;
}

bool reuse_archive_time(const std::filesystem::path& path, const Entry& entry,
                        const ExtractOptions& options)
{
    if (!options.overwrite || !options.reuse_matching_time ||
        (!entry.mtime_valid && entry.mtime_unix == 0)) return false;
    std::optional<std::int64_t> current;
    if (::cardwirth::platform::is_mounted_user_path(path)) {
        const auto stat = ::cardwirth::platform::stat_mounted_user_path(path);
        if (stat && stat->is_file() && (stat->modified_valid || stat->modified != 0)) current = stat->modified;
    } else {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) return false;
        const auto time = std::filesystem::last_write_time(path, error);
        if (!error) current = ::cardwirth::platform::storage_timestamp_from_file_time(time);
    }
    // Py と同様、getmtime を厳密に比較する。LHA
    // では 1 秒の差や小数時刻も異なる。 ZIP 用に別途定義
    // した許容差を、LHA のシナリオ経路へ黙って適用しない。
    constexpr auto unit = 1000000000LL;
    if (entry.mtime_unix > (std::numeric_limits<std::int64_t>::max)() / unit ||
        entry.mtime_unix < (std::numeric_limits<std::int64_t>::min)() / unit) return false;
    return current && *current == entry.mtime_unix * unit;
}

std::filesystem::path write_temporary(const std::filesystem::path& output, const Bytes& bytes)
{
    std::random_device random;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        auto temporary = output;
        temporary += ".cw-archive-tmp-" + std::to_string(random()) + "-" + std::to_string(attempt);
#if defined(_WIN32)
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) continue;
            throw ArchiveError("展開先一時ファイルを作成できません: " + path_text(temporary));
        }
        bool written = true;
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const DWORD request = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, (std::numeric_limits<DWORD>::max)()));
            DWORD completed = 0;
            if (!WriteFile(file, bytes.data() + offset, request, &completed, nullptr) || completed != request) { written = false; break; }
            offset += completed;
        }
        if (written) written = FlushFileBuffers(file) != FALSE;
        CloseHandle(file);
        if (!written) { DeleteFileW(temporary.c_str()); throw ArchiveError("展開先一時ファイルを書き込めません: " + path_text(temporary)); }
#else
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        const int file = ::open(temporary.c_str(), flags, 0600);
        if (file < 0) {
            if (errno == EEXIST) continue;
            throw ArchiveError("展開先一時ファイルを作成できません: " + path_text(temporary));
        }
        bool written = true;
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto completed = ::write(file, bytes.data() + offset, bytes.size() - offset);
            if (completed < 0 && errno == EINTR) continue;
            if (completed <= 0) { written = false; break; }
            offset += static_cast<std::size_t>(completed);
        }
        if (written && ::fsync(file) != 0) written = false;
        if (::close(file) != 0) written = false;
        if (!written) {
            std::error_code ec;
            std::filesystem::remove(temporary, ec);
            throw ArchiveError("展開先一時ファイルを書き込めません: " + path_text(temporary));
        }
#endif
        return temporary;
    }
    throw ArchiveError("衝突しない展開先一時ファイル名を確保できません: " + path_text(output));
}

void commit_temporary(const std::filesystem::path& temporary, const std::filesystem::path& output, bool overwrite)
{
#if defined(_WIN32)
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(temporary.c_str(), output.c_str(), flags)) {
        DeleteFileW(temporary.c_str());
        throw ArchiveError("展開先ファイルを確定できません: " + path_text(output));
    }
#else
    std::error_code ec;
    if (!overwrite) {
#if defined(__EMSCRIPTEN__)
        // WebStorage は POSIX のファイルシステムではなく、/user
        // のファイルはハードリンクで公開できない。 プラットフォームの Storage
        // が使う、Web Storage の置き換えなしの基本操作を再利用する。
        const int result = cardwirth_rename_no_replace(
            temporary.c_str(), output.c_str());
        if (result != 0) {
            std::filesystem::remove(temporary, ec);
            if (result == EEXIST)
                throw ArchiveError("展開先ファイルが既に存在します: " + path_text(output));
            throw ArchiveError("展開先ファイルを確定できません: " + path_text(output));
        }
        return;
#elif defined(__ANDROID__) && defined(SYS_renameat2)
        // Android のアプリ専用ストレージは、同じディレクトリー内の名前変
        // 更ができてもハードリンクを拒否する場合がある。 link()+unlink()
        // ではなく、カーネルの置き換えなしの名前変更を使って公開する。
        constexpr unsigned int kRenameNoReplace = 1U;
        if (::syscall(SYS_renameat2, AT_FDCWD, temporary.c_str(), AT_FDCWD,
                      output.c_str(), kRenameNoReplace) == 0) return;
        int error = errno;
        if (error == ENOSYS || error == EINVAL || error == EOPNOTSUPP) {
            // 古いカーネルには renameat2 がない場合がある。 対応し
            // ていれば、従来の不可分なハードリンク経路を代替として維持する。
            if (::link(temporary.c_str(), output.c_str()) == 0) {
                std::filesystem::remove(temporary, ec);
                if (ec) throw ArchiveError("展開先一時ファイルを削除できません: " +
                                           path_text(temporary));
                return;
            }
            error = errno;
        }
        std::filesystem::remove(temporary, ec);
        if (error == EEXIST)
            throw ArchiveError("展開先ファイルが既に存在します: " + path_text(output));
        throw ArchiveError("展開先ファイルを確定できません (" +
                           std::error_code(error, std::generic_category()).message() +
                           "): " + path_text(output));
#else
        // link() は同じファイルシステム内で、置き換えなしの不可分な公開を行う。
        // exists()+rename と異なり、同時に作られた出力先を上書きしない。
        if (::link(temporary.c_str(), output.c_str()) != 0) {
            const int error = errno;
            std::filesystem::remove(temporary, ec);
            if (error == EEXIST)
                throw ArchiveError("展開先ファイルが既に存在します: " + path_text(output));
            throw ArchiveError("展開先ファイルを確定できません: " + path_text(output));
        }
        std::filesystem::remove(temporary, ec);
        if (ec) throw ArchiveError("展開先一時ファイルを削除できません: " + path_text(temporary));
        return;
#endif
    }
    std::filesystem::rename(temporary, output, ec);
    if (ec) { std::filesystem::remove(temporary, ec); throw ArchiveError("展開先ファイルを確定できません: " + path_text(output)); }
#endif
}

std::string archive_name(const std::string& name, bool directory)
{
    return normalize_name(name, directory);
}

template<class Write>
void write_with_recovery(const std::filesystem::path& output, const ExtractOptions& options, Write&& write)
{
    try { write(); }
    catch (...) {
        if (!options.overwrite || !options.recover_overwrite) throw;
        options.recover_overwrite(output);
        write();
    }
}

} // namespace

#if defined(CARDWIRTH_STATIC_LHA_BACKEND)
// 固定版デコーダー用の非公開 C 境界。アーカイブ共通のファイル名 API を再利用
// し、 プラットフォームのコードページ処理は encoding.cpp に限定する。
extern "C" const wchar_t* cardwirth_lha_name_wide(const char* raw, std::size_t size,
                                               std::size_t* length) noexcept
{
    try {
        if (!raw && size != 0) return nullptr;
        const auto utf8 = decode_name_bytes(size == 0 ? std::string{} : std::string(raw, size));
        if (utf8.find('\0') != std::string::npos) return nullptr;
        const auto bytes = encoding::encode(utf8, sizeof(wchar_t) == 2 ?
            encoding::TextEncoding::Utf16LE : encoding::TextEncoding::Utf32LE);
        thread_local std::wstring wide;
        wide.resize(bytes.size() / sizeof(wchar_t));
        for (std::size_t i = 0; i < wide.size(); ++i) {
            std::uint32_t unit = 0;
            for (std::size_t byte = 0; byte < sizeof(wchar_t); ++byte)
                unit |= static_cast<std::uint32_t>(bytes[i * sizeof(wchar_t) + byte]) << (byte * 8);
            wide[i] = static_cast<wchar_t>(unit);
        }
        *length = wide.size();
        return wide.c_str(); // デコーダーは次に呼び出す前に値をコピーする。
    } catch (...) {
        return nullptr; // C デコーダーの境界を C++ 例外が越えないようにする。
    }
}
#endif

std::uint32_t crc32(const std::vector<std::byte>& data) noexcept
{
    return ~update_crc32(0xffffffffU, data);
}

bool is_safe_relative_path(const std::string& name) noexcept
{
    try { (void)normalize_name(name, true); return true; } catch (...) { return false; }
}

void validate_destination_root(const std::filesystem::path& root)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(root, error);
    if (error && error != std::errc::no_such_file_or_directory)
        throw ArchiveError("展開先rootを確認できません: " + path_text(root));
    if (std::filesystem::is_symlink(status))
        throw UnsafePath("展開先rootにsymlinkがあります: " + path_text(root));
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(root.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        throw UnsafePath("展開先rootにreparse pointがあります: " + path_text(root));
#endif
}

std::filesystem::path checked_destination(const std::filesystem::path& root, const std::string& name)
{
    validate_destination_root(root);
    return safe_destination(root, name);
}

std::uint16_t cabinet_file_count(const std::filesystem::path& path)
{
    ZipInput input(path);
    const auto header = input.read(0, 36);
    if (bytes_string(header, 0, 4) != "MSCF") throw InvalidArchive("CAB header is invalid");
    // cFiles は符号なしで維持する。Python の符号付きの読み取りとは異な
    // り、 既存の拡張として 32768～65535 項目の有効な CAB も扱う。
    return le16(header, 28);
}

void visit_cabinet_names(const std::filesystem::path& path,
                         const std::function<bool(const std::string&)>& visitor)
{
    visit_cabinet_name_records(path, [&](const std::string_view raw, const bool utf8) {
        // Engine アダプターの既存の厳密な UTF-8/CP932 デコー
        // ド方針を維持する。 Editor は低水準のレコード形式を別に使う。
        return visitor(decode_cab_name(std::string(raw), utf8));
    });
}

void visit_cabinet_name_records(
    const std::filesystem::path& path,
    const std::function<bool(std::string_view, bool)>& visitor)
{
    ZipInput input(path);
    const auto header = input.read(0, 36);
    if (bytes_string(header, 0, 4) != "MSCF") throw InvalidArchive("CAB header is invalid");
    std::uint64_t position = le32(header, 16);
    Bytes buffer; std::size_t cursor = 0;
    const auto next = [&]() -> Byte {
        if (cursor == buffer.size()) {
            if (position >= input.size()) throw InvalidArchive("CAB file table is truncated or unterminated");
            buffer = input.read(position, std::min<std::uint64_t>(4096, input.size() - position));
            position += buffer.size(); cursor = 0;
        }
        return buffer[cursor++];
    };
    for (std::size_t i = 0; i < le16(header, 28); ++i) {
        Bytes fixed(16); for (auto& byte : fixed) byte = next();
        std::string raw;
        for (;;) { const auto byte = next(); if (byte == Byte{}) break; raw.push_back(static_cast<char>(byte)); }
        // Py と同様、CFDATA を読む前に各名前を順にデコードする。 不正な
        // NUL 終端による無限ループは再現せず、有限の EOF 判定を維持する。
        const bool utf8 = (le16(fixed, 14) & 0x80U) != 0;
        if (!visitor(raw, utf8)) return;
    }
}

std::string decode_legacy_filename(const std::vector<std::byte>& raw)
{
    return decode_name_bytes(std::string(reinterpret_cast<const char*>(raw.data()), raw.size()));
}

static Format detect_input_format(const std::shared_ptr<ZipInput>& input)
{
    // 形式検出には識別子だけを読み、アーカイブ全体のコピーは作らない。
    const auto data = input->read(0, std::min<std::uint64_t>(input->size(), 7));
    if (data.size() >= 4 && bytes_string(data, 0, 4) == "MSCF") return Format::Cab;
    if ((data.size() >= 4 && le32(data, 0) == 0x04034b50U) || (data.size() >= 4 && le32(data, 0) == 0x06054b50U)) return Format::Zip;
    static constexpr std::array<std::uint8_t, 6> seven_zip_signature{0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c};
    if (data.size() >= seven_zip_signature.size() &&
        std::equal(seven_zip_signature.begin(), seven_zip_signature.end(), data.begin(),
                   [](std::uint8_t expected, Byte actual) {
                       return expected == static_cast<std::uint8_t>(actual);
                   })) return Format::SevenZip;
    std::uint64_t start = 0;
    // 長い接頭辞も含め、既存の LHA の十六進接頭辞の規則を維
    // 持する。 一度に保持するのは上限付きの一領域だけとする。
    if (data.size() >= 2 && std::isxdigit(static_cast<unsigned char>(u8(data, 0))) &&
        std::isxdigit(static_cast<unsigned char>(u8(data, 1)))) {
        std::uint64_t position = 0;
        bool non_hex = false;
        while (position < input->size() && !non_hex) {
            const auto window = input->read(position, std::min<std::uint64_t>(4096, input->size() - position));
            for (const auto byte : window) {
                if (!std::isxdigit(static_cast<unsigned char>(byte))) { non_hex = true; break; }
                ++position;
            }
        }
        if (input->size() - position >= 2) {
            const auto newline = input->read(position, 2);
            if (u8(newline, 0) == '\r' && u8(newline, 1) == '\n') start = position + 2;
        }
    }
    if (input->size() - start >= 7) {
        const auto header = start == 0 ? data : input->read(start, 7);
        if (u8(header, 2) == '-') return Format::Lha;
    }
    const auto tail_size = std::min<std::uint64_t>(input->size(), 0x10000U + 22U);
    if (find_zip_eocd(input->read(input->size() - tail_size, tail_size), 0)) return Format::Zip;
    return Format::Unknown;
}

Format detect_format(const std::filesystem::path& path)
{
    return detect_input_format(std::make_shared<ZipInput>(path));
}

Reader::Reader(const std::filesystem::path& path)
    : Reader(open(path)) {}

Reader::Reader(std::filesystem::path path, Format format, std::vector<Entry> entries, std::shared_ptr<const Impl> impl)
    : m_path(std::move(path)), m_format(format), m_entries(std::move(entries)), m_impl(std::move(impl))
{
    for (std::size_t index = 0; index < m_entries.size(); ++index) {
        m_entries[index].source_ = m_impl;
        m_entries[index].record_index_ = index;
    }
}

// Py は拡張子が .lzh なら LHA、それ以外は ZIP を選ぶ。共通 Reader
// ではバイト列の形式を検出し、 名前を変えた ZIP/LHA も読み取り可能な
// 形式を保ち、追加の CAB/7Z も同じ公開生成処理を使う。 そのため、識別子
// の破損は Py の項目読み取りより早い、open 時点で失敗する場合がある。
Reader Reader::open(const std::filesystem::path& path, ReaderProfile profile)
{
    auto input = std::make_shared<ZipInput>(path);
    const auto format = detect_input_format(input); std::vector<Entry> entries; std::shared_ptr<Impl> impl;
    if (format == Format::Zip) impl = parse_zip(path, entries, input, profile); else if (format == Format::Lha) impl = parse_lha(path, entries, profile); else if (format == Format::SevenZip) impl = parse_seven_zip(path, entries); else if (format == Format::Cab) impl = parse_cab(path, entries); else throw InvalidArchive("対応していないアーカイブ形式です: " + path_text(path));
    // Py の ZIP/LHA は重複する名前を順に展開し、文字列名で
    // 最後の記録を読む。 D・汎用の展開規則は維持し、EngineScenario
    // の既存プロファイルではその Py の動作に従う。
    impl->python_duplicate_extraction = profile == ReaderProfile::EngineScenario &&
        (format == Format::Zip || format == Format::Lha);
    return Reader(path, format, std::move(entries), std::move(impl));
}

Reader::Reader(Reader&&) noexcept = default;
Reader& Reader::operator=(Reader&&) noexcept = default;
Reader::~Reader() = default;

// Py の LHA は生の逆スラッシュのキーを公開するが、ネイティブの LHA・他形式
// はスラッシュへ正規化した共通キーを使う。 ZIP は D Editor の元の表記も
// 含め、プロファイル別の読み取り専用名を一覧・検索で維持する。 他の ZIP プロファ
// イルは、Windows だけの区切り変換を除き ZipInfo の正確なキーを使う。
bool Reader::contains(const std::string& name) const
{
    if (m_impl->exact_member_lookup)
        return std::any_of(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.name == name; });
    const auto normalized = archive_name(name, false); return std::any_of(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.name == normalized || (e.directory && e.name == normalized + "/"); });
}

std::vector<std::byte> Reader::read(const std::string& name) const
{
    if (m_impl->exact_member_lookup) {
        // 名前で索引を付けた読み取り専用表は、同じキーの最後の項目を選ぶ。 read(Entry)
        // は引き続き、その項目自身のローカル記録を選ぶ。
        const auto it = std::find_if(m_entries.rbegin(), m_entries.rend(), [&](const Entry& e) { return e.name == name; });
        if (it == m_entries.rend()) throw ArchiveError("アーカイブ内にファイルがありません: " + name);
        return read(*it);
    }
    if (m_impl->python_duplicate_extraction) {
        const auto normalized = archive_name(name, false);
        const auto it = std::find_if(m_entries.rbegin(), m_entries.rend(), [&](const Entry& e) {
            return e.name == normalized || (e.directory && e.name == normalized + "/");
        });
        if (it == m_entries.rend()) throw ArchiveError("アーカイブ内にファイルがありません: " + name);
        return read(*it);
    }
    const auto normalized = archive_name(name, false); const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.name == normalized || (e.directory && e.name == normalized + "/"); });
    if (it == m_entries.end()) throw ArchiveError("アーカイブ内にファイルがありません: " + name);
    return read(*it);
}

std::vector<std::byte> Reader::read(const Entry& entry) const
{
    return read_entry(entry, false);
}

std::vector<std::byte> Reader::read_payload(const Entry& entry) const
{
    return read_entry(entry, true);
}

std::vector<std::byte> Reader::read_entry(const Entry& entry, bool directory_payload) const
{
    const auto source = entry.source_.lock();
    if (!source || source.get() != m_impl.get() || !entry.record_index_ ||
        *entry.record_index_ >= m_entries.size())
        throw ArchiveError("Readerに属さないEntryです。");
    // Python の ZipInfo はファイル位置を公開するが、ネイティブの
    // Entry は開いた Reader に属する。 その所有境界を維持し、コ
    // ピーや Reader の移動でも内容を保持せず正確なメンバーを識別する。
    return read_record(*m_impl, m_impl->records.at(*entry.record_index_), directory_payload);
}

void Reader::validate_extraction(const ExtractOptions& options) const
{
    if (m_entries.size() > options.max_files) throw ArchiveError("アーカイブ内ファイル数が上限を超えました。");
    std::uint64_t total = 0;
    std::unordered_map<std::string, const Entry*> seen;
    std::vector<Entry> accepted_entries;
    for (const auto& entry : m_entries) {
        // ZipInfo が読み取り専用キーの NUL 以降を切り落としても、既存の展
        // 開検証は維持する。 元の ZIP バイト列で危険なパスを隠せないようにする。
        if (entry.name.find('\0') != std::string::npos ||
            (m_format == Format::Zip && entry.zip_raw_name.find('\0') != std::string::npos))
            throw UnsafePath("NULを含むアーカイブ名です。");
        if (!is_safe_relative_path(entry.name)) {
            if (options.skip_unsafe_paths) continue;
            throw UnsafePath("危険なアーカイブパスです: " + entry.name);
        }
        if (entry.symlink) {
            if (options.reject_symlinks) throw UnsafePath("symlinkエントリは展開できません: " + entry.name);
            throw UnsupportedCompression("symlink生成を許可するportable filesystem backendがありません: " + entry.name);
        }
        if (entry.uncompressed_size > options.max_file_size ||
            entry.uncompressed_size > options.max_total_size ||
            total > options.max_total_size - entry.uncompressed_size)
            throw ArchiveError("アーカイブ展開サイズが上限を超えました。");
        const auto [first, unique] = seen.emplace(extraction_key(entry.name), &entry);
        // Py と同様、同名や大文字小文字を区別しないホストの別表記も含め
        // 、重複する出力先を上書きする。 危険なパス・シンボリックリンク・
        // ファイルとディレクトリーの衝突は、書き込み前に引き続き拒否する。
        if (!unique && !(m_impl->python_duplicate_extraction &&
                first->second->directory == entry.directory))
            throw UnsafePath("重複する展開パスです: " + entry.name);
        if (!entry.directory) total += entry.uncompressed_size;
        if (options.skip_unsafe_paths) accepted_entries.push_back(entry);
    }
    validate_path_topology(options.skip_unsafe_paths ? accepted_entries : m_entries);
}

std::filesystem::path Reader::extract(const std::filesystem::path& destination, const ExtractOptions& options) const
{
    validate_extraction(options);
    if (::cardwirth::platform::is_mounted_user_path(destination)) {
        const auto root = destination.lexically_normal();
        if (!options.lazy_destination && !::cardwirth::platform::make_mounted_user_directory(root, true))
            throw ArchiveError("暗号化展開先rootを作成できません: " + path_text(root));
        std::uint64_t total = 0;
        std::unordered_set<std::string> seen;
        for (std::size_t i = 0; i < m_entries.size(); ++i) {
            if (options.cancel_before_entry && options.cancel_before_entry(i)) return {};
            const auto entry = extraction_time(m_entries[i], options);
            if (!is_safe_relative_path(entry.name)) {
                if (options.skip_unsafe_paths) continue;
                throw UnsafePath("危険なアーカイブパスです: " + entry.name);
            }
            if (entry.symlink) {
                if (options.reject_symlinks) throw UnsafePath("symlinkエントリは展開できません: " + entry.name);
                throw UnsupportedCompression("暗号化Storageにはsymlinkを生成できません: " + entry.name);
            }
            if (entry.uncompressed_size > options.max_file_size ||
                entry.uncompressed_size > options.max_total_size ||
                total > options.max_total_size - entry.uncompressed_size)
                throw ArchiveError("アーカイブ展開サイズが上限を超えました。");
            const auto key = extraction_key(entry.name);
            const bool repeated = !seen.insert(key).second;
            if (repeated && !m_impl->python_duplicate_extraction)
                throw UnsafePath("重複する展開パスです: " + entry.name);
            const auto normalized = normalize_name(entry.name, entry.directory);
            const auto out = (root / std::filesystem::u8path(normalized)).lexically_normal();
            const auto relative = out.lexically_relative(root);
            if (relative.empty() || relative.is_absolute() ||
                std::find(relative.begin(), relative.end(), std::filesystem::path("..")) != relative.end())
                throw UnsafePath("展開先rootの外を指すパスです: " + entry.name);
            if (entry.directory) {
                if (!::cardwirth::platform::make_mounted_user_directory(out, true))
                    throw ArchiveError("暗号化展開先ディレクトリを作成できません: " + path_text(out));
                continue;
            }
            // Py と同様、時刻変換・読み取りより先に親を作成する。 ルート
            // の遅延確保によって、この処理を読み取りエラーより後へ移さない。
            if (!::cardwirth::platform::make_mounted_user_directory(out.parent_path(), true))
                throw ArchiveError("展開先親ディレクトリを作成できません: " + path_text(out.parent_path()));
            if (entry.mtime_conversion_failed) throw ArchiveError("LHAファイル時刻を変換できません。");
            const auto status = ::cardwirth::platform::stat_mounted_user_path(out);
            if (status && status->exists() && !options.overwrite && !(repeated && m_impl->python_duplicate_extraction))
                throw ArchiveError("展開先ファイルが既に存在します: " + path_text(out));
            if (reuse_archive_time(out, entry, options)) continue;
            const auto bytes = m_impl->python_duplicate_extraction
                ? read(entry.name) : read_record(*m_impl, m_impl->records[i]);
            // 重複名の文字列読み取りでは、より大きい最後の記録が選ばれる場合
            // がある。 書き込み前に、展開済みバイト数へ同じ上限を適用する。
            if (bytes.size() > options.max_file_size || bytes.size() > options.max_total_size ||
                total > options.max_total_size - bytes.size())
                throw ArchiveError("アーカイブ展開サイズが上限を超えました。");
            write_with_recovery(out, options, [&] {
                if (!::cardwirth::platform::write_mounted_user_file(out, bytes,
                        !(options.overwrite && options.recover_overwrite)))
                    throw ArchiveError("暗号化展開先ファイルを書き込めません: " + path_text(out));
            });
            set_mtime(out, entry.mtime_unix, entry.mtime_valid);
            total += bytes.size();
        }
        return destination;
    }
    validate_destination_root(destination);
    std::error_code ec;
    if (!options.lazy_destination) {
        std::filesystem::create_directories(destination, ec); if (ec) throw ArchiveError("展開先を作成できません: " + path_text(destination));
        const auto destination_status = std::filesystem::symlink_status(destination, ec);
        if (ec || std::filesystem::is_symlink(destination_status)) throw UnsafePath("展開先rootにsymlinkがあります: " + path_text(destination));
    }
    std::uint64_t total = 0; std::unordered_set<std::string> seen;
    std::vector<std::tuple<std::filesystem::path, std::int64_t, bool>> directories;
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        if (options.cancel_before_entry && options.cancel_before_entry(i)) return {};
        const auto entry = extraction_time(m_entries[i], options);
        if (!is_safe_relative_path(entry.name)) {
            if (options.skip_unsafe_paths) continue;
            throw UnsafePath("危険なアーカイブパスです: " + entry.name);
        }
        if (entry.symlink) {
            if (options.reject_symlinks) throw UnsafePath("symlinkエントリは展開できません: " + entry.name);
            throw UnsupportedCompression("symlink生成を許可するportable filesystem backendがありません: " + entry.name);
        }
        if (entry.uncompressed_size > options.max_file_size || entry.uncompressed_size > options.max_total_size || total > options.max_total_size - entry.uncompressed_size) throw ArchiveError("アーカイブ展開サイズが上限を超えました。");
        const auto key = extraction_key(entry.name);
        const bool repeated = !seen.insert(key).second;
        if (repeated && !m_impl->python_duplicate_extraction)
            throw UnsafePath("重複する展開パスです: " + entry.name);
        const auto out = checked_destination(destination, entry.name);
        if (entry.directory) { std::filesystem::create_directories(out, ec); if (ec) throw ArchiveError("展開先ディレクトリを作成できません: " + path_text(out)); (void)safe_destination(destination, entry.name); directories.emplace_back(out, entry.mtime_unix, entry.mtime_valid); continue; }
        const auto parent = out.parent_path(); std::filesystem::create_directories(parent, ec); if (ec) throw ArchiveError("展開先ディレクトリを作成できません: " + path_text(parent));
        (void)safe_destination(destination, entry.name);
        if (entry.mtime_conversion_failed) throw ArchiveError("LHAファイル時刻を変換できません。");
        if (std::filesystem::exists(out, ec) && !options.overwrite && !(repeated && m_impl->python_duplicate_extraction)) throw ArchiveError("展開先ファイルが既に存在します: " + path_text(out));
        if (reuse_archive_time(out, entry, options)) continue;
        const auto bytes = m_impl->python_duplicate_extraction
                ? read(entry.name) : read_record(*m_impl, m_impl->records[i]);
            // 重複名の文字列読み取りでは、より大きい最後の記録が選ばれる場合
            // がある。 書き込み前に、展開済みバイト数へ同じ上限を適用する。
            if (bytes.size() > options.max_file_size || bytes.size() > options.max_total_size ||
                total > options.max_total_size - bytes.size())
                throw ArchiveError("アーカイブ展開サイズが上限を超えました。");
        write_with_recovery(out, options, [&] {
            if (options.overwrite && options.recover_overwrite) {
                // Python と同様、最初に既存のシナリオファイルを開いて切り
                // 詰める。 不可分な置き換えは使用中のファイルロック下でも成功し
                // 、Deleted への移動・再試行という動作を隠す場合がある
                // ため、 共通プロバイダーの実際の書き込み・フラッシュを使う。
                ::cardwirth::platform::NativeStorage storage(out.parent_path(), false);
                if (!storage.write(std::filesystem::absolute(out), bytes, false))
                    throw ArchiveError(storage.last_error());
                return;
            }
            const auto temporary = write_temporary(out, bytes);
            commit_temporary(temporary, out, options.overwrite || (repeated && m_impl->python_duplicate_extraction));
        });
        set_mtime(out, entry.mtime_unix, entry.mtime_valid);
        total += bytes.size();
    }
    for (auto it = directories.rbegin(); it != directories.rend(); ++it)
        set_mtime(std::get<0>(*it), std::get<1>(*it), std::get<2>(*it));
    return destination;
}

struct Writer::Pending {
    std::string name;
    std::string key;
    bool directory{};
    Compression compression{Compression::Stored};
    std::uint64_t mtime_unix{};
    bool utf8_filename{true};
    bool legacy_from_utf8{};
    std::optional<ZipMemberMetadata> metadata;
    bool emit_extended_timestamp{true};
    std::uint16_t flags{}, method{}, dos_date{}, dos_clock{};
    std::uint32_t crc{};
    std::uint64_t compressed{}, uncompressed{}, offset{};
    bool local_zip64{};
};

struct Writer::Output {
    // ofstream と異なるのはこのストリーム基本処理だけとする。 Engine・Editor・
    // Explorer の全 ZIP 規則と記録は write_member/close を使う。
    class MountedBuffer final : public std::streambuf {
    public:
        explicit MountedBuffer(::cardwirth::platform::StorageWriteHandle& handle) : handle_(handle) {
            setp(buffer_.data(), buffer_.data() + buffer_.size());
        }
    protected:
        int sync() override {
            const auto count = static_cast<std::size_t>(pptr() - pbase());
            if (count && (!handle_.write(std::span<const Byte>(
                    reinterpret_cast<const Byte*>(pbase()), count)) || !handle_.good())) return -1;
            written_ += count;
            setp(buffer_.data(), buffer_.data() + buffer_.size());
            return 0;
        }
        int_type overflow(int_type value) override {
            if (sync() != 0) return traits_type::eof();
            if (!traits_type::eq_int_type(value, traits_type::eof())) {
                *pptr() = traits_type::to_char_type(value); pbump(1);
            }
            return traits_type::not_eof(value);
        }
        std::streamsize xsputn(const char* source, std::streamsize count) override {
            std::streamsize used = 0;
            while (used < count) {
                if (pptr() == epptr() && sync() != 0) break;
                const auto amount = std::min<std::streamsize>(count - used, epptr() - pptr());
                std::memcpy(pptr(), source + used, static_cast<std::size_t>(amount));
                pbump(static_cast<int>(amount)); used += amount;
            }
            return used;
        }
        pos_type seekoff(off_type offset, std::ios_base::seekdir direction,
                         std::ios_base::openmode mode) override {
            // StorageWriteHandle は順次書き込みなので、tellp にヘッダーへのシークは不要である。
            if (offset != 0 || direction != std::ios_base::cur || !(mode & std::ios_base::out))
                return pos_type(off_type(-1));
            const auto position = written_ + static_cast<std::uint64_t>(pptr() - pbase());
            if (position > static_cast<std::uint64_t>((std::numeric_limits<off_type>::max)()))
                return pos_type(off_type(-1));
            return pos_type(static_cast<off_type>(position));
        }
    private:
        ::cardwirth::platform::StorageWriteHandle& handle_;
        std::array<char, 8192> buffer_{};
        std::uint64_t written_{};
    };
    std::filesystem::path temporary;
    bool mounted{}, committed{}, deferred_encoding_failure{};
    ::cw::lib::ExceptionPtr failure;
    std::ofstream native;
    std::unique_ptr<::cardwirth::platform::StorageWriteHandle> handle;
    std::unique_ptr<MountedBuffer> buffer;
    std::unique_ptr<std::ostream> mounted_stream;

    std::ostream& stream() { return mounted ? *mounted_stream : native; }
    void release() noexcept {
        mounted_stream.reset(); buffer.reset(); handle.reset();
        if (native.is_open()) native.close();
    }
    ~Output() {
        release();
        if (committed || temporary.empty()) return;
        try {
            if (mounted) (void)::cardwirth::platform::remove_mounted_user_path(temporary);
            else { std::error_code ec; std::filesystem::remove(temporary, ec); }
        } catch (...) {} // 後処理で、それ以前の入出力・文字コードの失敗を隠さない。
    }
};

namespace {

constexpr std::uint64_t kDZipEndRecordSize = 22;
constexpr std::uint64_t kDZip64EndRecordReservation = 20 + 56;

bool d_zip_layout_fits(const std::uint64_t previous_local_size,
                       const std::uint64_t previous_central_size,
                       const std::uint64_t name_size,
                       const std::uint64_t compressed_size,
                       const std::uint64_t limit) noexcept
{
    const auto add = [](std::uint64_t& value, const std::uint64_t amount) {
        if (amount > (std::numeric_limits<std::uint64_t>::max)() - value) return false;
        value += amount;
        return true;
    };
    auto projected = previous_local_size;
    if (!add(projected, 30) || !add(projected, name_size) || !add(projected, compressed_size) ||
        !add(projected, previous_central_size) || !add(projected, 46) ||
        !add(projected, name_size) || !add(projected, kDZipEndRecordSize) ||
        !add(projected, kDZip64EndRecordReservation)) return false;
    return projected <= limit;
}

} // namespace

Writer::Writer(const std::filesystem::path& path, const WriterOptions& options)
    : m_path(path), m_options(options) {}

Writer Writer::create(const std::filesystem::path& path, const WriterOptions& options) { return Writer(path, options); }

Writer Writer::append(const std::filesystem::path& path, const WriterOptions& options)
{
    const auto reader = Reader::open(path);
    if (reader.format() != Format::Zip) throw UnsupportedCompression("既存ZIP以外への追加はサポートしていません。");
    auto writer = Writer::create(path, options);
    try {
        for (const auto& entry : reader.entries()) {
            // 各メンバーをコピーし終えるまで、開いた元ファイルを利用可能
            // に保つ。 終了まで全展開済みメンバーを保持する必要はない。
            const auto bytes = entry.directory ? Bytes{} : reader.read(entry);
            writer.add_zip_member(entry.name, entry.zip_raw_name, bytes, entry.directory,
                entry.directory ? Compression::Stored : entry.compression, entry.utf8_name,
                ZipMemberMetadata{static_cast<std::uint64_t>(entry.mtime_unix),
                    entry.zip_external_attributes, entry.zip_internal_attributes, entry.zip_made_by});
        }
    } catch (...) {
        // 追記用の生成処理が失敗した場合、Writer の自動終了デ
        // ストラクターで、 一部だけコピーした元データを公開しない。
        writer.m_closed = true;
        throw;
    }
    return writer;
}

Writer::Writer(Writer&& other) noexcept
    : m_path(std::move(other.m_path)), m_options(other.m_options),
      m_pending(std::move(other.m_pending)), m_output(std::move(other.m_output)),
      m_d_zip_local_layout_size(other.m_d_zip_local_layout_size),
      m_d_zip_central_layout_size(other.m_d_zip_central_layout_size), m_closed(other.m_closed)
{
    other.m_closed = true;
}

Writer& Writer::operator=(Writer&& other) noexcept
{
    if (this == &other) return *this;
    if (!m_closed) { try { close(); } catch (...) {} }
    m_path = std::move(other.m_path); m_options = other.m_options;
    m_pending = std::move(other.m_pending); m_output = std::move(other.m_output);
    m_d_zip_local_layout_size = other.m_d_zip_local_layout_size;
    m_d_zip_central_layout_size = other.m_d_zip_central_layout_size;
    m_closed = other.m_closed;
    other.m_closed = true;
    return *this;
}
Writer::~Writer() noexcept { if (!m_closed) { try { close(); } catch (...) {} } }

void Writer::ensure_output()
{
    if (m_output) return;
    auto output = std::make_unique<Output>();
    output->mounted = ::cardwirth::platform::is_mounted_user_path(m_path);
    std::error_code ec;
    const auto parent = m_path.parent_path();
    if (!output->mounted && !parent.empty()) std::filesystem::create_directories(parent, ec);
    if (ec) throw ArchiveError("ZIP出力先を作成できません: " + path_text(parent));
    output->temporary = std::filesystem::u8path(path_text(m_path) + ".cw-archive-tmp-" +
        std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    if (output->mounted) {
        // 暗号化 UserData は既存のプロバイダー内で扱う。 一部のプロバイダ
        // ーは入力またはシリアライズ済み出力を内部に一つ保持するが、 この Writer
        // 自体は展開済みの全ツリーや二つ目の ZIP 文字列を保持しない。
        output->handle = ::cardwirth::platform::open_mounted_user_write_file(output->temporary);
        if (!output->handle || !output->handle->good())
            throw ArchiveError("ZIP出力先を作成できません: " + path_text(m_path));
        output->buffer = std::make_unique<Output::MountedBuffer>(*output->handle);
        output->mounted_stream = std::make_unique<std::ostream>(output->buffer.get());
    } else {
        output->native.open(output->temporary, std::ios::binary | std::ios::trunc);
        if (!output->native) throw ArchiveError("ZIP出力先を作成できません: " + path_text(m_path));
    }
    m_output = std::move(output);
}

void Writer::write_member(Pending& item,
                          const std::function<std::size_t(std::span<Byte>)>& read,
                          const std::uint64_t expected_size)
{
    if (m_closed) throw ArchiveError("閉じたWriterには追加できません。");
    if (std::any_of(m_pending.begin(), m_pending.end(), [&](const Pending& p) { return p.key == item.key; }))
        throw ArchiveError("重複するアーカイブ名です: " + item.key);
    if (item.compression != Compression::Stored && item.compression != Compression::Deflated)
        throw UnsupportedCompression("ZIP Writerが対応する圧縮方式はstored/deflateだけです。");
    item.emit_extended_timestamp = item.emit_extended_timestamp && m_options.emit_extended_timestamps;
    ensure_output();
    if (m_output->failure) {
        if (m_output->deferred_encoding_failure) return;
        ::cw::lib::rethrow_exception(m_output->failure);
    }
    if (item.legacy_from_utf8) {
        try { item.name = encode_legacy_path_name(item.name, item.directory); }
        catch (...) {
            // add_path の既存の文字コードエラーの遅延通知を維
            // 持する。 close で報告し、一時出力先は公開しない。
            m_output->failure = ::cw::lib::capture_exception();
            m_output->deferred_encoding_failure = true;
            return;
        }
    }
    try {
        if (item.name.size() > (std::numeric_limits<std::uint16_t>::max)())
            throw ArchiveError("ZIPファイル名が65535バイトを超えました。");
        auto& out = m_output->stream();
        const auto position = out.tellp();
        if (position < 0) throw ArchiveError("ZIP出力位置を取得できません。");
        item.offset = static_cast<std::uint64_t>(position);
        item.method = item.directory || item.compression == Compression::Stored ? 0 : 8;
        const bool needs_utf8 = item.utf8_filename && std::any_of(item.name.begin(), item.name.end(),
            [](unsigned char byte) { return byte >= 0x80; });
        // ZIP 表現の意図的な違いとして、Py はシーク可能なファイルのローカルヘッダーを書き換えるが、
        // StorageWriteHandle は一部の環境で順次書き込みのみとなる。 ビット 3 とデ
        // ータ記述子を使い、全プロバイダーで同じ上限付きライターを使えるようにする。 ASCII は ZipInfo._encodeFilenameFlags
        // と同様、ビット 11 を未設定にする。
        item.flags = static_cast<std::uint16_t>(8 | (needs_utf8 ? 0x800 : 0));
        // Py の size*1.05 の確認と同様、メンバーを書き込む前に ZIP64 を予約する。
        // 圧縮前後のバイト数は zlib の環境依存の uLong 合計ではなく uint64_t で管理する。
        item.local_zip64 = !m_options.d_zip_logical_layout_limit &&
            (expected_size > 0x7fffffffULL || expected_size + expected_size / 20 > 0x7fffffffULL);
        const bool timestamp = item.emit_extended_timestamp && item.mtime_unix != 0 && item.mtime_unix <= 0xffffffffULL;
        if (item.metadata && item.metadata->dos_time) {
            item.dos_date = static_cast<std::uint16_t>(*item.metadata->dos_time >> 16);
            item.dos_clock = static_cast<std::uint16_t>(*item.metadata->dos_time);
        } else {
            const auto [date, clock] = unix_to_dos_time(item.mtime_unix);
            item.dos_date = date; item.dos_clock = clock;
        }
        out.write("PK\x03\x04", 4); put16(out, item.local_zip64 ? 45 : 20);
        put16(out, item.flags); put16(out, item.method); put16(out, item.dos_clock); put16(out, item.dos_date); put32(out, 0);
        put32(out, item.local_zip64 ? 0xffffffffU : 0); put32(out, item.local_zip64 ? 0xffffffffU : 0);
        put16(out, static_cast<std::uint16_t>(item.name.size()));
        put16(out, static_cast<std::uint16_t>((item.local_zip64 ? 20 : 0) + (timestamp ? 9 : 0)));
        out.write(item.name.data(), static_cast<std::streamsize>(item.name.size()));
        if (item.local_zip64) { put16(out, 1); put16(out, 16); put64(out, 0); put64(out, 0); }
        if (timestamp) { put16(out, 0x5455); put16(out, 5); out.put(1); put32(out, static_cast<std::uint32_t>(item.mtime_unix)); }
        const auto* api = item.method == 8 ? &zlib() : nullptr;
        ZStream stream{};
        bool initialized = false;
        struct EndDeflate {
            const ZlibApi* api; ZStream& stream; bool& initialized;
            ~EndDeflate() { if (initialized) api->deflate_end(&stream); }
        } end{api, stream, initialized};
        if (item.method == 8) {
            if (!api->available) throw BackendUnavailable("deflate圧縮には実行時 zlib backend が必要です。");
            if (api->deflate_init2(&stream, std::clamp(m_options.compression_level, 0, 9), 8, -15, 8, 0,
                    "1.2.11", static_cast<int>(sizeof(ZStream))) != 0)
                throw ArchiveError("deflate初期化に失敗しました。");
            initialized = true;
        }
        std::array<Byte, 8192> input{}; // Py の ZipFile.write と同様、shutil.copyfileobj(..., 1024*8) 相当の単位でコピーする。
        std::array<unsigned char, 65536> compressed{};
        std::uint32_t checksum = 0xffffffffU;
        const auto emit = [&](const void* bytes, std::size_t count) {
            out.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(count));
            if (!out) throw ArchiveError("ZIPを書き込めません: " + path_text(m_path));
            if (count > (std::numeric_limits<std::uint64_t>::max)() - item.compressed)
                throw ArchiveError("ZIP圧縮サイズが大きすぎます。");
            item.compressed += count;
        };
        for (;;) {
            const auto count = item.directory ? 0 : read(input);
            if (count > input.size()) throw ArchiveError("ZIP入力サイズが不正です。");
            if (!count) break;
            if (count > (std::numeric_limits<std::uint64_t>::max)() - item.uncompressed)
                throw ArchiveError("ZIP入力サイズが大きすぎます。");
            item.uncompressed += count;
            checksum = update_crc32(checksum, std::span<const Byte>(input.data(), count));
            if (!item.method) { emit(input.data(), count); continue; }
            stream.next_in = reinterpret_cast<unsigned char*>(input.data());
            stream.avail_in = static_cast<unsigned int>(count);
            do {
                stream.next_out = compressed.data(); stream.avail_out = static_cast<unsigned int>(compressed.size());
                if (api->deflate(&stream, 0) != 0) throw ArchiveError("deflate圧縮に失敗しました。");
                emit(compressed.data(), compressed.size() - stream.avail_out);
            } while (stream.avail_in != 0 || stream.avail_out == 0);
        }
        if (item.method == 8) {
            int result;
            do {
                stream.next_out = compressed.data(); stream.avail_out = static_cast<unsigned int>(compressed.size());
                result = api->deflate(&stream, 4); // Z_FINISH を指定し、Z_STREAM_END まで繰り返す。
                if (result != 0 && result != 1) throw ArchiveError("deflate圧縮に失敗しました。");
                emit(compressed.data(), compressed.size() - stream.avail_out);
            } while (result != 1);
        }
        if (!item.local_zip64 && (item.uncompressed > 0xffffffffULL || item.compressed > 0xffffffffULL))
            throw ArchiveError("ZIP64が必要な入力サイズへ変更されました。");
        if (m_options.d_zip_logical_layout_limit) {
            if (!d_zip_layout_fits(m_d_zip_local_layout_size, m_d_zip_central_layout_size,
                                   item.name.size(), item.compressed,
                                   *m_options.d_zip_logical_layout_limit))
                throw ArchiveError("ZIPのD互換classic layout上限を超えました。");
            m_d_zip_local_layout_size += 30 + item.name.size() + item.compressed;
            m_d_zip_central_layout_size += 46 + item.name.size();
        }
        item.crc = ~checksum;
        out.write("PK\x07\x08", 4); put32(out, item.crc);
        if (item.local_zip64) { put64(out, item.compressed); put64(out, item.uncompressed); }
        else { put32(out, static_cast<std::uint32_t>(item.compressed)); put32(out, static_cast<std::uint32_t>(item.uncompressed)); }
        if (!out) throw ArchiveError("ZIPを書き込めません: " + path_text(m_path));
        m_pending.push_back(std::move(item)); // 中央のメタデータだけを保持し、内容は含めない。
    } catch (...) {
        m_output->failure = ::cw::lib::capture_exception();
        throw;
    }
}

void Writer::add_zip_member(const std::string& logical_name, const std::string& raw_name,
                            const std::vector<Byte>& data, const bool directory,
                            const Compression compression, const bool utf8_filename,
                            const ZipMemberMetadata& metadata)
{
    const auto logical = normalize_name(logical_name, directory);
    const auto decoded = utf8_filename ? raw_name : decode_name_bytes(raw_name);
    if (normalize_name(decoded, false) != normalize_name(logical, false))
        throw UnsafePath("ZIP raw and logical member names disagree.");
    if (raw_name.empty() || raw_name.find('\0') != std::string::npos)
        throw UnsafePath("Invalid raw ZIP member name.");
    if (directory && !data.empty()) throw ArchiveError("Directory has payload.");
    Pending item;
    item.name = raw_name; item.key = extraction_key(logical); item.directory = directory;
    item.compression = directory ? Compression::Stored : compression;
    item.utf8_filename = utf8_filename; item.mtime_unix = metadata.mtime_unix; item.metadata = metadata;
    std::size_t used = 0;
    write_member(item, [&](std::span<Byte> destination) {
        const auto count = (std::min)(destination.size(), data.size() - used);
        if (count) std::memcpy(destination.data(), data.data() + used, count);
        used += count; return count;
    }, data.size());
}

void Writer::add_directory(const std::string& name) { add_directory(name, m_options.utf8_filenames); }

void Writer::add_directory(const std::string& name, const bool utf8_filename)
{
    Pending item;
    item.name = utf8_filename ? archive_name(name, true) : normalize_legacy_name(name, true);
    if (item.name.back() != '/') item.name.push_back('/');
    item.key = extraction_key(utf8_filename ? item.name : decode_name_bytes(item.name));
    item.directory = true; item.mtime_unix = current_unix_time(); item.utf8_filename = utf8_filename;
    write_member(item, [](std::span<Byte>) { return std::size_t{0}; }, 0);
}

void Writer::add_file(const std::string& name, const std::vector<Byte>& data, Compression compression)
{ add_file(name, data.data(), data.size(), compression, m_options.utf8_filenames); }

void Writer::add_file(const std::string& name, const std::vector<Byte>& data,
                      const Compression compression, const bool utf8_filename)
{ add_file(name, data.data(), data.size(), compression, utf8_filename); }

void Writer::add_file(const std::string& name, const void* data, std::size_t size, Compression compression)
{ add_file(name, data, size, compression, m_options.utf8_filenames); }

void Writer::add_file(const std::string& name, const void* data, std::size_t size,
                      const Compression compression, const bool utf8_filename)
{
    if (!data && size != 0) throw ArchiveError("nullデータです。");
    Pending item;
    item.name = utf8_filename ? archive_name(name, false) : normalize_legacy_name(name, false);
    item.key = extraction_key(utf8_filename ? item.name : decode_name_bytes(item.name));
    item.compression = compression; item.mtime_unix = current_unix_time(); item.utf8_filename = utf8_filename;
    std::size_t used = 0;
    write_member(item, [&](std::span<Byte> destination) {
        const auto count = (std::min)(destination.size(), size - used);
        if (count) std::memcpy(destination.data(), static_cast<const Byte*>(data) + used, count);
        used += count; return count;
    }, size);
}

void Writer::add_source_file(const std::filesystem::path& source, const std::string& name,
                             const Compression compression)
{
    Pending item;
    item.name = archive_name(name, false); item.key = extraction_key(item.name);
    item.compression = compression; item.mtime_unix = file_unix_time(source);
    item.metadata = ZipMemberMetadata{item.mtime_unix,
        (zip_source_file_mode(source) & 0xffffU) << 16, 0, zip_creator_version()};
    item.emit_extended_timestamp = false; // Py の ZipInfo.from_file と同様、UT 追加情報は付けない。
    item.utf8_filename = m_options.utf8_filenames; item.legacy_from_utf8 = !item.utf8_filename;
    std::ifstream native;
    std::unique_ptr<::cardwirth::platform::StorageReadHandle> mounted;
    std::uint64_t size;
    if (::cardwirth::platform::is_mounted_user_path(source)) {
        const auto stat = ::cardwirth::platform::stat_mounted_user_path(source);
        if (!stat || !stat->is_file()) throw ArchiveError("ZIP入力を開けません: " + path_text(source));
        size = stat->size;
        mounted = ::cardwirth::platform::open_mounted_user_file(source);
        if (!mounted || !mounted->good()) throw ArchiveError("ZIP入力を開けません: " + path_text(source));
    } else {
        std::error_code ec;
        size = std::filesystem::file_size(source, ec);
        if (ec) throw ArchiveError("ZIP入力サイズを取得できません: " + path_text(source));
        native.open(source, std::ios::binary);
        if (!native) throw ArchiveError("ZIP入力を開けません: " + path_text(source));
    }
    write_member(item, [&](std::span<Byte> destination) {
        if (mounted) {
            const auto count = mounted->read(destination);
            if (!mounted->good()) throw ArchiveError("ZIP入力を読み取れません: " + path_text(source));
            return count;
        }
        native.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));
        if (native.bad() || (native.fail() && !native.eof()))
            throw ArchiveError("ZIP入力を読み取れません: " + path_text(source));
        return static_cast<std::size_t>(native.gcount());
    }, size);
}

void Writer::add_path(const std::filesystem::path& source, const std::string& archive_prefix, Compression compression)
{
    add_path(source, archive_prefix, compression, true);
}

void Writer::add_path(const std::filesystem::path& source, const std::string& archive_prefix,
                      Compression compression, bool allow_files)
{
    const bool mounted_source = ::cardwirth::platform::is_mounted_user_path(source);
    const bool mounted_destination = ::cardwirth::platform::is_mounted_user_path(m_path);
    if (mounted_source && !mounted_destination)
        throw ArchiveError("暗号化UserDataを通常ファイルのアーカイブへ書き出すことはできません。");
    const auto same_output_path = [&](const std::filesystem::path& candidate,
                                      const std::filesystem::path& output) {
        if (mounted_destination) {
            const auto left = ::cardwirth::platform::resolve_mounted_user_path(candidate);
            const auto right = ::cardwirth::platform::resolve_mounted_user_path(output);
            return left && right && *left == *right;
        }
        std::error_code identity_error;
        if (std::filesystem::equivalent(candidate, output, identity_error)) return true;
        identity_error.clear();
        const auto left = std::filesystem::weakly_canonical(candidate, identity_error);
        if (identity_error) throw ArchiveError("Cannot resolve archive source identity");
        const auto right = std::filesystem::weakly_canonical(output, identity_error);
        if (identity_error) throw ArchiveError("Cannot resolve archive output identity");
        return left == right;
    };
    const auto is_output = [&](const std::filesystem::path& candidate) {
        // 安全上の意図的な差異として、自身の出力 ZIP を入力に含めない。
        // Py は os.walk の前に出力を開くため、自分自身を収集す
        // る場合がある。 この Writer の出力先と使用中の一時ファイル
        // だけを除外し、他の ZIP や一時名に見える入力は通常どおり扱う。
        return same_output_path(candidate, m_path) ||
            (m_output && same_output_path(candidate, m_output->temporary));
    };
    std::error_code ec;
    auto prefix = archive_prefix.empty() ? std::string{} : archive_name(archive_prefix, true);
    if (!prefix.empty() && prefix.back() != '/') prefix.push_back('/');
    const auto add_generated_directory = [&](const std::string& utf8_name, const std::filesystem::path& path) {
        Pending item;
        item.name = archive_name(utf8_name, true); if (item.name.back() != '/') item.name.push_back('/');
        item.key = extraction_key(item.name); item.directory = true; item.mtime_unix = file_unix_time(path);
        // Py の compress_zip と同様、ディレクトリー印には ZipInfo/writestr を使う。 既
        // 定の external_attr はディレクトリーの stat モードではなく 0600 << 16 とする。
        item.metadata = ZipMemberMetadata{item.mtime_unix, 0600U << 16, 0, zip_creator_version()};
        item.emit_extended_timestamp = false;
        item.utf8_filename = m_options.utf8_filenames; item.legacy_from_utf8 = !item.utf8_filename;
        write_member(item, [](std::span<Byte>) { return std::size_t{0}; }, 0);
    };
    const auto status = mounted_source ? ::cardwirth::platform::stat_mounted_user_path(source)
                                      : std::optional<::cardwirth::platform::StorageStat>{};
    if (mounted_source && (!status || !status->exists())) throw ArchiveError("Archive source does not exist");
    const auto native_status = mounted_source ? std::filesystem::file_status{} : std::filesystem::symlink_status(source, ec);
    if (!mounted_source && (ec || !std::filesystem::exists(native_status))) throw ArchiveError("Archive source does not exist");
    if (mounted_source ? status->type == ::cardwirth::platform::StorageEntryType::Symlink
                       : std::filesystem::is_symlink(native_status)) throw UnsafePath("Archive source is a symlink");
    const bool file = mounted_source ? status->is_file() : std::filesystem::is_regular_file(native_status);
    const bool directory = mounted_source ? status->is_directory() : std::filesystem::is_directory(native_status);
    if (file) {
        if (is_output(source)) return;
        if (!allow_files) throw std::invalid_argument("Unsupported not Unicode File Name.");
        add_source_file(source, prefix.empty() ? path_text(source.filename()) : prefix.substr(0, prefix.size() - 1), compression);
        return;
    }
    if (!directory) throw ArchiveError("Archive source is not a file or directory");
    if (!prefix.empty()) add_generated_directory(prefix, source);
    struct Child { std::filesystem::path path; bool directory{}, file{}, symlink{}; };
    const auto children = [&](const std::filesystem::path& dir) {
        std::vector<Child> result;
        if (mounted_source) {
            const auto entries = ::cardwirth::platform::list_mounted_user_path(dir);
            if (!entries) throw ArchiveError("Cannot list archive source");
            for (const auto& entry : *entries) result.push_back({entry.path, entry.stat.is_directory(),
                entry.stat.is_file(), entry.stat.type == ::cardwirth::platform::StorageEntryType::Symlink});
        } else {
            std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
            if (ec) throw ArchiveError("Cannot list archive source");
            for (; it != end; it.increment(ec)) {
                if (ec) throw ArchiveError("Cannot list archive source");
                const auto st = it->symlink_status(ec);
                if (ec) throw ArchiveError("Cannot inspect archive source");
                result.push_back({it->path(), std::filesystem::is_directory(st),
                    std::filesystem::is_regular_file(st), std::filesystem::is_symlink(st)});
            }
            if (ec) throw ArchiveError("Cannot list archive source");
        }
        return result;
    };
    const auto add_tree = [&](const auto& self, const std::filesystem::path& dir) -> void {
        const auto entries = children(dir);
        const auto name = [&](const Child& child) {
            // Py はパスとスラッシュを文字列として除くため、呼び出し側
            // が末尾スラッシュを付けると、項目名に元のディレクトリーが
            // 残る場合がある。 ここではリンクを解決せず字句上の要素を
            // 使い、どの表記でも同じ入力元相対のアーカイブ構成を作る。
            const auto relative = child.path.lexically_relative(source);
            if (relative.empty() || relative.is_absolute() ||
                std::ranges::any_of(relative, [](const auto& part) { return part == ".."; }))
                throw UnsafePath("Archive source relative path is invalid");
            return prefix + path_text(relative);
        };
        // compress_zip と同様、上から順に、この階層のディレ
        // クトリー、ファイル、各部分ツリーを走査し、プロバイダーの順序を
        // 並べ替えない。 Py のコピー・書き込みはファイルリンクをたど
        // るが、ネイティブの入力走査ではリンクを拒否し、 指定ルート外の
        // 参照先を黙って含めない。ディレクトリーリンクも例外にしない。
        for (const auto& child : entries) {
            if (is_output(child.path)) continue;
            if (child.symlink) throw UnsafePath("Archive source contains a symlink");
            if (child.directory) add_generated_directory(name(child) + "/", child.path);
        }
        for (const auto& child : entries) if (child.file) {
            if (is_output(child.path)) continue;
            if (!allow_files) throw std::invalid_argument("Unsupported not Unicode File Name.");
            add_source_file(child.path, name(child), compression);
        }
        for (const auto& child : entries) if (child.directory) self(self, child.path);
    };
    add_tree(add_tree, source);
}

void Writer::close()
{
    if (m_closed) return;
    ensure_output();
    if (m_output->failure) ::cw::lib::rethrow_exception(m_output->failure);
    try {
        const bool d_classic_layout = m_options.d_zip_logical_layout_limit.has_value();
        auto& out = m_output->stream();
        const auto cd_stream_offset = out.tellp();
        if (cd_stream_offset < 0) throw ArchiveError("ZIP central directory位置を取得できません。");
        const auto cd_offset = static_cast<std::uint64_t>(cd_stream_offset);
        for (const auto& item : m_pending) {
            const bool size64 = item.compressed > 0xffffffffULL || item.uncompressed > 0xffffffffULL;
            const bool offset64 = item.offset > 0xffffffffULL;
            if (d_classic_layout && (size64 || offset64))
                throw ArchiveError("D互換classic ZIPではmember size/offsetのZIP64を使用できません。");
            const bool timestamp = item.emit_extended_timestamp && item.mtime_unix != 0 && item.mtime_unix <= 0xffffffffULL;
            const std::uint16_t zip64_extra_size = (size64 || offset64) ? static_cast<std::uint16_t>(4 + (size64 ? 16 : 0) + (offset64 ? 8 : 0)) : 0;
            const std::uint16_t extra_size = static_cast<std::uint16_t>(zip64_extra_size + (timestamp ? 9 : 0));
            out.write("PK\x01\x02", 4);
            auto made_by = item.metadata ? item.metadata->made_by : std::uint16_t{45};
            if (item.metadata && !item.emit_extended_timestamp && (size64 || offset64 || item.local_zip64))
                made_by = static_cast<std::uint16_t>((made_by & 0xff00U) |
                    (std::max)(static_cast<unsigned>(made_by & 0xffU), 45U));
            put16(out, made_by);
            put16(out, (size64 || offset64 || item.local_zip64) ? 45 : 20);
            put16(out, item.flags); put16(out, item.method); put16(out, item.dos_clock); put16(out, item.dos_date); put32(out, item.crc);
            put32(out, size64 ? 0xffffffffU : static_cast<std::uint32_t>(item.compressed)); put32(out, size64 ? 0xffffffffU : static_cast<std::uint32_t>(item.uncompressed));
            put16(out, static_cast<std::uint16_t>(item.name.size())); put16(out, extra_size); put16(out, 0); put16(out, 0);
            put16(out, item.metadata ? item.metadata->internal_attributes : 0);
            put32(out, item.metadata ? item.metadata->external_attributes : (item.directory ? 0x10U : 0U));
            put32(out, offset64 ? 0xffffffffU : static_cast<std::uint32_t>(item.offset));
            out.write(item.name.data(), static_cast<std::streamsize>(item.name.size()));
            if (zip64_extra_size) {
                put16(out, 1); put16(out, static_cast<std::uint16_t>(zip64_extra_size - 4));
                if (size64) { put64(out, item.uncompressed); put64(out, item.compressed); }
                if (offset64) put64(out, item.offset);
            }
            if (timestamp) { put16(out, 0x5455); put16(out, 5); out.put(1); put32(out, static_cast<std::uint32_t>(item.mtime_unix)); }
        }
        const auto cd_end_stream = out.tellp();
        if (cd_end_stream < 0) throw ArchiveError("ZIP central directory終端を取得できません。");
        const auto cd_size = static_cast<std::uint64_t>(cd_end_stream) - cd_offset;
        if (d_classic_layout && (cd_size > 0xffffffffULL || cd_offset > 0xffffffffULL))
            throw ArchiveError("D互換classic ZIPではcentral directory size/offsetのZIP64を使用できません。");
        const bool zip64 = d_classic_layout
            ? m_pending.size() > 0xffffULL
            : m_pending.size() > 0xffffULL || cd_size > 0xffffffffULL || cd_offset > 0xffffffffULL ||
                std::any_of(m_pending.begin(), m_pending.end(), [](const Pending& item) {
                    return item.compressed > 0xffffffffULL || item.uncompressed > 0xffffffffULL ||
                           item.offset > 0xffffffffULL;
                });
        if (zip64) {
            const auto offset = static_cast<std::uint64_t>(out.tellp());
            out.write("PK\x06\x06", 4); put64(out, 44); put16(out, 45); put16(out, 45); put32(out, 0); put32(out, 0);
            put64(out, m_pending.size()); put64(out, m_pending.size()); put64(out, cd_size); put64(out, cd_offset);
            out.write("PK\x06\x07", 4); put32(out, 0); put64(out, offset); put32(out, 1);
        }
        out.write("PK\x05\x06", 4); put16(out, 0); put16(out, 0);
        put16(out, m_pending.size() > 0xffffULL ? 0xffffU : static_cast<std::uint16_t>(m_pending.size()));
        put16(out, m_pending.size() > 0xffffULL ? 0xffffU : static_cast<std::uint16_t>(m_pending.size()));
        put32(out, cd_size > 0xffffffffULL ? 0xffffffffU : static_cast<std::uint32_t>(cd_size));
        put32(out, cd_offset > 0xffffffffULL ? 0xffffffffU : static_cast<std::uint32_t>(cd_offset)); put16(out, 0);
        out.flush();
        if (!out) throw ArchiveError("ZIPを書き込めません: " + path_text(m_path));
        if (m_output->mounted) {
            if (!m_output->handle->flush() || !m_output->handle->good())
                throw ArchiveError("暗号化UserDataへZIPを書き込めません: " + path_text(m_path));
            m_output->release(); // 公開する前に、プロバイダーのハンドルを閉じる。
            if (!::cardwirth::platform::rename_mounted_user_path(m_output->temporary, m_path, true))
                throw ArchiveError("暗号化UserDataへZIPを反映できません: " + path_text(m_path));
        } else {
            m_output->native.close();
            if (!m_output->native) throw ArchiveError("ZIPを書き込めません: " + path_text(m_path));
            commit_temporary(m_output->temporary, m_path, true);
        }
        m_output->committed = true;
        m_closed = true;
    } catch (...) {
        m_output->failure = ::cw::lib::capture_exception();
        throw;
    }
}

} // namespace cw::lib::archive
