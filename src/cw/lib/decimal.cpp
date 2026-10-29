#include "decimal.hpp"
#include "python_decimal_digit.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

namespace cw::lib {
namespace {

#if defined(CW_DECIMAL_HAS_BOOST)
using InternalCoefficient = boost::multiprecision::cpp_int;
#else
using InternalCoefficient = DecimalString;
#endif

constexpr std::size_t kHardMaxDigits = 1'000'000;

DecimalString strip_leading_zeroes(const DecimalString& input) {
    const std::size_t first = input.find_first_not_of('0');
    if (first == DecimalString::npos) {
        return "0";
    }
    return input.substr(first);
}

bool is_zero_string(const DecimalString& input) {
    return input == "0" || input.find_first_not_of('0') == DecimalString::npos;
}

#if !defined(CW_DECIMAL_HAS_BOOST)
int compare_strings(const DecimalString& lhs, const DecimalString& rhs) {
    const DecimalString a = strip_leading_zeroes(lhs);
    const DecimalString b = strip_leading_zeroes(rhs);
    if (a.size() != b.size()) {
        return a.size() < b.size() ? -1 : 1;
    }
    if (a == b) {
        return 0;
    }
    return a < b ? -1 : 1;
}

DecimalString add_strings(const DecimalString& lhs, const DecimalString& rhs) {
    std::size_t i = lhs.size();
    std::size_t j = rhs.size();
    int carry = 0;
    DecimalString result;
    result.reserve(std::max(i, j) + 1);
    while (i != 0 || j != 0 || carry != 0) {
        const int a = i == 0 ? 0 : lhs[--i] - '0';
        const int b = j == 0 ? 0 : rhs[--j] - '0';
        const int digit = a + b + carry;
        result.push_back(static_cast<char>('0' + digit % 10));
        carry = digit / 10;
    }
    std::reverse(result.begin(), result.end());
    return strip_leading_zeroes(result);
}

DecimalString subtract_strings(const DecimalString& lhs, const DecimalString& rhs) {
    // 呼び出し元は lhs >= rhs を保証する。
    std::size_t i = lhs.size();
    std::size_t j = rhs.size();
    int borrow = 0;
    DecimalString result;
    result.reserve(lhs.size());
    while (i != 0) {
        int digit = (lhs[--i] - '0') - borrow;
        const int b = j == 0 ? 0 : rhs[--j] - '0';
        if (digit < b) {
            digit += 10;
            borrow = 1;
        } else {
            borrow = 0;
        }
        result.push_back(static_cast<char>('0' + digit - b));
    }
    std::reverse(result.begin(), result.end());
    return strip_leading_zeroes(result);
}

DecimalString multiply_strings(const DecimalString& lhs, const DecimalString& rhs) {
    if (is_zero_string(lhs) || is_zero_string(rhs)) {
        return "0";
    }
    std::vector<std::uint64_t> digits(lhs.size() + rhs.size(), 0);
    for (std::size_t i = lhs.size(); i-- > 0;) {
        for (std::size_t j = rhs.size(); j-- > 0;) {
            digits[i + j + 1] += static_cast<std::uint64_t>(lhs[i] - '0') *
                                 static_cast<std::uint64_t>(rhs[j] - '0');
        }
    }
    for (std::size_t i = digits.size() - 1; i > 0; --i) {
        digits[i - 1] += digits[i] / 10U;
        digits[i] %= 10U;
    }
    DecimalString result;
    result.reserve(digits.size());
    for (const std::uint64_t digit : digits) {
        result.push_back(static_cast<char>('0' + digit));
    }
    return strip_leading_zeroes(result);
}

DecimalString multiply_small(const DecimalString& value, unsigned int multiplier) {
    if (multiplier == 0 || is_zero_string(value)) {
        return "0";
    }
    std::size_t i = value.size();
    unsigned int carry = 0;
    DecimalString result;
    result.reserve(value.size() + 3);
    while (i != 0 || carry != 0) {
        const unsigned int digit = i == 0 ? 0U : static_cast<unsigned int>(value[--i] - '0');
        const unsigned int product = digit * multiplier + carry;
        result.push_back(static_cast<char>('0' + product % 10));
        carry = product / 10;
    }
    std::reverse(result.begin(), result.end());
    return strip_leading_zeroes(result);
}

std::pair<DecimalString, DecimalString> divide_strings(const DecimalString& dividend,
                                                         const DecimalString& divisor) {
    if (is_zero_string(divisor)) {
        throw DecimalDivisionByZero("decimal integer division by zero");
    }
    DecimalString quotient;
    DecimalString remainder = "0";
    quotient.reserve(dividend.size());
    for (const char digit : dividend) {
        if (remainder == "0") {
            remainder.assign(1, digit);
        } else {
            remainder.push_back(digit);
        }
        remainder = strip_leading_zeroes(remainder);
        unsigned int qdigit = 0;
        // 十進の筆算除算で一桁の値は最大 9 となる。 上限付きのルー
        // プを使い、この代替実装を機械整数のビット幅に依存させない。
        for (unsigned int candidate = 9; candidate != 0; --candidate) {
            if (compare_strings(multiply_small(divisor, candidate), remainder) <= 0) {
                qdigit = candidate;
                remainder = subtract_strings(remainder, multiply_small(divisor, candidate));
                break;
            }
        }
        quotient.push_back(static_cast<char>('0' + qdigit));
    }
    return {strip_leading_zeroes(quotient), strip_leading_zeroes(remainder)};
}
#endif

InternalCoefficient coefficient_from_string(const DecimalString& text) {
#if defined(CW_DECIMAL_HAS_BOOST)
    InternalCoefficient result = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            throw DecimalInvalidOperation("invalid decimal coefficient");
        }
        result *= 10;
        result += static_cast<unsigned int>(ch - '0');
    }
    return result;
#else
    return strip_leading_zeroes(text);
#endif
}

DecimalString coefficient_to_string(const InternalCoefficient& value) {
#if defined(CW_DECIMAL_HAS_BOOST)
    return value.convert_to<DecimalString>();
#else
    return value;
#endif
}

