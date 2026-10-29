#include "cwxeditor_src/core/cwx/utils.hpp"

#include "cwxeditor_src/core/cwx/binary.hpp"
#include "cwxeditor_src/core/cwx/imagesize.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/core/cwx/sjis.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"

#include <chrono>
#include <bit>
#include <ctime>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <new>
#include <random>
#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#else
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pwd.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#endif

namespace cwxeditor::cwx::utils {
namespace {

[[nodiscard]] String lowerAscii(StringView value) {
    String result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return result;
}

[[nodiscard]] String extensionOf(StringView value) {
    return enumutils::lowerCase(pathExtension(value));
}

// Phobosは属性を二度問い合わせる。existsはネイティブのエ
// ラーを無視し、POSIXのリンクをたどらない。後続のisFileは
// リンクをたどり、失敗を伝播する。この基本処理だけをOS固有とする。
[[nodiscard]] bool nativePathExists(const Path& value) noexcept {
#if defined(_WIN32)
    return ::GetFileAttributesW(value.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat attributes{};
    return ::lstat(value.c_str(), &attributes) == 0;
#endif
}

struct NativePathKind { bool file; bool directory; };

[[nodiscard]] NativePathKind nativePathKind(const Path& value) {
#if defined(_WIN32)
    const auto attributes = ::GetFileAttributesW(value.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto failure = ::GetLastError();
        throw std::filesystem::filesystem_error("GetFileAttributes", value,
            std::error_code(static_cast<int>(failure), std::system_category()));
    }
    const auto directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return {!directory, directory};
#else
    struct stat attributes{};
    if (::stat(value.c_str(), &attributes) != 0) {
        const auto failure = errno;
        throw std::filesystem::filesystem_error("stat", value,
            std::error_code(failure, std::generic_category()));
    }
    return {S_ISREG(attributes.st_mode) != 0, S_ISDIR(attributes.st_mode) != 0};
#endif
}

[[nodiscard]] bool nativePathIsFile(const Path& value) {
    return nativePathKind(value).file;
}

[[nodiscard]] std::int64_t stdTimeFromFileTimeTicks(std::uint64_t ticks) {
    constexpr std::int64_t epoch = 504'911'232'000'000'000LL;
    if (ticks > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() - epoch))
        throw std::out_of_range("FILETIME cannot be represented as stdTime");
    return static_cast<std::int64_t>(ticks) + epoch;
}

[[nodiscard]] Bytes readFileBytes(const Path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > static_cast<std::uintmax_t>(
                           std::numeric_limits<std::size_t>::max())) {
        if (error) throw std::filesystem::filesystem_error("file_size", path, error);
        throw std::length_error("File is too large to read into memory");
    }
    Bytes bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::filesystem::filesystem_error(
        "open", path, std::make_error_code(std::errc::io_error));
    if (!bytes.empty()) {
        if (bytes.size() > static_cast<std::size_t>(
                               std::numeric_limits<std::streamsize>::max())) {
            throw std::length_error("File is too large for stream I/O");
        }
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (input.gcount() != static_cast<std::streamsize>(bytes.size()) || !input)
            throw std::filesystem::filesystem_error(
                "read", path, std::make_error_code(std::errc::io_error));
    }
    return bytes;
}

// Adapt the additional native stream API at the raw I/O boundary only.
// Bypass formatted-stream flags/exception masks, like D's FILE operations;
// tellの番兵値と未検査のseekを維持し、freadの一項目全体
// または空のスライスを返す。部分読み込みで書かれたバイトは保持する。
class SoundStreamInput {
public:
    explicit SoundStreamInput(std::istream& stream) : buffer_(stream.rdbuf()) {
        if (!buffer_) throw std::invalid_argument("sound stream has no buffer");
    }
    [[nodiscard]] std::int64_t tell() {
        return static_cast<std::streamoff>(buffer_->pubseekoff(
            0, std::ios::cur, std::ios::in));
    }
    void seek(std::int64_t offset, int origin = SEEK_SET) {
        const auto direction = origin == SEEK_END ? std::ios::end : std::ios::beg;
        (void)buffer_->pubseekoff(static_cast<std::streamoff>(offset), direction,
                                 std::ios::in);
    }
    [[nodiscard]] std::uint64_t size() {
        const auto position = tell();
        seek(0, SEEK_END);
        const auto result = tell();
        seek(position);
        return static_cast<std::uint64_t>(result);
    }
    [[nodiscard]] lib::MutableByteView rawRead(lib::MutableByteView bytes) {
        const auto count = buffer_->sgetn(reinterpret_cast<char*>(bytes.data()),
                                         static_cast<std::streamsize>(bytes.size()));
        return bytes.first(count == static_cast<std::streamsize>(bytes.size())
                               ? bytes.size() : 0U);
    }
private:
    std::streambuf* buffer_;
};

template <typename Input>
[[nodiscard]] String normalizedSoundExtFrom(Input& input) {
    const auto position = input.tell();
    String result;
    try {
        result = [&]() -> String {
            input.seek(0);
            Bytes bytes(static_cast<std::size_t>(
                std::min<std::uint64_t>(1084U, input.size())));
            (void)input.rawRead(lib::MutableByteView(bytes.data(), bytes.size()));
            auto extension = soundType(ByteView(bytes.data(), bytes.size()));
            if (!extension.empty()) return extension;
            if (input.size() < 147U) return {};
            input.seek(-128, SEEK_END);
            // Dは途中でファイルが大きくなっても、元のスライスを再利用する。
            if (bytes.size() < 147U) throw std::out_of_range("sound probe slice");
            bytes.resize(147U);
            std::fill_n(bytes.begin(), 19U, 0);
            const auto tail = input.rawRead(lib::MutableByteView(bytes.data() + 19U, 128U));
            return soundType(ByteView(tail.data(), tail.size()));
        }();
    } catch (...) {
        // Dのscope(exit)は、確保や読み込みの失敗時も位置を復元する。
        input.seek(position);
        throw;
    }
    input.seek(position);
    return result;
}

[[nodiscard]] std::vector<char32_t> decodeUtf8(StringView value) {
    std::vector<char32_t> result;
    std::size_t i = 0;
    while (i < value.size()) {
        const auto first = static_cast<unsigned char>(value[i++]);
        if (first < 0x80U) {
            result.push_back(first);
            continue;
        }
        std::size_t count = 0;
        char32_t codepoint = 0;
        if ((first & 0xe0U) == 0xc0U) {
            count = 1;
            codepoint = first & 0x1fU;
        } else if ((first & 0xf0U) == 0xe0U) {
            count = 2;
            codepoint = first & 0x0fU;
        } else if ((first & 0xf8U) == 0xf0U) {
            count = 3;
            codepoint = first & 0x07U;
        } else {
            result.push_back(0xfffdU);
            continue;
        }
        if (i + count > value.size()) {
            result.push_back(0xfffdU);
            break;
        }
        bool valid = true;
        for (std::size_t j = 0; j < count; ++j) {
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xc0U) != 0x80U) {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if (!valid || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            result.push_back(0xfffdU);
        } else {
            result.push_back(codepoint);
        }
    }
    return result;
}

void appendUtf8(String& result, char32_t codepoint) {
    if (codepoint <= 0x7fU) {
        result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffU) {
        result.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
        result.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0xffffU) {
        result.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
        result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        result.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else {
        result.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
        result.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
        result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        result.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    }
}

[[nodiscard]] char32_t lowerAsciiCodepoint(char32_t value) noexcept {
    return value >= U'A' && value <= U'Z' ? value + (U'a' - U'A') : value;
}

[[nodiscard]] String encodeBase64(ByteView input) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String result;
    result.reserve(((input.size() + 2U) / 3U) * 4U);
    for (std::size_t i = 0; i < input.size(); i += 3U) {
        const auto remaining = input.size() - i;
        const auto first = input[i];
        const auto second = remaining > 1U ? input[i + 1U] : 0U;
        const auto third = remaining > 2U ? input[i + 2U] : 0U;
        result.push_back(alphabet[first >> 2U]);
        result.push_back(alphabet[((first & 0x03U) << 4U) | (second >> 4U)]);
        result.push_back(remaining > 1U ? alphabet[((second & 0x0fU) << 2U) | (third >> 6U)] : '=');
        result.push_back(remaining > 2U ? alphabet[third & 0x3fU] : '=');
    }
    return result;
}

[[nodiscard]] int base64Value(char value) noexcept {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

[[nodiscard]] Bytes decodeBase64(StringView input) {
    if (input.size() % 4U != 0U) throw std::invalid_argument("invalid base64 length");
    // PhobosのBase64.decodeの逐次読み取りに従
    // う。「=」の値は-1で、三・四文字目にある場合だけ復号を終
    // える。特に、その番兵値の後でDが読まない文字は検証しない。
    const auto decode = [](char character) {
        if (character == '=') return -1;
        const auto value = base64Value(character);
        if (value < 0) throw std::invalid_argument("invalid base64 value");
        return value;
    };
    Bytes result;
    result.reserve((input.size() / 4U) * 3U);
    for (std::size_t i = 0; i < input.size(); i += 4U) {
        const auto a = decode(input[i]);
        const auto b = decode(input[i + 1U]);
        // 最初の組にパディング値がある場合も、符号なしの左シフトでC++
        // の未定義動作を避け、Dの下位バイトへの変換を再現する。
        result.push_back(static_cast<std::uint8_t>(
            (static_cast<unsigned>(a) << 2U) | static_cast<unsigned>(b >> 4)));
        const auto c = decode(input[i + 2U]);
        if (c == -1) break;
        result.push_back(static_cast<std::uint8_t>(
            (static_cast<unsigned>(b) << 4U) | static_cast<unsigned>(c >> 2)));
        const auto d = decode(input[i + 3U]);
        if (d == -1) break;
        result.push_back(static_cast<std::uint8_t>(
            (static_cast<unsigned>(c) << 6U) | static_cast<unsigned>(d)));
    }
    auto expected = (input.size() / 4U) * 3U;
    if (!input.empty()) expected -= input[input.size() - 2U] == '=' ? 2U :
                                   input.back() == '=' ? 1U : 0U;
    // Phobosは出力契約でrealDecodeLengthを検査する。C++の
    // アサートが無効なビルドでも、その契約違反をネイティブの失敗として報告する。
    if (result.size() != expected) throw std::invalid_argument("invalid base64 padding");
    return result;
}

[[nodiscard]] std::vector<char32_t> simpleGlobPattern(StringView pattern) {
    return decodeUtf8(pattern);
}

[[nodiscard]] bool classMatches(const std::vector<char32_t>& pattern,
                                std::size_t begin, std::size_t end,
                                char32_t value) {
    bool negate = begin < end && pattern[begin] == U'!';
    if (negate) ++begin;
    bool matched = false;
    for (std::size_t i = begin; i < end; ++i) {
        // DのsimpleGlobTransrateは、ハイフンも含め文字クラス内の各文字をエス
        // ケープする。そのため[A-Z]は文字範囲ではなく、A、ハイフン、Zのいずれかを表す。
        if (pattern[i] == value) {
            matched = true;
        }
    }
    return negate ? !matched : matched;
}

[[nodiscard]] bool globMatchImpl(const std::vector<char32_t>& value,
                                 const std::vector<char32_t>& pattern,
                                 std::size_t vi, std::size_t pi) {
    while (pi < pattern.size()) {
        if (pattern[pi] == U'*') {
            while (pi < pattern.size() && pattern[pi] == U'*') ++pi;
            if (pi == pattern.size()) return true;
            for (std::size_t i = vi; i <= value.size(); ++i) {
                if (globMatchImpl(value, pattern, i, pi)) return true;
            }
            return false;
        }
        if (vi >= value.size()) return false;
        if (pattern[pi] == U'?') {
            ++vi;
            ++pi;
            continue;
        }
        if (pattern[pi] == U'[') {
            auto close = pi + 1U;
            while (close < pattern.size() && pattern[close] != U']') ++close;
            if (close < pattern.size()) {
                if (!classMatches(pattern, pi + 1U, close, value[vi])) return false;
                vi++;
                pi = close + 1U;
                continue;
            }
        }
        if (pattern[pi] != value[vi]) return false;
        ++vi;
        ++pi;
    }
    return vi == value.size();
}

[[nodiscard]] String translateGlob(StringView pattern) {
    const auto values = decodeUtf8(pattern);
    String result;
    bool in_bracket = false;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto c = values[i];
        if (c == U'[' && !in_bracket) {
            const auto close = std::find(values.begin() + static_cast<std::ptrdiff_t>(i + 1U),
                                         values.end(), U']');
            if (close == values.end()) {
                result += "\\[";
            } else {
                in_bracket = true;
                result.push_back('[');
                if (i + 1U < values.size() && values[i + 1U] == U'!') {
                    result.push_back('^');
                    ++i;
                }
            }
        } else if (c == U']' && in_bracket) {
            result.push_back(']');
            in_bracket = false;
        } else if (!in_bracket && c == U'?') {
            result.push_back('.');
        } else if (!in_bracket && c == U'*') {
            result += ".*";
        } else {
            // Dは文字クラス内も含め、全リテラルにstd.regex.escaper
            // を使う。この公開変換をclassMatchesと一致させる。
            constexpr std::u32string_view escaped = U"[]\\^$.|?,-;:#&%/<>`*+(){}~";
            if (escaped.find(c) != std::u32string_view::npos) {
                result.push_back('\\');
            }
            appendUtf8(result, c);
        }
    }
    return result;
}

[[nodiscard]] bool isAsciiSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' ||
           c == '\f' || c == '\r';
}

