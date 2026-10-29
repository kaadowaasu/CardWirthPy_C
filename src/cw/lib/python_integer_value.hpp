#pragma once

#include "cw/lib/python_integer.hpp"
#include <cmath>
#include <ostream>
#include <iomanip>
#include <locale>
#include <limits>
#include <sstream>

namespace cw::lib {

// Python の int はネイティブ Integer の 64
// ビットに制限されない。大きな動的整数は文字列ではなく数値 Object
// として保持する。整数だけの Decimal 演算は共通の
// 正確な係数処理を precision=0 で使い、小数の丸
// めを行わない。既存の係数の確保上限で極端な入力を制限する。
class PythonInteger final : public Object {
public:
    PythonInteger() = default;
    PythonInteger(Integer value) : value_(value) {}

    static std::optional<PythonInteger> parse(const String& text) {
        const auto normalized = normalize_python_integer(text);
        if (!normalized) return std::nullopt;
        return PythonInteger(Decimal::parse(*normalized, context()));
    }

    static PythonInteger from_truncated_real(Real value) {
        if (!std::isfinite(value))
            throw std::overflow_error("Cannot convert non-finite float to Python integer");
        const auto native_bound = std::ldexp(Real{1}, std::numeric_limits<Integer>::digits);
        if (value >= -native_bound && value < native_bound)
            return PythonInteger(static_cast<Integer>(value));
        // Python の int(float) は二進浮動小数点値の整数部分全体を保持する。固
        // 定幅へのキャストでは、後の範囲補正や API 検査より先にオーバーフローしてしまう。
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << std::fixed << std::setprecision(0) << std::trunc(value);
        return PythonInteger(Decimal::parse(text.str(), context()));
    }

    bool is_zero() const noexcept { return value_.is_zero(); }
    Integer to_integer() const { return value_.to_integer(); }
    Real to_real() const {
        const auto result = value_.to_real();
        if (!std::isfinite(result))
            throw std::overflow_error("Python integer is too large for float");
        return result;
    }
    String to_string() const {
        if (is_zero()) return "0";
        return (value_.negative() ? "-" : "") + value_.coefficient_string();
    }

    PythonInteger floor_divide(const PythonInteger& rhs) const {
        return PythonInteger(value_.floor_divide(rhs.value_, context()));
    }

    friend PythonInteger operator+(const PythonInteger& lhs, const PythonInteger& rhs) {
        return PythonInteger(lhs.value_.add(rhs.value_, context()));
    }
    friend PythonInteger operator-(const PythonInteger& lhs, const PythonInteger& rhs) {
        return PythonInteger(lhs.value_.subtract(rhs.value_, context()));
    }
    friend PythonInteger operator*(const PythonInteger& lhs, const PythonInteger& rhs) {
        return PythonInteger(lhs.value_.multiply(rhs.value_, context()));
    }
    friend bool operator==(const PythonInteger& lhs, const PythonInteger& rhs) {
        return lhs.value_.compare(rhs.value_) == 0;
    }
    friend bool operator<(const PythonInteger& lhs, const PythonInteger& rhs) {
        return lhs.value_.compare(rhs.value_) < 0;
    }
    friend bool operator>(const PythonInteger& lhs, const PythonInteger& rhs) { return rhs < lhs; }
    friend bool operator<=(const PythonInteger& lhs, const PythonInteger& rhs) { return !(rhs < lhs); }
    friend bool operator>=(const PythonInteger& lhs, const PythonInteger& rhs) { return !(lhs < rhs); }
    friend std::ostream& operator<<(std::ostream& output, const PythonInteger& value) {
        return output << value.to_string();
    }

private:
    explicit PythonInteger(Decimal value) : value_(std::move(value)) {}
    static DecimalContext context() {
        auto result = DecimalContext::default_context();
        result.precision = 0;
        return result;
    }
    Decimal value_;
};

inline std::shared_ptr<PythonInteger> python_integer_object(const Value& value) {
    const auto* object = std::get_if<ObjectPtr>(&value.storage());
    return object ? std::dynamic_pointer_cast<PythonInteger>(*object) : nullptr;
}

inline std::optional<Value> parse_python_integer_value(const String& text) {
    if (const auto small = parse_python_integer(text)) return Value(*small);
    if (auto large = PythonInteger::parse(text))
        return Value(std::static_pointer_cast<Object>(
            std::make_shared<PythonInteger>(std::move(*large))));
    return std::nullopt;
}

} // namespace cw::lib
