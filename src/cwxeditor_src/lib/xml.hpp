#pragma once

// CWXEditor移植版で使う、外部依存のない小さなXML DOM
// 。元のEditorはcwx.xml.XNodeを通じてdxml
// のプルパーサーとライターを使う。このヘッダーは公開契約をdxml
// やGUI・OSライブラリから独立させつつ、create/newElement/newAttr
// 、attr/valueTo、child/childText
// 、ハンドラーによる巡回、XML宣言付きUTF-8
// 直列化など、XNodeに対応する操作を提供する。

#include <cstddef>
#include <cstdint>
#include <array>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <locale.h>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "cwxeditor_src/lib/scalar_conversion.hpp"
#include "cwxeditor_src/lib/shared_dictionary.hpp"
#include "cwxeditor_src/lib/utf8.hpp"
#include "cwxeditor_src/lib/scalar_format.hpp"

namespace cwxeditor::lib {

enum class XmlNodeType {
    Element,
    Text,
    CData,
    Comment,
    ProcessingInstruction,
};

struct XmlAttribute {
    std::string name;
    std::string value;
};

class XmlParseError : public std::runtime_error {
public:
    XmlParseError(std::string message, std::size_t offset, std::size_t line,
                  std::size_t column);

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t line() const noexcept { return line_; }
    [[nodiscard]] std::size_t column() const noexcept { return column_; }

private:
    std::size_t offset_{};
    std::size_t line_{};
    std::size_t column_{};
};

class XmlValueError : public std::invalid_argument {
public:
    explicit XmlValueError(const std::string& message) : std::invalid_argument(message) {}
};

// C++20には列挙名の組み込みリフレクションがない。Editorコア
// はDの列挙のメンバー名情報を持つ箇所で、このトレイトを特殊化できる。
template <typename E, typename Enable = void>
struct XmlEnumTraits {};

struct XmlParseOptions {
    // XNode.parse(xml, allowSingleElement)との
    // ソース互換性のために残す。規格に準拠した単一の自己終了要素からなる文書は、
    // どちらの値でも受け付ける。trueでは単一要素の断片も明示的に受け付ける。
    bool allow_single_element{false};

    // trueの場合、文書内のXMLコメント、CDATA
    // 区間、処理命令をDOMの子として保持する。
    bool preserve_misc{true};

    // XMLの空白だけからなるテキストノードを明示的に捨てる。D互換のXmlNode
    // 表示は、children()ではテキストを除外し、value()では保持する。
    bool ignore_whitespace_only_text{false};

    // DのXNodeパーサーは、XML 1.0の改行正規化を行うdxmlのXML
    // パーサーへ委ねる。このC++の窓口も同じ既定動作を維持し、元のCRバイト
    // が必要な呼び出し側向けに、意図的に正規化を無効化する選択肢を提供する。
    bool normalize_newlines{true};
};

struct XmlWriterOptions {
    // XNode.text()は既定でこの宣言を含む。
    bool declaration{true};

    // 整形モードは元のdxmlライターに従う。要素間を改行で分
    // けるが、indentを設定しない限り字下げは行わない。
    bool pretty{true};
    std::string indent;
    std::string line_ending{"\n"};