[[nodiscard]] String trimAscii(StringView value) {
    std::size_t begin = 0;
    while (begin < value.size() && isAsciiSpace(value[begin])) ++begin;
    std::size_t end = value.size();
    while (end > begin && isAsciiSpace(value[end - 1U])) --end;
    return String(value.substr(begin, end - begin));
}

[[nodiscard]] Path normalizedAbsolute(const Path& value) {
    return nabs(value);
}

[[nodiscard]] Path pathFromUtf8(const StringView value) {
    if (value.empty()) return {};
#if defined(_WIN32)
    const auto* first = reinterpret_cast<const char8_t*>(value.data());
    return Path(std::u8string(first, first + value.size()));
#else
    return Path(String(value));
#endif
}

[[nodiscard]] String pathString(const Path& value) {
    const auto encoded = value.u8string();
    return String(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

[[nodiscard]] constexpr bool nativePathSeparator(char value) noexcept {
    return value == '/' || (Path::preferred_separator == '\\' && value == '\\');
}

[[nodiscard]] constexpr bool nativeElementSeparator(char value) noexcept {
    return nativePathSeparator(value) || (Path::preferred_separator == '\\' && value == ':');
}

[[nodiscard]] std::size_t nativePathRootLength(StringView value) {
    if (value.empty()) return 0;
    if (nativePathSeparator(value.front())) {
        if (Path::preferred_separator == '\\' && value.size() >= 3 &&
            nativePathSeparator(value[1]) && !nativePathSeparator(value[2])) {
            std::size_t end = 3;
            while (end < value.size() && !nativePathSeparator(value[end])) ++end;
            if (end < value.size()) {
                auto share = end;
                do { ++share; } while (share < value.size() && nativePathSeparator(value[share]));
                if (share < value.size()) {
                    do { ++share; } while (share < value.size() && !nativePathSeparator(value[share]));
                    end = share;
                }
            }
            return end;
        }
        return 1;
    }
    return Path::preferred_separator == '\\' && value.size() >= 3 &&
        value[1] == ':' && nativePathSeparator(value[2]) ? 3 : 0;
}

[[nodiscard]] StringView nativePathDriveName(StringView value) {
    // PhobosのdriveNameはUNCルートに共有名も含むが、filesystem
    // のroot_name()はサーバーだけを含む。POSIXパスにはドライブ名がない。
    if constexpr (Path::preferred_separator == '\\') {
        if (value.size() >= 2 && value[1] == ':') return value.substr(0, 2);
        if (value.size() >= 3 && nativePathSeparator(value[0]) &&
            nativePathSeparator(value[1]) && !nativePathSeparator(value[2]))
            return value.substr(0, nativePathRootLength(value));
    }
    return {};
}

[[nodiscard]] Path nativeRenamePath(const Path& value) {
    // Dのネイティブパスの境界に合わせ、Windows名は正規化し、POSIX
    // の相対パスやシンボリックリンクの表記は名前変更処理にそのまま渡す。
    if constexpr (Path::preferred_separator == '\\') return nabs(value);
    return value;
}

// DのPhobos v2.097.0のstd.pathのCaseSensitive.osDefaultは、Windows/Darwinでは大文字小文字を区別しない。
[[nodiscard]] bool pathCaseInsensitive() noexcept {
#if defined(_WIN32) || defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

[[nodiscard]] bool nativePathAbsolute(StringView value) {
    const auto root = nativePathRootLength(value);
    return Path::preferred_separator == '\\' ? root > 1U : root != 0U;
}

[[nodiscard]] String absolutePathValue(StringView value) {
    if (value.empty()) return {};
    if (nativePathAbsolute(value)) return String(value);
    // OSの基本処理はgetcwdだけ。Phobosはドライブ相対名を構成要素として結合する。filesystem::absolute
    // やGetFullPathNameを使うと解決されてしまう。
    const auto base = pathString(std::filesystem::current_path());
    // Phobosは基準が相対パスならExceptionを送出する。ネイティブの対応する例外分類を使う。
    if (!nativePathAbsolute(base)) throw std::invalid_argument("Base directory must be absolute");
    return buildPath(base, value);
}

[[nodiscard]] std::vector<StringView> nativePathParts(StringView value) {
    std::vector<StringView> parts;
    auto position = nativePathRootLength(value);
    if (position) parts.push_back(value.substr(0, position));
    while (position < value.size()) {
        if (nativePathSeparator(value[position])) { ++position; continue; }
        const auto first = position;
        while (position < value.size() && !nativePathSeparator(value[position])) ++position;
        parts.push_back(value.substr(first, position - first));
    }
    return parts;
}

[[nodiscard]] bool sameNativePathElement(StringView first, StringView second) {
    // 入力はnormpathの区切り文字変換済み。PhobosのfilenameCmp
    // はスカラー値のtoLowerを使い、icmpの完全な大文字小
    // 文字の候補は使わない。不正UTFの扱いは実装定義なので、この
    // ネイティブのスカラー変換には共通の厳密なデコーダーを使う。
    return pathCaseInsensitive()
        ? enumutils::simpleLowerCase(first) == enumutils::simpleLowerCase(second)
        : first == second;
}

[[nodiscard]] String relativePathValue(StringView path, StringView base) {
    // Phobosの不正な基準パスの失敗を、ネイティブの例外型で維持する。
    if (!nativePathAbsolute(base)) throw std::invalid_argument("Base directory must be absolute");
    const auto pathParts = nativePathParts(path);
    const auto baseParts = nativePathParts(base);
    const bool differentRoot = !sameNativePathElement(baseParts.front(), pathParts.front());
    std::size_t common = 1;
    while (common < baseParts.size() && common < pathParts.size() &&
           sameNativePathElement(baseParts[common], pathParts[common])) ++common;
    // Phobosは異なるルートのため未変更パスを選ぶ場合も、先に共通の
    // 末尾部分を計算する。実ファイルやシンボリックリンクは解決しない。
    if (differentRoot) return String(path);
    String result;
    const auto append = [&](StringView element) {
        if (!result.empty()) result += static_cast<char>(Path::preferred_separator);
        result += element;
    };
    for (auto i = common; i < baseParts.size(); ++i) append("..");
    for (auto i = common; i < pathParts.size(); ++i) append(pathParts[i]);
    return result.empty() ? String(".") : result;
}

[[nodiscard]] bool isPathPrefix(StringView root, StringView path) {
    const auto child = nabs(path);
    const auto base = nabs(root);
    if (!fnstartsWith(child, base)) return false;
    // Dの不具合への意図的な修正。区切りで終わるルートはすでに境界を持つ。追加の区
    // 切りを要求すると、Windowsのドライブルートや「/」の子が拒否される。
    return child.size() == base.size() ||
        (!base.empty() && nativePathSeparator(base.back())) ||
        nativePathSeparator(child[base.size()]);
}

[[nodiscard]] String lowerPathString(StringView value) {
    return pathCaseInsensitive() ? lowerAscii(value) : String(value);
}

// RFC 1321のMD5。暗号ライブラリへの依存を増やさないよう、この補助処理内に実装する。
struct Md5 {
    std::uint32_t state[4]{0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U};
    std::uint64_t bits{};
    Bytes buffer;

    static constexpr std::uint32_t leftRotate(std::uint32_t value, unsigned count) {
        return (value << count) | (value >> (32U - count));
    }

    void process(const std::uint8_t* block) {
        static constexpr std::uint32_t shifts[64] = {
            7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
            5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
        static constexpr std::uint32_t constants[64] = {
            0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU,
            0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U,
            0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
            0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U,
            0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
            0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
            0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
            0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU,
            0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
            0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
            0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U,
            0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
            0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U,
            0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
            0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
            0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U};
        std::uint32_t words[16]{};
        for (std::size_t i = 0; i < 16U; ++i) {
            words[i] = static_cast<std::uint32_t>(block[i * 4U]) |
                       (static_cast<std::uint32_t>(block[i * 4U + 1U]) << 8U) |
                       (static_cast<std::uint32_t>(block[i * 4U + 2U]) << 16U) |
                       (static_cast<std::uint32_t>(block[i * 4U + 3U]) << 24U);
        }
        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        for (std::size_t i = 0; i < 64U; ++i) {
            std::uint32_t function = 0;
            std::size_t index = 0;
            if (i < 16U) {
                function = (b & c) | ((~b) & d);
                index = i;
            } else if (i < 32U) {
                function = (d & b) | ((~d) & c);
                index = (5U * i + 1U) % 16U;
            } else if (i < 48U) {
                function = b ^ c ^ d;
                index = (3U * i + 5U) % 16U;
            } else {
                function = c ^ (b | (~d));
                index = (7U * i) % 16U;
            }
            const auto next = d;
            d = c;
            c = b;
            b += leftRotate(a + function + constants[i] + words[index], shifts[i]);
            a = next;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
    }

    void update(ByteView bytes) {
        bits += static_cast<std::uint64_t>(bytes.size()) * 8U;
        buffer.insert(buffer.end(), bytes.begin(), bytes.end());
        while (buffer.size() >= 64U) {
            process(buffer.data());
            buffer.erase(buffer.begin(), buffer.begin() + 64);
        }
    }

    [[nodiscard]] std::array<std::uint8_t, 16> finish() {
        const auto original_bits = bits;
        buffer.push_back(0x80U);
        while (buffer.size() % 64U != 56U) buffer.push_back(0U);
        for (unsigned i = 0; i < 8U; ++i) {
            buffer.push_back(static_cast<std::uint8_t>(original_bits >> (8U * i)));
        }
        for (std::size_t i = 0; i < buffer.size(); i += 64U) process(buffer.data() + i);
        std::array<std::uint8_t, 16> result{};
        for (std::size_t i = 0; i < 4U; ++i) {
            for (unsigned j = 0; j < 4U; ++j) {
                result[i * 4U + j] = static_cast<std::uint8_t>(state[i] >> (8U * j));
            }
        }
        return result;
    }
};

[[nodiscard]] String md5Hex(ByteView bytes) {
    Md5 md5;
    md5.update(bytes);
    const auto digest = md5.finish();
    String result;
    result.reserve(32U);
    static constexpr char hex[] = "0123456789ABCDEF";
    for (const auto value : digest) {
        result.push_back(hex[value >> 4U]);
        result.push_back(hex[value & 0x0fU]);
    }
    return result;
}

[[nodiscard]] String quoteCommand(StringView value) {
    String result = "\"";
    for (const auto c : value) {
        if (c == '"') result += "\\\"";
        else result.push_back(c);
    }
    result.push_back('"');
    return result;
}

}  // namespace

String detail::debugValue(StringView value) { return String(value); }
String detail::debugValue(const char* value) { return value == nullptr ? String{} : String(value); }
String detail::debugValue(char value) { return String(1, value); }
String detail::debugValue(bool value) { return value ? "true" : "false"; }

String detail::formatCharacter(char32_t codepoint) {
    if (codepoint > 0x10ffffU ||
        (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
        throw std::invalid_argument("D %c format received an invalid Unicode scalar");
    }
    String result;
    appendUtf8(result, codepoint);
    return result;
}

String decorateDebugln(String value, const std::source_location location, bool buildInfo) {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#if defined(_WIN32)
    ::localtime_s(&local, &now);
#else
    ::localtime_r(&now, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    if (buildInfo) out << " [" << APP_BUILD << "]\t";
    else out << ' ';
    out << location.file_name() << ':' << location.line() << ' ' << value;
    return out.str();
}

String printStackTrace() {
    String result = "Stack Trace --------";
    for (const auto& entry : perf::stStack) {
        result += "\n" + entry.file + ", " + std::to_string(entry.line);
    }
    fdebugln(result);
    clearStackTrace();
    return createDebugln(result);
}

void clearStackTrace() noexcept { perf::stStack.clear(); }

String printStackTraceNoError() {
    String result = "Stack Trace --------";
    const auto count = std::min(perf::tStackLen, perf::tStack.size());
    for (std::size_t i = 0; i < count; ++i) {
        result += "\n" + perf::tStack[i].file + ", " + std::to_string(perf::tStack[i].line);
    }
    fdebugln(result);
    return createDebugln(result);
}

void cwriteln(StringView value) {
#if defined(_CONSOLE) || defined(CWXEDITOR_CONSOLE) || defined(CONSOLE)
    static std::mutex output_mutex;
    const std::lock_guard lock(output_mutex);
#if defined(_WIN32)
    // DのWindowsのConsole分岐はtoMBSz（CP_ACP）とprintf
    // で改行付き出力を行う。printfのNUL終端を適用する前に、UTF-8入力全体
    // を変換する。下記のPOSIX分岐は、元のUTF-8文字列を長さ指定で出力する。
    String bytes(value);
    if (std::any_of(value.begin(), value.end(), [](unsigned char c) { return c >= 0x80U; })) {
        if (value.size() > static_cast<std::size_t>(INT_MAX))
            throw std::length_error("Console text exceeds the native conversion limit");
        const int wide_size = ::MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (wide_size == 0)
            throw std::system_error(static_cast<int>(::GetLastError()),
                                    std::system_category(), "Console UTF-8 conversion");
        std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
        if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                static_cast<int>(value.size()), wide.data(), wide_size) != wide_size)
            throw std::system_error(static_cast<int>(::GetLastError()),
                                    std::system_category(), "Console UTF-8 conversion");
        const int byte_size = ::WideCharToMultiByte(
            CP_ACP, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (byte_size == 0)
            throw std::system_error(static_cast<int>(::GetLastError()),
                                    std::system_category(), "Console ANSI conversion");
        bytes.resize(static_cast<std::size_t>(byte_size));
        if (::WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, bytes.data(), byte_size,
                                 nullptr, nullptr) != byte_size)
            throw std::system_error(static_cast<int>(::GetLastError()),
                                    std::system_category(), "Console ANSI conversion");
    }
    std::cout << bytes.c_str() << '\n';
#else
    std::cout << value << '\n';
#endif
#else
    // Dのversion(Console)本体はGUIビルドには含まれない。Editorビルドでは出力しない。
    (void)value;
#endif
}

void* dlopen(StringView library) {
#if defined(_WIN32)
    const auto wide = pathFromUtf8(library).wstring();
    return reinterpret_cast<void*>(::LoadLibraryW(wide.c_str()));
#else
    return ::dlopen(String(library).c_str(), RTLD_NOW);
#endif
}

void* dlsym(void* library, StringView symbol) {
    if (library == nullptr) return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<void*>(::GetProcAddress(
        reinterpret_cast<HMODULE>(library), String(symbol).c_str()));
#else
    return ::dlsym(library, String(symbol).c_str());
#endif
}

void dlclose(void*& library) noexcept {
    if (library == nullptr) return;
#if defined(_WIN32)
    ::FreeLibrary(reinterpret_cast<HMODULE>(library));
#else
    ::dlclose(library);
#endif
    library = nullptr;
}

Path appDataDir(const Path& application_path) {
#if defined(_WIN32)
    wchar_t value[MAX_PATH]{};
    if (SUCCEEDED(::SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, value)))
        return Path(value);
#else
    if (const auto* user = ::getpwuid(::getuid()); user != nullptr && user->pw_dir != nullptr) {
        return Path(user->pw_dir);
    }
    // Dはgetpwuid()の結果を無条件に参照する。アカウント項目
    // のないホストではnull参照を再現せず、利用可能な代替値を使う。
    if (const auto value = getenv("HOME"); !value.empty()) return Path(value);
#endif
    return application_path.parent_path();
}

String objectIDValue(const void* object) {
    using Clock = std::chrono::steady_clock;
    // DはObject引数の格納場所のアドレスを整形する。そのGC/ABI
    // 上の格納場所には移植可能なネイティブの対応物がないため、ここでは
    // 呼び出し側が識別用アドレスを渡す。型付きアダプターはネイティブ引
    // 数のアドレスを使う。DのMonoTimeは対応する刻み数と周波数
    // の組を公開する。ネイティブの単調時計は周期が異なり得るため、生
    // のカウントをすべてナノ秒と呼ばず、整数の1/den単位で表す。
    using Ticks = std::chrono::duration<Clock::rep, std::ratio<1, Clock::period::den>>;
    const auto ticks = std::chrono::duration_cast<Ticks>(Clock::now().time_since_epoch()).count();
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0') << std::setw(8)
           << reinterpret_cast<std::uintptr_t>(object)
           << '-' << std::dec << ticks << '/' << Clock::period::den;
    return stream.str();
}

bool isBinImg(StringView path) noexcept {
    return path.size() >= B_IMG.size() && path.substr(0, B_IMG.size()) == B_IMG;
}

Bytes strToBImg(StringView value) {
    // Dはスキーマの長さで切り出し、スキーマの内容自体は検査しない。
    return decodeBase64(value.substr(B_IMG.size()));
}

String bImgToStr(ByteView value) { return String(B_IMG) + encodeBase64(value); }
String bImgToStr(const Bytes& value) { return bImgToStr(ByteView(value.data(), value.size())); }

bool isBinSnd(StringView path) noexcept {
    return path.size() >= B_SND.size() && path.substr(0, B_SND.size()) == B_SND;
}

Bytes strToBSnd(StringView value) {
    // Dはスキーマの長さで切り出し、スキーマの内容自体は検査しない。
    return decodeBase64(value.substr(B_SND.size()));
}

String bSndToStr(ByteView value) { return String(B_SND) + encodeBase64(value); }
String bSndToStr(const Bytes& value) { return bSndToStr(ByteView(value.data(), value.size())); }

String getNormalizedSoundExt(ByteView bytes) {
    auto result = soundType(bytes);
    return result;
}

String getNormalizedSoundExt(std::istream& stream) {
    SoundStreamInput input(stream);
    return normalizedSoundExtFrom(input);
}

String getNormalizedSoundExt(binary::RawFile& file) {
    return normalizedSoundExtFrom(file);
}

String getNormalizedSoundExt(const Path& file) {
    binary::RawFile input(pathString(file), "rb");
    return getNormalizedSoundExt(input);
}

bool isSoundExt(StringView path) {
    const auto ext = extensionOf(path);
    return ext == ".aiff" || ext == ".mid" || ext == ".midi" ||
           ext == ".mod" || ext == ".s3m" || ext == ".xm" || ext == ".it" ||
           ext == ".mt2" || ext == ".669" || ext == ".med" || ext == ".mp3" ||
           ext == ".ogg" || ext == ".ogv" || ext == ".oga" || ext == ".ogx" ||
           ext == ".voc" || ext == ".wav";
}

bool pathIsAbsolute(StringView path) noexcept {
    return nativePathAbsolute(path);
}

StringView pathExtension(StringView path) noexcept {
    for (auto i = path.size(); i-- > 0 && !nativeElementSeparator(path[i]);) {
        if (path[i] == '.' && i > 0 && !nativeElementSeparator(path[i - 1U])) return path.substr(i);
    }
    return {};
}

StringView pathBaseName(StringView path) noexcept {
    const auto original = path;
    bool unc = false;
    if constexpr (Path::preferred_separator == '\\') {
        if (path.size() >= 2 && path[1] == ':') {
            path.remove_prefix(2);
        } else if (path.size() >= 3 && nativePathSeparator(path[0]) &&
                   nativePathSeparator(path[1]) && !nativePathSeparator(path[2])) {
            unc = true;
            std::size_t rootEnd = 3;
            while (rootEnd < path.size() && !nativePathSeparator(path[rootEnd])) ++rootEnd;
            if (rootEnd < path.size()) {
                auto share = rootEnd;
                do { ++share; } while (share < path.size() && nativePathSeparator(path[share]));
                if (share < path.size()) {
                    do { ++share; } while (share < path.size() && !nativePathSeparator(path[share]));
                    rootEnd = share;
                }
            }
            path.remove_prefix(rootEnd);
        }
    }
    if (path.empty()) return unc ? original.substr(0, 1) : path;
    const auto driveStripped = path;
    while (!path.empty() && nativePathSeparator(path.back())) path.remove_suffix(1);
    if (path.empty()) return driveStripped.substr(0, 1);
    auto start = path.size();
    while (start && !nativeElementSeparator(path[start - 1])) --start;
    return path.substr(start);
}

StringView pathDirName(StringView path) noexcept {
    // PhobosのdirNameと同様に、コード単位のスライスまたは静的なドットを返す
    // 。ネイティブのparent_path()は空の名前や末尾の区切りで動作が異なる。
    if (path.empty()) return ".";
    auto trimmed = path;
    while (!trimmed.empty() && nativePathSeparator(trimmed.back())) trimmed.remove_suffix(1);
    if (trimmed.empty()) return path.substr(0, 1);
    if constexpr (Path::preferred_separator == '\\') {
        const bool unc = trimmed.size() >= 3 && nativePathSeparator(trimmed[0]) &&
            nativePathSeparator(trimmed[1]) && !nativePathSeparator(trimmed[2]);
        if (unc && nativePathRootLength(trimmed) == trimmed.size()) return trimmed;
        if (trimmed.size() == 2 && trimmed[1] == ':' && path.size() > 2)
            return path.substr(0, 3);
    }
    auto end = trimmed.size();
    while (end && !nativeElementSeparator(trimmed[end - 1])) --end;
    if (!end) return ".";
    const auto separator = end - 1;
    if (!separator) return trimmed.substr(0, 1);
    if constexpr (Path::preferred_separator == '\\') {
        if (trimmed[separator] == ':' || trimmed[separator - 1] == ':')
            return trimmed.substr(0, end);
    }
    auto directory = trimmed.substr(0, separator);
    while (!directory.empty() && nativePathSeparator(directory.back())) directory.remove_suffix(1);
    return directory;
}

String setPathExtension(StringView path, StringView extension) {
    // 同じコード単位幅のPhobosのwithExtensionは、コード単位で操
    // 作する。この文字列操作をネイティブパスやUnicodeの検証境界にしない。
    String result(path.substr(0, path.size() - pathExtension(path).size()));
    if (!extension.empty() && extension.front() != '.') result.push_back('.');
    result.append(extension);
    return result;
}

String normalizedSoundExt(StringView extension) {
    const auto lower = lowerAscii(extension);
    if (lower == ".aiff") return ".aiff";
    if (lower == ".mid" || lower == ".midi") return ".mid";
    if (lower == ".mod" || lower == ".s3m" || lower == ".xm" || lower == ".it" ||
        lower == ".mt2" || lower == ".669" || lower == ".med") return ".mod";
    if (lower == ".mp3") return ".mp3";
    if (lower == ".ogg" || lower == ".ogv" || lower == ".oga" || lower == ".ogx") return ".ogg";
    if (lower == ".voc") return ".voc";
    if (lower == ".wav") return ".wav";
    return String(extension);
}

String soundType(ByteView bytes) noexcept {
    const auto has = [bytes](std::size_t offset, std::size_t count) {
        return offset <= bytes.size() && count <= bytes.size() - offset;
    };
    const auto text = [bytes](std::size_t offset, StringView value) {
        if (offset + value.size() > bytes.size()) return false;
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (bytes[offset + i] != static_cast<std::uint8_t>(value[i])) return false;
        }
        return true;
    };
    if (has(0, 12) && text(0, "RIFF") && text(8, "WAVE")) return ".wav";
    if (text(0, "MThd")) return ".mid";
    if (text(0, "OggS")) return ".ogg";
    if (bytes.size() >= 128U && text(bytes.size() - 128U, "TAG")) return ".mp3";
    if (text(0, "ID3")) return ".mp3";
    // FORM/AIFFに対するD実装の従来の戻り値を維持する。
    if (has(0, 12) && text(0, "FORM") && text(8, "AIFF")) return ".wav";
    if (text(0, "Creative Voice File")) return ".voc";
    if (has(1080U, 4U) &&
        ((text(1080U, "M.K.") || text(1080U, "4CHN") || text(1080U, "6CHN") ||
          text(1080U, "8CHN") || text(1080U, "4FLT") || text(1080U, "8FLT")))) {
        return ".mod";
    }
    if (has(0, 4) && bytes[0] == 0xffU && bytes[1] == 0xfbU) return ".mp3";
    return {};
}

String formatReal(const lib::Real80 value, const std::uint32_t max_len) {
    // Dはpow(10.0, maxLen)を使うため、倍率と最後のint/scale
    // 除算はbinary64。乗算と符号付きの半分の加算はDのrealを使う。
    const auto power = std::pow(10.0, static_cast<double>(max_len));
    if (!value.isFinite()) {
        throw std::out_of_range("formatReal input cannot be converted by D roundTo!int");
    }
    if (!std::isfinite(power) || power == 0.0) {
        throw std::out_of_range("formatReal scale cannot be converted by D roundTo!int");
    }
    const auto scaled = value * lib::Real80(power);
    const auto adjusted = scaled + lib::Real80(value < lib::Real80(0) ? -0.5 : 0.5);
    if (!adjusted.isFinite()) {
        throw std::out_of_range("formatReal result exceeds D roundTo!int range");
    }
    // ネイティブ整数へ変換する前にreal精度で切り捨てる。先にdouble
    // へ変換すると、丸めでint32の境界を越える場合がある。
    const auto rounded = adjusted - adjusted.fmod(lib::Real80(1));
    if (rounded < lib::Real80(std::numeric_limits<std::int32_t>::min()) ||
        rounded > lib::Real80(std::numeric_limits<std::int32_t>::max())) {
        throw std::out_of_range("formatReal result exceeds D roundTo!int range");
    }
    const auto integer = static_cast<std::int32_t>(rounded.toDouble());
    const auto output = static_cast<double>(integer) / power;
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(max_len) << output;
    auto result = stream.str();
    // maxLen=0で整数部のゼロを除く動作も含め、Dの末尾除去の規則を維持する。
    while (result.size() > 1U && result.back() == '0') result.pop_back();
    if (!result.empty() && result.back() == '.') result.pop_back();
    return result;
}

String formatReal(const long double value, const std::uint32_t max_len) {
    // ネイティブABIのアダプターだけを担う。全ホストで同じreal演算を使う。
    return formatReal(lib::Real80(value), max_len);
}

Wildcard::Wildcard(StringView pattern, bool ignore_case)
    : pattern_(compile(pattern)), ignore_case_(ignore_case) {}

std::shared_ptr<const Wildcard::Pattern> Wildcard::compile(StringView pattern) {
    auto result = std::make_shared<Pattern>(1U);
    bool escaped = false;
    std::size_t byte = 0;
    while (byte < pattern.size()) {
        const auto scalar = enumutils::decodeUtf8Scalar(pattern, byte);
        const auto c = scalar.value;
        byte = scalar.next;
        if (c == U'?' && !escaped) {
            result->back().push_back(Character{U'\0', true});
        } else if (c == U'*' && !escaped) {
            while (byte < pattern.size() && pattern[byte] == '*') ++byte;
            // 所有関係の連鎖だけを平坦化し、星印の後の空の末端_right
            // も保持する。検索はDの再帰的な流れに従う。
            result->emplace_back();
        } else if (c == U'\\') {
            if (escaped) result->back().push_back(Character{c, false});
            escaped = !escaped;
        } else {
            result->back().push_back(Character{c, false});
            escaped = false;
        }
    }
    if (escaped) result->back().push_back(Character{U'\\', false});
    return result;
}

bool Wildcard::Character::match(char32_t candidate, bool ignore_case) const noexcept {
    if (question) return true;
    return ignore_case ? lowerAsciiCodepoint(value) == lowerAsciiCodepoint(candidate)
                       : value == candidate;
}

std::ptrdiff_t Wildcard::findWords(std::u32string_view source,
                                  std::size_t segment, bool reverse) const {
    const auto& left = (*pattern_)[segment];
    if (source.size() < left.size()) return -1;
    const auto last = source.size() - left.size();
    for (std::size_t step = 0;; ++step) {
        const auto index = reverse ? last - step : step;
        std::size_t j = 0;
        while (j < left.size() && left[j].match(source[index + j], ignore_case_)) ++j;
        if (j == left.size()) return static_cast<std::ptrdiff_t>(index);
        if (step == last) break;
    }
    return -1;
}

std::ptrdiff_t Wildcard::findRange(std::u32string_view& source,
                                  std::size_t segment, bool next,
                                  std::size_t& length) const {
    length = 0; // 再帰呼び出しも含め、Dのout引数を初期化する。
    const auto base = source;
    const auto left_size = (*pattern_)[segment].size();
    for (;;) {
        const auto index = findWords(source, segment, next);
        if (index == -1) return -1;
        if (segment + 1U < pattern_->size()) {
            // 短くした入力元で候補を再試行するが、各末尾部
            // 分はDのsbaseと同じく元の基準から作る。
            auto suffix = base.substr(static_cast<std::size_t>(index) + left_size);
            const auto right = findRange(suffix, segment + 1U, true, length);
            if (right == -1) {
                if (!next) return -1;
                if (source.empty()) throw std::out_of_range("wildcard retry slice");
                source.remove_suffix(1U);
                continue;
            }
            length += left_size + static_cast<std::size_t>(right);
            return index;
        }
        length = left_size;
        return index;
    }
}

std::ptrdiff_t Wildcard::find(StringView value) const {
    if (value.empty()) return -1;
    const auto input = enumutils::toUtf32Replacing(value);
    std::u32string_view source(input);
    std::size_t length = 0;
    const auto index = findRange(source, 0, false, length);
    if (index == -1) return -1;
    String prefix;
    for (const auto c : source.substr(0, static_cast<std::size_t>(index))) appendUtf8(prefix, c);
    return static_cast<std::ptrdiff_t>(prefix.size());
}

bool Wildcard::match(StringView value) const {
    // Dの空入力での早期終了を意図的に修正する。先頭の*が
    // 、必要な末尾（*aや*?など）を捨ててはならない。
    if (value.empty()) return std::all_of(pattern_->begin(), pattern_->end(),
                                         [](const auto& left) { return left.empty(); });
    const auto input = enumutils::toUtf32Replacing(value);
    std::u32string_view source(input);
    std::size_t length = 0;
    const auto index = findRange(source, 0, false, length);
    return index == 0 && input.size() == length;
}

String Wildcard::replace(StringView value, StringView replacement) const {
    if (value.empty()) return String(value);
    const auto input = enumutils::toUtf32Replacing(value);
    const auto replacement_chars = enumutils::toUtf32Replacing(replacement);
    // Dの不具合への意図的な修正。空パターンでは一致長0が続くため、Dの入力
    // ・置換文字列の復号後に、既存のネイティブの何もしない動作を維持する。
    if (pattern_->size() == 1U && pattern_->front().empty()) return String(value);
    std::u32string result;
    std::u32string_view source(input);
    while (!source.empty()) {
        std::size_t length = 0;
        const auto index = findRange(source, 0, false, length);
        if (index == -1) {
            result.append(source);
            break;
        }
        result.append(source.substr(0, static_cast<std::size_t>(index)));
        result.append(replacement_chars);
        source.remove_prefix(static_cast<std::size_t>(index) + length);
    }
    String output;
    for (const auto c : result) appendUtf8(output, c);
    return output;
}

std::size_t Wildcard::count(StringView value) const {
    if (value.empty()) return 0;
    const auto input = enumutils::toUtf32Replacing(value);
    // replace()と同じ、空パターンで終了させる意図的な修正。
    if (pattern_->size() == 1U && pattern_->front().empty()) return 0;
    std::u32string_view source(input);
    std::size_t count = 0;
    while (!source.empty()) {
        std::size_t length = 0;
        const auto index = findRange(source, 0, false, length);
        if (index == -1) break;
        ++count;
        source.remove_prefix(static_cast<std::size_t>(index) + length);
    }
    return count;
}

bool simpleGlob(StringView value, StringView pattern) {
    return globMatchImpl(simpleGlobPattern(value), simpleGlobPattern(pattern), 0, 0);
}

String simpleGlobTransrate(StringView pattern) { return translateGlob(pattern); }

Bytes readBinary(const Path& file) { return readFileBytes(file); }

String readTextFile(const Path& file, bool& is_sjis) {
    is_sjis = false;
    try {
        is_sjis = true;
        const auto bytes = readFileBytes(file);
        const auto raw = String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return sjis::touni(raw);
    } catch (...) {
        const auto bytes = readFileBytes(file);
        auto raw = String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        enumutils::validateUtf8(raw);
        is_sjis = false;
        return raw;
    }
}

void writeTextFile(const Path& file, StringView value, bool is_sjis,
                   const lib::Owner<filesync::FileSync>& sync) {
    const auto encoded = is_sjis ? sjis::tosjis(value) : String(value);
    const auto* data = reinterpret_cast<const std::uint8_t*>(encoded.data());
    filesync::writeFile(file, ByteView(data, encoded.size()), sync);
}

String md5Digest(ByteView data) { return md5Hex(data); }
String md5Digest(const Bytes& data) { return md5Digest(ByteView(data.data(), data.size())); }

String fileToMD5Digest(const Path& file) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) return {};
    try {
        const auto bytes = readFileBytes(file);
        if (bytes.empty() && std::filesystem::file_size(file, error) != 0U) return {};
        return md5Digest(bytes);
    } catch (const std::exception& exception) {
        fdebugln(exception.what());
        return {};
    }
}

