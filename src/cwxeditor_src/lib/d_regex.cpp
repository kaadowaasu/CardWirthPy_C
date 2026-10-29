#include "cwxeditor_src/lib/d_regex.hpp"
#include "cwxeditor_src/lib/d_regex_properties.hpp"
#define PCRE2_CODE_UNIT_WIDTH 32
#include <pcre2.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <map>
#include <mutex>
#include <optional>
#include <memory>

namespace cwxeditor::lib {
namespace {
using Text = std::u32string;
Text foldSet(Text atom, bool property);
Text folded(Text atom, bool ignore_case) { return ignore_case ? foldSet(std::move(atom), false) : std::move(atom); }
bool white(char32_t c) {
    return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xa0 || c == 0x1680 ||
        (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
Text hex(char32_t value) {
    Text result; do { const auto n = value & 15; result.push_back(U"0123456789abcdef"[n]); value >>= 4; } while (value);
    std::reverse(result.begin(), result.end()); return U"\\x{" + result + U"}";
}
Text propertyRanges(std::u32string_view source) {
    std::string bytes;
    for (const auto c : source) {
        if (c == U'-' || c == U'_' || c == U' ') continue;
        bytes.push_back(static_cast<char>(static_cast<unsigned char>(c >= U'A' && c <= U'Z' ? c + 32 : c)));
    }
    // parsePropertySpec は、残りの各 dchar を先に char へ変換し
    // て MAX_PROPERTY を検査し、 その後 comparePropertyName
    // が復号後の White_Space を除く。 Unicode空白を直接除くと、U+3000
    // （変換後NUL）を誤って無視したり、U+0120（変換後ASCII空白）を拒否したりす
    // る。 厳密な共通のメッセージUTF-8変換とは分け、D版固有の名前の扱いを維持する。
    if (bytes.empty() || bytes.size() >= 128) throw std::invalid_argument("Invalid D regex property name");
    Text normalized;
    for (std::size_t at = 0; at < bytes.size();) {
        const std::string_view remaining(bytes.data() + at, bytes.size() - at);
        bool skipped{};
        for (const auto whitespace : d_regex_data::propertyNameWhitespace) if (remaining.starts_with(whitespace)) { at += whitespace.size(); skipped = true; break; }
        if (skipped) continue;
        const auto c = static_cast<unsigned char>(bytes[at++]);
        if (c == '-' || c == '_') continue;
        // D版の公開名はASCII。 それ以外の復号後の非空白文字や、char
        // 変換で残った不正なUTF-8はプロパティ名にはできない。
        if (c >= 128) throw std::invalid_argument("Invalid D regex property name");
        normalized.push_back(c >= U'A' && c <= U'Z' ? c + 32 : c);
    }
    if (normalized.empty()) throw std::invalid_argument("Invalid D regex property name");
    const auto& sets = d_regex_data::properties;
    const auto found = std::lower_bound(std::begin(sets), std::end(sets), normalized, [](const auto& item, const auto& name) { return item.name < name; });
    if (found == std::end(sets) || found->name != normalized) throw std::invalid_argument("Unknown D regex property");
    // D版のプロパティ名と区間には文字体系や In+ブロックも
    // あり、PCREの形式とは異なる。 PCRE・OSへ分類や
    // 名前を委ねず、固定した元の区間データをここで復号する。
    const auto intervals = found->intervals; std::size_t at{}; char32_t base{}; Text result;
    auto number = [&] {
        const auto first = static_cast<unsigned char>(intervals[at++]);
        std::uint32_t value = first;
        if (first & 0x80) {
            value = first & 31; const auto extra = ((first >> 5) & 1) + 1;
            for (int n = 0; n < extra; ++n) value = value * 256 + static_cast<unsigned char>(intervals[at++]);
        }
        return value;
    };
    while (at < intervals.size()) {
        const char32_t start = base + number(); const char32_t end = at == intervals.size() ? 0x110000 : start + number();
        result += hex(start); if (end != start + 1) result += U"-" + hex(end - 1);
        base = end;
    }
    return result;
}
const Text& word() { static const Text ranges = propertyRanges(U"Alphabetic") + propertyRanges(U"M") + propertyRanges(U"Nd") + propertyRanges(U"Pc"); return ranges; }
void requireNonemptySet(const Text& set);
Text propertySet(std::u32string_view name, bool negate, bool fold) {
    Text set = U"[" + propertyRanges(name) + U"]";
    // D版の caseEnclose と同様、LCからの大小文字の対応を加えてからプロパティ
    // を否定する。 否定集合にPCREの大小文字無視を適用すると、この順序と意味が逆転する。
    if (fold) set = foldSet(std::move(set), true);
    if (negate) set = U"(?!" + set + U")[\\s\\S]";
    requireNonemptySet(set);
    // 条件検査と一つのスカラー値の消費で、元の集合アトム一つを構成する。 後続の
    // 量指定子は、ワイルドカードのスカラーだけでなく両方を繰り返す必要がある。
    return U"(?:" + set + U")";
}
Text escape(std::u32string_view source, std::size_t& i, bool inside_class, bool ignore_case = false) {
    if (++i == source.size()) throw std::invalid_argument("Unfinished D regex escape");
    const auto c = source[i++];
    if (c == U'p' || c == U'P') {
        if (i == source.size()) throw std::invalid_argument("Unfinished D regex property");
        if (source[i] != U'{') {
            if (source[i] >= 0x80) throw std::invalid_argument("Invalid D regex property");
            return propertySet(source.substr(i++, 1), c == U'P', ignore_case);
        }
        const auto end = source.find(U'}', i + 1);
        if (end == source.npos) throw std::invalid_argument("Unfinished D regex property");
        auto result = propertySet(source.substr(i + 1, end - i - 1), c == U'P', ignore_case); i = end + 1; return result;
    }
    if (c == U'x' || c == U'u' || c == U'U') {
        const std::size_t digits = c == U'x' ? 2 : c == U'u' ? 4 : 8;
        if (digits > source.size() - i) throw std::invalid_argument("Short D regex hex escape");
        std::uint32_t value{};
        for (std::size_t n = 0; n < digits; ++n) {
            const auto h = source[i++]; const auto d = h >= U'0' && h <= U'9' ? h - U'0' : h >= U'a' && h <= U'f' ? h - U'a' + 10 : h >= U'A' && h <= U'F' ? h - U'A' + 10 : 16;
            if (d == 16) throw std::invalid_argument("Invalid D regex hex escape"); value = (value << 4) | d;
        }
        if (value > 0x10ffff) throw std::invalid_argument("Invalid D regex codepoint");
        return hex(value);
    }
    if (c == U'0') { if (inside_class) throw std::invalid_argument("Invalid D regex class escape"); return U"\\x{0}"; }
    if (!inside_class && c >= U'1' && c <= U'9') {
        Text result = U"\\" + Text(1, c);
        while (i < source.size() && source[i] >= U'0' && source[i] <= U'9') result.push_back(source[i++]);
        return result;
    }
    const auto control_index = Text(U"fnrtv").find(c);
    if (control_index != Text::npos) return hex(U"\f\n\r\t\v"[control_index]);
    if (Text(U"dDsS").find(c) != Text::npos) return U"[" + Text(c == U'D' || c == U'S' ? U"^" : U"") + propertyRanges(c == U'd' || c == U'D' ? U"Nd" : U"White_Space") + U"]";
    if (c == U'c') {
        if (i == source.size()) throw std::invalid_argument("Short D regex control escape");
        const auto letter = source[i++];
        if (!((letter >= U'a' && letter <= U'z') || (letter >= U'A' && letter <= U'Z'))) throw std::invalid_argument("Only letters are allowed after D regex control escape");
        return hex(letter & 0x1f);
    }
    // D版と同様、単独の未知のエスケープはその文字自身として扱い、 PCRE
    // だけのエスケープ（\A、\K、\Q、\Rなど）を公開しない。
    if (inside_class && Text(U"[]\\^$.|?,-;:#&%/<>`*+(){}~").find(c) == Text::npos) throw std::invalid_argument("Invalid D regex class escape");
    return hex(c);
}
Text classUnion(Text left, Text right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    return U"(?:" + left + U"|" + right + U")";
}
Text classComplement(const Text& set) { return U"(?!(?:" + set + U"))[\\s\\S]"; }
struct ClassElement { Text atom; std::optional<char32_t> literal; };
ClassElement classElement(std::u32string_view source, std::size_t& i, bool ignore_case) {
    if (source[i] != U'\\') { const auto c = source[i++]; return {hex(c), c}; }
    if (i + 1 < source.size() && (source[i + 1] == U'w' || source[i + 1] == U'W')) {
        const auto invert = source[i + 1] == U'W'; i += 2; return {U"[" + Text(invert ? U"^" : U"") + word() + U"]", std::nullopt};
    }
    auto atom = escape(source, i, true, ignore_case);
    std::optional<char32_t> literal;
    if (atom.starts_with(U"\\x{")) {
        char32_t value{};
        for (std::size_t n = 3; n + 1 < atom.size(); ++n) value = value * 16 + (atom[n] <= U'9' ? atom[n] - U'0' : atom[n] - U'a' + 10);
        literal = value;
    }
    return {std::move(atom), literal};
}
void requireNonemptySet(const Text& set) {
    static std::mutex mutex;
    static std::map<Text, bool> known;
    std::lock_guard lock(mutex);
    if (const auto found = known.find(set); found != known.end()) {
        if (!found->second) throw std::invalid_argument("Empty D regex CodepointSet");
        return;
    }
    int error{}; PCRE2_SIZE offset{};
    const auto code = std::unique_ptr<pcre2_code, decltype(&pcre2_code_free)>(pcre2_compile(reinterpret_cast<PCRE2_SPTR>(set.data()), set.size(), PCRE2_UCP, &error, &offset, nullptr), pcre2_code_free);
    if (!code) throw std::invalid_argument("Invalid D regex CodepointSet");
    const auto data = std::unique_ptr<pcre2_match_data, decltype(&pcre2_match_data_free)>(pcre2_match_data_create_from_pattern(code.get(), nullptr), pcre2_match_data_free);
    if (!data) throw std::bad_alloc();
    const auto context = std::unique_ptr<pcre2_match_context, decltype(&pcre2_match_context_free)>(pcre2_match_context_create(nullptr), pcre2_match_context_free);
    if (!context) throw std::bad_alloc();
    pcre2_set_match_limit(context.get(), UINT32_MAX);
    // Phobos は最終的に空となる CodepointSet をコンパイル時
    // に拒否する。 変換した単一コードポイントの所属判定を、D版のコードポイント
    // 範囲全体で調べる。 下の dstring 表現に合わせ、32ビットのコー
    // ド単位ではサロゲートの分類も含める。 OSの分類に頼らず共通処理で行う。
    static const Text domain = [] { Text value; value.reserve(0x110000); for (char32_t c = 0; c < 0x110000; ++c) value.push_back(c); return value; }();
    const auto matched = pcre2_match(code.get(), reinterpret_cast<PCRE2_SPTR>(domain.data()), domain.size(), 0, 0, data.get(), context.get());
    if (matched < 0 && matched != PCRE2_ERROR_NOMATCH) throw std::runtime_error("D regex CodepointSet validation failed");
    // 公開の呼び出し元は任意のパターンを渡せるため、この検証キャッシュには上限を設ける。
    if (known.size() >= 128) known.clear();
    known.emplace(set, matched >= 0);
    if (matched < 0) throw std::invalid_argument("Empty D regex CodepointSet");
}
Text foldSet(Text atom, bool property) {
    static std::mutex mutex;
    static std::map<std::pair<Text, bool>, Text> cache;
    std::lock_guard lock(mutex);
    const auto key = std::make_pair(atom, property);
    if (const auto found = cache.find(key); found != cache.end()) return found->second;
    int error{}; PCRE2_SIZE offset{};
    const auto code = std::unique_ptr<pcre2_code, decltype(&pcre2_code_free)>(pcre2_compile(reinterpret_cast<PCRE2_SPTR>(atom.data()), atom.size(), PCRE2_UCP, &error, &offset, nullptr), pcre2_code_free);
    if (!code) throw std::invalid_argument("Invalid D regex fold set");
    const auto data = std::unique_ptr<pcre2_match_data, decltype(&pcre2_match_data_free)>(pcre2_match_data_create_from_pattern(code.get(), nullptr), pcre2_match_data_free);
    if (!data) throw std::bad_alloc();
    static const Text domain = [] { Text text; for (const auto& entry : d_regex_data::simpleFolds) text.push_back(entry.scalar); return text; }();
    Text additions;
    for (std::size_t at = 0; at < domain.size();) {
        const auto matched = pcre2_match(code.get(), reinterpret_cast<PCRE2_SPTR>(domain.data()), domain.size(), at, 0, data.get(), nullptr);
        if (matched == PCRE2_ERROR_NOMATCH) break;
        if (matched < 0) throw std::runtime_error("D regex fold set match failed");
        const auto offsets = pcre2_get_ovector_pointer(data.get());
        const auto& entry = d_regex_data::simpleFolds[offsets[0]];
        if (!property || entry.cased) additions.append(entry.group);
        at = offsets[1];
    }
    std::sort(additions.begin(), additions.end()); additions.erase(std::unique(additions.begin(), additions.end()), additions.end());
    if (!additions.empty()) {
        Text ranges; for (const auto c : additions) ranges += hex(c);
        atom = U"(?:" + atom + U"|[" + ranges + U"])";
    }
    // addWithFlags が走査する simpleCaseFoldings(0) の単一
    // 要素の Range は空。 プロパティの caseEnclose は、NULを含む元の集
    // 合を保持する。 PCREのUnicode版のデータではなく、固定したD版の対応表を使う。
    if (!property) atom = U"(?!\\x{0})" + atom;
    if (cache.size() >= 128) cache.clear();
    cache.emplace(key, atom); return atom;
}
Text literal(char32_t scalar, bool ignore_case) {
    if (!ignore_case) return hex(scalar);
    const auto& folds = d_regex_data::simpleFolds;
    const auto found = std::lower_bound(std::begin(folds), std::end(folds), scalar, [](const auto& entry, char32_t c) { return entry.scalar < c; });
    if (found == std::end(folds) || found->scalar != scalar) return scalar == 0 ? Text{} : hex(scalar);
    Text ranges; for (const auto c : found->group) ranges += hex(c);
    return U"[" + ranges + U"]";
}
Text characterClass(std::u32string_view source, std::size_t& i, bool ignore_case) {
    ++i; const bool negate = i < source.size() && source[i] == U'^'; if (negate) ++i;
    Text current; std::vector<Text> values; std::vector<char32_t> operators; bool first = true;
    auto apply = [&] {
        if (values.size() < 2 || operators.empty()) throw std::invalid_argument("Missing D regex class operand");
        const auto op = operators.back(); operators.pop_back(); const auto right = std::move(values.back()); values.pop_back(); auto& left = values.back();
        if (op == U'|') left = classUnion(std::move(left), right);
        else if (op == U'&') left = U"(?=" + left + U")" + right;
        else if (op == U'-') left = U"(?!" + right + U")" + left;
        else left = classUnion(U"(?!" + right + U")" + left, U"(?!" + left + U")" + right);
    };
    // D版の入れ子の CodepointSet と二重記号の演算に
    // は、独自の評価順序がある。 保留中の和集合は次の演算より先に
    // まとめる。 PCREの異なる拡張文字クラスの文法・優先順位や
    // 、二文字の消費を使わず、単一スカラーの条件検査として表す。
    while (i < source.size() && (source[i] != U']' || first)) {
        first = false;
        const auto c = source[i];
        if (c == U'[') { current = classUnion(std::move(current), characterClass(source, i, ignore_case)); continue; }
        if (i + 1 < source.size() && source[i + 1] == c && Text(U"|&-~").find(c) != Text::npos) {
            if (current.empty()) throw std::invalid_argument("Missing D regex class operand"); values.push_back(std::move(current)); current.clear();
            while (!operators.empty() && operators.back() == U'|') apply();
            operators.push_back(c); i += 2; continue;
        }
        auto element = classElement(source, i, ignore_case);
        bool fold = ignore_case && element.literal.has_value();
        if (element.literal) {
            if (i + 1 < source.size() && source[i] == U'-' && source[i + 1] != U']' && source[i + 1] != U'[' && source[i + 1] != U'-') {
                ++i; const bool escaped_end = source[i] == U'\\'; const auto end = classElement(source, i, ignore_case);
                if (!end.literal || *end.literal < *element.literal) throw std::invalid_argument("Invalid D regex class range");
                element.atom = U"[" + hex(*element.literal) + U"-" + hex(*end.literal) + U"]";
                // D版の CharDashEscape と同様、区間は大小文字の対応を加えずに追加
                // する。 通常の端点では、各コードポイントに addWithFlags を使う。
                fold = ignore_case && !escaped_end;
            } else {
                // 次が別のエスケープの場合、State.Char は最後のリテラルをそのまま追加する。
                fold = ignore_case && !(i < source.size() && source[i] == U'\\');
            }
        }
        current = classUnion(std::move(current), folded(std::move(element.atom), fold));
    }
    if (i == source.size()) throw std::invalid_argument("Unfinished D regex character class");
    ++i; if (current.empty()) throw std::invalid_argument("Missing D regex class operand"); values.push_back(std::move(current));
    while (!operators.empty()) apply();
    if (values.size() != 1) throw std::invalid_argument("Invalid D regex class operands");
    return U"(?:" + (negate ? classComplement(values.front()) : std::move(values.front())) + U")";
}
struct PhysicalCapture { std::uint32_t actual, begin; };
struct RepeatSpec {
    std::uint32_t minimum, maximum;
    bool unbounded;
    std::uint32_t minimum_width;
    std::uint32_t assertion_scope{};
    std::vector<std::uint32_t> parents;
};
struct Translation { Text pattern; std::vector<std::vector<PhysicalCapture>> captures; std::vector<RepeatSpec> repeats; };
std::uint32_t atomMinimumWidth(const Text& atom) {
    int error{}; PCRE2_SIZE offset{};
    const auto code = std::unique_ptr<pcre2_code, decltype(&pcre2_code_free)>(pcre2_compile(reinterpret_cast<PCRE2_SPTR>(atom.data()), atom.size(), PCRE2_UCP, &error, &offset, nullptr), pcre2_code_free);
    std::uint32_t width{};
    if (code) pcre2_pattern_info(code.get(), PCRE2_INFO_MINLENGTH, &width);
    return width;
}
Translation translate(std::u32string_view pattern) {
    Text result; bool ignore_case = true, multiline = false, dot_all = false, freeform = false;
    bool can_repeat = false;
    std::size_t atom_start{}; std::uint32_t atom_base{};
    struct Group { bool lookaround; std::optional<std::uint32_t> capture; std::uint32_t global; std::size_t start; std::uint32_t base; std::uint32_t scope; };
    std::vector<Group> groups;
    std::vector<std::uint32_t> counts{1}; // 外側のグループ 0 は一致全体を表す。
    std::vector<std::vector<PhysicalCapture>> captures{{{0, 0}}};
    std::uint32_t actual{};
    std::vector<RepeatSpec> repeats;
    std::uint32_t assertion_count{};
    struct BudgetRepeat {
        std::size_t start;
        std::uint32_t minimum, maximum;
        bool unbounded;
        std::vector<BudgetRepeat> children;
    };
    std::vector<BudgetRepeat> budget;
    auto recordBudget = [&](std::uint32_t minimum, std::uint32_t maximum, bool unbounded) {
        BudgetRepeat node{atom_start, minimum, maximum, unbounded, {}};
        while (!budget.empty() && budget.back().start >= atom_start) {
            node.children.push_back(std::move(budget.back())); budget.pop_back();
        }
        std::reverse(node.children.begin(), node.children.end());
        budget.push_back(std::move(node));
        std::uint64_t cumulative{};
        // Phobos の後処理は、小さい量指定子や条件検査も含め
        // 、有限の中間表現の繰り返しをすべて数える。 子の基数には
        // 、祖先の最大値とカウンターのゼロ状態を含める。 必須の接
        // 頭部分を持つ無限反復は、末尾にアトムの中間表現を複製し
        // 、 その複製は接頭部分のリセット後に独自の寄与を持つ。
        auto walk = [&](auto&& self, const BudgetRepeat& item, std::uint64_t range) -> void {
            auto children = [&](std::uint64_t child_range) {
                for (const auto& child : item.children) self(self, child, child_range);
            };
            if (!item.unbounded && item.minimum == 1 && item.maximum == 1) { children(range); return; }
            const auto maximum = item.unbounded ? item.minimum : item.maximum;
            if (!item.unbounded || item.minimum > 1) {
                const auto contribution = range * maximum;
                cumulative += contribution;
                if (cumulative >= (std::uint64_t{1} << 20)) throw std::invalid_argument("D regex cumulative repetition length limit exceeded");
                children(contribution + range);
            } else if (item.minimum) children(range);
            if (item.unbounded) children(range);
        };
        for (const auto& item : budget) walk(walk, item, 1);
    };
    const bool has_lookbehind = pattern.find(U"(?<=") != pattern.npos || pattern.find(U"(?<!") != pattern.npos;
    auto decimal = [](std::uint32_t number) { Text digits; for (const auto c : std::to_string(number)) digits.push_back(c); return digits; };
    auto open = [&](bool capture, bool lookaround) {
        const auto start = result.size(); const auto base = actual;
        if (lookaround) counts.push_back(0);
        const auto local = capture ? std::optional<std::uint32_t>(counts.back()++) : std::nullopt;
        std::uint32_t global{};
        if (capture) {
            for (const auto count : counts) global += count;
            --global;
            captures.push_back({{++actual, ++actual}});
        }
        groups.push_back({lookaround, local, global, start, base, lookaround ? ++assertion_count : 0});
        // この内部の空キャプチャーで、元のD版のキャプチャー番号を変えず、GroupEnd
        // 前でも現在の GroupStart の位置を取得する。 D版
        // では GroupStart が全選択肢より前にあるため、 内部
        // の印は最初の分岐だけでなく、入れ子の分岐グループの外に置く。
        return capture ? Text(U"()(?:") : Text{};
    };
    auto lowerRepeat = [&](std::uint32_t minimum, std::uint32_t maximum, bool unbounded, bool lazy) {
        const auto atom = result.substr(atom_start);
        std::vector<std::uint32_t> children;
        for (std::size_t at = 0; (at = atom.find(U"(?C\"Q,", at)) != Text::npos;) {
            at += 6; std::uint32_t child{};
            while (at < atom.size() && atom[at] >= U'0' && atom[at] <= U'9') child = child * 10 + atom[at++] - U'0';
            if (std::find(children.begin(), children.end(), child) == children.end()) children.push_back(child);
        }
        if (minimum <= 65535 && (unbounded || maximum <= 65535) && children.empty()) return false;
        // D版の {1,1} には RepeatEnd・カウンターがない。 変
        // 換したアトムを囲む他の有限反復は、? や小さい波括弧の回数も含め、
        // 同じ記録を使い、符号化されたカウンターの文脈を参照できるようにする。
        if (!unbounded && minimum == 1 && maximum == 1) return false;
        result.resize(atom_start);
        const auto index = static_cast<std::uint32_t>(repeats.size());
        const auto id = decimal(index);
        std::uint32_t scope{};
        for (auto group = groups.rbegin(); group != groups.rend(); ++group) if (group->lookaround) { scope = group->scope; break; }
        repeats.push_back({minimum, maximum, unbounded,
            atom.find(U"(?C\"r,") == Text::npos && !has_lookbehind ? atomMinimumWidth(atom) : 0, scope, {}});
        for (const auto child : children) if (repeats[child].assertion_scope == scope) repeats[child].parents.push_back(index);
        auto call = [&](char32_t mode) { return U"(?C\"" + Text(1, mode) + U"," + id + U"\")"; };
        const auto consume = atom + call(U'I'), stop = call(U'S');
        const auto choice = lazy ? stop + U"|" + consume : consume + U"|" + stop;
        const auto active = U"(?(?=" + call(U'A') + U")";
        Text body = active + U"(?:" + call(U'T') + U"(?:" + choice + U")|" + call(U'U') + U"(*FAIL))|)";
        // ネイティブの回数制限付きブロックは、元のアトム・キャプチャー枠を一
        // つ保持する。 各二進ブロックに条件を付け、早期終了時は残りのブロッ
        // ク全体を飛ばす。 終了のたびに最大65535回の空の埋め合わせを実
        // 行しない。 無限反復の試行を二回加え、ブロック全体が空でも InfiniteEnd
        // が初回の印を設定し、再訪を拒否できるようにする。
        std::uint64_t capacity = unbounded ? std::uint64_t(minimum) + 2 : maximum;
        // 回数ゼロでは実行を飛ばすが、元のネイティブのキャプチャ
        // ー枠は宣言し、後続のD版のグループIDをずらさない。
        if (!capacity) body = U"(?:" + body + U"){0}";
        while (capacity > 1) {
            body = active + U"(?:" + body + U"){2}|)";
            capacity = (capacity + 1) / 2;
        }
        if (unbounded) body = U"(?:" + body + U"){1,}";
        result += U"(?:" + call(U'Q') + body + call(U'F') + U"|" + call(U'V') + U"(*FAIL))";
        return true;
    };
    for (std::size_t i = 0; i < pattern.size();) {
        const auto c = pattern[i];
        if (freeform && white(c)) { ++i; continue; }
        if (c == U'(' && i + 2 < pattern.size() && pattern[i + 1] == U'?') {
            const auto start = i; can_repeat = false;
            if (pattern[i + 2] == U'#') { const auto end = pattern.find(U')', i + 3); if (end == pattern.npos) throw std::invalid_argument("Unfinished D regex comment"); i = end + 1; continue; }
            if (pattern[i + 2] == U':' || pattern[i + 2] == U'=' || pattern[i + 2] == U'!') { open(false, pattern[i + 2] != U':'); result += pattern.substr(i, 3); i += 3; continue; }
            if (pattern[i + 2] == U'<' && i + 3 < pattern.size() && (pattern[i + 3] == U'=' || pattern[i + 3] == U'!')) { open(false, true); result += pattern.substr(i, 4); i += 4; continue; }
            if (pattern[i + 2] == U'P' && i + 3 < pattern.size() && pattern[i + 3] == U'<') { const auto end = pattern.find(U'>', i + 4); if (end == pattern.npos) throw std::invalid_argument("Unfinished D regex group name"); const auto marker = open(true, false); result += pattern.substr(i, end - i + 1); result += marker; i = end + 1; continue; }
            i += 2; bool enable = true;
            while (i < pattern.size() && pattern[i] != U')') {
                const auto flag = pattern[i++];
                if (flag == U'-' && enable) { enable = false; continue; }
                auto target = flag == U'i' ? &ignore_case : flag == U'm' ? &multiline : flag == U's' ? &dot_all : flag == U'x' ? &freeform : nullptr;
                if (!target) throw std::invalid_argument("Invalid D regex flags at " + std::to_string(start));
                *target = enable;
            }
            if (i == pattern.size()) throw std::invalid_argument("Unfinished D regex flags"); ++i; continue;
        }
        if (c == U'[') { atom_start = result.size(); atom_base = actual; auto set = characterClass(pattern, i, ignore_case); requireNonemptySet(set); result += set; can_repeat = true; continue; }
        if (c == U'\\') {
            atom_start = result.size(); atom_base = actual;
            can_repeat = true;
            if (i + 1 < pattern.size() && pattern[i + 1] >= U'1' && pattern[i + 1] <= U'9') {
                std::uint32_t maximum{}; for (const auto count : counts) maximum += count;
                auto reference = static_cast<std::uint32_t>(pattern[++i] - U'0');
                if (reference >= maximum) throw std::invalid_argument("D regex backref to unseen group");
                auto advance = [&] { ++i; if (freeform) while (i < pattern.size() && white(pattern[i])) ++i; };
                advance();
                // D版のPerl形式の曖昧さ解消と同様、上限を超える最後の桁を消費し
                // てから、 位置を戻さず参照番号を10で割る。 先読み・後読みのカウ
                // ンターは groupStack と同様にゼロで始まり、閉じるとき
                // に統合する。 未閉鎖グループの拒否には、そのローカルIDを使う。
                while (reference < maximum && i < pattern.size() && pattern[i] >= U'0' && pattern[i] <= U'9') { reference = reference * 10 + pattern[i] - U'0'; advance(); }
                if (reference >= maximum) reference /= 10;
                if (std::any_of(groups.begin(), groups.end(), [&](const Group& group) { return group.capture && *group.capture == reference; })) throw std::invalid_argument("D regex backref to open group");
                {
                    const auto start = ++actual;
                    const bool is_open = std::any_of(groups.begin(), groups.end(), [&](const Group& group) { return group.global == reference; });
                    const auto fields = decimal(reference) + U"," + Text(is_open ? U"1" : U"0") + U"," + decimal(start);
                    // 許可された内側・外側の未閉鎖参照は、PCREの前回閉じ
                    // た部分文字列ではなく、D版の現在の開始位置と以前の終了
                    // 位置を使う。 内部のコールアウトでその範囲を正確に比較
                    // し、不正な範囲の RangeError は制御可能なC++
                    // 例外に変換する。 論理キャプチャーの物理的な複製がす
                    // べて判明した、変換後に解決する。 通常の閉じた参照に
                    // は、PCREの固定長・バックトラックの機能を使う。
                    result += U"(?:()(?C\"r," + fields + U"\"))";
                }
                continue;
            }
            if (i + 1 < pattern.size() && (pattern[i + 1] == U'w' || pattern[i + 1] == U'W')) { result += U"[" + Text(pattern[i + 1] == U'W' ? U"^" : U"") + word() + U"]"; i += 2; continue; }
            if (i + 1 < pattern.size() && (pattern[i + 1] == U'b' || pattern[i + 1] == U'B')) {
                const auto w = U"[" + word() + U"]";
                const auto boundary = U"(?:(?<!" + w + U")(?=" + w + U")|(?<=" + w + U")(?!" + w + U"))";
                result += pattern[i + 1] == U'b' ? boundary : U"(?!" + boundary + U")"; i += 2; continue;
            }
            // Phobos と同様、エスケープしたリテラル・十六進・制御文字は IR.Char
            // として扱い、 暗黙の i フラグがあっても後方参照は完全一致で比
            // 較する。 ここで大小文字の展開を行うのは、プロパティの集合だけ。
            result += escape(pattern, i, false, ignore_case); continue;
        }
        if (c == U'*' || c == U'+' || c == U'?' || c == U'{') {
            if (!can_repeat) throw std::invalid_argument("D regex repetition without an atom");
            ++i;
            auto skip = [&] { if (freeform) while (i < pattern.size() && white(pattern[i])) ++i; };
            auto advance = [&] { ++i; skip(); };
            // Phobos の parseQuantifier は十進数の波括弧
            // 形式を要求するが、 PCREは不正な波括弧をリテラルとし、所有的量
            // 指定子も受け付ける。 一つのアトムを渡す前に、元の文法を検証する。
            if (c == U'{') {
                skip();
                auto number = [&]() {
                    if (i == pattern.size() || pattern[i] < U'0' || pattern[i] > U'9') throw std::invalid_argument("D regex repetition requires a number");
                    std::uint32_t value{};
                    while (i < pattern.size() && pattern[i] >= U'0' && pattern[i] <= U'9') {
                        if (value >= 0xffffffffu / 10) throw std::invalid_argument("D regex repetition number overflow");
                        value = value * 10 + pattern[i] - U'0'; advance();
                    }
                    return value;
                };
                const auto minimum = number(); auto maximum = minimum; bool unbounded = false;
                if (i < pattern.size() && pattern[i] == U',') {
                    advance();
                    if (i < pattern.size() && pattern[i] == U'}') unbounded = true;
                    else maximum = number();
                    // D版と同様、コンマを使う分岐だけは、自由形式が無
                    // 効でも、閉じ波括弧の前の空白を明示的に飛ばす。
                    while (i < pattern.size() && white(pattern[i])) ++i;
                }
                if (i == pattern.size() || pattern[i] != U'}' || (!unbounded && minimum > maximum)) throw std::invalid_argument("Invalid D regex repetition");
                advance();
                const bool lazy = i < pattern.size() && pattern[i] == U'?';
                recordBudget(minimum, maximum, unbounded);
                if (lowerRepeat(minimum, maximum, unbounded, lazy)) {
                    if (lazy) advance();
                    can_repeat = false; continue;
                }
                result += U"{" + decimal(minimum);
                if (unbounded) result += U","; else if (maximum != minimum) result += U"," + decimal(maximum);
                result += U"}";
            } else {
                skip(); const bool lazy = i < pattern.size() && pattern[i] == U'?';
                recordBudget(c == U'+' ? 1 : 0, c == U'?' ? 1 : 0, c != U'?');
                if (lowerRepeat(c == U'+' ? 1 : 0, c == U'?' ? 1 : 0, c != U'?', lazy)) {
                    if (lazy) advance(); can_repeat = false; continue;
                }
                result.push_back(c);
            }
            if (i < pattern.size() && pattern[i] == U'?') { result.push_back(U'?'); advance(); }
            can_repeat = false; continue;
        }
        if (c == U'}') throw std::invalid_argument("D regex closing brace without repetition");
        ++i;
        if (c == U'(') { const auto marker = open(true, false); result.push_back(c); result += marker; can_repeat = false; continue; }
        if (c == U')') {
            if (groups.empty()) throw std::invalid_argument("Unmatched D regex group close");
            const auto group = groups.back(); groups.pop_back();
            if (group.lookaround) {
                const auto count = counts.back(); counts.pop_back(); counts.back() += count;
                if (result.substr(group.start).find(U"(?C\"Q,") != Text::npos) {
                    const auto header = result[group.start + 2] == U'<' ? 4 : 3;
                    result.insert(group.start + header, U"(?C\"H," + decimal(group.scope) + U"\")");
                }
            }
            if (group.capture) result.push_back(U')');
            result.push_back(c); atom_start = group.start; atom_base = group.base; can_repeat = !group.lookaround; continue;
        }
        can_repeat = c != U'|';
        atom_start = result.size(); atom_base = actual;
        // Phobos のドットはCR/LFだけを除くが、複数行のアンカーはNEL/LS/PS
        // も認識する。 PCREの一つの改行モードだけでは、両方を表現できない。
        if (c == U'.') result += dot_all ? U"[\\s\\S]" : U"[^\\r\\n]";
        else if (c == U'^') result += multiline ? U"(?:\\A|(?<=[\\n\\x{85}\\x{2028}\\x{2029}])|(?<=\\r)(?!\\n))" : U"\\A";
        else if (c == U'$') result += multiline ? U"(?:\\z|(?=[\\r\\x{85}\\x{2028}\\x{2029}])|(?<!\\r)(?=\\n))" : U"\\z";
        else if (c == U'|') result.push_back(c);
        else result += literal(c, ignore_case);
    }
    Text resolved;
    for (std::size_t at = 0; at < result.size();) {
        if (!result.substr(at).starts_with(U"(?C\"r,")) { resolved.push_back(result[at++]); continue; }
        const auto end = result.find(U"\")", at + 5);
        std::uint32_t fields[3]{}; std::size_t field{};
        for (auto i = at + 6; i < end; ++i) { if (result[i] == U',') ++field; else fields[field] = fields[field] * 10 + result[i] - U'0'; }
        if (!fields[1] && captures.at(fields[0]).size() == 1) {
            resolved += U"(?C\"n," + decimal(fields[0]) + U"\")\\g{" + decimal(captures[fields[0]][0].actual) + U"}";
        } else {
            const auto values = decimal(fields[0]) + U",0," + decimal(fields[2]);
            resolved += U"(?:(?C\"l," + values + U"\")[\\s\\S])*?(?C\"o," + values + U"\")";
        }
        at = end + 2;
    }
    return {std::move(resolved), std::move(captures), std::move(repeats)};
}
// カウンターの記録は対象文字列の位置ではなく、実際のネイティブの失敗
// 経路に従う。 Iはアトムの別候補の再試行でも重複実行の影響を出さ
// ない。Uは反復前の状態を復元して内側の成功済み範囲を除き、 Vは
// 量指定子の呼び出し全体を終了する。ゼロ長の有限反復もこれで扱う。
struct MatchState {
    const std::vector<std::vector<PhysicalCapture>>& captures;
    const std::vector<RepeatSpec>& repeats;
    struct Counter { std::uint64_t count{}; bool stopped{}; };
    struct Trace {
        std::uint64_t mask{};
        PCRE2_SIZE offset{};
        bool mark(PCRE2_SIZE position) {
            const auto distance = position - offset;
            if (distance < 64) {
                const auto bit = std::uint64_t{1} << distance;
                const bool repeated = (mask & bit) != 0;
                mask |= bit; return repeated;
            }
            offset = position; mask = 1; return false;
        }
    };
    using Merge = std::map<std::uint64_t, Trace>;
    struct Frame { bool invocation; std::uint32_t id; Counter state; std::size_t owner; PCRE2_SIZE position; std::shared_ptr<Merge> merge; };
    std::vector<Frame> frames;
    std::vector<std::array<std::vector<std::size_t>, 2>> indexes;
    std::vector<std::map<std::vector<std::uint64_t>, std::shared_ptr<Merge>>> merges;
    MatchState(const std::vector<std::vector<PhysicalCapture>>& c, const std::vector<RepeatSpec>& r) : captures(c), repeats(r), indexes(r.size()), merges(r.size()) {}
    // D版の matchImpl と同様、実行スタックはリセットするが、統合状態は検索の再試行や
    // 続く一致をまたいで残す。 新しい matchAll の対象を設定したときだけ初期化し直す。
    void clearJournal() { frames.clear(); for (auto& pair : indexes) for (auto& list : pair) list.clear(); }
    void beginAssertion(std::uint32_t scope) {
        // Phobos と同様、各条件検査に入るたびに新しい内部の照合器を
        // 作る。 そのカウンター・印は外側の照合器には属さない。 ネイティ
        // ブの失敗経路を戻る間、既存の記録フレームは自身の表を保持する。
        for (std::size_t id = 0; id < repeats.size(); ++id) if (repeats[id].assertion_scope == scope) merges[id].clear();
    }
    void push(Frame frame) { indexes[frame.id][frame.invocation].push_back(frames.size()); frames.push_back(frame); }
    void trim(std::size_t size) {
        while (frames.size() > size) { const auto frame = frames.back(); indexes[frame.id][frame.invocation].pop_back(); frames.pop_back(); }
    }
    std::optional<std::size_t> find(std::uint32_t id, bool invocation) const {
        const auto& list = indexes[id][invocation]; return list.empty() ? std::nullopt : std::optional<std::size_t>(list.back());
    }
    int repeat(char32_t mode, std::uint32_t id, const pcre2_callout_block& block) {
        if (id >= repeats.size()) return PCRE2_ERROR_CALLOUT;
        const auto& spec = repeats[id];
        if (mode == U'Q') {
            std::vector<std::uint64_t> context;
            for (const auto parent : spec.parents) {
                const auto frame = find(parent, true);
                if (!frame) return PCRE2_ERROR_CALLOUT;
                const auto count = frames[*frame].state.count;
                if (!repeats[parent].unbounded) context.push_back(count);
                else {
                    // D版と同様、必須の接頭部分と無限反復の末尾には、別のア
                    // トム中間表現と合流判定位置を作り、 その後で有限カウン
                    // ターをリセットする。 子孫のキーには、増え続けるネイテ
                    // ィブの反復番号ではなく、その判定位置の段階を保持する。
                    const auto minimum = repeats[parent].minimum;
                    const bool prefix = minimum && count < minimum;
                    context.push_back(prefix ? 0 : 1);
                    context.push_back(prefix && minimum > 1 ? count : 0);
                }
            }
            auto& merge = merges[id][context];
            if (!merge) merge = std::make_shared<Merge>();
            push({true, id, {}, 0, block.current_position, merge});
            return spec.minimum_width && spec.minimum > (block.subject_length - block.current_position) / spec.minimum_width ? 1 : 0;
        }
        const auto owner = find(id, true);
        // 幅の制限で拒否した範囲も記録し、Vの経路が同じ量指定子の
        // 古い呼び出しではなく、今回の試行を終了するようにする。
        if (mode == U'V') { if (owner) trim(*owner); return 0; }
        if (!owner) return PCRE2_ERROR_CALLOUT;
        const auto state = frames[*owner].state;
        if (mode == U'A') return !state.stopped && (spec.unbounded || state.count < spec.maximum) ? 0 : 1;
        // Phobos と同様、キャプチャー内容とは独立して、合流判定位置・符
        // 号化カウンター・対象文字列の位置を統合する。 祖先の回数の組は、混合
        // 基数のカウンターの疎な表現に相当し、Q/Vや検索開始位置の前進をまた
        // いで印を保持する。 条件検査はD版と同様、独自の新しい照合表を使う。
        if (mode == U'F') {
            if (state.count < spec.minimum) return 1;
            if (spec.unbounded) return state.stopped ? 0 : 1;
            return !spec.unbounded && !state.stopped && state.count == spec.maximum &&
                (*frames[*owner].merge)[state.count].mark(block.current_position) ? 1 : 0;
        }
        if (mode == U'T') {
            const auto merge = frames[*owner].merge;
            // 失敗した反復も記録し、Uの経路が同じアトムの以前の有
            // 効な反復ではなく、今回の試行を復元するようにする。
            push({false, id, state, *owner, block.current_position, {}});
            if (!spec.unbounded || (spec.minimum > 1 && state.count <= spec.minimum))
                return (*merge)[state.count].mark(block.current_position) ? 1 : 0;
            // InfiniteStart には初期の印がない。 必須部分のカウンターをリセ
            // ットした後は、続く InfiniteEnd が一つの合流判定位置を共有する。
            return state.count > spec.minimum && (*merge)[UINT64_MAX].mark(block.current_position) ? 1 : 0;
        }
        const auto iteration = find(id, false);
        if (!iteration || frames[*iteration].owner != *owner) return PCRE2_ERROR_CALLOUT;
        const auto prior = frames[*iteration];
        if (mode == U'U') { frames[*owner].state = prior.state; trim(*iteration); return 0; }
        if (mode == U'S') {
            if (prior.state.count < spec.minimum) return 1;
            frames[*owner].state = {prior.state.count, true}; return 0;
        }
        if (mode == U'I') {
            const auto count = prior.state.count + 1;
            frames[*owner].state = {count, false}; return 0;
        }
        return PCRE2_ERROR_CALLOUT;
    }
};
int backrefCallout(pcre2_callout_block* block, void* user) noexcept {
    auto& state = *static_cast<MatchState*>(user);
    if (block->callout_flags & PCRE2_CALLOUT_STARTMATCH) state.clearJournal();
    if (!block->callout_string || block->callout_string_length < 3) return PCRE2_ERROR_CALLOUT;
    const auto mode = block->callout_string[0];
    std::uint32_t fields[3]{}; std::size_t field{};
    for (std::size_t i = 2; i < block->callout_string_length; ++i) {
        const auto c = block->callout_string[i];
        if (c == U',') { if (++field == 3) return PCRE2_ERROR_CALLOUT; }
        else if (c >= U'0' && c <= U'9') fields[field] = fields[field] * 10 + c - U'0';
        else return PCRE2_ERROR_CALLOUT;
    }
    if (mode >= U'A' && mode <= U'Z') {
        if (field) return PCRE2_ERROR_CALLOUT;
        try {
            if (mode == U'H') { state.beginAssertion(fields[0]); return 0; }
            return state.repeat(mode, fields[0], *block);
        } catch (...) { return PCRE2_ERROR_NOMEMORY; }
    }
    auto offset = [&](std::uint32_t capture, bool end) { return capture < block->capture_top ? block->offset_vector[capture * 2 + (end ? 1 : 0)] : PCRE2_UNSET; };
    if ((mode != U'n' && (field != 2 || (mode != U'l' && mode != U'o')))) return PCRE2_ERROR_CALLOUT;
    const auto& captures = state.captures;
    if (fields[0] >= captures.size()) return PCRE2_ERROR_CALLOUT;
    PCRE2_SIZE begin = PCRE2_UNSET, end{};
    for (const auto physical : captures[fields[0]]) {
        const auto candidate_begin = offset(physical.begin, false), candidate_end = offset(physical.actual, true);
        if (candidate_begin != PCRE2_UNSET && (begin == PCRE2_UNSET || candidate_begin > begin)) begin = candidate_begin;
        if (candidate_end != PCRE2_UNSET && candidate_end > end) end = candidate_end;
    }
    if (mode == U'n') return begin == PCRE2_UNSET || begin > end || end > block->subject_length ? PCRE2_ERROR_CALLOUT : 0;
    const auto start = offset(fields[2], false);
    if (begin == PCRE2_UNSET || start == PCRE2_UNSET || begin > end || end > block->subject_length || block->current_position < start) return PCRE2_ERROR_CALLOUT;
    const auto length = block->current_position - start, expected = end - begin;
    if (mode == U'o') return length == expected ? 0 : 1;
    return length < expected && block->current_position < block->subject_length && block->subject[block->current_position] == block->subject[begin + length] ? 0 : 1;
}
}
struct DRegex::Impl {
    pcre2_code* code{};
    std::vector<std::vector<PhysicalCapture>> captures;
    std::vector<RepeatSpec> repeats;
    ~Impl() { if (code) pcre2_code_free(code); }
};
DRegex::DRegex(std::u32string_view pattern) : impl_(std::make_unique<Impl>()) {
    for (const auto c : pattern) if (c > 0x10ffff) throw std::invalid_argument("Invalid D regex codepoint");
    auto translated = translate(pattern); impl_->captures = std::move(translated.captures); impl_->repeats = std::move(translated.repeats); int error{}; PCRE2_SIZE offset{};
    // D版の dstring は一位置に一つの dchar を持ち、
    // 正規表現のプロパティにはサロゲートの分類・ブロックも含められる
    // 。 PCREのUTFフラグはその有効な範囲をコンパイル時に拒否
    // するため、32ビットのコード単位とUCPを使う。 メッセージ
    // のUTF-8変換は共通の呼び出し元で厳密に検証し、ここでは
    // dchar の範囲を別に制限する。 上のプロパティ表と単純な
    // 大小文字対応はPhobosに固定し、PCREの版やOSロケー
    // ルに依存させない。 D版の後方参照の範囲エラーを実行時に検出
    // できるよう、コールアウトを飛ばし得る 開始位置・最小長の検査
    // 、自動所有化、ドットスターのアンカー化も、ここで無効にする。
    impl_->code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(translated.pattern.data()), translated.pattern.size(), PCRE2_UCP | PCRE2_DUPNAMES | PCRE2_NO_START_OPTIMIZE | PCRE2_NO_AUTO_POSSESS | PCRE2_NO_DOTSTAR_ANCHOR, &error, &offset, nullptr);
    if (!impl_->code) throw std::invalid_argument("Invalid D regex (PCRE2 " + std::to_string(error) + ", translated offset " + std::to_string(offset) + ")");
}
DRegex::~DRegex() = default;
DRegex::DRegex(DRegex&&) noexcept = default;
DRegex& DRegex::operator=(DRegex&&) noexcept = default;
std::vector<Text> DRegex::matchAll(std::u32string_view subject) const {
    for (const auto c : subject) if (c > 0x10ffff) throw std::invalid_argument("Invalid D regex subject codepoint");
    std::vector<Text> result;
    const auto data = std::unique_ptr<pcre2_match_data, decltype(&pcre2_match_data_free)>(pcre2_match_data_create_from_pattern(impl_->code, nullptr), pcre2_match_data_free);
    if (!data) throw std::bad_alloc();
    const auto context = std::unique_ptr<pcre2_match_context, decltype(&pcre2_match_context_free)>(pcre2_match_context_create(nullptr), pcre2_match_context_free);
    if (!context) throw std::bad_alloc();
    // Group.init は空文字列ではなく [size_t.max
    // .. 0]。 PCREの未設定参照の失敗・空扱いを引き継が
    // ず、選択肢も含め、参照を実行する時点で元の範囲を検査する。
    MatchState state{impl_->captures, impl_->repeats};
    pcre2_set_callout(context.get(), backrefCallout, &state);
    // D版で有効な回数は、空のアトムでもPCREの既定のバックトラッ
    // ク深さの上限を超える場合がある。 資源不足は例外として扱い、ラ
    // イブラリの既定値をD版の構文・有限回数の制限と読み替えない。
    pcre2_set_depth_limit(context.get(), 0xffffffffu);
    pcre2_set_match_limit(context.get(), 0xffffffffu);
    pcre2_set_heap_limit(context.get(), 0xffffffffu);
    std::size_t start{};
    const char32_t empty_subject{};
    const auto subject_data = subject.data() ? subject.data() : &empty_subject;
    for (;;) {
        const int matched = pcre2_match(impl_->code, reinterpret_cast<PCRE2_SPTR>(subject_data), subject.size(), start, 0, data.get(), context.get());
        if (matched == PCRE2_ERROR_NOMATCH) break;
        if (matched < 0) throw std::runtime_error("D regex match failed (PCRE2 " + std::to_string(matched) + ")");
        const auto offsets = pcre2_get_ovector_pointer(data.get());
        result.emplace_back(subject.substr(offsets[0], offsets[1] - offsets[0]));
        // Phobos の matchFinalize と同様、末尾で終了
        // し、空の一致の後は一スカラー進める。 PCREの同じ位置で非空の
        // 別候補を再試行する動作は余分な一致を返すため、意図的に使わない。
        if (offsets[1] == subject.size()) break;
        start = offsets[1] + (offsets[0] == offsets[1] ? 1 : 0);
    }
    return result;
}
}
