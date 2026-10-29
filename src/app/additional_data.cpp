#include "additional_data.hpp"

#include "cw/lib/archive.hpp"
#include "platform/storage/storage.hpp"
#if defined(__APPLE__)
#include "platform_apple.hpp"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <jni.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif

namespace cardwirth::app {
namespace {

constexpr std::uint64_t kMaxFile = 256ULL * 1024 * 1024;
constexpr std::uint64_t kMaxTotal = 512ULL * 1024 * 1024;
constexpr std::size_t kMaxEntries = 20000;
constexpr std::string_view kBase = "https://webcardwirth.net/py_c/DownloadFiles/";

struct Package {
    std::string kind, file, target;
    std::vector<std::string> roots;
};

void append_utf8(std::string& out, unsigned cp) {
    if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 63)));
    } else if (cp <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
        out.push_back(static_cast<char>(0x80 | (cp & 63)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
        out.push_back(static_cast<char>(0x80 | (cp & 63)));
    }
}

class Json {
public:
    explicit Json(std::string_view input) : input_(input) {}
    void space() { while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) ++pos_; }
    bool take(char c) { space(); if (pos_ < input_.size() && input_[pos_] == c) { ++pos_; return true; } return false; }
    void need(char c) { if (!take(c)) throw std::runtime_error("配布一覧の形式が不正です。"); }
    unsigned hex4() {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos_ >= input_.size()) throw std::runtime_error("配布一覧の文字列が不正です。");
            char c = input_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= c - '0';
            else if (c >= 'a' && c <= 'f') value |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') value |= c - 'A' + 10;
            else throw std::runtime_error("配布一覧の文字列が不正です。");
        }
        return value;
    }
    std::string string() {
        need('"');
        std::string result;
        while (pos_ < input_.size()) {
            unsigned char c = static_cast<unsigned char>(input_[pos_++]);
            if (c == '"') return result;
            if (c < 0x20) throw std::runtime_error("配布一覧の文字列が不正です。");
            if (c != '\\') { result.push_back(static_cast<char>(c)); continue; }
            if (pos_ >= input_.size()) break;
            char escaped = input_[pos_++];
            switch (escaped) {
            case '"': case '\\': case '/': result.push_back(escaped); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                unsigned cp = hex4();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (pos_ + 2 > input_.size() || input_.substr(pos_, 2) != "\\u")
                        throw std::runtime_error("配布一覧のUnicodeが不正です。");
                    pos_ += 2;
                    unsigned low = hex4();
                    if (low < 0xdc00 || low > 0xdfff) throw std::runtime_error("配布一覧のUnicodeが不正です。");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff) throw std::runtime_error("配布一覧のUnicodeが不正です。");
                append_utf8(result, cp);
                break;
            }
            default: throw std::runtime_error("配布一覧の文字列が不正です。");
            }
        }
        throw std::runtime_error("配布一覧の文字列が閉じられていません。");
    }
    int integer() {
        space();
        int value = 0;
        bool found = false;
        while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') {
            found = true;
            if (value > 100000) throw std::runtime_error("配布一覧の数値が大きすぎます。");
            value = value * 10 + input_[pos_++] - '0';
        }
        if (!found) throw std::runtime_error("配布一覧の数値が不正です。");
        return value;
    }
    void skip(int depth = 0) {
        if (depth > 20) throw std::runtime_error("配布一覧が深すぎます。");
        space();
        if (take('{')) {
            if (!take('}')) do { string(); need(':'); skip(depth + 1); } while (take(','));
            need('}');
        } else if (take('[')) {
            if (!take(']')) do { skip(depth + 1); } while (take(','));
            need(']');
        } else if (pos_ < input_.size() && input_[pos_] == '"') string();
        else {
            std::size_t start = pos_;
            while (pos_ < input_.size() && input_[pos_] != ',' && input_[pos_] != '}' && input_[pos_] != ']' &&
                   !std::isspace(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            if (start == pos_) throw std::runtime_error("配布一覧の値が不正です。");
        }
    }
    std::vector<Package> packages() {
        need('[');
        std::vector<Package> result;
        if (take(']')) return result;
        do {
            if (result.size() >= kMaxEntries) throw std::runtime_error("配布項目が多すぎます。");
            Package item;
            need('{');
            if (!take('}')) {
                do {
                    auto key = string(); need(':');
                    if (key == "kind") item.kind = string();
                    else if (key == "file") item.file = string();
                    else if (key == "target") item.target = string();
                    else if (key == "roots") {
                        need('[');
                        if (!take(']')) {
                            do { item.roots.push_back(string()); if (item.roots.size() > 20) throw std::runtime_error("配布ルートが多すぎます。"); } while (take(','));
                            need(']');
                        }
                    } else skip();
                } while (take(','));
                need('}');
            }
            result.push_back(std::move(item));
        } while (take(','));
        need(']');
        return result;
    }
    std::vector<Package> manifest(const std::string& group) {
        need('{');
        int format = 0;
        std::vector<Package> selected;
        if (!take('}')) {
            do {
                auto key = string(); need(':');
                if (key == "format") format = integer();
                else if (key == group) selected = packages();
                else skip();
            } while (take(','));
            need('}');
        }
        space();
        if (pos_ != input_.size() || format != 1 || selected.empty())
            throw std::runtime_error("配布一覧が未配置、または空です。");
        return selected;
    }
private:
    std::string_view input_;
    std::size_t pos_{};
};

bool clean_path(std::string_view path) {
    if (path.empty() || path.size() > 4096 || path.front() == '/' || path.back() == '/') return false;
    if (path.find_first_of("\\:") != std::string_view::npos || path.find('\0') != std::string_view::npos) return false;
    std::size_t start = 0;
    while (start < path.size()) {
        auto end = path.find('/', start);
        if (end == std::string_view::npos) end = path.size();
        auto part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = end + 1;
    }
    return true;
}

std::string url_for(std::string_view relative) {
    if (!clean_path(relative)) throw std::runtime_error("不正な配布パスです。");
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string url(kBase);
    for (unsigned char c : relative) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') url.push_back(static_cast<char>(c));
        else { url.push_back('%'); url.push_back(digits[c >> 4]); url.push_back(digits[c & 15]); }
    }
    return url;
}