bool coefficient_is_zero(const InternalCoefficient& value) {
#if defined(CW_DECIMAL_HAS_BOOST)
    return value == 0;
#else
    return is_zero_string(value);
#endif
}

int coefficient_compare(const InternalCoefficient& lhs, const InternalCoefficient& rhs) {
#if defined(CW_DECIMAL_HAS_BOOST)
    return lhs < rhs ? -1 : (lhs > rhs ? 1 : 0);
#else
    return compare_strings(lhs, rhs);
#endif
}

InternalCoefficient coefficient_add(const InternalCoefficient& lhs, const InternalCoefficient& rhs) {
#if defined(CW_DECIMAL_HAS_BOOST)
    return lhs + rhs;
#else
    return add_strings(lhs, rhs);
#endif
}

InternalCoefficient coefficient_subtract(const InternalCoefficient& lhs, const InternalCoefficient& rhs) {
#if defined(CW_DECIMAL_HAS_BOOST)
    if (lhs < rhs) {
        throw DecimalInvalidOperation("negative decimal coefficient");
    }
    return lhs - rhs;
#else
    if (compare_strings(lhs, rhs) < 0) {
        throw DecimalInvalidOperation("negative decimal coefficient");
    }
    return subtract_strings(lhs, rhs);
#endif
}

InternalCoefficient coefficient_multiply(const InternalCoefficient& lhs, const InternalCoefficient& rhs) {
#if defined(CW_DECIMAL_HAS_BOOST)
    return lhs * rhs;
#else
    return multiply_strings(lhs, rhs);
#endif
}

std::pair<InternalCoefficient, InternalCoefficient> coefficient_divide(const InternalCoefficient& lhs,
                                                                         const InternalCoefficient& rhs) {
#if defined(CW_DECIMAL_HAS_BOOST)
    if (rhs == 0) {
        throw DecimalDivisionByZero("decimal integer division by zero");
    }
    return {lhs / rhs, lhs % rhs};
#else
    const auto parts = divide_strings(lhs, rhs);
    return {parts.first, parts.second};
#endif
}

InternalCoefficient coefficient_pow10(std::size_t exponent) {
    if (exponent > kHardMaxDigits) {
        throw DecimalOverflow("decimal exponent is too large");
    }
#if defined(CW_DECIMAL_HAS_BOOST)
    InternalCoefficient value = 1;
    for (std::size_t i = 0; i < exponent; ++i) {
        value *= 10;
    }
    return value;
#else
    return DecimalString("1") + DecimalString(exponent, '0');
#endif
}

InternalCoefficient coefficient_multiply_small(const InternalCoefficient& value, unsigned int multiplier) {
#if defined(CW_DECIMAL_HAS_BOOST)
    return value * multiplier;
#else
    return multiply_small(value, multiplier);
#endif
}

InternalCoefficient coefficient_power_small(unsigned int base, std::size_t exponent) {
    InternalCoefficient result = coefficient_from_string("1");
    const InternalCoefficient multiplier = coefficient_from_string(std::to_string(base));
    for (std::size_t i = 0; i < exponent; ++i) {
        result = coefficient_multiply(result, multiplier);
    }
    return result;
}

InternalCoefficient coefficient_gcd(InternalCoefficient lhs, InternalCoefficient rhs) {
    while (!coefficient_is_zero(rhs)) {
        const auto parts = coefficient_divide(lhs, rhs);
        lhs = rhs;
        rhs = parts.second;
    }
    return lhs;
}

bool checked_add(DecimalInteger lhs, DecimalInteger rhs, DecimalInteger& result) {
    if ((rhs > 0 && lhs > std::numeric_limits<DecimalInteger>::max() - rhs) ||
        (rhs < 0 && lhs < std::numeric_limits<DecimalInteger>::min() - rhs)) {
        return false;
    }
    result = static_cast<DecimalInteger>(lhs + rhs);
    return true;
}

bool checked_subtract(DecimalInteger lhs, DecimalInteger rhs, DecimalInteger& result) {
    if (rhs == std::numeric_limits<DecimalInteger>::min()) {
        if (lhs >= 0) {
            return false;
        }
        result = static_cast<DecimalInteger>(lhs - rhs);
        return true;
    }
    return checked_add(lhs, static_cast<DecimalInteger>(-rhs), result);
}

std::size_t positive_scale_difference(DecimalInteger larger, DecimalInteger smaller) {
    DecimalInteger difference = 0;
    if (!checked_subtract(larger, smaller, difference) || difference < 0) {
        throw DecimalOverflow("decimal scale difference is too large");
    }
    if (static_cast<std::uint64_t>(difference) > kHardMaxDigits) {
        throw DecimalOverflow("decimal scale difference is too large");
    }
    return static_cast<std::size_t>(difference);
}

InternalCoefficient scale_coefficient(const InternalCoefficient& coefficient, std::size_t exponent) {
    if (exponent == 0 || coefficient_is_zero(coefficient)) {
        return coefficient;
    }
    return coefficient_multiply(coefficient, coefficient_pow10(exponent));
}

bool coefficient_is_odd(const InternalCoefficient& value) {
    return !coefficient_is_zero(coefficient_divide(value, coefficient_from_string("2")).second);
}

InternalCoefficient round_quotient_clean(const InternalCoefficient& numerator,
                                         const InternalCoefficient& denominator,
                                         bool negative,
                                         DecimalRounding rounding,
                                         InternalCoefficient* remainder_out = nullptr) {
    const auto parts = coefficient_divide(numerator, denominator);
    if (remainder_out != nullptr) {
        *remainder_out = parts.second;
    }
    if (coefficient_is_zero(parts.second)) {
        return parts.first;
    }
    bool increment = false;
    switch (rounding) {
    case DecimalRounding::Down:
        break;
    case DecimalRounding::Floor:
        increment = negative;
        break;
    case DecimalRounding::Ceiling:
        increment = !negative;
        break;
    case DecimalRounding::HalfEven: {
        const int cmp = coefficient_compare(coefficient_multiply_small(parts.second, 2), denominator);
        increment = cmp > 0 || (cmp == 0 && coefficient_is_odd(parts.first));
        break;
    }
    case DecimalRounding::HalfUp: {
        const int cmp = coefficient_compare(coefficient_multiply_small(parts.second, 2), denominator);
        increment = cmp >= 0;
        break;
    }
    }
    return increment ? coefficient_add(parts.first, coefficient_from_string("1")) : parts.first;
}