    // 元のライターは空の文書ルートを開始・終了タグに展開
    // するが、ルート以外の空要素は<tag/>で書く。
    bool self_close_empty{true};
    bool expand_empty_root{true};
};

namespace detail {

// Dの文字列の結果はGCによって保存領域を保持するが、C++のbasic_string_view
// は変換時の一時領域を保持できない。これらの読み取
// り境界では、どのコード単位幅でも借用として扱う。
template <typename T>
struct is_xml_borrowed_string_view : std::false_type {};

template <typename Character, typename Traits>
struct is_xml_borrowed_string_view<std::basic_string_view<Character, Traits>>
    : std::true_type {};

template <typename T>
inline constexpr bool is_xml_borrowed_string_view_v =
    is_xml_borrowed_string_view<std::remove_cvref_t<T>>::value;

inline std::string xml_encode_unicode_scalar(const std::uint32_t value) {
    try {
        return utf8::encode_scalar(value);
    } catch (const std::invalid_argument&) {
        throw XmlValueError("Invalid XML Unicode scalar");
    }
}

template <typename T>
T xml_parse_unicode_character(const std::string_view text) {
    try {
        const auto scalar = static_cast<std::uint32_t>(utf8::decode_one_scalar(text));
        if constexpr (std::is_same_v<T, char16_t> ||
                      (std::is_same_v<T, wchar_t> && sizeof(wchar_t) == 2U)) {
            if (scalar > 0xFFFFU) {
                throw std::invalid_argument("Unicode scalar does not fit one UTF-16 code unit");
            }
        } else if constexpr (std::is_same_v<T, wchar_t>) {
            static_assert(sizeof(wchar_t) == 4U,
                          "wchar_t must be UTF-16 or UTF-32 sized for XML scalar conversion");
        }
        return static_cast<T>(scalar);
    } catch (const std::invalid_argument&) {
        throw XmlValueError("Invalid XML Unicode scalar: " + std::string(text));
    }
}

template <typename T>
std::string xml_unicode_character_to_string(const T value) {
    static_assert(std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t> ||
                  std::is_same_v<T, wchar_t>);
    if constexpr (std::is_same_v<T, wchar_t>) {
        static_assert(sizeof(wchar_t) == 2U || sizeof(wchar_t) == 4U,
                      "wchar_t must be UTF-16 or UTF-32 sized for XML scalar conversion");
    }
    return xml_encode_unicode_scalar(static_cast<std::uint32_t>(value));
}

template <typename T>
std::string xml_unicode_string_to_string(const std::basic_string_view<T> text) {
    static_assert(sizeof(T) == 2U || sizeof(T) == 4U);
    std::string result;
    for (std::size_t index = 0U; index < text.size(); ++index) {
        auto scalar = static_cast<std::uint32_t>(text[index]);
        if constexpr (sizeof(T) == 2U) {
            if (scalar >= 0xD800U && scalar <= 0xDBFFU) {
                if (++index == text.size()) {
                    throw XmlValueError("Incomplete XML UTF-16 surrogate pair");
                }
                const auto low = static_cast<std::uint32_t>(text[index]);
                if (low < 0xDC00U || low > 0xDFFFU) {
                    throw XmlValueError("Invalid XML UTF-16 surrogate pair");
                }
                scalar = 0x10000U + ((scalar - 0xD800U) << 10U) + (low - 0xDC00U);
            }
        }
        // 共通のスカラー値検証は、単独の下位サロゲートと不正なUTF-32
        // 値を拒否し、Dの文字列間変換の失敗条件に合わせる。
        result += xml_encode_unicode_scalar(scalar);
    }
    return result;
}

template <typename T>
std::string xml_to_string(const T& value);

template <typename Range>
inline constexpr bool xml_is_character_range_v = [] {
    if constexpr (std::ranges::input_range<const Range>) {
        using Character = std::ranges::range_value_t<const Range>;
        return std::is_same_v<Character, char> ||
#ifdef __cpp_char8_t
               std::is_same_v<Character, char8_t> ||
#endif
               std::is_same_v<Character, char16_t> ||
               std::is_same_v<Character, char32_t> ||
               std::is_same_v<Character, wchar_t>;
    } else {
        return false;
    }
}();

template <typename T>
inline constexpr bool xml_is_string_like_v = [] {
    using U = std::remove_cvref_t<T>;
    // nullptr_tはnullのC文字列へ変換できるが、Dはnull
    // リテラルを値nullとして整形するため、値変換の経路に保つ。
    return !std::is_same_v<U, std::nullptr_t> &&
           (std::is_convertible_v<const U&, std::string_view> ||
#ifdef __cpp_char8_t
            std::is_convertible_v<const U&, std::u8string_view> ||
#endif
            std::is_convertible_v<const U&, std::u16string_view> ||
            std::is_convertible_v<const U&, std::u32string_view> ||
            std::is_convertible_v<const U&, std::wstring_view> ||
            xml_is_character_range_v<U>);
}();

struct XmlGraphicalRange {
    std::uint32_t first;
    std::uint32_t last;
};

// DMD 2.097.0のstd.uni.isGraphicalは、一般カ
// テゴリL、M、N、P、S、Zsを対象とする。これらの範囲は固定したその
// 判定を全Unicodeスカラー値に適用して生成したもの。この表を使い、
// ロケール依存のC/C++文字分類を避けて共通ライターの動作を維持する。
inline constexpr XmlGraphicalRange xml_graphical_ranges[] = {
    {0x0020U, 0x007EU},
    {0x00A0U, 0x00ACU},
    {0x00AEU, 0x0377U},
    {0x037AU, 0x037EU},
    {0x0384U, 0x038AU},
    {0x038CU, 0x038CU},
    {0x038EU, 0x03A1U},
    {0x03A3U, 0x0527U},
    {0x0531U, 0x0556U},
    {0x0559U, 0x055FU},
    {0x0561U, 0x0587U},
    {0x0589U, 0x058AU},
    {0x058FU, 0x058FU},
    {0x0591U, 0x05C7U},
    {0x05D0U, 0x05EAU},
    {0x05F0U, 0x05F4U},
    {0x0606U, 0x061BU},
    {0x061EU, 0x06DCU},
    {0x06DEU, 0x070DU},
    {0x0710U, 0x074AU},
    {0x074DU, 0x07B1U},
    {0x07C0U, 0x07FAU},
    {0x0800U, 0x082DU},
    {0x0830U, 0x083EU},
    {0x0840U, 0x085BU},
    {0x085EU, 0x085EU},
    {0x08A0U, 0x08A0U},
    {0x08A2U, 0x08ACU},
    {0x08E4U, 0x08FEU},
    {0x0900U, 0x0977U},
    {0x0979U, 0x097FU},
    {0x0981U, 0x0983U},
    {0x0985U, 0x098CU},
    {0x098FU, 0x0990U},
    {0x0993U, 0x09A8U},
    {0x09AAU, 0x09B0U},
    {0x09B2U, 0x09B2U},
    {0x09B6U, 0x09B9U},
    {0x09BCU, 0x09C4U},
    {0x09C7U, 0x09C8U},
    {0x09CBU, 0x09CEU},
    {0x09D7U, 0x09D7U},
    {0x09DCU, 0x09DDU},
    {0x09DFU, 0x09E3U},
    {0x09E6U, 0x09FBU},
    {0x0A01U, 0x0A03U},
    {0x0A05U, 0x0A0AU},
    {0x0A0FU, 0x0A10U},
    {0x0A13U, 0x0A28U},
    {0x0A2AU, 0x0A30U},
    {0x0A32U, 0x0A33U},
    {0x0A35U, 0x0A36U},
    {0x0A38U, 0x0A39U},
    {0x0A3CU, 0x0A3CU},
    {0x0A3EU, 0x0A42U},
    {0x0A47U, 0x0A48U},
    {0x0A4BU, 0x0A4DU},
    {0x0A51U, 0x0A51U},
    {0x0A59U, 0x0A5CU},
    {0x0A5EU, 0x0A5EU},
    {0x0A66U, 0x0A75U},
    {0x0A81U, 0x0A83U},
    {0x0A85U, 0x0A8DU},
    {0x0A8FU, 0x0A91U},
    {0x0A93U, 0x0AA8U},
    {0x0AAAU, 0x0AB0U},
    {0x0AB2U, 0x0AB3U},
    {0x0AB5U, 0x0AB9U},
    {0x0ABCU, 0x0AC5U},
    {0x0AC7U, 0x0AC9U},
    {0x0ACBU, 0x0ACDU},
    {0x0AD0U, 0x0AD0U},
    {0x0AE0U, 0x0AE3U},
    {0x0AE6U, 0x0AF1U},
    {0x0B01U, 0x0B03U},
    {0x0B05U, 0x0B0CU},
    {0x0B0FU, 0x0B10U},
    {0x0B13U, 0x0B28U},
    {0x0B2AU, 0x0B30U},
    {0x0B32U, 0x0B33U},
    {0x0B35U, 0x0B39U},
    {0x0B3CU, 0x0B44U},
    {0x0B47U, 0x0B48U},
    {0x0B4BU, 0x0B4DU},
    {0x0B56U, 0x0B57U},
    {0x0B5CU, 0x0B5DU},
    {0x0B5FU, 0x0B63U},
    {0x0B66U, 0x0B77U},
    {0x0B82U, 0x0B83U},
    {0x0B85U, 0x0B8AU},
    {0x0B8EU, 0x0B90U},
    {0x0B92U, 0x0B95U},
    {0x0B99U, 0x0B9AU},
    {0x0B9CU, 0x0B9CU},
    {0x0B9EU, 0x0B9FU},
    {0x0BA3U, 0x0BA4U},
    {0x0BA8U, 0x0BAAU},
    {0x0BAEU, 0x0BB9U},
    {0x0BBEU, 0x0BC2U},
    {0x0BC6U, 0x0BC8U},
    {0x0BCAU, 0x0BCDU},
    {0x0BD0U, 0x0BD0U},
    {0x0BD7U, 0x0BD7U},
    {0x0BE6U, 0x0BFAU},
    {0x0C01U, 0x0C03U},
    {0x0C05U, 0x0C0CU},
    {0x0C0EU, 0x0C10U},
    {0x0C12U, 0x0C28U},
    {0x0C2AU, 0x0C33U},
    {0x0C35U, 0x0C39U},
    {0x0C3DU, 0x0C44U},
    {0x0C46U, 0x0C48U},
    {0x0C4AU, 0x0C4DU},
    {0x0C55U, 0x0C56U},
    {0x0C58U, 0x0C59U},
    {0x0C60U, 0x0C63U},
    {0x0C66U, 0x0C6FU},
    {0x0C78U, 0x0C7FU},
    {0x0C82U, 0x0C83U},
    {0x0C85U, 0x0C8CU},
    {0x0C8EU, 0x0C90U},
    {0x0C92U, 0x0CA8U},
    {0x0CAAU, 0x0CB3U},
    {0x0CB5U, 0x0CB9U},
    {0x0CBCU, 0x0CC4U},
    {0x0CC6U, 0x0CC8U},
    {0x0CCAU, 0x0CCDU},
    {0x0CD5U, 0x0CD6U},
    {0x0CDEU, 0x0CDEU},
    {0x0CE0U, 0x0CE3U},
    {0x0CE6U, 0x0CEFU},
    {0x0CF1U, 0x0CF2U},
    {0x0D02U, 0x0D03U},
    {0x0D05U, 0x0D0CU},
    {0x0D0EU, 0x0D10U},
    {0x0D12U, 0x0D3AU},
    {0x0D3DU, 0x0D44U},
    {0x0D46U, 0x0D48U},
    {0x0D4AU, 0x0D4EU},
    {0x0D57U, 0x0D57U},
    {0x0D60U, 0x0D63U},
    {0x0D66U, 0x0D75U},
    {0x0D79U, 0x0D7FU},
    {0x0D82U, 0x0D83U},
    {0x0D85U, 0x0D96U},
    {0x0D9AU, 0x0DB1U},
    {0x0DB3U, 0x0DBBU},
    {0x0DBDU, 0x0DBDU},
    {0x0DC0U, 0x0DC6U},
    {0x0DCAU, 0x0DCAU},
    {0x0DCFU, 0x0DD4U},
    {0x0DD6U, 0x0DD6U},
    {0x0DD8U, 0x0DDFU},
    {0x0DF2U, 0x0DF4U},
    {0x0E01U, 0x0E3AU},
    {0x0E3FU, 0x0E5BU},
    {0x0E81U, 0x0E82U},
    {0x0E84U, 0x0E84U},
    {0x0E87U, 0x0E88U},
    {0x0E8AU, 0x0E8AU},
    {0x0E8DU, 0x0E8DU},
    {0x0E94U, 0x0E97U},
    {0x0E99U, 0x0E9FU},
    {0x0EA1U, 0x0EA3U},
    {0x0EA5U, 0x0EA5U},
    {0x0EA7U, 0x0EA7U},
    {0x0EAAU, 0x0EABU},
    {0x0EADU, 0x0EB9U},
    {0x0EBBU, 0x0EBDU},
    {0x0EC0U, 0x0EC4U},
    {0x0EC6U, 0x0EC6U},
    {0x0EC8U, 0x0ECDU},
    {0x0ED0U, 0x0ED9U},
    {0x0EDCU, 0x0EDFU},
    {0x0F00U, 0x0F47U},
    {0x0F49U, 0x0F6CU},
    {0x0F71U, 0x0F97U},
    {0x0F99U, 0x0FBCU},
    {0x0FBEU, 0x0FCCU},
    {0x0FCEU, 0x0FDAU},
    {0x1000U, 0x10C5U},
    {0x10C7U, 0x10C7U},
    {0x10CDU, 0x10CDU},
    {0x10D0U, 0x1248U},
    {0x124AU, 0x124DU},
    {0x1250U, 0x1256U},
    {0x1258U, 0x1258U},
    {0x125AU, 0x125DU},
    {0x1260U, 0x1288U},
    {0x128AU, 0x128DU},
    {0x1290U, 0x12B0U},
    {0x12B2U, 0x12B5U},
    {0x12B8U, 0x12BEU},
    {0x12C0U, 0x12C0U},
    {0x12C2U, 0x12C5U},
    {0x12C8U, 0x12D6U},
    {0x12D8U, 0x1310U},
    {0x1312U, 0x1315U},
    {0x1318U, 0x135AU},
    {0x135DU, 0x137CU},
    {0x1380U, 0x1399U},
    {0x13A0U, 0x13F4U},
    {0x1400U, 0x169CU},
    {0x16A0U, 0x16F0U},
    {0x1700U, 0x170CU},
    {0x170EU, 0x1714U},
    {0x1720U, 0x1736U},
    {0x1740U, 0x1753U},
    {0x1760U, 0x176CU},
    {0x176EU, 0x1770U},
    {0x1772U, 0x1773U},
    {0x1780U, 0x17DDU},
    {0x17E0U, 0x17E9U},
    {0x17F0U, 0x17F9U},
    {0x1800U, 0x180DU},
    {0x1810U, 0x1819U},
    {0x1820U, 0x1877U},
    {0x1880U, 0x18AAU},
    {0x18B0U, 0x18F5U},
    {0x1900U, 0x191CU},
    {0x1920U, 0x192BU},
    {0x1930U, 0x193BU},
    {0x1940U, 0x1940U},
    {0x1944U, 0x196DU},
    {0x1970U, 0x1974U},
    {0x1980U, 0x19ABU},
    {0x19B0U, 0x19C9U},
    {0x19D0U, 0x19DAU},
    {0x19DEU, 0x1A1BU},
    {0x1A1EU, 0x1A5EU},
    {0x1A60U, 0x1A7CU},
    {0x1A7FU, 0x1A89U},
    {0x1A90U, 0x1A99U},
    {0x1AA0U, 0x1AADU},
    {0x1B00U, 0x1B4BU},
    {0x1B50U, 0x1B7CU},
    {0x1B80U, 0x1BF3U},
    {0x1BFCU, 0x1C37U},
    {0x1C3BU, 0x1C49U},
    {0x1C4DU, 0x1C7FU},
    {0x1CC0U, 0x1CC7U},
    {0x1CD0U, 0x1CF6U},
    {0x1D00U, 0x1DE6U},
    {0x1DFCU, 0x1F15U},
    {0x1F18U, 0x1F1DU},
    {0x1F20U, 0x1F45U},
    {0x1F48U, 0x1F4DU},
    {0x1F50U, 0x1F57U},
    {0x1F59U, 0x1F59U},
    {0x1F5BU, 0x1F5BU},
    {0x1F5DU, 0x1F5DU},
    {0x1F5FU, 0x1F7DU},
    {0x1F80U, 0x1FB4U},
    {0x1FB6U, 0x1FC4U},
    {0x1FC6U, 0x1FD3U},
    {0x1FD6U, 0x1FDBU},
    {0x1FDDU, 0x1FEFU},
    {0x1FF2U, 0x1FF4U},
    {0x1FF6U, 0x1FFEU},
    {0x2000U, 0x200AU},
    {0x2010U, 0x2027U},
    {0x202FU, 0x205FU},
    {0x2070U, 0x2071U},
    {0x2074U, 0x208EU},
    {0x2090U, 0x209CU},
    {0x20A0U, 0x20BAU},
    {0x20D0U, 0x20F0U},
    {0x2100U, 0x2189U},
    {0x2190U, 0x23F3U},
    {0x2400U, 0x2426U},
    {0x2440U, 0x244AU},
    {0x2460U, 0x26FFU},
    {0x2701U, 0x2B4CU},
    {0x2B50U, 0x2B59U},
    {0x2C00U, 0x2C2EU},
    {0x2C30U, 0x2C5EU},
    {0x2C60U, 0x2CF3U},
    {0x2CF9U, 0x2D25U},
    {0x2D27U, 0x2D27U},
    {0x2D2DU, 0x2D2DU},
    {0x2D30U, 0x2D67U},
    {0x2D6FU, 0x2D70U},
    {0x2D7FU, 0x2D96U},
    {0x2DA0U, 0x2DA6U},
    {0x2DA8U, 0x2DAEU},
    {0x2DB0U, 0x2DB6U},
    {0x2DB8U, 0x2DBEU},
    {0x2DC0U, 0x2DC6U},
    {0x2DC8U, 0x2DCEU},
    {0x2DD0U, 0x2DD6U},
    {0x2DD8U, 0x2DDEU},
    {0x2DE0U, 0x2E3BU},
    {0x2E80U, 0x2E99U},
    {0x2E9BU, 0x2EF3U},
    {0x2F00U, 0x2FD5U},
    {0x2FF0U, 0x2FFBU},
    {0x3000U, 0x303FU},
    {0x3041U, 0x3096U},
    {0x3099U, 0x30FFU},
    {0x3105U, 0x312DU},
    {0x3131U, 0x318EU},
    {0x3190U, 0x31BAU},
    {0x31C0U, 0x31E3U},
    {0x31F0U, 0x321EU},
    {0x3220U, 0x32FEU},
    {0x3300U, 0x4DB5U},
    {0x4DC0U, 0x9FCCU},
    {0xA000U, 0xA48CU},
    {0xA490U, 0xA4C6U},
    {0xA4D0U, 0xA62BU},
    {0xA640U, 0xA697U},
    {0xA69FU, 0xA6F7U},
    {0xA700U, 0xA78EU},
    {0xA790U, 0xA793U},
    {0xA7A0U, 0xA7AAU},
    {0xA7F8U, 0xA82BU},
    {0xA830U, 0xA839U},
    {0xA840U, 0xA877U},
    {0xA880U, 0xA8C4U},
    {0xA8CEU, 0xA8D9U},
    {0xA8E0U, 0xA8FBU},
    {0xA900U, 0xA953U},
    {0xA95FU, 0xA97CU},
    {0xA980U, 0xA9CDU},
    {0xA9CFU, 0xA9D9U},
    {0xA9DEU, 0xA9DFU},
    {0xAA00U, 0xAA36U},
    {0xAA40U, 0xAA4DU},
    {0xAA50U, 0xAA59U},
    {0xAA5CU, 0xAA7BU},
    {0xAA80U, 0xAAC2U},
    {0xAADBU, 0xAAF6U},
    {0xAB01U, 0xAB06U},
    {0xAB09U, 0xAB0EU},
    {0xAB11U, 0xAB16U},
    {0xAB20U, 0xAB26U},
    {0xAB28U, 0xAB2EU},
    {0xABC0U, 0xABEDU},
    {0xABF0U, 0xABF9U},
    {0xAC00U, 0xD7A3U},
    {0xD7B0U, 0xD7C6U},
    {0xD7CBU, 0xD7FBU},
    {0xF900U, 0xFA6DU},
    {0xFA70U, 0xFAD9U},
    {0xFB00U, 0xFB06U},
    {0xFB13U, 0xFB17U},
    {0xFB1DU, 0xFB36U},
    {0xFB38U, 0xFB3CU},
    {0xFB3EU, 0xFB3EU},
    {0xFB40U, 0xFB41U},
    {0xFB43U, 0xFB44U},
    {0xFB46U, 0xFBC1U},
    {0xFBD3U, 0xFD3FU},
    {0xFD50U, 0xFD8FU},
    {0xFD92U, 0xFDC7U},
    {0xFDF0U, 0xFDFDU},
    {0xFE00U, 0xFE19U},
    {0xFE20U, 0xFE26U},
    {0xFE30U, 0xFE52U},
    {0xFE54U, 0xFE66U},
    {0xFE68U, 0xFE6BU},
    {0xFE70U, 0xFE74U},
    {0xFE76U, 0xFEFCU},
    {0xFF01U, 0xFFBEU},
    {0xFFC2U, 0xFFC7U},
    {0xFFCAU, 0xFFCFU},
    {0xFFD2U, 0xFFD7U},
    {0xFFDAU, 0xFFDCU},
    {0xFFE0U, 0xFFE6U},
    {0xFFE8U, 0xFFEEU},
    {0xFFFCU, 0xFFFDU},
    {0x10000U, 0x1000BU},
    {0x1000DU, 0x10026U},
    {0x10028U, 0x1003AU},
    {0x1003CU, 0x1003DU},
    {0x1003FU, 0x1004DU},
    {0x10050U, 0x1005DU},
    {0x10080U, 0x100FAU},
    {0x10100U, 0x10102U},
    {0x10107U, 0x10133U},
    {0x10137U, 0x1018AU},
    {0x10190U, 0x1019BU},
    {0x101D0U, 0x101FDU},
    {0x10280U, 0x1029CU},
    {0x102A0U, 0x102D0U},
    {0x10300U, 0x1031EU},
    {0x10320U, 0x10323U},
    {0x10330U, 0x1034AU},
    {0x10380U, 0x1039DU},
    {0x1039FU, 0x103C3U},
    {0x103C8U, 0x103D5U},
    {0x10400U, 0x1049DU},
    {0x104A0U, 0x104A9U},
    {0x10800U, 0x10805U},
    {0x10808U, 0x10808U},
    {0x1080AU, 0x10835U},
    {0x10837U, 0x10838U},
    {0x1083CU, 0x1083CU},
    {0x1083FU, 0x10855U},
    {0x10857U, 0x1085FU},
    {0x10900U, 0x1091BU},
    {0x1091FU, 0x10939U},
    {0x1093FU, 0x1093FU},
    {0x10980U, 0x109B7U},
    {0x109BEU, 0x109BFU},
    {0x10A00U, 0x10A03U},
    {0x10A05U, 0x10A06U},
    {0x10A0CU, 0x10A13U},
    {0x10A15U, 0x10A17U},
    {0x10A19U, 0x10A33U},
    {0x10A38U, 0x10A3AU},
    {0x10A3FU, 0x10A47U},
    {0x10A50U, 0x10A58U},
    {0x10A60U, 0x10A7FU},
    {0x10B00U, 0x10B35U},
    {0x10B39U, 0x10B55U},
    {0x10B58U, 0x10B72U},
    {0x10B78U, 0x10B7FU},
    {0x10C00U, 0x10C48U},
    {0x10E60U, 0x10E7EU},
    {0x11000U, 0x1104DU},
    {0x11052U, 0x1106FU},
    {0x11080U, 0x110BCU},
    {0x110BEU, 0x110C1U},
    {0x110D0U, 0x110E8U},
    {0x110F0U, 0x110F9U},
    {0x11100U, 0x11134U},
    {0x11136U, 0x11143U},
    {0x11180U, 0x111C8U},
    {0x111D0U, 0x111D9U},
    {0x11680U, 0x116B7U},
    {0x116C0U, 0x116C9U},
    {0x12000U, 0x1236EU},
    {0x12400U, 0x12462U},
    {0x12470U, 0x12473U},
    {0x13000U, 0x1342EU},
    {0x16800U, 0x16A38U},
    {0x16F00U, 0x16F44U},
    {0x16F50U, 0x16F7EU},
    {0x16F8FU, 0x16F9FU},
    {0x1B000U, 0x1B001U},
    {0x1D000U, 0x1D0F5U},
    {0x1D100U, 0x1D126U},
    {0x1D129U, 0x1D172U},
    {0x1D17BU, 0x1D1DDU},
    {0x1D200U, 0x1D245U},
    {0x1D300U, 0x1D356U},
    {0x1D360U, 0x1D371U},
    {0x1D400U, 0x1D454U},
    {0x1D456U, 0x1D49CU},
    {0x1D49EU, 0x1D49FU},
    {0x1D4A2U, 0x1D4A2U},
    {0x1D4A5U, 0x1D4A6U},
    {0x1D4A9U, 0x1D4ACU},
    {0x1D4AEU, 0x1D4B9U},
    {0x1D4BBU, 0x1D4BBU},
    {0x1D4BDU, 0x1D4C3U},
    {0x1D4C5U, 0x1D505U},
    {0x1D507U, 0x1D50AU},
    {0x1D50DU, 0x1D514U},
    {0x1D516U, 0x1D51CU},
    {0x1D51EU, 0x1D539U},
    {0x1D53BU, 0x1D53EU},
    {0x1D540U, 0x1D544U},
    {0x1D546U, 0x1D546U},
    {0x1D54AU, 0x1D550U},
    {0x1D552U, 0x1D6A5U},
    {0x1D6A8U, 0x1D7CBU},
    {0x1D7CEU, 0x1D7FFU},
    {0x1EE00U, 0x1EE03U},
    {0x1EE05U, 0x1EE1FU},
    {0x1EE21U, 0x1EE22U},
    {0x1EE24U, 0x1EE24U},
    {0x1EE27U, 0x1EE27U},
    {0x1EE29U, 0x1EE32U},
    {0x1EE34U, 0x1EE37U},
    {0x1EE39U, 0x1EE39U},
    {0x1EE3BU, 0x1EE3BU},
    {0x1EE42U, 0x1EE42U},
    {0x1EE47U, 0x1EE47U},
    {0x1EE49U, 0x1EE49U},
    {0x1EE4BU, 0x1EE4BU},
    {0x1EE4DU, 0x1EE4FU},
    {0x1EE51U, 0x1EE52U},
    {0x1EE54U, 0x1EE54U},
    {0x1EE57U, 0x1EE57U},
    {0x1EE59U, 0x1EE59U},
    {0x1EE5BU, 0x1EE5BU},
    {0x1EE5DU, 0x1EE5DU},
    {0x1EE5FU, 0x1EE5FU},
    {0x1EE61U, 0x1EE62U},
    {0x1EE64U, 0x1EE64U},
    {0x1EE67U, 0x1EE6AU},
    {0x1EE6CU, 0x1EE72U},
    {0x1EE74U, 0x1EE77U},
    {0x1EE79U, 0x1EE7CU},
    {0x1EE7EU, 0x1EE7EU},
    {0x1EE80U, 0x1EE89U},
    {0x1EE8BU, 0x1EE9BU},
    {0x1EEA1U, 0x1EEA3U},
    {0x1EEA5U, 0x1EEA9U},
    {0x1EEABU, 0x1EEBBU},
    {0x1EEF0U, 0x1EEF1U},
    {0x1F000U, 0x1F02BU},
    {0x1F030U, 0x1F093U},
    {0x1F0A0U, 0x1F0AEU},
    {0x1F0B1U, 0x1F0BEU},
    {0x1F0C1U, 0x1F0CFU},
    {0x1F0D1U, 0x1F0DFU},
    {0x1F100U, 0x1F10AU},
    {0x1F110U, 0x1F12EU},
    {0x1F130U, 0x1F16BU},
    {0x1F170U, 0x1F19AU},
    {0x1F1E6U, 0x1F202U},
    {0x1F210U, 0x1F23AU},
    {0x1F240U, 0x1F248U},
    {0x1F250U, 0x1F251U},
    {0x1F300U, 0x1F320U},
    {0x1F330U, 0x1F335U},
    {0x1F337U, 0x1F37CU},
    {0x1F380U, 0x1F393U},
    {0x1F3A0U, 0x1F3C4U},
    {0x1F3C6U, 0x1F3CAU},
    {0x1F3E0U, 0x1F3F0U},
    {0x1F400U, 0x1F43EU},
    {0x1F440U, 0x1F440U},
    {0x1F442U, 0x1F4F7U},
    {0x1F4F9U, 0x1F4FCU},
    {0x1F500U, 0x1F53DU},
    {0x1F540U, 0x1F543U},
    {0x1F550U, 0x1F567U},
    {0x1F5FBU, 0x1F640U},
    {0x1F645U, 0x1F64FU},
    {0x1F680U, 0x1F6C5U},
    {0x1F700U, 0x1F773U},
    {0x20000U, 0x2A6D6U},
    {0x2A700U, 0x2B734U},
    {0x2B740U, 0x2B81DU},
    {0x2F800U, 0x2FA1DU},
    {0xE0100U, 0xE01EFU},
};

inline bool xml_unicode_is_graphical(const std::uint32_t scalar) noexcept {
    std::size_t lower = 0U;
    std::size_t upper = sizeof(xml_graphical_ranges) / sizeof(xml_graphical_ranges[0]);
    while (lower < upper) {
        const auto middle = lower + (upper - lower) / 2U;
        const auto& range = xml_graphical_ranges[middle];
        if (scalar < range.first) {
            upper = middle;
        } else if (scalar > range.last) {
            lower = middle + 1U;
        } else {
            return true;
        }
    }
    return false;
}

inline void xml_append_array_hex(std::string& result, const std::uint32_t value,
                                 const std::size_t width) {
    constexpr char hex[] = "0123456789ABCDEF";
    for (std::size_t digit = width; digit > 0U; --digit) {
        const auto shift = static_cast<unsigned>((digit - 1U) * 4U);
        result.push_back(hex[(value >> shift) & 0x0FU]);
    }
}

inline void xml_append_unpadded_array_hex(std::string& result, std::uint32_t value) {
    constexpr char hex[] = "0123456789ABCDEF";
    char digits[8];
    std::size_t count = 0U;
    do {
        digits[count++] = hex[value & 0x0FU];
        value >>= 4U;
    } while (value != 0U);
    while (count > 0U) result.push_back(digits[--count]);
}

template <typename Character>
constexpr std::string_view xml_d_array_character_type_name() noexcept {
    using CharacterType = std::remove_cv_t<Character>;
    if constexpr (std::is_same_v<CharacterType, char>
#ifdef __cpp_char8_t
                  || std::is_same_v<CharacterType, char8_t>
#endif
    ) {
        return "char";
    } else if constexpr (std::is_same_v<CharacterType, char16_t> ||
                         (std::is_same_v<CharacterType, wchar_t> && sizeof(wchar_t) == 2U)) {
        return "wchar";
    } else {
        return "dchar";
    }
}

template <typename Character>
std::uint32_t xml_array_code_unit_value(const Character value) noexcept {
    using CharacterType = std::remove_cv_t<Character>;
    static_assert(sizeof(CharacterType) == 1U || sizeof(CharacterType) == 2U ||
                  sizeof(CharacterType) == 4U);
    if constexpr (sizeof(CharacterType) == 1U) {
        return static_cast<unsigned char>(value);
    } else if constexpr (sizeof(CharacterType) == 2U) {
        return static_cast<std::uint16_t>(value);
    } else {
        return static_cast<std::uint32_t>(value);
    }
}

template <typename Character>
std::string xml_format_invalid_array_string(
    const std::basic_string_view<Character> code_units) {
    std::string result = "[";
    const auto type_name = xml_d_array_character_type_name<Character>();
    bool first = true;
    for (const auto code_unit : code_units) {
        if (!first) result += ", ";
        result += "cast(";
        result += type_name;
        result += ") 0x";
        xml_append_unpadded_array_hex(result, xml_array_code_unit_value(code_unit));
        first = false;
    }
    result.push_back(']');
    return result;
}

inline void xml_format_array_codepoint(std::string& result, const std::uint32_t scalar) {
    if (xml_unicode_is_graphical(scalar)) {
        if (scalar == static_cast<std::uint32_t>('"') || scalar == static_cast<std::uint32_t>('\\')) {
            result.push_back('\\');
        }
        result += xml_encode_unicode_scalar(scalar);
        return;
    }

    if (scalar <= 0xFFU) {
        switch (scalar) {
            case '\n': result += "\\n"; return;
            case '\r': result += "\\r"; return;
            case '\t': result += "\\t"; return;
            case '\a': result += "\\a"; return;
            case '\b': result += "\\b"; return;
            case '\f': result += "\\f"; return;
            case '\v': result += "\\v"; return;
            case '\0': result += "\\0"; return;
            default: break;
        }
        result += "\\x";
        xml_append_array_hex(result, scalar, 2U);
    } else if (scalar <= 0xFFFFU) {
        result += "\\u";
        xml_append_array_hex(result, scalar, 4U);
    } else {
        result += "\\U";
        xml_append_array_hex(result, scalar, 8U);
    }
}

template <typename Character>
std::string xml_format_array_string(const std::basic_string_view<Character> code_units) {
    static_assert(sizeof(Character) == 1U || sizeof(Character) == 2U ||
                  sizeof(Character) == 4U);
    if (code_units.empty()) return "\"\"";

    std::vector<std::uint32_t> scalars;
    scalars.reserve(code_units.size());
    bool valid = true;

    if constexpr (std::is_same_v<std::remove_cv_t<Character>, char>
#ifdef __cpp_char8_t
                  || std::is_same_v<std::remove_cv_t<Character>, char8_t>
#endif
    ) {
        std::string_view remaining;
        if constexpr (std::is_same_v<std::remove_cv_t<Character>, char>) {
            remaining = std::string_view(code_units.data(), code_units.size());
        } else {
            remaining = std::string_view(reinterpret_cast<const char*>(code_units.data()),
                                         code_units.size());
        }
        try {
            while (!remaining.empty()) {
                const auto scalar = static_cast<std::uint32_t>(utf8::consume_scalar(remaining));
                if (scalar == 0xFFFEU || scalar == 0xFFFFU) {
                    valid = false;
                    break;
                }
                scalars.push_back(scalar);
            }
        } catch (const std::invalid_argument&) {
            valid = false;
        }
    } else if constexpr (sizeof(Character) == 2U) {
        for (std::size_t index = 0U; index < code_units.size(); ++index) {
            auto scalar = xml_array_code_unit_value(code_units[index]);
            if (scalar >= 0xD800U && scalar <= 0xDBFFU) {
                if (++index == code_units.size()) {
                    valid = false;
                    break;
                }
                const auto low = xml_array_code_unit_value(code_units[index]);
                if (low < 0xDC00U || low > 0xDFFFU) {
                    valid = false;
                    break;
                }
                scalar = 0x10000U + ((scalar - 0xD800U) << 10U) + (low - 0xDC00U);
            } else if (scalar >= 0xDC00U && scalar <= 0xDFFFU) {
                valid = false;
                break;
            }
            if (scalar == 0xFFFEU || scalar == 0xFFFFU) {
                valid = false;
                break;
            }
            scalars.push_back(scalar);
        }
    } else {
        for (const auto code_unit : code_units) {
            const auto scalar = xml_array_code_unit_value(code_unit);
            if (!utf8::is_scalar(scalar) || scalar == 0xFFFEU || scalar == 0xFFFFU) {
                valid = false;
                break;
            }
            scalars.push_back(scalar);
        }
    }

    if (!valid) return xml_format_invalid_array_string(code_units);

    std::string result = "\"";
    for (const auto scalar : scalars) xml_format_array_codepoint(result, scalar);
    result.push_back('"');
    return result;
}

template <typename StringLike>
std::string xml_format_array_string_element(const StringLike& value) {
    using U = std::remove_cvref_t<StringLike>;
    if constexpr (std::is_pointer_v<U>) {
        if (value == nullptr) return "\"\"";
        using Character = std::remove_cv_t<std::remove_pointer_t<U>>;
        const auto length = std::char_traits<Character>::length(value);
        return xml_format_array_string(std::basic_string_view<Character>(value, length));
    } else if constexpr (std::is_array_v<U>) {
        using Character = std::remove_cv_t<std::remove_extent_t<U>>;
        constexpr std::size_t extent = std::extent_v<U>;
        std::size_t length = extent;
        if constexpr (extent != 0U) {
            if (value[extent - 1U] == Character{}) --length;
        }
        return xml_format_array_string(std::basic_string_view<Character>(value, length));
    } else if constexpr (xml_is_character_range_v<U>) {
        using Character = std::ranges::range_value_t<const U>;
        std::basic_string<Character> code_units;
        if constexpr (std::ranges::sized_range<const U>) {
            code_units.reserve(static_cast<std::size_t>(std::ranges::size(value)));
        }
        for (const auto code_unit : value) code_units.push_back(code_unit);
        return xml_format_array_string(std::basic_string_view<Character>(code_units));
    } else if constexpr (requires { std::string_view(value); }) {
        return xml_format_array_string(std::string_view(value));
#ifdef __cpp_char8_t
    } else if constexpr (requires { std::u8string_view(value); }) {
        return xml_format_array_string(std::u8string_view(value));
#endif
    } else if constexpr (requires { std::u16string_view(value); }) {
        return xml_format_array_string(std::u16string_view(value));
    } else if constexpr (requires { std::u32string_view(value); }) {
        return xml_format_array_string(std::u32string_view(value));
    } else if constexpr (requires { std::wstring_view(value); }) {
        return xml_format_array_string(std::wstring_view(value));
    } else {
        return xml_format_array_string(std::string_view(xml_to_string(value)));
    }
}
template <typename Range>
std::string xml_character_range_to_string(const Range& value) {
    using Character = std::ranges::range_value_t<const Range>;
    if constexpr (std::is_same_v<Character, char>
#ifdef __cpp_char8_t
                  || std::is_same_v<Character, char8_t>
#endif
    ) {
        std::string result;
        if constexpr (std::ranges::sized_range<const Range>) {
            result.reserve(static_cast<std::size_t>(std::ranges::size(value)));
        }
        for (const auto character : value) {
            result.push_back(static_cast<char>(character));
        }
        return result;
    } else {
        using String = std::basic_string<Character>;
        String converted;
        if constexpr (std::ranges::sized_range<const Range>) {
            converted.reserve(static_cast<std::size_t>(std::ranges::size(value)));
        }
        for (const auto character : value) {
            converted.push_back(character);
        }
        return xml_unicode_string_to_string(std::basic_string_view<Character>(converted));
    }
}

template <typename T>
std::string xml_format_container_element(const T& value) {
    if constexpr (xml_is_string_like_v<T>) {
        return xml_format_array_string_element(value);
    } else {
        return xml_to_string(value);
    }
}

template <typename Range>
std::string xml_range_to_string(const Range& value) {
    // Dのstd.convは配列と連想配列を再帰的に整形する。ネイティブのDictionary/SharedDictionary
    // と標準mapのキー型・値型の契約で連想範囲を識別
    // する。任意の集成体は引き続き挿入演算子を使う。
    std::string result = "[";
    bool first = true;
    for (const auto& element : value) {
        if (!first) result += ", ";
        if constexpr (requires { typename Range::key_type; typename Range::mapped_type; }) {
            result += xml_format_container_element(element.first);
            result += ":";
            result += xml_format_container_element(element.second);
        } else {
            result += xml_format_container_element(element);
        }
        first = false;
    }
    result.push_back(']');
    return result;
}

template <typename T>
std::string xml_to_string(const T& value) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, std::nullptr_t>) {
        return "null";
    } else if constexpr (std::is_same_v<U, std::string>) {
        return value;
    } else if constexpr (std::is_same_v<U, std::string_view>) {
        return std::string(value);
    } else if constexpr (std::is_array_v<U> &&
                         std::is_same_v<std::remove_cv_t<std::remove_extent_t<U>>, char>) {
        constexpr std::size_t extent = std::extent_v<U>;
        std::size_t length = extent;
        if constexpr (extent != 0U) {
            // C++文字列リテラルの末尾のゼロは終端。それより前のゼ
            // ロのコード単位は、D互換の文字列値の一部として残す。
            if (value[extent - 1U] == '\0') --length;
        }
        return std::string(value, length);
#ifdef __cpp_char8_t
    } else if constexpr (std::is_same_v<U, std::u8string>) {
        // C++20のchar8_t文字列は、Dのchar[]に対応するUTF-8
        // コード単位を持つ。埋め込みNULバイトも含め、明示された長さを維持する。
        if (value.empty()) return {};
        return std::string(reinterpret_cast<const char*>(value.data()), value.size());
    } else if constexpr (std::is_same_v<U, std::u8string_view>) {
        if (value.empty()) return {};
        return std::string(reinterpret_cast<const char*>(value.data()), value.size());
    } else if constexpr (std::is_array_v<U> &&
                         std::is_same_v<std::remove_cv_t<std::remove_extent_t<U>>, char8_t>) {
        constexpr std::size_t extent = std::extent_v<U>;
        std::size_t length = extent;
        if constexpr (extent != 0U) {
            // u8文字列リテラルの末尾には、内容に含まれない終端が一つある。
            // それより前のNULコード単位は、Dの文字列と同様に保持する。
            if (value[extent - 1U] == u8'\0') --length;
        }
        if (length == 0U) return {};
        return std::string(reinterpret_cast<const char*>(value), length);
    } else if constexpr (std::is_same_v<U, char8_t*> ||
                         std::is_same_v<U, const char8_t*>) {
        if (value == nullptr) return {};
        const std::u8string_view view(value);
        if (view.empty()) return {};
        return std::string(reinterpret_cast<const char*>(view.data()), view.size());
#endif
    } else if constexpr (std::is_same_v<U, char*> || std::is_same_v<U, const char*>) {
        return value == nullptr ? std::string{} : std::string(value);
    } else if constexpr (std::is_array_v<U> &&
                         (std::is_same_v<std::remove_cv_t<std::remove_extent_t<U>>, char16_t> ||
                          std::is_same_v<std::remove_cv_t<std::remove_extent_t<U>>, char32_t> ||
                          std::is_same_v<std::remove_cv_t<std::remove_extent_t<U>>, wchar_t>)) {
        using Character = std::remove_cv_t<std::remove_extent_t<U>>;
        constexpr std::size_t extent = std::extent_v<U>;
        std::size_t length = extent;
        if constexpr (extent != 0U) {
            if (value[extent - 1U] == Character{}) --length;
        }
        return xml_unicode_string_to_string(std::basic_string_view<Character>(value, length));
    } else if constexpr (std::is_convertible_v<const T&, std::u16string_view> ||
                         std::is_convertible_v<const T&, std::u32string_view> ||
                         std::is_convertible_v<const T&, std::wstring_view>) {
        // Dのto!stringはwchar/dchar文字列全体を文字コード
        // 変換する。所有文字列、ビュー、C文字列も同じ経路を使い、UTF-16/UTF-32
        // 文字列を挿入できないナロー文字ストリームへ渡さない。
        if constexpr (std::is_pointer_v<U>) {
            if (value == nullptr) return {};
        }
        if constexpr (std::is_convertible_v<const T&, std::u16string_view>) {
            return xml_unicode_string_to_string(std::u16string_view(value));
        } else if constexpr (std::is_convertible_v<const T&, std::u32string_view>) {
            return xml_unicode_string_to_string(std::u32string_view(value));
        } else {
            return xml_unicode_string_to_string(std::wstring_view(value));
        }
#ifdef __cpp_char8_t
    } else if constexpr (std::is_same_v<U, char8_t>) {
        // DのcharはUTF-8の生のコード単位一つ。C++20ではchar8_tが対応する。
        return std::string(reinterpret_cast<const char*>(&value), sizeof(value));
#endif
    } else if constexpr (std::is_same_v<U, char>) {
        // DのcharはUTF-8の生のコード単位一つであり、数値やUnicodeスカラー値ではない。
        return std::string(1U, value);
    } else if constexpr (std::is_same_v<U, char16_t> || std::is_same_v<U, char32_t> ||
                         std::is_same_v<U, wchar_t>) {
        // Dのwchar/dcharは一つのUnicodeスカラー値としてUTF-8へ符号化する。wchar_tはネ
        // イティブのコード単位幅に従い、WindowsではUTF-16、一般的なUnixではUTF-32として扱う。
        return xml_unicode_character_to_string(value);
    } else if constexpr (std::is_same_v<U, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_enum_v<U>) {
        if constexpr (requires { XmlEnumTraits<U>::to_string(value); }) {
            return XmlEnumTraits<U>::to_string(value);
        } else {
            // 未登録のC++列挙は、数値へ変換する拡張動作を維持する。Dのstd.conv.to!string
            // は列挙メンバーのシンボル名を使う。
            using Underlying = std::underlying_type_t<U>;
            return xml_to_string(static_cast<Underlying>(value));
        }
    } else if constexpr (is_real_number_v<U>) {
        return scalar_format::floating_to_string(value);
    } else if constexpr (std::is_arithmetic_v<U>) {
        // 一般的な実装ではuint8_t/int8_tは文字型になるが、
        // dxml/XNodeの呼び出し側は数値としての整形を期待する。
        std::ostringstream stream;
        // 数値の出力はプロセスのロケールに依存させない。
        stream.imbue(std::locale::classic());
        if constexpr (std::is_same_v<U, signed char> || std::is_same_v<U, unsigned char>) {
            stream << static_cast<int>(value);
        } else {
            stream << value;
        }
        return stream.str();
    } else if constexpr (requires { std::string(value.toString()); }) {
        // Dのstd.convは既存のtoStringメソッドへ委ねる。対応するオブジ
        // ェクトでも、範囲やストリームとして扱う前に、その文字列表現の生成処理を使う。
        return std::string(value.toString());
    } else if constexpr (xml_is_character_range_v<U>) {
        // Dのchar[]は生のUTF-8コード単位、wchar[]/dchar[]
        // はUnicode文字列。これらの配列の契約を数値の配列と区別する。
        return xml_character_range_to_string(value);
    } else if constexpr (std::ranges::input_range<const U>) {
        return xml_range_to_string(value);
    } else {
        std::ostringstream stream;
        stream << value;
        return stream.str();
    }
}

