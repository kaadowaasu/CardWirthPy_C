#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cwxeditor::lib {

struct ClipboardData {
    // Editor共通の文字列はUTF-8・LFに統一する。
    // OSの形式との相互変換は 実際のクリップボード側で行い
    // 、DWTや共通の状態管理にはOS別の分岐を持たせない。
    std::string text;
    std::vector<std::filesystem::path> files;
    std::unordered_map<std::string, std::vector<std::byte>> formats;
};

[[nodiscard]] inline std::string normalize_clipboard_text(
    std::string text) {
    std::size_t offset = 0U;
    while ((offset = text.find("\r\n", offset)) != std::string::npos) {
        text.replace(offset, 2U, "\n");
        ++offset;
    }
    return text;
}

[[nodiscard]] inline std::string clipboard_text_for_native(
    const std::string_view text, const std::string_view line_ending) {
    if (line_ending.empty() || line_ending == "\n") return std::string(text);
    std::string result;
    result.reserve(text.size());
    for (const char value : text) {
        if (value == '\n') result.append(line_ending);
        else result.push_back(value);
    }
    return result;
}

class ClipboardBackend {
public:
    virtual ~ClipboardBackend() = default;
    virtual void clear() = 0;
    virtual void write(const ClipboardData& data) = 0;
    [[nodiscard]] virtual ClipboardData read() const = 0;
    [[nodiscard]] virtual bool contains(std::string_view mime_type) const = 0;
    // D/SWTは一度に一つの転送形式を要求する。 対応可否の確認だけ
    // で、無関係な画像や遅延提供のクリップボード形式を実体化しない。
    [[nodiscard]] virtual ClipboardData read_formats(std::span<const std::string_view> requested) const {
        const auto source = read();
        ClipboardData result;
        for (const auto mime : requested) {
            if (mime == "text/plain" || mime == "text/plain;charset=utf-8") result.text = source.text;
            else if (mime == "application/x-cwxeditor-file-list") result.files = source.files;
            if (const auto found = source.formats.find(std::string(mime)); found != source.formats.end())
                result.formats.emplace(found->first, found->second);
        }
        return result;
    }
    [[nodiscard]] std::string read_text() const {
        constexpr std::array<std::string_view,2> requested{"text/plain;charset=utf-8", "text/plain"};
        return read_formats(requested).text;
    }
};

}  // namespace cwxeditor::lib
