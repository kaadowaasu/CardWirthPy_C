#include "cwxeditor_src/core/cwx/jpy.hpp"

#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/sjis.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <new>
#include <regex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <type_traits>

namespace cwxeditor::cwx::jpy {

namespace {

using String = lib::String;
using StringView = lib::StringView;
using Message = msgs::Message;
namespace fs = std::filesystem;
namespace structs = ::cwxeditor::cwx::structs;

[[nodiscard]] bool asciiSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
           c == '\v';
}

[[nodiscard]] String trim(StringView value) {
    std::size_t begin = 0;
    while (begin < value.size() && asciiSpace(value[begin])) ++begin;
    std::size_t end = value.size();
    while (end > begin && asciiSpace(value[end - 1])) --end;
    return String(value.substr(begin, end - begin));
}

[[nodiscard]] String lowerAscii(StringView value) {
    String result(value);
    for (char& c : result) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= static_cast<unsigned char>('A') &&
            byte <= static_cast<unsigned char>('Z')) {
            c = static_cast<char>(byte + ('a' - 'A'));
        }
    }
    return result;
}

[[nodiscard]] String chomp(StringView line) { return utils::chompLine(line); }
[[nodiscard]] lib::Array<String> splitLinesKeep(StringView text) { return utils::splitLines(text,true); }
[[nodiscard]] lib::Array<String> splitLinesNoTerm(StringView text) { return utils::splitLines(text); }

[[nodiscard]] std::size_t newlineCount(StringView value) noexcept {
    return static_cast<std::size_t>(
        std::count(value.begin(), value.end(), static_cast<char>('\n')));
}

[[nodiscard]] String removeNewlines(String value) {
    value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    return value;
}

[[nodiscard]] String pathString(const fs::path& value) {
    if (value.empty()) return {};
    // JPYパスは共通APIをUTF-8で通る。generic_string()は有
    // 効な日本語文字も、Windowsの現在のコードページを経由させる場合がある。
    return utils::encodePath(structs::pathToUtf8(value));
}

[[nodiscard]] fs::path makePath(StringView value) {
    // Stringのバイト列をstd::filesystemのロケール依存の
    // ナロー文字パス構築で解釈せず、DのUnicodeパス操作に合わせる。
    return structs::pathFromUtf8(value);
}

[[nodiscard]] String dirName(StringView value) {
    const auto parent = makePath(value).parent_path();
    return pathString(parent);
}

[[nodiscard]] String baseName(StringView value) {
    return pathString(makePath(value).filename());
}

[[nodiscard]] String buildPath(StringView first, StringView second) {
    if (first.empty()) return String(second);
    if (second.empty()) return String(first);
    auto result = makePath(first);
    result /= makePath(second);
    return pathString(result);
}

[[nodiscard]] String normalizedAbsolute(StringView value) {
    return utils::nabs(value);
}

[[nodiscard]] String relativePath(StringView target, StringView base) {
    if (base.empty()) return String(target);
    return utils::encodePath(utils::abs2rel(
        StringView(normalizedAbsolute(target)),
        StringView(normalizedAbsolute(base))));
}

[[nodiscard]] String messageText(const Message& message) {
    return static_cast<const String&>(message);
}

[[nodiscard]] String formatMessage(const Message& message, StringView value) {
    return utils::tryFormat(messageText(message), String(value));
}