template <typename T>
T xml_parse_floating(std::string_view text) {
    try {
        return scalar_conversion::floating_from_string<T>(text);
    } catch (const std::invalid_argument&) {
        throw XmlValueError("Invalid XML number: " + std::string(text));
    }
}

template <typename T>
T xml_from_string(std::string_view text);

template <typename T>
struct xml_vector_traits {
    static constexpr bool is_vector = false;
};

template <typename Element, typename Allocator>
struct xml_vector_traits<std::vector<Element, Allocator>> {
    static constexpr bool is_vector = true;
    using element_type = Element;
};

template <typename T>
inline constexpr bool xml_is_vector_v =
    xml_vector_traits<std::remove_cvref_t<T>>::is_vector;

template <typename T>
struct xml_static_array_traits {
    static constexpr bool is_static_array = false;
};

template <typename Element, std::size_t Size>
struct xml_static_array_traits<std::array<Element, Size>> {
    static constexpr bool is_static_array = true;
    using element_type = Element;
    static constexpr std::size_t size = Size;
};

template <typename T>
inline constexpr bool xml_is_static_array_v =
    xml_static_array_traits<std::remove_cvref_t<T>>::is_static_array;

template <typename T>
struct xml_dictionary_traits {
    static constexpr bool is_dictionary = false;
};

