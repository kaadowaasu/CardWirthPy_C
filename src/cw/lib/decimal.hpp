#pragma once

// 計算処理の境界で使う正確な十進演算。Python
// の Decimal の無限大、quiet/signaling
// NaN にも対応する。表現は非負の係
// 数 × 10^(-scale) とし、
// Python 互換の符号付きゼロも含め
// 符号を別に保持する。double など
// 二進浮動小数点は使わない。利用可能なら
// 係数に Boost.Multiprecision
// を使い、Boost がない環境
// では十進文字列の整数で代替する。

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#if defined(__has_include)
#  if __has_include(<boost/multiprecision/cpp_int.hpp>)
#    include <boost/multiprecision/cpp_int.hpp>
#    define CW_DECIMAL_HAS_BOOST 1
#  endif
#endif

namespace cw::lib {

using DecimalString = std::string;
using DecimalInteger = std::int64_t;

enum class DecimalRounding {
    HalfEven,
    Down,
    Floor,
    Ceiling,
    HalfUp,
    // 連携コード用に、Python の decimal と同じ綴りの別名を提供する。
    ROUND_HALF_EVEN = HalfEven,
    ROUND_DOWN = Down,
    ROUND_FLOOR = Floor,
    ROUND_CEILING = Ceiling,
    ROUND_HALF_UP = HalfUp,
};

struct DecimalContext {
    // Python の Decimal の既定精度。ゼロは無制限を
    // 表すが、その場合の循環小数の除算は不正確な表現を黙って選ばず
    // DecimalInvalidOperation を投げる。
    std::size_t precision = 28;
    DecimalRounding rounding = DecimalRounding::HalfEven;
    // 指数と係数の安全上の上限。精度設定ではなく、
    // 極端な入力でメモリーを使い尽くすことを防ぐ。
    std::size_t max_digits = 1'000'000;

    static DecimalContext default_context() noexcept;
};

using DecimalRoundingMode = DecimalRounding;
using DecimalContextType = DecimalContext;

class DecimalError : public std::runtime_error {
public:
    enum class Kind { InvalidOperation, DivisionByZero, Overflow };

    DecimalError(Kind kind, const DecimalString& message);
    Kind kind() const noexcept { return m_kind; }

private:
    Kind m_kind;
};

class DecimalInvalidOperation final : public DecimalError {
public:
    explicit DecimalInvalidOperation(const DecimalString& message);
};

class DecimalDivisionByZero final : public DecimalError {
public:
    explicit DecimalDivisionByZero(const DecimalString& message);
};

class DecimalOverflow final : public DecimalError {
public:
    explicit DecimalOverflow(const DecimalString& message);
};

class Decimal {
public:
    Decimal() noexcept;
    explicit Decimal(DecimalInteger value);
    explicit Decimal(const DecimalString& text);

    static Decimal parse(const DecimalString& text,
                         const DecimalContext& context = DecimalContext::default_context());

    // 正確な内部表現の取得関数。coefficient_string()
    // は常に非負の十進整数で、0 以外に先頭のゼロを付けない。
    bool negative() const noexcept { return m_negative; }
    bool is_finite() const noexcept { return m_special == Special::Finite; }
    bool is_nan() const noexcept { return m_special == Special::QuietNaN || m_special == Special::SignalingNaN; }
    bool is_snan() const noexcept { return m_special == Special::SignalingNaN; }
    bool is_zero() const noexcept;
    DecimalInteger scale() const noexcept { return m_scale; }
    DecimalString coefficient_string() const;

    // preserve_scale=true は表現の桁数に基づく末尾のゼロを保持する。normalize
    // () は不要な末尾のゼロを除いた、同じ数値の正規化済みDecimalを返す。
    DecimalString to_string(bool preserve_scale = true) const;
    // Python の float 互換境界への精度を失う変換。演算自体は正確なまま保つ。
    double to_real() const;
    Decimal normalize() const;