[[nodiscard]] bool parseIntStrict(StringView text, std::int32_t& result,
                                  int base = 10) noexcept {
    String value = trim(text);
    if (value.empty()) return false;
    bool plus = false;
    if (value.front() == '+') {
        plus = true;
        value.erase(value.begin());
        if (value.empty()) return false;
    }
    std::int64_t parsed = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto converted = std::from_chars(begin, end, parsed, base);
    if (converted.ec != std::errc{} || converted.ptr != end ||
        parsed < std::numeric_limits<std::int32_t>::min() ||
        parsed > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    result = static_cast<std::int32_t>(parsed);
    (void)plus;
    return true;
}

[[nodiscard]] bool isSignedDecimal(StringView text) noexcept {
    if (text.empty()) return false;
    std::size_t pos =
        text.front() == '-' || text.front() == '+' ? 1U : 0U;
    if (pos == text.size()) return false;
    return std::all_of(text.begin() + static_cast<std::ptrdiff_t>(pos),
                       text.end(), [](char c) {
                           return c >= '0' && c <= '9';
                       });
}

[[nodiscard]] bool parseHex(StringView text, std::uint32_t& result) noexcept {
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    std::uint32_t parsed = 0;
    const auto converted = std::from_chars(begin, end, parsed, 16);
    if (converted.ec != std::errc{} || converted.ptr != end) return false;
    result = parsed;
    return true;
}

[[nodiscard]] bool cixEqual(StringView left, StringView right) noexcept {
    return lowerAscii(left) == lowerAscii(right);
}

[[nodiscard]] bool validClosingTag(StringView candidate) {
    if (candidate.size() < 4 || !candidate.starts_with("</") ||
        candidate.back() != '>') {
        return false;
    }
    const auto name = lowerAscii(candidate.substr(2, candidate.size() - 3));
    return name == "b" || name == "i" || name == "u" || name == "s" ||
           name == "shiftx" || name == "shifty" || name == "lineheight" ||
           name == "font";
}

struct ParsedTag {
    String name;
    std::int32_t value = 0;
    bool hasValue = false;
    std::unordered_map<String, String> attr;
};

[[nodiscard]] bool parseTagCore(StringView source, ParsedTag& result) {
    if (source.size() < 3 || source.front() != '<' || source.back() != '>') {
        return false;
    }
    std::size_t pos = 1;
    const std::size_t end = source.size() - 1;
    const std::size_t nameStart = pos;
    while (pos < end) {
        const auto c = static_cast<unsigned char>(source[pos]);
        if (!std::isalpha(c)) break;
        ++pos;
    }
    if (pos == nameStart) return false;
    result.name = lowerAscii(source.substr(nameStart, pos - nameStart));
    if (pos < end && source[pos] == '=') {
        result.hasValue = true;
        ++pos;
        String token;
        if (pos < end && source[pos] == '"') {
            ++pos;
            const auto quote = source.find('"', pos);
            if (quote == StringView::npos || quote >= end) return false;
            token = String(source.substr(pos, quote - pos));
            pos = quote + 1;
        } else {
            const auto tokenStart = pos;
            while (pos < end && !asciiSpace(source[pos])) ++pos;
            if (tokenStart == pos) return false;
            token = String(source.substr(tokenStart, pos - tokenStart));
        }
        if (!parseIntStrict(token, result.value)) {
            // DはTAGに一致した数値をto!intで変換し、オーバーフ
            // ローを外へ伝播する。共通の解析経路でもこの動作を維持する。
            if (isSignedDecimal(token)) {
                throw std::out_of_range("JPTX numeric tag value is out of range");
            }
            return false;
        }
    }
    while (pos < end) {
        while (pos < end && asciiSpace(source[pos])) ++pos;
        if (pos == end) break;
        const auto keyStart = pos;
        while (pos < end && std::isalpha(static_cast<unsigned char>(source[pos]))) {
            ++pos;
        }
        if (keyStart == pos || pos >= end || source[pos] != '=') return false;
        String key = lowerAscii(source.substr(keyStart, pos - keyStart));
        ++pos;
        String value;
        if (pos < end && source[pos] == '"') {
            ++pos;
            const auto quote = source.find('"', pos);
            if (quote == StringView::npos || quote >= end) return false;
            value = String(source.substr(pos, quote - pos));
            pos = quote + 1;
        } else {
            // 引用符のないFACE文字列は空白を含められるが、後
            // 続の属性代入はネイティブ側の項目の境界として扱う。
            const auto valueStart = pos;
            if (key == "face") {
                // DのTAGが認識する、引用符なしで複数の語を含むFACEを
                // 受け付ける。ただしネイティブ側の修正を維持し、後続の属性
                // 代入は新しい値の開始、タグの「>」はFACEの外とする。
                while (pos < end) {
                    if (asciiSpace(source[pos])) {
                        auto next = pos;
                        while (next < end && asciiSpace(source[next])) ++next;
                        const auto nextName = next;
                        while (next < end && std::isalpha(
                                   static_cast<unsigned char>(source[next]))) {
                            ++next;
                        }
                        if (nextName < next && next < end && source[next] == '=') {
                            break;
                        }
                    }
                    ++pos;
                }
            } else {
                // ほかの引用符なしの属性値は、空白で区切ったままにする。
                while (pos < end && !asciiSpace(source[pos])) ++pos;
            }
            auto valueEnd = pos;
            while (valueEnd > valueStart && asciiSpace(source[valueEnd - 1])) {
                --valueEnd;
            }
            if (valueStart == valueEnd) return false;
            value = String(source.substr(valueStart, valueEnd - valueStart));
        }
        if (value.empty()) return false;
        result.attr[std::move(key)] = std::move(value);
    }
    return true;
}

[[nodiscard]] bool validOpeningTag(StringView candidate, ParsedTag* parsed = nullptr) {
    ParsedTag local;
    if (!parseTagCore(candidate, local)) return false;
    const auto& name = local.name;
    if (name == "br" || name == "b" || name == "i" || name == "u" ||
        name == "s") {
        if (local.hasValue || !local.attr.empty()) return false;
    } else if (name == "shiftx" || name == "shifty" ||
               name == "lineheight") {
        if (!local.hasValue || !local.attr.empty()) return false;
    } else if (name == "font") {
        if (local.hasValue || local.attr.empty()) return false;
        for (const auto& [key, value] : local.attr) {
            if (key != "face" && key != "color" && key != "pixels") return false;
            if (key == "color" &&
                (value.size() != 7 ||
                 (value.front() != '$' && value.front() != '#'))) {
                return false;
            }
            if (key == "color") {
                std::uint32_t ignored = 0;
                if (!parseHex(value.substr(1), ignored)) return false;
            }
            if (key == "pixels") {
                std::int32_t ignored = 0;
                // JptxTagはPIXELSを文字列として保持する。後でパーサ
                // ーがintValで変換し、オーバーフローを報告して0を返す。
                if (!parseIntStrict(value, ignored) &&
                    !isSignedDecimal(value)) {
                    return false;
                }
            }
        }
    } else {
        return false;
    }
    if (parsed != nullptr) *parsed = std::move(local);
    return true;
}

// JptxTag::parseと両方のパーサー入口で、一つの検証・変
// 換経路を共有する。省略可能なプロパティは診断文の選択にだけ使う。
[[nodiscard]] JptxTag parseOpeningTag(StringView startTag,
                                      const props::CProps* prop,
                                      StringView file, std::size_t line,
                                      EffectBoosterError& errInfo) {
    ParsedTag parsed;
    if (!validOpeningTag(startTag, &parsed)) {
        const auto message =
            prop != nullptr
                ? formatMessage(prop->requiredMsgs().jpyErrorInvalidStartTag,
                                startTag)
                : "Invalid start tag: " + String(startTag);
        errInfo.add(message, String(file), line);
        return {};
    }
    JptxTag result;
    result.name = std::move(parsed.name);
    result.tagValue = parsed.value;
    result.attr = std::move(parsed.attr);
    return result;
}

[[nodiscard]] bool findNextTag(StringView text, std::size_t from,
                               std::size_t& tagStart, std::size_t& tagEnd,
                               bool& closing) {
    // Dのjpy.d890-895のTAGの認識範囲は、追加の公開タ
    // グパーサーより狭い。未対応の空白やプラス付きの形式は通常の文字
    // 列として残す。DのTAGは空白を含む引用符なしFACEを受け
    // 付けるが、その属性パーサーは後続属性や閉じる「>」までFACE
    // へ取り込む場合がある。空白付き形式を認識しつつネイティブの修
    // 正を維持し、「>」でタグを区切り、parseTagCoreで
    // 後続属性を分離する。文法はASCIIの名前とバイト列を保つ書
    // 体名リテラルで構成され、全OSで同じ正規表現処理を使える。
    static const std::regex tags(
        R"jptx(</(b|i|u|s|shiftx|shifty|lineheight|font)>|<(br|b|i|u|s|shiftx=("-?[0-9]+"|-?[0-9]+)|shifty=("-?[0-9]+"|-?[0-9]+)|lineheight=("-?[0-9]+"|-?[0-9]+)|font( +(face=("[^"]+"|[^">]+)|color=("[\$#][0-9A-Fa-f]{6}"|[\$#][0-9A-Fa-f]{6})|pixels=("-?[0-9]+"|-?[0-9]+)))+)>)jptx",
        std::regex::ECMAScript | std::regex::icase);
    std::match_results<StringView::const_iterator> match;
    if (from > text.size() || !std::regex_search(text.begin()+static_cast<std::ptrdiff_t>(from),text.end(),match,tags)) return false;
    tagStart = from+static_cast<std::size_t>(match.position());
    tagEnd = tagStart+static_cast<std::size_t>(match.length());
    closing = text[tagStart+1]=='/';
    return true;
}

[[nodiscard]] bool isStandaloneTag(StringView line) {
    std::size_t start = 0;
    std::size_t end = 0;
    bool closing = false;
    return findNextTag(line, 0, start, end, closing) && start == 0 &&
           end == line.size();
}

[[nodiscard]] String normalizeNewlines(StringView text) {
    String result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            result.push_back('\n');
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else {
            result.push_back(text[i]);
        }
    }
    return result;
}

[[nodiscard]] String autolineText(StringView text) {
    const auto lines = splitLinesNoTerm(text);
    String result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        result += lines[i];
        if (i + 1 < lines.size() && !isStandaloneTag(lines[i])) {
            result += "<br>";
        }
    }
    return result;
}

template <typename Enum>
Enum enumValImpl(StringView value, const props::CProps* prop, StringView file,
                 std::size_t line, EffectBoosterError& errInfo) {
    std::int32_t parsed = 0;
    if (!parseIntStrict(value, parsed)) {
        if (prop != nullptr) {
            errInfo.add(formatMessage(prop->requiredMsgs().jpyErrorInvalidEnum, value),
                        String(file), line);
        } else {
            errInfo.add("Invalid enum: " + String(value), String(file), line);
        }
        if constexpr (std::is_same_v<Enum, Dirtype>) return Dirtype::CURRENT;
        else return static_cast<Enum>(0);
    }
    return static_cast<Enum>(parsed);
}

template <typename Enum>
Enum enumValOptional(StringView value, const props::CProps* prop,
                     StringView file, std::size_t line,
                     EffectBoosterError& errInfo) {
    return enumValImpl<Enum>(value, prop, file, line, errInfo);
}

[[nodiscard]] String materialPathFor(StringView filename, StringView sPath,
                                      StringView fPath) {
    const auto dir = dirName(fPath);
    const auto fname = normalizedAbsolute(buildPath(dir, filename));
    const auto scenario = normalizedAbsolute(sPath);
    if (utils::fnstartsWith(fname, scenario)) {
        return utils::encodePath(utils::abs2rel(StringView(fname),
                                                StringView(scenario)));
    }
    return {};
}

}  // namespace

EffectBoosterError::EffectBoosterError() = default;

const char* EffectBoosterError::what() const noexcept {
    return "EffectBooster error";
}

const std::vector<EffectBoosterError::EffectBoosterErrorInfo>&
EffectBoosterError::errors() const noexcept {
    return errors_;
}

void EffectBoosterError::add(String msg, String file, std::size_t line) {
    errors_.push_back(
        EffectBoosterErrorInfo{std::move(msg), std::move(file), line});
}

CPoint pointVal(StringView value, const props::CProps& prop, StringView file,
                std::size_t line, EffectBoosterError& errInfo) {
    const auto comma = value.find(',');
    if (comma == StringView::npos) {
        errInfo.add(formatMessage(prop.requiredMsgs().jpyErrorInvalidPoint, value),
                    String(file), line);
        return {};
    }
    // Dは値全体を分割し、最初の二項目だけを使う。座標エラ
    // ーを追加するのではなく、数値変換の失敗を伝播する。
    const auto nextComma = value.find(',', comma + 1);
    const auto yEnd = nextComma == StringView::npos ? value.size() : nextComma;
    std::int32_t x = 0;
    std::int32_t y = 0;
    if (!parseIntStrict(value.substr(0, comma), x) ||
        !parseIntStrict(value.substr(comma + 1, yEnd - comma - 1), y)) {
        throw std::invalid_argument("invalid JPY point");
    }
    return CPoint{x, y};
}

CRect rectVal(StringView value, const props::CProps& prop, StringView file,
              std::size_t line, EffectBoosterError& errInfo) {
    std::int32_t values[4]{};
    std::size_t begin = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        const auto comma = value.find(',', begin);
        const auto end = comma == StringView::npos ? value.size() : comma;
        if (!parseIntStrict(value.substr(begin, end - begin), values[index])) {
            errInfo.add(formatMessage(prop.requiredMsgs().jpyErrorInvalidRect, value),
                        String(file), line);
            return {};
        }
        if (comma == StringView::npos) {
            if (index != 3) {
                errInfo.add(
                    formatMessage(prop.requiredMsgs().jpyErrorInvalidRect, value),
                    String(file), line);
                return {};
            }
            begin = value.size();
        } else {
            begin = comma + 1;
        }
    }
    return CRect{values[0], values[1], values[2], values[3]};
}

