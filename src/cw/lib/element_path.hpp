#pragma once

#include "cw/lib/python_integer_value.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace cw::lib::element_path {

// Py の公開 XML ラッパーは find() を ElementTree
// に委譲する。選択の一連の処理をここで保ち、後続の条件・パス
// 要素を処理する前に、中間の子の一致を先頭一つへ減らさない。
struct Step {
    enum class Axis { child, descendant, self, parent } axis{Axis::child};
    String tag;
    std::vector<String> predicates;
};

inline bool tag_matches(const String& actual, const String& requested) {
    if (requested == "*" || requested == "{*}*") return true;
    if (requested == "{}*") return actual.empty() || actual.front() != '{';
    if (requested.starts_with("{*}")) {
        const auto local = requested.substr(3);
        const auto close = actual.find('}');
        return actual == local || (actual.starts_with('{') && close != String::npos && actual.substr(close + 1) == local);
    }
    if (requested.starts_with('{') && requested.ends_with("}*")) {
        const auto prefix = requested.substr(0, requested.size() - 1);
        return actual.starts_with(prefix);
    }
    return actual == requested;
}

inline void validate_name(const String& name) {
    // これらの API に名前空間引数はない。Py は未解決の接頭辞を拒否
    // し、展開済みの {uri} 名は XML の呼び出し側から直接渡す。
    const auto close = name.starts_with('{') ? name.find('}') : String::npos;
    const auto local_start = close == String::npos ? 0 : close + 1;
    if (name.find(':', local_start) != String::npos)
        throw std::invalid_argument("XML path: prefix not found in prefix map");
}

inline std::vector<Step> parse(String path) {
    if (path.empty()) return {};
    if (path.front() == '/') throw std::invalid_argument("cannot use absolute path on element");
    if (path.back() == '/') path += '*';
    std::vector<Step> result;
    bool descendant = false;
    std::size_t offset = 0;
    while (offset < path.size()) {
        Step step;
        step.axis = descendant ? Step::Axis::descendant : Step::Axis::child;
        const auto begin = offset;
        bool expanded = false;
        while (offset < path.size()) {
            const auto ch = path[offset];
            if (ch == '{') expanded = true;
            if (ch == '}') expanded = false;
            if (!expanded && (ch == '/' || ch == '[')) break;
            ++offset;
        }
        step.tag = path.substr(begin, offset - begin);
        if (step.tag.empty()) throw std::invalid_argument("invalid XML path");
        if (step.tag == ".") {
            if (descendant) throw std::invalid_argument("invalid descendant");
            step.axis = Step::Axis::self;
        } else if (step.tag == "..") {
            if (descendant) throw std::invalid_argument("invalid descendant");
            step.axis = Step::Axis::parent;
        } else {
            validate_name(step.tag);
        }
        while (offset < path.size() && path[offset] == '[') {
            const auto predicate_begin = ++offset;
            char quote = 0;
            for (; offset < path.size(); ++offset) {
                const auto ch = path[offset];
                if (quote) { if (ch == quote) quote = 0; }
                else if (ch == '\'' || ch == '"') quote = ch;
                else if (ch == ']') break;
                else if (ch == '[') throw std::invalid_argument("invalid XML predicate");
            }
            if (offset == path.size()) throw std::invalid_argument("invalid XML predicate");
            step.predicates.push_back(path.substr(predicate_begin, offset - predicate_begin));
            ++offset;
        }
        result.push_back(std::move(step));
        if (offset == path.size()) break;
        if (path[offset] != '/') throw std::invalid_argument("invalid XML path");
        ++offset;
        descendant = offset < path.size() && path[offset] == '/';
        if (descendant) ++offset;
        if (offset == path.size()) throw std::invalid_argument("invalid XML path");
    }
    return result;
}