String buildPath(StringView left, StringView right) {
    // PhobosのchainPathは表記を維持し、右側のドライブ相対パスを構成要素として扱
    // う。Windowsのfilesystem::path::operator/とは異なる。
    if (left.empty()) return String(right);
    if (right.empty()) return String(left);
    auto position = left.size();
    bool separator = false;
    const auto root = nativePathRootLength(right);
    if (root) {
        if (Path::preferred_separator == '\\' && root == 1) {
            position = nativePathRootLength(left);
            if (position && nativePathSeparator(left[position - 1])) --position;
        } else {
            position = 0;
        }
    } else {
        separator = !nativePathSeparator(left.back());
    }
    String result(left.substr(0, position));
    if (separator) result += static_cast<char>(Path::preferred_separator);
    result += right;
    return result;
}

String normpath(StringView value) {
    // 末尾の区切りやドライブ名をfilesystemライブラリの規則で扱わず、Phobos
    // のasNormalizedPathのルート・要素の規則に従う。
    const auto root = nativePathRootLength(value);
    auto position = root;
    while (position < value.size() && nativePathSeparator(value[position])) ++position;
    const bool rooted = position != 0;
    std::vector<StringView> elements;
    while (position < value.size()) {
        const auto start = position;
        while (position < value.size() && !nativePathSeparator(value[position])) ++position;
        const auto element = value.substr(start, position - start);
        while (position < value.size() && nativePathSeparator(value[position])) ++position;
        if (element == ".") continue;
        if (element == "..") {
            if (!elements.empty() && elements.back() != "..") elements.pop_back();
            else if (!rooted) elements.push_back(element);
        } else {
            elements.push_back(element);
        }
    }
    String result(value.substr(0, root));
    if (Path::preferred_separator == '\\') std::replace(result.begin(), result.end(), '/', '\\');
    for (const auto element : elements) {
        if (!result.empty() && !nativePathSeparator(result.back()))
            result += static_cast<char>(Path::preferred_separator);
        result += element;
    }
    // Phobosの文字範囲はNULを終端の番兵とする。空でない入力の最初
    // に出力する単位がNULなら、コンストラクターはドットで置き換える。
    if (!result.empty() && result.front() == '\0') result.front() = '.';
    if (const auto nul = result.find('\0'); nul != String::npos) result.resize(nul);
    return result == "." ? String{} : result;
}