CRGB rgbVal(StringView value, const props::CProps& prop, StringView file,
            std::size_t line, EffectBoosterError& errInfo) {
    std::uint32_t number = 0;
    if (value.size() >= 7 && (value.front() == '$' || value.front() == '#')) {
        const auto digits = value.substr(1, 6);
        if (!parseHex(digits, number)) {
            errInfo.add(formatMessage(prop.requiredMsgs().jpyErrorInvalidRGB, value),
                        String(file), line);
            return {};
        }
    } else if (!parseHex(value, number) ||
               number > static_cast<std::uint32_t>(
                            std::numeric_limits<std::int32_t>::max())) {
        errInfo.add(formatMessage(prop.requiredMsgs().jpyErrorInvalidRGB, value),
                    String(file), line);
        return {};
    }
    return CRGB{(number >> 16U) & 0xFFU, (number >> 8U) & 0xFFU,
                number & 0xFFU};
}

template <typename Enum>
Enum enumVal(StringView value, const props::CProps& prop, StringView file,
             std::size_t line, EffectBoosterError& errInfo) {
    return enumValOptional<Enum>(value, &prop, file, line, errInfo);
}

template Animation enumVal<Animation>(StringView, const props::CProps&, StringView,
                                      std::size_t, EffectBoosterError&);
template Colormap enumVal<Colormap>(StringView, const props::CProps&, StringView,
                                    std::size_t, EffectBoosterError&);
template Dirtype enumVal<Dirtype>(StringView, const props::CProps&, StringView,
                                  std::size_t, EffectBoosterError&);
template Colorexchange enumVal<Colorexchange>(StringView, const props::CProps&,
                                              StringView, std::size_t,
                                              EffectBoosterError&);
template Filter enumVal<Filter>(StringView, const props::CProps&, StringView,
                                std::size_t, EffectBoosterError&);
template Cache enumVal<Cache>(StringView, const props::CProps&, StringView,
                              std::size_t, EffectBoosterError&);
template Mask enumVal<Mask>(StringView, const props::CProps&, StringView,
                            std::size_t, EffectBoosterError&);
template Noise enumVal<Noise>(StringView, const props::CProps&, StringView,
                              std::size_t, EffectBoosterError&);
template Paintmode enumVal<Paintmode>(StringView, const props::CProps&, StringView,
                                      std::size_t, EffectBoosterError&);
template Turn enumVal<Turn>(StringView, const props::CProps&, StringView,
                            std::size_t, EffectBoosterError&);

String strVal(StringView value, const props::CProps& prop, StringView file,
              std::size_t line, EffectBoosterError& errInfo) {
    try {
        utils::validate(value);
        return String(value);
    } catch (...) {
        try {
            return sjis::touni(value);
        } catch (...) {
            errInfo.add(formatMessage(prop.requiredMsgs().jpyErrorInvalidStr, value),
                        String(file), line);
            return {};
        }
    }
}

std::int32_t intVal(StringView value, const props::CProps& prop,
                    StringView file, std::size_t line,
                    EffectBoosterError& errInfo) {
    String parsed = trim(value);
    if (parsed.ends_with("px")) parsed.erase(parsed.size() - 2);
    std::int32_t result = 0;
    if (!parseIntStrict(parsed, result)) {
        errInfo.add(formatMessage(prop.requiredMsgs().jpyErrorInvalidInt, parsed),
                    String(file), line);
        return 0;
    }
    return result;
}

bool boolVal(StringView value, const props::CProps&, StringView,
             std::size_t, EffectBoosterError&) {
    return value == "1";
}

bool validUtf8(const StringView text) noexcept {
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = bytes[i++];
        if (lead <= 0x7fU) continue;
        std::size_t count{};
        std::uint32_t code{};
        std::uint32_t minimum{};
        if (lead >= 0xc2U && lead <= 0xdfU) {
            count = 1; code = lead & 0x1fU; minimum = 0x80U;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            count = 2; code = lead & 0x0fU; minimum = 0x800U;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            count = 3; code = lead & 0x07U; minimum = 0x10000U;
        } else {
            return false;
        }
        if (i + count > text.size()) return false;
        for (std::size_t n = 0; n < count; ++n) {
            const unsigned char next = bytes[i++];
            if ((next & 0xc0U) != 0x80U) return false;
            code = (code << 6U) | (next & 0x3fU);
        }
        if (code < minimum || code > 0x10ffffU ||
            (code >= 0xd800U && code <= 0xdfffU)) return false;
    }
    return true;
}

class CallbackPathUser final : public usecounter::PathUser {
public:
    CallbackPathUser(path::CWXPath* owner,
                     std::function<bool(const usecounter::PathId&)> change)
        : PathUser(owner), change_(std::move(change)) {}

    bool change(const usecounter::PathId& value) override {
        return change_ ? change_(value) : PathUser::change(value);
    }

private:
    std::function<bool(const usecounter::PathId&)> change_;
};

