#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/core/cwx/msgutils.hpp"

#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/unicode_grapheme_data.hpp"
// 承認済み仕様差: Unicode規則の世代差は、現行D2.112と同じ分割を維持する。
// 家族の絵文字は1文字として扱い、国旗の列は2個の指示記号ごとに分ける。
#include "cwxeditor_src/lib/d_regex.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace cwxeditor::cwx::msgutils {
namespace {

using CP = DChar;
using U32 = DString;

[[nodiscard]] bool is_continuation(unsigned char byte) noexcept
{
    return (byte & 0xC0u) == 0x80u;
}

[[nodiscard]] std::vector<CP> decode_utf8(std::string_view text)
{
    std::vector<CP> result;
    result.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        CP value = 0;
        std::size_t length = 0;
        if (first <= 0x7Fu) {
            value = first;
            length = 1;
        } else if (first >= 0xC2u && first <= 0xDFu) {
            value = static_cast<CP>(first & 0x1Fu);
            length = 2;
        } else if (first >= 0xE0u && first <= 0xEFu) {
            value = static_cast<CP>(first & 0x0Fu);
            length = 3;
        } else if (first >= 0xF0u && first <= 0xF4u) {
            value = static_cast<CP>(first & 0x07u);
            length = 4;
        } else {
            throw std::invalid_argument("invalid UTF-8 leading byte at offset " +
                                        std::to_string(index));
        }
        if (index + length > text.size()) {
            throw std::invalid_argument("truncated UTF-8 sequence at offset " +
                                        std::to_string(index));
        }
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto byte = static_cast<unsigned char>(text[index + offset]);
            if (!is_continuation(byte)) {
                throw std::invalid_argument("invalid UTF-8 continuation byte at offset " +
                                            std::to_string(index + offset));
            }
            value = (value << 6) | static_cast<CP>(byte & 0x3Fu);
        }
        if ((length == 3 && value < 0x800u) ||
            (length == 4 && value < 0x10000u) ||
            (value >= 0xD800u && value <= 0xDFFFu) || value > 0x10FFFFu) {
            throw std::invalid_argument("invalid UTF-8 scalar at offset " +
                                        std::to_string(index));
        }
        result.push_back(value);
        index += length;
    }
    return result;
}

[[nodiscard]] U32 decode_to_u32(std::string_view text)
{
    const auto codepoints = decode_utf8(text);
    return U32(codepoints.begin(), codepoints.end());
}

[[nodiscard]] std::vector<CP> decode_utf16(std::u16string_view text)
{
    // DはbyGraphemeの前にwchar文字列を自動復号する。そのデコ
    // ーダーへ入る前に、不正なサロゲートの失敗をネイティブの例外へ変換する。
    std::vector<CP> result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        CP value = text[index];
        if (value >= 0xD800u && value <= 0xDBFFu) {
            if (index + 1 == text.size() || text[index + 1] < 0xDC00u || text[index + 1] > 0xDFFFu)
                throw std::invalid_argument("invalid UTF-16 surrogate pair");
            value = 0x10000u + ((value - 0xD800u) << 10) + (text[++index] - 0xDC00u);
        } else if (value >= 0xDC00u && value <= 0xDFFFu) {
            throw std::invalid_argument("unpaired UTF-16 low surrogate");
        }
        result.push_back(value);
    }
    return result;
}