struct RationalMagnitude {
    InternalCoefficient numerator;
    InternalCoefficient denominator;
    bool negative;
};

RationalMagnitude ratio_for(const Decimal& lhs, const Decimal& rhs) {
    const DecimalString lhs_coeff = lhs.coefficient_string();
    const DecimalString rhs_coeff = rhs.coefficient_string();
    const InternalCoefficient lhs_value = coefficient_from_string(lhs_coeff);
    const InternalCoefficient rhs_value = coefficient_from_string(rhs_coeff);
    if (coefficient_is_zero(rhs_value)) {
        if (coefficient_is_zero(lhs_value)) throw DecimalInvalidOperation("zero divided by zero");
        throw DecimalDivisionByZero("decimal division by zero");
    }
    DecimalInteger delta = 0;
    if (!checked_subtract(rhs.scale(), lhs.scale(), delta)) {
        throw DecimalOverflow("decimal scale difference overflow");
    }
    RationalMagnitude result{lhs_value, rhs_value, lhs.negative() != rhs.negative()};
    if (delta >= 0) {
        result.numerator = scale_coefficient(result.numerator, static_cast<std::size_t>(delta));
    } else {
        DecimalInteger positive_delta = 0;
        if (!checked_subtract(0, delta, positive_delta)) {
            throw DecimalOverflow("decimal scale difference overflow");
        }
        result.denominator = scale_coefficient(result.denominator, static_cast<std::size_t>(positive_delta));
    }
    return result;
}

// 正の整数に対して floor(log10(numerator / denominator)) を求める。
DecimalInteger ratio_exponent(const InternalCoefficient& numerator, const InternalCoefficient& denominator) {
    const DecimalInteger digit_difference = static_cast<DecimalInteger>(coefficient_to_string(numerator).size()) -
                                            static_cast<DecimalInteger>(coefficient_to_string(denominator).size());
    if (digit_difference >= 0) {
        const InternalCoefficient adjusted = scale_coefficient(denominator, static_cast<std::size_t>(digit_difference));
        return coefficient_compare(numerator, adjusted) < 0 ? digit_difference - 1 : digit_difference;
    }
    const std::size_t shift = static_cast<std::size_t>(-digit_difference);
    const InternalCoefficient adjusted = scale_coefficient(numerator, shift);
    return coefficient_compare(adjusted, denominator) < 0 ? digit_difference - 1 : digit_difference;
}

void ensure_digit_bound(const InternalCoefficient& coefficient, std::size_t max_digits) {
    const std::size_t allowed = max_digits == 0 ? kHardMaxDigits : std::min(max_digits, kHardMaxDigits);
    if (coefficient_to_string(coefficient).size() > allowed) {
        throw DecimalOverflow("decimal coefficient exceeds configured limit");
    }
}

DecimalInteger parse_signed_integer(const DecimalString& text, std::size_t begin, std::size_t end) {
    if (begin >= end) {
        throw DecimalInvalidOperation("missing decimal exponent");
    }
    bool negative = false;
    if (text[begin] == '+' || text[begin] == '-') {
        negative = text[begin] == '-';
        ++begin;
    }
    if (begin >= end) {
        throw DecimalInvalidOperation("missing decimal exponent digits");
    }
    std::uint64_t magnitude = 0;
    const std::uint64_t positive_limit = static_cast<std::uint64_t>(std::numeric_limits<DecimalInteger>::max());
    const std::uint64_t negative_limit = positive_limit + 1U;
    const std::uint64_t limit = negative ? negative_limit : positive_limit;
    for (; begin < end; ++begin) {
        const char ch = text[begin];
        if (ch < '0' || ch > '9') {
            throw DecimalInvalidOperation("invalid decimal exponent");
        }
        const unsigned int digit = static_cast<unsigned int>(ch - '0');
        if (magnitude > (limit - digit) / 10U) {
            throw DecimalOverflow("decimal exponent overflows int64");
        }
        magnitude = magnitude * 10U + digit;
    }
    if (negative) {
        if (magnitude == negative_limit) {
            return std::numeric_limits<DecimalInteger>::min();
        }
        return -static_cast<DecimalInteger>(magnitude);
    }
    return static_cast<DecimalInteger>(magnitude);
}

DecimalInteger decimal_integer_from_coefficient(const InternalCoefficient& coefficient,
                                                 bool negative,
                                                 std::size_t max_digits) {
    ensure_digit_bound(coefficient, max_digits);
    const DecimalString digits = coefficient_to_string(coefficient);
    const std::uint64_t positive_limit = static_cast<std::uint64_t>(std::numeric_limits<DecimalInteger>::max());
    const std::uint64_t negative_limit = positive_limit + 1U;
    const std::uint64_t limit = negative ? negative_limit : positive_limit;
    std::uint64_t value = 0;
    for (const char ch : digits) {
        const unsigned int digit = static_cast<unsigned int>(ch - '0');
        if (value > (limit - digit) / 10U) {
            throw DecimalOverflow("decimal integer conversion overflows int64");
        }
        value = value * 10U + digit;
    }
    if (!negative) {
        return static_cast<DecimalInteger>(value);
    }
    if (value == negative_limit) {
        return std::numeric_limits<DecimalInteger>::min();
    }
    return -static_cast<DecimalInteger>(value);
}

} // namespace

DecimalContext DecimalContext::default_context() noexcept {
    return DecimalContext{};
}

DecimalError::DecimalError(Kind kind, const DecimalString& message)
    : std::runtime_error(message), m_kind(kind) {}

