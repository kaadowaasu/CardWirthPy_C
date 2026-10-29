#include "cw/lib/regex_grapheme.hpp"
#include "cw/lib/regex_grapheme_unicode_data.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace cw::lib::regex_grapheme {
namespace {

constexpr std::size_t kMaxPatternCodepoints = 4096;
constexpr std::size_t kMaxPatternDepth = 128;
constexpr std::size_t kMaxMatchDepth = 256;

using CP = std::uint32_t;
using String = ::cw::lib::String;

struct Decoded {
    CP value{};
    std::size_t begin{};
    std::size_t end{};
};

[[noreturn]] void invalid_utf8(std::size_t offset, const String& reason)
{
    throw InvalidUtf8(offset, "不正なUTF-8 (" + reason + ")");
}

std::vector<Decoded> decode_items(const String& text)
{
    std::vector<Decoded> result;
    result.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t begin = i;
        const auto byte = static_cast<unsigned char>(text[i]);
        CP value = 0;
        std::size_t length = 0;
        if (byte <= 0x7Fu) {
            value = byte; length = 1;
        } else if (byte >= 0xC2u && byte <= 0xDFu) {
            value = byte & 0x1Fu; length = 2;
        } else if (byte >= 0xE0u && byte <= 0xEFu) {
            value = byte & 0x0Fu; length = 3;
        } else if (byte >= 0xF0u && byte <= 0xF4u) {
            value = byte & 0x07u; length = 4;
        } else {
            invalid_utf8(i, "leading byte");
        }
        if (i + length > text.size()) invalid_utf8(i, "truncated sequence");
        for (std::size_t n = 1; n < length; ++n) {
            const auto continuation = static_cast<unsigned char>(text[i + n]);
            if ((continuation & 0xC0u) != 0x80u) invalid_utf8(i + n, "continuation byte");
            value = (value << 6) | (continuation & 0x3Fu);
        }
        if ((length == 3 && value < 0x800u) || (length == 4 && value < 0x10000u) ||
            (value >= 0xD800u && value <= 0xDFFFu) || value > 0x10FFFFu) {
            invalid_utf8(begin, "overlong, surrogate, or out-of-range scalar");
        }
        result.push_back({value, begin, i + length});
        i += length;
    }
    return result;
}

template<std::size_t N>
bool in_ranges(CP value, const std::pair<CP, CP> (&ranges)[N])
{
    for (const auto& range : ranges) if (range.first <= value && value <= range.second) return true;
    return false;
}

using Group = unicode_data::Group;

template<class Range, std::size_t N>
std::size_t range_index(CP value, const Range (&ranges)[N])
{
    std::size_t first = 0, last = N;
    while (first < last) {
        const auto middle = first + (last - first) / 2;
        if (value < ranges[middle].first) last = middle;
        else if (ranges[middle].last < value) first = middle + 1;
        else return middle;
    }
    return N;
}

Group gcb_group(CP value)
{
    const auto index = range_index(value, unicode_data::groups);
    return index == std::size(unicode_data::groups) ? Group::Other : unicode_data::groups[index].group;
}

bool is_mark(CP value)
{
    // Py の regex の Mark 属性は、GCB 分類とは別の表で扱う。
    return range_index(value, unicode_data::marks) != std::size(unicode_data::marks);
}

bool is_spacing_mark(CP value) { return gcb_group(value) == Group::SpacingMark; }
bool is_extend(CP value) { return gcb_group(value) == Group::Extend; }
bool is_zwj(CP value) { return gcb_group(value) == Group::ZWJ; }
bool is_cr(CP value) { return gcb_group(value) == Group::CR; }
bool is_lf(CP value) { return gcb_group(value) == Group::LF; }
bool is_control(CP value) { return gcb_group(value) == Group::Control; }
bool is_prepend(CP value) { return gcb_group(value) == Group::Prepend; }
bool is_ri(CP value) { return gcb_group(value) == Group::RI; }
bool is_ep(CP value) { return gcb_group(value) == Group::EP; }