inline String predicate_without_space(const String& source) {
    String result;
    String signature;
    // ElementPath は空白トークンを無視するが、隣接する名前トークンは分ける。[1
    // 2] や [Sub Sub] を [12] や [SubSub] にしてはならない。
    for (std::size_t offset = 0; offset < source.size();) {
        const auto ch = source[offset];
        if (std::isspace(static_cast<unsigned char>(ch))) { ++offset; continue; }
        const auto begin = offset;
        if (ch == '\'' || ch == '"') {
            ++offset;
            while (offset < source.size() && source[offset] != ch) ++offset;
            if (offset == source.size()) throw std::invalid_argument("invalid XML predicate");
            ++offset;
            signature += '\'';
        } else if (ch == '!' && offset + 1 < source.size() && source[offset + 1] == '=') {
            offset += 2;
            signature += "!=";
        } else if (String("@=()./*!").find(ch) != String::npos) {
            ++offset;
            signature += ch;
        } else {
            if (ch == '{') {
                const auto close = source.find('}', offset + 1);
                if (close == String::npos) throw std::invalid_argument("invalid XML predicate");
                offset = close + 1;
            }
            while (offset < source.size() &&
                   !std::isspace(static_cast<unsigned char>(source[offset])) &&
                   String("@=()/!'\"").find(source[offset]) == String::npos) ++offset;
            signature += '-';
        }
        result += source.substr(begin, offset - begin);
    }
    // Reboot の参照ソースは Python 3.8 を指定する。不等価の条
    // 件は Python 3.10 で加わったため、参照版の仕様には含めない。
    if (signature != "@-" && signature != "@-='" &&
        signature != "-" && signature != ".='" &&
        signature != "-='" &&
        signature != "-()" && signature != "-()-")
        throw std::invalid_argument("invalid XML predicate");
    return result;
}

inline bool positional_integer(const String& text) {
    // ElementPath は int() より前に、任意の負号と十進数字の形式を検査する
    // 。Python の int() が許す下線や正号も、ここでは子のタグの一部として扱う。
    if (text.empty() || text.front() == '+' || text.find('_') != String::npos) return false;
    const auto normalized = normalize_python_integer(text);
    return normalized && normalized->find_first_not_of("-0123456789") == String::npos;
}

template<class Element>
String inner_text(const std::shared_ptr<Element>& element) {
    String text = element->m_text;
    for (const auto& child : element->m_children) if (child) {
        text += inner_text(child);
        text += child->m_tail;
    }
    return text;
}