void append_utf8(std::string& result, CP value)
{
    // DのdcharからUTF-8への変換は、文字列のコールバックを呼ぶ前に
    // サロゲートのコードポイントを拒否する。不正なバイト列を後段へ渡さない。
    if (value >= 0xD800u && value <= 0xDFFFu) {
        throw std::invalid_argument("invalid Unicode scalar");
    }
    if (value <= 0x7Fu) {
        result.push_back(static_cast<char>(value));
    } else if (value <= 0x7FFu) {
        result.push_back(static_cast<char>(0xC0u | (value >> 6)));
        result.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
    } else if (value <= 0xFFFFu) {
        result.push_back(static_cast<char>(0xE0u | (value >> 12)));
        result.push_back(static_cast<char>(0x80u | ((value >> 6) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
    } else if (value <= 0x10FFFFu) {
        result.push_back(static_cast<char>(0xF0u | (value >> 18)));
        result.push_back(static_cast<char>(0x80u | ((value >> 12) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | ((value >> 6) & 0x3Fu)));
        result.push_back(static_cast<char>(0x80u | (value & 0x3Fu)));
    } else {
        throw std::invalid_argument("invalid Unicode scalar");
    }
}

[[nodiscard]] std::string encode_utf8(std::u32string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const auto value : text) append_utf8(result, value);
    return result;
}

[[nodiscard]] bool is_whitespace(CP value) noexcept
{
    return enumutils::isWhiteSpace(value);
}

[[nodiscard]] std::string_view native_base_name(std::string_view path) {
    return utils::pathBaseName(path);
}

[[nodiscard]] U32 basename_u32(std::string_view path)
{
    return decode_to_u32(enumutils::lowerCase(native_base_name(path)));
}

[[nodiscard]] bool starts_with(std::u32string_view value, std::u32string_view prefix)
{
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] bool is_ascii(CP value) noexcept { return value <= 0x7Fu; }

[[nodiscard]] bool is_regional_indicator(CP value) noexcept {
    return value >= 0x1F1E6u && value <= 0x1F1FFu;
}

[[nodiscard]] constexpr bool is_hangul_l(CP value) noexcept {
    return (value >= 0x1100u && value < 0x1160u) ||
           (value >= 0xA960u && value < 0xA97Du);
}
[[nodiscard]] constexpr bool is_hangul_v(CP value) noexcept {
    return (value >= 0x1160u && value < 0x11A8u) ||
           (value >= 0xD7B0u && value < 0xD7C7u);
}
[[nodiscard]] constexpr bool is_hangul_t(CP value) noexcept {
    return (value >= 0x11A8u && value < 0x1200u) ||
           (value >= 0xD7CBu && value < 0xD7FCu);
}

enum class GraphemeState { Start, CR, RI, L, V, LVT, Emoji, EmojiZWJ, Prepend, End };
enum class GraphemeAction { GoOn, Redo, Include, Exclude };

// PhobosのgraphemeTransformsを移植し、状態変更後に同じ
// スカラー値を再処理する動作も含める。属性への所属は固定したboolのトライ木
// から取得し、一般カテゴリによる近似やホストの正規表現エンジンには頼らない。
[[nodiscard]] GraphemeAction grapheme_transform(
    GraphemeState& state, CP value, std::uint8_t properties)
{
    const auto has = [properties](grapheme_data::Property property) {
        return (properties & static_cast<std::uint8_t>(property)) != 0;
    };
    switch (state) {
    case GraphemeState::Start:
        if (has(grapheme_data::Control) || value == U'\n') return GraphemeAction::Include;
        state = value == U'\r' ? GraphemeState::CR
            : is_regional_indicator(value) ? GraphemeState::RI
            : is_hangul_l(value) ? GraphemeState::L
            : has(grapheme_data::HangulLV) || is_hangul_v(value) ? GraphemeState::V
            : has(grapheme_data::HangulLVT) || is_hangul_t(value) ? GraphemeState::LVT
            : has(grapheme_data::Prepend) ? GraphemeState::Prepend
            : has(grapheme_data::ExtendedPictographic) ? GraphemeState::Emoji
            : GraphemeState::End;
        return GraphemeAction::GoOn;
    case GraphemeState::CR:
        return value == U'\n' ? GraphemeAction::Include : GraphemeAction::Exclude;
    case GraphemeState::RI:
        state = GraphemeState::End;
        return is_regional_indicator(value) ? GraphemeAction::GoOn : GraphemeAction::Redo;
    case GraphemeState::L:
        if (is_hangul_l(value)) return GraphemeAction::GoOn;
        if (is_hangul_v(value) || has(grapheme_data::HangulLV)) {
            state = GraphemeState::V;
            return GraphemeAction::GoOn;
        }
        if (has(grapheme_data::HangulLVT)) {
            state = GraphemeState::LVT;
            return GraphemeAction::GoOn;
        }
        state = GraphemeState::End;
        return GraphemeAction::Redo;
    case GraphemeState::V:
        if (is_hangul_v(value)) return GraphemeAction::GoOn;
        if (is_hangul_t(value)) {
            state = GraphemeState::LVT;
            return GraphemeAction::GoOn;
        }
        state = GraphemeState::End;
        return GraphemeAction::Redo;
    case GraphemeState::LVT:
        if (is_hangul_t(value)) return GraphemeAction::GoOn;
        state = GraphemeState::End;
        return GraphemeAction::Redo;
    case GraphemeState::Emoji:
        if (has(grapheme_data::Extend)) return GraphemeAction::GoOn;
        if (value == 0x200Du) {
            state = GraphemeState::EmojiZWJ;
            return GraphemeAction::GoOn;
        }
        state = GraphemeState::End;
        return GraphemeAction::Redo;
    case GraphemeState::EmojiZWJ:
        state = GraphemeState::Emoji;
        return has(grapheme_data::ExtendedPictographic) ? GraphemeAction::GoOn : GraphemeAction::Redo;
    case GraphemeState::Prepend:
        if (has(grapheme_data::Control) || value == U'\r' || value == U'\n') return GraphemeAction::Exclude;
        state = GraphemeState::Start;
        return GraphemeAction::Redo;
    case GraphemeState::End:
        return !has(grapheme_data::Extend) && !has(grapheme_data::SpacingMark) && value != 0x200Du
            ? GraphemeAction::Exclude : GraphemeAction::GoOn;
    }
    // Dのfinal switchも未知の状態を受け付けない。
    throw std::logic_error("invalid grapheme decoder state");
}

[[nodiscard]] std::vector<Grapheme> graphemes_from_codepoints(const std::vector<CP>& cps)
{
    std::vector<Grapheme> result;
    std::size_t begin = 0;
    while (begin < cps.size()) {
        auto state = GraphemeState::Start;
        auto end = begin;
        while (end < cps.size()) {
            const auto value = cps[end];
            // 未検査の添字アクセスをせず、Dのトライ木の範囲アサートに合わせる。
            if (value > 0x10FFFFu) throw std::out_of_range("grapheme scalar outside Unicode domain");
            const auto properties = grapheme_data::properties(value);
            GraphemeAction action;
            do {
                action = grapheme_transform(state, value, properties);
            } while (action == GraphemeAction::Redo);
            if (action == GraphemeAction::Exclude) break;
            ++end;
            if (action == GraphemeAction::Include) break;
        }
        Grapheme cluster(cps.begin() + static_cast<std::ptrdiff_t>(begin),
                         cps.begin() + static_cast<std::ptrdiff_t>(end));
        if (cluster.size() > 1 &&
            (cluster[1] == 0xFF9Eu || cluster[1] == 0xFF9Fu)) {
            result.emplace_back(1, cluster.front());
            // Dは後続部分にbyGraphemeを再帰的に実行する
            // 。状態は新しく開始するため、絵文字やZWJ列内の半角
            // 記号では、古いクラスタを切り出すだけでは足りない。
            auto suffix = graphemes_from_codepoints(std::vector<CP>(cluster.begin() + 1, cluster.end()));
            for (auto& item : suffix) result.push_back(std::move(item));
        } else {
            result.push_back(std::move(cluster));
        }
        begin = end;
    }
    return result;
}

[[nodiscard]] std::vector<U32> sorted_chars(std::u32string_view chars)
{
    std::vector<U32> result;
    result.reserve(chars.size());
    for (const auto c : chars) result.emplace_back(1, c);
    std::sort(result.begin(), result.end());
    return result;
}

[[nodiscard]] U32 find_base_name(std::string_view path)
{
    return enumutils::toUtf32Replacing(native_base_name(path));
}

}  // namespace

namespace {

// Dはデリゲートハンドルをコピーし、再帰呼び出し間で状態を共有
// する。ネイティブのコールバック値も入口で一度だけ保持し、展
// 開中はその状態を借用して、状態を持つ関数対象を複製しない。
struct FormatInputs {
    VarGetter getFlag;
    VarGetter getStep;
    VarGetter getVariant;
    NameGetter getName;
    VersionPredicate isTargetVersion;
    std::string prefixSystemVarName;
    MaterialPredicate hasMaterial;
};

[[nodiscard]] U32 format_msg_impl(
    std::string_view text,
    const FormatInputs& inputs,
    FontMap& fonts,
    ColorMap& colors,
    bool full,
    std::size_t startIndex,
    std::size_t stack)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d89(__FILE__, __LINE__);
    const auto& getFlag = inputs.getFlag;
    const auto& getStep = inputs.getStep;
    const auto& getVariant = inputs.getVariant;
    const auto& getName = inputs.getName;
    const auto& isTargetVersion = inputs.isTargetVersion;
    const auto& prefixSystemVarName = inputs.prefixSystemVarName;
    const auto& hasMaterial = inputs.hasMaterial;
    const auto dtext = toGraphemeArray(text);
    U32 result;

    // Dはコールバックをまたいでデリゲート値と不変の接頭辞を保持する。ネイティ
    // ブのこれらの入力も所有し、再入した処理で検索先が変わらないようにする。

    for (std::size_t i = 0; i < dtext.size(); ++i) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d92(__FILE__, __LINE__);
        const auto& grapheme = dtext[i];
        if (grapheme.size() != 1) {
            result += grapheme;
            continue;
        }
        const CP c = grapheme.empty() ? 0 : grapheme.front();

        auto variable = [&](const VarGetter& callback, const U32& delimiter) {
            ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d94(__FILE__, __LINE__);
            std::ptrdiff_t next = -1;
            for (std::size_t probe = i + 1; probe < dtext.size(); ++probe) {
                if (dtext[probe] == delimiter) {
                    next = static_cast<std::ptrdiff_t>(probe - (i + 1));
                    break;
                }
            }
            if (next < 0) return false;

            const auto end = i + 1u + static_cast<std::size_t>(next);
            U32 field;
            for (std::size_t probe = i + 1; probe < end; ++probe) field += dtext[probe];
            for (std::size_t probe = i + 1; probe < end; ++probe) {
                if (dtext[probe] == U"\n") return false;
            }

            const auto field_utf8 = encode_utf8(field);
            // Dのデリゲート呼び出しと同様に、取得関数がなければここで
            // 失敗する。全変数が存在しないものとして黙って扱わない。
            const auto value = callback(field_utf8);
            if (!value.exists) {
                ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d101(__FILE__, __LINE__);
                const bool target_v2 = !isTargetVersion || isTargetVersion("2");
                if (target_v2 && c == U'$' && !prefixSystemVarName.empty() &&
                    field_utf8.starts_with(prefixSystemVarName)) {
                    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d102(__FILE__, __LINE__);
                    // CardWirth 1.60は、??で始まる未知のステップを削除する。
                    i = end;
                    return true;
                }
                if (!full) {
                    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d108(__FILE__, __LINE__);
                    if (delimiter == U"$" || delimiter == U"%") {
                        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d109(__FILE__, __LINE__);
                        // CardWirth 1.50の選択肢表示は
                        // 、変数が不明な場合に開始記号を削除する。
                        result += field;
                        result += delimiter;
                    } else {
                        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d112(__FILE__, __LINE__);
                        result += delimiter;
                        result += field;
                        result += delimiter;
                    }
                    i = end;
                    return true;
                }
                return false;
            }

            i = end;
            std::string replacement = value.value;
            if (value.expandSPChars && stack == 0) {
                ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d123(__FILE__, __LINE__);
                replacement = encode_utf8(format_msg_impl(
                    replacement, inputs,
                    fonts, colors, full, result.size() + startIndex, stack + 1));
            }
            result += decode_to_u32(replacement);
            return true;
        };

        switch (c) {
        case U'#': {
            if (i + 1u == dtext.size() || dtext[i + 1u] == U"\n") {
                result += grapheme;
                break;
            }
            const auto& next = dtext[i + 1u];
            // Dは書記素全体を文字列として大文字化する。一つのスカラー値か
            // ら複数のコードポイントへ展開するUnicodeの対応も含む。
            const auto nc = decode_to_u32(enumutils::upperCase(encode_utf8(next)));
            if (full) {
                ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d135(__FILE__, __LINE__);
                const auto path = next.size() == 1 ? encodeFontPath(next.front(), ".bmp") : "";
                if (!path.empty()) {
                    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d137(__FILE__, __LINE__);
                    if (hasMaterial && hasMaterial(path)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d138(__FILE__, __LINE__);
                        fonts[result.size() + startIndex] = path;
                    } else {
                        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d140(__FILE__, __LINE__);
                        if (nc == U"M" || nc == U"R" || nc == U"U" || nc == U"C" ||
                            nc == U"I" || nc == U"T" || nc == U"Y") {
                            result += decode_to_u32(getName(static_cast<char>(nc.front())));
                            ++i;
                            continue;
                        } else {
                            if (!hasMaterial) { ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d147(__FILE__, __LINE__); fonts[result.size() + startIndex] = path; }
                        }
                    }
                }
            } else {
                ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d154(__FILE__, __LINE__);
                const bool special = nc == U"C" ? isTargetVersion && isTargetVersion("")
                    : nc == U"M" || nc == U"R" || nc == U"U" || nc == U"T" || nc == U"Y";
                if (special) {
                    result += decode_to_u32(getName(static_cast<char>(nc.front())));
                    ++i;
                    continue;
                }
            }
            result += grapheme;
            break;
        }
        case U'%':
            if (!variable(getFlag, U"%")) result += grapheme;
            break;
        case U'$':
            if (!variable(getStep, U"$")) result += grapheme;
            break;
        case U'@':
            if (!variable(getVariant, U"@")) result += grapheme;
            break;
        case U'&':
            if (full && i + 1u < dtext.size() && dtext[i + 1u] != U"\n" &&
                dtext[i + 1u].size() == 1 && is_ascii(dtext[i + 1u].front())) {
                ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d183(__FILE__, __LINE__);
                colors[result.size() + startIndex] = static_cast<char>(
                    enumutils::upperScalar(dtext[i + 1u].front()));
            }
            result += grapheme;
            break;
        default:
            result += grapheme;
            break;
        }
    }
    return result;
}

[[nodiscard]] std::string repl_text_flag_step(
    std::string_view text,
    std::string_view oldValue,
    std::string_view newValue,
    CP primary,
    CP secondary,
    CP tertiary,
    CP target)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d333(__FILE__, __LINE__);
    U32 dtext = enumutils::toUtf32Replacing(text);
    const U32 old_u32 = enumutils::toUtf32Replacing(oldValue);
    const U32 new_u32 = enumutils::toUtf32Replacing(newValue);
    U32 buffer;

    for (std::size_t i = 0; i < dtext.size(); ++i) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d338(__FILE__, __LINE__);
        const CP c = dtext[i];
        if (c == U'#') {
            buffer.push_back(c);
            if (i + 1u < dtext.size()) { ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d343(__FILE__, __LINE__); buffer.push_back(dtext[++i]); }
        } else if (c == primary) {
            const auto next_it = std::find(dtext.begin() + static_cast<std::ptrdiff_t>(i + 1u),
                                           dtext.end(), primary);
            if (next_it == dtext.end()) {
                buffer.push_back(c);
            } else {
                const auto next = static_cast<std::size_t>(next_it - dtext.begin());
                if (U32(dtext.begin() + static_cast<std::ptrdiff_t>(i + 1u),
                        dtext.begin() + static_cast<std::ptrdiff_t>(next)) == old_u32) {
                    buffer.push_back(target);
                    buffer += new_u32;
                    buffer.push_back(target);
                } else {
                    buffer.append(dtext.begin() + static_cast<std::ptrdiff_t>(i),
                                  dtext.begin() + static_cast<std::ptrdiff_t>(next + 1u));
                }
                if (next < dtext.size()) {
                    dtext = dtext.substr(next);
                    i = 0;
                }
            }
        } else if (c == secondary || c == tertiary) {
            const auto next_it = std::find(dtext.begin() + static_cast<std::ptrdiff_t>(i + 1u),
                                           dtext.end(), c);
            if (next_it == dtext.end()) {
                buffer.push_back(c);
            } else {
                const auto next = static_cast<std::size_t>(next_it - dtext.begin());
                buffer.push_back(c);
                buffer.append(dtext.begin() + static_cast<std::ptrdiff_t>(i + 1u),
                              dtext.begin() + static_cast<std::ptrdiff_t>(next + 1u));
                if (next < dtext.size()) {
                    dtext = dtext.substr(next);
                    i = 0;
                }
            }
        } else {
            buffer.push_back(c);
        }
    }
    return encode_utf8(buffer);
}

}  // namespace