DecimalInvalidOperation::DecimalInvalidOperation(const DecimalString& message)
    : DecimalError(Kind::InvalidOperation, message) {}

DecimalDivisionByZero::DecimalDivisionByZero(const DecimalString& message)
    : DecimalError(Kind::DivisionByZero, message) {}

DecimalOverflow::DecimalOverflow(const DecimalString& message)
    : DecimalError(Kind::Overflow, message) {}

Decimal::Decimal() noexcept
    : m_negative(false), m_scale(0), m_coefficient(coefficient_from_string("0")) {}

Decimal::Decimal(DecimalInteger value)
    : m_negative(value < 0), m_scale(0), m_coefficient(coefficient_from_string("0")) {
    const DecimalString text = std::to_string(value);
    const std::size_t begin = m_negative ? 1 : 0;
    m_coefficient = coefficient_from_string(text.substr(begin));
    if (coefficient_is_zero(m_coefficient)) {
        m_negative = false;
    }
}

Decimal::Decimal(const DecimalString& text)
    : Decimal(parse(text)) {}

Decimal::Decimal(bool negative, DecimalInteger scale, const Coefficient& coefficient)
    : m_negative(negative), m_scale(scale), m_coefficient(coefficient) {}

Decimal Decimal::parse(const DecimalString& input, const DecimalContext& context) {
    // Python の Decimal と同様、文字列入力のアンダースコアと Unicode の十進数
    // 字 Nd を受け付ける。 共通境界で正規化し、式の字句解析は分ける。元の Python 3.8
    // に合わせ、Unicode 12.1 の Nd、630 文字を使う。 unicode.org/Public/12.1.0/ucd/extracted/DerivedNumericType.txt
    //

    DecimalString source;
    source.reserve(input.size());
    for (std::size_t i = 0; i < input.size();) {
        const auto first = static_cast<unsigned char>(input[i++]);
        unsigned cp = first;
        unsigned remaining = 0;
        if (first >= 0xc2 && first <= 0xdf) { cp = first & 0x1f; remaining = 1; }
        else if (first >= 0xe0 && first <= 0xef) { cp = first & 0x0f; remaining = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { cp = first & 0x07; remaining = 3; }
        else if (first >= 0x80) throw DecimalInvalidOperation("invalid UTF-8 decimal string");
        for (unsigned n = 0; n < remaining; ++n) {
            if (i >= input.size() || (static_cast<unsigned char>(input[i]) & 0xc0) != 0x80)
                throw DecimalInvalidOperation("invalid UTF-8 decimal string");
            cp = (cp << 6) | (static_cast<unsigned char>(input[i++]) & 0x3f);
        }
        if ((remaining == 2 && cp < 0x800) || (remaining == 3 && cp < 0x10000) ||
            (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff)
            throw DecimalInvalidOperation("invalid UTF-8 decimal string");
        if (cp == '_') continue;
        if (const auto digit = python_decimal_digit(cp)) {
            source.push_back(static_cast<char>('0' + *digit)); continue;
        }
        if ((cp >= 0x1c && cp <= 0x1f) || cp == 0x85 || cp == 0xa0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) ||
            cp == 0x2028 || cp == 0x2029 || cp == 0x202f || cp == 0x205f || cp == 0x3000)
            source.push_back(' ');
        else if (cp < 0x80) source.push_back(static_cast<char>(cp));
        else throw DecimalInvalidOperation("invalid character in decimal string");
    }
    if (source.empty()) {
        throw DecimalInvalidOperation("empty decimal string");
    }
    std::size_t begin = 0;
    while (begin < source.size() && std::isspace(static_cast<unsigned char>(source[begin])) != 0) {
        ++begin;
    }
    std::size_t end = source.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(source[end - 1])) != 0) {
        --end;
    }
    if (begin == end) {
        throw DecimalInvalidOperation("empty decimal string");
    }

    bool negative = false;
    if (source[begin] == '+' || source[begin] == '-') {
        negative = source[begin] == '-';
        ++begin;
    }
    if (begin == end) {
        throw DecimalInvalidOperation("missing decimal digits");
    }

    DecimalString special = source.substr(begin, end - begin);
    std::transform(special.begin(), special.end(), special.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (special == "inf" || special == "infinity" || special.rfind("nan", 0) == 0 || special.rfind("snan", 0) == 0) {
        Decimal result;
        result.m_negative = negative;
        if (special == "inf" || special == "infinity") result.m_special = Special::Infinity;
        else {
            const bool signaling = special.front() == 's';
            auto payload = special.substr(signaling ? 4 : 3);
            if (!std::all_of(payload.begin(), payload.end(), [](char c) { return c >= '0' && c <= '9'; }))
                throw DecimalInvalidOperation("invalid NaN payload");
            payload = strip_leading_zeroes(payload);
            if (payload == "0") payload.clear();
            if (payload.size() > context.max_digits) throw DecimalOverflow("NaN payload is too large");
            result.m_special = signaling ? Special::SignalingNaN : Special::QuietNaN;
            result.m_nan_payload = std::move(payload);
        }
        return result;
    }

    std::size_t exponent_position = DecimalString::npos;
    for (std::size_t i = begin; i < end; ++i) {
        if (source[i] == 'e' || source[i] == 'E') {
            if (exponent_position != DecimalString::npos) {
                throw DecimalInvalidOperation("multiple decimal exponents");
            }
            exponent_position = i;
        }
    }
    const std::size_t significand_end = exponent_position == DecimalString::npos ? end : exponent_position;
    DecimalInteger exponent = 0;
    if (exponent_position != DecimalString::npos) {
        exponent = parse_signed_integer(source, exponent_position + 1, end);
    }

    DecimalString digits;
    digits.reserve(significand_end - begin);
    std::size_t fractional_digits = 0;
    bool dot_seen = false;
    bool digit_seen = false;
    for (std::size_t i = begin; i < significand_end; ++i) {
        const char ch = source[i];
        if (ch == '.') {
            if (dot_seen) {
                throw DecimalInvalidOperation("multiple decimal points");
            }
            dot_seen = true;
        } else if (ch >= '0' && ch <= '9') {
            digits.push_back(ch);
            digit_seen = true;
            if (dot_seen) {
                ++fractional_digits;
            }
        } else {
            throw DecimalInvalidOperation("invalid character in decimal string");
        }
    }
    if (!digit_seen) {
        throw DecimalInvalidOperation("decimal string has no digits");
    }
    digits = strip_leading_zeroes(digits);

    if (fractional_digits > static_cast<std::size_t>(std::numeric_limits<DecimalInteger>::max())) {
        throw DecimalOverflow("decimal fractional scale overflows int64");
    }
    DecimalInteger scale = 0;
    if (!checked_subtract(static_cast<DecimalInteger>(fractional_digits), exponent, scale)) {
        throw DecimalOverflow("decimal scale overflows int64");
    }
    const InternalCoefficient coefficient = coefficient_from_string(digits);
    ensure_digit_bound(coefficient, context.max_digits);
    if (coefficient_is_zero(coefficient)) {
        return Decimal(negative, scale, coefficient);
    }
    return Decimal(negative, scale, coefficient);
}