template<class Element>
std::shared_ptr<Element> find(const std::shared_ptr<Element>& root, const String& path) {
    if (path.empty()) return {};
    if (path == ".") return root;
    // 多くの呼び出しは Property や Name など直下の子を一つ選
    // ぶ。同じ子の選択処理を使い、複数段階の候補処理用の領域は確保しない。
    if (path.find_first_of("/[]*.") == String::npos) {
        validate_name(path);
        for (const auto& child : root->m_children)
            if (child && child->m_tag == path) return child;
        return {};
    }
    const auto steps = parse(path);
    if (steps.empty()) return {};
    using Node = std::shared_ptr<Element>;
    std::map<Element*, Node> parents;
    bool parents_ready = false;
    const auto prepare_parents = [&] {
        if (parents_ready) return;
        const auto walk = [&](const auto& self, const Node& parent) -> void {
            for (const auto& child : parent->m_children) if (child) {
                parents[child.get()] = parent;
                self(self, child);
            }
        };
        walk(walk, root);
        parents_ready = true;
    };
    std::vector<Node> matches{root};
    for (const auto& step : steps) {
        std::vector<Node> selected;
        std::set<Element*> seen_parents;
        for (const auto& node : matches) {
            if (step.axis == Step::Axis::self) selected.push_back(node);
            else if (step.axis == Step::Axis::parent) {
                prepare_parents();
                const auto parent = parents.find(node.get());
                if (parent != parents.end() && seen_parents.insert(parent->second.get()).second)
                    selected.push_back(parent->second);
            } else if (step.axis == Step::Axis::descendant) {
                const auto walk = [&](const auto& self, const Node& parent) -> void {
                    for (const auto& child : parent->m_children) if (child) {
                        if (tag_matches(child->m_tag, step.tag)) selected.push_back(child);
                        self(self, child);
                    }
                };
                walk(walk, node);
            } else {
                for (const auto& child : node->m_children)
                    if (child && tag_matches(child->m_tag, step.tag)) selected.push_back(child);
            }
        }
        for (const auto& raw_predicate : step.predicates) {
            const auto predicate = predicate_without_space(raw_predicate);
            if (predicate.empty()) throw std::invalid_argument("invalid XML predicate");
            bool from_last = false;
            std::optional<PythonInteger> index;
            if (predicate == "last()") {
                from_last = true;
                index = PythonInteger(-1);
            } else if (predicate.starts_with("last()")) {
                from_last = true;
                const auto offset = PythonInteger::parse(predicate.substr(6));
                if (!offset || *offset >= 0) throw std::invalid_argument("XPath offset from last() must be negative");
                index = *offset - 1;
            } else if (positional_integer(predicate)) {
                if (const auto number = PythonInteger::parse(predicate)) {
                    if (*number < 1) throw std::invalid_argument("XPath position >= 1 expected");
                    index = *number - 1;
                }
            }
            std::vector<Node> filtered;
            if (index) {
                prepare_parents();
                for (const auto& node : selected) {
                    const auto parent = parents.find(node.get());
                    if (parent == parents.end()) continue;
                    std::vector<Node> siblings;
                    for (const auto& sibling : parent->second->m_children)
                        if (sibling && sibling->m_tag == node->m_tag) siblings.push_back(sibling);
                    const auto size = PythonInteger(static_cast<Integer>(siblings.size()));
                    const auto position = from_last ? size + *index : *index;
                    if (position >= 0 && position < size && siblings[static_cast<std::size_t>(position.to_integer())] == node)
                        filtered.push_back(node);
                }
            } else {
                // 展開済みの名前空間 URI 自体に = が含まれる場合がある
                // 。この条件の演算子として扱うのは {uri} の外側だけ。
                auto equals = String::npos;
                bool expanded = false;
                for (std::size_t i = 0; i < predicate.size(); ++i) {
                    if (predicate[i] == '{') expanded = true;
                    else if (predicate[i] == '}') expanded = false;
                    else if (!expanded && predicate[i] == '=') { equals = i; break; }
                }
                const auto key = equals == String::npos ? predicate : predicate.substr(0, equals);
                String expected;
                if (equals != String::npos) {
                    const auto quoted = predicate.substr(equals + 1);
                    if (quoted.size() < 2 || (quoted.front() != '\'' && quoted.front() != '"') || quoted.back() != quoted.front())
                        throw std::invalid_argument("invalid XML predicate");
                    expected = quoted.substr(1, quoted.size() - 2);
                }
                const auto uri_end = key.find('}');
                const auto local_key = uri_end == String::npos ? key : key.substr(uri_end + 1);
                if (key.empty() || local_key.find('(') != String::npos || local_key.find(')') != String::npos)
                    throw std::invalid_argument("unsupported XML predicate");
                const bool attribute = key.front() == '@';
                const auto name = attribute ? key.substr(1) : key;
                if (name.empty()) throw std::invalid_argument("invalid XML predicate");
                if (!attribute && name != "." && equals != String::npos && positional_integer(name))
                    throw std::invalid_argument("invalid XML predicate");
                if (name == "." && equals == String::npos)
                    throw std::invalid_argument("invalid XML predicate");
                validate_name(name);
                const auto text_matches = [&](const String& text) { return text == expected; };
                for (const auto& node : selected) {
                    bool keep = false;
                    if (attribute) {
                        const auto attr = node->m_attributes.find(name);
                        keep = attr != node->m_attributes.end() && (equals == String::npos || text_matches(attr->second));
                    } else if (name == "." && equals != String::npos) {
                        keep = text_matches(inner_text(node));
                    } else {
                        for (const auto& child : node->m_children) if (child && tag_matches(child->m_tag, name)) {
                            if (equals == String::npos || text_matches(inner_text(child))) { keep = true; break; }
                        }
                    }
                    if (keep) filtered.push_back(node);
                }
            }
            selected = std::move(filtered);
        }
        matches = std::move(selected);
    }
    return matches.empty() ? Node{} : matches.front();
}

} // namespace cw::lib::element_path
