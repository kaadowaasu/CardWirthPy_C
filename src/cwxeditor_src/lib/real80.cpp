#include "cwxeditor_src/lib/real80.hpp"

#include <platform.h>
extern "C" {
#include <internals.h>
#include <softfloat.h>
}

#include <cmath>
#include <cstring>
#include <limits>

namespace cwxeditor::lib {
namespace {

// SoftFloat の状態は、組み込み先のビルドでスレッドごとに保持
// する。 各ラッパー操作は一時的にD版の標準の real の方針を選び
// 、 他の呼び出し元へ戻る前に、元の制御・例外の全ビットを復元する。
class DRealPolicyScope {
public:
    DRealPolicyScope() noexcept
        : oldRounding_(softfloat_roundingMode),
          oldTininess_(softfloat_detectTininess),
          oldFlags_(softfloat_exceptionFlags),
          oldPrecision_(extF80_roundingPrecision) {
        softfloat_roundingMode = softfloat_round_near_even;
        softfloat_detectTininess = softfloat_tininess_afterRounding;
        softfloat_exceptionFlags = 0;
        extF80_roundingPrecision = 80;
    }

    DRealPolicyScope(const DRealPolicyScope&) = delete;
    DRealPolicyScope& operator=(const DRealPolicyScope&) = delete;