template <typename Key, typename Value>
struct xml_dictionary_traits<Dictionary<Key, Value>> {
    static constexpr bool is_dictionary = true;
    using key_type = Key;
    using mapped_type = Value;
};

template <typename Key, typename Value>
struct xml_dictionary_traits<SharedDictionary<Key, Value>> {
    static constexpr bool is_dictionary = true;
    using key_type = Key;
    using mapped_type = Value;
};

template <typename T>
inline constexpr bool xml_is_dictionary_v =
    xml_dictionary_traits<std::remove_cvref_t<T>>::is_dictionary;

template <typename T>
inline constexpr bool xml_is_container_v =
    xml_is_vector_v<T> || xml_is_static_array_v<T> || xml_is_dictionary_v<T>;

template <typename T>
inline constexpr bool xml_is_owning_string_v =
    std::is_same_v<std::remove_cvref_t<T>, std::string>
#ifdef __cpp_char8_t
    || std::is_same_v<std::remove_cvref_t<T>, std::u8string>
#endif
    || std::is_same_v<std::remove_cvref_t<T>, std::u16string>
    || std::is_same_v<std::remove_cvref_t<T>, std::u32string>
    || std::is_same_v<std::remove_cvref_t<T>, std::wstring>;

template <typename T>
inline constexpr bool xml_is_character_code_unit_v =
    std::is_same_v<std::remove_cv_t<T>, char>