bool Decimal::is_zero() const noexcept {
    return is_finite() && coefficient_is_zero(m_coefficient);
}

Decimal Decimal::checked_nan(const Decimal* rhs, const DecimalContext& context) const {
    if (is_snan() || (rhs && rhs->is_snan())) throw DecimalInvalidOperation("signaling NaN operation");
    auto result = is_nan() ? *this : *rhs;
    if (context.precision && result.m_nan_payload.size() > context.precision) {
        result.m_nan_payload.erase(0, result.m_nan_payload.size() - context.precision);
        result.m_nan_payload = strip_leading_zeroes(result.m_nan_payload);
        if (result.m_nan_payload == "0") result.m_nan_payload.clear();
    }
    return result;
}

DecimalString Decimal::coefficient_string() const {
    return coefficient_to_string(m_coefficient);
}

double Decimal::to_real() const {
    if (is_snan()) throw DecimalInvalidOperation("cannot convert signaling NaN to float");
    if (is_nan()) return std::copysign(std::numeric_limits<double>::quiet_NaN(), m_negative ? -1.0 : 1.0);
    // strtod はアンダーフローを符号付きゼロ・非正規化数、オーバーフローを無限大
    // として保持する。 std::stod は両方の ERANGE で out_of_range
    // を投げ、保存値の小さな有限小数が誤って無限大になるため使わない。
    const auto text = to_string(false);
    return std::strtod(text.c_str(), nullptr);
}

DecimalString Decimal::to_string(bool preserve_scale) const {
    if (!is_finite()) return (m_negative ? "-" : "") +
        (is_nan() ? (is_snan() ? DecimalString("sNaN") : DecimalString("NaN")) + m_nan_payload : DecimalString("Infinity"));
    if (!preserve_scale) {
        return normalize().to_string(true);
    }
    DecimalString digits = coefficient_string();
    DecimalString result;
    if (m_scale >= 0) {
        const std::size_t scale = static_cast<std::size_t>(m_scale);
        if (scale > kHardMaxDigits) {
            if (is_zero()) return (m_negative ? DecimalString("-0E-") : DecimalString("0E-")) + std::to_string(scale);
            throw DecimalOverflow("decimal output scale is too large");
        }
        if (scale == 0) {
            result = digits;
        } else if (digits.size() > scale) {
            const std::size_t point = digits.size() - scale;
            result = digits.substr(0, point) + "." + digits.substr(point);
        } else {
            result = "0." + DecimalString(scale - digits.size(), '0') + digits;
        }
    } else {
        DecimalInteger positive_scale = 0;
        if (!checked_subtract(0, m_scale, positive_scale) ||
            static_cast<std::uint64_t>(positive_scale) > kHardMaxDigits) {
            throw DecimalOverflow("decimal output exponent is too large");
        }
        result = digits + DecimalString(static_cast<std::size_t>(positive_scale), '0');
    }
    if (m_negative) {
        result.insert(result.begin(), '-');
    }
    return result;
}

Decimal Decimal::normalize() const {
    if (is_nan()) return checked_nan();
    if (!is_finite()) return *this;
    if (is_zero()) {
        return Decimal(m_negative, 0, m_coefficient);
    }
    InternalCoefficient coefficient = m_coefficient;
    DecimalInteger scale = m_scale;
    const InternalCoefficient ten = coefficient_from_string("10");
    while (!coefficient_is_zero(coefficient)) {
        const auto parts = coefficient_divide(coefficient, ten);
        if (!coefficient_is_zero(parts.second)) {
            break;
        }
        coefficient = parts.first;
        if (!checked_subtract(scale, 1, scale)) {
            throw DecimalOverflow("decimal scale underflow while normalizing");
        }
    }
    return Decimal(m_negative, scale, coefficient);
}

Decimal Decimal::trunc() const {
    if (is_nan()) return checked_nan();
    if (!is_finite()) return *this;
    if (m_scale <= 0) {
        return *this;
    }
    const InternalCoefficient divisor = coefficient_pow10(static_cast<std::size_t>(m_scale));
    const auto parts = coefficient_divide(m_coefficient, divisor);
    return Decimal(m_negative, 0, parts.first);
}

Decimal Decimal::floor() const {
    if (is_nan()) return checked_nan();
    if (!is_finite()) return *this;
    Decimal result = trunc();
    if (m_negative && !is_zero()) {
        // 負の小数の floor は、ゼロ方向の切り捨てより一つ小さい。 -0.x
        // でも trunc() はゼロになるため、同じ調整を行う。
        if (m_scale > 0) {
            const InternalCoefficient divisor = coefficient_pow10(static_cast<std::size_t>(m_scale));
            if (!coefficient_is_zero(coefficient_divide(m_coefficient, divisor).second)) {
                result = result.subtract(Decimal(1), DecimalContext{0, DecimalRounding::Down, kHardMaxDigits});
            }
        }
    }
    return result;
}