Path normpath(const Path& value) {
    return pathFromUtf8(normpath(StringView(pathString(value))));
}

String nabs(StringView value) { return normpath(StringView(absolutePathValue(value))); }
Path nabs(const Path& value) { return pathFromUtf8(nabs(StringView(pathString(value)))); }

String abs2rel(StringView first, StringView second) {
    const auto path = nabs(first);
    // DのrelativePathの基準引数は遅延評価。特に最初のパスが空
    // の場合、二つ目のパスや現在のディレクトリを解決・拒否してはならない。
    if (!nativePathAbsolute(path)) return normpath(StringView(path));
    const auto base = nabs(second);
    return normpath(StringView(relativePathValue(path, base)));
}

Path abs2rel(const Path& first, const Path& second) {
    return pathFromUtf8(abs2rel(StringView(pathString(first)), StringView(pathString(second))));
}

bool isSubDirectory(StringView value, StringView parent) {
    const auto relative = abs2rel(value, parent);
    if (nativePathAbsolute(relative)) return false;
    return relative.empty() || relative == "." ||
        !(relative == ".." || relative.starts_with(".." + String(1, static_cast<char>(Path::preferred_separator))));
}

bool isSubDirectory(const Path& value, const Path& parent) {
    return isSubDirectory(StringView(pathString(value)), StringView(pathString(parent)));
}