#ifdef __cpp_char8_t
    || std::is_same_v<std::remove_cv_t<T>, char8_t>
#endif
    || std::is_same_v<std::remove_cv_t<T>, char16_t>
    || std::is_same_v<std::remove_cv_t<T>, char32_t>
    || std::is_same_v<std::remove_cv_t<T>, wchar_t>;

template <typename T>
inline constexpr bool xml_is_dchar_code_unit_v =
    std::is_same_v<std::remove_cv_t<T>, char32_t>;

template <typename T>
constexpr bool xml_is_supported_array_element() {
    using U = std::remove_cvref_t<T>;
    if constexpr (xml_is_vector_v<U>) {
        using Element = typename xml_vector_traits<U>::element_type;
        return xml_is_supported_array_element<Element>();
    } else if constexpr (xml_is_static_array_v<U>) {
        using Element = typename xml_static_array_traits<U>::element_type;
        return xml_is_supported_array_element<Element>();
    } else if constexpr (xml_is_dictionary_v<U>) {
        using Traits = xml_dictionary_traits<U>;
        return xml_is_supported_array_element<typename Traits::key_type>() &&
               xml_is_supported_array_element<typename Traits::mapped_type>();
    } else {
        // Dのstd.convはスカラー、列挙、文字列の型群の配列を解析
        // する。このアダプターも対応する型群に限定し、将来ストリームから
        // 読み取り可能なC++型すべてをDの配列と同等として扱わない。
        return std::is_same_v<U, bool> || std::is_integral_v<U> ||
               is_real_number_v<U> || std::is_enum_v<U> ||
               xml_is_owning_string_v<U> || xml_is_character_code_unit_v<U>;
    }
}