CP decodeFontPath(std::string_view path)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d20(__FILE__, __LINE__);
    if (!isSPFontFile(path)) {
        throw std::invalid_argument("not a CardWirth font path");
    }
    // Dはbasenameや小文字化した表記ではなく、元の文字列をバ
    // イト位置5で切り出す。ディレクトリ入力での検証失敗を維持する。
    auto name = path.substr(5);
    name = name.substr(0, name.size() - utils::pathExtension(name).size());
    const auto codepoints = decode_to_u32(name);
    if (codepoints.size() != 1) throw std::invalid_argument("invalid font path");
    return enumutils::upperScalar(codepoints.front());
}

std::string encodeFontPath(CP c, std::string_view ext)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d27(__FILE__, __LINE__);
    // Dはcwx.utils.isFileNameCharを呼ぶ。ここでも
    // 同じ公開補助関数を使う。ネイティブ側は対象環境間で使える名前を意図的
    // に選ぶ規則を持つため、画像処理でその規則を重複実装・上書きしない。
    if (!utils::isFileNameChar(c)) return {};
    std::string result = "font_";
    append_utf8(result, c);
    result.append(ext);
    return result;
}

std::string formatMsg(
    std::string_view text,
    const VarGetter& getFlag,
    const VarGetter& getStep,
    const VarGetter& getVariant,
    const NameGetter& getName,
    const VersionPredicate& isTargetVersion,
    std::string_view prefixSystemVarName,
    const MaterialPredicate& hasMaterial,
    FontMap& fonts,
    ColorMap& colors)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d50(__FILE__, __LINE__);
    const FormatInputs inputs{getFlag, getStep, getVariant, getName,
        isTargetVersion, std::string(prefixSystemVarName), hasMaterial};
    fonts.clear();
    colors.clear();
    return encode_utf8(format_msg_impl(
        text, inputs, fonts, colors, true, 0, 0));
}

