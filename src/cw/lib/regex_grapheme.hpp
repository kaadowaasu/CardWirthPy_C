#pragma once

// Python の util.py の移植で使う、確定的でロケール非
// 依存の Unicode 境界ヘルパー。ICU/PCRE には依存せ
// ず、CardWirth が使う二つの Python パターンと、
// 同じ安全な境界を持つ小さな fullmatch 文法を提供する。
#include "cw/lib/compat.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace cw::lib::regex_grapheme {

class InvalidUtf8 : public std::runtime_error {
public:
    InvalidUtf8(std::size_t byte_offset, const String& message);
    [[nodiscard]] std::size_t byte_offset() const noexcept { return m_byte_offset; }

private:
    std::size_t m_byte_offset{};
};

// UTF-8 を厳密に Unicode スカラー値へ復号する。冗
// 長な符号化、サロゲート、途中で切れた列、U+10FFFF 超は
// InvalidUtf8 を投げ、置換文字を黙って挿入しない。
[[nodiscard]] std::vector<std::uint32_t> decode_utf8(const String& text);

// Py の grapheme 0.6.0 の状態機械と Unicode
// 13.0.0 の属性表の移植。戻り文字列は元の UTF-8 バイ
// トを保つ。graphemes() は util.py の従来の規則
// で、半角濁点・半濁点で終わる二コードポイントのまとまりを分割する。
[[nodiscard]] std::vector<String> grapheme_clusters(const String& text);
[[nodiscard]] std::vector<String> graphemes(const String& text);

// util.py の固定の regex.findall パターン専用の移植で、regex 全体の代替では
// ない。Mark と IGNORECASE は regex 2022.9.13 に従う。対象は (?>[a-z0-9_]\p{M}*)+
// | (?>[ａ-ｚＡ-Ｚ０-９＿]\p{M}*)+ | .\p{M}* を regex.IGNORECASE
// で使うもの。Python と同じくドットは改行に一致しない。
[[nodiscard]] std::vector<String> findall_word_tokens(const String& text);
[[nodiscard]] std::vector<String> findall(const String& text);

// util.regex_fullmatch 用の安全な全文一致処理。
// リテラル、ドット、文字集合・範囲、グループ、選択、^/$、*・+・
// ? に対応する。不正・未対応のパターンは false、不正 UTF-8
// は破損を観測できるよう InvalidUtf8 を投げる。\p{M}/\P{M}
// は findall_word_tokens と同じ Unicode
// のマーク属性を使う。上限はパターン 4096 コードポイ
// ント、グループ 128 段、照合再帰 256 段。超過時は
// C++ スタックを使い尽くさず、Python の RecursionError
// の代替と同じ false を返す。
[[nodiscard]] bool fullmatch(const String& pattern, const String& text, bool ignore_case = false);

}  // namespace cw::lib::regex_grapheme