String readJPYFile(StringView file, const props::CProps& prop,
                   EffectBoosterError& errInfo, bool& isSJIS) {
    const String filename(file);
    // Dは最初の読み込み前に出力値を設定し、二つの読み込みがともに失敗してもtrue
    // を残す。UTF-8の代替読み込みが成功した場合だけfalseへ変える。
    isSJIS = true;
    try {
        const auto bytes = utils::readBinary(makePath(filename));
        const String raw(bytes.begin(), bytes.end());
        return sjis::touni(raw);
    } catch (const std::bad_alloc&) {
        throw; // Dのcatch(Exception)は、メモリ確保のErrorを捕捉しない。
    } catch (const std::exception&) {
        try {
            const auto bytes = utils::readBinary(makePath(filename));
            const String raw(bytes.begin(), bytes.end());
            if (!validUtf8(raw)) throw std::runtime_error("invalid UTF-8");
            isSJIS = false;
            return raw;
        } catch (const std::bad_alloc&) {
            throw;
        } catch (const std::exception&) {
            errInfo.add(messageText(prop.requiredMsgs().jpyErrorInvalidEncoding), filename, 0);
            return {};
        }
    }
}

String readJPYFile(StringView file, const props::CProps& prop,
                   EffectBoosterError& errInfo) {
    bool isSJIS = false;
    return readJPYFile(file, prop, errInfo, isSJIS);
}

void writeJPYFile(StringView file, StringView value, bool isSJIS,
                  const lib::Owner<FileSync>& sync) {
    utils::writeTextFile(makePath(file), value, isSJIS, sync);
}

String stripValue(StringView eqAfter) {
    String value = trim(eqAfter);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

Jpy1 Jpy1::load(const props::CProps& prop, StringView scenarioPath,
                StringView filePath) {
    Jpy1 result;
    result.sPath = String(scenarioPath);
    result.jpy1Path = String(filePath);
    EffectBoosterError errors;
    result.lines = splitLinesKeep(
        readJPYFile(filePath, prop, errors, result.isSJIS));
    std::unordered_map<String, bool> sectionNames;
    bool hasExchange = false;

    for (std::size_t index = 0; index < result.lines.size(); ++index) {
        const String original = result.lines[index];
        const String line = trim(chomp(original));
        const std::size_t lineNumber = index + 1;
        if (line.empty() || line.front() == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            auto section = std::make_shared<Jpy1Sec>();
            section->sPath = result.sPath;
            section->fPath = result.jpy1Path;
            section->label = trim(line.substr(1, line.size() - 2));
            const auto lowerName = utils::caseFold(section->label);
            if (sectionNames.contains(lowerName)) {
                errors.add(formatMessage(prop.requiredMsgs().jpyErrorDupSection, line),
                             result.jpy1Path, lineNumber);
            } else {
                sectionNames.emplace(lowerName, true);
            }
            result.sections.push_back(std::move(section));
            hasExchange = false;
            continue;
        }
        if (result.sections.empty()) {
            errors.add(messageText(prop.requiredMsgs().jpyErrorLabelNotFound),
                        result.jpy1Path, lineNumber);
            throw errors;
        }

        auto& section = *result.sections.back();
        const auto equal = line.find('=');
        if (equal == String::npos) {
            errors.add(formatMessage(prop.requiredMsgs().jpyErrorInvalidLine, line),
                        result.jpy1Path, lineNumber);
            continue;
        }
        const auto key = trim(line.substr(0, equal));
        const auto value = stripValue(line.substr(equal + 1));
        const auto command = lowerAscii(key);
        if (command == "backwidth") {
            section.backwidth = intVal(value, prop, result.jpy1Path, lineNumber,
                                       errors);
        } else if (command == "backheight") {
            section.backheight = intVal(value, prop, result.jpy1Path, lineNumber,
                                        errors);
        } else if (command == "backcolor") {
            section.backcolor = rgbVal(value, prop, result.jpy1Path, lineNumber,
                                       errors);
        } else if (command == "width") {
            section.width = intVal(value, prop, result.jpy1Path, lineNumber,
                                   errors);
        } else if (command == "height") {
            section.height = intVal(value, prop, result.jpy1Path, lineNumber,
                                    errors);
        } else if (command == "color") {
            section.color = rgbVal(value, prop, result.jpy1Path, lineNumber,
                                   errors);
        } else if (command == "dirdepth") {
            if (result.sections.size() == 1 && section.label == "init") {
                result.dirdepth = intVal(value, prop, result.jpy1Path,
                                         lineNumber, errors);
                result.dirdepthIndex = static_cast<std::ptrdiff_t>(index);
                result.dirdepthLine = original;
            }
        } else if (command == "filename") {
            section.filename = strVal(value, prop, result.jpy1Path, lineNumber,
                                      errors);
            section.filenameIndex = static_cast<std::ptrdiff_t>(index);
            section.filenameLine = original;
        } else if (command == "flagname") {
            // 画像には影響しない、Editor専用のコマンド。
        } else if (command == "dirtype") {
            section.dirtype = enumVal<Dirtype>(value, prop, result.jpy1Path,
                                               lineNumber, errors);
        } else if (command == "loadcache") {
            section.loadcache = enumVal<Cache>(value, prop, result.jpy1Path,
                                               lineNumber, errors);
        } else if (command == "savecache") {
            section.savecache = enumVal<Cache>(value, prop, result.jpy1Path,
                                               lineNumber, errors);
        } else if (command == "visible") {
            section.visible = boolVal(value, prop, result.jpy1Path, lineNumber,
                                     errors);
        } else if (command == "position") {
            section.position = pointVal(value, prop, result.jpy1Path, lineNumber,
                                        errors);
        } else if (command == "transparent") {
            section.transparent = boolVal(value, prop, result.jpy1Path,
                                          lineNumber, errors);
        } else if (command == "clip") {
            section.clip = rectVal(value, prop, result.jpy1Path, lineNumber,
                                   errors);
        } else if (command == "paintmode") {
            section.paintmode = enumVal<Paintmode>(
                value, prop, result.jpy1Path, lineNumber, errors);
        } else if (command == "alpha") {
            section.alpha = intVal(value, prop, result.jpy1Path, lineNumber,
                                   errors);
        } else if (command == "animeclip") {
            section.animeclip = rectVal(value, prop, result.jpy1Path,
                                        lineNumber, errors);
        } else if (command == "animation") {
            section.animation = enumVal<Animation>(
                value, prop, result.jpy1Path, lineNumber, errors);
        } else if (command == "animeposition") {
            section.animeposition = pointVal(value, prop, result.jpy1Path,
                                             lineNumber, errors);
        } else if (command == "animemove") {
            section.animemove = pointVal(value, prop, result.jpy1Path,
                                         lineNumber, errors);
        } else if (command == "wait") {
            section.wait = intVal(value, prop, result.jpy1Path, lineNumber,
                                  errors);
        } else if (command == "animespeed") {
            section.animespeed = intVal(value, prop, result.jpy1Path, lineNumber,
                                        errors);
        } else if (command == "smooth") {
            section.smooth = boolVal(value, prop, result.jpy1Path, lineNumber,
                                     errors);
        } else if (command == "colorexchange") {
            if (!hasExchange) {
                section.colorexchange = enumVal<Colorexchange>(
                    value, prop, result.jpy1Path, lineNumber, errors);
                hasExchange = true;
            }
        } else if (command == "exchange") {
            // cwconv.dllは従来の別名を優先する。
            section.colorexchange = enumVal<Colorexchange>(
                value, prop, result.jpy1Path, lineNumber, errors);
            hasExchange = true;
        } else if (command == "colormap") {
            section.colormap = enumVal<Colormap>(value, prop, result.jpy1Path,
                                                 lineNumber, errors);
        } else if (command == "filter") {
            section.filter = enumVal<Filter>(value, prop, result.jpy1Path,
                                             lineNumber, errors);
        } else if (command == "mask") {
            section.mask = enumVal<Mask>(value, prop, result.jpy1Path, lineNumber,
                                         errors);
        } else if (command == "noise") {
            section.noise = enumVal<Noise>(value, prop, result.jpy1Path,
                                           lineNumber, errors);
        } else if (command == "noisepoint") {
            section.noisepoint = intVal(value, prop, result.jpy1Path, lineNumber,
                                        errors);
        } else if (command == "turn") {
            section.turn = enumVal<Turn>(value, prop, result.jpy1Path, lineNumber,
                                         errors);
        } else if (command == "flip") {
            section.flip = boolVal(value, prop, result.jpy1Path, lineNumber,
                                   errors);
        } else if (command == "mirror") {
            section.mirror = boolVal(value, prop, result.jpy1Path, lineNumber,
                                     errors);
        } else if (command == "comment") {
            section.comment = strVal(value, prop, result.jpy1Path, lineNumber,
                                     errors);
        } else {
            errors.add(formatMessage(prop.requiredMsgs().jpyErrorInvalidCommand, key),
                        result.jpy1Path, lineNumber);
        }
    }

    if (!errors.errors().empty()) throw errors;
    for (auto& section : result.sections) {
        section->dirdepth = result.dirdepth;
        section->path(section->toMaterialPath());
        section->needUpdate = false;
    }
    return result;
}

void Jpy1::setUseCounter(const usecounter::CounterOwner& uc) {
    for (auto& section : sections) {
        if (!section) throw std::invalid_argument("Null JPY section");
        section->setUseCounter(uc);
    }
}

void Jpy1::setUseCounter(usecounter::UseCounter* uc) {
    if (!uc) { removeUseCounter(); return; }
    try { setUseCounter(uc->shared_from_this()); }
    catch (const std::bad_weak_ptr&) { removeUseCounter(); }
}

void Jpy1::removeUseCounter() {
    for (auto& section : sections) {
        if (!section) throw std::invalid_argument("Null JPY section");
        section->removeUseCounter();
    }
}

bool Jpy1::renameFile(StringView oldPath, StringView newPath) {
    const auto file = normalizedAbsolute(jpy1Path);
    const auto old = normalizedAbsolute(oldPath);
    if (!utils::fnstartsWith(file, old)) return false;
    String renamed(newPath);
    if (!utils::cfnmatch(file, old)) renamed = buildPath(newPath, relativePath(file, old));
    jpy1Path = std::move(renamed);
    for (auto& section : sections) {
        const auto material = section->toMaterialPath();
        section->fPath = jpy1Path;
        if (material.empty()) continue;
        switch (section->dirtype) {
        case Dirtype::CURRENT:
        case Dirtype::SCENARIO:
        case Dirtype::PARENT:
            section->path(material);
            break;
        default:
            break;
        }
    }
    return true;
}

bool Jpy1::updateJpy1File(const props::CProps& prop, bool rewrite,
                          const lib::Owner<FileSync>& sync) {
    (void)prop;
    bool update = false;
    for (auto& section : sections) {
        if (section->dirtype != Dirtype::CURRENT &&
            section->dirtype != Dirtype::SCENARIO &&
            section->dirtype != Dirtype::PARENT) {
            continue;
        }
        if (!section->needUpdate) continue;
        if (section->filenameIndex != -1) {
            const auto equal = section->filenameLine.find('=');
            if (equal != String::npos &&
                static_cast<std::size_t>(section->filenameIndex) < lines.size()) {
                const auto ret = section->filenameLine.substr(
                    chomp(section->filenameLine).size());
                lines[static_cast<std::size_t>(section->filenameIndex)] =
                    section->filenameLine.substr(0, equal + 1) + section->filename +
                    ret;
                update = true;
            }
        }
        section->needUpdate = false;
    }
    if (update && rewrite) {
        String output;
        for (const auto& line : lines) output += line;
        writeJPYFile(jpy1Path, output, isSJIS, sync);
    }
    return update && rewrite;
}

String Jpy1Sec::cwxPath(bool) { return {}; }

Jpy1Sec::Jpy1Sec()
    : path_user_(std::make_shared<CallbackPathUser>(
          static_cast<path::CWXPath*>(this),
          [this](const usecounter::PathId& value) {
              return change(value);
          })) {}

lib::Owner<path::CWXPath> Jpy1Sec::findCWXPath(StringView value) {
    if (!path::cpempty(value)) return {};
    try {
        return std::static_pointer_cast<path::CWXPath>(shared_from_this());
    } catch (const std::bad_weak_ptr&) {
        return {};
    }
}

lib::Array<lib::Observer<path::CWXPath>> Jpy1Sec::cwxChilds() { return {}; }

lib::Observer<path::CWXPath> Jpy1Sec::cwxParent() { return {}; }

bool Jpy1Sec::change(StringView newValue) {
    return change(usecounter::toPathId(newValue));
}

bool Jpy1Sec::change(usecounter::PathId newValue) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    // Dのsuper.changeに合わせ、PathId全体を維持してコールバ
    // ックの橋渡しを経由しない。Dは基底の結果を無視し、常にtrueを返す。
    static_cast<void>(path_user_->PathUser::change(newValue));
    const auto newName = fromMaterialPath(String(newValue));
    if (newName != filename) {
        filename = newName;
        needUpdate = true;
    }
    return true;
}

