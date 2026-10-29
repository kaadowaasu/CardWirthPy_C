#include "cwxeditor_src/lib/real80.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cwxeditor::lib {
namespace {

// ホストの浮動小数点型を通さず、正確な十進整数を組み立てる。 最大の乗
// 数でも、桁ブロック×乗数＋桁上がりが uint64_t 内に収まる。
class DecimalInteger {
public:
    explicit DecimalInteger(std::uint64_t value) {
        do {
            limbs_.push_back(static_cast<std::uint32_t>(value % base));
            value /= base;
        } while (value != 0);
    }

    void multiply(std::uint64_t factor) {
        std::uint64_t carry = 0;
        for (auto& limb : limbs_) {
            const std::uint64_t product =
                static_cast<std::uint64_t>(limb) * factor + carry;
            limb = static_cast<std::uint32_t>(product % base);
            carry = product / base;
        }
        while (carry != 0) {
            limbs_.push_back(static_cast<std::uint32_t>(carry % base));
            carry /= base;
        }
    }

    void multiplyPowerOfTwo(unsigned exponent) {
        constexpr std::uint64_t factor = std::uint64_t{1} << 29;
        while (exponent >= 29) {
            multiply(factor);
            exponent -= 29;
        }
        if (exponent != 0) multiply(std::uint64_t{1} << exponent);
    }

    void multiplyPowerOfFive(unsigned exponent) {
        constexpr std::uint64_t factor = 1220703125; // 5^13
        while (exponent >= 13) {
            multiply(factor);
            exponent -= 13;
        }
        std::uint64_t remainderFactor = 1;
        while (exponent-- != 0) remainderFactor *= 5;
        if (remainderFactor != 1) multiply(remainderFactor);
    }