Decimal Decimal::ceil() const {
    if (is_nan()) return checked_nan();
    if (!is_finite()) return *this;
    Decimal result = trunc();
    if (!m_negative && !is_zero() && m_scale > 0) {
        const InternalCoefficient divisor = coefficient_pow10(static_cast<std::size_t>(m_scale));
        if (!coefficient_is_zero(coefficient_divide(m_coefficient, divisor).second)) {
            result = result.add(Decimal(1), DecimalContext{0, DecimalRounding::Down, kHardMaxDigits});
        }
    }
    return result;
}

DecimalInteger Decimal::to_integer(DecimalRounding rounding) const {
    if (!is_finite()) throw DecimalInvalidOperation("cannot convert non-finite Decimal to integer");
    if (m_scale <= 0) {
        DecimalInteger positive_scale = 0;
        if (!checked_subtract(0, m_scale, positive_scale)) {
            throw DecimalOverflow("decimal integer conversion scale is too large");
        }
        const InternalCoefficient value = scale_coefficient(m_coefficient, static_cast<std::size_t>(positive_scale));
        return decimal_integer_from_coefficient(value, m_negative, kHardMaxDigits);
    }
    const InternalCoefficient divisor = coefficient_pow10(static_cast<std::size_t>(m_scale));
    const InternalCoefficient value = round_quotient_clean(m_coefficient, divisor, m_negative, rounding);
    return decimal_integer_from_coefficient(value, m_negative, kHardMaxDigits);
}

Decimal Decimal::plus(const DecimalContext& context) const {
    auto result = apply_context(context);
    if (result.is_zero() && context.rounding != DecimalRounding::Floor) result.m_negative = false;
    return result;
}

Decimal Decimal::minus(const DecimalContext& context) const {
    if (is_nan()) return checked_nan(nullptr, context);
    auto result = *this;
    result.m_negative = !result.m_negative;
    return result.plus(context);
}

Decimal Decimal::add(const Decimal& rhs, const DecimalContext& context) const {
    if (is_nan() || rhs.is_nan()) return checked_nan(&rhs, context);
    if (!is_finite() || !rhs.is_finite()) {
        if (!is_finite() && !rhs.is_finite() && m_negative != rhs.m_negative)
            throw DecimalInvalidOperation("opposite infinities in addition");
        return !is_finite() ? *this : rhs;
    }
    const DecimalInteger common_scale = std::max(m_scale, rhs.m_scale);
    const InternalCoefficient lhs_value = scale_coefficient(m_coefficient, positive_scale_difference(common_scale, m_scale));
    const InternalCoefficient rhs_value = scale_coefficient(rhs.m_coefficient, positive_scale_difference(common_scale, rhs.m_scale));
    InternalCoefficient coefficient;
    bool negative = false;
    if (m_negative == rhs.m_negative) {
        coefficient = coefficient_add(lhs_value, rhs_value);
        negative = m_negative;
    } else {
        const int comparison = coefficient_compare(lhs_value, rhs_value);
        if (comparison == 0) {
            return Decimal(context.rounding == DecimalRounding::Floor, common_scale, coefficient_from_string("0"));
        }
        if (comparison > 0) {
            coefficient = coefficient_subtract(lhs_value, rhs_value);
            negative = m_negative;
        } else {
            coefficient = coefficient_subtract(rhs_value, lhs_value);
            negative = rhs.m_negative;
        }
    }
    ensure_digit_bound(coefficient, context.max_digits);
    return Decimal(negative, common_scale, coefficient).apply_context(context);
}

Decimal Decimal::subtract(const Decimal& rhs, const DecimalContext& context) const {
    if (is_nan() || rhs.is_nan()) return checked_nan(&rhs, context);
    Decimal negated = rhs;
    negated.m_negative = !negated.m_negative;
    return add(negated, context);
}

Decimal Decimal::multiply(const Decimal& rhs, const DecimalContext& context) const {
    if (is_nan() || rhs.is_nan()) return checked_nan(&rhs, context);
    if (!is_finite() || !rhs.is_finite()) {
        if (is_zero() || rhs.is_zero()) throw DecimalInvalidOperation("zero times infinity");
        Decimal result = !is_finite() ? *this : rhs;
        result.m_negative = m_negative != rhs.m_negative;
        return result;
    }
    DecimalInteger scale = 0;
    if (!checked_add(m_scale, rhs.m_scale, scale)) {
        throw DecimalOverflow("decimal multiplication scale overflow");
    }
    const InternalCoefficient coefficient = coefficient_multiply(m_coefficient, rhs.m_coefficient);
    ensure_digit_bound(coefficient, context.max_digits);
    return Decimal(m_negative != rhs.m_negative, scale, coefficient).apply_context(context);
}