void Jpy1Sec::path(StringView value) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    const String material(value);
    // Dは表示ファイル名の計算前にPathUserとカウンターを更新する。
    path_user_->path(material);
    const auto newName = fromMaterialPath(material);
    if (newName != filename) {
        filename = newName;
        needUpdate = true;
    }
}

String Jpy1Sec::path() const noexcept {
    return path_user_ ? path_user_->path() : String{};
}
void Jpy1Sec::setUseCounter(const usecounter::CounterOwner& uc) {
    path_user_->setUseCounter(uc);
}
void Jpy1Sec::setUseCounter(usecounter::UseCounter* uc) {
    if (!uc) { removeUseCounter(); return; }
    try { setUseCounter(uc->shared_from_this()); }
    catch (const std::bad_weak_ptr&) { removeUseCounter(); }
}
void Jpy1Sec::removeUseCounter() { path_user_->removeUseCounter(); }
usecounter::UseCounter* Jpy1Sec::useCounter() const noexcept {
    return path_user_->useCounter().get();
}

void Jpy1Sec::changed() {}

String Jpy1Sec::toMaterialPath() const {
    return toMaterialPathImpl(filename, sPath, fPath, dirtype, dirdepth);
}

String Jpy1Sec::toMaterialPathImpl(StringView value, StringView scenarioPath,
                                    StringView filePath, Dirtype type,
                                    std::int32_t depth) {
    String directory;
    switch (type) {
    case Dirtype::CURRENT:
        directory = dirName(filePath);
        break;
    case Dirtype::TABLE:
    case Dirtype::SCHEME:
    case Dirtype::WAV:
    case Dirtype::PROGRAM:
        return String(value);
    case Dirtype::SCENARIO:
        if (scenarioPath.empty()) return {};
        directory = dirName(filePath);
        for (std::int32_t index = 0; index < depth; ++index) {
            directory = dirName(directory);
        }
        break;
    case Dirtype::PARENT:
        directory = dirName(dirName(filePath));
        break;
    default:
        return {};
    }
    const auto fname = normalizedAbsolute(buildPath(directory, value));
    const auto scenario = normalizedAbsolute(scenarioPath);
    if (utils::fnstartsWith(fname, scenario)) {
        return utils::encodePath(utils::abs2rel(StringView(fname),
                                                StringView(scenario)));
    }
    return {};
}

