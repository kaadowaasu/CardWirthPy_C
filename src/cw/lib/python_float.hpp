#pragma once
#include <cmath>
#include <charconv>
#include <array>
#include <string>
#include <algorithm>
#include <stdexcept>

namespace cw::lib {
// CPython の round(float) は、ホストの浮動小数点丸めモードに関係な
// く偶数丸めを使う。ここでは丸めた double を保持し、範囲検査は呼び出し側が行う。
inline double python_float_round_integral(double value)
{
    double rounded = std::round(value);
    if (std::fabs(value - rounded) == 0.5)
        rounded = 2.0 * std::round(value / 2.0);
    return rounded;
}
// Python の float の // は余りから整数の商を求める。丸めた除算の結果に床関
// 数を適用するだけでは、通常の音声レートでも一つずれる場合がある。参照: CPython
// 3.11 の Objects/floatobject.c、_float_div_mod。
inline double python_float_floor_divide(double numerator, double denominator)
{
    if (denominator == 0.0) throw std::domain_error("float floor division by zero");
    const double remainder = std::fmod(numerator, denominator);
    double quotient = (numerator - remainder) / denominator;
    if (remainder != 0.0 && ((remainder < 0.0) != (denominator < 0.0)))
        quotient -= 1.0;
    if (quotient == 0.0) return std::copysign(0.0, numerator / denominator);
    const double integral = std::floor(quotient);
    return quotient - integral > 0.5 ? integral + 1.0 : integral;
}
// Python の float の str/repr は元へ戻せる最短の桁を
// 使い、十進指数 -4～15 は固定小数点表記にする。固定表記の整数値には
// .0 を残す。ロケールや OS の UI に依存しない共通処理とする。
inline std::string python_float_repr(double value)
{
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return std::signbit(value) ? "-inf" : "inf";
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                      value, std::chars_format::general);
    if (result.ec != std::errc{}) throw std::runtime_error("float formatting failed");
    std::string text(buffer.data(), result.ptr);
    const bool negative = !text.empty() && text.front() == '-';
    if (negative) text.erase(0, 1);
    int exponent = 0;
    if (const auto position = text.find_first_of("eE"); position != std::string::npos) {
        exponent = std::stoi(text.substr(position + 1));
        text.erase(position);
    }
    const auto dot = text.find('.');
    int decimal_position = static_cast<int>(dot == std::string::npos ? text.size() : dot) + exponent;
    if (dot != std::string::npos) text.erase(dot, 1);
    // 少なくとも一桁を残し、意味のないゼロを除く。
    while (text.size() > 1 && text.back() == '0') text.pop_back();
    while (text.size() > 1 && text.front() == '0') { text.erase(0, 1); --decimal_position; }
    const bool zero = text == "0";
    if (zero) decimal_position = 1;
    const int scientific_exponent = decimal_position - 1;
    if (scientific_exponent >= -4 && scientific_exponent < 16) {
        if (decimal_position <= 0)
            text = "0." + std::string(static_cast<std::size_t>(-decimal_position), '0') + text;
        else if (decimal_position >= static_cast<int>(text.size()))
            text += std::string(static_cast<std::size_t>(decimal_position) - text.size(), '0') + ".0";
        else text.insert(static_cast<std::size_t>(decimal_position), 1, '.');
    } else {
        if (text.size() > 1) text.insert(1, 1, '.');
        auto digits = std::to_string(std::abs(scientific_exponent));
        if (digits.size() < 2) digits.insert(0, 1, '0');
        text += scientific_exponent < 0 ? "e-" : "e+";
        text += digits;
    }
    return negative ? "-" + text : text;
}

}