template <typename Vector>
Vector xml_character_vector_from_string(const std::string_view text) {
    using Character = typename Vector::value_type;
    Vector result;
    if constexpr (std::is_same_v<Character, char>
#ifdef __cpp_char8_t
                  || std::is_same_v<Character, char8_t>
#endif
    ) {
        result.reserve(text.size());
        for (const unsigned char code_unit : text) {
            result.push_back(static_cast<Character>(code_unit));
        }
    } else {
        using String = std::basic_string<Character>;
        const auto converted = xml_from_string<String>(text);
        result.assign(converted.begin(), converted.end());
    }
    return result;
}

class XmlArrayValueParser {
public:
    explicit XmlArrayValueParser(const std::string_view text) : text_(text) {}

    template <typename Container>
    Container parse() {
        auto result = parse_container<Container>();
        if (position_ != text_.size()) fail();
        return result;
    }

private:
    static constexpr bool is_space(const char value) noexcept {
        return value == ' ' || value == '\t' || value == '\n' ||
               value == '\r' || value == '\v' || value == '\f';
    }

    void skip_space() noexcept {
        while (position_ < text_.size() && is_space(text_[position_])) ++position_;
    }

    [[noreturn]] void fail() const {
        throw XmlValueError("Invalid XML array: " + std::string(text_));
    }

    void require(const char expected) {
        if (position_ >= text_.size() || text_[position_] != expected) fail();
        ++position_;
    }

    static bool is_octal_digit(const char value) noexcept {
        return value >= '0' && value <= '7';
    }

    static int hex_digit(const char value) noexcept {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        return -1;
    }

    std::uint32_t parse_escape_scalar() {
        if (position_ >= text_.size()) fail();
        const char escape = text_[position_++];
        switch (escape) {
            case '"': return '"';
            case '\'': return '\'';
            case '?': return '?';
            case '\\': return '\\';
            case 'a': return '\a';
            case 'b': return '\b';
            case 'f': return '\f';
            case 'n': return '\n';
            case 'r': return '\r';
            case 't': return '\t';
            case 'v': return '\v';
            case 'x': case 'u': case 'U': {
                const std::size_t digits = escape == 'x' ? 2U : escape == 'u' ? 4U : 8U;
                std::uint32_t value = 0U;
                for (std::size_t index = 0U; index < digits; ++index) {
                    if (position_ >= text_.size()) fail();
                    const int digit = hex_digit(text_[position_++]);
                    if (digit < 0) fail();
                    value = (value << 4U) | static_cast<std::uint32_t>(digit);
                }
                return value;
            }
            default:
                if (!is_octal_digit(escape)) fail();
                {
                    const auto first = static_cast<std::uint32_t>(escape - '0');
                    auto value = first;
                    std::size_t digits = 1U;
                    while (digits < 3U && position_ < text_.size() &&
                           is_octal_digit(text_[position_])) {
                        value = value * 8U + static_cast<std::uint32_t>(text_[position_++] - '0');
                        ++digits;
                    }
                    if (digits == 3U && first > 3U) fail();
                    return value;
                }
        }
    }

    std::uint32_t parse_utf8_scalar() {
        auto remaining = text_.substr(position_);
        const auto previous_size = remaining.size();
        try {
            const auto scalar = utf8::consume_scalar(remaining);
            position_ += previous_size - remaining.size();
            return static_cast<std::uint32_t>(scalar);
        } catch (const std::invalid_argument&) {
            fail();
        }
    }

    std::string parse_quoted_string() {
        require('"');
        std::string result;
        while (position_ < text_.size()) {
            if (text_[position_] == '"') {
                ++position_;
                return result;
            }
            // PhobosのparseEscapeはdcharを返す。stringへの追加時にUTF-8
            // へ符号化してから、wchar/dchar文字列のコード単位幅へ変換する。
            const auto scalar = text_[position_] == '\\'
                ? (++position_, parse_escape_scalar())
                : parse_utf8_scalar();
            try {
                result += xml_encode_unicode_scalar(scalar);
            } catch (const XmlValueError&) {
                fail();
            }
        }
        fail();
    }

    std::uint32_t parse_quoted_character() {
        require('\'');
        if (position_ >= text_.size()) fail();
        const auto scalar = text_[position_] == '\\'
            ? (++position_, parse_escape_scalar())
            : parse_utf8_scalar();
        require('\'');
        if (!utf8::is_scalar(scalar)) fail();
        return scalar;
    }

    std::string parse_character_array_string() {
        require('[');
        skip_space();
        std::string result;
        if (position_ < text_.size() && text_[position_] == ']') {
            ++position_;
            return result;
        }
        for (;;) {
            skip_space();
            const auto scalar = parse_quoted_character();
            try {
                result += xml_encode_unicode_scalar(scalar);
            } catch (const XmlValueError&) {
                fail();
            }
            skip_space();
            if (position_ < text_.size() && text_[position_] == ']') {
                ++position_;
                return result;
            }
            require(',');
            skip_space();
            if (position_ < text_.size() && text_[position_] == ']') {
                ++position_;
                return result;
            }
        }
    }

    std::string parse_string_element() {
        skip_space();
        if (position_ < text_.size() && text_[position_] == '"') {
            return parse_quoted_string();
        }
        if (position_ < text_.size() && text_[position_] == '[') {
            return parse_character_array_string();
        }
        fail();
    }

    template <typename Element>
    Element parse_element(const char extra_terminator = '\0') {
        skip_space();
        using U = std::remove_cvref_t<Element>;
        if constexpr (xml_is_vector_v<U>) {
            using NestedElement = typename xml_vector_traits<U>::element_type;
            static_assert(xml_is_supported_array_element<Element>(),
                          "XML array values must map to a D std.conv array element family");
            if constexpr (xml_is_character_code_unit_v<NestedElement>) {
                const auto text = parse_string_element();
                return xml_character_vector_from_string<U>(text);
            } else {
                return parse_container<U>();
            }
        } else if constexpr (xml_is_static_array_v<U> || xml_is_dictionary_v<U>) {
            static_assert(xml_is_supported_array_element<U>(),
                          "XML container values must map to a D std.conv container element family");
            return parse_container<U>();
        } else if constexpr (std::is_same_v<U, char16_t> ||
                             std::is_same_v<U, char32_t> ||
                             std::is_same_v<U, wchar_t>) {
            const auto scalar = parse_quoted_character();
            return xml_parse_unicode_character<U>(xml_encode_unicode_scalar(scalar));
        } else if constexpr (xml_is_owning_string_v<U>) {
            const auto text = parse_string_element();
            return xml_from_string<U>(text);
        } else {
            static_assert(xml_is_supported_array_element<Element>(),
                          "XML array values must map to a D std.conv array element family");
            const auto begin = position_;
            while (position_ < text_.size() && text_[position_] != ',' &&
                   text_[position_] != ']' &&
                   (extra_terminator == '\0' || text_[position_] != extra_terminator)) {
                ++position_;
            }
            auto token = text_.substr(begin, position_ - begin);
            while (!token.empty() && is_space(token.back())) token.remove_suffix(1U);
            if (token.empty()) fail();
            return xml_from_string<U>(token);
        }
    }

    template <typename Container>
    Container parse_container() {
        using U = std::remove_cvref_t<Container>;
        if constexpr (xml_is_vector_v<U>) {
            return parse_vector<U>();
        } else if constexpr (xml_is_static_array_v<U>) {
            return parse_static_array<U>();
        } else if constexpr (xml_is_dictionary_v<U>) {
            return parse_dictionary<U>();
        } else {
            static_assert(xml_is_container_v<U>,
                          "XML value parser accepts only supported containers");
        }
    }