    ~DRealPolicyScope() {
        softfloat_roundingMode = oldRounding_;
        softfloat_detectTininess = oldTininess_;
        softfloat_exceptionFlags = oldFlags_;
        extF80_roundingPrecision = oldPrecision_;
    }

private:
    uint_fast8_t oldRounding_;
    uint_fast8_t oldTininess_;
    uint_fast8_t oldFlags_;
    uint_fast8_t oldPrecision_;
};

extFloat80_t toSoft(Real80 value) noexcept {
    const auto bits = value.bits();
    extFloat80_t result{};
    result.signExp = bits.signExp;
    result.signif = bits.significand;
    return result;
}

Real80 fromSoft(extFloat80_t value) noexcept {
    return Real80::fromBits(value.signExp, value.signif);
}

Real80 roundToDReal80(bool negative,
                      std::int64_t exponent,
                      std::uint64_t significand,
                      std::uint64_t extra) noexcept {
    if (significand == 0 && extra == 0) return Real80::zero(negative);

    // 指数の範囲検査より先に正規化する。 元が非正規化数で
    // も、大きな正の倍率で通常の有限値になる場合がある。
    if (significand == 0) {
        exponent -= 64;
        significand = extra;
        extra = 0;
    }
    int shift = 0;
    while ((significand & UINT64_C(0x8000000000000000)) == 0) {
        significand = (significand << 1) | (extra >> 63);
        extra <<= 1;
        ++shift;
    }
    exponent -= shift;

    if (exponent > INT64_C(0x7ffe)) return Real80::infinity(negative);
    // 正規化した仮数部の指数が-64なら、非正規化数の
    // 最小単位の半分未満となる。 -63は上位の仮数ビ
    // ットによって中点を超える場合があるため保持する。
    if (exponent < -63) return Real80::zero(negative);

    DRealPolicyScope policy;
    return fromSoft(softfloat_roundPackToExtF80(
        negative,
        static_cast<int_fast32_t>(exponent),
        significand,
        extra,
        80));
}

float32_t toSoft(float value) noexcept {
    float32_t result{};
    static_assert(sizeof(value) == sizeof(result.v));
    std::memcpy(&result.v, &value, sizeof(value));
    return result;
}

float64_t toSoft(double value) noexcept {
    float64_t result{};
    static_assert(sizeof(value) == sizeof(result.v));
    std::memcpy(&result.v, &value, sizeof(value));
    return result;
}

float fromSoft(float32_t value) noexcept {
    float result{};
    std::memcpy(&result, &value.v, sizeof(result));
    return result;
}

double fromSoft(float64_t value) noexcept {
    double result{};
    std::memcpy(&result, &value.v, sizeof(result));
    return result;
}

} // namespace

Real80::Real80(float value) noexcept {
    DRealPolicyScope policy;
    bits_ = fromSoft(f32_to_extF80(toSoft(value))).bits();
}

Real80::Real80(double value) noexcept {
    DRealPolicyScope policy;
    bits_ = fromSoft(f64_to_extF80(toSoft(value))).bits();
}

Real80::Real80(long double value) noexcept {
    const bool negative = std::signbit(value);
    if (std::isnan(value)) {
        bits_ = nan().bits();
        if (negative) bits_.signExp |= UINT16_C(0x8000);
        return;
    }
    if (std::isinf(value)) {
        bits_ = infinity(negative).bits();
        return;
    }
    if (value == 0.0L) {
        bits_ = zero(negative).bits();
        return;
    }

    // ホストの値を、先頭64ビットの仮数部と64ビットのガード・スティ
    // ッキービット用の末尾に分ける。 binary64とx87のbinary80
    // では正確で、AArch64のbinary128などの広い形
    // 式からも、 一度の最近接偶数丸めに十分な情報を保持する。
    int exponent = 0;
    const long double fraction = std::frexp(std::fabs(value), &exponent);
    const long double scaled = std::ldexp(fraction, 64);
    const auto significand = static_cast<std::uint64_t>(scaled);
    const long double fractionalTail = scaled - static_cast<long double>(significand);
    const long double scaledExtra = std::ldexp(fractionalTail, 64);
    auto extra = static_cast<std::uint64_t>(scaledExtra);
    if (scaledExtra != static_cast<long double>(extra)) extra |= 1;
    const auto encodedExponent = static_cast<std::int64_t>(exponent) + 16382;
    bits_ = roundToDReal80(negative, encodedExponent, significand, extra).bits();
}

Real80 Real80::fromBits(std::uint16_t signExp, std::uint64_t significand) noexcept {
    return Real80(Bits{signExp, significand});
}

Real80 Real80::fromSigned(std::int64_t value) noexcept {
    DRealPolicyScope policy;
    return fromSoft(i64_to_extF80(value));
}

Real80 Real80::fromUnsigned(std::uint64_t value) noexcept {
    DRealPolicyScope policy;
    return fromSoft(ui64_to_extF80(value));
}

Real80 Real80::zero(bool negative) noexcept {
    return fromBits(negative ? UINT16_C(0x8000) : 0, 0);
}

Real80 Real80::infinity(bool negative) noexcept {
    return fromBits(static_cast<std::uint16_t>((negative ? UINT16_C(0x8000) : 0) |
                                               UINT16_C(0x7fff)),
                    UINT64_C(0x8000000000000000));
}

Real80 Real80::nan() noexcept {
    return fromBits(UINT16_C(0x7fff), UINT64_C(0xc000000000000000));
}

bool Real80::isZero() const noexcept {
    return (bits_.signExp & UINT16_C(0x7fff)) == 0 && bits_.significand == 0;
}

bool Real80::isNegative() const noexcept {
    return (bits_.signExp & UINT16_C(0x8000)) != 0;
}

bool Real80::isInfinite() const noexcept {
    return (bits_.signExp & UINT16_C(0x7fff)) == UINT16_C(0x7fff) &&
           bits_.significand == UINT64_C(0x8000000000000000);
}

bool Real80::isNaN() const noexcept {
    return (bits_.signExp & UINT16_C(0x7fff)) == UINT16_C(0x7fff) &&
           bits_.significand != UINT64_C(0x8000000000000000);
}

float Real80::toFloat() const noexcept {
    DRealPolicyScope policy;
    return fromSoft(extF80_to_f32(toSoft(*this)));
}

double Real80::toDouble() const noexcept {
    DRealPolicyScope policy;
    return fromSoft(extF80_to_f64(toSoft(*this)));
}

long double Real80::toLongDouble() const noexcept {
    if (isNaN()) return std::copysign(std::numeric_limits<long double>::quiet_NaN(),
                                      isNegative() ? -1.0L : 1.0L);
    if (isInfinite()) return isNegative() ? -std::numeric_limits<long double>::infinity()
                                          : std::numeric_limits<long double>::infinity();
    if (isZero()) return std::copysign(0.0L, isNegative() ? -1.0L : 1.0L);

    if constexpr (std::numeric_limits<long double>::digits >= 64) {
        const int encodedExponent = bits_.signExp & UINT16_C(0x7fff);
        const int binaryExponent = (encodedExponent == 0 ? 1 : encodedExponent) - 16383 - 63;
        const long double magnitude = std::ldexp(static_cast<long double>(bits_.significand),
                                                 binaryExponent);
        return isNegative() ? -magnitude : magnitude;
    } else {
        return static_cast<long double>(toDouble());
    }
}

Real80 Real80::scaledByPowerOfTwo(int exponent) const noexcept {
    if (isZero() || isInfinite() || isNaN() || exponent == 0) return *this;

    const bool negative = isNegative();
    const auto significand = bits_.significand;
    const auto encodedExponent = static_cast<std::int64_t>(bits_.signExp & UINT16_C(0x7fff));
    // Ext80の非正規化数は指数フィールドがゼロだが、指数1と同じ倍率を
    // 使う。 SoftFloatの正規化処理が、先頭ゼロの数を考慮する。
    const std::int64_t adjustedExponent = (encodedExponent == 0 ? 1 : encodedExponent) +
                                          static_cast<std::int64_t>(exponent);
    return roundToDReal80(negative, adjustedExponent, significand, 0);
}

Real80 Real80::fmod(const Real80& divisor) const noexcept {
    DRealPolicyScope policy;
    Real80 remainder = fromSoft(extF80_rem(toSoft(*this), toSoft(divisor)));
    if (!remainder.isNaN() && !remainder.isZero() &&
        remainder.isNegative() != isNegative()) {
        const Real80 magnitude = divisor.isNegative() ? -divisor : divisor;
        remainder = remainder + (isNegative() ? -magnitude : magnitude);
    }
    return remainder;
}

Real80 Real80::operator-() const noexcept {
    Real80 result = *this;
    result.bits_.signExp ^= UINT16_C(0x8000);
    return result;
}

Real80 operator+(const Real80& lhs, const Real80& rhs) noexcept {
    DRealPolicyScope policy;
    return fromSoft(extF80_add(toSoft(lhs), toSoft(rhs)));
}

Real80 operator-(const Real80& lhs, const Real80& rhs) noexcept {
    DRealPolicyScope policy;
    return fromSoft(extF80_sub(toSoft(lhs), toSoft(rhs)));
}

Real80 operator*(const Real80& lhs, const Real80& rhs) noexcept {
    DRealPolicyScope policy;
    return fromSoft(extF80_mul(toSoft(lhs), toSoft(rhs)));
}

Real80 operator/(const Real80& lhs, const Real80& rhs) noexcept {
    DRealPolicyScope policy;
    return fromSoft(extF80_div(toSoft(lhs), toSoft(rhs)));
}

bool operator==(const Real80& lhs, const Real80& rhs) noexcept {
    DRealPolicyScope policy;
    return extF80_eq(toSoft(lhs), toSoft(rhs));
}

bool operator!=(const Real80& lhs, const Real80& rhs) noexcept {
    return !(lhs == rhs);
}

bool operator<(const Real80& lhs, const Real80& rhs) noexcept {
    DRealPolicyScope policy;
    return extF80_lt(toSoft(lhs), toSoft(rhs));
}

bool operator<=(const Real80& lhs, const Real80& rhs) noexcept {
    DRealPolicyScope policy;
    return extF80_le(toSoft(lhs), toSoft(rhs));
}

bool operator>(const Real80& lhs, const Real80& rhs) noexcept {
    return rhs < lhs;
}

bool operator>=(const Real80& lhs, const Real80& rhs) noexcept {
    return rhs <= lhs;
}

} // namespace cwxeditor::lib
