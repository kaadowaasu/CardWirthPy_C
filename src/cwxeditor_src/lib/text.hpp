#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::lib {

// DのネイティブEditorコントロールは、WindowsでだけCRLFから
// CRを取り除く。XMLの改行正規化や、受け渡す文字列全体の変換ではない。
[[nodiscard]] std::string normalize_editor_control_newlines(std::string text);

enum class TextEncoding { Utf8, Utf16LE, Utf16BE, ShiftJis, System };
enum class TextNormalization { None, NFC, NFD, NFKC, NFKD };

class TextBackend {
public:
    virtual ~TextBackend() = default;
    [[nodiscard]] virtual std::string decode(std::span<const std::byte> data,
                                             TextEncoding encoding,
                                             std::string& error) const = 0;
    [[nodiscard]] virtual std::vector<std::byte> encode(std::string_view text,
                                                        TextEncoding encoding,
                                                        std::string& error) const = 0;
    [[nodiscard]] virtual std::string normalize(std::string_view text,
                                                TextNormalization form) const = 0;
    [[nodiscard]] virtual std::string case_fold(std::string_view text) const = 0;
    [[nodiscard]] virtual int locale_compare(std::string_view left,
                                             std::string_view right) const = 0;
};

}  // namespace cwxeditor::lib
