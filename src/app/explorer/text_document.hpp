#pragma once

#include "cw/lib/encoding.hpp"

#include <filesystem>
#include <string>

namespace cardwirth::explorer {

struct TextDocument {
    std::filesystem::path relative_path;
    std::string title;
    std::string text;
    // エディター内部ではUTF-8・LFに統一したテキストを扱う。元の
    // バイト表現の情報をここへ保持し、 Explorerで開いただけで
    // 従来のCardWirth文書の保存形式が変わらないようにする。
    ::cw::lib::encoding::TextEncoding encoding{::cw::lib::encoding::TextEncoding::Utf8};
    bool byte_order_mark{};
    std::string newline{"\n"};
};

// UTF-8テキストの読み込みと安全な置き換えはSDLウィンドウから
// 分離し、 別の画面実装でも同じ編集規則を再利用できるようにする。
class TextDocumentService {
public:
    [[nodiscard]] static bool has_editable_extension(const std::filesystem::path& path);
    [[nodiscard]] static TextDocument load(const std::filesystem::path& absolute_path,
                                           const std::filesystem::path& relative_path);
    static void save(const std::filesystem::path& absolute_path, const TextDocument& document);
};

}  // namespace cardwirth::explorer
