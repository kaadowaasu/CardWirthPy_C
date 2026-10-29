#pragma once

#include "cw/lib/compat.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace cw::lib::encoding {

// Python codecs の errors 引数に対応する。変換の途中で暗黙のlocaleを
// 参照せず、変換境界で明示的に方針を指定する。
enum class ErrorPolicy { Strict, Replace, Ignore };

class EncodingError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class TextEncoding {
    Utf8,
    Utf8Bom,
    Utf16,
    Utf16LE,
    Utf16BE,
    Utf32,
    Utf32LE,
    Utf32BE,
    Cp932,
    EucJp,
    Cp437,
    Latin1,
    ShiftJis,
};

enum class ByteOrder { Little, Big };

enum class CharacterClass {
    Unknown,
    Symbol,
    Number,
    Latin,
    Hiragana,
    Katakana,
    HalfWidthKatakana,
    Greek,
    Cyrillic,
    CjkIdeograph,
};

struct DecodeOptions {
    ErrorPolicy errors{ErrorPolicy::Strict};
    bool strip_bom{true};
};

struct EncodeOptions {
    ErrorPolicy errors{ErrorPolicy::Strict};
    bool emit_bom{false};
};

[[nodiscard]] String decode(const Bytes& bytes, TextEncoding encoding,
                            DecodeOptions options = {});
[[nodiscard]] Bytes encode(const String& text, TextEncoding encoding,
                           EncodeOptions options = {});
[[nodiscard]] String decode_utf8(const Bytes& bytes,
                                 ErrorPolicy errors = ErrorPolicy::Strict,
                                 bool strip_bom = true);
// OS 既定のテキストファイル符号化。Py の open(..., encoding=None)
// に対応し、自動判定や BOM 除去は行わない。すべての呼び出し側で同じ境界を共有する。
[[nodiscard]] String decode_system_text(const Bytes& bytes);
[[nodiscard]] Bytes encode_utf8(const String& text,
                                ErrorPolicy errors = ErrorPolicy::Strict);

[[nodiscard]] String cp932_to_utf8(const Bytes& bytes,
                                   ErrorPolicy errors = ErrorPolicy::Strict);
[[nodiscard]] Bytes utf8_to_cp932(const String& text,
                                  ErrorPolicy errors = ErrorPolicy::Strict);
[[nodiscard]] String cp437_to_utf8(const Bytes& bytes,
                                   ErrorPolicy errors = ErrorPolicy::Strict);
[[nodiscard]] Bytes utf8_to_cp437(const String& text,
                                  ErrorPolicy errors = ErrorPolicy::Strict);
[[nodiscard]] String latin1_to_utf8(const Bytes& bytes);
[[nodiscard]] Bytes utf8_to_latin1(const String& text,
                                   ErrorPolicy errors = ErrorPolicy::Strict);
[[nodiscard]] String utf16_to_utf8(const Bytes& bytes, ByteOrder order,
                                   ErrorPolicy errors = ErrorPolicy::Strict,
                                   bool strip_bom = true);
[[nodiscard]] Bytes utf8_to_utf16(const String& text, ByteOrder order,
                                  ErrorPolicy errors = ErrorPolicy::Strict,
                                   bool emit_bom = false);
[[nodiscard]] String utf32_to_utf8(const Bytes& bytes, ByteOrder order,
                                   ErrorPolicy errors = ErrorPolicy::Strict,
                                   bool strip_bom = true);
[[nodiscard]] Bytes utf8_to_utf32(const String& text, ByteOrder order,
                                  ErrorPolicy errors = ErrorPolicy::Strict,
                                  bool emit_bom = false);
[[nodiscard]] String euc_jp_to_utf8(const Bytes& bytes,
                                    ErrorPolicy errors = ErrorPolicy::Strict);
[[nodiscard]] Bytes utf8_to_euc_jp(const String& text,
                                   ErrorPolicy errors = ErrorPolicy::Strict);

// CardWirthPy の util.decode_text/decode_zipname
// に対応するバイト変換。CP437 はどのバイト列も復号できるため、意図的に最後に試す。
[[nodiscard]] String decode_legacy_text(const Bytes& bytes);
[[nodiscard]] String decode_legacy_archive_name(const Bytes& bytes);
// ロケールに依存しない Unicode 15.0 の既定の小文字化。Python
// の str.lower に対応する。不正 UTF-8 はほかの比較ヘルパーと
// 同じ規則で置換する。casefold や正規化ではなく、元の表示文字列は保つ。
[[nodiscard]] String lower(const String& text);
// 元の Pipfile の Python3.8/UCD12.1 の小文字化。上の Unicode15
// の拡張を保ちつつ、同じ共通アルゴリズムに固定したデータを使う。
[[nodiscard]] String python_lower(const String& text);
// 元の Python3.8/UCD12.1 の完全な大文字化。複数コードポイントへの対応付けも含む。
[[nodiscard]] String python_upper(const String& text);
[[nodiscard]] String casefold(const String& text);
[[nodiscard]] String collation_key(const String& text);
[[nodiscard]] CharacterClass character_class(const String& character) noexcept;

// lib::String は常にUTF-8で、パス文字列は汎用の '/' 区切りを使う。
// OS固有の区切りは std::filesystem::path を実際のAPIへ渡す境界でのみ扱う。
[[nodiscard]] std::filesystem::path path_from_utf8(const String& text,
                                                   ErrorPolicy errors = ErrorPolicy::Strict);