bool isOuterPath(StringView value) {
    const auto normal = normpath(value);
    return normal.empty() || normal == "." || normal == ".." ||
        normal.starts_with(".." + String(1, static_cast<char>(Path::preferred_separator))) ||
        nativePathRootLength(normal) != 0;
}

bool istartsWith(StringView value, StringView prefix) {
    if (value.size() < prefix.size()) return false;
    const auto first = enumutils::toUtf32(value);
    const auto second = enumutils::toUtf32(prefix);
    if (first.size() < second.size()) return false;
    return enumutils::compareIgnoreCase(std::u32string_view(first).substr(0, second.size()),
                                       std::u32string_view(second)) == 0;
}

bool iendsWith(StringView value, StringView suffix) {
    // Dはicmpの呼び出し前にUTF-8のバイト長で末尾を選ぶ。入力全体を
    // ケースフォールドしたり、先にスカラー値の個数で末尾を選んだりしない。
    return value.size() >= suffix.size() &&
        enumutils::compareIgnoreCase(value.substr(value.size() - suffix.size()), suffix) == 0;
}

bool containsPath(const lib::Array<String>& paths, StringView path) {
    for (const auto& pattern : paths) {
        if (cglobMatch(path, pattern)) return true;
    }
    return false;
}

bool fnstartsWith(StringView value, StringView prefix) {
    return pathCaseInsensitive() ? istartsWith(value, prefix)
                                 : value.starts_with(prefix);
}

bool fnendsWith(StringView value, StringView suffix) {
    return pathCaseInsensitive() ? iendsWith(value, suffix)
                                 : value.ends_with(suffix);
}

String encodePath(String value) {
    if (isBinImg(value)) return value;
#if defined(_WIN32)
    std::replace(value.begin(), value.end(), '\\', '/');
#endif
    return value;
}

String decodePath(String value) {
    if (isBinImg(value)) return value;
#if defined(_WIN32)
    std::replace(value.begin(), value.end(), '/', '\\');
#endif
    return value;
}

String lastRet(String value) {
    if (value.empty()) return {};
    const auto first = value.find_last_not_of('\n');
    if (first == String::npos) return {};
    value.erase(first + 1U);
    if (value.empty() || value.back() != '\n') value.push_back('\n');
    return value == "\n" ? String{} : value;
}

namespace {
String createNewNameImpl(String base, lib::Callback<bool(String)> use,
                         bool space, std::span<const String> names) {
    // デリゲートはDの引数と同じ値渡し。未設定なら呼び出し失敗（std::bad_function_call
    // ）であり、利用可能な名前とは扱わない。
    if (use(base)) return base;
    std::size_t digit_left = String::npos;
    std::size_t digit_right = String::npos;
    String digits;
    const auto is_start_name = std::find(names.begin(), names.end(), base) != names.end();
    if (!is_start_name) {
        std::size_t i = base.size();
        while (i > 0U) {
            const auto c = base[i - 1U];
            if (c < '0' || c > '9') {
                if (digit_right != String::npos) {
                    digit_left = i;
                    break;
                }
                --i;
                continue;
            }
            if (digit_right == String::npos) digit_right = i;
            digits.insert(digits.begin(), c);
            --i;
        }
        if (digit_right != String::npos && digit_left == String::npos) digit_left = 0U;
    }
    String left;
    String right;
    String format;
    std::int64_t number = 2;
    if (!digits.empty()) {
        left = base.substr(0, digit_left);
        right = base.substr(digit_right);
        const auto parsed = detail::parseDInteger<std::int64_t>(digits);
        if (!parsed) throw std::out_of_range("createNewName digit run exceeds long");
        number = *parsed;
        format = "%0" + std::to_string(digits.size()) + "d";
    } else {
        left = base + (space ? " (" : "(");
        right = ")";
        format = "%d";
    }
    // Dは候補生成と判定だけでExceptionを捕捉す
    // る。最初の判定と数値変換は、この境界の外に置く。
    try {
        for (;;) {
            String candidate = left + tryFormat(format, number) + right;
            if (use(candidate)) return candidate;
            if (number == std::numeric_limits<std::int64_t>::max()) return base;
            ++number;
        }
    } catch (const std::exception& error) {
        (void)printStackTrace();
        debugln(error.what());
        return base;
    }
}
} // namespace

String createNewName(String base, lib::Callback<bool(String)> use,
                     bool space, const lib::Array<String>& start_names) {
    // ネイティブvector入力は、コールバック中に固定の領域・範囲を借用する。
    return createNewNameImpl(std::move(base), std::move(use), space, start_names);
}

String createNewName(String base, lib::Callback<bool(String)> use,
                     bool space, lib::SharedSlice<const String> start_names) {
    // コールバックが呼び出し側の記述子を再代入しても、Dのスライス記述子と領
    // 域を保持する。他のスライスからの項目変更は引き続き見えるようにする。
    return createNewNameImpl(std::move(base), std::move(use), space,
                             {start_names.data(), start_names.size()});
}

Path createNewFileName(const Path& value, bool is_directory) {
    return createNewFileName(value, is_directory, [](const Path& candidate) {
        std::error_code error;
        const bool present = std::filesystem::exists(candidate, error);
        if (error) throw std::filesystem::filesystem_error("createNewFileName", candidate, error);
        return !present;
    });
}

Path createNewFileName(const Path& value, bool is_directory,
                       const lib::Callback<bool(const Path&)>& available) {
    if (!available) throw std::invalid_argument("createNewFileName requires an availability predicate");
    const auto parent = value.parent_path();
    auto name = pathString(value.filename());
    auto extension = is_directory ? String{} : pathString(value.extension());
    if (!is_directory && !extension.empty()) name.resize(name.size() - extension.size());
    String extension2;
    if (!is_directory) {
        const auto info = scaledImageInfo(value);
        if (!info.path.empty()) {
            // Dはこの拡張子を除いた名前に親ディレクトリも含める。再構築時
            // に相対の親を二重追加しないよう、basenameだけを残す。
            name = pathString(info.path.stem());
            extension2 = ".x" + std::to_string(info.scale);
        }
    }
    name = createNewName(name, [=](String candidate) {
        auto result = parent / pathFromUtf8(candidate);
        if (!is_directory) result += pathFromUtf8(extension2 + extension);
        return available(result);
    }, false);
    auto result = parent / pathFromUtf8(name);
    if (!is_directory) result += pathFromUtf8(extension2 + extension);
    return result;
}

ScaledPathInfo scaledImageInfo(const Path& value) {
    if (!imagesize::isImageExt(pathString(value))) return {};
    const auto extension = value.extension().string();
    auto name = value;
    name.replace_extension();
    const auto scale_extension = lowerAscii(name.extension().string());
    for (const auto scale : {2U, 4U, 8U, 16U}) {
        if (scale_extension == ".x" + std::to_string(scale)) {
            auto base = name;
            base.replace_extension();
            base += extension;
            return {base, scale};
        }
    }
    return {};
}

Path noScaledPath(const Path& value) { return scaledImageInfo(value).path; }

ScaledPathInfo findScaledImage(const Path& value, std::uint32_t target_scale) {
    if (imagesize::isImageExt(pathString(value))) {
        auto stem = value;
        const auto extension = stem.extension();
        stem.replace_extension();
        while (target_scale > 1U) {
            auto candidate = stem;
            candidate += Path(".x" + std::to_string(target_scale));
            candidate += extension;
            if (nativePathExists(candidate) && nativePathIsFile(candidate)) return {candidate, target_scale};
            target_scale /= 2U;
        }
    }
    return {value, 1U};
}

String singleLine(StringView value) {
    String result;
    const auto space = [](char32_t c) {
        return (c >= 9 && c <= 13) || c == 0x20 || c == 0x85 || c == 0xa0 ||
            c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 ||
            c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
    };
    std::vector<char32_t> line;
    const auto flush = [&] {
        auto begin = line.begin(), end = line.end();
        while (begin != end && space(*begin)) ++begin;
        while (begin != end && space(*(end - 1))) --end;
        for (; begin != end; ++begin) appendUtf8(result, *begin);
        line.clear();
    };
    for (const auto c : decodeUtf8(value)) {
        if ((c >= 10 && c <= 13) || c == 0x85 || c == 0x2028 || c == 0x2029) flush();
        else line.push_back(c);
    }
    flush();
    return result;
}

String encodeLf(String value) {
    String result;
    result.reserve(value.size());
    for (const auto c : value) {
        if (c == '\\') result += "\\\\";
        else if (c == '\n') result += "\\n";
        else result.push_back(c);
    }
    return result;
}

String encodeXmlAndLf(String value) {
    auto result = encodeLf(std::move(value));
    String output;
    for (const auto c : result) {
        switch (c) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '"': output += "&quot;"; break;
        case '\'': output += "&apos;"; break;
        default: output.push_back(c); break;
        }
    }
    return output;
}

String encodeLf(const lib::Array<String>& values, bool last_lf) {
    String result;
    for (std::size_t i = 0; i < values.size(); ++i) {
        for (const auto c : values[i]) {
            if (c == '\\') result += "\\\\";
            else result.push_back(c);
        }
        if (last_lf || i + 1U < values.size()) result += "\\n";
    }
    return result;
}

String decodeLf2(StringView value) {
    enumutils::validateUtf8(value);
    String result;
    bool escaped = false;
    for (const auto c : value) {
        if (escaped) {
            if (c == 'n') result.push_back('\n');
            else if (c == '\\') result.push_back('\\');
            else {
                result.push_back('\\');
                result.push_back(c);
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else {
            result.push_back(c);
        }
    }
    if (escaped) result.push_back('\\');
    return result;
}

namespace {
std::size_t lineSeparatorSize(StringView text,std::size_t i) noexcept {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c == '\r') return i+1<text.size() && text[i+1]=='\n' ? 2 : 1;
    if (c == '\n' || c == '\v' || c == '\f') return 1;
    if (c == 0xc2 && i+1<text.size() && static_cast<unsigned char>(text[i+1])==0x85) return 2;
    if (c == 0xe2 && i+2<text.size() && static_cast<unsigned char>(text[i+1])==0x80 &&
        (static_cast<unsigned char>(text[i+2])==0xa8 || static_cast<unsigned char>(text[i+2])==0xa9)) return 3;
    return 0;
}
}

bool isNumeric(StringView value) {
    // Phobosはコード単位を走査し、ASCIIだけを大文字小文字変換する。コ
    // ンテントの振り分けと警告検査で、同じ確保不要のリテラルパーサーを共有する。
    if (value.empty()) return false;
    const auto equalAscii = [](StringView text, StringView expected) {
        if (text.size() != expected.size()) return false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const auto c = static_cast<char>(text[i] >= 'A' && text[i] <= 'Z'
                                                ? text[i] + ('a' - 'A') : text[i]);
            if (c != expected[i]) return false;
        }
        return true;
    };
    if (equalAscii(value, "nan") || equalAscii(value, "nani") || equalAscii(value, "nan+nani") ||
        equalAscii(value, "inf") || equalAscii(value, "-inf")) return true;
    auto text = value;
    bool point = false, exponent = false, complex = false, digits = false;
    const auto sign = [](char c) { return c == '+' || c == '-'; };
    if (sign(text.front())) text.remove_prefix(1);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto raw = static_cast<unsigned char>(text[i]);
        if (raw > 127) return false;
        const auto c = static_cast<char>(raw >= 'A' && raw <= 'Z' ? raw + ('a' - 'A') : raw);
        if (c >= '0' && c <= '9') { digits = true; continue; }
        if (c == '+') {
            if (i == 0) return false;
            point = exponent = digits = false; complex = true; continue;
        }
        if (c == 'e') {
            if (exponent || i + 1 == text.size() || !sign(text[i + 1])) return false;
            exponent = true; ++i; continue;
        }
        if (c == '.') { if (point) return false; point = true; continue; }
        if (!digits) return false;
        const bool integer = !point && !exponent && !complex;
        if (text.size() - i == 2) {
            const auto suffix = text.substr(i);
            return equalAscii(suffix, "ul") ? integer : equalAscii(suffix, "fi") || equalAscii(suffix, "li");
        }
        if (text.size() - i == 1) {
            if (integer && (c == 'u' || c == 'l')) return true;
            return complex ? c == 'i' : c == 'l' || c == 'f' || c == 'i';
        }
        return false;
    }
    return digits;
}