    std::string digits() const {
        std::string result = std::to_string(limbs_.back());
        for (std::size_t i = limbs_.size() - 1; i != 0; --i) {
            auto group = std::to_string(limbs_[i - 1]);
            result.append(9 - group.size(), '0');
            result += group;
        }
        return result;
    }

private:
    static constexpr std::uint64_t base = 1000000000;
    std::vector<std::uint32_t> limbs_;
};

bool incrementDecimal(std::string& digits) {
    for (std::size_t i = digits.size(); i != 0; --i) {
        if (digits[i - 1] != '9') {
            ++digits[i - 1];
            return false;
        }
        digits[i - 1] = '0';
    }
    digits.front() = '1';
    digits.back() = '0';
    return true;
}

void incrementWholeNumber(std::string& digits) {
    for (std::size_t i = digits.size(); i != 0; --i) {
        if (digits[i - 1] != '9') {
            ++digits[i - 1];
            return;
        }
        digits[i - 1] = '0';
    }
    digits.insert(digits.begin(), '1');
}

std::string roundToNearestInteger(const std::string& digits, int decimalScale) {
    const auto point = digits.size() - static_cast<std::size_t>(decimalScale);
    std::string integer = digits.substr(0, point);
    if (point == digits.size()) return integer;

    bool roundUp = digits[point] > '5';
    if (digits[point] == '5') {
        bool hasLaterNonzero = false;
        for (std::size_t i = point + 1; i < digits.size(); ++i)
            hasLaterNonzero = hasLaterNonzero || digits[i] != '0';
        roundUp = hasLaterNonzero || ((integer.back() - '0') & 1) != 0;
    }
    if (roundUp) incrementWholeNumber(integer);
    return integer;
}

std::string trimFractionZeros(std::string value) {
    const auto point = value.find('.');
    if (point == std::string::npos) return value;
    while (!value.empty() && value.back() == '0') value.pop_back();
    if (!value.empty() && value.back() == '.') value.pop_back();
    return value;
}

std::string formatExponent(int exponent) {
    const bool negative = exponent < 0;
    auto magnitude = std::to_string(negative ? -exponent : exponent);
    if (magnitude.size() < 2) magnitude.insert(0, 1, '0');
    return std::string(negative ? "e-" : "e+") + magnitude;
}

int compareMagnitudeToNormal(Real80::Bits value, std::uint16_t thresholdExponent,
                             std::uint64_t thresholdSignificand) {
    constexpr std::uint64_t fractionMask = 0x7fffffffffffffffULL;
    constexpr int exponentBias = 16383;

    const auto exponentField = static_cast<std::uint16_t>(value.signExp & 0x7fff);
    const auto fraction = value.significand & fractionMask;
    int valueExponent;
    std::uint64_t valueSignificand;
    if (exponentField == 0) {
        if (fraction == 0) return -1;
        unsigned topBit = 0;
        auto remaining = fraction;
        while ((remaining >>= 1) != 0) ++topBit;
        valueExponent = static_cast<int>(topBit) - 16445;
        valueSignificand = fraction << (63 - topBit);
    } else {
        valueExponent = static_cast<int>(exponentField) - exponentBias;
        valueSignificand = fraction | (std::uint64_t{1} << 63);
    }

    const int thresholdBinaryExponent = static_cast<int>(thresholdExponent) - exponentBias;
    if (valueExponent != thresholdBinaryExponent)
        return valueExponent < thresholdBinaryExponent ? -1 : 1;
    if (valueSignificand == thresholdSignificand) return 0;
    return valueSignificand < thresholdSignificand ? -1 : 1;
}

std::string formatDReal80(Real80::Bits bits) {
    constexpr std::uint64_t fractionMask = 0x7fffffffffffffffULL;
    constexpr std::uint16_t exponentMask = 0x7fff;
    constexpr int exponentBias = 16383;
    constexpr int precision = 6;
    // DMD 2.097の標準の %g の表記切り替え境界を、x87
    // の値として正確に表す。 下側の値は倍精度の境界式から来てお
    // り、その境界を合わせることでD版の同値時の動作を維持する。
    constexpr std::uint16_t lowerFormatExponent = 0x3ff1;
    constexpr std::uint64_t lowerFormatSignificand = 0xd1b71079aa22ef3dULL;
    constexpr std::uint16_t upperFormatExponent = 0x4012;
    constexpr std::uint64_t upperFormatSignificand = 0xf423f80000000000ULL;

    const bool negative = (bits.signExp & 0x8000) != 0;
    const auto sign = negative ? "-" : "";
    const auto exponentField = static_cast<std::uint16_t>(bits.signExp & exponentMask);
    const auto fraction = bits.significand & fractionMask;

    // D 2.097のビット抽出と同様、x87の明示的な整数ビッ
    // トは無視する。 非正規化数は小数部だけを使い、有限の通常指
    // 数には、非標準のx87符号化でも暗黙の上位ビットを加える。
    if (exponentField == exponentMask) {
        return std::string(sign) + (fraction == 0 ? "inf" : "nan");
    }
    if (exponentField == 0 && fraction == 0) {
        return sign + std::string("0");
    }

    const bool unnormal = exponentField != 0 &&
        (bits.significand & (std::uint64_t{1} << 63)) == 0;
    // Phobosの表記判定は元の real を比較するため、 未対応のx87
    // の非正規形式は順序比較不能となり、固定小数点形式の経路に入る。
    const bool useScientific = !unnormal &&
        (compareMagnitudeToNormal(bits, upperFormatExponent, upperFormatSignificand) >= 0 ||
         compareMagnitudeToNormal(bits, lowerFormatExponent, lowerFormatSignificand) <= 0);

    std::uint64_t significand = fraction;
    int binaryExponent;
    if (exponentField == 0) {
        binaryExponent = 1 - exponentBias - 63;
    } else {
        significand |= std::uint64_t{1} << 63;
        binaryExponent = static_cast<int>(exponentField) - exponentBias - 63;
    }

    DecimalInteger exact(significand);
    int decimalScale = 0;
    if (binaryExponent >= 0) {
        exact.multiplyPowerOfTwo(static_cast<unsigned>(binaryExponent));
    } else {
        decimalScale = -binaryExponent;
        exact.multiplyPowerOfFive(static_cast<unsigned>(decimalScale));
    }

    const auto exactDigits = exact.digits();
    const int sourceBinaryExponent = static_cast<int>(exponentField) - exponentBias;
    if (unnormal && sourceBinaryExponent >= 64) {
        // D版と同様、抽出した二進指数が64に達する非正規形式
        // では、 固定形式の整数経路で整数の全桁を保持する。
        return std::string(sign) + exactDigits;
    }
    if (unnormal && sourceBinaryExponent >= 20) {
        // その経路より下では、Phobosの固定形式処理と同様、
        // 大きな非正規形式の値を有効六桁にせず、整数に丸める。
        return std::string(sign) + roundToNearestInteger(exactDigits, decimalScale);
    }
    int decimalExponent = static_cast<int>(exactDigits.size()) - 1 - decimalScale;
    std::string rounded = exactDigits.substr(0, precision);
    if (rounded.size() < precision) rounded.append(precision - rounded.size(), '0');

    if (exactDigits.size() > precision) {
        const auto firstDiscarded = exactDigits[precision];
        bool roundUp = firstDiscarded > '5';
        if (firstDiscarded == '5') {
            bool hasLaterNonzero = false;
            for (std::size_t i = precision + 1; i < exactDigits.size(); ++i) {
                hasLaterNonzero = hasLaterNonzero || exactDigits[i] != '0';
            }
            const bool retainedIsOdd = ((rounded.back() - '0') & 1) != 0;
            roundUp = hasLaterNonzero || retainedIsOdd;
        }
        if (roundUp) {
            if (incrementDecimal(rounded)) ++decimalExponent;
        }
    }

    std::string result;
    if (!useScientific) {
        if (decimalExponent < 0) {
            result = "0.";
            result.append(static_cast<std::size_t>(-decimalExponent - 1), '0');
            result += rounded;
        } else {
            const auto decimalPoint = static_cast<std::size_t>(decimalExponent + 1);
            if (decimalPoint >= rounded.size()) {
                result = rounded;
                result.append(decimalPoint - rounded.size(), '0');
            } else {
                result = rounded.substr(0, decimalPoint) + "." + rounded.substr(decimalPoint);
            }
        }
        result = trimFractionZeros(std::move(result));
    } else {
        result.assign(1, rounded.front());
        auto fractionDigits = rounded.substr(1);
        while (!fractionDigits.empty() && fractionDigits.back() == '0') fractionDigits.pop_back();
        if (!fractionDigits.empty()) result += "." + fractionDigits;
        result += formatExponent(decimalExponent);
    }

    return std::string(sign) + result;
}

} // namespace

std::string Real80::toString() const {
    return formatDReal80(bits());
}

} // namespace cwxeditor::lib
