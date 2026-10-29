#pragma once

#include <charconv>
#include <cmath>
#include <initializer_list>
#include <locale>
#include <sstream>
#include <string_view>

namespace cw::keycode_level {
struct Key {
    std::string_view name;
    std::string_view result;
    double level{5.0};
};

// 不正な数値接尾辞はキーコードの文字列として残す。解析はロケールに依存させない。
inline Key parse(std::string_view text, double default_level = 5.0) {
    Key key{text, {}, default_level};
    for (const auto suffix : {std::string_view("○"), std::string_view("×")}) {
        if (text.ends_with(suffix)) {
            key.result = suffix;
            text.remove_suffix(suffix.size());
            key.name = text;
            break;
        }
    }
    // 先頭の@は既存の特殊な発火条件を表し、数値付きキーとしては扱わない。
    if (text.starts_with("@") || text.starts_with("＠")) return key;
    const auto ascii = text.rfind('@');
    const auto wide = text.rfind("＠");
    const bool use_wide = wide != text.npos && (ascii == text.npos || wide > ascii);
    const auto at = use_wide ? wide : ascii;
    if (at == text.npos || at == 0) return key;
    auto number = text.substr(at + (use_wide ? std::string_view("＠").size() : 1));
    if (number.starts_with('+')) {
        number.remove_prefix(1);
        if (number.starts_with('-') || number.starts_with('+')) return key;
    }
    if (number.empty()) return key;
    if (number.find_first_of(" \t\n\r\f\v") != number.npos) return key;
    double value{};
    std::istringstream stream{std::string(number)};
    stream.imbue(std::locale::classic());
    if (!(stream >> value)) return key;
    char trailing{};
    if ((stream >> trailing) || !stream.eof() || !std::isfinite(value))
        return key;
    key.name = text.substr(0, at);
    key.level = value;
    return key;
}

inline bool matches(std::string_view required, std::string_view supplied,
                    bool enabled, double default_level = 5.0) {
    if (!enabled) return required == supplied;
    const auto want = parse(required, default_level);
    const auto have = parse(supplied, default_level);
    return want.name == have.name && want.result == have.result && have.level >= want.level;
}
} // namespace cw::keycode_level