std::filesystem::path native_path(std::string_view utf8) {
    return std::filesystem::u8path(utf8.begin(), utf8.end());
}

void download(const std::string& url, const std::filesystem::path& destination, std::uint64_t limit) {
#if defined(__ANDROID__)
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) throw std::runtime_error("Androidの通信機能を取得できません。");
    jclass type = env->GetObjectClass(activity);
    jmethodID method = type ? env->GetMethodID(type, "cardwirthDownloadToFile", "(Ljava/lang/String;Ljava/lang/String;J)Z") : nullptr;
    const auto path_utf8 = destination.u8string();
    jstring source = env->NewStringUTF(url.c_str());
    jstring path = env->NewStringUTF(reinterpret_cast<const char*>(path_utf8.c_str()));
    jboolean ok = method && source && path ? env->CallBooleanMethod(activity, method, source, path, static_cast<jlong>(limit)) : JNI_FALSE;
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); ok = JNI_FALSE; }
    if (path) env->DeleteLocalRef(path);
    if (source) env->DeleteLocalRef(source);
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
    if (!ok) throw std::runtime_error("Androidで追加データを取得できませんでした。");
#elif defined(__APPLE__)
    if (!apple::download_https_file(url, destination, limit))
        throw std::runtime_error("追加データを取得できませんでした。");
#elif defined(_WIN32)
    const auto wide = [](const std::string& value) {
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
        if (!length) throw std::runtime_error("URLが不正です。");
        std::wstring result(length, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, result.data(), length);
        result.pop_back();
        return result;
    };
    auto path = wide(url.substr(std::string_view("https://webcardwirth.net").size()));
    HINTERNET session = WinHttpOpen(L"CardWirthC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) throw std::runtime_error("通信を開始できません。");
    HINTERNET connection = WinHttpConnect(session, L"webcardwirth.net", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    bool success = false;
    if (request) {
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
        WinHttpSetTimeouts(request, 10000, 10000, 30000, 30000);
        if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0, size = sizeof(status);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) && status == 200) {
                std::ofstream out(destination, std::ios::binary | std::ios::trunc);
                std::array<char, 65536> buffer{};
                std::uint64_t total = 0;
                success = out.good();
                while (success) {
                    DWORD read = 0;
                    if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) { success = false; break; }
                    if (!read) break;
                    total += read;
                    if (total > limit) { success = false; break; }
                    out.write(buffer.data(), read);
                    success = out.good();
                }
                out.close();
                success = success && total > 0 && out.good();
            }
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (!success) throw std::runtime_error("追加データを取得できませんでした。");
#else
    (void)url; (void)destination; (void)limit;
    throw std::runtime_error("この環境では追加データを取得できません。");