Decimal Decimal::divide(const Decimal& rhs, const DecimalContext& context) const {
    if (is_nan() || rhs.is_nan()) return checked_nan(&rhs, context);
    if (!is_finite() || !rhs.is_finite()) {
        if (!is_finite() && !rhs.is_finite()) throw DecimalInvalidOperation("infinity divided by infinity");
        Decimal result = !is_finite() ? *this : Decimal{};
        result.m_negative = m_negative != rhs.m_negative;
        // CPython の既定の Emin は -999999 であり、商がゼロの場合は Etiny を使う。
        if (context.precision > kHardMaxDigits) throw DecimalOverflow("decimal precision is too large");
        if (is_finite()) result.m_scale = 999999 + static_cast<DecimalInteger>(context.precision ? context.precision - 1 : 0);
        return result;
    }
    const RationalMagnitude ratio = ratio_for(*this, rhs);
    if (coefficient_is_zero(ratio.numerator)) {
        DecimalInteger preferred_scale = 0;
        if (!checked_subtract(m_scale, rhs.m_scale, preferred_scale))
            throw DecimalOverflow("decimal quotient preferred scale overflow");
        return Decimal(ratio.negative, preferred_scale, coefficient_from_string("0"));
    }

    if (context.precision == 0) {
        InternalCoefficient numerator = ratio.numerator;
        InternalCoefficient denominator = ratio.denominator;
        const InternalCoefficient gcd = coefficient_gcd(numerator, denominator);
        numerator = coefficient_divide(numerator, gcd).first;
        denominator = coefficient_divide(denominator, gcd).first;
        std::size_t twos = 0;
        std::size_t fives = 0;
        const InternalCoefficient two = coefficient_from_string("2");
        const InternalCoefficient five = coefficient_from_string("5");
        while (true) {
            const auto parts = coefficient_divide(denominator, two);
            if (!coefficient_is_zero(parts.second)) break;
            denominator = parts.first;
            ++twos;
        }
        while (true) {
            const auto parts = coefficient_divide(denominator, five);
            if (!coefficient_is_zero(parts.second)) break;
            denominator = parts.first;
            ++fives;
        }
        if (denominator != coefficient_from_string("1")) {
            throw DecimalInvalidOperation("non-terminating decimal division requires a precision");
        }
        const std::size_t scale = std::max(twos, fives);
        if (scale > kHardMaxDigits) {
            throw DecimalOverflow("exact decimal quotient scale is too large");
        }
        if (scale > twos) {
            numerator = coefficient_multiply(numerator, coefficient_power_small(2, scale - twos));
        }
        if (scale > fives) {
            numerator = coefficient_multiply(numerator, coefficient_power_small(5, scale - fives));
        }
        ensure_digit_bound(numerator, context.max_digits);
        return Decimal(ratio.negative, static_cast<DecimalInteger>(scale), numerator);
    }

    if (context.precision > static_cast<std::size_t>(std::numeric_limits<DecimalInteger>::max() - 1)) {
        throw DecimalOverflow("decimal precision is too large");
    }
    const DecimalInteger exponent = ratio_exponent(ratio.numerator, ratio.denominator);
    const DecimalInteger precision = static_cast<DecimalInteger>(context.precision);
    DecimalInteger target_scale = 0;
    if (!checked_subtract(precision, exponent + 1, target_scale)) {
        throw DecimalOverflow("decimal quotient scale overflow");
    }
    InternalCoefficient numerator = ratio.numerator;
    InternalCoefficient denominator = ratio.denominator;
    if (target_scale >= 0) {
        numerator = scale_coefficient(numerator, static_cast<std::size_t>(target_scale));
    } else {
        DecimalInteger positive_scale = 0;
        if (!checked_subtract(0, target_scale, positive_scale)) {
            throw DecimalOverflow("decimal quotient scale overflow");
        }
        denominator = scale_coefficient(denominator, static_cast<std::size_t>(positive_scale));
    }
    InternalCoefficient coefficient = round_quotient_clean(numerator, denominator, ratio.negative,
                                                            context.rounding);
    while (coefficient_to_string(coefficient).size() > context.precision) {
        if (!checked_subtract(target_scale, 1, target_scale)) {
            throw DecimalOverflow("decimal quotient scale underflow");
        }
        numerator = ratio.numerator;
        denominator = ratio.denominator;
        if (target_scale >= 0) {
            numerator = scale_coefficient(numerator, static_cast<std::size_t>(target_scale));
        } else {
            DecimalInteger positive_scale = 0;
            if (!checked_subtract(0, target_scale, positive_scale)) {
                throw DecimalOverflow("decimal quotient scale overflow");
            }
            denominator = scale_coefficient(denominator, static_cast<std::size_t>(positive_scale));
        }
        coefficient = round_quotient_clean(numerator, denominator, ratio.negative, context.rounding);
    }
    ensure_digit_bound(coefficient, context.max_digits);
    // 正確な商では、コンテキストの精度へ合わせるために追加したゼロだけを取り除
    // く。 1.0000/8 のような演算で、元の値が求める指数は維持する。
    if (coefficient_is_zero(coefficient_divide(numerator, denominator).second)) {
        DecimalInteger preferred_scale = 0;
        if (!checked_subtract(m_scale, rhs.m_scale, preferred_scale))
            throw DecimalOverflow("decimal quotient preferred scale overflow");
        const auto ten = coefficient_from_string("10");
        while (target_scale > preferred_scale && !coefficient_is_zero(coefficient)) {
            const auto parts = coefficient_divide(coefficient, ten);
            if (!coefficient_is_zero(parts.second)) break;
            coefficient = parts.first;
            --target_scale;
        }
    }
    return Decimal(ratio.negative, target_scale, coefficient);
}

Decimal Decimal::floor_divide(const Decimal& rhs, const DecimalContext& context) const {
    if (is_nan() || rhs.is_nan()) return checked_nan(&rhs, context);
    if (!is_finite() || !rhs.is_finite()) {
        auto result = divide(rhs, context);
        if (result.is_zero()) result.m_scale = 0;
        return result;
    }
    const RationalMagnitude ratio = ratio_for(*this, rhs);
    const auto parts = coefficient_divide(ratio.numerator, ratio.denominator);
    InternalCoefficient quotient = parts.first;
    if (ratio.negative && !coefficient_is_zero(parts.second)) {
        quotient = coefficient_add(quotient, coefficient_from_string("1"));
    }
    ensure_digit_bound(quotient, context.max_digits);
    return Decimal(ratio.negative, 0, quotient);
}

Decimal Decimal::remainder(const Decimal& rhs, const DecimalContext& context) const {
    if (is_nan() || rhs.is_nan()) return checked_nan(&rhs, context);
    if (!is_finite()) throw DecimalInvalidOperation("infinite dividend in remainder");
    if (!rhs.is_finite()) return apply_context(context);
    if (rhs.is_zero()) throw DecimalInvalidOperation("decimal remainder by zero");
    const RationalMagnitude ratio = ratio_for(*this, rhs);
    const auto parts = coefficient_divide(ratio.numerator, ratio.denominator);
    if (context.precision && coefficient_to_string(parts.first).size() > context.precision)
        throw DecimalInvalidOperation("decimal remainder quotient exceeds precision");
    const Decimal quotient(ratio.negative, 0, parts.first);
    const DecimalContext exact{0, DecimalRounding::Down, context.max_digits};
    auto result = subtract(rhs.multiply(quotient, exact), exact);
    result.m_negative = m_negative;
    return result;
}