lib::Array<String> splitLines(StringView text,bool keepTerminator) {
    // Dのstd.string.splitLinesと共通の区切り規則。エ
    // スケープの展開と空行の除外は、この基本処理ではなく呼び出し側で行う。
    enumutils::validateUtf8(text);
    lib::Array<String> lines;
    std::size_t start{};
    for (std::size_t i=0;i<text.size();) {
        const auto length=lineSeparatorSize(text,i);
        if (!length) {++i;continue;}
        lines.emplace_back(text.substr(start,i-start+(keepTerminator?length:0)));
        i+=length;start=i;
    }
    if (start<text.size()) lines.emplace_back(text.substr(start));
    return lines;
}

String chompLine(StringView text) {
    enumutils::validateUtf8(text);
    for (std::size_t offset=1;offset<=3 && offset<=text.size();++offset) {
        const auto start=text.size()-offset;
        if (lineSeparatorSize(text,start)==offset) {
            // CRを残さず、CRLFを一つの改行として優先する。
            if (offset==1 && text.back()=='\n' && start>0 && text[start-1]=='\r') return String(text.substr(0,start-1));
            return String(text.substr(0,start));
        }
    }
    return String(text);
}

lib::Array<String> decodeLf(StringView value, bool use_empty) {
    auto result=splitLines(decodeLf2(value));
    if (use_empty) while (!result.empty() && result.back().empty()) result.pop_back();
    else std::erase_if(result,[](const auto& line){return line.empty();});
    return result;
}

String fromBool(bool value) { return value ? "True" : "False"; }

bool parseBool(StringView value) {
    const auto lower = lowerAscii(value);
    if (lower == "true" || lower == "1") return true;
    if (lower == "false" || lower == "0") return false;
    throw std::invalid_argument("Not bool: " + String(value));
}

String toHex(StringView value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    String result;
    result.reserve(value.size() * 3U);
    for (const auto byte : value) {
        const auto c = static_cast<unsigned char>(byte);
        result.push_back('%');
        result.push_back(hex[c >> 4U]);
        result.push_back(hex[c & 0x0fU]);
    }
    return result;
}

String caseFold(const StringView value) {
    return enumutils::lowerCase(value);
}

String getenv(StringView variable) {
    const auto name = String(variable);
#if defined(_WIN32)
    const int name_length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()),
        nullptr, 0);
    if (name_length <= 0) return {};
    std::wstring wide_name(static_cast<std::size_t>(name_length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
                        static_cast<int>(name.size()), wide_name.data(), name_length);
    wchar_t* value = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&value, &length, wide_name.c_str()) != 0 || value == nullptr) return {};
    const int utf8_length = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value,
        static_cast<int>(length == 0U ? 0U : length - 1U), nullptr, 0, nullptr, nullptr);
    String result(static_cast<std::size_t>(std::max(utf8_length, 0)), '\0');
    if (utf8_length > 0)
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
                            static_cast<int>(length - 1U), result.data(), utf8_length,
                            nullptr, nullptr);
    std::free(value);
    return result;
#else
    const auto* value = std::getenv(name.c_str());
    return value == nullptr ? String{} : String(value);
#endif
}

bool isFileNameChar(char32_t value) noexcept {
    // 全対象環境でDのWindows版Editorのファイ
    // ル名変換を使う。同じ文書をWeb/POSIXで保存
    // してもシナリオ名を変えないため。DのOS別の選択を
    // 、環境間で共通の文書命名規則へ意図的に置き換える。
    if (value == 0 || value < 0x20U) return false;
    switch (value) {
    case U'\\': case U'/': case U':': case U'*': case U'?': case U'"':
    case U'<': case U'>': case U'|': return false;
    default: return true;
    }
}

String toFileName(String value, char32_t replacement) {
    String result;
    for (const auto c : decodeUtf8(value)) {
        if (isFileNameChar(c)) appendUtf8(result, c);
        else appendUtf8(result, replacement);
    }
    if (!result.empty() && result.back() == '.') {
        result.pop_back();
        appendUtf8(result, replacement);
    }
    return result;
}

String cleanFileName(String value) {
    // Windows版DはtoFileNameの前に両方の区切りを除く。バックス
    // ラッシュを含む共通サンプルのパッケージ名も、Webで同じファイル名を生成す
    // る必要がある。バックスラッシュを下線へ置き換えても名前が変わってしまう。
    value.erase(std::remove(value.begin(), value.end(), '\\'), value.end());
    value.erase(std::remove(value.begin(), value.end(), '/'), value.end());
    value.erase(std::remove(value.begin(), value.end(), '.'), value.end());
    value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
    value = toFileName(std::move(value));
    return value.empty() ? String("noname") : value;
}

Path createFileI(const Path& parent, StringView name, StringView extension,
                 StringView prefix, bool ignore_exists_file) {
    auto value = pathFromUtf8(toFileName(String(prefix) + cleanFileName(String(name))));
    if (!extension.empty()) {
        auto ext = String(extension);
        if (!ext.empty() && ext.front() != '.') ext.insert(ext.begin(), '.');
        value.replace_extension(pathFromUtf8(ext));
    }
    value = parent / value;
    return ignore_exists_file ? value : createNewFileName(value, false);
}

Path createFolder(const Path& parent, StringView name, bool ignore_exists_file) {
    const auto value = parent / pathFromUtf8(cleanFileName(String(name)));
    return ignore_exists_file ? value : createNewFileName(value, true);
}

void preRemove(const Path& path) {
#if defined(_WIN32)
    if (std::filesystem::exists(path)) {
        ::SetFileAttributesW(path.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
    }
#else
    (void)path;
#endif
}

Path copyTo(const Path& scenario_path, const Path& path, const Path& added,
            bool bin_img_to_ref, StringView exported_image_name) {
    const auto source_string = pathString(path);
    const auto is_binary_image = isBinImg(source_string);
    if (is_binary_image && !bin_img_to_ref) return {};
    const auto target_directory = pathFromUtf8(buildPath(pathString(scenario_path), pathString(added)));
    // Dは存在しないディレクトリだけを作り、ファイルシステムの失敗を伝播
    // する。空は上記の明示的な「埋め込み画像を維持」の結果として扱う。
    if (!std::filesystem::exists(target_directory))
        std::filesystem::create_directories(target_directory);
    Path destination;
    Bytes bytes;
    if (is_binary_image) {
        bytes = strToBImg(source_string);
        auto name = exported_image_name.empty() ? String("@simage(1)")
                                                : cleanFileName(String(exported_image_name));
        name += imagesize::imageType(ByteView(bytes.data(), bytes.size()));
        destination = pathFromUtf8(buildPath(pathString(target_directory), name));
    } else {
        destination = pathFromUtf8(buildPath(pathString(target_directory), pathBaseName(source_string)));
    }
    destination = createNewFileName(destination, false);
    if (is_binary_image) {
        std::ofstream output;
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output.open(destination, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        // 開くことに成功しても、埋め込み素材がディスクへ書けたとは限
        // らない。短い書き込みや失敗の後に新しい参照を公開しない。
        output.close();
    } else {
        std::filesystem::copy_file(path, destination,
                                   std::filesystem::copy_options::overwrite_existing);
    }
    return pathFromUtf8(buildPath(pathString(added), pathBaseName(pathString(destination))));
}

void copyAll(const Path& source, const Path& destination, bool overwrite) {
    std::error_code error;
    if (normalizedAbsolute(source) == normalizedAbsolute(destination)) return;
    const bool source_exists = std::filesystem::exists(source, error);
    if (error) throw std::filesystem::filesystem_error(
        "Unable to inspect copy source", source, error);
    if (!source_exists) return;
    const bool source_directory = std::filesystem::is_directory(source, error);
    if (error) throw std::filesystem::filesystem_error(
        "Unable to inspect copy source", source, error);
    if (source_directory) {
        std::filesystem::create_directories(destination, error);
        if (error) throw std::filesystem::filesystem_error(
            "Unable to create copy destination", destination, error);
        std::filesystem::directory_iterator current(source, error), end;
        if (error) throw std::filesystem::filesystem_error(
            "Unable to enumerate copy source", source, error);
        while (current != end) {
            copyAll(current->path(), destination / current->path().filename(), overwrite);
            current.increment(error);
            if (error) throw std::filesystem::filesystem_error(
                "Unable to enumerate copy source", source, error);
        }
    } else {
        const auto parent = destination.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, error);
            if (error) throw std::filesystem::filesystem_error(
                "Unable to create copy destination", parent, error);
        }
        // ファイルシステムが対応する場合、overwrite_existing
        // はアトミックに置換する。コピー成功前に旧ファイルを削除しない。
        std::filesystem::copy_file(source, destination,
                                   overwrite ? std::filesystem::copy_options::overwrite_existing
                                              : std::filesystem::copy_options::none,
                                   error);
        if (error) throw std::filesystem::filesystem_error(
            "Unable to copy file", source, destination, error);
    }
}

void renameFile(const Path& from, const Path& to) {
    const auto source = nativeRenamePath(from);
    const auto destination = nativeRenamePath(to);
    const auto source_text = pathString(source);
    const auto destination_text = pathString(destination);
    const bool same_drive = cfnmatch(nativePathDriveName(source_text),
                                    nativePathDriveName(destination_text));
    preRemove(destination);
    if (same_drive) {
        // Dは同一ドライブの名前変更失敗を伝播し、再帰コピーとして再試行しな
        // い。POSIXもマウントをまたぐ場合を含め、名前変更だけを使う。
        std::filesystem::rename(source, destination);
    } else {
        std::filesystem::copy_file(source, destination,
                                   std::filesystem::copy_options::overwrite_existing);
        preRemove(source);
        if (!std::filesystem::remove(source))
            throw std::filesystem::filesystem_error("remove", source,
                std::make_error_code(std::errc::no_such_file_or_directory));
    }
}

namespace {
struct NativeDirectoryCloser {
#if defined(_WIN32)
    void operator()(void* handle) const noexcept { ::FindClose(handle); }
#else
    void operator()(DIR* handle) const noexcept { ::closedir(handle); }
#endif
};
#if defined(_WIN32)
using NativeDirectoryHandle = std::unique_ptr<void, NativeDirectoryCloser>;
#else
using NativeDirectoryHandle = std::unique_ptr<DIR, NativeDirectoryCloser>;
#endif
} // namespace

struct ShallowDirectory::Impl {
    explicit Impl(Path directory) : directory_(std::move(directory)) {
#if defined(_WIN32)
        const auto pattern = pathFromUtf8(buildPath(pathString(directory_), "*.*"));
        const auto handle = ::FindFirstFileW(pattern.c_str(), &find_data_);
        if (handle == INVALID_HANDLE_VALUE) {
            const auto failure = ::GetLastError();
            throw std::filesystem::filesystem_error("FindFirstFile", directory_,
                std::error_code(static_cast<int>(failure), std::system_category()));
        }
        handle_.reset(handle);
#else
        auto* handle = ::opendir(directory_.empty() ? "." : directory_.c_str());
        if (!handle) {
            const auto failure = errno;
            throw std::filesystem::filesystem_error("opendir", directory_,
                std::error_code(failure, std::generic_category()));
        }
        handle_.reset(handle);
#endif
        load(false);
    }

    bool readNative(bool advance) {
#if defined(_WIN32)
        if (advance && !::FindNextFileW(handle_.get(), &find_data_)) return false;
        find_data_.cFileName[std::size(find_data_.cFileName) - 1U] = L'\0';
        native_name_ = Path(find_data_.cFileName);
        native_attributes_ = find_data_.dwFileAttributes;
        const auto ticks = [](const FILETIME& time) {
            return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32U) | time.dwLowDateTime;
        };
        native_times_ = std::array<std::uint64_t, 3>{ticks(find_data_.ftCreationTime),
            ticks(find_data_.ftLastAccessTime), ticks(find_data_.ftLastWriteTime)};
#else
        (void)advance;
        const auto* entry = ::readdir(handle_.get());
        if (!entry) return false;
        native_name_ = Path(entry->d_name);
#endif
        return true;
    }

    void load(bool advance) {
        // ネイティブのnext=falseはPhobosでも終端を表し、errnoやGetLastError
        // の追加検査はしない。ドットの除外と項目の構築は共有する。
        while (readNative(advance)) {
            advance = true;
            if (native_name_ == Path(".") || native_name_ == Path("..")) continue;
            Entry replacement;
            replacement.name_ = buildPath(pathString(directory_), pathString(native_name_));
            // WindowsのDirEntryは、呼び出し側がname/isFileしか
            // 読まない場合も、属性を保存する前に三つのSysTimeをすべて構築する。
            if (native_times_) {
                for (const auto ticks : *native_times_) (void)stdTimeFromFileTimeTicks(ticks);
            }
            replacement.attributes_ = native_attributes_;
            current_ = std::move(replacement);
            empty_ = false;
            return;
        }
        handle_.reset();
        empty_ = true;
    }

    // PhobosのRefCounted/DirHandleの解放をRAIIで置き換
    // える。ネイティブの名前の構築失敗も含む。カーソルのコピーは同じ状態を共有する。
    Path directory_;
    NativeDirectoryHandle handle_;