String Jpy1Sec::fromMaterialPath(StringView value) const {
    return fromMaterialPathImpl(value, sPath, fPath, dirtype, dirdepth);
}

String Jpy1Sec::fromMaterialPathImpl(StringView value, StringView scenarioPath,
                                      StringView filePath, Dirtype type,
                                      std::int32_t depth) {
    const auto relPath = [](StringView base, StringView target) {
        if (base.empty()) return String(target);
        return relativePath(target, base);
    };
    switch (type) {
    case Dirtype::CURRENT: {
        const auto fileRel = relativePath(filePath, scenarioPath);
        return buildPath(relPath(dirName(fileRel), dirName(value)),
                         baseName(value));
    }
    case Dirtype::TABLE:
    case Dirtype::SCHEME:
        break;
    case Dirtype::SCENARIO: {
        String directory = dirName(filePath);
        for (std::int32_t index = 0; index < depth; ++index) {
            if (utils::cfnmatch(normalizedAbsolute(directory),
                                normalizedAbsolute(scenarioPath))) {
                break;
            }
            directory = dirName(directory);
        }
        directory = relPath(scenarioPath, directory);
        return relPath(directory, value);
    }
    case Dirtype::WAV:
        break;
    case Dirtype::PARENT: {
        const auto fileRel = relativePath(filePath, scenarioPath);
        const auto directory = dirName(dirName(fileRel));
        return relPath(directory, value);
    }
    case Dirtype::PROGRAM:
        break;
    default:
        break;
    }
    return utils::decodePath(String(value));
}

JptxTag JptxTag::parse(StringView startTag, const props::CProps& prop,
                       StringView file, std::size_t line,
                       EffectBoosterError& errInfo) {
    return parseOpeningTag(startTag, &prop, file, line, errInfo);
}