std::string formatMsgImpl(
    std::string_view text,
    const VarGetter& getFlag,
    const VarGetter& getStep,
    const VarGetter& getVariant,
    const NameGetter& getName,
    const VersionPredicate& isTargetVersion,
    std::string_view prefixSystemVarName,
    const MaterialPredicate& hasMaterial,
    FontMap& fonts,
    ColorMap& colors,
    bool full,
    std::size_t startIndex,
    std::size_t stack)
{
    const FormatInputs inputs{getFlag, getStep, getVariant, getName,
        isTargetVersion, std::string(prefixSystemVarName), hasMaterial};
    return encode_utf8(format_msg_impl(
        text, inputs, fonts, colors, full, startIndex, stack));
}

std::string simpleFormatMsg(
    std::string_view text,
    const VarGetter& flags,
    const VarGetter& steps,
    const VarGetter& variants,
    const VarGetter& sysSteps,
    const NameMap& names,
    const VersionPredicate& isTargetVersion,
    std::string_view prefixSystemVarName)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d56(__FILE__, __LINE__);
    // Dは連想配列のハンドルを値で渡す。呼び出し側が別のNameMap
    // を代入しても、全整形コールバックの間、現在の表を保持する。
    const auto nameView = NameMap::fromSharedStorage(names.sharedStorage());
    // Dのデリゲート引数は値で渡される。呼び出し側の関数項目を読み
    // 直さず、選択したデリゲートを再入コールバックの間も保持する。
    const VarGetter flag_lookup = [getter = flags](const std::string& path) {
        return getter(path);
    };
    const VarGetter variant_lookup = [getter = variants](const std::string& path) {
        return getter(path);
    };
    const auto version = isTargetVersion;
    (void)prefixSystemVarName;  // Dの補助処理は意図的に空の接頭辞を渡す。
    const VarGetter step_lookup = [steps, sysSteps](const std::string& path) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d61(__FILE__, __LINE__);
        // Dではデリゲートがなければ呼び出し時に失敗する。ネイティブア
        // ダプターで、存在しない変数を正常に検索した結果へ変換しない。
        const auto value = steps(path);
        if (value.exists) return value;
        return sysSteps(enumutils::lowerCase(path));
    };
    const NameGetter name_lookup = [&](char name) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d67(__FILE__, __LINE__);
        // std.ascii.toUpperはプロセスのロケールに依存しない。
        const auto asciiUpper = [](char c) {
            return c >= 'a' && c <= 'z' ? static_cast<char>(c - ('a' - 'A')) : c;
        };
        const char upper = asciiUpper(name);
        for (const auto& [key, value] : nameView) {
            ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d69(__FILE__, __LINE__);
            if (asciiUpper(key) == upper) {
                ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d70(__FILE__, __LINE__);
                return value;
            }
        }
        return std::string("#") + name;
    };
    FontMap fonts;
    ColorMap colors;
    const MaterialPredicate no_material = [](const std::string&) { return false; };
    const FormatInputs inputs{flag_lookup, step_lookup, variant_lookup,
        name_lookup, version, {}, no_material};
    return encode_utf8(format_msg_impl(
        text, inputs, fonts, colors, false, 0, 0));
}