#if defined(_WIN32)
    WIN32_FIND_DATAW find_data_{};
#endif
    Path native_name_;
    std::uint32_t native_attributes_{};
    std::optional<std::array<std::uint64_t, 3>> native_times_;
    Entry current_;
    bool empty_{true};
};

void ShallowDirectory::Entry::loadType() const {
#if defined(_WIN32)
    // リンクをたどったり、途中で変わり得るファイルを再照会した
    // りせず、再解析ポイントのフラグも含む列挙時の状態を使う。
    is_directory_ = (attributes_ & FILE_ATTRIBUTE_DIRECTORY) != 0;
    is_file_ = !*is_directory_;
#else
    if (is_file_) return;
    struct stat attributes{};
    if (::stat(name_.c_str(), &attributes) == 0) {
        is_file_ = S_ISREG(attributes.st_mode) != 0;
        is_directory_ = S_ISDIR(attributes.st_mode) != 0;
        return;
    }
    if (!did_lstat_) {
        if (::lstat(name_.c_str(), &attributes) != 0) {
            const auto failure = errno;
            throw std::filesystem::filesystem_error("lstat", pathFromUtf8(name_),
                std::error_code(failure, std::generic_category()));
        }
        did_lstat_ = true;
    }
    // Phobosはstatが失敗しlstatが成功した項目を、すべてリンクとして扱う。成
    // 功したstatだけをキャッシュし、後のプロパティ問い合わせではstatを再試行する。
#endif
}

bool ShallowDirectory::Entry::isFile() const {
    loadType();
    return is_file_.value_or(false);
}

bool ShallowDirectory::Entry::isDirectory() const {
    loadType();
    return is_directory_.value_or(false);
}

ShallowDirectory::ShallowDirectory(Path directory)
    : impl_(std::make_shared<Impl>(std::move(directory))) {}

bool ShallowDirectory::empty() const noexcept { return !impl_ || impl_->empty_; }

ShallowDirectory::Entry ShallowDirectory::front() const {
    // 契約外の空範囲へのアクセスで古い状態を公開せず
    // 、ネイティブの入力範囲の前提条件を検査する。
    if (empty()) throw std::logic_error("Empty directory range front");
    return impl_->current_;
}

void ShallowDirectory::popFront() {
    if (empty()) throw std::logic_error("Empty directory range popFront");
    impl_->load(true);
}

struct DepthDirectory::Impl {
    explicit Impl(Path directory) {
        ShallowDirectory root(std::move(directory));
        if (root.empty()) return;
        current_ = root.front();
        stack_.push_back(std::move(root));
        descend();
    }

    void descend() {
        while (current_->isDirectory()) {
            auto directory = *current_;
            ShallowDirectory children(pathFromUtf8(directory.name()));
            if (children.empty()) break;
            current_ = children.front();
            stack_.push_back(std::move(children));
            stashed_.push_back(std::move(directory));
        }
    }

    void popFront() {
        stack_.back().popFront();
        if (!stack_.back().empty()) {
            current_ = stack_.back().front();
            descend();
        } else {
            stack_.pop_back();
            if (!stashed_.empty()) {
                current_ = std::move(stashed_.back());
                stashed_.pop_back();
            }
        }
    }

    std::vector<ShallowDirectory> stack_;
    std::vector<Entry> stashed_;
    std::optional<Entry> current_;
};

DepthDirectory::DepthDirectory(Path directory)
    : impl_(std::make_shared<Impl>(std::move(directory))) {}

bool DepthDirectory::empty() const noexcept {
    return !impl_ || (impl_->stack_.empty() && impl_->stashed_.empty());
}

DepthDirectory::Entry DepthDirectory::front() const {
    if (empty()) throw std::logic_error("Empty depth directory range front");
    return *impl_->current_;
}

void DepthDirectory::popFront() {
    if (empty()) throw std::logic_error("Empty depth directory range popFront");
    impl_->popFront();
}

lib::Array<String> clistdir(const Path& directory) {
    lib::Array<String> result;
    try {
        if (!pathExists(directory)) return result;
        for (ShallowDirectory entries(directory); !entries.empty(); entries.popFront()) {
            const auto entry = entries.front();
            result.emplace_back(pathBaseName(entry.name()));
        }
    } catch (const std::bad_alloc&) {
        // Dのメモリ確保のErrorはcatch(Exception)の対象外。
        throw;
    } catch (const std::exception& exception) {
        (void)printStackTrace();
        fdebugln(exception.what());
    }
    return result;
}

void delAll(const Path& path, bool force) {
    std::exception_ptr first_error;
    const auto remove_one = [&](const auto& self, const Path& current) -> void {
        try {
            std::error_code status_error;
            const auto status = std::filesystem::symlink_status(current, status_error);
            if (status_error == std::errc::no_such_file_or_directory) return;
            if (status_error) throw std::filesystem::filesystem_error(
                "status", current, status_error);
            preRemove(current);
            if (std::filesystem::is_directory(status) &&
                !std::filesystem::is_symlink(status)) {
                std::error_code iteration_error;
                std::filesystem::directory_iterator iterator(current, iteration_error);
                if (iteration_error) throw std::filesystem::filesystem_error(
                    "directory_iterator", current, iteration_error);
                for (const auto& child : iterator) self(self, child.path());
            }
            std::error_code remove_error;
            std::filesystem::remove(current, remove_error);
            if (remove_error) throw std::filesystem::filesystem_error(
                "remove", current, remove_error);
        } catch (...) {
            if (!force) throw;
            // Dはforce=trueの後処理後に、最初のFileException
            // のメッセージを包み直す。C++では元のfilesystem_error
            // を維持し、ネイティブの呼び出し側がerror_codeとパスを
            // 保持できるようにする。削除順の変更ではなく、言語境界の違い。
            if (!first_error) first_error = std::current_exception();
        }
    };
    remove_one(remove_one, path);
    if (first_error) std::rethrow_exception(first_error);
}

int fncmp(StringView first, StringView second) {
    return pathCaseInsensitive() ? enumutils::compareIgnoreCase(first, second)
                                 : first.compare(second);
}

int detail::naturalCompare(std::u32string_view first, std::u32string_view second,
                          bool ignore_case, bool filename_case) {
    const bool insensitive = ignore_case || (filename_case && pathCaseInsensitive());
    const auto compare = [insensitive](std::u32string_view left, std::u32string_view right) -> int {
        if (insensitive) return enumutils::compareIgnoreCase(left, right);
        const auto count = std::min(left.size(), right.size());
        for (std::size_t i = 0; i < count; ++i) {
            if (left[i] != right[i])
                return static_cast<std::int32_t>(static_cast<std::uint32_t>(left[i]) -
                                                 static_cast<std::uint32_t>(right[i]));
        }
        return static_cast<int>(left.size() > right.size()) -
               static_cast<int>(left.size() < right.size());
    };
    const auto digit = [](char32_t value) { return value >= U'0' && value <= U'9'; };
    std::size_t i = 0, j = 0;
    while (i < first.size() || j < second.size()) {
        if (i == first.size()) return -1;
        if (j == second.size()) return 1;
        auto iEnd = i, jEnd = j;
        while (iEnd < first.size() && digit(first[iEnd])) ++iEnd;
        while (jEnd < second.size() && digit(second[jEnd])) ++jEnd;
        const auto left = first.substr(i, iEnd - i);
        const auto right = second.substr(j, jEnd - j);
        if (!left.empty() && !right.empty()) {
            if (left.size() < right.size()) {
                const auto result = compare(zfill_(left, right.size()), right);
                if (result) return result;
            } else if (left.size() > right.size()) {
                const auto result = compare(left, zfill_(right, left.size()));
                if (result) return result;
            }
            const auto result = compare(left, right);
            if (result) return result;
            i = iEnd;
            j = jEnd;
        } else {
            // Dは完全にケースフォールドした文字列ではなく、一スカラ
            // ー値のスライスを比較する。その範囲を維持し、大文字小文
            // 字の展開が次の比較対象の文字まで消費しないようにする。
            if (compare(first.substr(i, 1U), second.substr(j, 1U)) < 0) return -1;
            if (compare(first.substr(i, 1U), second.substr(j, 1U)) > 0) return 1;
            ++i;
            ++j;
        }
    }
    return 0;
}

bool NCompareItem::operator==(const NCompareItem& other) const noexcept {
    return type == other.type && str == other.str;
}

int NCompareItem::compare(const NCompareItem& other) const noexcept {
    if (type == other.type && type == NCompareType::dec) {
        // ファクトリーの十進数字列は非負整数。有効桁のビューを比較
        // し、このnoexcept APIではメモリを確保しない。
        StringView left(number), right(other.number);
        const auto trim = [](StringView& value) {
            const auto first = value.find_first_not_of('0');
            if (first == StringView::npos) value = "0";
            else value.remove_prefix(first);
        };
        trim(left);
        trim(right);
        if (left.size() != right.size()) return left.size() < right.size() ? -1 : 1;
        if (left != right) return left < right ? -1 : 1;
    }
    return str < other.str ? -1 : (str > other.str ? 1 : 0);
}

NCompare::NCompare(String value, bool ignore_case, bool logical_sort)
    : NCompare(opCall(std::move(value), ignore_case, logical_sort)) {}

NCompare NCompare::opCall(String value, bool ignore_case, bool logical_sort) {
    NCompare result;
    result.value = std::move(value);
    if (result.value.empty()) return result;
    if (!logical_sort) {
        auto text = result.value;
        if (ignore_case) text = caseFold(text);
        result.items.push_back({NCompareType::str, std::move(text), {}});
        return result;
    }
    std::size_t from = 0;
    bool numeric = false;
    for (std::size_t i = 0; i < result.value.size(); ++i) {
        const bool next_numeric = result.value[i] >= '0' && result.value[i] <= '9';
        if (i != from && next_numeric != numeric) {
            const auto text = result.value.substr(from, i - from);
            if (numeric) result.items.push_back({NCompareType::dec, text, text});
            else result.items.push_back({NCompareType::str, ignore_case ? caseFold(text) : text, {}});
            from = i;
        }
        numeric = next_numeric;
    }
    const auto text = result.value.substr(from);
    if (numeric) result.items.push_back({NCompareType::dec, text, text});
    else result.items.push_back({NCompareType::str, ignore_case ? caseFold(text) : text, {}});
    return result;
}

bool NCompare::operator==(const NCompare& other) const noexcept { return items == other.items; }

int NCompare::compare(const NCompare& other) const noexcept {
    const auto count = std::min(items.size(), other.items.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto result = items[i].compare(other.items[i]);
        if (result != 0) return result;
    }
    return items.size() < other.items.size() ? -1 : (items.size() > other.items.size() ? 1 : 0);
}

bool exec(StringView process, const Path& work_dir, bool console, bool wait) {
#if defined(_WIN32)
    auto command = pathFromUtf8(process).wstring();
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!console) {
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
    }
    PROCESS_INFORMATION info{};
    const auto working_directory = work_dir.wstring();
    const auto working = work_dir.empty() ? static_cast<LPCWSTR>(nullptr)
                                          : working_directory.c_str();
    const auto success = ::CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr,
                                           FALSE, console ? 0U : CREATE_NEW_CONSOLE,
                                           nullptr, working, &startup, &info) != FALSE;
    if (!success) return false;
    if (wait) ::WaitForSingleObject(info.hProcess, INFINITE);
    ::CloseHandle(info.hThread);
    // Dはスレッドハンドルを閉じるが、プロセスハンドルを残す。呼び出しごとの漏
    // れを避けるため、省略可能な待機の後に両方のネイティブハンドルを閉じる。
    ::CloseHandle(info.hProcess);
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    // iOSは任意のプロセス作成を提供しない。呼び出し側の既存
    // の機能判定はfalseを外部プロセス未対応の結果として
    // 扱うため、ここでstd::systemへ切り替えない。
    (void)process;
    (void)work_dir;
    (void)console;
    (void)wait;
    return false;