namespace {

JptxTag parseOptionalTag(StringView startTag, const props::CProps* prop,
                         StringView file, std::size_t line,
                         EffectBoosterError& errInfo) {
    return parseOpeningTag(startTag, prop, file, line, errInfo);
}

void invokeFont(const JptxTag& tag, const props::CProps* prop, StringView file,
                std::size_t line, EffectBoosterError& errInfo,
                const std::function<void(String, CRGB, std::int32_t)>& callback) {
    if (!callback) return;
    String face;
    CRGB color{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    std::int32_t pixels = -1;
    if (const auto it = tag.attr.find("face"); it != tag.attr.end()) {
        if (prop != nullptr) {
            face = strVal(it->second, *prop, file, line, errInfo);
        } else {
            face = it->second;
        }
    }
    if (const auto it = tag.attr.find("color"); it != tag.attr.end()) {
        if (prop != nullptr) {
            color = rgbVal(it->second, *prop, file, line, errInfo);
        } else {
            std::uint32_t number = 0;
            if (parseHex(StringView(it->second).substr(1), number)) {
                color = CRGB{(number >> 16U) & 0xFFU, (number >> 8U) & 0xFFU,
                             number & 0xFFU};
            }
        }
    }
    if (const auto it = tag.attr.find("pixels"); it != tag.attr.end()) {
        if (prop != nullptr) {
            pixels = intVal(it->second, *prop, file, line, errInfo);
        } else {
            // プロパティなしのオーバーロードでも、DのintValの代替値
            // を維持する。診断を付けられるのはプロパティを持つ経路だけ。
            if (!parseIntStrict(it->second, pixels)) pixels = 0;
        }
    }
    callback(std::move(face), color, pixels);
}

}  // namespace

void JptxParser::startTag(StringView tagText, const props::CProps& prop,
                          StringView file, std::size_t line,
                          EffectBoosterError& errInfo) {
    const auto tag = parseOpeningTag(tagText, &prop, file, line, errInfo);
    if (tag.name.empty()) return;
    if (tag.name == "br") {
        if (onBR) onBR();
    } else if (tag.name == "b") {
        if (onB) onB();
    } else if (tag.name == "i") {
        if (onI) onI();
    } else if (tag.name == "u") {
        if (onU) onU();
    } else if (tag.name == "s") {
        if (onS) onS();
    } else if (tag.name == "shiftx") {
        if (onShiftx) onShiftx(tag.tagValue);
    } else if (tag.name == "shifty") {
        if (onShifty) onShifty(tag.tagValue);
    } else if (tag.name == "lineheight") {
        if (onLineheight) onLineheight(tag.tagValue);
    } else if (tag.name == "font") {
        invokeFont(tag, &prop, file, line, errInfo, onFont);
    }
}

void JptxParser::endTag(StringView tagText) {
    if (tagText.size() < 4) return;
    const auto tag = lowerAscii(tagText.substr(2, tagText.size() - 3));
    if (tag == "b") {
        if (onEndB) onEndB();
    } else if (tag == "i") {
        if (onEndI) onEndI();
    } else if (tag == "u") {
        if (onEndU) onEndU();
    } else if (tag == "s") {
        if (onEndS) onEndS();
    } else if (tag == "shiftx") {
        if (onEndShiftx) onEndShiftx();
    } else if (tag == "shifty") {
        if (onEndShifty) onEndShifty();
    } else if (tag == "lineheight") {
        if (onEndLineheight) onEndLineheight();
    } else if (tag == "font") {
        if (onEndFont) onEndFont();
    }
}

namespace {

void parseJptxText(JptxParser& parser, StringView input,
                   const props::CProps* prop, StringView file,
                   std::size_t line, EffectBoosterError& errInfo) {
    const String source = parser.autoline ? autolineText(input)
                                          : normalizeNewlines(input);
    std::size_t offset = 0;
    std::size_t currentLine = line;
    while (offset < source.size()) {
        std::size_t tagStart = 0;
        std::size_t tagEnd = 0;
        bool closing = false;
        if (!findNextTag(source, offset, tagStart, tagEnd, closing)) {
            if (parser.onText) parser.onText(String(source.substr(offset)));
            break;
        }
        const auto prefix = source.substr(offset, tagStart - offset);
        currentLine += newlineCount(prefix);
        if (parser.onText && !prefix.empty()) {
            parser.onText(removeNewlines(String(prefix)));
        }
        const auto tagText = source.substr(tagStart, tagEnd - tagStart);
        if (closing) {
            parser.endTag(tagText);
        } else if (prop != nullptr) {
            parser.startTag(tagText, *prop, file, currentLine, errInfo);
        } else {
            const auto tag = parseOptionalTag(tagText, nullptr, file,
                                              currentLine, errInfo);
            if (!tag.name.empty()) {
                if (tag.name == "br") {
                    if (parser.onBR) parser.onBR();
                } else if (tag.name == "b") {
                    if (parser.onB) parser.onB();
                } else if (tag.name == "i") {
                    if (parser.onI) parser.onI();
                } else if (tag.name == "u") {
                    if (parser.onU) parser.onU();
                } else if (tag.name == "s") {
                    if (parser.onS) parser.onS();
                } else if (tag.name == "shiftx") {
                    if (parser.onShiftx) parser.onShiftx(tag.tagValue);
                } else if (tag.name == "shifty") {
                    if (parser.onShifty) parser.onShifty(tag.tagValue);
                } else if (tag.name == "lineheight") {
                    if (parser.onLineheight) parser.onLineheight(tag.tagValue);
                } else if (tag.name == "font") {
                    invokeFont(tag, nullptr, file, currentLine, errInfo,
                               parser.onFont);
                }
            }
        }
        currentLine += newlineCount(tagText);
        offset = tagEnd;
    }
}

}  // namespace

void JptxParser::parse(StringView text, const props::CProps& prop,
                       StringView file, std::size_t line,
                       EffectBoosterError& errInfo) {
    parseJptxText(*this, text, &prop, file, line, errInfo);
}

void JptxParser::parse(StringView text, const props::CProps* prop,
                       StringView file, std::size_t line,
                       EffectBoosterError& errInfo) {
    parseJptxText(*this, text, prop, file, line, errInfo);
}

void Jptx::parse(const std::function<void(String, const JptxParam&)>& callback) {
    JptxParam param;
    param.lineheight = lineheight;
    param.face = fontface;
    param.color = fontcolor;
    param.pixels = fontpixels;

    std::int32_t boldDepth = 0;
    std::int32_t italicDepth = 0;
    std::int32_t underlineDepth = 0;
    std::int32_t strikeDepth = 0;
    std::vector<String> faceStack;
    std::vector<CRGB> colorStack;
    std::vector<std::int32_t> pixelsStack;

    JptxParser parser;
    // DのJptx.parseは、既定のautoline=trueでパーサーを構築す
    // る。解析されたJPTX設定はメタデータとして保持するが、ここでは適用しない。
    parser.onB = [&] {
        ++boldDepth;
        param.b = true;
    };
    parser.onEndB = [&] {
        --boldDepth;
        if (boldDepth <= 0) param.b = false;
    };
    parser.onI = [&] {
        ++italicDepth;
        param.i = true;
    };
    parser.onEndI = [&] {
        --italicDepth;
        if (italicDepth <= 0) param.i = false;
    };
    parser.onU = [&] {
        ++underlineDepth;
        param.u = true;
    };
    parser.onEndU = [&] {
        --underlineDepth;
        if (underlineDepth <= 0) param.u = false;
    };
    parser.onS = [&] {
        ++strikeDepth;
        param.s = true;
    };
    parser.onEndS = [&] {
        --strikeDepth;
        if (strikeDepth <= 0) param.s = false;
    };
    parser.onShiftx = [&](std::int32_t value) { param.shiftx = value; };
    parser.onShifty = [&](std::int32_t value) { param.shifty = value; };
    parser.onLineheight = [&](std::int32_t value) { param.lineheight = value; };
    parser.onFont = [&](String face, CRGB color, std::int32_t pixels) {
        if (face.empty()) face = param.face;
        if (color.r == 0xFFFFFFFFu) color = param.color;
        if (pixels == -1) pixels = param.pixels;
        faceStack.push_back(param.face);
        colorStack.push_back(param.color);
        pixelsStack.push_back(param.pixels);
        param.face = std::move(face);
        param.color = color;
        param.pixels = pixels;
    };
    parser.onEndFont = [&] {
        if (faceStack.empty()) return;
        param.face = faceStack.back();
        faceStack.pop_back();
        param.color = colorStack.back();
        colorStack.pop_back();
        param.pixels = pixelsStack.back();
        pixelsStack.pop_back();
    };
    parser.onBR = [&] {
        // Dは必須のonTextデリゲートを直接呼ぶ。ネイティブのstd::function
        // は空の場合があるため、安全に出力しない状態を維持する。
        if (callback) callback("\n", param);
        param.shiftx = 0;
        param.shifty = 0;
    };
    parser.onText = [&](String value) {
        if (callback) callback(std::move(value), param);
        param.shiftx = 0;
        param.shifty = 0;
    };

    EffectBoosterError errors;
    parser.parse(text, props, file, textLine, errors);
    if (!errors.errors().empty()) throw errors;
}

Jptx Jptx::load(const props::CProps& prop, StringView filePath) {
    Jptx result;
    result.file = String(filePath);
    result.props = &prop;
    EffectBoosterError errors;
    const auto lines = splitLinesNoTerm(readJPYFile(filePath, prop, errors));
    String text;
    bool init = false;
    bool textMode = false;
    bool textFirst = true;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto& current = lines[index];
        const auto lineNumber = index + 1;
        const auto line = trim(current);
        if (!textMode && !line.empty() && line.front() == ';') continue;
        if (!line.empty() && line.front() == '[' && line.back() == ']') {
            const auto label = trim(line.substr(1, line.size() - 2));
            if (label == "jptx:init") {
                if (!textMode) {
                    init = true;
                    textMode = false;
                    continue;
                }
            } else if (label == "jptx:begin") {
                if (!textMode) {
                    init = false;
                    textMode = true;
                    continue;
                }
            } else if (label == "jptx:end") {
                init = true;
                textMode = false;
                continue;
            }
        }
        if (init) {
            const auto equal = line.find('=');
            if (equal == String::npos) {
                errors.add(formatMessage(prop.requiredMsgs().jpyErrorInvalidLine, current),
                            String(filePath), lineNumber);
                continue;
            }
            const auto key = trim(line.substr(0, equal));
            const auto value = stripValue(line.substr(equal + 1));
            const auto command = lowerAscii(key);
            if (command == "backcolor") {
                result.backcolor = rgbVal(value, prop, filePath, lineNumber,
                                          errors);
            } else if (command == "backwidth") {
                result.backwidth = intVal(value, prop, filePath, lineNumber,
                                          errors);
            } else if (command == "backheight") {
                result.backheight = intVal(value, prop, filePath, lineNumber,
                                           errors);
            } else if (command == "autoline") {
                result.autoline = boolVal(value, prop, filePath, lineNumber,
                                          errors);
            } else if (command == "lineheight") {
                result.lineheight = intVal(value, prop, filePath, lineNumber,
                                           errors);
            } else if (command == "fontpixels") {
                result.fontpixels = intVal(value, prop, filePath, lineNumber,
                                           errors);
            } else if (command == "fontcolor") {
                result.fontcolor = rgbVal(value, prop, filePath, lineNumber,
                                          errors);
            } else if (command == "fontface") {
                result.fontface = strVal(value, prop, filePath, lineNumber,
                                         errors);
            } else if (command == "antialias") {
                result.antialias = intVal(value, prop, filePath, lineNumber,
                                          errors);
            } else if (command == "fonttransparent") {
                result.fonttransparent = boolVal(value, prop, filePath,
                                                 lineNumber, errors);
            } else {
                errors.add(formatMessage(prop.requiredMsgs().jpyErrorInvalidCommand, key),
                            String(filePath), lineNumber);
            }
        }
        if (textMode) {
            if (textFirst) {
                textFirst = false;
                result.textLine = lineNumber;
            } else {
                text.push_back('\n');
            }
            // readJPYFileはファイル全体を復号し、splitLinesNoTerm
            // はUTF-8を検証してから完全な改行区切りで分割する。そのため各行
            // は有効なUTF-8であり、Dの行ごとの文字コード代替処理にはここで
            // は到達しない。Dのcatch(Exception)はメモリ確保のError
            // を捕捉しないため、直接追加して確保・容量の失敗を伝播する。
            text += current;
        }
    }
    if (!text.empty() && text.back() == '\n') text.pop_back();
    result.text = std::move(text);
    if (!errors.errors().empty()) throw errors;
    return result;
}

