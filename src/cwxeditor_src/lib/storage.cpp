#include "storage.hpp"

#include "services.hpp"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>

namespace cwxeditor::lib::editor_storage {
namespace {

[[nodiscard]] bool valid_utf8(std::string_view value) {
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto first = static_cast<unsigned char>(value[offset]);
        std::uint32_t scalar = 0;
        std::size_t width = 0;
        if (first <= 0x7FU) {
            scalar = first;
            width = 1;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            scalar = first & 0x1FU;
            width = 2;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            scalar = first & 0x0FU;
            width = 3;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            scalar = first & 0x07U;
            width = 4;
        } else {
            return false;
        }
        if (offset + width > value.size()) return false;
        for (std::size_t index = 1; index < width; ++index) {
            const auto continuation = static_cast<unsigned char>(value[offset + index]);
            if ((continuation & 0xC0U) != 0x80U) return false;
            scalar = (scalar << 6U) | (continuation & 0x3FU);
        }
        if ((width == 3 && scalar < 0x800U) ||
            (width == 4 && scalar < 0x10000U) || scalar > 0x10FFFFU ||
            (scalar >= 0xD800U && scalar <= 0xDFFFU)) return false;
        offset += width;
    }
    return true;
}

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       if (character >= 'A' && character <= 'Z')
                           return static_cast<char>(character - 'A' + 'a');
                       return static_cast<char>(character);
                   });
    return value;
}

[[nodiscard]] bool portable_device_name(std::string_view component) {
    const auto lower = lower_ascii(std::string(component));
    const auto dot = lower.find('.');
    const auto stem = lower.substr(0, dot);
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return true;
    return stem.size() == 4 &&
           (stem.starts_with("com") || stem.starts_with("lpt")) &&
           stem.back() >= '1' && stem.back() <= '9';
}

[[nodiscard]] bool portable_component(std::string_view component) {
    if (component.empty() || component == "." || component == "..") return true;
    if (component.back() == '.' || component.back() == ' ') return false;
    if (portable_device_name(component)) return false;
    for (const auto character : component) {
        const auto value = static_cast<unsigned char>(character);
        if (value < 0x20U || character == '\0' ||
            std::string_view(":*?\"<>|").find(character) != std::string_view::npos)
            return false;
    }
    return true;
}

}  // namespace

std::optional<std::string> normalize_storage_path(std::string_view path,
                                                   std::string* error) {
    auto fail = [&](const char* message) -> std::optional<std::string> {
        if (error != nullptr) *error = message;
        return std::nullopt;
    };
    if (error != nullptr) error->clear();
    if (!valid_utf8(path)) return fail("Storage path is not valid UTF-8");
    if (path.empty() || path == ".") return std::string{};
    if (path.front() == '/' || path.front() == '\\')
        return fail("Storage path must be relative");
    if (path.find('\0') != std::string_view::npos)
        return fail("Storage path contains NUL");

    std::string result;
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const auto end = path.find('/', begin);
        const auto length = end == std::string_view::npos
            ? path.size() - begin
            : end - begin;
        const auto component = path.substr(begin, length);
        if (!component.empty() && component != ".") {
            if (component == "..")
                return fail("Storage path must not contain '..'");
            if (component.find('\\') != std::string_view::npos)
                return fail("Storage path contains an invalid separator");
            if (!portable_component(component))
                return fail("Storage path contains an invalid filename component");
            if (!result.empty()) result.push_back('/');
            result.append(component);
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return result;
}

bool write_native_file_atomic(const std::filesystem::path& target,
                              std::span<const std::byte> data,
                              std::string& error, bool create_parent) {
    error.clear();
    try {
        if (target.empty() || target.filename().empty() || target.filename() == "." ||
            target.filename() == ".." || target.native().find(
                std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos) {
            error = "atomic write requires a native file destination";
            return false;
        }
        const auto absolute = std::filesystem::absolute(target).lexically_normal();
        // NativeStorage は書き込み前にルートを検証・正規化する
        // 。 Windowsでは末端ディレクトリが未作成だと、weakly_canonical()
        // が access_denied を返す場合がある。 Storage
        // 構築前にこの境界アダプターの create_parent
        // の約束を満たし、 作成済みルートを NativeStorage
        // に検証させ、排他的な一時ファイルの置換を行わせる。
        if (create_parent && !absolute.parent_path().empty()) {
            std::error_code parent_error;
            std::filesystem::create_directories(absolute.parent_path(), parent_error);
            if (parent_error) {
                error = "atomic write parent: " + parent_error.message();
                return false;
            }
        }
        NativeStorage storage(absolute.parent_path(), false);
        if (!storage.write_atomic(absolute.filename(), data, false)) {
            error = storage.last_error();
            if (error.empty()) error = "atomic file write failed";
            return false;
        }
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
    } catch (...) {
        error = "atomic file write failed with an unknown exception";
    }
    return false;
}

std::unique_ptr<Storage> make_native_storage(
    std::string root_utf8,
    std::shared_ptr<::cwxeditor::lib::FileSystemBackend> filesystem,
    bool create_root) {
    // FileSystemBackend は旧エディタ専用実装の
    // もの。 ABI・ソース互換の引数として残すが、永続書き込み
    // はプラットフォームの NativeStorage を使う。
    (void)filesystem;
    return std::make_unique<NativeStorage>(std::move(root_utf8), create_root);
}

std::unique_ptr<Storage> make_native_storage(
    std::string root_utf8,
    const ::cwxeditor::lib::Services& services,
    bool create_root) {
    return make_native_storage(std::move(root_utf8), services.filesystem, create_root);
}

}  // namespace cwxeditor::lib::editor_storage