[[nodiscard]] String path_to_utf8(const std::filesystem::path& path,
                                  ErrorPolicy errors = ErrorPolicy::Strict);

// util.py の encodewrap/decodewrap/encodetextlist/decodetextlist 相当。
[[nodiscard]] String encodewrap(const String& text);
[[nodiscard]] String decodewrap(const String& text, const String& newline = "\n");
[[nodiscard]] String encodetextlist(const std::vector<String>& lines);
[[nodiscard]] std::vector<String> decodetextlist(const String& text);

// keepends なしの Python の str.splitlines() に対応する。String
// は UTF-8 なので、Unicode の NEL・行区切り・段落区切りをコードポイントとして認識する。
[[nodiscard]] std::vector<String> splitlines(const String& text);

// unicodedata.east_asian_width()/combining() と grapheme の、描画依存を
// 持たない近似。CardWirthの固定幅テキスト用に全角・曖昧幅を2とする。
[[nodiscard]] bool is_combining(std::uint32_t codepoint) noexcept;
[[nodiscard]] int codepoint_width(std::uint32_t codepoint) noexcept;
[[nodiscard]] bool is_half_width(const String& character) noexcept;
[[nodiscard]] std::vector<String> graphemes(const String& text);
[[nodiscard]] int text_width(const String& text);
[[nodiscard]] inline int get_strlen(const String& text) { return text_width(text); }

[[nodiscard]] std::tuple<String, String> slice_str(const String& text, int width);
[[nodiscard]] String rjustify(const String& text, int width, const String& fill);
[[nodiscard]] String ljustify(const String& text, int width, const String& fill);

[[nodiscard]] String default_wrap_chars();
[[nodiscard]] String default_open_chars();
[[nodiscard]] String default_close_chars();

// mode 1〜6 は util.py の固定幅モード。special_positions はPythonの
// spcharinfo相当で、入力文字列のUnicode文字インデックスを受け取り更新する。
[[nodiscard]] String txtwrap(const String& text, int mode, int width = 30,
                             const String& wrap_chars = {},
                             bool encoded_text = true,
                             std::set<std::size_t>* special_positions = nullptr);
[[nodiscard]] String wordwrap(const String& text, int width,
                              const String& open_chars = {},
                              const String& close_chars = {},
                              std::set<std::size_t>* special_positions = nullptr);

// struct.Struct の整数部分で使われる固定幅 endian 操作。
// Pythonの'@', '=', 'f', 'd', pointer形式はABI依存のためこの固定幅整数APIの
// 対象外とし、呼出し側が表現を指定する別APIで扱う。
// 範囲外はstruct.error相当としてEncodingErrorにする。
[[nodiscard]] std::uint8_t read_u8(const Bytes& data, std::size_t offset);
[[nodiscard]] std::int8_t read_i8(const Bytes& data, std::size_t offset);
[[nodiscard]] std::uint16_t read_u16(const Bytes& data, std::size_t offset,
                                     ByteOrder order = ByteOrder::Little);
[[nodiscard]] std::int16_t read_i16(const Bytes& data, std::size_t offset,
                                    ByteOrder order = ByteOrder::Little);
[[nodiscard]] std::uint32_t read_u32(const Bytes& data, std::size_t offset,
                                     ByteOrder order = ByteOrder::Little);
[[nodiscard]] std::int32_t read_i32(const Bytes& data, std::size_t offset,
                                    ByteOrder order = ByteOrder::Little);
[[nodiscard]] std::uint64_t read_u64(const Bytes& data, std::size_t offset,
                                     ByteOrder order = ByteOrder::Little);
[[nodiscard]] std::int64_t read_i64(const Bytes& data, std::size_t offset,
                                    ByteOrder order = ByteOrder::Little);

void append_u8(Bytes& data, std::uint8_t value);
void append_i8(Bytes& data, std::int8_t value);
void append_u16(Bytes& data, std::uint16_t value,
                ByteOrder order = ByteOrder::Little);
void append_i16(Bytes& data, std::int16_t value,
                ByteOrder order = ByteOrder::Little);
void append_u32(Bytes& data, std::uint32_t value,
                ByteOrder order = ByteOrder::Little);
void append_i32(Bytes& data, std::int32_t value,
                ByteOrder order = ByteOrder::Little);
void append_u64(Bytes& data, std::uint64_t value,
                ByteOrder order = ByteOrder::Little);
void append_i64(Bytes& data, std::int64_t value,
                ByteOrder order = ByteOrder::Little);

void store_u16(Bytes& data, std::size_t offset, std::uint16_t value,
               ByteOrder order = ByteOrder::Little);
void store_i16(Bytes& data, std::size_t offset, std::int16_t value,
               ByteOrder order = ByteOrder::Little);
void store_u32(Bytes& data, std::size_t offset, std::uint32_t value,
               ByteOrder order = ByteOrder::Little);
void store_i32(Bytes& data, std::size_t offset, std::int32_t value,
               ByteOrder order = ByteOrder::Little);
void store_u64(Bytes& data, std::size_t offset, std::uint64_t value,
               ByteOrder order = ByteOrder::Little);
void store_i64(Bytes& data, std::size_t offset, std::int64_t value,
               ByteOrder order = ByteOrder::Little);

}  // namespace cw::lib::encoding