String jptxText(StringView all) {
    String result;
    bool inText = false;
    for (const auto& line : splitLinesKeep(all)) {
        const auto withoutTerminator = chomp(line);
        if (!inText && cixEqual(withoutTerminator, "[jptx:begin]")) {
            inText = true;
        } else if (inText && cixEqual(withoutTerminator, "[jptx:end]")) {
            inText = false;
        } else if (inText) {
            result += line;
        }
    }
    return result;
}

String jptxText(StringView all, StringView replacement) {
    String result;
    bool inText = false;
    bool put = false;
    for (const auto& line : splitLinesKeep(all)) {
        const auto withoutTerminator = chomp(line);
        if (!inText && cixEqual(withoutTerminator, "[jptx:begin]")) {
            inText = true;
            result += line;
            if (!put) {
                result += replacement;
                put = true;
            }
        } else if (inText && cixEqual(withoutTerminator, "[jptx:end]")) {
            inText = false;
            result += line;
        } else if (!inText) {
            result += line;
        }
    }
    return result;
}

lib::Owner<Jpdc> Jpdc::load(const props::CProps& prop,
                             StringView scenarioPath, StringView filePath) {
    auto result = std::make_shared<Jpdc>();
    result->sPath = String(scenarioPath);
    result->jpdcPath = String(filePath);
    EffectBoosterError errors;
    result->lines = splitLinesKeep(
        readJPYFile(filePath, prop, errors, result->isSJIS));
    bool init = false;
    for (std::size_t index = 0; index < result->lines.size(); ++index) {
        const String original = result->lines[index];
        const auto line = trim(chomp(original));
        const auto lineNumber = index + 1;
        if (line.empty() || line.front() == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            if (trim(line.substr(1, line.size() - 2)) == "jpdc:init") {
                init = true;
                continue;
            }
        }
        if (!init) continue;
        const auto equal = line.find('=');
        if (equal == String::npos) {
            errors.add(formatMessage(prop.requiredMsgs().jpyErrorInvalidLine, line),
                        String(filePath), lineNumber);
            continue;
        }
        const auto key = trim(line.substr(0, equal));
        const auto value = stripValue(line.substr(equal + 1));
        const auto command = lowerAscii(key);
        if (command == "clip") {
            result->clip = rectVal(value, prop, filePath, lineNumber, errors);
        } else if (command == "copymode") {
            result->copymode = enumVal<Copymode>(value, prop, filePath,
                                                 lineNumber, errors);
        } else if (command == "savefilename") {
            result->saveFileName =
                strVal(value, prop, filePath, lineNumber, errors);
            result->savefilenameIndex = static_cast<std::ptrdiff_t>(index);
            result->savefilenameLine = original;
        } else if (command == "savecomment") {
            result->savecomment =
                strVal(value, prop, filePath, lineNumber, errors);
        } else {
            errors.add(formatMessage(prop.requiredMsgs().jpyErrorInvalidCommand, key),
                        String(filePath), lineNumber);
        }
    }
    if (!errors.errors().empty()) throw errors;
    result->path(result->toMaterialPath());
    result->needUpdate = false;
    return result;
}

String Jpdc::cwxPath(bool) { return {}; }

Jpdc::Jpdc()
    : path_user_(std::make_shared<CallbackPathUser>(
          static_cast<path::CWXPath*>(this),
          [this](const usecounter::PathId& value) {
              return change(value);
          })) {}

lib::Owner<path::CWXPath> Jpdc::findCWXPath(StringView value) {
    if (!path::cpempty(value)) return {};
    try {
        return std::static_pointer_cast<path::CWXPath>(shared_from_this());
    } catch (const std::bad_weak_ptr&) {
        return {};
    }
}

lib::Array<lib::Observer<path::CWXPath>> Jpdc::cwxChilds() { return {}; }

lib::Observer<path::CWXPath> Jpdc::cwxParent() { return {}; }

void Jpdc::changed() {}

bool Jpdc::change(StringView newValue) {
    return change(usecounter::toPathId(newValue));
}

bool Jpdc::change(usecounter::PathId newValue) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    // Dのsuper.changeに合わせ、PathId全体を維持してコールバ
    // ックの橋渡しを経由しない。Dは基底の結果を無視し、常にtrueを返す。
    static_cast<void>(path_user_->PathUser::change(newValue));
    const auto newName = fromMaterialPath(String(newValue));
    if (newName != saveFileName) {
        saveFileName = newName;
        needUpdate = true;
    }
    return true;
}

void Jpdc::path(StringView value) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    const String material(value);
    // Dは表示ファイル名の計算前にPathUserとカウンターを更新する。
    path_user_->path(material);
    const auto newName = fromMaterialPath(material);
    if (newName != saveFileName) {
        saveFileName = newName;
        needUpdate = true;
    }
}

String Jpdc::path() const noexcept {
    return path_user_ ? path_user_->path() : String{};
}
void Jpdc::setUseCounter(const usecounter::CounterOwner& uc) {
    path_user_->setUseCounter(uc);
}
void Jpdc::setUseCounter(usecounter::UseCounter* uc) {
    if (!uc) { removeUseCounter(); return; }
    try { setUseCounter(uc->shared_from_this()); }
    catch (const std::bad_weak_ptr&) { removeUseCounter(); }
}
usecounter::UseCounter* Jpdc::useCounter() const noexcept {
    return path_user_->useCounter().get();
}
void Jpdc::removeUseCounter() { path_user_->removeUseCounter(); }

String Jpdc::toMaterialPath() const {
    return toMaterialPathImpl(saveFileName, sPath, jpdcPath);
}

String Jpdc::toMaterialPathImpl(StringView filename, StringView scenarioPath,
                                 StringView filePath) {
    return materialPathFor(filename, scenarioPath, filePath);
}

String Jpdc::fromMaterialPath(StringView value) const {
    return fromMaterialPathImpl(value, sPath, jpdcPath);
}

String Jpdc::fromMaterialPathImpl(StringView value, StringView scenarioPath,
                                   StringView filePath) {
    const auto relPath = [](StringView base, StringView target) {
        if (base.empty()) return String(target);
        return relativePath(target, base);
    };
    const auto fileRel = relativePath(filePath, scenarioPath);
    return buildPath(relPath(dirName(fileRel), dirName(value)), baseName(value));
}

bool Jpdc::renameFile(StringView oldPath, StringView newPath) {
    const auto file = normalizedAbsolute(jpdcPath);
    const auto old = normalizedAbsolute(oldPath);
    if (!utils::fnstartsWith(file, old)) return false;
    String renamed(newPath);
    if (!utils::cfnmatch(file, old)) renamed = buildPath(newPath, relativePath(file, old));
    const auto material = toMaterialPath();
    jpdcPath = std::move(renamed);
    if (!material.empty()) path(material);
    return true;
}

bool Jpdc::updateJpdcFile(const props::CProps& prop, bool rewrite,
                          const lib::Owner<FileSync>& sync) {
    (void)prop;
    bool update = false;
    if (needUpdate) {
        if (savefilenameIndex != -1) {
            const auto equal = savefilenameLine.find('=');
            if (equal != String::npos &&
                static_cast<std::size_t>(savefilenameIndex) < lines.size()) {
                const auto ret = savefilenameLine.substr(
                    chomp(savefilenameLine).size());
                lines[static_cast<std::size_t>(savefilenameIndex)] =
                    savefilenameLine.substr(0, equal + 1) + saveFileName + ret;
                update = true;
            }
        }
        needUpdate = false;
    }
    if (update && rewrite) {
        String output;
        for (const auto& line : lines) output += line;
        writeJPYFile(jpdcPath, output, isSJIS, sync);
    }
    return update && rewrite;
}

}  // namespace cwxeditor::cwx::jpy