enum class Hangul { Other, L, V, T, LV, LVT };
Hangul hangul(CP value)
{
    switch (gcb_group(value)) {
    case Group::L: return Hangul::L;
    case Group::V: return Hangul::V;
    case Group::T: return Hangul::T;
    case Group::LV: return Hangul::LV;
    case Group::LVT: return Hangul::LVT;
    default: return Hangul::Other;
    }
}

enum class GraphemeState { Default, CR, Control, Prepend, L, V, T, Emoji, EmojiZWJ, RI };
struct GraphemeStep { bool boundary; GraphemeState next; };

GraphemeStep default_step(Group group)
{
    switch (group) {
    case Group::CR: return {true, GraphemeState::CR};
    case Group::LF: case Group::Control: return {true, GraphemeState::Control};
    case Group::Extend: case Group::SpacingMark: case Group::ZWJ: return {false, GraphemeState::Default};
    case Group::EP: return {true, GraphemeState::Emoji};
    case Group::RI: return {true, GraphemeState::RI};
    case Group::L: return {true, GraphemeState::L};
    case Group::LV: case Group::V: return {true, GraphemeState::V};
    case Group::LVT: case Group::T: return {true, GraphemeState::T};
    case Group::Prepend: return {true, GraphemeState::Prepend};
    default: return {true, GraphemeState::Default};
    }
}

GraphemeStep next_step(GraphemeState state, Group group)
{
    // Py が使う grapheme 0.6.0 の実際の状態機械に従い、RI
    // 列内の Extend 後の状態リセットも含める。 Unicode スカ
    // ラーごとに一つの状態で境界を決め、長い国旗列でも繰り返し後方走査しない。
    auto step = default_step(group);
    switch (state) {
    case GraphemeState::CR:
        if (group == Group::LF) return {false, GraphemeState::Control};
        step.boundary = true; return step;
    case GraphemeState::Control: step.boundary = true; return step;
    case GraphemeState::Prepend:
        if (group == Group::Control || group == Group::LF) return {true, GraphemeState::Default};
        if (group == Group::CR) return {true, GraphemeState::CR};
        step.boundary = false; return step;
    case GraphemeState::L:
        if (group == Group::L) return {false, GraphemeState::L};
        if (group == Group::V || group == Group::LV) return {false, GraphemeState::V};
        if (group == Group::LVT) return {false, GraphemeState::T};
        return step;
    case GraphemeState::V:
        if (group == Group::V) return {false, GraphemeState::V};
        if (group == Group::T) return {false, GraphemeState::T};
        return step;
    case GraphemeState::T:
        if (group == Group::T) return {false, GraphemeState::T};
        return step;
    case GraphemeState::Emoji:
        if (group == Group::Extend) return {false, GraphemeState::Emoji};
        if (group == Group::ZWJ) return {false, GraphemeState::EmojiZWJ};
        return step;
    case GraphemeState::EmojiZWJ:
        if (group == Group::EP) return {false, GraphemeState::Emoji};
        return step;
    case GraphemeState::RI:
        if (group == Group::RI) return {false, GraphemeState::Default};
        return step;
    default: return step;
    }
}

CP fold_case(CP value)
{
    if (value >= 'A' && value <= 'Z') return value + ('a' - 'A');
    if (value >= 0xFF21u && value <= 0xFF3Au) return value + 0x20u;
    if (value >= 0x00C0u && value <= 0x00D6u) return value + 0x20u;
    if (value >= 0x00D8u && value <= 0x00DEu) return value + 0x20u;
    return value;
}

bool ascii_word(CP value)
{
    // regex.IGNORECASE の [a-z] と同様、İ・ſ・K も受け付けるが、点なしの ı は含めない。
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || value == '_' ||
           value == 0x0130u || value == 0x017Fu || value == 0x212Au;
}
bool fullwidth_word(CP value)
{
    return (value >= 0xFF21u && value <= 0xFF3Au) || (value >= 0xFF41u && value <= 0xFF5Au) ||
           (value >= 0xFF10u && value <= 0xFF19u) || value == 0xFF3Fu;
}
bool word_base(CP value) { return ascii_word(value) || fullwidth_word(value); }

}  // namespace