Decimal Decimal::quantize(const Decimal& exponent, DecimalRounding rounding, std::size_t precision) const {
    if (is_nan() || exponent.is_nan()) {
        auto context = DecimalContext::default_context();
        if (precision) context.precision = precision;
        return checked_nan(&exponent, context);
    }
    if (!is_finite() || !exponent.is_finite()) {
        if (!is_finite() && !exponent.is_finite()) return *this;
        throw DecimalInvalidOperation("infinite quantize operand");
    }
    if (m_scale == exponent.m_scale) {
        if (precision != 0 && coefficient_to_string(m_coefficient).size() > precision) {
            throw DecimalInvalidOperation("quantized decimal exceeds precision");
        }
        return *this;
    }
    InternalCoefficient coefficient = m_coefficient;
    if (exponent.m_scale > m_scale) {
        coefficient = scale_coefficient(coefficient, positive_scale_difference(exponent.m_scale, m_scale));
    } else {
        const std::size_t divisor_scale = positive_scale_difference(m_scale, exponent.m_scale);
        const InternalCoefficient divisor = coefficient_pow10(divisor_scale);
        coefficient = round_quotient_clean(coefficient, divisor, m_negative, rounding);
    }
    if (precision != 0 && coefficient_to_string(coefficient).size() > precision) {
        throw DecimalInvalidOperation("quantized decimal exceeds precision");
    }
    ensure_digit_bound(coefficient, kHardMaxDigits);
    return Decimal(m_negative, exponent.m_scale, coefficient);
}

Decimal Decimal::apply_context(const DecimalContext& context) const {
    if (is_nan()) return checked_nan(nullptr, context);
    if (!is_finite()) return *this;
    ensure_digit_bound(m_coefficient, context.max_digits);
    if (context.precision == 0 || is_zero()) {
        return *this;
    }
    const std::size_t digits = coefficient_string().size();
    if (digits <= context.precision) {
        return *this;
    }
    const std::size_t difference = digits - context.precision;
    if (difference > static_cast<std::size_t>(std::numeric_limits<DecimalInteger>::max())) {
        throw DecimalOverflow("decimal precision difference is too large");
    }
    DecimalInteger target_scale = 0;
    if (!checked_subtract(m_scale, static_cast<DecimalInteger>(difference), target_scale)) {
        throw DecimalOverflow("decimal context scale overflow");
    }
    Decimal exponent(false, target_scale, coefficient_from_string("1"));
    Decimal result = quantize(exponent, context.rounding, 0);
    // 丸めによって 99... が 100... となり、有効桁が一つ増える場合
    // がある。 コンテキスト演算では指数をもう一度調整するが、指数固定の quantize()
    // は正しく InvalidOperation を通知する。
    while (result.coefficient_string().size() > context.precision) {
        if (!checked_subtract(target_scale, 1, target_scale)) {
            throw DecimalOverflow("decimal context scale underflow");
        }
        exponent = Decimal(false, target_scale, coefficient_from_string("1"));
        result = quantize(exponent, context.rounding, 0);
    }
    return result;
}

int Decimal::compare(const Decimal& rhs) const {
    if (is_nan() || rhs.is_nan()) throw DecimalInvalidOperation("NaN ordering comparison");
    if (!is_finite() || !rhs.is_finite()) {
        if (!is_finite() && !rhs.is_finite()) return m_negative == rhs.m_negative ? 0 : (m_negative ? -1 : 1);
        return !is_finite() ? (m_negative ? -1 : 1) : (rhs.m_negative ? 1 : -1);
    }
    // ゼロには大きさの指数がない。 小数桁数や符号によって、
    // 正の小数より大きく、または負の小数より小さく並べない。
    if (is_zero()) return rhs.is_zero() ? 0 : (rhs.m_negative ? 1 : -1);
    if (rhs.is_zero()) return m_negative ? -1 : 1;
    if (m_negative != rhs.m_negative) {
        return m_negative ? -1 : 1;
    }
    auto compare_magnitude = [](const Decimal& lhs, const Decimal& rhs) noexcept {
        const DecimalString lhs_digits = lhs.coefficient_string();
        const DecimalString rhs_digits = rhs.coefficient_string();
        auto exponent10 = [](std::size_t digits, DecimalInteger scale) noexcept {
            const auto length = static_cast<std::uint64_t>(digits - 1);
            if (scale >= 0) {
                const auto fractional = static_cast<std::uint64_t>(scale);
                return std::pair<bool, std::uint64_t>{fractional > length,
                    fractional > length ? fractional - length : length - fractional};
            }
            const auto integral = static_cast<std::uint64_t>(-(scale + 1)) + 1;
            return std::pair<bool, std::uint64_t>{false, length + integral};
        };
        const auto lhs_exponent = exponent10(lhs_digits.size(), lhs.m_scale);
        const auto rhs_exponent = exponent10(rhs_digits.size(), rhs.m_scale);
        if (lhs_exponent != rhs_exponent) {
            if (lhs_exponent.first != rhs_exponent.first) return lhs_exponent.first ? -1 : 1;
            const auto comparison = lhs_exponent.second < rhs_exponent.second ? -1 : 1;
            return lhs_exponent.first ? -comparison : comparison;
        }
        const std::size_t length = std::max(lhs_digits.size(), rhs_digits.size());
        for (std::size_t i = 0; i < length; ++i) {
            const char lhs_digit = i < lhs_digits.size() ? lhs_digits[i] : '0';
            const char rhs_digit = i < rhs_digits.size() ? rhs_digits[i] : '0';
            if (lhs_digit != rhs_digit) {
                return lhs_digit < rhs_digit ? -1 : 1;
            }
        }
        return 0;
    };
    const int magnitude = compare_magnitude(*this, rhs);
    return m_negative ? -magnitude : magnitude;
}

} // namespace cw::lib