std::vector<Grapheme> toGraphemeArray(std::string_view str)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d218(__FILE__, __LINE__);
    return graphemes_from_codepoints(decode_utf8(str));
}

std::vector<Grapheme> toGraphemeArray(std::u32string_view str)
{
    return graphemes_from_codepoints(std::vector<CP>(str.begin(), str.end()));
}

std::vector<Grapheme> toGraphemeArray(std::u16string_view str)
{
    return graphemes_from_codepoints(decode_utf16(str));
}

std::vector<Grapheme> toGraphemeArray(std::wstring_view str)
{
    // wchar_tはネイティブのコード単位幅に従い、WindowsではUTF-16、
    // 一般的なPOSIX対象ではUTF-32。どちらも同じデコーダーと属性表へ渡す。
    if constexpr (sizeof(wchar_t) == 2) {
        std::u16string units;
        units.reserve(str.size());
        for (const auto unit : str) units.push_back(static_cast<char16_t>(unit));
        return toGraphemeArray(std::u16string_view(units));
    } else {
        std::vector<CP> points;
        points.reserve(str.size());
        for (const auto point : str) points.push_back(static_cast<CP>(point));
        return graphemes_from_codepoints(points);
    }
}

void sortChars(std::vector<char>& chars)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d237(__FILE__, __LINE__);
    std::sort(chars.begin(), chars.end(), [](unsigned char a, unsigned char b) { return a < b; });
}

void sortChars(std::string& chars)
{
    std::sort(chars.begin(), chars.end(), [](unsigned char a, unsigned char b) { return a < b; });
}

void replOn(
    DString& dtext,
    DString& buffer,
    std::size_t& index,
    std::u32string_view oldValue,
    std::u32string_view newValue,
    DChar fromChar,
    DChar targetChar)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d302(__FILE__, __LINE__);
    if (index >= dtext.size()) throw std::out_of_range("replOn index");
    const auto next_it = std::find(dtext.begin() + static_cast<std::ptrdiff_t>(index + 1u),
                                   dtext.end(), fromChar);
    if (next_it == dtext.end()) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d315(__FILE__, __LINE__);
        buffer.push_back(dtext[index]);
        return;
    }
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d304(__FILE__, __LINE__);
    const auto next = static_cast<std::size_t>(next_it - dtext.begin());
    if (DString(dtext.begin() + static_cast<std::ptrdiff_t>(index + 1u),
                dtext.begin() + static_cast<std::ptrdiff_t>(next)) == oldValue) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d306(__FILE__, __LINE__);
        buffer.push_back(targetChar);
        buffer.append(newValue);
        buffer.push_back(targetChar);
    } else {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d308(__FILE__, __LINE__);
        buffer.append(dtext.begin() + static_cast<std::ptrdiff_t>(index),
                      dtext.begin() + static_cast<std::ptrdiff_t>(next + 1u));
    }
    if (next < dtext.size()) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d311(__FILE__, __LINE__);
        dtext = dtext.substr(next);
        index = 0;
    }
}

void replOff(DString& dtext, DString& buffer, std::size_t& index, DChar targetChar)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d319(__FILE__, __LINE__);
    if (index >= dtext.size()) throw std::out_of_range("replOff index");
    const auto next_it = std::find(dtext.begin() + static_cast<std::ptrdiff_t>(index + 1u),
                                   dtext.end(), targetChar);
    if (next_it == dtext.end()) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d328(__FILE__, __LINE__);
        buffer.push_back(dtext[index]);
        return;
    }
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d321(__FILE__, __LINE__);
    const auto next = static_cast<std::size_t>(next_it - dtext.begin());
    buffer.push_back(targetChar);
    buffer.append(dtext.begin() + static_cast<std::ptrdiff_t>(index + 1u),
                  dtext.begin() + static_cast<std::ptrdiff_t>(next + 1u));
    if (next < dtext.size()) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d324(__FILE__, __LINE__);
        dtext = dtext.substr(next);
        index = 0;
    }
}

std::string replTextFlagStepImpl(
    std::string_view text,
    std::string_view oldValue,
    std::string_view newValue,
    DChar primary,
    DChar secondary,
    DChar tertiary,
    DChar target)
{
    return repl_text_flag_step(text, oldValue, newValue, primary, secondary,
                               tertiary, target);
}

void textUseItems(
    std::string_view text,
    std::vector<std::string>& flags,
    std::vector<std::string>& steps,
    std::vector<std::string>& variables,
    std::vector<std::string>& fonts,
    std::vector<char>& colors)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d251(__FILE__, __LINE__);
    flags.clear();
    steps.clear();
    variables.clear();
    // Dの全out引数は、失敗経路も含め解析前にリセットする。
    fonts.clear();
    colors.clear();
    FontMap found_fonts;
    ColorMap found_colors;
    const VarGetter flag_lookup = [&](const std::string& path) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d254(__FILE__, __LINE__);
        flags.push_back(path);
        return VarValue{true, {}, false};
    };
    const VarGetter step_lookup = [&](const std::string& path) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d257(__FILE__, __LINE__);
        steps.push_back(path);
        return VarValue{true, {}, false};
    };
    const VarGetter variant_lookup = [&](const std::string& path) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d260(__FILE__, __LINE__);
        variables.push_back(path);
        return VarValue{true, {}, false};
    };
    const NameGetter no_name = [](char) { ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d263(__FILE__, __LINE__); return std::string{}; };
    const VersionPredicate no_version = [](const std::string&) { return false; };
    const MaterialPredicate no_material;
    (void)formatMsg(text, flag_lookup, step_lookup, variant_lookup, no_name,
                    no_version, {}, no_material, found_fonts, found_colors);

    fonts.clear();
    fonts.reserve(found_fonts.size());
    // Dは位置の順序を指定せず、連想配列の値を返す。
    for (const auto& [index, value] : found_fonts) {
        (void)index;
        fonts.push_back(value);
    }
    colors.clear();
    colors.reserve(found_colors.size());
    for (const auto& [index, value] : found_colors) {
        (void)index;
        colors.push_back(value);
    }
    sortChars(colors);
    colors.erase(std::unique(colors.begin(), colors.end()), colors.end());
}