    // 整数への切り捨てと、方向を指定した丸め。
    Decimal trunc() const;
    Decimal floor() const;
    Decimal ceil() const;
    Decimal plus(const DecimalContext& context = DecimalContext::default_context()) const;
    Decimal minus(const DecimalContext& context = DecimalContext::default_context()) const;
    DecimalInteger to_integer(DecimalRounding rounding = DecimalRounding::Down) const;

    Decimal add(const Decimal& rhs, const DecimalContext& context = DecimalContext::default_context()) const;
    Decimal subtract(const Decimal& rhs, const DecimalContext& context = DecimalContext::default_context()) const;
    Decimal multiply(const Decimal& rhs, const DecimalContext& context = DecimalContext::default_context()) const;
    Decimal divide(const Decimal& rhs, const DecimalContext& context = DecimalContext::default_context()) const;

    // Python の Decimal の % は、商をゼロ方向に切り捨てて求
    // める。floor_divide() は負数に対して数学的な床関数を使う。
    Decimal remainder(const Decimal& rhs, const DecimalContext& context = DecimalContext::default_context()) const;
    Decimal floor_divide(const Decimal& rhs, const DecimalContext& context = DecimalContext::default_context()) const;

    // rhs の指数・桁数にそろえる。Decimal.quantize(Decimal("1E-2"))
    // と同様、rhs の係数は使わない。
    Decimal quantize(const Decimal& exponent,
                     DecimalRounding rounding = DecimalRounding::HalfEven,
                     std::size_t precision = 0) const;

    int compare(const Decimal& rhs) const;

    friend Decimal operator+(const Decimal& lhs, const Decimal& rhs) { return lhs.add(rhs); }
    friend Decimal operator-(const Decimal& lhs, const Decimal& rhs) { return lhs.subtract(rhs); }
    friend Decimal operator*(const Decimal& lhs, const Decimal& rhs) { return lhs.multiply(rhs); }
    friend Decimal operator/(const Decimal& lhs, const Decimal& rhs) { return lhs.divide(rhs); }
    friend Decimal operator%(const Decimal& lhs, const Decimal& rhs) { return lhs.remainder(rhs); }

    friend bool operator==(const Decimal& lhs, const Decimal& rhs) {
        if (lhs.is_snan() || rhs.is_snan()) throw DecimalInvalidOperation("signaling NaN comparison");
        return !lhs.is_nan() && !rhs.is_nan() && lhs.compare(rhs) == 0;
    }
    friend bool operator!=(const Decimal& lhs, const Decimal& rhs) { return !(lhs == rhs); }
    friend bool operator<(const Decimal& lhs, const Decimal& rhs) { return lhs.compare(rhs) < 0; }
    friend bool operator<=(const Decimal& lhs, const Decimal& rhs) { return lhs.compare(rhs) <= 0; }
    friend bool operator>(const Decimal& lhs, const Decimal& rhs) { return lhs.compare(rhs) > 0; }
    friend bool operator>=(const Decimal& lhs, const Decimal& rhs) { return lhs.compare(rhs) >= 0; }

private:
    enum class Special { Finite, Infinity, QuietNaN, SignalingNaN };
    Special m_special{Special::Finite};
    DecimalString m_nan_payload{};
#if defined(CW_DECIMAL_HAS_BOOST)
    using Coefficient = boost::multiprecision::cpp_int;
#else
    // 符号・先頭ゼロのない十進数字。Boost ヘッダーなし
    // でもビルドできるようにする代替実装で、精度は失わない。
    using Coefficient = DecimalString;
#endif

    bool m_negative;
    DecimalInteger m_scale;
    Coefficient m_coefficient;

    Decimal(bool negative, DecimalInteger scale, const Coefficient& coefficient);
    Decimal apply_context(const DecimalContext& context) const;
    Decimal checked_nan(const Decimal* rhs = nullptr,
                        const DecimalContext& context = DecimalContext::default_context()) const;
};

} // namespace cw::lib