    template <typename Vector>
    Vector parse_vector() {
        using Element = typename xml_vector_traits<std::remove_cvref_t<Vector>>::element_type;
        static_assert(xml_is_supported_array_element<Element>(),
                      "XML array values must map to a D std.conv array element family");
        require('[');
        skip_space();
        Vector result;
        if (position_ < text_.size() && text_[position_] == ']') {
            ++position_;
            return result;
        }
        for (;;) {
            result.push_back(parse_element<Element>());
            skip_space();
            if (position_ < text_.size() && text_[position_] == ']') {
                ++position_;
                return result;
            }
            require(',');
            skip_space();
            // Phobosの動的配列パーサーは、末尾の区切り文字を一つ許可する。
            if (position_ < text_.size() && text_[position_] == ']') {
                ++position_;
                return result;
            }
        }
    }

    template <typename Array>
    Array parse_static_array() {
        using U = std::remove_cvref_t<Array>;
        using Traits = xml_static_array_traits<U>;
        using Element = typename Traits::element_type;
        constexpr auto expected = Traits::size;
        static_assert(xml_is_supported_array_element<Element>(),
                      "XML static arrays must map to a D std.conv array element family");
        static_assert(!xml_is_character_code_unit_v<Element> ||
                          xml_is_dchar_code_unit_v<Element>,
                      "D std.conv only parses dchar code units in static arrays");
        U result{};
        require('[');
        skip_space();
        if (position_ < text_.size() && text_[position_] == ']') {
            if constexpr (expected == 0U) {
                ++position_;
                return result;
            }
            fail();
        }
        std::size_t count = 0U;
        for (;;) {
            if (count == expected) fail();
            result[count++] = parse_element<Element>();
            skip_space();
            if (position_ < text_.size() && text_[position_] == ']') {
                if (count != expected) fail();
                ++position_;
                return result;
            }
            require(',');
            skip_space();
            // 動的配列と異なり、Phobosは次の要素の解析前に固
            // 定長の上限を確認するため、末尾のカンマも拒否する。
            if (count == expected) fail();
        }
    }

    template <typename DictionaryType>
    DictionaryType parse_dictionary() {
        using U = std::remove_cvref_t<DictionaryType>;
        using Traits = xml_dictionary_traits<U>;
        using Key = typename Traits::key_type;
        using Value = typename Traits::mapped_type;
        static_assert(xml_is_supported_array_element<Key>() &&
                          xml_is_supported_array_element<Value>(),
                      "XML associative arrays must map to supported D std.conv key/value types");
        static_assert((!xml_is_character_code_unit_v<Key> ||
                       xml_is_dchar_code_unit_v<Key>) &&
                          (!xml_is_character_code_unit_v<Value> ||
                           xml_is_dchar_code_unit_v<Value>),
                      "D std.conv only parses dchar code units as associative keys or values");
        U result{};
        require('[');
        skip_space();
        if (position_ < text_.size() && text_[position_] == ']') {
            ++position_;
            return result;
        }
        for (;;) {
            auto key = parse_element<Key>(':');
            skip_space();
            require(':');
            auto value = parse_element<Value>();
            // Dのstd.convは連想配列へ代入するため、同じキーが再登場すると前
            // の値を置き換える。ここでもoperator[]で同じ規則を適用する。
            result[key] = std::move(value);
            skip_space();
            if (position_ < text_.size() && text_[position_] == ']') {
                ++position_;
                return result;
            }
            require(',');
            skip_space();
            // 連想配列の解析では末尾のカンマを特別に許可しない。区
            // 切り文字の次が「]」なら、次のキーの解析が失敗する。
        }
    }

    std::string_view text_;
    std::size_t position_{};
};

template <typename Container>
Container xml_parse_container(std::string_view text) {
    static_assert(xml_is_container_v<Container>);
    if constexpr (xml_is_vector_v<Container>) {
        using Element = typename xml_vector_traits<std::remove_cvref_t<Container>>::element_type;
        static_assert(xml_is_supported_array_element<Element>(),
                      "XML arrays must map to a D std.conv array element family");
        if constexpr (xml_is_character_code_unit_v<Element>) {
            // Dのchar[]/wchar[]/dchar[]は文字列であり、角括弧付きで直列化す
            // る配列ではない。C++のコード単位ベクターの拡張も、同じ文字列として扱う経路に保つ。
            return xml_character_vector_from_string<Container>(text);
        } else {
            return XmlArrayValueParser(text).template parse<Container>();
        }
    } else {
        return XmlArrayValueParser(text).template parse<Container>();
    }
}

template <typename T>
T xml_from_string(std::string_view text) {
    using U = std::remove_cvref_t<T>;
    if constexpr (xml_is_container_v<U>) {
        return xml_parse_container<U>(text);
    } else if constexpr (std::is_same_v<U, std::string>) {
        return std::string(text);
#ifdef __cpp_char8_t
    } else if constexpr (std::is_same_v<U, std::u8string>) {
        if (text.empty()) return {};
        U result;
        result.reserve(text.size());
        for (const unsigned char byte : text) {
            result.push_back(static_cast<char8_t>(byte));
        }
        return result;
#endif
    } else if constexpr (std::is_same_v<U, std::u16string> ||
                         std::is_same_v<U, std::u32string> ||
                         std::is_same_v<U, std::wstring>) {
        // Dのto!wstring/to!dstringは、補助平面のスカラー
        // 値や埋め込みNULを含むUTF-8文字列全体を変換する。所有するワイ
        // ド文字列へのこの公開変換は、ナロー文字ストリームでは実装できない。
        using Character = typename U::value_type;
        static_assert(sizeof(Character) == 2U || sizeof(Character) == 4U);
        U result;
        auto remaining = text;
        try {
            while (!remaining.empty()) {
                auto scalar = static_cast<std::uint32_t>(utf8::consume_scalar(remaining));
                if constexpr (sizeof(Character) == 2U) {
                    if (scalar > 0xFFFFU) {
                        scalar -= 0x10000U;
                        result.push_back(static_cast<Character>(0xD800U + (scalar >> 10U)));
                        result.push_back(static_cast<Character>(0xDC00U + (scalar & 0x3FFU)));
                        continue;
                    }
                }
                result.push_back(static_cast<Character>(scalar));
            }
        } catch (const std::invalid_argument&) {
            throw XmlValueError("Invalid XML Unicode string: " + std::string(text));
        }
        return result;
    } else if constexpr (std::is_same_v<U, std::string_view>) {
        return text;
    } else if constexpr (std::is_same_v<U, bool>) {
        std::string lower;
        lower.reserve(text.size());
        for (const char value : text) {
            lower.push_back(static_cast<char>(value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value));
        }
        // Dのstd.conv.to!boolはtrue/falseという語を受け付け、数値の代替表記は受け付けない。
        if (lower == "true") return true;
        if (lower == "false") return false;
        throw XmlValueError("Invalid XML boolean: " + std::string(text));
    } else if constexpr (std::is_same_v<U, char>) {
        // Dのto!charは、そのバイトだけでは完全なUTF-8列でなくても、ちょう
        // ど一バイトを読み取る。複数バイトのUTF-8文字は一つのcharではない。
        if (text.size() != 1U) {
            throw XmlValueError("Invalid XML char: " + std::string(text));
        }
        return text.front();
#ifdef __cpp_char8_t
    } else if constexpr (std::is_same_v<U, char8_t>) {
        if (text.size() != 1U) {
            throw XmlValueError("Invalid XML char: " + std::string(text));
        }
        return static_cast<char8_t>(static_cast<unsigned char>(text.front()));
#endif
    } else if constexpr (std::is_same_v<U, char16_t> || std::is_same_v<U, char32_t> ||
                         std::is_same_v<U, wchar_t>) {
        return xml_parse_unicode_character<U>(text);
    } else if constexpr (std::is_enum_v<U>) {
        if constexpr (requires { XmlEnumTraits<U>::from_string(text); }) {
            return XmlEnumTraits<U>::from_string(text);
        } else {
            // この代替処理は、xml_to_stringで説明した数値変
            // 換の拡張に対応する。登録済みのD列挙はシンボル名を使う。
            using Underlying = std::underlying_type_t<U>;
            return static_cast<U>(xml_from_string<Underlying>(text));
        }
    } else if constexpr (std::is_integral_v<U>) {
        try {
            return scalar_conversion::integral_from_string<U>(text);
        } catch (const std::invalid_argument&) {
            throw XmlValueError("Invalid XML integer: " + std::string(text));
        }
    } else if constexpr (is_real_number_v<U>) {
        return xml_parse_floating<U>(text);
    } else {
        std::istringstream stream{std::string(text)};
        stream >> std::ws;
        T result{};
        stream >> result;
        stream >> std::ws;
        if (!stream || !stream.eof()) {
            throw XmlValueError("Invalid XML value: " + std::string(text));
        }
        return result;
    }
}

}  // namespace detail

class XmlWriter;
class XmlDocument;
class XmlParser;
class XmlDomParser;

class XmlNode {
public:
    struct Impl;

private:
    std::shared_ptr<Impl> impl_;
    bool root_{false};
    bool dxml_compat_{false};

    explicit XmlNode(std::shared_ptr<Impl> impl, bool root, bool dxml_compat = false);

    friend class XmlWriter;
    friend class XmlDocument;
    friend class XmlParser;
    friend class XmlDomParser;

public:
    using Handler = std::function<void(XmlNode&)>;

    XmlNode() = default;
    ~XmlNode();
    XmlNode(const XmlNode&) = default;
    XmlNode(XmlNode&&) noexcept = default;
    XmlNode& operator=(const XmlNode&) = default;
    XmlNode& operator=(XmlNode&&) noexcept = default;

    static XmlNode create(std::string_view root_name, std::string_view value = {});
    static XmlNode parse(std::string_view xml, bool allow_single_element = false);
    static XmlNode parse(std::string_view xml, const XmlParseOptions& options);

    [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(impl_); }
    explicit operator bool() const noexcept { return valid(); }
    [[nodiscard]] bool is_root() const noexcept { return valid() && root_; }
    [[nodiscard]] bool isRoot() const noexcept { return is_root(); }
    [[nodiscard]] XmlNodeType type() const noexcept;
    [[nodiscard]] bool is_element() const noexcept { return type() == XmlNodeType::Element; }

    [[nodiscard]] std::string name() const;
    [[nodiscard]] std::string value() const;
    void value(std::string_view text);
    void set_value(std::string_view text) { value(text); }
    [[nodiscard]] std::string text_content() const;
    [[nodiscard]] std::string data() const;

    // Dはここで所有された値を返す。value()は一時的なstd::string
    // を返すため、参照や借用ビューではこのAPIを安全に実装できない。
    template <typename T>
        requires (!std::is_reference_v<T> && !std::is_pointer_v<T> &&
                  !detail::is_xml_borrowed_string_view_v<T>)
    [[nodiscard]] T value_to() const {
        return detail::xml_from_string<T>(value());
    }