std::vector<char> namesInText(std::string_view text, bool full)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d281(__FILE__, __LINE__);
    std::vector<char> result;
    FontMap fonts;
    ColorMap colors;
    const VarGetter existing = [](const std::string&) { return VarValue{true, {}, false}; };
    const NameGetter collect_name = [&](char name) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d289(__FILE__, __LINE__);
        result.push_back(name);
        return std::string("#") + name;
    };
    const MaterialPredicate no_material = [](const std::string&) { return false; };
    const FormatInputs inputs{existing, existing, existing, collect_name,
        {}, {}, no_material};
    (void)format_msg_impl(text, inputs, fonts, colors, full, 0, 0);
    return result;
}

std::string replTextUseFlag(
    std::string_view text, std::string_view oldFlag, std::string_view newFlag)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d367(__FILE__, __LINE__);
    return repl_text_flag_step(text, oldFlag, newFlag, U'%', U'$', U'@', U'%');
}

std::string replTextUseStep(
    std::string_view text, std::string_view oldStep, std::string_view newStep)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d380(__FILE__, __LINE__);
    return repl_text_flag_step(text, oldStep, newStep, U'$', U'%', U'@', U'$');
}

std::string replTextUseVariant(
    std::string_view text, std::string_view oldVariant, std::string_view newVariant)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d393(__FILE__, __LINE__);
    return repl_text_flag_step(text, oldVariant, newVariant, U'@', U'%', U'$', U'@');
}

std::string replStepToVariantInText(
    std::string_view text, std::string_view oldStep, std::string_view newStep)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d406(__FILE__, __LINE__);
    return repl_text_flag_step(text, oldStep, newStep, U'$', U'%', U'@', U'@');
}

bool isSPFontFile(std::string_view file)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d418(__FILE__, __LINE__);
    const auto basename = basename_u32(file);
    return basename.size() == 10 && basename.substr(0, 5) == U"font_" &&
           basename.substr(6) == U".bmp";
}

std::string replTextUseFont(
    std::string_view text, std::string_view oldFont, std::string_view newFont)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d436(__FILE__, __LINE__);
    // 旧表記を検査する前に両方の名前を復号する点も含め、Dの入力
    // 契約を維持する。アサートの代わりにネイティブの例外を使う。
    const auto old_lower = basename_u32(oldFont);
    (void)basename_u32(newFont);
    if (!starts_with(old_lower, U"font_") ||
        old_lower.size() < 4 || old_lower.substr(old_lower.size() - 4) != U".bmp") {
        throw std::invalid_argument("invalid old font filename");
    }
    if (!isSPFontFile(oldFont) || !isSPFontFile(newFont)) return std::string(text);
    U32 dtext = enumutils::toUtf32Replacing(text);
    const auto new_base = find_base_name(newFont);
    const auto old_base = find_base_name(oldFont);
    const auto old_char = enumutils::lowerCase(encode_utf8(U32(1, old_base[5])));
    // Dは旧記号を大文字小文字を区別せずに比較するが、挿
    // 入する記号は置換先ファイル名の表記どおりにする。
    const auto new_char = new_base[5];
    U32 buffer;
    for (std::size_t i = 0; i < dtext.size(); ++i) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d444(__FILE__, __LINE__);
        const auto c = dtext[i];
        switch (c) {
        case U'#':
            buffer.push_back(c);
            if (i + 1u < dtext.size()) {
                ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d449(__FILE__, __LINE__);
                const auto next = dtext[i + 1u];
                const auto next_lower = enumutils::lowerCase(encode_utf8(U32(1, next)));
                buffer.push_back(next_lower == old_char ? new_char : next);
                ++i;
            }
            break;
        case U'%':
        case U'$': {
            const auto next_it = std::find(dtext.begin() + static_cast<std::ptrdiff_t>(i + 1u),
                                           dtext.end(), c);
            if (next_it == dtext.end()) {
                buffer.push_back(c);
            } else {
                const auto next = static_cast<std::size_t>(next_it - dtext.begin());
                buffer.push_back(c);
                buffer.append(dtext.begin() + static_cast<std::ptrdiff_t>(i + 1u),
                              dtext.begin() + static_cast<std::ptrdiff_t>(next + 1u));
                if (next < dtext.size()) {
                    dtext = dtext.substr(next);
                    i = 0;
                }
            }
            break;
        }
        default:
            buffer.push_back(c);
            break;
        }
    }
    return encode_utf8(buffer);
}

DString putColor(DString text, DChar color, std::size_t start, std::size_t end)
{
    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d479(__FILE__, __LINE__);
    if (start > text.size() || end > text.size() || start > end) {
        throw std::out_of_range("putColor range is outside the message");
    }
    if (start == end) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d480(__FILE__, __LINE__);
        DString result = text.substr(0, start);
        result.push_back(U'&');
        result.push_back(color);
        result += text.substr(end);
        return result;
    }

    DChar default_color = U'W';
    if (start >= 1) {
        ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d484(__FILE__, __LINE__);
        // Dのtext[1 .. start]に対するforeach_reverse
        // は、text[i]の検査にスライス相対のインデックスを使う。そのため
        // ここでは、認識した色コードの直前のアンパサンドを探す対応処理になる。
        for (std::size_t index = start - 1; index >= 1; --index) {
            ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d485(__FILE__, __LINE__);
            const auto c = text[index];
            switch (c) {
            case U'W': case U'R': case U'B': case U'G': case U'Y':
            case U'O': case U'P': case U'L': case U'D':
                if (text[index - 1] == U'&') {
                    ::cwxeditor::cwx::perf::TraceScope trace_msgutils_d489(__FILE__, __LINE__);
                    default_color = c;
                    index = 0;
                }
                break;
            default:
                break;
            }
            if (index == 0) break;
        }
    }
    if (default_color == color) return text;
    DString result = text.substr(0, start);
    result.push_back(U'&');
    result.push_back(color);
    result += text.substr(start, end - start);
    result.push_back(U'&');
    result.push_back(default_color);
    result += text.substr(end);
    return result;
}

