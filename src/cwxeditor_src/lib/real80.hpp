#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace cwxeditor::lib {

// Dの80ビットreal演算を扱う、プラットフォームに
// 依存しない値型。内部表現は公開APIに出さず、正規化
// した符号・指数と明示的な仮数のワードだけを受け渡す。
class Real80 {
public:
    struct Bits {
        std::uint16_t signExp = 0;
        std::uint64_t significand = 0;
    };

    constexpr Real80() noexcept = default;
    Real80(float value) noexcept;
    Real80(double value) noexcept;
    Real80(long double value) noexcept;

    template <typename Integer, std::enable_if_t<std::is_integral_v<Integer>, int> = 0>
    Real80(Integer value) noexcept : bits_{} {
        if constexpr (std::is_signed_v<Integer>) {
            *this = fromSigned(static_cast<std::int64_t>(value));
        } else {
            *this = fromUnsigned(static_cast<std::uint64_t>(value));
        }
    }

    static Real80 fromBits(std::uint16_t signExp, std::uint64_t significand) noexcept;
    static Real80 fromSigned(std::int64_t value) noexcept;
    static Real80 fromUnsigned(std::uint64_t value) noexcept;
    static Real80 zero(bool negative = false) noexcept;
    static Real80 infinity(bool negative = false) noexcept;
    static Real80 nan() noexcept;
    static Real80 parse(std::string_view value);

    Bits bits() const noexcept { return bits_; }

    bool isZero() const noexcept;
    bool isNegative() const noexcept;
    bool isInfinite() const noexcept;
    bool isNaN() const noexcept;
    bool isFinite() const noexcept { return !isInfinite() && !isNaN(); }

    float toFloat() const noexcept;
    double toDouble() const noexcept;
    long double toLongDouble() const noexcept;
    std::string toString() const;

    Real80 scaledByPowerOfTwo(int exponent) const noexcept;
    Real80 fmod(const Real80& divisor) const noexcept;

    Real80 operator+() const noexcept { return *this; }
    Real80 operator-() const noexcept;

    friend Real80 operator+(const Real80& lhs, const Real80& rhs) noexcept;
    friend Real80 operator-(const Real80& lhs, const Real80& rhs) noexcept;
    friend Real80 operator*(const Real80& lhs, const Real80& rhs) noexcept;
    friend Real80 operator/(const Real80& lhs, const Real80& rhs) noexcept;

    friend bool operator==(const Real80& lhs, const Real80& rhs) noexcept;
    friend bool operator!=(const Real80& lhs, const Real80& rhs) noexcept;
    friend bool operator<(const Real80& lhs, const Real80& rhs) noexcept;
    friend bool operator<=(const Real80& lhs, const Real80& rhs) noexcept;
    friend bool operator>(const Real80& lhs, const Real80& rhs) noexcept;
    friend bool operator>=(const Real80& lhs, const Real80& rhs) noexcept;

private:
    explicit constexpr Real80(Bits bits) noexcept : bits_(bits) {}

    Bits bits_{};
};

// スカラー値の処理経路を選ぶ共通トレイト。Real80を組み
// 込み算術型と見なしたり標準ライブラリのトレイトを特殊化した
// りせず、ネイティブ浮動小数点型と同じ文字列変換経路へ渡す。
template <typename T>
inline constexpr bool is_real_number_v =
    std::is_floating_point_v<std::remove_cvref_t<T>> ||
    std::is_same_v<std::remove_cvref_t<T>, Real80>;

} // namespace cwxeditor::lib