    template <typename T>
        requires (!std::is_reference_v<T> && !std::is_pointer_v<T> &&
                  !detail::is_xml_borrowed_string_view_v<T>)
    [[nodiscard]] T valueTo() const {
        return value_to<T>();
    }

    XmlNode new_element(std::string_view child_name);

    template <typename T>
    XmlNode new_element(std::string_view child_name, const T& child_value) {
        return new_element_with_value(child_name, detail::xml_to_string(child_value));
    }

    XmlNode newElement(std::string_view child_name) { return new_element(child_name); }

    template <typename T>
    XmlNode newElement(std::string_view child_name, const T& child_value) {
        return new_element(child_name, child_value);
    }

    void new_attr(std::string_view attribute_name, std::string_view attribute_value);

    template <typename T>
    void new_attr(std::string_view attribute_name, const T& attribute_value) {
        const auto text = detail::xml_to_string(attribute_value);
        new_attr(attribute_name, std::string_view{text});
    }

    void newAttr(std::string_view attribute_name, std::string_view attribute_value) {
        new_attr(attribute_name, attribute_value);
    }

    template <typename T>
    void newAttr(std::string_view attribute_name, const T& attribute_value) {
        new_attr(attribute_name, attribute_value);
    }

    [[nodiscard]] std::vector<XmlAttribute> attributes() const;
    [[nodiscard]] std::optional<std::string> attribute(std::string_view attribute_name) const;
    [[nodiscard]] bool has_attr(std::string_view attribute_name) const;
    [[nodiscard]] bool hasAttr(std::string_view attribute_name) const { return has_attr(attribute_name); }

    std::string attr(std::string_view attribute_name, bool nothing_is_error,
                     std::string_view default_value = {}) const;

    // attribute()が返す所有文字列のoptionalはこの関数の終了
    // 時に破棄されるため、型付き変換の結果は自身で領域を所有する必要がある。
    template <typename T = std::string>
        requires (!std::is_reference_v<T> && !std::is_pointer_v<T> &&
                  !detail::is_xml_borrowed_string_view_v<T> &&
                  (!std::is_convertible_v<T, std::string_view> ||
                   std::is_same_v<std::remove_cvref_t<T>, std::string>))
    [[nodiscard]] T attr(std::string_view attribute_name, bool nothing_is_error,
                         T default_value = T{}) const {
        const auto found = attribute(attribute_name);
        if (!found.has_value()) {
            if (nothing_is_error) {
                throw XmlValueError("XML attribute is not found: " + std::string(attribute_name));
            }
            return default_value;
        }
        return detail::xml_from_string<T>(*found);
    }

    // Dの遅延評価される既定式は、C++では呼び出し可能なオブジェクトとして渡
    // す。既存のattr()は即時評価とソース互換性を維持する。attribute()
    // は一時文字列を返すため、借用ビューではなく所有する値を返す。
    template <typename T = std::string, typename F>
        requires (std::is_invocable_r_v<T, F&> && !std::is_reference_v<T> &&
                  !std::is_pointer_v<T> &&
                  !detail::is_xml_borrowed_string_view_v<T>)
    [[nodiscard]] T attrLazy(std::string_view attribute_name, bool nothing_is_error,
                             F&& default_value) const {
        const auto found = attribute(attribute_name);
        if (found.has_value()) return detail::xml_from_string<T>(*found);
        if (nothing_is_error) {
            throw XmlValueError("XML attribute is not found: " + std::string(attribute_name));
        }
        return std::invoke(default_value);
    }

    void remove_attr(std::string_view attribute_name);
    void remove_attr_recursive(std::string_view attribute_name);
    void removeAttrRecursive(std::string_view attribute_name) { remove_attr_recursive(attribute_name); }

    XmlNode child(std::string_view child_name, bool nothing_is_error = false) const;

    std::string child_text(std::string_view child_name, bool nothing_is_error,
                           std::string_view default_value = {}) const;

    // child()とvalue()も所有する一時値を生成する
    // ため、借用した結果ではDの値を返す変換を安全に表せない。
    template <typename T = std::string>
        requires (!std::is_reference_v<T> && !std::is_pointer_v<T> &&
                  !detail::is_xml_borrowed_string_view_v<T> &&
                  (!std::is_convertible_v<T, std::string_view> ||
                   std::is_same_v<std::remove_cvref_t<T>, std::string>))
    [[nodiscard]] T child_text(std::string_view child_name, bool nothing_is_error,
                               T default_value = T{}) const {
        const auto found = child(child_name, nothing_is_error);
        if (!found.valid()) return default_value;
        return detail::xml_from_string<T>(found.value());
    }

    [[nodiscard]] std::string childText(std::string_view child_name, bool nothing_is_error,
                                        std::string_view default_value = {}) const {
        return child_text(child_name, nothing_is_error, default_value);
    }

    template <typename T = std::string>
        requires (!std::is_reference_v<T> && !std::is_pointer_v<T> &&
                  !detail::is_xml_borrowed_string_view_v<T> &&
                  (!std::is_convertible_v<T, std::string_view> ||
                   std::is_same_v<std::remove_cvref_t<T>, std::string>))
    [[nodiscard]] T childText(std::string_view child_name, bool nothing_is_error,
                              T default_value = T{}) const {
        return child_text<T>(child_name, nothing_is_error, default_value);
    }

    [[nodiscard]] std::vector<XmlNode> children() const;
    [[nodiscard]] std::vector<XmlNode> element_children() const;
    [[nodiscard]] std::vector<XmlNode> find_all(std::string_view child_name) const;

    // ハンドラーのキーは要素名。空文字列または「*」はワイルド
    // カードで、dxmlの連想配列のnullキーに相当する。
    std::map<std::string, Handler> onTag;
    // 新しいC++の呼び出し側向けのsnake_case名。parse()は両方の
    // マップを受け付け、set_handler()は利便性のため両方に設定する。
    std::map<std::string, Handler> on_tag;

    // 明示的なD互換の登録窓口。設定済みの登録はXmlNodeのコピー間で共
    // 有し、正常な配送後はこのハンドルだけを切り離す。既存のonTag/on_tag
    // とparse()は、取得時の状態を使う従来の利便性を維持する。
    void setLiveHandler(std::string name, Handler handler);
    void clearLiveHandlers() noexcept { live_handlers_.reset(); }
    void parseLive();

    void parse();
    // D形式のonTagマップを文書順に配送する。各子を現在のマップで解決
    // し、先行するコールバックから後続のハンドラーを登録できるようにする
    // 。on_tagはC++用の別名であり、ここでは使わない。onTag
    // を直接変更した際に、同期されていない古い項目が残るのを避けるため。
    void parseDCompatible();
    void parse_children() { parse(); }
    void set_handler(std::string name, Handler handler);
    void setHandler(std::string name, Handler handler) { set_handler(std::move(name), std::move(handler)); }
    void traverse(const Handler& handler);

    XmlNode append(XmlNode child);
    XmlNode append_text(std::string_view text);
    XmlNode append_cdata(std::string_view text);
    XmlNode append_comment(std::string_view text);
    XmlNode append_processing_instruction(std::string_view target, std::string_view text = {});
    void clear_children();

    [[nodiscard]] XmlNode clone(bool as_root = false) const;

    [[nodiscard]] std::string serialize(const XmlWriterOptions& options = {}) const;
    [[nodiscard]] std::string text() const {
        if (!is_root()) throw XmlValueError("Node is not Root: " + name());
        return serialize();
    }

private:
    std::shared_ptr<std::map<std::string, Handler>> live_handlers_;
    XmlNode new_element_with_value(std::string_view child_name, std::string_view child_value);
};

class XmlDocument {
public:
    XmlDocument() = default;
    explicit XmlDocument(XmlNode root);

    static XmlDocument create(std::string_view root_name, std::string_view value = {});
    static XmlDocument parse(std::string_view xml, const XmlParseOptions& options = {});

    [[nodiscard]] bool valid() const noexcept { return root_.valid(); }
    [[nodiscard]] XmlNode root() const { return root_; }
    void set_root(XmlNode root);
    [[nodiscard]] std::string serialize(const XmlWriterOptions& options = {}) const;
    [[nodiscard]] std::string text() const { return serialize(); }

private:
    XmlNode root_;
};

class XmlParser {
public:
    static XmlDocument parse(std::string_view xml, const XmlParseOptions& options = {}) {
        return XmlDocument::parse(xml, options);
    }

    static XmlNode parse_node(std::string_view xml, const XmlParseOptions& options = {}) {
        return XmlDocument::parse(xml, options).root();
    }
};

class XmlWriter {
public:
    explicit XmlWriter(XmlWriterOptions options = {}) : options_(std::move(options)) {}

    [[nodiscard]] std::string write(const XmlNode& node) const;
    [[nodiscard]] std::string operator()(const XmlNode& node) const { return write(node); }
    [[nodiscard]] const XmlWriterOptions& options() const noexcept { return options_; }

    static std::string write(const XmlNode& node, const XmlWriterOptions& options);
    static std::string serialize(const XmlNode& node, const XmlWriterOptions& options = {}) {
        return write(node, options);
    }

    static std::string escape_text(std::string_view text);
    static std::string escape_attribute(std::string_view text);

private:
    XmlWriterOptions options_;
};

// 元のDモジュールと、初期のC++移植層で使われる名前。
using XNode = XmlNode;
using Parser = XmlParser;
using Writer = XmlWriter;

}  // namespace cwxeditor::lib

// 入れ子の名前空間で呼び出し箇所を読みやすくしつつ、公開部品を生成され
// たcwxeditor::cwx::xmlの宣言一覧に依存させない。
namespace cwxeditor::lib::xml {
using ::cwxeditor::lib::Parser;
using ::cwxeditor::lib::Writer;
using ::cwxeditor::lib::XNode;
using ::cwxeditor::lib::XmlAttribute;
using ::cwxeditor::lib::XmlDocument;
using ::cwxeditor::lib::XmlNode;
using ::cwxeditor::lib::XmlNodeType;
using ::cwxeditor::lib::XmlParseError;
using ::cwxeditor::lib::XmlParseOptions;
using ::cwxeditor::lib::XmlParser;
using ::cwxeditor::lib::XmlValueError;
using ::cwxeditor::lib::XmlWriter;
using ::cwxeditor::lib::XmlWriterOptions;
}  // namespace cwxeditor::lib::xml