namespace {

struct WrappedChar {
    std::size_t index{};
    U32 str{};
    bool isFont{false};
    bool isColor{false};

    [[nodiscard]] bool is_special() const noexcept { return isFont || isColor; }
};

[[nodiscard]] std::size_t width_of(const WidthGetter& getWidth, std::u32string_view value)
{
    return getWidth(encode_utf8(value));
}

[[nodiscard]] std::size_t slice_pos(
    std::u32string_view value, std::size_t width, const WidthGetter& getWidth)
{
    std::size_t left_length = 0;
    std::size_t left_width = 0;
    for (const auto c : value) {
        const U32 one(1, c);
        const auto c_width = width_of(getWidth, one);
        if (width < left_width + c_width) break;
        ++left_length;
        left_width += c_width;
    }
    return left_length;
}

[[nodiscard]] bool match_open(
    const WrappedChar& value, const std::vector<U32>& openChars)
{
    return !value.is_special() &&
           std::find(openChars.begin(), openChars.end(), value.str) != openChars.end();
}

[[nodiscard]] bool match_close(
    const WrappedChar& value, const std::vector<U32>& closeChars)
{
    return !value.is_special() &&
           std::find(closeChars.begin(), closeChars.end(), value.str) != closeChars.end();
}

template <typename Matcher>
[[nodiscard]] bool match_last(
    const std::vector<WrappedChar>& values, Matcher&& matcher)
{
    for (auto iterator = values.rbegin(); iterator != values.rend(); ++iterator) {
        if (iterator->isColor) continue;
        return matcher(*iterator);
    }
    return false;
}

[[nodiscard]] std::string wrap_msg_line(
    U32 text,
    std::size_t width,
    const WidthGetter& getWidth,
    bool boundaryCheck,
    const std::vector<U32>& openChars,
    const std::vector<U32>& closeChars,
    const cwxeditor::lib::DRegex& wordRegex,
    FontMap& fonts,
    ColorMap& colors,
    FontMap& fonts2,
    ColorMap& colors2,
    std::size_t startIndex,
    std::size_t resultIndex)
{
    (void)boundaryCheck;
    std::vector<U32> words;
    const auto regex_words = wordRegex.matchAll(text);
    std::size_t index = startIndex;
    U32 special_prefix;
    for (const auto& regex_word : regex_words) {
        U32 word = regex_word;
        if (!special_prefix.empty()) {
            // Dはここでword[0]を参照し、空の特殊文字の組を拒否する
            // 。C++の未定義アクセスを避け、同じ例外の境界を維持する。
            if (word.empty()) throw std::out_of_range("Empty regex hit after message special prefix");
            special_prefix += word.front();
            words.push_back(std::move(special_prefix));
            special_prefix.clear();
            if (word.size() > 1) {
                words.emplace_back(word.begin() + 1, word.end());
            }
        } else if (fonts.contains(index) || colors.contains(index)) {
            special_prefix = word;
        } else {
            words.push_back(std::move(word));
        }
        index += regex_word.size();
    }

    if (!special_prefix.empty()) throw std::runtime_error("Incomplete message regex special prefix");

    std::vector<std::vector<WrappedChar>> lines;
    std::vector<WrappedChar> buffer;
    std::size_t buffer_length = 0;
    const auto hash_width = width_of(getWidth, U"#");
    index = startIndex;

    for (auto word : words) {
        const bool isFont = fonts.contains(index);
        const bool isColor = colors.contains(index);
        auto word_length = width_of(getWidth, word);

        if (width < buffer_length + word_length) {
            const auto matchOpenLast = [&](const std::vector<WrappedChar>& values) {
                return match_last(values, [&](const WrappedChar& value) {
                    return match_open(value, openChars);
                });
            };
            const auto matchCloseLast = [&](const std::vector<WrappedChar>& values) {
                return match_last(values, [&](const WrappedChar& value) {
                    return match_close(value, closeChars);
                });
            };

            const auto appendWordWrap = [&]() {
                if (isFont || isColor) return;
                // 行がいっぱいで語を一文字も取り込めない場合、Dと同
                // じ空の断片を維持する。ただし空行に最初の文字が収
                // まらない場合（幅0も含む）は必ず進め、残りが空な
                // ら停止する。Dはその条件で無限ループになり得るた
                // め。この防御は、後の呼び出しで小さい幅を返す状態
                // 付きの取得関数にも適用する、ネイティブ側の修正。
                while (width < buffer_length + width_of(getWidth, word) && !word.empty()) {
                    // Dのsize_tの減算は折り返す。ここで範囲制限すると
                    // 、句読点のぶら下げや末尾の開き括弧の後の結果が変わる。
                    const auto available = width - buffer_length;
                    auto position = slice_pos(word, available, getWidth);
                    if (position == 0 && buffer.empty()) position = 1;
                    U32 word2(word.begin(), word.begin() + static_cast<std::ptrdiff_t>(position));
                    U32 word3(word.begin() + static_cast<std::ptrdiff_t>(position), word.end());
                    if (!word2.empty()) word2.push_back(U'-');
                    buffer.push_back({index, std::move(word2), false, false});
                    word = std::move(word3);
                    lines.push_back(std::move(buffer));
                    buffer.clear();
                    buffer_length = 0;
                }
            };

            const auto breakBeforeOpenChar = [&](std::vector<WrappedChar> values) {
                while (!values.empty() && matchOpenLast(values)) values.pop_back();
                if (!values.empty()) {
                    const auto split = values.size();
                    lines.emplace_back(buffer.begin(),
                                       buffer.begin() + static_cast<std::ptrdiff_t>(split));
                    buffer.erase(buffer.begin(),
                                 buffer.begin() + static_cast<std::ptrdiff_t>(split));
                    buffer_length = 0;
                    for (const auto& item : buffer) buffer_length += width_of(getWidth, item.str);
                } else {
                    appendWordWrap();
                }
            };

            if (!buffer.empty() && matchOpenLast(buffer) && !matchCloseLast(buffer)) {
                breakBeforeOpenChar(buffer);
                word_length = width_of(getWidth, word);
            } else if (!(word.size() == 1 && is_whitespace(word.front()))) {
                const WrappedChar candidate{index, word, isFont, isColor};
                if (match_close(candidate, closeChars)) {
                    if (width < buffer_length ||
                        (width == buffer_length && hash_width < word_length)) {
                        auto values = buffer;
                        while (!values.empty() && matchCloseLast(values)) values.pop_back();
                        if (values.empty() || (values.size() == 1 && !matchOpenLast(values))) {
                            lines.push_back(std::move(buffer));
                            buffer.clear();
                            buffer_length = 0;
                        } else if (values.size() >= 2 &&
                                   !matchOpenLast(std::vector<WrappedChar>(
                                       values.begin(), values.end() - 1))) {
                            const auto split = values.size() - 1;
                            lines.emplace_back(buffer.begin(),
                                               buffer.begin() + static_cast<std::ptrdiff_t>(split));
                            buffer.erase(buffer.begin(),
                                         buffer.begin() + static_cast<std::ptrdiff_t>(split));
                            buffer_length = 0;
                            for (const auto& item : buffer) {
                                buffer_length += width_of(getWidth, item.str);
                            }
                        } else {
                            breakBeforeOpenChar(values);
                            word_length = width_of(getWidth, word);
                        }
                    }
                } else {
                    if (!buffer.empty()) {
                        lines.push_back(std::move(buffer));
                        buffer.clear();
                        buffer_length = 0;
                    }
                    appendWordWrap();
                    word_length = width_of(getWidth, word);
                }
            }
        }

        if (!word.empty()) {
            buffer.push_back({index, word, isFont, isColor});
            if (!isColor) buffer_length += word_length;
        }
        index += word.size();
    }
    if (!buffer.empty()) lines.push_back(std::move(buffer));

    std::vector<U32> result_lines;
    for (const auto& line : lines) {
        U32 result_line;
        for (const auto& item : line) {
            if (item.isFont) {
                fonts2[resultIndex] = fonts.at(item.index);
            }
            if (item.isColor) {
                colors2[resultIndex] = colors.at(item.index);
            }
            result_line += item.str;
            resultIndex += item.str.size();
        }
        result_lines.push_back(std::move(result_line));
        ++resultIndex;
    }

    U32 joined;
    for (std::size_t line = 0; line < result_lines.size(); ++line) {
        if (line) joined.push_back(U'\n');
        joined += result_lines[line];
    }
    return encode_utf8(joined);
}

[[nodiscard]] std::vector<U32> split_lines(U32 text)
{
    // 同じ区切り・空行の共通規則を使い、DのsplitLinesに
    // 合わせる。空入力は行なし、途中の空行は維持、末尾の改行では空
    // 行を追加しない。ここではUTF-8変換は厳密に行われている。
    std::vector<U32> lines;
    for (const auto& line : utils::splitLines(encode_utf8(text)))
        lines.push_back(decode_to_u32(line));
    return lines;
}

}  // namespace

