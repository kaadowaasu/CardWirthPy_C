#include "text_document.hpp"
#include "filesystem.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace cardwirth::explorer {
namespace {

constexpr std::uintmax_t kMaximumEditableBytes = 2U * 1024U * 1024U;

std::string path_text(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

std::string lowercase_extension(const std::filesystem::path& path) {
    auto result = path_text(path.extension());
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return result;
}

using ::cw::lib::Bytes;
using ::cw::lib::encoding::DecodeOptions;
using ::cw::lib::encoding::EncodeOptions;
using ::cw::lib::encoding::EncodingError;
using ::cw::lib::encoding::ErrorPolicy;
using ::cw::lib::encoding::TextEncoding;

bool starts_with(const Bytes& bytes, std::initializer_list<unsigned char> prefix) {
    if (bytes.size() < prefix.size()) return false;
    return std::equal(prefix.begin(), prefix.end(), bytes.begin());
}

struct DecodedDocument {
    std::string text;
    TextEncoding encoding{TextEncoding::Utf8};
    bool bom{};
};

DecodedDocument decode_document(const Bytes& bytes) {
    const auto decode_as = [&](TextEncoding encoding, bool bom) {
        return DecodedDocument{::cw::lib::encoding::decode(
            bytes, encoding, DecodeOptions{ErrorPolicy::Strict, true}), encoding, bom};
    };
    if (starts_with(bytes, {0xef, 0xbb, 0xbf})) return decode_as(TextEncoding::Utf8, true);
    if (starts_with(bytes, {0xff, 0xfe, 0x00, 0x00}))
        return decode_as(TextEncoding::Utf32LE, true);
    if (starts_with(bytes, {0x00, 0x00, 0xfe, 0xff}))
        return decode_as(TextEncoding::Utf32BE, true);
    if (starts_with(bytes, {0xff, 0xfe})) return decode_as(TextEncoding::Utf16LE, true);
    if (starts_with(bytes, {0xfe, 0xff})) return decode_as(TextEncoding::Utf16BE, true);
    for (const auto encoding : {TextEncoding::Utf8, TextEncoding::Cp932,
                                TextEncoding::EucJp}) {
        try { return decode_as(encoding, false); }
        catch (const EncodingError&) {}
    }
    throw std::runtime_error("対応するテキスト文字コードとして読み取れません");
}

std::string detect_newline(const std::string& text) {
    std::size_t crlf = 0, lf = 0, cr = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') { ++crlf; ++i; }
            else ++cr;
        } else if (text[i] == '\n') ++lf;
    }
    if (crlf >= lf && crlf >= cr && crlf != 0) return "\r\n";
    if (cr > lf && cr != 0) return "\r";
    return "\n";
}

std::string normalize_newlines(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            result.push_back('\n');
        } else result.push_back(text[i]);
    }
    return result;
}

std::string restore_newlines(const std::string& text, const std::string& newline) {
    if (newline == "\n") return text;
    std::string result;
    result.reserve(text.size() + std::count(text.begin(), text.end(), '\n'));
    for (const char value : text) {
        if (value == '\n') result += newline;
        else result.push_back(value);
    }
    return result;
}

std::filesystem::path temporary_sibling(const std::filesystem::path& path,
                                         const char* marker) {
    const auto suffix = std::to_string(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return path.parent_path() /
        std::filesystem::path("." + path_text(path.filename()) + marker + suffix);
}

}  // namespace

bool TextDocumentService::has_editable_extension(const std::filesystem::path& path) {
    static const std::unordered_set<std::string> extensions{
        ".txt", ".xml", ".json", ".ini", ".cfg", ".conf", ".csv", ".tsv",
        ".log", ".md", ".yaml", ".yml", ".c", ".cc", ".cpp", ".h", ".hpp",
        ".py", ".js", ".ts", ".css", ".html", ".htm", ".bat", ".cmd", ".ps1"};
    return extensions.contains(lowercase_extension(path));
}

TextDocument TextDocumentService::load(const std::filesystem::path& absolute_path,
                                       const std::filesystem::path& relative_path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(absolute_path, error) || error)
        throw std::invalid_argument("編集対象が通常ファイルではありません");
    const auto size = std::filesystem::file_size(absolute_path, error);
    if (error) throw std::runtime_error("テキストのサイズを取得できません");
    if (size > kMaximumEditableBytes)
        throw std::runtime_error("2 MiBを超えるテキストは編集できません");

    std::ifstream input(absolute_path, std::ios::binary);
    if (!input) throw std::runtime_error("テキストを開けません: " + path_text(relative_path));
    const std::vector<unsigned char> raw((std::istreambuf_iterator<char>(input)),
                                         std::istreambuf_iterator<char>());
    if (!input.eof() && input.fail()) throw std::runtime_error("テキストを読み取れません");
    const Bytes bytes(raw.begin(), raw.end());
    auto decoded = decode_document(bytes);
    if (decoded.text.find('\0') != std::string::npos)
        throw std::runtime_error("バイナリデータを含むファイルは編集できません");
    const auto newline = detect_newline(decoded.text);
    return {relative_path, path_text(relative_path.filename()),
            normalize_newlines(decoded.text), decoded.encoding, decoded.bom, newline};
}

void TextDocumentService::save(const std::filesystem::path& absolute_path,
                               const TextDocument& document) {
    const auto normalized = restore_newlines(document.text, document.newline);
    Bytes encoded;
    try {
        encoded = ::cw::lib::encoding::encode(normalized, document.encoding,
            EncodeOptions{ErrorPolicy::Strict, document.byte_order_mark});
    } catch (const EncodingError& error) {
        throw std::invalid_argument(std::string("元の文字コードで保存できません: ") + error.what());
    }
    const auto temporary = temporary_sibling(absolute_path, ".cw-edit-");
#if defined(_WIN32)
    const auto backup = temporary_sibling(absolute_path, ".cw-backup-");
#endif
    std::error_code error;
    try {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("一時ファイルを作成できません");
            output.write(reinterpret_cast<const char*>(encoded.data()),
                         static_cast<std::streamsize>(encoded.size()));
            output.flush();
            if (!output) throw std::runtime_error("テキストを保存できません");
        }
#if defined(_WIN32)
        if (!ReplaceFileW(absolute_path.c_str(), temporary.c_str(), backup.c_str(),
                          REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            const auto code = GetLastError();
            throw std::runtime_error("編集内容を安全に置換できません (Windows error " +
                                     std::to_string(code) + ")");
        }
#else
        if (!replace_file_atomically(temporary, absolute_path, error))
            throw std::runtime_error("編集内容を確定できません: " + error.message());
#endif
#if defined(_WIN32)
        std::filesystem::remove(backup, error);
#endif
    } catch (...) {
        std::filesystem::remove(temporary, error);
        throw;
    }
}

}  // namespace cardwirth::explorer
