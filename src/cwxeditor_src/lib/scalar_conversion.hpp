#pragma once

#include "cwxeditor_src/lib/real80.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace cwxeditor::lib::scalar_conversion {

[[noreturn]] inline void invalid_integral_value(const std::string_view text) {
    throw std::invalid_argument("invalid integral value: " + std::string(text));
}

template <typename T>
T integral_from_string(std::string_view text) {
    static_assert(std::is_integral_v<T> &&
                      !std::is_same_v<std::remove_cvref_t<T>, bool>,
                  "integral_from_string requires a non-boolean integral type");

    const auto originalText = text;
    if (text.empty()) invalid_integral_value(originalText);
    if constexpr (std::is_signed_v<T>) {
        if (text.front() == '+') {
            text.remove_prefix(1);
            // 元のDの変換は先頭のプラス記号を一つだけ許可する。残り
            // のトークンは別の符号ではなく数字から始まる必要がある。
            if (text.empty() || text.front() == '+' || text.front() == '-') {
                invalid_integral_value(originalText);
            }
        }
    }

    T result{};
    const auto* first = text.data();
    const auto* last = first + text.size();
    const auto parsed = std::from_chars(first, last, result);
    if (parsed.ec != std::errc{} || parsed.ptr != last) {
        invalid_integral_value(originalText);
    }
    return result;
}

[[noreturn]] inline void invalid_floating_value(const std::string_view text) {
    throw std::invalid_argument("invalid floating-point value: " + std::string(text));
}

template <typename T>
T floating_from_string(const std::string_view text) {
    static_assert(is_real_number_v<T>,
                  "floating_from_string requires a real-number type");
    Real80 intermediate;
    try {
        intermediate = Real80::parse(text);
    } catch (const std::invalid_argument&) {
        invalid_floating_value(text);
    }
    if constexpr (std::is_same_v<std::remove_cvref_t<T>, Real80>) {
        return intermediate;
    } else {
        T result{};
        if constexpr (std::is_same_v<T, float>) {
            result = intermediate.toFloat();
        } else if constexpr (std::is_same_v<T, double>) {
            result = intermediate.toDouble();
        } else {
            static_assert(std::is_same_v<T, long double>);
            result = intermediate.toLongDouble();
        }
        // Phobosはrealとして解析した後、最終的なTargetに収まるか確認する。
        // 明示的なinf/nanは許可するが、有限値からTargetへのオーバーフローは
        // 拒否し、ゼロへのアンダーフローは許可する。すべてのホストで同じ解析規則を使う。
        if (intermediate.isFinite() && !std::isfinite(result)) invalid_floating_value(text);
        return result;
    }
}

} // namespace cwxeditor::lib::scalar_conversion