DString wrapMsgImpl(
    DString text,
    std::size_t width,
    const WidthGetter& getWidth,
    bool boundaryCheck,
    const std::vector<DString>& openChars,
    const std::vector<DString>& closeChars,
    FontMap& fonts,
    ColorMap& colors,
    FontMap& fonts2,
    ColorMap& colors2,
    std::size_t startIndex,
    std::size_t resultIndex,
    std::u32string_view wordRegex)
{
    (void)boundaryCheck;
    // 公開ネイティブアダプターは一つのデリゲート値を保持する。補助処
    // 理は同じ関数オブジェクトを借用し、状態を持つ幅計算を共有する。
    const WidthGetter widthGetter = getWidth;
    const cwxeditor::lib::DRegex compiled(wordRegex);
    return decode_to_u32(wrap_msg_line(
        std::move(text), width, widthGetter, boundaryCheck, openChars, closeChars,
        compiled, fonts, colors, fonts2, colors2, startIndex, resultIndex));
}

std::size_t slicePos(
    std::u32string_view text, std::size_t width, const WidthGetter& getWidth)
{
    const WidthGetter widthGetter = getWidth;
    return slice_pos(text, width, widthGetter);
}

std::string wrapMsg(
    std::string_view text,
    std::size_t width,
    const WidthGetter& getWidth,
    bool boundaryCheck,
    std::u32string_view openChars,
    std::u32string_view closeChars,
    std::u32string_view wordRegex,
    FontMap& fonts,
    ColorMap& colors)
{
    // Dはデリゲートを値で受け取る。ネイティブの再入コードが呼び出し側のstd::function
    // 項目を置換しても、行のコールバックをまたいで保持する。
    const WidthGetter widthGetter = getWidth;
    // Dはテキストが空でも、行の巡回前にコンパイルする。
    const cwxeditor::lib::DRegex compiled(wordRegex);
    const auto sorted_open = sorted_chars(boundaryCheck ? openChars : U"");
    const auto sorted_close = sorted_chars(boundaryCheck ? closeChars : U"");
    const auto source = decode_to_u32(text);
    const auto lines = split_lines(source);
    FontMap fonts2;
    ColorMap colors2;
    std::vector<std::string> wrapped_lines;
    std::size_t source_index = 0;
    std::size_t result_index = 0;
    for (const auto& line : lines) {
        auto wrapped = wrap_msg_line(
            line, width, widthGetter, boundaryCheck, sorted_open, sorted_close,
            compiled, fonts, colors, fonts2, colors2, source_index, result_index);
        const auto wrapped_u32 = decode_to_u32(wrapped);
        wrapped_lines.push_back(std::move(wrapped));
        source_index += line.size() + 1u;
        result_index += wrapped_u32.size() + 1u;
    }
    fonts = std::move(fonts2);
    colors = std::move(colors2);

    std::string result;
    for (std::size_t line = 0; line < wrapped_lines.size(); ++line) {
        if (line) result.push_back('\n');
        result += wrapped_lines[line];
    }
    return result;
}

std::string wrapMsg(
    std::string_view text,
    std::size_t width,
    const WidthGetter& getWidth,
    bool boundaryCheck,
    const std::vector<DChar>& openChars,
    const std::vector<DChar>& closeChars,
    std::u32string_view wordRegex,
    FontMap& fonts,
    ColorMap& colors)
{
    return wrapMsg(text, width, getWidth, boundaryCheck,
                   std::u32string_view(openChars.data(), openChars.size()),
                   std::u32string_view(closeChars.data(), closeChars.size()),
                   wordRegex, fonts, colors);
}

}  // namespace cwxeditor::cwx::msgutils