InvalidUtf8::InvalidUtf8(std::size_t byte_offset, const String& message)
    : std::runtime_error(message), m_byte_offset(byte_offset)
{
}

std::vector<std::uint32_t> decode_utf8(const String& text)
{
    std::vector<std::uint32_t> result;
    for (const auto& item : decode_items(text)) result.push_back(item.value);
    return result;
}

std::vector<String> grapheme_clusters(const String& text)
{
    const auto items = decode_items(text);
    std::vector<String> result;
    if (items.empty()) return result;
    std::size_t begin = 0;
    auto state = default_step(gcb_group(items.front().value)).next;
    for (std::size_t i = 1; i < items.size(); ++i) {
        const auto step = next_step(state, gcb_group(items[i].value));
        state = step.next;
        if (step.boundary) {
            result.push_back(text.substr(items[begin].begin, items[i - 1].end - items[begin].begin));
            begin = i;
        }
    }
    result.push_back(text.substr(items[begin].begin, items.back().end - items[begin].begin));
    return result;
}

std::vector<String> graphemes(const String& text)
{
    std::vector<String> result;
    for (const auto& cluster : grapheme_clusters(text)) {
        const auto items = decode_items(cluster);
        if (items.size() == 2 && (items[1].value == 0xFF9Eu || items[1].value == 0xFF9Fu)) {
            result.push_back(cluster.substr(0, items[0].end));
            result.push_back(cluster.substr(items[0].end));
        } else {
            result.push_back(cluster);
        }
    }
    return result;
}

std::vector<String> findall_word_tokens(const String& text)
{
    const auto items = decode_items(text);
    std::vector<String> result;
    std::size_t i = 0;
    while (i < items.size()) {
        if (items[i].value == '\n') { ++i; continue; }
        const std::size_t begin = i;
        // Python のパターンは ASCII と全角を別の選択肢と
        // して持つ。 二つの単語列を混ぜず、開始時の文字種を維持する。
        const bool starts_ascii = ascii_word(items[i].value);
        const bool starts_fullwidth = fullwidth_word(items[i].value);
        if (starts_ascii || starts_fullwidth) {
            do {
                ++i;
                while (i < items.size() && is_mark(items[i].value)) ++i;
            } while (i < items.size() && (starts_ascii ? ascii_word(items[i].value) : fullwidth_word(items[i].value)));
        } else {
            ++i;
            while (i < items.size() && is_mark(items[i].value)) ++i;
        }
        result.push_back(text.substr(items[begin].begin, items[i - 1].end - items[begin].begin));
    }
    return result;
}

std::vector<String> findall(const String& text)
{
    return findall_word_tokens(text);
}