#endif
}

std::vector<std::byte> read_limited(const std::filesystem::path& path, std::uint64_t limit) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec || size > limit) throw std::runtime_error("ダウンロードサイズが上限を超えています。");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    std::ifstream in(path, std::ios::binary);
    if (!in || !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("ダウンロードしたファイルを読めません。");
    return bytes;
}

} // namespace

int install_additional_data(const std::filesystem::path& user_root,
                            const std::string& group,
                            const std::function<void(int, int)>& progress) {
    // ALLのホーム画面では、ブラウザー用インストーラーの基本パッケージ群を使う。
    // Data/Scenarioのファイルも共通のマニフェストと展開処理で扱う。
    if (group != "basic" && group != "scenarios" && group != "faces") throw std::runtime_error("不正なデータ分類です。");
    static std::atomic<unsigned> sequence{0};
    auto staging = std::filesystem::temp_directory_path() /
        ("cardwirth-additional-" + std::to_string(++sequence) + "-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(staging);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{staging};
    auto manifest_path = staging / "manifest.json";
    download(url_for("manifest.json"), manifest_path, 4 * 1024 * 1024);
    auto manifest_bytes = read_limited(manifest_path, 4 * 1024 * 1024);
    std::string manifest(reinterpret_cast<const char*>(manifest_bytes.data()), manifest_bytes.size());
    auto packages = Json(manifest).manifest(group);
    std::vector<std::pair<std::string, std::vector<std::byte>>> files;
    std::uint64_t expanded = 0;
    for (std::size_t i = 0; i < packages.size(); ++i) {
        const auto& package = packages[i];
        if (!clean_path(package.file) || !clean_path(package.target) ||
            !(package.target == "Data" || package.target.rfind("Data/", 0) == 0 ||
              package.target == "Scenario" || package.target.rfind("Scenario/", 0) == 0 ||
              package.target == "Yado" || package.target.rfind("Yado/", 0) == 0 ||
              package.target == "System" || package.target.rfind("System/", 0) == 0) ||
            (group == "scenarios" && package.target.rfind("Scenario/", 0) != 0) ||
            (group == "faces" && package.target != "Data/Face"))
            throw std::runtime_error("配布一覧の配置先が不正です。");
        progress(static_cast<int>(i + 1), static_cast<int>(packages.size()));
        auto download_path = staging / ("download-" + std::to_string(i));
        download(url_for(package.file), download_path, kMaxFile);
        const auto add = [&](std::string path, std::vector<std::byte> bytes) {
            if (!clean_path(path) || bytes.size() > kMaxFile || files.size() >= kMaxEntries ||
                expanded + bytes.size() > kMaxTotal)
                throw std::runtime_error("展開するデータが上限を超えています。");
            expanded += bytes.size();
            files.emplace_back(std::move(path), std::move(bytes));
        };
        if (package.kind == "file") {
            add(package.target, read_limited(download_path, kMaxFile));
        } else if (package.kind == "zip") {
            auto archive = cw::lib::archive::Reader::open(download_path);
            cw::lib::archive::ExtractOptions options;
            options.max_file_size = kMaxFile;
            options.max_total_size = kMaxTotal - expanded;
            options.max_files = kMaxEntries - files.size();
            archive.validate_extraction(options);
            for (const auto& entry : archive.entries()) {
                if (entry.directory) continue;
                if (entry.symlink || !clean_path(entry.name)) throw std::runtime_error("ZIP内のパスが不正です。");
                std::string name = entry.name;
                for (const auto& root : package.roots) {
                    if (!clean_path(root)) throw std::runtime_error("配布一覧のルートが不正です。");
                    std::string prefix = root + "/";
                    if (name.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), name.begin(),
                        [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); })) {
                        name.erase(0, prefix.size());
                        break;
                    }
                }
                add(package.target + "/" + name, archive.read(entry));
            }
        } else throw std::runtime_error("不正な配布形式です。");
    }
    cardwirth::platform::NativeStorage storage(user_root);
    int count = 0;
    for (const auto& [path, bytes] : files) {
        if (!storage.write_atomic(native_path(path),
                                  std::span<const std::byte>(bytes.data(), bytes.size()), true))
            throw std::runtime_error("追加データを保存できません: " + storage.last_error());
        ++count;
    }
    return count;
}

} // namespace cardwirth::app