#else
    // Dの未検証のPOSIXのexecv(process, null)に対する意図的な
    // 拡張。Editorの呼び出し側はコマンド行を渡し、waitではWindowsと同
    // じく終了を待ってから戻る。共通APIは子の終了状態ではなく、起動成功を報告する。
    const auto pid = ::fork();
    if (pid < 0) return false;
    if (pid == 0) {
        if (!work_dir.empty()) ::chdir(work_dir.c_str());
        const auto status = std::system(String(process).c_str());
        std::_Exit(status == 0 ? 0 : 1);
    }
    if (wait) {
        int status = 0;
        // 上記のWaitForSingleObjectと同様に、待機しても起動結果
        // は変えない。シグナルで待機を飛ばさないよう、中断された待機を再試行する。
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
    (void)console;
#endif
    return true;
}

bool hasPath(const Path& root, const Path& path) {
    // Dの不具合への意図的な修正。末尾の区切りを理由に子孫を拒否しない
    // 。共通の接頭辞・境界の規則により、似た名前の兄弟とは区別する。
    return isPathPrefix(StringView(pathString(root)), StringView(pathString(path)));
}

bool hasPath(StringView root, StringView path) {
    return isPathPrefix(root, path);
}

bool detail::cachePathExists(const Path& path) noexcept {
    return pathExists(path);
}

bool pathExists(const Path& path) noexcept {
    return nativePathExists(path);
}

bool pathIsDirectory(const Path& path) {
    return nativePathKind(path).directory;
}

bool canAccessPath(StringView path) {
#if defined(_WIN32)
    return nativePathExists(pathFromUtf8(path));
#else
    // DにはここでPOSIXの事前検査を行う処理がない。後のディレクトリ
    // 問い合わせで失敗を報告するため、独自の状態・権限判定を追加しない。
    (void)path;
    return true;
#endif
}

Path detail::fileCacheKey(const Path& path) {
    auto key = nabs(StringView(pathString(path)));
    if (pathCaseInsensitive()) key = caseFold(key);
    return pathFromUtf8(key);
}

std::int64_t detail::cacheModifiedTime(const Path& path) {
    // OS固有なのはPhobosと同じ時刻取得だけ。Windowsは再解析ポイン
    // トの情報を含むGetFileAttributesEx、POSIXはリンクを
    // たどるstatを使う。キャッシュの規則はすべてFileCacheに置く。
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        const auto failure = ::GetLastError();
        throw std::filesystem::filesystem_error("GetFileAttributesEx", path,
            std::error_code(static_cast<int>(failure), std::system_category()));
    }
    const auto ticks = (static_cast<std::uint64_t>(attributes.ftLastWriteTime.dwHighDateTime) << 32U)
                     | attributes.ftLastWriteTime.dwLowDateTime;
    return stdTimeFromFileTimeTicks(ticks);
#else
    struct stat attributes{};
    if (::stat(path.c_str(), &attributes) != 0) {
        const auto failure = errno;
        throw std::filesystem::filesystem_error("stat", path,
            std::error_code(failure, std::generic_category()));
    }
    const auto nanoseconds = []<typename Stat>(const Stat& value) -> std::int64_t {
        if constexpr (requires { value.st_mtim.tv_nsec; }) return value.st_mtim.tv_nsec;
        else if constexpr (requires { value.st_mtimespec.tv_nsec; }) return value.st_mtimespec.tv_nsec;
        else if constexpr (requires { value.st_mtimensec; }) return value.st_mtimensec;
        else if constexpr (requires { value.st_mtime_nsec; }) return value.st_mtime_nsec;
        else if constexpr (requires { value.__st_mtimensec; }) return value.__st_mtimensec;
        else return 0;
    }(attributes);
    // Phobosは秒未満の値を100ns単位に切り捨てる。符号なし演算でDのlong
    // の折り返しを表し、C++の符号付きオーバーフローの未定義動作を避ける。
    const auto ticks = 621'355'968'000'000'000ULL
                     + static_cast<std::uint64_t>(attributes.st_mtime) * 10'000'000ULL
                     + static_cast<std::uint64_t>(nanoseconds / 100);
    return std::bit_cast<std::int64_t>(ticks);
#endif
}

bool hasParDir(StringView value) {
    const auto normal = normpath(value);
    if (normal.empty()) return false;
    const auto parent = Path(".." + std::string(1, Path::preferred_separator));
    const auto normal_string = normal;
    if (normal_string.starts_with(parent.string())) return true;
    const auto needle = std::string(1, Path::preferred_separator) + ".." +
                        std::string(1, Path::preferred_separator);
    return normal_string.find(needle) != String::npos;
}

std::size_t icount(StringView value, StringView sub) {
    if (sub.empty()) return 0;
    std::size_t count = 0;
    std::size_t from = 0;
    while (from <= value.size()) {
        const auto lower_value = lowerAscii(value.substr(from));
        const auto lower_sub = lowerAscii(sub);
        const auto position = lower_value.find(lower_sub);
        if (position == String::npos) break;
        ++count;
        from += position + sub.size();
    }
    return count;
}

String ireplace(String value, StringView from, StringView to) {
    if (from.empty()) return value;
    String result;
    std::size_t offset = 0;
    const auto lower_from = lowerAscii(from);
    while (offset <= value.size()) {
        const auto lower_value = lowerAscii(StringView(value).substr(offset));
        const auto position = lower_value.find(lower_from);
        if (position == String::npos) {
            result += value.substr(offset);
            break;
        }
        result += value.substr(offset, position);
        result += to;
        offset += position + from.size();
    }
    return result;
}

std::size_t lengthJ(StringView text) {
    std::size_t result = 0;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto scalar = enumutils::decodeUtf8Scalar(text, offset);
        result += scalar.next - offset > 1U ? 2U : 1U;
        offset = scalar.next;
    }
    return result;
}

std::size_t lineCount(const lib::Array<String>& lines) {
    std::size_t first = lines.size();
    std::size_t last = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!lines[i].empty()) {
            first = std::min(first, i);
            last = i + 1U;
        }
    }
    return first == lines.size() ? 0U : last - first;
}

String sliceJ(StringView text, std::size_t from, std::size_t to) {
    const auto rangeError = [&] {
        // Use a native exception type with the same input/range diagnostic.
        return std::out_of_range("text: " + String(text) + ", from: " +
                                 std::to_string(from) + ", to: " + std::to_string(to));
    };
    if (from > to) throw rangeError();
    std::size_t width = 0;
    std::size_t begin_byte = String::npos;
    std::size_t end_byte = String::npos;
    std::size_t byte = 0;
    // Decode incrementally: D stops foreach once the requested end is found,
    // so validating or replacing an unread suffix would change its behavior.
    while (byte < text.size()) {
        const auto scalar = enumutils::decodeUtf8Scalar(text, byte);
        const auto next_width = width + (scalar.next - byte > 1U ? 2U : 1U);
        if (begin_byte == String::npos && from <= width) begin_byte = byte;
        if (to <= next_width) {
            end_byte = to < next_width ? byte : scalar.next;
            if (begin_byte == String::npos) begin_byte = end_byte;
            break;
        }
        width = next_width;
        byte = scalar.next;
    }
    if (end_byte == String::npos)
        throw rangeError();
    if (begin_byte == String::npos) begin_byte = end_byte;
    return String(text.substr(begin_byte, end_byte - begin_byte));
}

String astripl(String value) {
    std::size_t first = 0;
    while (first < value.size() && isAsciiSpace(value[first])) ++first;
    value.erase(0, first);
    return value;
}

String astripr(String value) {
    while (!value.empty() && isAsciiSpace(value.back())) value.pop_back();
    return value;
}

String astrip(String value) { return astripr(astripl(std::move(value))); }

bool cglobMatch(StringView value, StringView pattern) {
    String escaped;
    for (const auto c : pattern) {
        if (c == '\\') escaped += "\\\\";
        else escaped.push_back(c);
    }
    return Wildcard(escaped, pathCaseInsensitive()).match(value);
}

bool cfnmatch(StringView first, StringView second) {
    // DのfilenameCmpはbyDcharの置換付き復号とスカラー単位の小文字化を使う。
    // 文字列全体を厳密に検証して、比較対象外の不正UTF-8まで例外にしない。
    auto left = enumutils::toUtf32Replacing(first);
    auto right = enumutils::toUtf32Replacing(second);
    const bool insensitive = pathCaseInsensitive();
    for (auto* text : {&left, &right}) {
        for (auto& scalar : *text) {
            if (insensitive) scalar = enumutils::lowerScalar(scalar);
#if defined(_WIN32)
            if (scalar == U'/') scalar = U'\\';
#endif
        }
    }
    return left == right;
}

bool isVariableName(StringView name) noexcept {
    if (name.empty()) return false;
    const auto first = static_cast<unsigned char>(name.front());
    if (!(std::isalpha(first) || name.front() == '_')) return false;
    for (const auto c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (!(std::isalnum(byte) || c == '_')) return false;
    }
    static constexpr StringView keywords[] = {
        "alignas", "alignof", "and", "and_eq", "asm", "atomic_cancel",
        "atomic_commit", "atomic_noexcept", "auto", "bitand", "bitor",
        "bool", "break", "case", "catch", "char", "char8_t", "char16_t",
        "char32_t", "class", "compl", "concept", "const", "consteval",
        "constexpr", "constinit", "const_cast", "continue", "co_await",
        "co_return", "co_yield", "decltype", "default", "delete", "do",
        "double", "dynamic_cast", "else", "enum", "explicit", "export",
        "extern", "false", "float", "for", "friend", "goto", "if",
        "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
        "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private",
        "protected", "public", "reflexpr", "register", "reinterpret_cast",
        "requires", "return", "short", "signed", "sizeof", "static",
        "static_assert", "static_cast", "struct", "switch", "template",
        "this", "thread_local", "throw", "true", "try", "typedef", "typeid",
        "typename", "union", "unsigned", "using", "virtual", "void",
        "volatile", "wchar_t", "while", "xor", "xor_eq", "version"};
    return std::find(std::begin(keywords), std::end(keywords), name) == std::end(keywords);
}

String variableName(StringView name) {
    return isVariableName(name) ? String(name) : String(name) + '_';
}

Path exeName(const Path& args0) {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(32768U);
    const auto size = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size != 0U && size < buffer.size()) return Path(std::wstring(buffer.data(), size));
#elif defined(__linux__)
    std::vector<char> buffer(PATH_MAX);
    const auto size = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1U);
    if (size > 0) return Path(String(buffer.data(), static_cast<std::size_t>(size)));
#endif
    return args0;
}

std::size_t workingMemory() noexcept {
#if defined(_WIN32)
    using GetProcessMemoryInfoFn = BOOL(WINAPI*)(HANDLE, void*, DWORD);
    struct Counters {
        DWORD cb;
        DWORD page_fault_count;
        SIZE_T peak_working_set_size;
        SIZE_T working_set_size;
        SIZE_T quota_peak_paged_pool_usage;
        SIZE_T quota_paged_pool_usage;
        SIZE_T quota_peak_non_paged_pool_usage;
        SIZE_T quota_non_paged_pool_usage;
        SIZE_T pagefile_usage;
        SIZE_T peak_pagefile_usage;
    } counters{};
    auto library = ::LoadLibraryW(L"psapi.dll");
    // DはPSAPIとそのシンボルの存在を前提とする。C++では失敗を検査し
    // 、Windows APIがなければnullポインターを呼ばず0を返す。
    if (!library) return 0;
    const auto function = reinterpret_cast<GetProcessMemoryInfoFn>(
        ::GetProcAddress(library, "GetProcessMemoryInfo"));
    counters.cb = sizeof(counters);
    const auto result = function != nullptr &&
                        function(::GetCurrentProcess(), &counters, sizeof(counters));
    ::FreeLibrary(library);
    return result ? static_cast<std::size_t>(counters.working_set_size) : 0U;
#elif defined(__linux__)
    std::ifstream input("/proc/self/statm");
    std::uint64_t pages = 0;
    std::uint64_t resident = 0;
    if (!(input >> pages >> resident)) return 0;
    return static_cast<std::size_t>(resident * static_cast<std::uint64_t>(::sysconf(_SC_PAGESIZE)));
#else
    return 0;
#endif
}

}  // namespace cwxeditor::cwx::utils