namespace {

enum class NodeKind { Empty, Literal, Dot, Class, Mark, Sequence, Alternate, Repeat, Begin, End };
struct Node {
    NodeKind kind{NodeKind::Empty};
    CP value{};
    bool negate{};
    std::vector<std::pair<CP, CP>> ranges{};
    std::vector<std::shared_ptr<Node>> children{};
    std::shared_ptr<Node> child{};
    std::size_t minimum{};
    std::size_t maximum{};
};
class PatternError final : public std::runtime_error {
public:
    PatternError() : std::runtime_error("invalid regex pattern") {}
};

class Parser {
public:
    explicit Parser(std::vector<CP> pattern) : m_pattern(std::move(pattern)) {}
    std::shared_ptr<Node> parse()
    {
        auto result = expression();
        if (m_position != m_pattern.size()) throw PatternError();
        return result;
    }

private:
    std::shared_ptr<Node> expression()
    {
        std::vector<std::shared_ptr<Node>> alternatives;
        alternatives.push_back(sequence());
        while (take('|')) alternatives.push_back(sequence());
        if (alternatives.size() == 1) return alternatives.front();
        auto result = std::make_shared<Node>(); result->kind = NodeKind::Alternate; result->children = std::move(alternatives); return result;
    }
    std::shared_ptr<Node> sequence()
    {
        std::vector<std::shared_ptr<Node>> nodes;
        while (m_position < m_pattern.size() && m_pattern[m_position] != ')' && m_pattern[m_position] != '|') {
            auto atom = atom_node();
            if (m_position < m_pattern.size() && (m_pattern[m_position] == '*' || m_pattern[m_position] == '+' || m_pattern[m_position] == '?')) {
                const CP quantifier = m_pattern[m_position++];
                auto repeat = std::make_shared<Node>(); repeat->kind = NodeKind::Repeat; repeat->child = atom;
                repeat->minimum = quantifier == '+' ? 1u : 0u; repeat->maximum = quantifier == '?' ? 1u : 0u;
                nodes.push_back(std::move(repeat));
                if (m_position < m_pattern.size() && (m_pattern[m_position] == '*' || m_pattern[m_position] == '+' || m_pattern[m_position] == '?')) throw PatternError();
            } else nodes.push_back(std::move(atom));
        }
        if (nodes.size() == 1) return nodes.front();
        auto result = std::make_shared<Node>(); result->kind = NodeKind::Sequence; result->children = std::move(nodes); return result;
    }
    std::shared_ptr<Node> atom_node()
    {
        if (m_position >= m_pattern.size()) throw PatternError();
        const CP value = m_pattern[m_position++];
        if (value == '^') { auto n = std::make_shared<Node>(); n->kind = NodeKind::Begin; return n; }
        if (value == '$') { auto n = std::make_shared<Node>(); n->kind = NodeKind::End; return n; }
        if (value == '.') { auto n = std::make_shared<Node>(); n->kind = NodeKind::Dot; return n; }
        if (value == '(') {
            if (++m_depth > kMaxPatternDepth) throw PatternError();
            auto inner = expression();
            --m_depth;
            if (!take(')')) throw PatternError();
            return inner;
        }
        if (value == '[') return class_node();
        if (value == ']' || value == '*' || value == '+' || value == '?' || value == '|' || value == '{' || value == '}') throw PatternError();
        if (value != '\\') { auto n = std::make_shared<Node>(); n->kind = NodeKind::Literal; n->value = value; return n; }
        if (m_position >= m_pattern.size()) throw PatternError();
        const CP escaped = m_pattern[m_position++];
        if (escaped == 'p' || escaped == 'P') {
            if (!take('{') || !take('M') || !take('}')) throw PatternError();
            auto n = std::make_shared<Node>(); n->kind = NodeKind::Mark; n->negate = escaped == 'P'; return n;
        }
        if (escaped == 'd' || escaped == 'D' || escaped == 'w' || escaped == 'W' || escaped == 's' || escaped == 'S') throw PatternError();
        auto n = std::make_shared<Node>(); n->kind = NodeKind::Literal; n->value = escaped; return n;
    }
    std::shared_ptr<Node> class_node()
    {
        auto n = std::make_shared<Node>(); n->kind = NodeKind::Class;
        if (take('^')) n->negate = true;
        bool any = false;
        while (m_position < m_pattern.size() && m_pattern[m_position] != ']') {
            CP first = class_character();
            CP last = first;
            if (take('-')) {
                if (m_position >= m_pattern.size() || m_pattern[m_position] == ']') throw PatternError();
                last = class_character(); if (last < first) throw PatternError();
            }
            n->ranges.emplace_back(first, last); any = true;
        }
        if (!take(']') || !any) throw PatternError();
        return n;
    }
    CP class_character()
    {
        if (m_position >= m_pattern.size()) throw PatternError();
        if (m_pattern[m_position] != '\\') return m_pattern[m_position++];
        ++m_position; if (m_position >= m_pattern.size()) throw PatternError();
        const CP value = m_pattern[m_position++];
        if (value == 'd' || value == 'D' || value == 'w' || value == 'W' || value == 's' || value == 'S' || value == 'p' || value == 'P') throw PatternError();
        return value;
    }
    bool take(CP value) { if (m_position < m_pattern.size() && m_pattern[m_position] == value) { ++m_position; return true; } return false; }
    std::vector<CP> m_pattern;
    std::size_t m_position{};
    std::size_t m_depth{};
};

CP match_case(CP value, bool ignore_case) { return ignore_case ? fold_case(value) : value; }
bool in_class(CP value, const Node& node, bool ignore_case)
{
    const CP candidate = match_case(value, ignore_case);
    bool matched = false;
    for (const auto& range : node.ranges) {
        const CP first = match_case(range.first, ignore_case), last = match_case(range.second, ignore_case);
        if (first <= candidate && candidate <= last) { matched = true; break; }
    }
    return node.negate ? !matched : matched;
}

std::set<std::size_t> match_node(const Node& node, const std::vector<CP>& text, std::size_t position, bool ignore_case, std::size_t depth);
std::set<std::size_t> match_sequence(const std::vector<std::shared_ptr<Node>>& nodes, const std::vector<CP>& text, std::size_t position, bool ignore_case, const std::size_t depth)
{
    std::set<std::size_t> positions{position};
    for (const auto& node : nodes) {
        std::set<std::size_t> next;
        for (const auto current : positions) {
            const auto reached = match_node(*node, text, current, ignore_case, depth + 1);
            next.insert(reached.begin(), reached.end());
        }
        positions = std::move(next);
        if (positions.empty()) break;
    }
    return positions;
}

std::set<std::size_t> match_node(const Node& node, const std::vector<CP>& text, std::size_t position, bool ignore_case, const std::size_t depth)
{
    if (depth > kMaxMatchDepth) throw PatternError();
    switch (node.kind) {
    case NodeKind::Empty: return {position};
    case NodeKind::Literal:
        if (position < text.size() && match_case(text[position], ignore_case) == match_case(node.value, ignore_case)) return {position + 1};
        return {};
    case NodeKind::Dot:
        return position < text.size() && text[position] != '\n' ? std::set<std::size_t>{position + 1} : std::set<std::size_t>{};
    case NodeKind::Mark:
        if (position < text.size() && (is_mark(text[position]) != node.negate)) return {position + 1};
        return {};
    case NodeKind::Class:
        if (position < text.size() && in_class(text[position], node, ignore_case)) return {position + 1};
        return {};
    case NodeKind::Sequence: return match_sequence(node.children, text, position, ignore_case, depth);
    case NodeKind::Alternate: {
        std::set<std::size_t> result;
        for (const auto& child : node.children) { const auto reached = match_node(*child, text, position, ignore_case, depth + 1); result.insert(reached.begin(), reached.end()); }
        return result;
    }
    case NodeKind::Begin: return position == 0 ? std::set<std::size_t>{position} : std::set<std::size_t>{};
    case NodeKind::End: return position == text.size() ? std::set<std::size_t>{position} : std::set<std::size_t>{};
    case NodeKind::Repeat: {
        std::set<std::size_t> current{position}, result;
        const std::size_t limit = node.maximum == 0 ? text.size() + 1 : node.maximum;
        for (std::size_t count = 0; count <= limit; ++count) {
            if (count >= node.minimum) result.insert(current.begin(), current.end());
            if (count == limit) break;
            std::set<std::size_t> next;
            for (const auto current_position : current) {
                const auto reached = match_node(*node.child, text, current_position, ignore_case, depth + 1);
                for (const auto reached_position : reached) if (reached_position != current_position) next.insert(reached_position);
            }
            if (next.empty()) break;
            current = std::move(next);
        }
        return result;
    }
    }
    return {};
}

}  // namespace

bool fullmatch(const String& pattern, const String& text, bool ignore_case)
{
    const auto pattern_items = decode_utf8(pattern);
    const auto text_items = decode_utf8(text);
    try {
        if (pattern_items.size() > kMaxPatternCodepoints) throw PatternError();
        auto ast = Parser(pattern_items).parse();
        const auto positions = match_node(*ast, text_items, 0, ignore_case, 0);
        return positions.find(text_items.size()) != positions.end();
    } catch (const PatternError&) {
        return false;
    }
}

}  // namespace cw::lib::regex_grapheme
