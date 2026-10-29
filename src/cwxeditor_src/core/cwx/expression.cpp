#include "cwxeditor_src/core/cwx/expression.hpp"

#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/lib/scalar_format.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <locale>
#include <random>
#include <set>
#include <sstream>
#include <span>
#include <stack>
#include <type_traits>
#include <utility>

namespace cwxeditor::cwx::expression {
namespace {

using types::VariantType;

[[nodiscard]] const Part& requiredPart(const ConstPartPtr& part) {
    // Dは使用時点で要素を参照する。勝手にUnknown値を作った
    // り未定義動作を起こしたりせず、ネイティブの失敗を報告する。
    if (!part) throw std::invalid_argument("null expression part");
    return *part;
}

[[nodiscard]] String lower(StringView value) {
    return enumutils::lowerCase(value);
}

[[nodiscard]] String upper(StringView value) {
    return enumutils::upperCase(value);
}

[[nodiscard]] bool requiresWsn5(StringView name) {
    static constexpr std::array<StringView, 24> names{
        "latestusecardowner", "list", "list_e", "at", "partymoney", "partynumber",
        "yadoname", "skintype", "battleround", "castlevel", "couponvalue", "liferatio",
        "statusvalue", "statusround", "selectedcard", "cardname", "cardtype", "cardrarity",
        "cardprice", "cardlevel", "cardcount", "findkeycode", "keycodetext", "cardinfo"};
    return std::find(names.begin(), names.end(), lower(name)) != names.end();
}

[[nodiscard]] String quote(StringView value) {
    String result{"\""};
    for (const char c : value) {
        if (c == '"') result += "\"\"";
        else result.push_back(c);
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] std::vector<char32_t> decodeUtf8(StringView value) {
    std::vector<char32_t> result;
    for (std::size_t i = 0; i < value.size();) {
        const auto c = static_cast<unsigned char>(value[i]);
        std::size_t width{};
        char32_t codepoint{};
        char32_t minimum{};
        if (c < 0x80U) {
            result.push_back(c);
            ++i;
        } else {
            if (c >= 0xc2U && c <= 0xdfU) {
                width = 2;
                codepoint = c & 0x1fU;
                minimum = 0x80U;
            } else if (c >= 0xe0U && c <= 0xefU) {
                width = 3;
                codepoint = c & 0x0fU;
                minimum = 0x800U;
            } else if (c >= 0xf0U && c <= 0xf4U) {
                width = 4;
                codepoint = c & 0x07U;
                minimum = 0x10000U;
            } else {
                throw std::invalid_argument("invalid UTF-8 leading byte");
            }
            if (i + width > value.size()) {
                throw std::invalid_argument("truncated UTF-8 sequence");
            }
            for (std::size_t n = 1; n < width; ++n) {
                const auto continuation = static_cast<unsigned char>(value[i + n]);
                if ((continuation & 0xc0U) != 0x80U) {
                    throw std::invalid_argument("invalid UTF-8 continuation byte");
                }
                codepoint = (codepoint << 6) | (continuation & 0x3fU);
            }
            if (codepoint < minimum || codepoint > 0x10ffffU ||
                (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
                throw std::invalid_argument("invalid UTF-8 code point");
            }
            result.push_back(codepoint);
            i += width;
        }
    }
    return result;
}

[[nodiscard]] String encodeUtf8(const std::vector<char32_t>& value) {
    String result;
    for (const char32_t c : value) {
        if (c > 0x10ffffU || (c >= 0xd800U && c <= 0xdfffU)) {
            throw std::invalid_argument("invalid Unicode code point");
        }
        if (c <= 0x7f) {
            result.push_back(static_cast<char>(c));
        } else if (c <= 0x7ff) {
            result.push_back(static_cast<char>(0xc0 | (c >> 6)));
            result.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else if (c <= 0xffff) {
            result.push_back(static_cast<char>(0xe0 | (c >> 12)));
            result.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else {
            result.push_back(static_cast<char>(0xf0 | (c >> 18)));
            result.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        }
    }
    return result;
}

[[nodiscard]] String numberText(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    // expression.dはコモンの数値を小数点以下8桁で整形してから、
    // 不要なゼロを除く。往復変換重視のmax_digits10を使うと、STR()
    // 、XML、インライン変数編集に47.420000000000002
    // のような二進表現の誤差が現れ、Python/Dの動作と異なる。
    stream.setf(std::ios::fixed);
    stream << std::setprecision(8) << value;
    String result = stream.str();
    while (result.size() > 1 && result.back() == '0') result.pop_back();
    if (!result.empty() && result.back() == '.') result.pop_back();
    return result;
}

[[nodiscard]] bool parseNumber(StringView value, double& result) {
    // DのparseFunctionNumberは十進数・指数の文字列を受け付け
    // 、strtodの十六進拡張は受け付けない。検証は共通の変換処理に集約する。
    std::size_t index = 0;
    const auto spaces = [&] { while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index]))) ++index; };
    const auto digits = [&] { const auto first = index; while (index < value.size() && std::isdigit(static_cast<unsigned char>(value[index]))) ++index; return index != first; };
    spaces();
    if (index < value.size() && (value[index] == '+' || value[index] == '-')) ++index;
    const bool integer = digits();
    bool fraction = false;
    if (index < value.size() && value[index] == '.') { ++index; fraction = digits(); }
    if (!integer && !fraction) return false;
    if (index < value.size() && (value[index] == 'e' || value[index] == 'E')) {
        ++index;
        if (index < value.size() && (value[index] == '+' || value[index] == '-')) ++index;
        if (!digits()) return false;
    }
    spaces();
    if (index != value.size()) return false;
    String copy(value);
    const char* begin = copy.c_str();
    char* end = nullptr;
    result = std::strtod(begin, &end);
    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (end == begin || *end != '\0' || !std::isfinite(result)) return false;
    return true;
}

[[nodiscard]] bool parseValueNumber(StringView value, double& result) {
    std::size_t index = 0;
    while (index < value.size() &&
           std::isspace(static_cast<unsigned char>(value[index]))) ++index;
    if (index < value.size() && value[index] == '-') ++index;
    const auto integerBegin = index;
    while (index < value.size() &&
           std::isdigit(static_cast<unsigned char>(value[index]))) ++index;
    const bool hasInteger = index != integerBegin;
    bool hasFraction = false;
    if (index < value.size() && value[index] == '.') {
        ++index;
        const auto fractionBegin = index;
        while (index < value.size() &&
               std::isdigit(static_cast<unsigned char>(value[index]))) ++index;
        hasFraction = index != fractionBegin;
    }
    if (!hasInteger && !hasFraction) return false;
    while (index < value.size() &&
           std::isspace(static_cast<unsigned char>(value[index]))) ++index;
    return index == value.size() && parseNumber(value, result);
}

[[nodiscard]] std::size_t utf8Length(StringView value) {
    return decodeUtf8(value).size();
}

[[nodiscard]] String utf8Left(StringView value, std::size_t count) {
    const auto points = enumutils::toUtf32Replacing(value);
    return encodeUtf8(std::vector<char32_t>(points.begin(), points.begin() + std::min(count, points.size())));
}

[[nodiscard]] String utf8Right(StringView value, std::size_t count) {
    const auto points = enumutils::toUtf32Replacing(value);
    return encodeUtf8(std::vector<char32_t>(points.end() - std::min(count, points.size()), points.end()));
}

[[nodiscard]] String utf8Mid(std::u32string_view points, std::size_t first,
                              std::size_t count = std::numeric_limits<std::size_t>::max()) {
    if (first >= points.size()) return {};
    const auto last = first + std::min(count, points.size() - first);
    return encodeUtf8(std::vector<char32_t>(points.begin() + first,
                                             points.begin() + last));
}

[[nodiscard]] std::size_t findUtf8(StringView needle, StringView haystack,
                                   std::size_t startOne) {
    const auto needlePoints = decodeUtf8(needle);
    const auto haystackPoints = decodeUtf8(haystack);
    if (startOne == 0 || startOne > haystackPoints.size()) return 0;
    if (needlePoints.empty()) return haystackPoints.empty() ? 0 : startOne;
    if (needlePoints.size() > haystackPoints.size() - (startOne - 1)) return 0;
    for (std::size_t i = startOne - 1;
         i + needlePoints.size() <= haystackPoints.size(); ++i) {
        if (std::equal(needlePoints.begin(), needlePoints.end(),
                       haystackPoints.begin() + i)) return i + 1;
    }
    return 0;
}

[[nodiscard]] PartPtr number(Token token, double value) {
    return std::make_shared<NumberValue>(std::move(token), value);
}
[[nodiscard]] PartPtr string(Token token, String value) {
    return std::make_shared<StringValue>(std::move(token), std::move(value));
}
[[nodiscard]] PartPtr boolean(Token token, bool value) {
    return std::make_shared<BooleanValue>(std::move(token), value);
}
[[nodiscard]] PartPtr list(Token token, PartList values) {
    return std::make_shared<ListValue>(std::move(token), std::move(values));
}
[[nodiscard]] PartPtr listConst(Token token, ConstPartList values) {
    return std::make_shared<ListValue>(std::move(token), std::move(values));
}
[[nodiscard]] PartPtr unknown(Token token) {
    return std::make_shared<UnknownValue>(std::move(token));
}

[[nodiscard]] String errorText(StringView message, StringView name = {}) {
    if (name.empty()) return String(message);
    String result(message);
    result += name;
    return result;
}

void addError(ExprErrors& errors, String message, const Part& part,
              const CProps* = nullptr) {
    errors.emplace_back(std::move(message), part.token.line, part.token.pos,
                        __FILE__, __LINE__);
}

template <typename... Args>
[[nodiscard]] String expressionMessage(const CProps* prop,
    const msgs::Message msgs::Msgs::* field, StringView fallback, const Args&... args) {
    return utils::tryFormat(prop ? (prop->requiredMsgs().*field).value : String(fallback), args...);
}

[[nodiscard]] bool isUnknown(const ConstPartPtr& value) {
    return std::dynamic_pointer_cast<const UnknownValue>(value) != nullptr;
}

[[nodiscard]] ConstPartPtr evaluate(const CProps* prop, EvalMode mode,
                                    const VariableInfo& info, ConstPartPtr value,
                                    ExprErrors& errors);

[[nodiscard]] bool partEqual(const CProps* prop, EvalMode mode,
                             const VariableInfo& info, ConstPartPtr lhs, ConstPartPtr rhs,
                             ExprErrors& errors);

[[nodiscard]] const StructureInfo& cardInfo() {
    static const StructureInfo value{
        "cardinfo", "5", false,
        {StructureMember{"castindex", "5", false, VariantVal::numValue(0), true,
                         VariantType::Number, -2, {}},
         StructureMember{"cardindex", "5", false, VariantVal::numValue(0), true,
                         VariantType::Number, 0, {}},
         StructureMember{"actioncardid", "5", false, VariantVal::numValue(-2), true,
                         VariantType::Number, std::numeric_limits<double>::quiet_NaN(), {}}},
        2};
    return value;
}

[[nodiscard]] bool argCount(const CProps* prop, const Function& func, ConstPartView args,
                            std::size_t min, std::size_t max,
                            ExprErrors& errors) {
    if (args.size() >= min && args.size() <= max) return true;
    String message;
    if (min == max) {
        message = expressionMessage(prop, &msgs::Msgs::expressionErrorInvalidArgumentCount,
            "Invalid argument count: %1$s, %2$s", upper(func.funcName), min);
    } else {
        message = expressionMessage(prop, &msgs::Msgs::expressionErrorInvalidArgumentCount2,
            "Invalid argument count: %1$s, %2$s-%3$s", upper(func.funcName), min, max);
    }
    addError(errors, std::move(message), func);
    return false;
}

[[nodiscard]] const NumberValue* asNumber(const ConstPartPtr& value) {
    return dynamic_cast<const NumberValue*>(value.get());
}
[[nodiscard]] const StringValue* asString(const ConstPartPtr& value) {
    return dynamic_cast<const StringValue*>(value.get());
}
[[nodiscard]] const BooleanValue* asBoolean(const ConstPartPtr& value) {
    return dynamic_cast<const BooleanValue*>(value.get());
}
[[nodiscard]] const ListValue* asList(const ConstPartPtr& value) {
    return dynamic_cast<const ListValue*>(value.get());
}
[[nodiscard]] const StructureValue* asStructure(const ConstPartPtr& value) {
    return dynamic_cast<const StructureValue*>(value.get());
}

[[nodiscard]] const NumberValue* requireNumber(const CProps* prop, const Function& func,
                                               ConstPartView args,
                                               std::size_t index,
                                               ExprErrors& errors) {
    const auto& argument = args.at(index);
    if (const auto* value = asNumber(argument)) return value;
    if (isUnknown(argument)) {
        static const NumberValue unknownValue(Token{}, 0.0);
        return &unknownValue;
    }
    addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorArgumentIsNotNumber,
        "Argument is not number: %1$s, %2$s", upper(func.funcName), index + 1), requiredPart(argument));
    return nullptr;
}
[[nodiscard]] std::vector<const NumberValue*> requireAllNumbers(
    const CProps* prop, const Function& func, ConstPartView args, ExprErrors& errors) {
    // ネイティブポインターは、保持されたPartListまたは不変のUnknown
    // 代替値から借用する。Dはこれらの数値引数をGC参照で保持する。
    std::vector<const NumberValue*> result;
    result.reserve(args.size());
    for (std::size_t index = 0; index < args.size(); ++index) {
        const auto* value = requireNumber(prop, func, args, index, errors);
        if (!value) return {};
        result.push_back(value);
    }
    return result;
}
[[nodiscard]] const StringValue* requireString(const CProps* prop, const Function& func,
                                               ConstPartView args,
                                               std::size_t index,
                                               ExprErrors& errors) {
    const auto& argument = args.at(index);
    if (const auto* value = asString(argument)) return value;
    if (isUnknown(argument)) {
        static const StringValue unknownValue(Token{}, {});
        return &unknownValue;
    }
    addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorArgumentIsNotString,
        "Argument is not string: %1$s, %2$s", upper(func.funcName), index + 1), requiredPart(argument));
    return nullptr;
}
[[nodiscard]] const BooleanValue* requireBoolean(const CProps* prop, const Function& func,
                                                 ConstPartView args,
                                                 std::size_t index,
                                                 ExprErrors& errors) {
    const auto& argument = args.at(index);
    if (const auto* value = asBoolean(argument)) return value;
    if (isUnknown(argument)) {
        static const BooleanValue unknownValue(Token{}, false);
        return &unknownValue;
    }
    addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorArgumentIsNotBoolean,
        "Argument is not boolean: %1$s, %2$s", upper(func.funcName), index + 1), requiredPart(argument));
    return nullptr;
}
[[nodiscard]] const ListValue* requireList(const CProps* prop, const Function& func,
                                           ConstPartView args,
                                           std::size_t index,
                                           ExprErrors& errors) {
    const auto& argument = args.at(index);
    if (const auto* value = asList(argument)) return value;
    if (isUnknown(argument)) return nullptr;
    addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorArgumentIsNotList,
        "Argument is not list: %1$s, %2$s", upper(func.funcName), index + 1), requiredPart(argument));
    return nullptr;
}
[[nodiscard]] const StructureValue* requireStructure(const CProps* prop, const Function& func,
                                                     ConstPartView args,
                                                     std::size_t index,
                                                     StringView name,
                                                     ExprErrors& errors) {
    const auto& argument = args.at(index);
    if (const auto* value = asStructure(argument);
        value && value->structName == name) return value;
    if (isUnknown(argument)) return nullptr;
    addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorArgumentIsNotStructure,
        "Argument is not structure %3$s: %1$s, %2$s", upper(func.funcName), index + 1,
        upper(name)), requiredPart(argument));
    return nullptr;
}

[[nodiscard]] const NumberValue* requireMinNumber(const CProps* prop, const Function& func,
                                                   ConstPartView args,
                                                   std::size_t index, double minimum,
                                                   EvalMode mode, ExprErrors& errors) {
    const auto* result = requireNumber(prop, func, args, index, errors);
    if (!result) return nullptr;
    if (result->numVal < minimum) {
        if (mode != EvalMode::TypeCheck) {
            addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorMinimumValue,
                "Minimum value: %1$s, %2$s, %3$s < %4$s", upper(func.funcName), index + 1,
                minimum, result->numVal), *args[index]);
        }
        return nullptr;
    }
    return result;
}

// Dは整数変換後に有限のインデックスを検査する。C++では先に変
// 換可能範囲を確認し、非有限値や過大値の浮動小数点から整数への未
// 定義変換を避けつつ、小数部分を同じように切り捨てる。範囲外の
// 代替処理は防御用であり、Dの実行結果と同じだとはみなさない。
template<class Integer>
[[nodiscard]] std::optional<Integer> finiteTruncatedInteger(double value) {
    if (!std::isfinite(value)) return {};
    const auto integral = std::trunc(value);
    // 2の累乗の排他的上限は、long doubleの精度がdouble
    // と同じ対象でも正確に表せる。uint64の最大値の境界も同様。
    const auto upper = std::ldexp(1.0, std::numeric_limits<Integer>::digits);
    const auto lower = std::numeric_limits<Integer>::is_signed ? -upper : 0.0;
    if (integral < lower || integral >= upper) return {};
    return static_cast<Integer>(integral);
}

template<class Integer>
[[nodiscard]] Integer checkedTruncatedInteger(double value) {
    // Dの浮動小数点から整数へのキャストは切り捨てるが、範囲外では無効
    // な結果になる。C++では未定義動作になるため、NaNや無限大を
    // 下限違反として扱うのではなく、この共通の変換境界で失敗させる。
    const auto result = finiteTruncatedInteger<Integer>(value);
    if (!result) throw std::out_of_range("expression integer conversion out of range");
    return *result;
}

[[nodiscard]] std::optional<std::uint32_t> truncatedUint32Bits(double value) {
    // int64を経由して切り捨て、その後uint32のビット列を保持
    // する。これは、取得関数のDのfloatからuint/intへの変
    // 換が定義される範囲で一致する。負の番兵値など範囲外の入力は、Dの
    // キャスト結果と同じとは保証せず、既存の一定のネイティブ動作を維持
    // する。非有限値とint64範囲外の入力は防御的な代替処理を使う。
    const auto integer = finiteTruncatedInteger<std::int64_t>(value);
    if (!integer) return {};
    return static_cast<std::uint32_t>(*integer);
}

void operatorTypeError(const CProps* prop, const ConstPartPtr& operand,
                       const msgs::Message msgs::Msgs::* field,
                       StringView fallback, ExprErrors& errors) {
    if (isUnknown(operand)) return;
    const auto& part = requiredPart(operand);
    addError(errors, expressionMessage(prop, field, fallback, part.token.token), part);
}

void reportRawSymbol(const CProps* prop, const ConstPartPtr& operand, ExprErrors& errors) {
    if (dynamic_cast<const Symbol*>(operand.get())) {
        const auto& part = requiredPart(operand);
        addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorUnknownSymbol,
            "Unknown symbol: %1$s", part.token.token), part);
    }
}

template<class Value>
struct OperandPair {
    const Value* lhs{};
    const Value* rhs{};
    explicit operator bool() const noexcept { return lhs && rhs; }
};

template<class Value>
[[nodiscard]] OperandPair<Value> typedOperands(
    const CProps* prop, const ConstPartPtr& lhs, const ConstPartPtr& rhs,
    const msgs::Message msgs::Msgs::* field, StringView fallback, ExprErrors& errors) {
    // DのGC参照に対応し、呼び出し側が保持する被演算子のOwnerから
    // 借用する。Unknownによる抑止も含め、まずlhsを検証する。
    const auto* left = dynamic_cast<const Value*>(lhs.get());
    if (!left) { operatorTypeError(prop, lhs, field, fallback, errors); return {}; }
    const auto* right = dynamic_cast<const Value*>(rhs.get());
    if (!right) { operatorTypeError(prop, rhs, field, fallback, errors); return {}; }
    return {left, right};
}

[[nodiscard]] OperandPair<StringValue> stringOperands(
    const CProps* prop, const ConstPartPtr& lhs, const ConstPartPtr& rhs, ExprErrors& errors) {
    return typedOperands<StringValue>(prop, lhs, rhs,
        &msgs::Msgs::expressionErrorValueIsNotString, "Value is not string: %1$s", errors);
}
[[nodiscard]] OperandPair<NumberValue> numberOperands(
    const CProps* prop, const ConstPartPtr& lhs, const ConstPartPtr& rhs, ExprErrors& errors) {
    return typedOperands<NumberValue>(prop, lhs, rhs,
        &msgs::Msgs::expressionErrorValueIsNotNumber, "Value is not number: %1$s", errors);
}
[[nodiscard]] OperandPair<BooleanValue> booleanOperands(
    const CProps* prop, const ConstPartPtr& lhs, const ConstPartPtr& rhs, ExprErrors& errors) {
    return typedOperands<BooleanValue>(prop, lhs, rhs,
        &msgs::Msgs::expressionErrorValueIsNotBoolean, "Value is not boolean: %1$s", errors);
}
[[nodiscard]] const ListValue* listOperand(
    const CProps* prop, const ConstPartPtr& operand, ExprErrors& errors) {
    // 呼び出し側のOwnerが保持する、GC参照に相当する同じオブジェクトを借用する。
    const auto* value = asList(operand);
    if (!value) operatorTypeError(prop, operand,
        &msgs::Msgs::expressionErrorValueIsNotList, "Value is not list: %1$s", errors);
    return value;
}
[[nodiscard]] const StructureValue* structureOperand(
    const CProps* prop, const ConstPartPtr& operand, ExprErrors& errors) {
    // listOperandと同様に、呼び出し側がこの借用オブジェクトのOwnerを保持する。
    const auto* value = asStructure(operand);
    if (!value) operatorTypeError(prop, operand,
        &msgs::Msgs::expressionErrorValueIsNotStructure, "Value is not structure: %1$s", errors);
    return value;
}

enum class OperandOrdering { Less, Equal, Greater, Unordered };

// Dのリスト順序比較に対する既存の辞書順の修正を維持する。順序が決ま
// る不一致要素で結果を確定し、より大きい組を無視して後の小さい組を採
// 用しない。子の比較が失敗または順序不定なら、Dと同様に継続する。
// 単一値のNaNは順序不定であり、三分比較の等しい結果にはしない。
[[nodiscard]] std::optional<OperandOrdering> compareOrdering(
    const CProps* prop, const ConstPartPtr& lhs, const ConstPartPtr& rhs, ExprErrors& errors,
    bool reportSymbols = false) {
    if (reportSymbols) {
        reportRawSymbol(prop, lhs, errors);
        reportRawSymbol(prop, rhs, errors);
    }
    if (const auto* left = asList(lhs)) {
        const auto* right = listOperand(prop, rhs, errors);
        if (!right) return {};
        const auto length = std::min(left->listVal.size(), right->listVal.size());
        for (std::size_t i = 0; i < length; ++i) {
            const auto nested = compareOrdering(prop, left->listVal[i], right->listVal[i], errors, true);
            if (nested && *nested != OperandOrdering::Equal && *nested != OperandOrdering::Unordered)
                return nested;
        }
        return left->listVal.size() < right->listVal.size() ? OperandOrdering::Less
            : left->listVal.size() > right->listVal.size() ? OperandOrdering::Greater
            : OperandOrdering::Equal;
    }
    const auto values = numberOperands(prop, lhs, rhs, errors);
    if (!values) return {};
    if (values.lhs->numVal < values.rhs->numVal) return OperandOrdering::Less;
    if (values.lhs->numVal > values.rhs->numVal) return OperandOrdering::Greater;
    if (values.lhs->numVal == values.rhs->numVal) return OperandOrdering::Equal;
    return OperandOrdering::Unordered;
}

// Dの演算子の等価判定は、遅延関数を評価せず保存済み要素を比較
// する。局所的な型の診断は、リスト内でない場合だけスコープ終
// 了時に公開する。構造の違いの診断は呼び出し側へ直ちに渡す。
[[nodiscard]] bool operatorEqual(const CProps* prop, const ConstPartPtr& lhs,
                                 const ConstPartPtr& rhs, ExprErrors& errors,
                                 bool unequal = false, bool inList = false) {
    ExprErrors local;
    const auto compare = [&]() -> bool {
        if (const auto* left = asList(lhs)) {
            const auto* right = listOperand(prop, rhs, local);
            if (!right) return false;
            const auto length = std::min(left->listVal.size(), right->listVal.size());
            for (std::size_t i = 0; i < length; ++i)
                if (!operatorEqual(prop, left->listVal[i], right->listVal[i], local, false, true)) return unequal;
            const bool equal = left->listVal.size() == right->listVal.size();
            return unequal ? !equal : equal;
        }
        if (const auto* left = asStructure(lhs)) {
            const auto* right = structureOperand(prop, rhs, local);
            if (!right) return false;
            if (left->structName != right->structName) {
                addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorCompareDifferentStructures,
                    "Compare different structures: %1$s <> %2$s", left->structName, right->structName), requiredPart(rhs));
                return false;
            }
            // Dは二つの配列を組にしてたどる前に、要素数の一致をアサートする。
            if (left->structVal.size() != right->structVal.size())
                throw std::invalid_argument("structure comparison member count mismatch");
            for (std::size_t i = 0; i < left->structVal.size(); ++i)
                if (!operatorEqual(prop, left->structVal[i], right->structVal[i], local, false, true)) return unequal;
            return !unequal;
        }
        bool equal = false;
        if (!inList && (asString(lhs) || asString(rhs))) {
            if (asList(rhs)) operatorTypeError(prop, lhs,
                &msgs::Msgs::expressionErrorValueIsNotList, "Value is not list: %1$s", errors);
            else if (asStructure(rhs)) operatorTypeError(prop, lhs,
                &msgs::Msgs::expressionErrorValueIsNotStructure, "Value is not structure: %1$s", errors);
            const auto leftText = requiredPart(lhs).stringValue();
            const auto rightText = requiredPart(rhs).stringValue();
            equal = leftText == rightText;
        } else if (asString(lhs)) {
            const auto values = stringOperands(prop, lhs, rhs, local);
            equal = values && values.lhs->strVal == values.rhs->strVal;
        } else if (asBoolean(lhs)) {
            const auto values = booleanOperands(prop, lhs, rhs, local);
            equal = values && values.lhs->boolVal == values.rhs->boolVal;
        } else {
            const auto values = numberOperands(prop, lhs, rhs, local);
            equal = values && values.lhs->numVal == values.rhs->numVal;
        }
        return unequal ? !equal : equal;
    };
    const auto publish = [&] {
        if (!inList) errors.insert(errors.end(), local.begin(), local.end());
    };
    bool result;
    try { result = compare(); }
    catch (...) { publish(); throw; }
    publish();
    return result;
}

[[nodiscard]] String idString(StringView value) { return String(value); }
template <typename Id>
[[nodiscard]] String idString(const Id& value) {
    if constexpr (requires { value.id; }) {
        return String(value.id);
    } else {
        return String(value);
    }
}

[[nodiscard]] lib::Array<Token> tokenize(const CProps* prop, StringView input,
                                         ExprErrors& errors, Token& endToken) {
    String normalized;
    normalized.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\r') {
            if (i + 1 < input.size() && input[i + 1] == '\n') ++i;
            normalized.push_back('\n');
        } else if (input[i] == static_cast<char>(0xe3) && i + 2 < input.size() &&
                   static_cast<unsigned char>(input[i + 1]) == 0x80 &&
                   static_cast<unsigned char>(input[i + 2]) == 0x80) {
            normalized.push_back(' ');
            i += 2;
        } else normalized.push_back(input[i]);
    }
    const auto scalar = [&](std::size_t offset) {
        return enumutils::decodeUtf8Scalar(normalized, offset);
    };
    const auto digit = [](char32_t c) { return c >= U'0' && c <= U'9'; };
    const auto letter = [](char32_t c) {
        // 固定したDの単純ケースフォールドによる[a-z]の閉包は、長いsとケルビン記号だけ
        // を追加する。完全なtoLowerでは、ドット付き大文字Iまで誤って含めてしまう。
        return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || c == 0x17f || c == 0x212a;
    };
    lib::Array<Token> result;
    std::size_t search = 0, previousEnd = 0, line = 1, pos = 1;
    // ロケール依存のネイティブ正規表現ではなく、Dの固定された順序付き正規表現に特化
    // する。matchAllの未一致の先頭部分の読み飛ばし、最長一致・後戻り、UTF-8
    // のバイト位置の動作を維持し、全走査経路でスカラー値のデコーダーを共有する。
    while (search < normalized.size()) {
        const auto first = scalar(search);
        const auto begin = search;
        auto end = first.next;
        bool matched = true, whitespace = false;
        if (digit(first.value)) {
            while (end < normalized.size()) {
                const auto next = scalar(end);
                if (!digit(next.value)) break;
                end = next.next;
            }
            if (end < normalized.size() && scalar(end).value == U'.') {
                const auto fraction = scalar(end).next;
                if (fraction < normalized.size() && digit(scalar(fraction).value)) {
                    end = scalar(fraction).next;
                    while (end < normalized.size()) {
                        const auto next = scalar(end);
                        if (!digit(next.value)) break;
                        end = next.next;
                    }
                }
            }
        } else if (letter(first.value) || first.value == U'_') {
            while (end < normalized.size()) {
                const auto next = scalar(end);
                if (!(letter(next.value) || digit(next.value) || next.value == U'_')) break;
                end = next.next;
            }
        } else if (std::u32string_view{U"+-*/%~.(),"}.find(first.value) != std::u32string_view::npos) {
            // これらの固定の一文字の選択肢は、引用符付き文字列より先に評価する。
        } else if (first.value == U'"' || first.value == U'@') {
            auto probe = first.next;
            if (first.value == U'@') {
                if (probe == normalized.size() || scalar(probe).value != U'"') matched = false;
                else probe = scalar(probe).next;
            }
            if (matched) {
                std::size_t closing = String::npos;
                while (probe < normalized.size()) {
                    const auto next = scalar(probe);
                    if (next.value != U'"') { probe = next.next; continue; }
                    closing = next.next;
                    if (next.next < normalized.size() && scalar(next.next).value == U'"') {
                        probe = scalar(next.next).next;
                        continue;
                    }
                    break;
                }
                if (closing == String::npos) matched = false;
                else end = closing;
            }
        } else if (first.value == U'<' || first.value == U'>' || first.value == U'=') {
            if (first.value != U'=' && end < normalized.size()) {
                const auto next = scalar(end);
                if (next.value == U'=' || (first.value == U'<' && next.value == U'>')) end = next.next;
            }
        } else if (first.value == U'\n') {
            whitespace = true; // 改行リテラルは、最長一致の\s+より先に評価する。
        } else if (enumutils::isWhiteSpace(first.value)) {
            whitespace = true;
            while (end < normalized.size()) {
                const auto next = scalar(end);
                if (!enumutils::isWhiteSpace(next.value)) break;
                end = next.next;
            }
        } else matched = false;
        if (!matched) { search = first.next; continue; }
        // Dの正規表現VMは、一文字を消費した後、一致を公開する前に
        // 次のスカラー値を読む。不正な後続部分での失敗も維持する。
        if (end < normalized.size()) static_cast<void>(scalar(end));
        if (begin != previousEnd) {
            errors.emplace_back(expressionMessage(prop, &msgs::Msgs::expressionErrorInvalidCharacter, "Invalid character."),
                                line, pos, __FILE__, __LINE__);
        }
        previousEnd = end;
        const auto hit = StringView(normalized).substr(begin, end - begin);
        if (!whitespace) result.push_back(Token{true, line, pos, begin, String(hit)});
        // Dが行を進めるのは、正確に「\n」へ一致した場合だけ。空白から
        // 始まる最長一致の空白列は改行も含み得るが、バイト単位で扱う。
        if (hit == "\n") { ++line; pos = 1; }
        else pos += hit.size();
        search = end;
    }
    // 次の正規表現の一致がなければ、未一致の末尾に対する隙間の診断は出さない。
    endToken = Token{true, line, pos, previousEnd, {}};
    return result;
}

}  // namespace

VariantVal::VariantVal(const flag::Variant& value)
    : valid(true), type(value.type()), numVal(value.numVal()), strVal(value.strVal()),
      boolVal(value.boolVal()), structName(value.structName()) {
    listVal.reserve(value.listVal().size());
    for (const auto& item : value.listVal()) listVal.emplace_back(item);
    structVal.reserve(value.structVal().size());
    for (const auto& item : value.structVal()) structVal.emplace_back(item);
}

String variantValueToTextImpl(VariantType type, double numVal, StringView strVal,
                              bool boolVal, const lib::Array<VariantVal>& listVal,
                              StringView structName,
                              const lib::Array<VariantVal>& structVal) {
    switch (type) {
    case VariantType::Number: return numberText(numVal);
    case VariantType::String: return quote(strVal);
    case VariantType::Boolean: return boolVal ? "TRUE" : "FALSE";
    case VariantType::List: {
        String result{"LIST("};
        for (std::size_t i = 0; i < listVal.size(); ++i) {
            if (i) result += ", ";
            result += variantValueToText(listVal[i]);
        }
        result += ')';
        return result;
    }
    case VariantType::Structure: {
        String result = upper(structName) + "(";
        const auto values = cutOptionalMembers(
            structName, lib::SharedSlice<VariantVal>(structVal));
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i) result += ", ";
            result += variantValueToText(values[i]);
        }
        result += ')';
        return result;
    }
    }
    // Dはfinal switchを使う。未知の識別値は無効な状態。
    throw std::invalid_argument("unknown expression text variant type");
}

String variantValueToPreviewTextImpl(VariantType type, double numVal,
                                     StringView strVal, bool boolVal,
                                     const lib::Array<VariantVal>& listVal,
                                     StringView structName,
                                     const lib::Array<VariantVal>& structVal) {
    switch (type) {
    case VariantType::Number:
        return variantValueToTextImpl(type, numVal, strVal, boolVal, listVal, structName, structVal);
    case VariantType::String: return String(strVal);
    case VariantType::Boolean: return boolVal ? "TRUE" : "FALSE";
    case VariantType::List: {
        String result{"LIST("};
        for (std::size_t i = 0; i < listVal.size(); ++i) {
            if (i) result += ", ";
            result += variantValueToPreviewText(listVal[i]);
        }
        result += ')';
        return result;
    }
    case VariantType::Structure: {
        String result = upper(structName) + "(";
        const auto values = cutOptionalMembers(
            structName, lib::SharedSlice<VariantVal>(structVal));
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i) result += ", ";
            result += variantValueToPreviewText(values[i]);
        }
        result += ')';
        return result;
    }
    }
    throw std::invalid_argument("unknown expression preview variant type");
}

String variantValueToText(const VariantVal& value) {
    return variantValueToTextImpl(value.type, value.numVal, value.strVal, value.boolVal,
                                  value.listVal, value.structName, value.structVal);
}
String variantValueToPreviewText(const VariantVal& value) {
    return variantValueToPreviewTextImpl(value.type, value.numVal, value.strVal,
                                         value.boolVal, value.listVal,
                                         value.structName, value.structVal);
}
String variantValueToText(const flag::Variant& value) {
    return variantValueToText(VariantVal(value));
}
String variantValueToPreviewText(const flag::Variant& value) {
    return variantValueToPreviewText(VariantVal(value));
}

VariantVal variantValueFromText(StringView text) {
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        String value(text.substr(1, text.size() - 2));
        String unescaped;
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '"' && i + 1 < value.size() && value[i + 1] == '"') {
                unescaped.push_back('"');
                ++i;
            } else if (value[i] == '"') {
                return VariantVal::invalidValue();
            } else {
                unescaped.push_back(value[i]);
            }
        }
        return VariantVal::strValue(std::move(unescaped));
    }
    const auto valueLower = lower(text);
    if (valueLower == "true") return VariantVal::boolValue(true);
    if (valueLower == "false") return VariantVal::boolValue(false);
    std::size_t index = 0;
    if (index < text.size() && (text[index] == '+' || text[index] == '-')) ++index;
    const auto integerStart = index;
    while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) ++index;
    if (index == integerStart) return VariantVal::invalidValue();
    if (index < text.size() && text[index] == '.') {
        ++index;
        const auto fractionStart = index;
        while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) ++index;
        if (index == fractionStart) return VariantVal::invalidValue();
    }
    if (index != text.size()) return VariantVal::invalidValue();
    double numberValue{};
    if (parseNumber(text, numberValue)) return VariantVal::numValue(numberValue);
    return VariantVal::invalidValue();
}

lib::Array<FuncDef> functionDefinitions(const CProps* prop) {
    static const std::array<std::pair<StringView, ArgType>, 105> definitions{{
        {"LEN", ArgType::Number}, {"FIND", ArgType::Number}, {"LEFT", ArgType::String},
        {"RIGHT", ArgType::String}, {"MID", ArgType::String}, {"STR", ArgType::String},
        {"VALUE", ArgType::Number}, {"INT", ArgType::Number}, {"LIST", ArgType::List},
        {"LIST_E", ArgType::List}, {"AT", ArgType::Any}, {"LLEN", ArgType::Number},
        {"LFIND", ArgType::List}, {"LLEFT", ArgType::List}, {"LRIGHT", ArgType::List},
        {"LMID", ArgType::List}, {"IF", ArgType::Any}, {"IS_STR", ArgType::Boolean},
        {"IS_NUMBER", ArgType::Boolean}, {"IS_LIST", ArgType::Boolean}, {"AND_D", ArgType::Boolean},
        {"OR_D", ArgType::Boolean}, {"DICE", ArgType::Number}, {"MAX", ArgType::Number},
        {"MIN", ArgType::Number}, {"VAR", ArgType::Any}, {"FLAGVALUE", ArgType::Boolean},
        {"FLAGTEXT", ArgType::String}, {"STEPVALUE", ArgType::Number}, {"STEPTEXT", ArgType::String},
        {"STEPMAX", ArgType::Number}, {"SELECTED", ArgType::Number},
        {"LATESTUSECARDOWNER", ArgType::Number}, {"CHAREXISTS", ArgType::Boolean},
        {"CASTTYPE", ArgType::Number}, {"CASTNAME", ArgType::String}, {"CASTLEVEL", ArgType::Number},
        {"LIFERATIO", ArgType::Number}, {"STATUSVALUE", ArgType::Number}, {"STATUSROUND", ArgType::Number},
        {"SELECTEDCARD", ArgType::CardInfo}, {"CARDNAME", ArgType::String}, {"CARDTYPE", ArgType::Number},
        {"CARDRARITY", ArgType::Number}, {"CARDPRICE", ArgType::Number}, {"CARDLEVEL", ArgType::Number},
        {"CARDCOUNT", ArgType::Number}, {"FINDKEYCODE", ArgType::Number}, {"KEYCODETEXT", ArgType::String},
        {"FINDCOUPON", ArgType::Number}, {"COUPONTEXT", ArgType::String}, {"COUPONVALUE", ArgType::Number},
        {"FINDGOSSIP", ArgType::Number}, {"GOSSIPTEXT", ArgType::String}, {"PARTYNAME", ArgType::String},
        {"PARTYMONEY", ArgType::Number}, {"PARTYNUMBER", ArgType::Number}, {"YADONAME", ArgType::String},
        {"ISNOVELMODE", ArgType::Boolean},
        {"BATTLEROUND", ArgType::Number}, {"SKINTYPE", ArgType::String}, {"FOR", ArgType::List},
        {"SWITCH", ArgType::Any}, {"SUM", ArgType::Number}, {"EVAL", ArgType::Any},
        {"FORMAT", ArgType::String}, {"SIND", ArgType::Number}, {"COSD", ArgType::Number},
        {"TAND", ArgType::Number}, {"DISTANCE", ArgType::Number}, {"INPOLYGON", ArgType::Boolean},
        {"PARSEFLOAT", ArgType::Number}, {"PARSEINT", ArgType::Number}, {"FLOOR", ArgType::Number},
        {"CEIL", ArgType::Number}, {"MAKELIST", ArgType::List}, {"LFINDRAND", ArgType::Number},
        {"GETRANDOMLIST", ArgType::List}, {"CONCATSTRINGS", ArgType::String},
        {"PICKBYINDICES", ArgType::List}, {"LGETRANDOM", ArgType::Any},
        {"LSETMULTI", ArgType::Boolean}, {"LREMOVE", ArgType::Boolean},
        {"LREMOVEMULTI", ArgType::Boolean}, {"LINSERT", ArgType::Boolean},
        {"LUNIQUE", ArgType::List}, {"LREMOVEALL", ArgType::Boolean}, {"LCOUNT", ArgType::Number},
        {"LSET", ArgType::Boolean}, {"CONTAIN", ArgType::Boolean}, {"SCENARIONAME", ArgType::String},
        {"CARDMAXUSELIMIT", ArgType::Number}, {"DATETIME", ArgType::List}, {"UNIXTIME", ArgType::Number},
        {"GETVARIANTVALUEOFCARD", ArgType::Any}, {"GETFLAGVALUEOFCARD", ArgType::Boolean},
        {"GETSTEPVALUEOFCARD", ArgType::Number}, {"HAS_VARIANTVALUE", ArgType::Boolean},
        {"HAS_STEP", ArgType::Boolean}, {"HAS_FLAG", ArgType::Boolean},
        {"SETVARIANTVALUEOFCARD", ArgType::Boolean}, {"SETSTEPVALUEOFCARD", ArgType::Boolean},
        {"SETFLAGVALUEOFCARD", ArgType::Boolean}, {"MOUSEX", ArgType::Number}, {"MOUSEY", ArgType::Number},
    }};
    // DのfunctionDefinitionsは、これらの接尾辞でCProps.msgs
    // のfuncDesc*、funcShortDesc*、funcExample*
    // を読む。この表をdefinitionsと同じ順に保ち、ローカライズ
    // されたヘルプをGUIで再構築せず、コアのメタデータ契約に含める。
    static constexpr auto messageSuffixes = std::to_array<StringView>({
        "Len", "Find", "Left", "Right", "Mid", "Str", "Value", "Int",
        "List", "List_e", "At", "LLen", "LFind", "LLeft", "LRight", "LMid",
        "If", "IsStr", "IsNumber", "IsList", "AndD", "OrD", "Dice", "Max",
        "Min", "Var", "FlagValue", "FlagText", "StepValue", "StepText", "StepMax",
        "Selected", "LatestUseCardOwner", "CharExists", "CastType", "CastName",
        "CastLevel", "LifeRatio", "StatusValue", "StatusRound", "SelectedCard",
        "CardName", "CardType", "CardRarity", "CardPrice", "CardLevel", "CardCount",
        "FindKeyCode", "KeyCodeText", "FindCoupon", "CouponText", "CouponValue",
        "FindGossip", "GossipText", "PartyName", "PartyMoney", "PartyNumber", "YadoName", "IsNovelMode",
        "BattleRound", "SkinType", "For", "Switch", "Sum", "Eval", "Format", "Sind",
        "Cosd", "Tand", "Distance", "InPolygon", "ParseFloat", "ParseInt", "Floor",
        "Ceil", "List", "LFind", "GetRandomList", "ConcatStrings", "PickByIndices",
        "LGetRandom", "LSetMulti", "LRemove", "LRemoveMulti", "LInsert", "LUnique",
        "LRemoveAll", "LCount", "LSet", "Contain", "ScenarioName", "CardMaxUseLimit",
        "DateTime", "UnixTime", "Var", "FlagValue", "StepValue", "HasVariantValue",
        "HasStep", "HasFlag", "SetVariantValueOfCard", "SetStepValueOfCard",
        "SetFlagValueOfCard", "MouseX", "MouseY",
    });
    static_assert(messageSuffixes.size() == definitions.size(),
                  "Every function must have a corresponding localized-help suffix");
    lib::Array<FuncDef> result;
    result.reserve(definitions.size());
    const auto categoryOf = [](StringView name) {
        const auto in = [name](std::initializer_list<StringView> values) {
            for (const auto value : values) if (name == value) return true;
            return false;
        };
        using Category = types::FunctionCategory;
        if (in({"len", "find", "left", "right", "mid", "str", "format", "concatstrings"})) {
            return Category::StringOperation;
        }
        if (in({"value", "int", "dice", "max", "min", "floor", "ceil", "sind", "cosd", "tand", "distance"})) {
            return Category::NumberOperation;
        }
        if (in({"list", "list_e", "at", "llen", "lfind", "lleft", "lright", "lmid",
                "for", "sum", "makelist", "lfindrand", "getrandomlist", "pickbyindices",
                "lgetrandom", "lsetmulti", "lremove", "lremovemulti", "linsert", "lunique",
                "lremoveall", "lcount", "lset"})) {
            return Category::ListOperation;
        }
        if (in({"str", "value", "int", "eval", "parsefloat", "parseint", "format"})) {
            return Category::Conversion;
        }
        if (in({"var", "flagvalue", "flagtext", "stepvalue", "steptext", "stepmax"})) {
            return Category::VariableOperation;
        }
        if (in({"selected", "latestusecardowner", "charexists", "casttype", "castname",
                "castlevel", "liferatio", "statusvalue", "statusround"})) {
            return Category::CastInformation;
        }
        if (in({"selectedcard", "cardname", "cardtype", "cardrarity", "cardprice", "cardlevel",
                "cardcount", "findkeycode", "keycodetext"})) {
            return Category::CardInformation;
        }
        if (in({"findcoupon", "coupontext", "couponvalue", "findgossip", "gossiptext"})) {
            return Category::CouponInformation;
        }
        if (in({"datetime"})) return Category::ListOperation;
        if (in({"unixtime", "mousex", "mousey"})) return Category::NumberOperation;
        if (in({"cardmaxuselimit", "getvariantvalueofcard", "getflagvalueofcard",
                "getstepvalueofcard", "has_variantvalue", "has_step", "has_flag",
                "setvariantvalueofcard", "setstepvalueofcard", "setflagvalueofcard"})) {
            return Category::VariableOperation;
        }
        if (in({"partyname", "partymoney", "partynumber", "yadoname", "battleround", "skintype"})) {
            return Category::PlayingInformation;
        }
        return Category::Etc;
    };
    const auto messageValue = [prop](StringView key) -> String {
        if (!prop) return {};
        const auto* message = prop->requiredMsgs().findMessage(key);
        return message ? message->value : String{};
    };
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const auto& [name, returnType] = definitions[index];
        FuncDef definition;
        definition.name = String(name);
        definition.returnType = returnType;
        definition.category.push_back(categoryOf(lower(name)));
        const auto lowerName = lower(name);
        if (lowerName == "str" || lowerName == "value" || lowerName == "int") {
            definition.category.push_back(types::FunctionCategory::Conversion);
        }
        const auto suffix = messageSuffixes[index];
        definition.desc = messageValue("funcDesc" + String(suffix));
        definition.shortDesc = messageValue("funcShortDesc" + String(suffix));
        definition.example = messageValue("funcExample" + String(suffix));
        // ALLの拡張には、従来のDのメッセージ資源に対応項
        // 目がない。この項目へ次の関数のヘルプを流用しない。
        if (lowerName == "isnovelmode") {
            if (definition.desc.empty()) definition.desc = "novelモードで動作している場合はTRUE、それ以外はFALSEを返します。";
            if (definition.shortDesc.empty()) definition.shortDesc = "novelモードか判定";
            if (definition.example.empty()) definition.example = "ISNOVELMODE()";
        }
        // これらのDのMsg項目は、メンバー名と一致しない
        // 従来のXMLキーを維持する。そのためキー検索だけ
        // では、説明と短い説明のレコードを区別できない。
        if (prop && suffix == "Find") {
            definition.shortDesc = prop->requiredMsgs().funcShortDescFind.value;
        } else if (prop && suffix == "LFind") {
            definition.shortDesc = prop->requiredMsgs().funcShortDescLFind.value;
        } else if (prop && suffix == "KeyCodeText") {
            definition.desc = prop->requiredMsgs().funcDescKeyCodeText.value;
        }
        if (lowerName == "statusvalue" || lowerName == "statusround") {
            definition.desc = utils::tryFormat(
                definition.desc, messageValue("funcAdditionalDescStatuses"));
        } else if (lowerName == "findkeycode" || lowerName == "findcoupon" ||
                   lowerName == "findgossip") {
            definition.desc = utils::tryFormat(
                definition.desc, messageValue("funcAdditionalDescSpecialCharacters"));
        }
        result.push_back(std::move(definition));
    }
    const auto argument = [](ArgType type, ArgType initType = ArgType::NoArgument,
                             StringView initValue = {}, bool optional = false,
                             bool varArg = false) {
        return ArgDef{type, {}, initType, String(initValue), optional, varArg};
    };
    const auto setArgs = [&](std::initializer_list<StringView> names,
                             std::initializer_list<ArgDef> args) {
        for (auto& definition : result) {
            for (const auto name : names) {
                if (definition.name == name) {
                    definition.args.assign(args.begin(), args.end());
                    break;
                }
            }
        }
    };
    setArgs({"LEN"}, {argument(ArgType::String, ArgType::String)});
    setArgs({"FIND"}, {argument(ArgType::String, ArgType::String), argument(ArgType::String, ArgType::String),
                        argument(ArgType::Number, ArgType::NoArgument, "1", true)});
    setArgs({"LEFT", "RIGHT"}, {argument(ArgType::String, ArgType::String),
                                  argument(ArgType::Number, ArgType::Number, "0")});
    setArgs({"MID"}, {argument(ArgType::String, ArgType::String),
                       argument(ArgType::Number, ArgType::Number, "1"),
                       argument(ArgType::Number, ArgType::NoArgument, "0", true)});
    setArgs({"STR"}, {argument(ArgType::Any, ArgType::String)});
    setArgs({"VALUE", "INT"}, {argument(ArgType::NumberOrString, ArgType::Number, "0")});
    setArgs({"LIST", "LIST_E", "MAKELIST"},
            {argument(ArgType::Any, ArgType::String, {}, false, true)});
    setArgs({"AT"}, {argument(ArgType::List, ArgType::List), argument(ArgType::Number, ArgType::Number, "1")});
    setArgs({"LLEN"}, {argument(ArgType::List, ArgType::List)});
    setArgs({"LFIND", "LFINDRAND"}, {argument(ArgType::Any, ArgType::String), argument(ArgType::List, ArgType::List),
                                       argument(ArgType::Number, ArgType::NoArgument, "1", true)});
    setArgs({"LLEFT", "LRIGHT"}, {argument(ArgType::List, ArgType::List),
                                    argument(ArgType::Number, ArgType::Number, "0")});
    setArgs({"LMID"}, {argument(ArgType::List, ArgType::List),
                        argument(ArgType::Number, ArgType::Number, "1"),
                        argument(ArgType::Number, ArgType::NoArgument, "0", true)});
    setArgs({"IF"}, {argument(ArgType::Boolean, ArgType::Boolean, "TRUE"),
                      argument(ArgType::Any, ArgType::String),
                      argument(ArgType::Any, ArgType::String)});
    setArgs({"IS_STR", "IS_NUMBER", "IS_LIST"},
            {argument(ArgType::Any, ArgType::Expression)});
    setArgs({"AND_D", "OR_D"},
            {argument(ArgType::Boolean, ArgType::Boolean, "TRUE", false, true)});
    setArgs({"DICE"}, {argument(ArgType::Number, ArgType::Number, "1"),
                        argument(ArgType::Number, ArgType::Number, "6")});
    setArgs({"MAX", "MIN"},
            {argument(ArgType::Number, ArgType::Number, "0", false, true)});
    setArgs({"VAR"}, {argument(ArgType::Variant, ArgType::Variant)});
    setArgs({"FLAGVALUE"}, {argument(ArgType::Flag, ArgType::Flag)});
    setArgs({"FLAGTEXT"}, {argument(ArgType::Flag, ArgType::Flag),
                            argument(ArgType::Boolean, ArgType::NoArgument, "TRUE", true)});
    setArgs({"STEPVALUE"}, {argument(ArgType::Step, ArgType::Step)});
    setArgs({"STEPTEXT"}, {argument(ArgType::Step, ArgType::Step),
                            argument(ArgType::Number, ArgType::NoArgument, "0", true)});
    setArgs({"STEPMAX"}, {argument(ArgType::Step, ArgType::Step)});
    setArgs({"CHAREXISTS", "CASTTYPE", "CASTNAME", "CASTLEVEL", "LIFERATIO"},
            {argument(ArgType::Number, ArgType::Expression, "SELECTED()")});
    setArgs({"STATUSVALUE", "STATUSROUND"},
            {argument(ArgType::Number, ArgType::Expression, "SELECTED()"),
             argument(ArgType::Status, ArgType::Status, "POISON")});
    setArgs({"CARDNAME", "CARDTYPE", "CARDRARITY", "CARDPRICE", "CARDLEVEL", "CARDCOUNT"},
            {argument(ArgType::CardInfo, ArgType::Expression, "SELECTEDCARD()")});
    setArgs({"FINDKEYCODE"}, {argument(ArgType::CardInfo, ArgType::Expression, "SELECTEDCARD()"),
                              argument(ArgType::String, ArgType::String, "*"),
                              argument(ArgType::Number, ArgType::NoArgument, {}, true)});
    setArgs({"KEYCODETEXT"}, {argument(ArgType::CardInfo, ArgType::Expression, "SELECTEDCARD()"),
                               argument(ArgType::Number, ArgType::Number, "1")});
    setArgs({"FINDCOUPON"}, {argument(ArgType::Number, ArgType::Number, "1"),
                              argument(ArgType::String, ArgType::String, "*"),
                              argument(ArgType::Number, ArgType::NoArgument, "1", true)});
    setArgs({"COUPONTEXT"}, {argument(ArgType::Number, ArgType::Expression, "SELECTED()"),
                              argument(ArgType::Number, ArgType::Number, "1")});
    setArgs({"COUPONVALUE"}, {argument(ArgType::Number, ArgType::Expression, "SELECTED()"),
                               argument(ArgType::String, ArgType::String)});
    setArgs({"FINDGOSSIP"}, {argument(ArgType::String, ArgType::String, "*"),
                              argument(ArgType::Number, ArgType::NoArgument, "1", true)});
    setArgs({"GOSSIPTEXT"}, {argument(ArgType::Number, ArgType::Number, "1")});
    setArgs({"FOR"}, {argument(ArgType::Any, ArgType::Any, "5"),
                       argument(ArgType::Any, ArgType::String), argument(ArgType::Any, ArgType::Any)});
    setArgs({"SWITCH"}, {argument(ArgType::Number, ArgType::Number, "1"),
                          argument(ArgType::Any, ArgType::Any), argument(ArgType::List, ArgType::List)});
    setArgs({"SUM"}, {argument(ArgType::List, ArgType::List),
                       argument(ArgType::Boolean, ArgType::Boolean, "FALSE", true)});
    setArgs({"EVAL"}, {argument(ArgType::Any, ArgType::String)});
    setArgs({"FORMAT"}, {argument(ArgType::String, ArgType::String),
                          argument(ArgType::Any, ArgType::String, {}, true, true)});
    setArgs({"SIND", "COSD", "TAND", "FLOOR", "CEIL"},
            {argument(ArgType::Number, ArgType::Number, "0")});
    setArgs({"DISTANCE"}, {argument(ArgType::Number, ArgType::Number, "0"),
                            argument(ArgType::Number, ArgType::Number, "0"),
                            argument(ArgType::Number, ArgType::Number, "0"),
                            argument(ArgType::Number, ArgType::Number, "0")});
    setArgs({"INPOLYGON"}, {argument(ArgType::List, ArgType::List), argument(ArgType::List, ArgType::List)});
    setArgs({"PARSEFLOAT", "PARSEINT"}, {argument(ArgType::Any, ArgType::String),
                                           argument(ArgType::Any, ArgType::NoArgument, "0", true)});
    setArgs({"GETRANDOMLIST"}, {argument(ArgType::Number, ArgType::Number, "1"),
                                 argument(ArgType::Number, ArgType::Number, "1"),
                                 argument(ArgType::Number, ArgType::Number, "1")});
    setArgs({"CONCATSTRINGS"}, {argument(ArgType::List, ArgType::List),
                                 argument(ArgType::String, ArgType::String, {}, true)});
    setArgs({"PICKBYINDICES"}, {argument(ArgType::List, ArgType::List), argument(ArgType::List, ArgType::List)});
    setArgs({"LGETRANDOM"}, {argument(ArgType::List, ArgType::List)});
    setArgs({"LSETMULTI"}, {argument(ArgType::String, ArgType::String), argument(ArgType::List, ArgType::List),
                             argument(ArgType::List, ArgType::List)});
    setArgs({"LREMOVE"}, {argument(ArgType::String, ArgType::String),
                           argument(ArgType::Number, ArgType::Number, "1")});
    setArgs({"LREMOVEMULTI"}, {argument(ArgType::String, ArgType::String), argument(ArgType::List, ArgType::List)});
    setArgs({"LINSERT"}, {argument(ArgType::String, ArgType::String),
                           argument(ArgType::Number, ArgType::Number, "1"),
                           argument(ArgType::Any, ArgType::Expression)});
    setArgs({"LUNIQUE"}, {argument(ArgType::List, ArgType::List)});
    setArgs({"LREMOVEALL"}, {argument(ArgType::String, ArgType::String), argument(ArgType::Any, ArgType::String)});
    setArgs({"LCOUNT"}, {argument(ArgType::Any, ArgType::String),
                          argument(ArgType::Any, ArgType::String)});
    setArgs({"LSET"}, {argument(ArgType::String, ArgType::String),
                        argument(ArgType::NumberOrList, ArgType::Number, "1"),
                        argument(ArgType::Any, ArgType::String)});
    setArgs({"CONTAIN"}, {argument(ArgType::List, ArgType::List), argument(ArgType::List, ArgType::List)});
    setArgs({"CARDMAXUSELIMIT"}, {argument(ArgType::CardInfo, ArgType::CardInfo)});
    setArgs({"GETVARIANTVALUEOFCARD"}, {argument(ArgType::CardInfo, ArgType::CardInfo),
                                         argument(ArgType::Variant, ArgType::Variant)});
    setArgs({"GETFLAGVALUEOFCARD"}, {argument(ArgType::CardInfo, ArgType::CardInfo),
                                      argument(ArgType::Flag, ArgType::Flag)});
    setArgs({"GETSTEPVALUEOFCARD"}, {argument(ArgType::CardInfo, ArgType::CardInfo),
                                      argument(ArgType::Step, ArgType::Step)});
    setArgs({"HAS_VARIANTVALUE", "HAS_STEP", "HAS_FLAG"},
            {argument(ArgType::CardInfo, ArgType::CardInfo), argument(ArgType::String, ArgType::String)});
    setArgs({"SETVARIANTVALUEOFCARD"}, {argument(ArgType::CardInfo, ArgType::CardInfo),
                                         argument(ArgType::String, ArgType::String), argument(ArgType::Any, ArgType::String)});
    setArgs({"SETSTEPVALUEOFCARD"}, {argument(ArgType::CardInfo, ArgType::CardInfo),
                                      argument(ArgType::String, ArgType::String), argument(ArgType::Number, ArgType::Number, "0")});
    setArgs({"SETFLAGVALUEOFCARD"}, {argument(ArgType::CardInfo, ArgType::CardInfo),
                                       argument(ArgType::String, ArgType::String), argument(ArgType::Boolean, ArgType::Boolean, "TRUE")});
    const auto setArgNames = [&](std::initializer_list<StringView> names,
                                 std::initializer_list<StringView> messageKeys) {
        if (!prop) return;
        for (auto& definition : result) {
            const auto selected = std::find(names.begin(), names.end(), StringView(definition.name));
            if (selected == names.end() || definition.args.size() != messageKeys.size()) continue;
            auto key = messageKeys.begin();
            for (auto& arg : definition.args) {
                arg.name = messageValue(*key);
                ++key;
            }
        }
    };
    setArgNames({"LFIND", "LFINDRAND"},
                {"exprAnyFindValueDesc", "exprListDesc", "exprFindStartPositionDesc"});
    setArgNames({"STR", "LIST", "LIST_E", "IS_STR", "IS_NUMBER", "IS_LIST", "EVAL", "MAKELIST"},
                {"exprAnyValueDesc"});
    setArgNames({"PARSEFLOAT", "PARSEINT", "LCOUNT"},
                {"exprAnyValueDesc", "exprAnyValueDesc"});
    setArgNames({"FOR"}, {"exprAnyValueDesc", "exprAnyValueDesc", "exprAnyValueDesc"});
    setArgNames({"AND_D", "OR_D"}, {"exprBooleanDesc"});
    setArgNames({"IF"}, {"exprBooleanDesc", "exprIfTrueDesc", "exprIfFalseDesc"});
    setArgNames({"CARDNAME", "CARDTYPE", "CARDRARITY", "CARDPRICE", "CARDLEVEL", "CARDCOUNT",
                 "CARDMAXUSELIMIT"}, {"exprCardInfoDesc"});
    setArgNames({"FINDKEYCODE"},
                {"exprCardInfoDesc", "exprFindPatternDesc", "exprFindStartPositionDesc"});
    setArgNames({"GETFLAGVALUEOFCARD"}, {"exprCardInfoDesc", "exprFlagDesc"});
    setArgNames({"KEYCODETEXT"}, {"exprCardInfoDesc", "exprKeyCodeNumberDesc"});
    setArgNames({"GETSTEPVALUEOFCARD"}, {"exprCardInfoDesc", "exprStepDesc"});
    setArgNames({"HAS_VARIANTVALUE", "HAS_STEP", "HAS_FLAG"},
                {"exprCardInfoDesc", "exprStringDesc"});
    setArgNames({"SETVARIANTVALUEOFCARD"},
                {"exprCardInfoDesc", "exprStringDesc", "exprAnyValueDesc"});
    setArgNames({"SETFLAGVALUEOFCARD"},
                {"exprCardInfoDesc", "exprStringDesc", "exprFlagValueDesc"});
    setArgNames({"SETSTEPVALUEOFCARD"},
                {"exprCardInfoDesc", "exprStringDesc", "exprStepValueDesc"});
    setArgNames({"GETVARIANTVALUEOFCARD"}, {"exprCardInfoDesc", "exprVariantDesc"});
    setArgNames({"CHAREXISTS", "CASTTYPE", "CASTNAME", "CASTLEVEL", "LIFERATIO"},
                {"exprCastNumberDesc"});
    setArgNames({"STATUSVALUE", "STATUSROUND"},
                {"exprCastNumberDesc", "exprCastStatusDesc"});
    setArgNames({"COUPONVALUE"}, {"exprCastNumberDesc", "exprCouponNameDesc"});
    setArgNames({"COUPONTEXT"}, {"exprCastNumberDesc", "exprCouponNumberDesc"});
    setArgNames({"FINDCOUPON"},
                {"exprCastNumberDesc", "exprFindPatternDesc", "exprFindStartPositionDesc"});
    setArgNames({"DICE"}, {"exprDiceTimesDesc", "exprDiceSidesDesc"});
    setArgNames({"FINDGOSSIP"}, {"exprFindPatternDesc", "exprFindStartPositionDesc"});
    setArgNames({"FIND"},
                {"exprFindStringDesc", "exprTargetStringDesc", "exprFindStartPositionDesc"});
    setArgNames({"FLAGVALUE"}, {"exprFlagDesc"});
    setArgNames({"FLAGTEXT"}, {"exprFlagDesc", "exprFlagValueDesc"});
    setArgNames({"GOSSIPTEXT"}, {"exprGossipNumberDesc"});
    setArgNames({"LLEN", "LGETRANDOM", "LUNIQUE"}, {"exprListDesc"});
    setArgNames({"SUM"}, {"exprListDesc", "exprConvertFlagDesc"});
    setArgNames({"INPOLYGON", "PICKBYINDICES", "CONTAIN"},
                {"exprListDesc", "exprListDesc"});
    setArgNames({"LLEFT", "LRIGHT"}, {"exprListDesc", "exprListLengthDesc"});
    setArgNames({"AT"}, {"exprListDesc", "exprListPositionDesc"});
    setArgNames({"LMID"}, {"exprListDesc", "exprListPositionDesc", "exprListLengthDesc"});
    setArgNames({"CONCATSTRINGS"}, {"exprListDesc", "exprStringDesc"});
    setArgNames({"SIND", "COSD", "TAND", "FLOOR", "CEIL"}, {"exprNumberDesc"});
    setArgNames({"GETRANDOMLIST"}, {"exprNumberDesc", "exprNumberDesc", "exprListLengthDesc"});
    setArgNames({"DISTANCE"},
                {"exprNumberDesc", "exprNumberDesc", "exprNumberDesc", "exprNumberDesc"});
    setArgNames({"STEPVALUE", "STEPMAX"}, {"exprStepDesc"});
    setArgNames({"STEPTEXT"}, {"exprStepDesc", "exprStepValueDesc"});
    setArgNames({"LEN"}, {"exprStringDesc"});
    setArgNames({"FORMAT", "LREMOVEALL"}, {"exprStringDesc", "exprAnyValueDesc"});
    setArgNames({"LREMOVEMULTI"}, {"exprStringDesc", "exprListDesc"});
    setArgNames({"LSETMULTI"}, {"exprStringDesc", "exprListDesc", "exprListDesc"});
    setArgNames({"LREMOVE"}, {"exprStringDesc", "exprListPositionDesc"});
    setArgNames({"LINSERT", "LSET"},
                {"exprStringDesc", "exprListPositionDesc", "exprAnyValueDesc"});
    setArgNames({"LEFT", "RIGHT"}, {"exprStringDesc", "exprStringLengthDesc"});
    setArgNames({"MID"}, {"exprStringDesc", "exprStringPositionDesc", "exprStringLengthDesc"});
    setArgNames({"SWITCH"}, {"exprSwitchIndexDesc", "exprAnyValueDesc", "exprListDesc"});
    setArgNames({"VALUE", "INT"}, {"exprValueArgDesc"});
    setArgNames({"MAX", "MIN"}, {"exprVariableLengthNumberDesc"});
    setArgNames({"VAR"}, {"exprVariantDesc"});
    return result;
}

void Expression::changed() {
    if (changed_) changed_();
}

void Expression::removeTextUseCounter() {
    // ネイティブの所有元を使い、コールバックをまたいでDの各配列引数を保持する。
    const auto detach = [](const auto& users) {
        const auto retainedUsers = users;
        for (const auto& user : retainedUsers) user->removeUseCounter();
    };
    detach(flags_);
    detach(steps_);
    detach(variants_);
    detach(coupons_);
    detach(gossips_);
    detach(keyCodes_);
}

void Expression::rebuildUsers() {
    const auto registerFunction = [&](const std::shared_ptr<const Function>& function) {
        const auto name = lower(function->funcName);
        auto addVariant = [&](const String& value) {
            if (value.empty()) return;
            auto user = std::make_shared<usecounter::VariantUser>(user_owner_);
            if (uc_) user->setUseCounter(uc_);
            user->variant(value);
            variants_.push_back(std::move(user));
        };
        auto addFlag = [&](const String& value) {
            if (value.empty()) return;
            auto user = std::make_shared<usecounter::FlagUser>(user_owner_);
            if (uc_) user->setUseCounter(uc_);
            user->flag(value);
            flags_.push_back(std::move(user));
        };
        auto addStep = [&](const String& value) {
            if (value.empty()) return;
            auto user = std::make_shared<usecounter::StepUser>(user_owner_);
            if (uc_) user->setUseCounter(uc_);
            user->step(value);
            steps_.push_back(std::move(user));
        };
        auto addCoupon = [&](const String& value) {
            if (value.empty()) return;
            auto user = std::make_shared<usecounter::CouponUser>(user_owner_, true);
            if (uc_) user->setUseCounter(uc_, ucOwner_);
            user->coupon(value);
            coupons_.push_back(std::move(user));
        };
        auto addGossip = [&](const String& value) {
            if (value.empty()) return;
            auto user = std::make_shared<usecounter::GossipUser>(user_owner_, true);
            if (uc_) user->setUseCounter(uc_, ucOwner_);
            user->gossip(value);
            gossips_.push_back(std::move(user));
        };
        auto addKeyCode = [&](const String& value) {
            if (value.empty()) return;
            auto user = std::make_shared<usecounter::KeyCodeUser>(user_owner_);
            if (uc_) user->setUseCounter(uc_, ucOwner_);
            user->keyCode(value);
            keyCodes_.push_back(std::move(user));
        };
        if (name == "findcoupon" && function->args.size() >= 2) {
            if (const auto* value = asString(function->args[1])) addCoupon(value->strVal);
        } else if (!function->args.empty()) {
            const auto* value = asString(function->args[0]);
            if (value) {
                if (name == "var") addVariant(value->strVal);
                else if (name == "flagvalue" || name == "flagtext") addFlag(value->strVal);
                else if (name == "stepvalue" || name == "steptext" || name == "stepmax") addStep(value->strVal);
                else if (name == "findgossip") addGossip(value->strVal);
                else if (name == "findkeycode") addKeyCode(value->strVal);
            }
        }
    };
    std::function<void(ConstPartView)> recurse = [&](ConstPartView values) {
        for (const auto& value : values) {
            if (const auto expression = std::dynamic_pointer_cast<const Expr>(value)) {
                recurse(expression->parts);
            } else if (const auto function = std::dynamic_pointer_cast<const Function>(value)) {
                recurse(function->args);
                registerFunction(function);
            } else if (const auto operation = std::dynamic_pointer_cast<const Operator>(value)) {
                // Dは平坦な後置記法の配列に、演算子より先に被演算子を格納する。ネイテ
                // ィブASTでは入れ子になっているため、たどってDの順序を復元する。
                if (operation->lhs) recurse(ConstPartList{operation->lhs});
                if (operation->rhs) recurse(ConstPartList{operation->rhs});
            } else if (const auto unary = std::dynamic_pointer_cast<const UnaryOperator>(value)) {
                if (unary->operand) recurse(ConstPartList{unary->operand});
            }
        }
    };
    recurse(expr_);
}

void Expression::text(std::string_view value) {
    String normalized(value);
    String crlf;
    crlf.reserve(normalized.size());
    for (std::size_t i = 0; i < normalized.size(); ++i) {
        if (normalized[i] == '\r') {
            if (i + 1 < normalized.size() && normalized[i + 1] == '\n') ++i;
            crlf.push_back('\n');
        } else {
            crlf.push_back(normalized[i]);
        }
    }
    if (text_ == crlf) return;
    changed();
    removeTextUseCounter();
    text_ = std::move(crlf);
    flags_.clear();
    steps_.clear();
    variants_.clear();
    coupons_.clear();
    gossips_.clear();
    keyCodes_.clear();
    ExprErrors errors;
    expr_ = parseExpression(nullptr, text_, errors);
    rebuildUsers();
    // UCContはDの切り離した集合の置換規則を適用する際、このネイテ
    // ィブのリビジョンを使う。Expressionの再解析直後の弱参照監
    // 視先と、コールバックが作った無関係な接続先の登録を区別するため。
    ++use_counter_revision_;
}

ExprErrors Expression::getExpressionErrors(const CProps* prop,
                                            const VariableInfo& info) const {
    ExprErrors errors;
    const auto parsed = parseExpression(prop, text_, errors);
    if (errors.empty() && !parsed.empty()) (void)calculate(prop, EvalMode::TypeCheck, info, parsed, errors);
    return errors;
}

VariantVal Expression::returnValue(const CProps* prop) const {
    ExprErrors errors;
    VariableInfo info;
    // DのExpression.returnValueは、TypeCheckに空のcastNameデリゲートを渡す。
    info.castName = [](std::uint32_t) { return String{}; };
    info.isTargetWsnVersion = [prop](const String& requiredVersion) {
        if (!prop) return true;
        return prop->isTargetVersion(summary::Summary::Owner{}, "1.50", requiredVersion);
    };
    const auto value = calculate(prop, EvalMode::TypeCheck, info, expr_, errors);
    return partToVariantVal(value);
}

lib::Array<String> Expression::flagsInText() const {
    lib::Array<String> result;
    for (const auto& user : flags_) if (user) result.push_back(user->flag());
    return result;
}
lib::Array<String> Expression::stepsInText() const {
    lib::Array<String> result;
    for (const auto& user : steps_) if (user) result.push_back(user->step());
    return result;
}
lib::Array<String> Expression::variantsInText() const {
    lib::Array<String> result;
    for (const auto& user : variants_) if (user) result.push_back(user->variant());
    return result;
}
lib::Array<String> Expression::couponsInText() const {
    lib::Array<String> result;
    for (const auto& user : coupons_) if (user) result.push_back(user->coupon());
    return result;
}
lib::Array<String> Expression::gossipsInText() const {
    lib::Array<String> result;
    for (const auto& user : gossips_) if (user) result.push_back(user->gossip());
    return result;
}
lib::Array<String> Expression::keyCodesInText() const {
    lib::Array<String> result;
    for (const auto& user : keyCodes_) if (user) result.push_back(user->keyCode());
    return result;
}

void Expression::setUseCounter(const usecounter::CounterOwner& counter,
                               const lib::Owner<CwxPath>& owner) {
    ++use_counter_revision_;
    ++use_counter_binding_revision_;
    // Dはカウンター、所有元、各配列記述子を値で渡す。コールバック中はネ
    // イティブの所有元を保持し、nullの所有元をthisで代用しない。
    const auto desiredCounter = counter;
    const auto desiredOwner = owner;
    const auto apply = [](const auto& users, const auto& attach) {
        const auto retainedUsers = users;
        for (const auto& user : retainedUsers) attach(user);
    };
    const auto attach = [&](const auto& user) { user->setUseCounter(desiredCounter); };
    const auto attachOwned = [&](const auto& user) { user->setUseCounter(desiredCounter, desiredOwner); };
    apply(flags_, attach);
    apply(steps_, attach);
    apply(variants_, attach);
    apply(coupons_, attachOwned);
    apply(gossips_, attachOwned);
    apply(keyCodes_, attachOwned);
    uc_ = desiredCounter;
    ucOwner_ = desiredOwner;
}

void Expression::removeUseCounter() {
    ++use_counter_revision_;
    ++use_counter_binding_revision_;
    removeTextUseCounter();
    uc_.reset();
    ucOwner_.reset();
}

void Expression::changeInText(std::size_t index, std::string_view id) {
    // 汎用のISimpleTextHolderへの配送は、式のテキ
    // スト内で唯一型指定のない識別子であるコモン参照として扱う。
    changeVariantInText(index, id);
}
void Expression::changeFlagInText(std::size_t index, std::string_view id) {
    if (index < flags_.size() && flags_[index]) (void)flags_[index]->change(usecounter::FlagId(id));
}
void Expression::changeStepInText(std::size_t index, std::string_view id) {
    if (index < steps_.size() && steps_[index]) (void)steps_[index]->change(usecounter::StepId(id));
}
void Expression::changeVariantInText(std::size_t index, std::string_view id) {
    if (index < variants_.size() && variants_[index]) (void)variants_[index]->change(usecounter::VariantId(id));
}
void Expression::changePathInText(std::size_t, std::string_view) {}
void Expression::changeInText(std::size_t index, const usecounter::FlagId& id) { changeFlagInText(index, id.id); }
void Expression::changeInText(std::size_t index, const usecounter::StepId& id) { changeStepInText(index, id.id); }
void Expression::changeInText(std::size_t index, const usecounter::VariantId& id) { changeVariantInText(index, id.id); }
void Expression::changeInText(std::size_t index, const usecounter::CouponId& id) {
    if (index < coupons_.size() && coupons_[index]) (void)coupons_[index]->change(usecounter::CouponId(id.id));
}
void Expression::changeInText(std::size_t index, const usecounter::GossipId& id) {
    if (index < gossips_.size() && gossips_[index]) (void)gossips_[index]->change(usecounter::GossipId(id.id));
}

template <typename Id>
bool Expression::replaceInExpression(const Id& oldValue, const Id& newValue) {
    const String oldText = idString(oldValue);
    const String newText = idString(newValue);
    std::vector<std::pair<std::size_t, String>> replacements;
    const auto addReplacement = [&](const ConstPartPtr& part) {
        if (const auto* value = asString(part); value && value->strVal == oldText) {
            replacements.emplace_back(value->token.index, value->token.token);
        }
    };
    std::function<void(ConstPartView)> recurse = [&](ConstPartView values) {
        for (const auto& value : values) {
            if (const auto function = std::dynamic_pointer_cast<const Function>(value)) {
                const auto name = lower(function->funcName);
                if constexpr (std::is_same_v<Id, usecounter::CouponId>) {
                    if (name == "findcoupon" && function->args.size() >= 2) addReplacement(function->args[1]);
                } else {
                    if (!function->args.empty() &&
                        ((std::is_same_v<Id, usecounter::VariantId> && name == "var") ||
                         (std::is_same_v<Id, usecounter::FlagId> && (name == "flagvalue" || name == "flagtext")) ||
                         (std::is_same_v<Id, usecounter::StepId> && (name == "stepvalue" || name == "steptext" || name == "stepmax")) ||
                         (std::is_same_v<Id, usecounter::GossipId> && name == "findgossip"))) {
                        addReplacement(function->args[0]);
                    }
                }
                recurse(function->args);
            } else if (const auto expression = std::dynamic_pointer_cast<const Expr>(value)) {
                recurse(expression->parts);
            } else if (const auto operation = std::dynamic_pointer_cast<const Operator>(value)) {
                if (operation->lhs) recurse(ConstPartList{operation->lhs});
                if (operation->rhs) recurse(ConstPartList{operation->rhs});
            } else if (const auto unary = std::dynamic_pointer_cast<const UnaryOperator>(value)) {
                if (unary->operand) recurse(ConstPartList{unary->operand});
            }
        }
    };
    recurse(expr_);
    if (replacements.empty()) return false;
    std::sort(replacements.begin(), replacements.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });
    String updated = text_;
    for (const auto& [index, token] : replacements) {
        if (index + token.size() > updated.size()) continue;
        updated.replace(index, token.size(), quote(newText));
    }
    text(std::move(updated));
    return true;
}

bool Expression::changeCallback(const usecounter::FlagId& oldValue,
                                const usecounter::FlagId& newValue) {
    (void)replaceInExpression(oldValue, newValue);
    changed();
    return true;
}
bool Expression::changeCallback(const usecounter::StepId& oldValue,
                                const usecounter::StepId& newValue) {
    (void)replaceInExpression(oldValue, newValue);
    changed();
    return true;
}
bool Expression::changeCallback(const usecounter::VariantId& oldValue,
                                const usecounter::VariantId& newValue) {
    (void)replaceInExpression(oldValue, newValue);
    changed();
    return true;
}
bool Expression::changeCallback(const usecounter::CouponId& oldValue,
                                const usecounter::CouponId& newValue) {
    (void)replaceInExpression(oldValue, newValue);
    changed();
    return true;
}
bool Expression::changeCallback(const usecounter::GossipId& oldValue,
                                const usecounter::GossipId& newValue) {
    (void)replaceInExpression(oldValue, newValue);
    changed();
    return true;
}

String Expression::cwxPath(bool id) {
    const auto parent = owner();
    if (!parent) return {};
    return path::cpjoin(parent, "expr", id);
}

lib::Owner<CwxPath> Expression::findCWXPath(std::string_view value) {
    if (path::cpempty(value)) return usecounter::detail::nonOwning<CwxPath>(this);
    return {};
}

lib::Observer<CwxPath> Expression::cwxParent() {
    return owner();
}

ExprException::ExprException(String file, std::size_t line, String expression,
                             ExprErrors errors)
    : std::runtime_error([&] {
          std::ostringstream text;
          text << "expression error: " << expression << ", [";
          for (std::size_t i = 0; i < errors.size(); ++i) {
              if (i) text << ", ";
              const auto& error = errors[i];
              // Dの整形処理は、const修飾された構造体の型名を公開する。
              text << "const(ExprError)(" << std::quoted(error.message) << ", " << error.errLine
                   << ", " << error.errPos << ", " << std::quoted(error.file) << ", " << error.line << ')';
          }
          return text.str() + ']';
      }()),
      file_(std::move(file)), line_(line), expression_(std::move(expression)),
      errors_(std::move(errors)) {}

Part::Part(Token value) : token(std::move(value)) {}
ConstPartPtr Part::toValueObj(const CProps*, ExprErrors&) const {
    // DのクラスはGCが所有し、thisを返して動的な型と同一性を
    // 保持する。ネイティブのconst所有元も同じオブジェクトを公
    // 開し、この読み取り専用の変換経路から可変ハンドルを作らない。
    const auto retained = weak_from_this().lock();
    if (!retained) throw std::logic_error("expression part requires shared ownership");
    return retained;
}
String Part::stringValue() const { throw std::runtime_error("No supported."); }
String Part::toString() const { return "Part"; }

Expr::Expr(Token value, PartList values)
    : Part(std::move(value)), parts(std::move(values)) {}
Expr::Expr(Token value, PartSlice values)
    : Part(std::move(value)), parts(std::move(values)) {}
Expr::Expr(Token value, ConstPartView values)
    : Part(std::move(value)), parts(std::move(values)) {}
Expr::Expr(Token value, ConstPartList values)
    : Part(std::move(value)), parts(std::move(values)) {}
String Expr::toString() const {
    String result{"["};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) result += ", ";
        result += requiredPart(parts[i]).toString();
    }
    result += ']';
    return result;
}

NumberValue::NumberValue(Token value, double numberValue)
    : Part(std::move(value)), numVal(numberValue) {}
String NumberValue::stringValue() const { return numberText(numVal); }
String NumberValue::toString() const { return stringValue(); }

StringValue::StringValue(Token value, String stringValueValue)
    : Part(std::move(value)), strVal(std::move(stringValueValue)) {}
String StringValue::stringValue() const { return strVal; }
String StringValue::toString() const { return quote(strVal); }

BooleanValue::BooleanValue(Token value, bool booleanValue)
    : Part(std::move(value)), boolVal(booleanValue) {}
String BooleanValue::stringValue() const { return boolVal ? "TRUE" : "FALSE"; }
String BooleanValue::toString() const { return stringValue(); }

ListValue::ListValue(Token value, PartList values)
    : Part(std::move(value)), listVal(std::move(values)) {}
ListValue::ListValue(Token value, PartSlice values)
    : Part(std::move(value)), listVal(std::move(values)) {}
ListValue::ListValue(Token value, ConstPartView values)
    : Part(std::move(value)), listVal(std::move(values)) {}
ListValue::ListValue(Token value, ConstPartList values)
    : Part(std::move(value)), listVal(std::move(values)) {}
String ListValue::stringValue() const {
    String result{"LIST("};
    for (std::size_t i = 0; i < listVal.size(); ++i) {
        if (i) result += ", ";
        result += requiredPart(listVal[i]).toString();
    }
    result += ')';
    return result;
}
String ListValue::toString() const { return stringValue(); }

UnknownValue::UnknownValue(Token value) : Part(std::move(value)) {}
String UnknownValue::stringValue() const { return {}; }
String UnknownValue::toString() const { return "Unknown"; }

namespace {
ConstPartView completeStructureMembers(const Token& token, StringView name,
                                       ConstPartView values) {
    const auto& info = structureInfo(name);
    if (values.size() < info.requiredMemberNum || values.size() > info.members.size()) {
        // Dは必須の個数を確認してから、残りの定義をスライスする。
        throw std::invalid_argument("structure value member count out of range");
    }
    if (values.size() == info.members.size()) return values;

    // 構造体の既定値は記述子を拡張する。入力に追加可能な可変
    // 項目がない場合は、const所有元の領域を確保する。
    ConstPartList completed;
    completed.reserve(info.members.size());
    for (std::size_t index = 0; index < values.size(); ++index)
        completed.push_back(values[index]);
    while (completed.size() < info.members.size()) {
        completed.push_back(variantValToValueObj(
            token, info.members[completed.size()].defaultValue));
    }
    return ConstPartView(std::move(completed));
}
} // namespace

StructureValue::StructureValue(Token value, String name, PartList values)
    : Part(std::move(value)), structName(lower(name)),
      structVal(completeStructureMembers(token, structName,
                                         ConstPartView(std::move(values)))) {}
StructureValue::StructureValue(Token value, String name, ConstPartList values)
    : Part(std::move(value)), structName(lower(name)),
      structVal(completeStructureMembers(token, structName,
                                         ConstPartView(std::move(values)))) {}
StructureValue::StructureValue(Token value, String name, ConstPartView values)
    : Part(std::move(value)), structName(lower(name)),
      structVal(completeStructureMembers(token, structName, std::move(values))) {}
String StructureValue::stringValue() const { return variantValueToText(partToVariantVal(std::make_shared<StructureValue>(*this))); }
String StructureValue::toString() const { return stringValue(); }

Symbol::Symbol(Token value, String stringValueValue)
    : Part(std::move(value)), symbol(std::move(stringValueValue)) {}
String Symbol::stringValue() const { return {}; }
String Symbol::toString() const { return upper(symbol); }

// DはAST表示用に元の演算子表記を保持し、評価時に正規化する。
UnaryOperator::UnaryOperator(Token value, String op)
    : Part(std::move(value)), operator_(std::move(op)) {}
String UnaryOperator::toString() const { return "UOp(" + operator_ + ", " + std::to_string(token.line) + ":" + std::to_string(token.pos) + ")"; }

Operator::Operator(Token value, String op)
    : Part(std::move(value)), operator_(std::move(op)) {}
String Operator::toString() const { return "Op(" + operator_ + ", " + std::to_string(token.line) + ":" + std::to_string(token.pos) + ")"; }

VariantVal VariantVal::numValue(double value) {
    VariantVal result(VariantType::Number);
    result.numVal = value;
    return result;
}
VariantVal VariantVal::strValue(String value) {
    VariantVal result(VariantType::String);
    result.strVal = std::move(value);
    return result;
}
VariantVal VariantVal::boolValue(bool value) {
    VariantVal result(VariantType::Boolean);
    result.boolVal = value;
    return result;
}
VariantVal VariantVal::listValue(lib::Array<VariantVal> value) {
    VariantVal result(VariantType::List);
    result.listVal = std::move(value);
    return result;
}
VariantVal VariantVal::structValue(String name, lib::Array<VariantVal> value) {
    VariantVal result(VariantType::Structure);
    result.structName = std::move(name);
    result.structVal = std::move(value);
    return result;
}

bool operator==(const VariantVal& lhs, const VariantVal& rhs) {
    // DのVariantValは値構造体。自動生成される等価判定は、この公開された
    // 可変APIから参照できる非アクティブな内容も含め、全フィールドを比較する。
    return lhs.valid == rhs.valid && lhs.type == rhs.type &&
        lhs.numVal == rhs.numVal && lhs.strVal == rhs.strVal &&
        lhs.boolVal == rhs.boolVal && lhs.listVal == rhs.listVal &&
        lhs.structName == rhs.structName && lhs.structVal == rhs.structVal;
}

StructureInfo::StructureInfo(String valueName, String version, bool publicValue,
                             lib::SharedSlice<const StructureMember> valueMembers,
                             std::size_t required)
    : name(std::move(valueName)), dataVersion(std::move(version)),
      isPublic(publicValue), members(std::move(valueMembers)),
      requiredMemberNum(required) {
    // DのGCが保持するメンバースライスと同様に、入力項目を保持する。
    // 検索表は構築時の状態を維持し、同じキーは最後の定義を優先する。
    if (requiredMemberNum > members.size()) {
        throw std::invalid_argument("required structure members exceed member count");
    }
    for (std::size_t i = 0; i < members.size(); ++i) {
        indexTable_[members[i].name] = i;
    }
}
std::ptrdiff_t StructureInfo::indexOf(StringView memberName) const {
    const auto found = indexTable_.find(String(memberName));
    return found == indexTable_.end() ? -1 : static_cast<std::ptrdiff_t>(found->second);
}

VariableInfo::VariableInfo(const CProps* prop, const summary::Summary* summ,
                           const usecounter::CounterOwner& uc,
                           String targetVersion, const msgutils::NameMap& names,
                           const VarGetter& flags, const VarGetter& steps,
                           const VarGetter& variants, const VarGetter& sysSteps,
                           CastNameGetter castNameGetter,
                           CardNameGetter cardNameGetter,
                           TextGetter partyNameGetter, TextGetter yadoNameGetter,
                           TextGetter scenarioNameGetter, MouseGetter mouseXGetter,
                           MouseGetter mouseYGetter,
                           CardVariantGetter variantCardGetter,
                           CardFlagGetter flagCardGetter,
                           CardStepGetter stepCardGetter)
    : VariableInfo() {
    // DのVariableInfoの完全なコンストラクターは、選択、クーポン、ゴシップ、キー
    // コードに何もしない検索処理を設定する（expression.d:2026-2033）
    // 。ここでも呼び出し可能にし、既定構築のVariableInfoは未設定のままにする。
    selectedPlayerCardNumber = [] { return std::uint32_t{0}; };
    findCoupon = [](std::uint32_t, const String&, std::uint32_t) {
        return std::uint32_t{0};
    };
    couponText = [](std::uint32_t, std::uint32_t) { return String{}; };
    findGossip = [](const String&, std::uint32_t) { return std::uint32_t{0}; };
    gossipText = [](std::uint32_t) { return String{}; };
    findKeyCode = [](const StructureValue&, const String&, std::uint32_t) {
        return std::uint32_t{0};
    };
    KeyCodeText = [](const StructureValue&, std::uint32_t) { return String{}; };

    // DのクロージャーはSummary/UseCounterと連想配列のハンドルを保持する。共有されて
    // いないネイティブのSummaryとCPropsの入力は、呼び出し側が所有する借用のままとする。
    auto scenario = summ ? std::const_pointer_cast<summary::Summary>(summ->weak_from_this().lock())
                         : lib::Owner<summary::Summary>{};
    if (summ && !scenario)
        scenario = usecounter::detail::nonOwning(const_cast<summary::Summary*>(summ));
    const auto counter = uc;
    const auto sharedNames = names.sharedStorage();
    const String target = std::move(targetVersion);
    const auto currentRoot = [scenario] {
        return scenario ? scenario->flagDirRoot() : flag::FlagDirPtr{};
    };
    isTargetWsnVersion = [prop, scenario, target](const String& version) {
        return prop ? prop->isTargetVersion(scenario, target, version) : true;
    };
    // Dの展開処理にはnullのプロパティへの代替がない。変更可能な設定をこ
    // こで固定せず、コールバックの実行時に接頭辞とバージョン情報を取得する。
    const auto expansionVersion = [prop, scenario, target](const String& version) {
        if (!prop) throw std::invalid_argument("text expansion requires editor properties");
        return prop->isTargetVersion(scenario, target, version);
    };
    existsFlag = [currentRoot, counter](const String& path) {
        return static_cast<bool>(flag::findVar<flag::Flag>(currentRoot(), counter, path));
    };
    existsStep = [currentRoot, counter](const String& path) {
        return static_cast<bool>(flag::findVar<flag::Step>(currentRoot(), counter, path));
    };
    existsVariant = [currentRoot, counter](const String& path) {
        return static_cast<bool>(flag::findVar<flag::Variant>(currentRoot(), counter, path));
    };
    variantValue = [currentRoot, counter, scenario](const String& path) {
        if (!scenario) return VariantVal::invalidValue();
        const auto value = flag::findVar<flag::Variant>(currentRoot(), counter, path);
        if (!value) throw std::invalid_argument("expression variant does not exist");
        return VariantVal(*value);
    };
    flagValue = [currentRoot, counter](const String& path) {
        const auto value = flag::findVar<flag::Flag>(currentRoot(), counter, path);
        return value ? value->onOff() : false;
    };
    stepValue = [currentRoot, counter](const String& path) {
        const auto value = flag::findVar<flag::Step>(currentRoot(), counter, path);
        return value ? value->select() : std::uint32_t{0};
    };
    flagText = [currentRoot, counter, scenario, prop, flags, steps, variants, sysSteps,
                sharedNames, expansionVersion](const String& path, bool value) {
        if (!scenario) return String{};
        const auto variable = flag::findVar<flag::Flag>(currentRoot(), counter, path);
        if (!variable) throw std::invalid_argument("expression flag does not exist");
        auto text = value ? variable->on() : variable->off();
        if (variable->expandSPChars()) {
            if (!prop) throw std::invalid_argument("text expansion requires editor properties");
            const String prefix = prop->sys().prefixSystemVarName();
            const auto nameView = msgutils::NameMap::fromSharedStorage(sharedNames);
            text = msgutils::simpleFormatMsg(text, flags, steps, variants, sysSteps,
                                             nameView, expansionVersion, prefix);
        }
        return text;
    };
    stepText = [currentRoot, counter, scenario, prop, flags, steps, variants, sysSteps,
                sharedNames, expansionVersion](const String& path, std::uint32_t value) {
        if (!scenario) return String{};
        const auto variable = flag::findVar<flag::Step>(currentRoot(), counter, path);
        if (!variable) throw std::invalid_argument("expression step does not exist");
        auto text = variable->getValue(value);
        if (variable->expandSPChars()) {
            if (!prop) throw std::invalid_argument("text expansion requires editor properties");
            const String prefix = prop->sys().prefixSystemVarName();
            const auto nameView = msgutils::NameMap::fromSharedStorage(sharedNames);
            text = msgutils::simpleFormatMsg(text, flags, steps, variants, sysSteps,
                                             nameView, expansionVersion, prefix);
        }
        return text;
    };
    stepMax = [currentRoot, counter](const String& path) {
        const auto value = flag::findVar<flag::Step>(currentRoot(), counter, path);
        return value ? value->count() : std::uint32_t{0};
    };
    castName = std::move(castNameGetter);
    cardName = std::move(cardNameGetter);
    partyName = std::move(partyNameGetter);
    yadoName = std::move(yadoNameGetter);
    scenarioName = std::move(scenarioNameGetter);
    mouseX = std::move(mouseXGetter);
    mouseY = std::move(mouseYGetter);
    variantValueOfCard = std::move(variantCardGetter);
    flagValueOfCard = std::move(flagCardGetter);
    stepValueOfCard = std::move(stepCardGetter);
}

VariableInfo::VariableInfo(const VariableInfo& base, ExistsGetter variantExists,
                           VariantGetter variantLookup)
    : VariableInfo(base) {
    existsVariant = std::move(variantExists);
    variantValue = std::move(variantLookup);
}

namespace {
[[nodiscard]] ConstPartPtr dispatchFunction(const CProps* prop, EvalMode mode,
                                            const VariableInfo& info,
                                            const Function& func, ConstPartView args,
                                            ExprErrors& errors, StringView functionName);
}

Function::Function(Token value, String name, PartList arguments)
    : Part(std::move(value)), funcName(std::move(name)), args(std::move(arguments)) {}
Function::Function(Token value, String name, PartSlice arguments)
    : Part(std::move(value)), funcName(std::move(name)), args(std::move(arguments)) {}
Function::Function(Token value, String name, ConstPartView arguments)
    : Part(std::move(value)), funcName(std::move(name)), args(std::move(arguments)) {}
Function::Function(Token value, String name, ConstPartList arguments)
    : Part(std::move(value)), funcName(std::move(name)), args(std::move(arguments)) {}
String Function::toString() const {
    String result = funcName + "(";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) result += ", ";
        result += requiredPart(args[i]).toString();
    }
    result += ')';
    return result;
}

const StructureInfo& structureInfo(StringView name) {
    if (lower(name) == "cardinfo") return cardInfo();
    throw std::invalid_argument("Structure has not been declared: " + String(name));
}

lib::SharedSlice<const StructureMember> structureMembers(StringView name) {
    return structureInfo(name).members;
}

namespace {
template <typename T>
lib::SharedSlice<T> cutOptionalMembersImpl(StringView name,
                                           lib::SharedSlice<T> value) {
    const auto& info = structureInfo(name);
    if (value.size() != info.members.size()) {
        throw std::invalid_argument("optional structure member count out of range");
    }
    // このネイティブの値所有アダプターは右辺値の保存領域を生存させるが
    // 、Dの書き込み可能な入力項目との共有関係にはまだ対応していない。
    while (value.size() > info.requiredMemberNum) {
        const std::size_t index = value.size() - 1;
        if (value[index] != info.members[index].defaultValue) break;
        value = value.slice(0, index);
    }
    return value;
}
} // namespace

lib::SharedSlice<VariantVal> cutOptionalMembers(
    StringView name, lib::SharedSlice<VariantVal> value) {
    return cutOptionalMembersImpl(name, std::move(value));
}

lib::SharedSlice<const VariantVal> cutOptionalMembers(
    StringView name, lib::SharedSlice<const VariantVal> value) {
    return cutOptionalMembersImpl(name, std::move(value));
}

bool isValidStructure(StringView name, const lib::Array<String>& memberNames,
                      const lib::Array<VariantVal>& values) {
    if (memberNames.size() != values.size()) return false;
    const auto& info = [&]() -> const StructureInfo& {
        try { return structureInfo(name); }
        catch (...) { return cardInfo(); }
    }();
    if (lower(name) != lower(info.name) || memberNames.size() > info.members.size()) {
        return false;
    }
    for (std::size_t i = 0; i < memberNames.size(); ++i) {
        if (lower(memberNames[i]) != lower(info.members[i].name)) return false;
        const auto& member = info.members[i];
        if (!member.typeCheck) continue;
        if (!values[i].valid || values[i].type != member.type) return false;
        if (member.type == VariantType::Number &&
            !std::isnan(member.minValue) && values[i].numVal < member.minValue) return false;
        if (member.type == VariantType::Structure &&
            lower(values[i].structName) != lower(member.structName)) return false;
    }
    return info.requiredMemberNum <= memberNames.size() ||
           memberNames.size() == info.members.size();
}

PartPtr variantValToValueObj(Token token, const VariantVal& value) {
    // Dはこの前提条件をアサートし、Unknown値ではなくfinal switchを使う。
    if (!value.valid) throw std::invalid_argument("invalid expression variant value");
    switch (value.type) {
    case VariantType::Number: return number(std::move(token), value.numVal);
    case VariantType::String: return string(std::move(token), value.strVal);
    case VariantType::Boolean: return boolean(std::move(token), value.boolVal);
    case VariantType::List: {
        PartList values;
        for (const auto& item : value.listVal) values.push_back(variantValToValueObj(token, item));
        return list(std::move(token), std::move(values));
    }
    case VariantType::Structure: {
        PartList values;
        for (const auto& item : value.structVal) values.push_back(variantValToValueObj(token, item));
        return std::make_shared<StructureValue>(std::move(token), value.structName,
                                                std::move(values));
    }
    }
    throw std::invalid_argument("unknown expression variant type");
}

VariantVal partToVariantVal(ConstPartPtr value) {
    if (!value) return VariantVal::invalidValue();
    if (const auto* v = asNumber(value)) return VariantVal::numValue(v->numVal);
    if (const auto* v = asString(value)) return VariantVal::strValue(v->strVal);
    if (const auto* v = asBoolean(value)) return VariantVal::boolValue(v->boolVal);
    if (const auto* v = asList(value)) {
        lib::Array<VariantVal> values;
        for (const auto& item : v->listVal) values.push_back(partToVariantVal(item));
        return VariantVal::listValue(std::move(values));
    }
    if (const auto* v = asStructure(value)) {
        lib::Array<VariantVal> values;
        for (const auto& item : v->structVal) values.push_back(partToVariantVal(item));
        return VariantVal::structValue(v->structName, std::move(values));
    }
    return VariantVal::invalidValue();
}

ConstPartPtr Symbol::toValueObj(const CProps* prop, ExprErrors& errors) const {
    static const std::unordered_map<String, VariantVal> symbols{
        {"newline", VariantVal::strValue("\n")},
        {"player", VariantVal::numValue(1)}, {"enemy", VariantVal::numValue(2)},
        {"friend", VariantVal::numValue(3)}, {"skill", VariantVal::numValue(1)},
        {"item", VariantVal::numValue(2)}, {"beast", VariantVal::numValue(3)},
        {"actioncard", VariantVal::numValue(-1)}, {"rare", VariantVal::numValue(1)},
        {"premier", VariantVal::numValue(2)}, {"poison", VariantVal::numValue(8)},
        {"sleep", VariantVal::numValue(9)}, {"bind", VariantVal::numValue(10)},
        {"paralyze", VariantVal::numValue(11)}, {"confuse", VariantVal::numValue(12)},
        {"overheat", VariantVal::numValue(13)}, {"brave", VariantVal::numValue(14)},
        {"panic", VariantVal::numValue(15)}, {"silence", VariantVal::numValue(16)},
        {"faceup", VariantVal::numValue(17)}, {"antimagic", VariantVal::numValue(18)},
        {"enhaction", VariantVal::numValue(19)}, {"enhavoid", VariantVal::numValue(20)},
        {"enhresist", VariantVal::numValue(21)}, {"enhdefense", VariantVal::numValue(22)},
    };
    const auto found = symbols.find(lower(symbol));
    if (found == symbols.end()) {
        addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorUnknownSymbol, "Unknown symbol: %1$s", symbol), *this);
        return number(token, 0);
    }
    return variantValToValueObj(token, found->second);
}

ConstPartPtr UnaryOperator::call(const CProps* prop, ConstPartPtr rhs,
                                 ExprErrors& errors) const {
    reportRawSymbol(prop, rhs, errors);
    const auto op = lower(operator_);
    if (op == "not") {
        if (const auto* value = asBoolean(rhs)) return boolean(token, !value->boolVal);
        operatorTypeError(prop, rhs, &msgs::Msgs::expressionErrorValueIsNotBoolean,
                          "Value is not boolean: %1$s", errors);
        return boolean(token, false);
    }
    // Dのfinal switchは、この時点で未知の演算子を拒否する。
    if (op != "+" && op != "-") throw std::invalid_argument("unknown unary expression operator");
    if (const auto* value = asNumber(rhs))
        return number(token, op == "-" ? -value->numVal : value->numVal);
    operatorTypeError(prop, rhs, &msgs::Msgs::expressionErrorValueIsNotNumber,
                      "Value is not number: %1$s", errors);
    return number(token, 0);
}

ConstPartPtr Operator::call(const CProps* prop, EvalMode mode,
                            ConstPartPtr lhsValue, ConstPartPtr rhsValue,
                            ExprErrors& errors) const {
    return call(prop, mode, VariableInfo{}, std::move(lhsValue),
                std::move(rhsValue), errors);
}

ConstPartPtr Operator::call(const CProps* prop, EvalMode mode,
                            const VariableInfo& info, ConstPartPtr lhsValue,
                            ConstPartPtr rhsValue, ExprErrors& errors) const {
    (void)info; // ネイティブ側で追加した評価用の状態。Dは評価済みのPartを扱う。Dは実際に
    // 使うまでnullの被演算子を保持するため、Unknownを作って代用しない。
    const ConstPartPtr leftValue = std::move(lhsValue);
    const ConstPartPtr rightValue = std::move(rhsValue);
    reportRawSymbol(prop, leftValue, errors);
    if (operator_ != ".") reportRawSymbol(prop, rightValue, errors);
    const auto op = lower(operator_);
    if (op == ".") {
        const auto* structure = structureOperand(prop, leftValue, errors);
        if (!structure) return unknown(requiredPart(rightValue).token);
        const auto* member = dynamic_cast<const Symbol*>(rightValue.get());
        if (!member) {
            const auto& part = requiredPart(rightValue);
            addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorTokenIsNotStructureMemberName,
                "Token is not structure member name: %1$s", part.token.token), part);
            return unknown(part.token);
        }
        const auto& structureInfoValue = structureInfo(structure->structName);
        // Dは追加の大文字小文字変換を行わず、保存されたキーでSTRUCTSを直接参照する。
        if (structureInfoValue.name != structure->structName)
            throw std::out_of_range("structure operator requires an exact registered key");
        const auto index = structureInfoValue.indexOf(lower(member->symbol));
        if (index == -1) {
            String members;
            for (const auto& candidate : structureInfoValue.members) {
                if (!candidate.isPublic) continue;
                if (!members.empty()) members += ", ";
                members += upper(candidate.name);
            }
            const auto message = members.empty()
                ? expressionMessage(prop, &msgs::Msgs::expressionErrorStructureHasNotPublicMember,
                    "Structure %1$s has not public member.", upper(structureInfoValue.name))
                : expressionMessage(prop, members.size() == 1
                        ? &msgs::Msgs::expressionErrorSymbolIsNotStructureMemberNameOne
                        : &msgs::Msgs::expressionErrorSymbolIsNotStructureMemberName,
                    "Symbol is not structure member name: %1$s (%2$s)", rightValue->token.token, members);
            addError(errors, message, *rightValue);
            return unknown(rightValue->token);
        }
        const auto nativeIndex = static_cast<std::size_t>(index);
        const auto& memberInfo = structureInfoValue.members.at(nativeIndex);
        if (!memberInfo.isPublic && mode != EvalMode::All) {
            addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorPermissionOfStructureMember,
                "Structure member %1$s.%2$s is not accessible.",
                upper(structureInfoValue.name), upper(memberInfo.name)), *this);
        }
        return structure->structVal.at(nativeIndex);
    }
    if (op == "~") {
        if (const auto* leftList = asList(leftValue)) {
            ConstPartList values;
            values.reserve(leftList->listVal.size());
            for (const auto& item : leftList->listVal) values.push_back(item);
            if (const auto* rightList = asList(rightValue))
                values.insert(values.end(), rightList->listVal.begin(), rightList->listVal.end());
            else values.push_back(rightValue);
            return listConst(token, std::move(values));
        }
        if (const auto* rightList = asList(rightValue)) {
            ConstPartList values{leftValue};
            values.insert(values.end(), rightList->listVal.begin(), rightList->listVal.end());
            return listConst(token, std::move(values));
        }
        const auto leftText = requiredPart(leftValue).stringValue();
        const auto rightText = requiredPart(rightValue).stringValue();
        return string(token, leftText + rightText);
    }
    if (op == "and" || op == "or") {
        const auto values = booleanOperands(prop, leftValue, rightValue, errors);
        if (!values) return boolean(token, false);
        return boolean(token, op == "and" ? values.lhs->boolVal && values.rhs->boolVal
                                           : values.lhs->boolVal || values.rhs->boolVal);
    }
    if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
        const auto values = numberOperands(prop, leftValue, rightValue, errors);
        if (!values) return number(token, 0);
        const auto left = values.lhs->numVal;
        const auto right = values.rhs->numVal;
        if ((op == "/" || op == "%") && right == 0) {
            if (mode != EvalMode::TypeCheck) addError(errors, expressionMessage(prop,
                &msgs::Msgs::expressionErrorDivisionByZero, "Division by zero."), requiredPart(rightValue));
            return number(token, 0);
        }
        if (op == "+") return number(token, left + right);
        if (op == "-") return number(token, left - right);
        if (op == "*") return number(token, left * right);
        if (op == "/") return number(token, left / right);
        // Dの浮動小数点の%演算は、ネイティブのfmodで提供する。
        return number(token, std::fmod(left, right));
    }
    if (op == "=" || op == "<>")
        return boolean(token, operatorEqual(prop, leftValue, rightValue, errors, op == "<>"));
    // Dのfinal switchは、未登録の演算子をUnknownへ置き換えない。
    if (op != "<" && op != "<=" && op != ">" && op != ">=")
        throw std::invalid_argument("unknown binary expression operator");
    const auto comparison = compareOrdering(prop, leftValue, rightValue, errors);
    if (!comparison || *comparison == OperandOrdering::Unordered) return boolean(token, false);
    if (op == "<") return boolean(token, *comparison == OperandOrdering::Less);
    if (op == ">") return boolean(token, *comparison == OperandOrdering::Greater);
    if (op == "<=") return boolean(token, *comparison != OperandOrdering::Greater);
    return boolean(token, *comparison != OperandOrdering::Less);
}

PartList parseExpression(const CProps* prop, StringView text, ExprErrors& errors) {
    Token endToken;
    const auto tokens = tokenize(prop, text, errors, endToken);
    std::size_t index = 0;
    auto result = parseSemantics(prop, tokens, index, errors);
    if (index != tokens.size()) {
        errors.emplace_back(expressionMessage(prop, &msgs::Msgs::expressionErrorInvalidSemantics, "Invalid semantics."),
                            endToken.line, endToken.pos, __FILE__, __LINE__);
    }
    return result;
}

PartList parseSemantics(const CProps* prop, const lib::Array<Token>& tokens,
                        std::size_t& index, ExprErrors& errors) {
    struct PendingOperator {
        std::size_t parLevel;
        int opLevel;
        bool unary;
        Token token;
    };
    PartList result;
    std::vector<PendingOperator> operators;
    bool isOp = true;
    std::size_t parLevel = 0;
    int opLevel = 0;
    if (index >= tokens.size()) return result;
    const auto invalid = [&](auto member, StringView fallback, const Token& token) {
        errors.emplace_back(expressionMessage(prop, member, fallback), token.line, token.pos, __FILE__, __LINE__);
    };
    const auto emit = [&](const PendingOperator& operation) {
        if (operation.unary) result.push_back(std::make_shared<UnaryOperator>(operation.token, operation.token.token));
        else result.push_back(std::make_shared<Operator>(operation.token, operation.token.token));
    };
    // 異なる回復規則でネイティブの木を構築せず、不完全な
    // 入力の診断も含め、Dの演算子スタックの順序を使う。
    while (index < tokens.size()) {
        const Token token = tokens[index];
        const String text = token.token;
        const auto folded = lower(text);
        bool unary = false;
        if (folded == "not") {
            if (!isOp) invalid(&msgs::Msgs::expressionErrorNeedBoolean, "Need boolean.", token);
            opLevel = 2;
            unary = true;
        } else if (folded == ".") {
            if (isOp) invalid(&msgs::Msgs::expressionErrorNeedSymbolOrNumber, "Need symbol or number.", token);
            opLevel = 100;
        } else if (folded == "-" || folded == "+") {
            unary = isOp;
            opLevel = unary ? 99 : 4;
        } else if (folded == "~") {
            if (isOp) invalid(&msgs::Msgs::expressionErrorNeedSymbolOrNumber, "Need symbol or number.", token);
            opLevel = 4;
        } else if (folded == "/" || folded == "*" || folded == "%") {
            if (isOp) invalid(&msgs::Msgs::expressionErrorNeedSymbolOrNumber, "Need symbol or number.", token);
            opLevel = 5;
        } else if (folded == "<=" || folded == ">=" || folded == "<>" || folded == "<" || folded == ">" || folded == "=") {
            if (isOp) invalid(&msgs::Msgs::expressionErrorNeedSymbolOrNumber, "Need symbol or number.", token);
            opLevel = 3;
        } else if (folded == "and" || folded == "or") {
            if (isOp) invalid(&msgs::Msgs::expressionErrorNeedSymbolOrNumber, "Need symbol or number.", token);
            opLevel = folded == "and" ? 1 : 0;
        } else if (folded == "(") {
            if (!isOp) invalid(&msgs::Msgs::expressionErrorNeedOperator, "Need operator.", token);
            ++parLevel;
            ++index;
            continue;
        } else if (folded == ")") {
            if (isOp) invalid(&msgs::Msgs::expressionErrorNeedSymbolOrNumber, "Need symbol or number.", token);
            if (parLevel == 0) break;
            --parLevel;
            ++index;
            continue;
        } else if (folded == ",") {
            if (parLevel == 0) break;
            if (isOp) invalid(&msgs::Msgs::expressionErrorNeedSymbolOrNumber, "Need symbol or number.", token);
            else invalid(&msgs::Msgs::expressionErrorNeedOperator, "Need operator.", token);
            ++index;
            continue;
        } else if (folded == "true" || folded == "false") {
            if (!isOp) invalid(&msgs::Msgs::expressionErrorNeedOperator, "Need operator.", token);
            result.push_back(boolean(token, folded == "true"));
            isOp = false;
            ++index;
            continue;
        } else {
            if (text.empty()) throw std::out_of_range("Empty expression token");
            if (text.front() == '"') {
                if (!isOp) invalid(&msgs::Msgs::expressionErrorNeedOperator, "Need operator.", token);
                if (text.size() < 2 || text.back() != '"') throw std::logic_error("Unterminated expression string token");
                String body = text.substr(1, text.size() - 2);
                for (std::size_t quote = 0; (quote = body.find("\"\"", quote)) != String::npos; ++quote) body.erase(quote, 1);
                result.push_back(string(token, std::move(body)));
                ++index;
            } else if (text.front() == '@') {
                if (!isOp) invalid(&msgs::Msgs::expressionErrorNeedOperator, "Need operator.", token);
                if (text.size() < 3 || text[1] != '"' || text.back() != '"') throw std::logic_error("Invalid expression variable token");
                String body = text.substr(2, text.size() - 3);
                for (std::size_t quote = 0; (quote = body.find("\"\"", quote)) != String::npos; ++quote) body.erase(quote, 1);
                const auto argument = string(Token{true, token.line, token.pos, token.index + 1, text.substr(1)}, std::move(body));
                result.push_back(std::make_shared<Function>(token, "var", PartList{argument}));
                ++index;
            } else if (text.front() >= '0' && text.front() <= '9') {
                if (!isOp) invalid(&msgs::Msgs::expressionErrorNeedOperator, "Need operator.", token);
                double value = 0;
                // Phobosは非有限値への変換を範囲エラーとして拒否する。
                if (parseNumber(text, value)) result.push_back(number(token, value));
                else {
                    static_cast<void>(utils::printStackTrace());
                    utils::debugln("Invalid expression number: ", text);
                    invalid(&msgs::Msgs::expressionErrorInvalidNumber, "Invalid number.", token);
                }
                ++index;
            } else {
                if (!isOp) invalid(&msgs::Msgs::expressionErrorNeedOperator, "Need operator.", token);
                if (index + 1 < tokens.size() && tokens[index + 1].token == "(") {
                    auto args = parseArguments(prop, tokens, index, errors);
                    result.push_back(std::make_shared<Function>(token, text, std::move(args)));
                } else {
                    result.push_back(std::make_shared<Symbol>(token, text));
                    ++index;
                }
            }
            isOp = false;
            continue;
        }
        const PendingOperator next{parLevel, opLevel, unary, token};
        while (!operators.empty() && std::pair{next.parLevel, next.opLevel} <=
                                      std::pair{operators.back().parLevel, operators.back().opLevel}) {
            if (unary && operators.back().unary) break;
            emit(operators.back());
            operators.pop_back();
        }
        operators.push_back(next);
        isOp = true;
        ++index;
    }
    // Dはここでスタックを処理し、対応する閉じ記号がないという追加診断は出さない。
    while (!operators.empty()) {
        emit(operators.back());
        operators.pop_back();
    }
    return result;
}

PartList parseArguments(const CProps* prop, const lib::Array<Token>& tokens,
                        std::size_t& index, ExprErrors& errors) {
    ++index;
    if (index >= tokens.size()) {
        const auto& token = tokens.at(index - 1);
        errors.emplace_back(expressionMessage(prop, &msgs::Msgs::expressionErrorInvalidFunctionCall, "Invalid function call."),
                            token.line, token.pos, __FILE__, __LINE__);
        return {};
    }
    if (tokens[index].token != "(") {
        const auto& token = tokens[index];
        errors.emplace_back(expressionMessage(prop, &msgs::Msgs::expressionErrorNeedOpenParen, "Need open paren."),
                            token.line, token.pos, __FILE__, __LINE__);
        return {};
    }
    PartList result;
    while (index + 1 < tokens.size() && tokens[index].token != ")") {
        ++index;
        const Token token = tokens[index];
        if (token.token == ")") break;
        auto argument = parseSemantics(prop, tokens, index, errors);
        if (argument.size() == 1) result.push_back(argument.front());
        else if (!argument.empty()) result.push_back(std::make_shared<Expr>(token, std::move(argument)));
        else errors.emplace_back(expressionMessage(prop, &msgs::Msgs::expressionErrorNoArgument, "No argument."),
                                 token.line, token.pos, __FILE__, __LINE__);
    }
    ++index;
    return result;
}

namespace {

[[nodiscard]] ConstPartPtr evaluate(const CProps* prop, EvalMode mode,
                                    const VariableInfo& info, ConstPartPtr value,
                                    ExprErrors& errors) {
    if (!value) return unknown({});
    if (auto function = std::dynamic_pointer_cast<const Function>(value)) {
        return function->call(prop, mode, info, errors);
    }
    if (auto expression = std::dynamic_pointer_cast<const Expr>(value)) {
        return calculate(prop, mode, info, expression->parts, errors);
    }
    if (auto unary = std::dynamic_pointer_cast<const UnaryOperator>(value)) {
        return unary->call(prop, evaluate(prop, mode, info, unary->operand, errors), errors);
    }
    if (auto operation = std::dynamic_pointer_cast<const Operator>(value)) {
        // Dの後置記法の評価器は、診断後も演算の仮オブジェクトで続行
        // する。被演算子の字句順を維持し、Function::call
        // の即時評価引数の失敗判定と遅延関数の意味は別に保つ。
        const auto lhs = evaluate(prop, mode, info, operation->lhs, errors);
        const auto rhs = operation->operator_ == "." ? operation->rhs
            : evaluate(prop, mode, info, operation->rhs, errors);
        return operation->call(prop, mode, info, lhs, rhs, errors);
    }
    if (std::dynamic_pointer_cast<const Symbol>(value)) return value->toValueObj(prop, errors);
    return value;
}

[[nodiscard]] bool partEqual(const CProps* prop, EvalMode mode,
                             const VariableInfo& info, ConstPartPtr lhs, ConstPartPtr rhs,
                             ExprErrors& errors) {
    if (const auto* leftList = asList(lhs)) {
        const auto* rightList = asList(rhs);
        if (!rightList || leftList->listVal.size() != rightList->listVal.size()) return false;
        const auto length = leftList->listVal.size();
        for (std::size_t i = 0; i < length; ++i) {
            const auto left = evalListElement(prop, mode, info, *leftList, i, errors);
            const auto right = evalListElement(prop, mode, info, *rightList, i, errors);
            if (!partEqual(prop, mode, info, left, right, errors)) return false;
        }
        return true;
    }
    if (const auto* leftStruct = asStructure(lhs)) {
        const auto* rightStruct = asStructure(rhs);
        if (!rightStruct || leftStruct->structName != rightStruct->structName ||
            leftStruct->structVal.size() != rightStruct->structVal.size()) return false;
        const auto length = leftStruct->structVal.size();
        for (std::size_t i = 0; i < length; ++i) {
            const auto left = evalFunctionPart(prop, mode, info, leftStruct->structVal.at(i), errors);
            const auto right = evalFunctionPart(prop, mode, info, rightStruct->structVal.at(i), errors);
            if (!partEqual(prop, mode, info, left, right, errors)) return false;
        }
        return true;
    }
    // Dが評価するのは上記のコンテナー要素であり、ここで任意の単一値の被演算子を
    // 評価しない。単一値の等価判定は演算子検査を共有し、型の診断は局所的に扱う。
    ExprErrors comparisonErrors;
    return operatorEqual(prop, lhs, rhs, comparisonErrors, false, true);
}

[[nodiscard]] ConstPartList evaluatedArgs(const CProps* prop, EvalMode mode,
                                          const VariableInfo& info, ConstPartView args,
                                          ExprErrors& errors) {
    ConstPartList result;
    result.reserve(args.size());
    for (const auto& arg : args) result.push_back(evaluate(prop, mode, info, arg, errors));
    return result;
}

[[nodiscard]] std::uint32_t positiveIndex(const ConstPartPtr& value) {
    const auto* numberValue = asNumber(value);
    if (!numberValue || !std::isfinite(numberValue->numVal) || numberValue->numVal < 1) return 0;
    return static_cast<std::uint32_t>(std::floor(numberValue->numVal));
}

[[nodiscard]] PartPtr functionCardInfo(const CProps* prop, EvalMode mode, const Function& func,
                                       ConstPartView args, ExprErrors& errors) {
    if (mode != EvalMode::All) {
        addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorPermissionOfStructure, "Structure is not accessible: %1$s", "CARDINFO"), func);
        PartList defaults;
        const auto& info = cardInfo();
        defaults.reserve(info.members.size());
        for (const auto& member : info.members) {
            defaults.push_back(variantValToValueObj(func.token, member.defaultValue));
        }
        return std::make_shared<StructureValue>(func.token, info.name, std::move(defaults));
    }
    (void)argCount(prop, func, args, 2, 3, errors);
    PartList values;
    values.reserve(3);
    if (!args.empty()) {
        const auto* castIndex = requireMinNumber(prop, func, args, 0, -2, mode, errors);
        values.push_back(castIndex ? number(func.token, castIndex->numVal)
                                   : number(func.token, 0));
    } else {
        values.push_back(number(func.token, 0));
    }
    if (args.size() >= 2) {
        const auto* cardIndex = requireMinNumber(prop, func, args, 1, 0, mode, errors);
        values.push_back(cardIndex ? number(func.token, cardIndex->numVal)
                                   : number(func.token, 0));
    } else {
        values.push_back(number(func.token, 0));
    }
    if (args.size() >= 3) {
        const auto* actionCard = requireNumber(prop, func, args, 2, errors);
        values.push_back(actionCard ? number(func.token, actionCard->numVal)
                                    : number(func.token, -2));
    } else {
        values.push_back(number(func.token, -2));
    }
    return std::make_shared<StructureValue>(func.token, "cardinfo", std::move(values));
}

[[nodiscard]] PartPtr functionList(const Function& func, ConstPartView args) {
    return std::make_shared<ListValue>(func.token, std::move(args));
}

[[nodiscard]] PartPtr functionString(const CProps* prop, const Function& func, ConstPartView args,
                                     ExprErrors& errors) {
    if (!argCount(prop, func, args, 1, 1, errors)) return string(func.token, {});
    if (asList(args[0]) || asStructure(args[0])) {
        addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorConversionToString, "Conversion to string error: %1$s", args[0]->token.token), *args[0]);
    }
    return string(func.token, args[0]->stringValue());
}

[[nodiscard]] PartPtr functionValue(const CProps* prop, const Function& func, ConstPartView args,
                                     EvalMode mode, ExprErrors& errors) {
    if (!argCount(prop, func, args, 1, 1, errors)) return number(func.token, 0);
    if (isUnknown(args[0])) return number(func.token, 0);
    if (const auto* value = asNumber(args[0])) return number(func.token, value->numVal);
    if (const auto* value = asString(args[0])) {
        double numberValue{};
        if (parseValueNumber(value->strVal, numberValue)) {
            return number(func.token, numberValue);
        }
    }
    if (mode != EvalMode::TypeCheck || !asString(args[0])) addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorConversionToNumber, "Conversion to number error: %1$s", asString(args[0]) ? asString(args[0])->strVal : args[0]->token.token), *args[0]);
    return number(func.token, 0);
}

[[nodiscard]] PartPtr functionInt(const CProps* prop, const Function& func, ConstPartView args,
                                   EvalMode mode, ExprErrors& errors) {
    if (!argCount(prop, func, args, 1, 1, errors)) return number(func.token, 0);
    if (isUnknown(args[0])) return number(func.token, 0);
    if (const auto* value = asNumber(args[0])) return number(func.token, std::trunc(value->numVal));
    if (const auto* value = asString(args[0])) {
        double numberValue{};
        if (parseNumber(value->strVal, numberValue)) return number(func.token, std::trunc(numberValue));
    }
    if (mode != EvalMode::TypeCheck || !asString(args[0])) addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorConversionToNumber, "Conversion to number error: %1$s", asString(args[0]) ? asString(args[0])->strVal : args[0]->token.token), *args[0]);
    return number(func.token, 0);
}

[[nodiscard]] PartPtr functionVariable(const CProps* prop, EvalMode mode,
                                       const VariableInfo& info,
                                       const Function& func, ConstPartView args,
                                       ExprErrors& errors) {
    if (mode != EvalMode::TypeCheck && (!info.existsVariant || !info.variantValue))
        throw std::runtime_error(upper(func.funcName) + " is not callable.");
    if (!argCount(prop, func, args, 1, 1, errors)) {
        return mode == EvalMode::TypeCheck ? unknown(func.token) : number(func.token, 0);
    }
    const auto* name = requireString(prop, func, args, 0, errors);
    if (!name) return number(func.token, 0);
    if (mode == EvalMode::TypeCheck) return unknown(func.token);
    if (!info.existsVariant || !info.variantValue || !info.existsVariant(name->strVal)) {
        addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorVariantNotFound, "Variant not found: %1$s", name->strVal), *args[0], prop);
        return number(func.token, 0);
    }
    const auto value = info.variantValue(name->strVal);
    return variantValToValueObj(func.token, value);
}

[[nodiscard]] PartPtr functionFlag(const CProps* prop, EvalMode mode,
                                    const VariableInfo& info, const Function& func,
                                    ConstPartView args, ExprErrors& errors,
                                    bool textResult) {
    if (mode != EvalMode::TypeCheck &&
        (!info.existsFlag || !info.flagValue || (textResult && !info.flagText)))
        throw std::runtime_error(upper(func.funcName) + " is not callable.");
    if (!argCount(prop, func, args, textResult ? 1 : 1, textResult ? 2 : 1, errors)) {
        return textResult ? string(func.token, {}) : boolean(func.token, false);
    }
    const auto* name = requireString(prop, func, args, 0, errors);
    if (!name) return textResult ? string(func.token, {}) : boolean(func.token, false);
    if (mode == EvalMode::TypeCheck) return textResult ? string(func.token, {}) : boolean(func.token, false);
    if (!info.existsFlag || !info.existsFlag(name->strVal)) {
        addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorFlagNotFound, "Flag not found: %1$s", name->strVal), *args[0], prop);
        return textResult ? string(func.token, {}) : boolean(func.token, false);
    }
    if (!textResult) return boolean(func.token, info.flagValue(name->strVal));
    if (args.size() == 2) {
        const auto* requested = requireBoolean(prop, func, args, 1, errors);
        if (!requested) return string(func.token, {});
        return string(func.token, info.flagText ? info.flagText(name->strVal, requested->boolVal) : String{});
    }
    return string(func.token, info.flagText(name->strVal, info.flagValue(name->strVal)));
}

[[nodiscard]] PartPtr functionStep(const CProps* prop, EvalMode mode,
                                   const VariableInfo& info, const Function& func,
                                   ConstPartView args, ExprErrors& errors,
                                   StringView kind) {
    const bool textResult = kind == "text";
    const bool maxResult = kind == "max";
    if (mode != EvalMode::TypeCheck &&
        (!info.existsStep || (!maxResult && !info.stepValue) ||
         ((textResult || maxResult) && !info.stepMax) || (textResult && !info.stepText)))
        throw std::runtime_error(upper(func.funcName) + " is not callable.");
    if (!argCount(prop, func, args, 1, textResult ? 2 : 1, errors)) {
        return textResult ? string(func.token, {}) : number(func.token, 0);
    }
    const auto* name = requireString(prop, func, args, 0, errors);
    if (!name) return textResult ? string(func.token, {}) : number(func.token, 0);
    if (mode == EvalMode::TypeCheck) return textResult ? string(func.token, {}) : number(func.token, 0);
    if (!info.existsStep || !info.existsStep(name->strVal)) {
        addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorStepNotFound, "Step not found: %1$s", name->strVal), *args[0], prop);
        return textResult ? string(func.token, {}) : number(func.token, 0);
    }
    if (maxResult) return number(func.token, info.stepMax ? info.stepMax(name->strVal) : 0);
    if (!textResult) return number(func.token, info.stepValue(name->strVal));
    if (args.size() == 2) {
        const auto* requested = requireNumber(prop, func, args, 1, errors);
        if (!requested) return string(func.token, {});
        const auto converted = truncatedUint32Bits(requested->numVal);
        if (!converted) return string(func.token, {});
        const auto value = *converted;
        if (info.stepMax && value > info.stepMax(name->strVal)) {
            addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorInvalidStepValue, "Invalid step value: %1$s[%2$s]", name->strVal, value), *args[0]);
            return string(func.token, {});
        }
        return string(func.token, info.stepText(name->strVal, value));
    }
    return string(func.token, info.stepText(name->strVal, info.stepValue(name->strVal)));
}

[[nodiscard]] PartPtr functionNumericList(const CProps* prop, const Function& func, ConstPartView args,
                                           bool maximum, ExprErrors& errors) {
    if (args.empty()) {
        addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorNoArgumentWithFunctionName,
            "No argument: %1$s", upper(func.funcName)), func);
        return number(func.token, 0);
    }
    const auto values = requireAllNumbers(prop, func, args, errors);
    if (values.empty()) return number(func.token, 0);
    // Phobosのreduceは最初の値を初期値にする。max(a,b)はa<b?b:a、min(a,b)
    // はa<b?a:bで、NaNや符号付きゼロが同順位の場合の動作も含めて合わせる。
    double result = values.front()->numVal;
    for (std::size_t i = 1; i < values.size(); ++i) {
        const auto next = values[i]->numVal;
        result = maximum ? (result < next ? next : result)
                         : (result < next ? result : next);
    }
    return number(func.token, result);
}

[[nodiscard]] ConstPartPtr evalExpressionValue(const CProps* prop, EvalMode mode,
                                               const VariableInfo& info, ConstPartPtr value,
                                               ExprErrors& errors) {
    if (!value) return unknown({});
    if (const auto* textValue = asString(value)) {
        ExprErrors local;
        const auto parsed = parseExpression(prop, textValue->strVal, local);
        errors.insert(errors.end(), local.begin(), local.end());
        if (!errors.empty()) {
            return unknown(textValue->token);
        }
        return calculate(prop, mode, info, parsed, errors);
    }
    if (const auto* listValue = asList(value)) {
        ConstPartList result;
        result.reserve(listValue->listVal.size());
        for (const auto& item : listValue->listVal) {
            const auto before = errors.size();
            const auto evaluated = evaluate(prop, mode, info, item, errors);
            if (before < errors.size()) return std::make_shared<ListValue>(listValue->token, std::move(result));
            if (mode == EvalMode::TypeCheck && asString(evaluated)) {
                result.push_back(unknown(evaluated->token));
            } else {
                result.push_back(evalExpressionValue(prop, mode, info, evaluated, errors));
                if (before < errors.size()) return std::make_shared<ListValue>(listValue->token, std::move(result));
            }
        }
        return std::make_shared<ListValue>(listValue->token, std::move(result));
    }
    if (const auto* structureValue = asStructure(value)) {
        ConstPartList result;
        result.reserve(structureValue->structVal.size());
        for (const auto& item : structureValue->structVal) {
            const auto before = errors.size();
            const auto evaluated = evaluate(prop, mode, info, item, errors);
            if (before < errors.size()) return unknown(structureValue->token);
            result.push_back(evalExpressionValue(prop, mode, info, evaluated, errors));
            if (before < errors.size()) return unknown(structureValue->token);
        }
        return std::make_shared<StructureValue>(structureValue->token,
                                                structureValue->structName,
                                                std::move(result));
    }
    if (isUnknown(value) && mode == EvalMode::TypeCheck) return value;
    addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorArgumentIsNotString, "Argument is not string: %1$s, %2$s", "EVAL", 1), *value);
    return unknown(value->token);
}

[[nodiscard]] ConstPartPtr functionEval(const CProps* prop, EvalMode mode,
                                         const VariableInfo& info, const Function& func,
                                         ConstPartView args, ExprErrors& errors) {
    if (!argCount(prop, func, args, 1, 1, errors)) return unknown(func.token);
    return evalExpressionValue(prop, mode, info, args[0], errors);
}

[[nodiscard]] ConstPartPtr dispatchFunction(const CProps* prop, EvalMode mode,
                                            const VariableInfo& info,
                                            const Function& func, ConstPartView args,
                                            ExprErrors& errors, StringView functionName) {
    const String name = lower(functionName);
    // ATはバージョンより先に引数の個数を検査する。他のWsn.5ハンドラーはバージョンを先に検査する。
    if (requiresWsn5(name) && name != "at") {
        (void)checkWsnVersionForFunctionExists(prop, info, func, "5", errors);
    }
    if (mode != EvalMode::TypeCheck) {
        const bool missing =
            (name == "findcoupon" && !info.findCoupon) ||
            (name == "coupontext" && !info.couponText) ||
            (name == "findgossip" && !info.findGossip) ||
            (name == "gossiptext" && !info.gossipText) ||
            (name == "partyname" && !info.partyName) ||
            (name == "yadoname" && !info.yadoName) ||
            (name == "findkeycode" && !info.findKeyCode) ||
            (name == "keycodetext" && !info.KeyCodeText);
        if (missing) throw std::runtime_error(upper(func.funcName) + " is not callable.");
    }
    if (name == "cardinfo") return functionCardInfo(prop, mode, func, args, errors);
    if (name == "max") return functionNumericList(prop, func, args, true, errors);
    if (name == "min") return functionNumericList(prop, func, args, false, errors);
    if (name == "len") {
        if (!argCount(prop, func, args, 1, 1, errors)) return number(func.token, 0);
        const auto* value = requireString(prop, func, args, 0, errors);
        return value ? number(func.token, static_cast<double>(utf8Length(value->strVal)))
                     : number(func.token, 0);
    }
    if (name == "find") {
        if (!argCount(prop, func, args, 2, 3, errors)) return number(func.token, 0);
        const auto* needle = requireString(prop, func, args, 0, errors);
        if (!needle) return number(func.token, 0);
        const auto* haystack = requireString(prop, func, args, 1, errors);
        if (!haystack) return number(func.token, 0);
        std::size_t start = 1;
        if (args.size() == 3) {
            const auto* n = requireMinNumber(prop, func, args, 2, 0, mode, errors);
            if (!n) return number(func.token, 0);
            start = checkedTruncatedInteger<std::size_t>(n->numVal);
        }
        if (start == 0) return number(func.token, 0);
        return number(func.token, static_cast<double>(findUtf8(needle->strVal,
                                                               haystack->strVal, start)));
    }
    if (name == "left" || name == "right") {
        if (!argCount(prop, func, args, 2, 2, errors)) return string(func.token, {});
        const auto* source = requireString(prop, func, args, 0, errors);
        if (!source) return string(func.token, {});
        const auto* count = requireMinNumber(prop, func, args, 1, 0, mode, errors);
        if (!count) return string(func.token, {});
        const auto length = checkedTruncatedInteger<std::size_t>(count->numVal);
        return string(func.token, name == "left" ? utf8Left(source->strVal, length)
                                                  : utf8Right(source->strVal, length));
    }
    if (name == "mid") {
        if (!argCount(prop, func, args, 2, 3, errors)) return string(func.token, {});
        const auto* source = requireString(prop, func, args, 0, errors);
        if (!source) return string(func.token, {});
        const auto* start = requireMinNumber(prop, func, args, 1, 1, mode, errors);
        if (!start) return string(func.token, {});
        const auto first = checkedTruncatedInteger<std::size_t>(start->numVal) - 1;
        // Dは省略可能な個数の検査・評価より先にUTF-32へコピーする。個
        // 数の評価が入力元の値を変更しても、その取得済みの状態を維持する。
        const auto points = enumutils::toUtf32Replacing(source->strVal);
        if (first >= points.size()) return string(func.token, {});
        if (args.size() == 3) {
            const auto* count = requireMinNumber(prop, func, args, 2, 0, mode, errors);
            if (!count) return string(func.token, {});
            return string(func.token, utf8Mid(points, first,
                                              checkedTruncatedInteger<std::size_t>(count->numVal)));
        }
        return string(func.token, utf8Mid(points, first));
    }
    if (name == "str") return functionString(prop, func, args, errors);
    if (name == "value") return functionValue(prop, func, args, mode, errors);
    if (name == "int") return functionInt(prop, func, args, mode, errors);
    if (name == "is_str" || name == "is_number" || name == "is_list") {
        if (!argCount(prop, func, args, 1, 1, errors)) return boolean(func.token, false);
        return boolean(func.token,
                       name == "is_str" ? asString(args[0]) != nullptr
                       : name == "is_number" ? asNumber(args[0]) != nullptr
                                             : asList(args[0]) != nullptr);
    }
    if (name == "var") return functionVariable(prop, mode, info, func, args, errors);
    if (name == "flagvalue") return functionFlag(prop, mode, info, func, args, errors, false);
    if (name == "flagtext") return functionFlag(prop, mode, info, func, args, errors, true);
    if (name == "stepvalue") return functionStep(prop, mode, info, func, args, errors, "value");
    if (name == "steptext") return functionStep(prop, mode, info, func, args, errors, "text");
    if (name == "stepmax") return functionStep(prop, mode, info, func, args, errors, "max");
    if (name == "dice") {
        if (!argCount(prop, func, args, 2, 2, errors)) return number(func.token, 0);
        const auto* times = requireMinNumber(prop, func, args, 0, 0, mode, errors);
        if (!times) return number(func.token, 0);
        const auto* sides = requireMinNumber(prop, func, args, 1, 0, mode, errors);
        if (!sides) return number(func.token, 0);
        const auto count = checkedTruncatedInteger<std::size_t>(times->numVal);
        const auto faces = checkedTruncatedInteger<std::size_t>(sides->numVal);
        if (count == 0 || faces == 0) return number(func.token, 0);
        if (mode == EvalMode::TypeCheck) return number(func.token, static_cast<double>(count));
        // Dはuniform(size_t(0), sides)+1を使う。ネイティブの乱数生
        // 成器と種は言語ランタイム固有とし、同じ一様分布の範囲とsize_tの演算を維持する。
        static thread_local std::mt19937_64 generator(std::random_device{}());
        std::uniform_int_distribution<std::size_t> distribution(0, faces - 1);
        std::size_t result = 0;
        for (std::size_t i = 0; i < count; ++i) {
            result += distribution(generator) + 1;
        }
        return number(func.token, static_cast<double>(result));
    }
    if (name == "selected") {
        if (!argCount(prop, func, args, 0, 0, errors)) return number(func.token, 0);
        if (mode != EvalMode::TypeCheck && !info.selectedPlayerCardNumber)
            throw std::runtime_error(upper(func.funcName) + " is not callable.");
        return number(func.token, mode == EvalMode::TypeCheck || !info.selectedPlayerCardNumber
                                   ? 0 : info.selectedPlayerCardNumber());
    }
    if (name == "latestusecardowner") {
        (void)argCount(prop, func, args, 0, 0, errors);
        return number(func.token, 0);
    }
    if (name == "charexists") {
        if (!argCount(prop, func, args, 1, 1, errors)) return boolean(func.token, false);
        const auto* n = requireMinNumber(prop, func, args, 0, 0, mode, errors);
        if (!n || mode == EvalMode::TypeCheck) return boolean(func.token, false);
        if (!info.castName) throw std::runtime_error(upper(func.funcName) + " is not callable.");
        const auto index = finiteTruncatedInteger<std::uint32_t>(n->numVal);
        if (!index || *index == 0) return boolean(func.token, false);
        return boolean(func.token, !info.castName(*index).empty());
    }
    if (name == "casttype") {
        if (!argCount(prop, func, args, 1, 1, errors)) return number(func.token, 0);
        const auto* n = requireMinNumber(prop, func, args, 0, 0, mode, errors);
        if (!n) return number(func.token, 0);
        const auto index = checkedTruncatedInteger<std::uint32_t>(n->numVal);
        if (index == 0) return number(func.token, 0);
        // 変換とゼロの処理後にpropへアクセスする、Dの順序に合わせる。
        if (!prop) throw std::invalid_argument("CASTTYPE requires editor properties");
        return number(func.token, index <= prop->looks().partyMax() ? 1 : 0);
    }
    if (name == "castname") {
        if (!argCount(prop, func, args, 1, 1, errors)) return string(func.token, {});
        const auto* n = requireMinNumber(prop, func, args, 0, 0, mode, errors);
        if (!n) return string(func.token, {});
        const auto index = checkedTruncatedInteger<std::uint32_t>(n->numVal);
        if (index == 0) return string(func.token, {});
        return string(func.token, info.castName(index));
    }
    if (name == "findcoupon") {
        if (!argCount(prop, func, args, 2, 3, errors)) return number(func.token, 0);
        const auto* cast = requireMinNumber(prop, func, args, 0, 0, mode, errors);
        if (!cast) return number(func.token, 0);
        const auto* pattern = requireString(prop, func, args, 1, errors);
        if (!cast || !pattern) return number(func.token, 0);
        if (mode == EvalMode::TypeCheck) return number(func.token, 0);
        std::uint32_t start = 1;
        if (args.size() == 3) {
            const auto* value = requireMinNumber(prop, func, args, 2, 0, mode, errors);
            if (!value) return number(func.token, 0);
            const auto position = finiteTruncatedInteger<std::uint32_t>(value->numVal);
            if (!position || *position == 0) return number(func.token, 0);
            start = *position;
        }
        if (mode == EvalMode::TypeCheck || !info.findCoupon) return number(func.token, 0);
        return number(func.token, info.findCoupon(checkedTruncatedInteger<std::uint32_t>(cast->numVal), pattern->strVal, start));
    }
    if (name == "coupontext") {
        if (!argCount(prop, func, args, 2, 2, errors)) return string(func.token, {});
        const auto* cast = requireMinNumber(prop, func, args, 0, 0, mode, errors);
        if (!cast) return number(func.token, 0);
        const auto* numberValue = requireMinNumber(prop, func, args, 1, 0, mode, errors);
        if (!numberValue) return number(func.token, 0);
        if (mode == EvalMode::TypeCheck) return string(func.token, {});
        return string(func.token, info.couponText(checkedTruncatedInteger<std::uint32_t>(cast->numVal),
                                                  checkedTruncatedInteger<std::uint32_t>(numberValue->numVal)));
    }
    if (name == "findgossip") {
        if (!argCount(prop, func, args, 1, 2, errors)) return number(func.token, 0);
        const auto* pattern = requireString(prop, func, args, 0, errors);
        if (!pattern) return number(func.token, 0);
        std::uint32_t start = 1;
        if (args.size() == 2) {
            const auto* value = requireMinNumber(prop, func, args, 1, 0, mode, errors);
            if (!value) return number(func.token, 0);
            const auto position = finiteTruncatedInteger<std::uint32_t>(value->numVal);
            if (!position || *position == 0) return number(func.token, 0);
            start = *position;
        }
        if (mode == EvalMode::TypeCheck || !info.findGossip) return number(func.token, 0);
        return number(func.token, info.findGossip(pattern->strVal, start));
    }
    if (name == "gossiptext") {
        if (!argCount(prop, func, args, 1, 1, errors)) return string(func.token, {});
        const auto* value = requireMinNumber(prop, func, args, 0, 0, mode, errors);
        if (!value) return number(func.token, 0);
        if (mode == EvalMode::TypeCheck) return string(func.token, {});
        return string(func.token, info.gossipText(checkedTruncatedInteger<std::uint32_t>(value->numVal)));
    }
    if (name == "partyname") {
        if (!argCount(prop, func, args, 0, 0, errors)) return string(func.token, {});
        return string(func.token, mode == EvalMode::TypeCheck || !info.partyName ? String{} : info.partyName());
    }
    if (name == "list" || name == "list_e" || name == "makelist") {
        if (name != "makelist") return functionList(func, args);
        if (!argCount(prop, func, args, 2, 2, errors)) return list(func.token, {});
        const auto* first = requireNumber(prop, func, args, 0, errors);
        const auto* last = requireNumber(prop, func, args, 1, errors);
        if (!first || !last || std::floor(last->numVal) < std::floor(first->numVal)) return list(func.token, {});
        PartList values;
        for (std::int64_t i = static_cast<std::int64_t>(std::floor(first->numVal));
             i <= static_cast<std::int64_t>(std::floor(last->numVal)); ++i) {
            values.push_back(number(func.token, static_cast<double>(i)));
            if (i == std::numeric_limits<std::int64_t>::max()) break;
        }
        return list(func.token, std::move(values));
    }
    if (name == "at") {
        if (!argCount(prop, func, args, 2, 2, errors)) return unknown(func.token);
        (void)checkWsnVersionForFunctionExists(prop, info, func, "5", errors);
        const auto* source = requireList(prop, func, args, 0, errors);
        if (!source) return unknown(func.token);
        const auto* position = requireMinNumber(prop, func, args, 1, 1, mode, errors);
        if (!position || position->numVal < 1) return unknown(func.token);
        const auto index = checkedTruncatedInteger<std::size_t>(position->numVal);
        if (index > source->listVal.size()) {
            const auto message = prop
                ? utils::tryFormat(prop->requiredMsgs().expressionErrorListIndexIsOutOfRange.value,
                                   index, source->listVal.size())
                : String("List index is out of range: %1$s, %2$s");
            addError(errors, utils::tryFormat(message, upper(func.funcName), 1), *args[1]);
            return unknown(func.token);
        }
        return evaluate(prop, mode, info, source->listVal[index - 1], errors);
    }
    if (name == "llen") {
        if (!argCount(prop, func, args, 1, 1, errors)) return number(func.token, 0);
        const auto* source = requireList(prop, func, args, 0, errors);
        return number(func.token, source ? static_cast<double>(source->listVal.size()) : 0);
    }
    if (name == "lfind" || name == "lfindrand") {
        const auto hasOptionalStart = name == "lfind";
        if (!argCount(prop, func, args, 2, hasOptionalStart ? 3 : 2, errors)) {
            return number(func.token, 0);
        }
        const auto* source = requireList(prop, func, args, 1, errors);
        if (!source) return number(func.token, 0);
        std::size_t start = 0;
        if (args.size() == 3) {
            const auto* position = requireMinNumber(prop, func, args, 2, 0, mode, errors);
            if (!position) return number(func.token, 0);
            start = checkedTruncatedInteger<std::size_t>(position->numVal);
            if (start == 0) return number(func.token, 0);
            --start;
            if (start >= source->listVal.size()) return number(func.token, 0);
        }
        std::vector<std::size_t> matches;
        const auto sourceLength = source->listVal.size();
        for (std::size_t i = start; i < sourceLength; ++i) {
            const auto value = evalListElement(prop, mode, info, *source, i, errors);
            if (partEqual(prop, mode, info, args[0], value, errors)) {
                if (name == "lfind") {
                    return number(func.token, static_cast<double>(i + 1));
                }
                matches.push_back(i + 1);
            }
        }
        if (matches.empty()) return number(func.token, 0);
        if (name == "lfindrand" && mode == EvalMode::TypeCheck) return number(func.token, 0);
        if (name == "lfindrand") {
            // Dと同じ一様分布で候補を選ぶ。乱数生成器と種はネイティブ側のものを使う。
            static thread_local std::mt19937_64 generator(std::random_device{}());
            std::uniform_int_distribution<std::size_t> distribution(0, matches.size() - 1);
            return number(func.token, static_cast<double>(matches[distribution(generator)]));
        }
        return number(func.token, 0);
    }
    if (name == "lleft" || name == "lright" || name == "lmid") {
        const bool isMid = name == "lmid";
        if (!argCount(prop, func, args, 2, isMid ? 3 : 2, errors)) return list(func.token, {});
        const auto* source = requireList(prop, func, args, 0, errors);
        if (!source) return list(func.token, {});
        const auto* position = requireMinNumber(prop, func, args, 1, isMid ? 1 : 0,
                                                mode, errors);
        if (!position) return list(func.token, {});
        const auto n = checkedTruncatedInteger<std::size_t>(position->numVal);
        if (name == "lleft") {
            const auto end = std::min(n, source->listVal.size());
            return std::make_shared<ListValue>(func.token, source->listVal.slice(0, end));
        }
        if (name == "lright") {
            const auto start = source->listVal.size() - std::min(n, source->listVal.size());
            return std::make_shared<ListValue>(func.token,
                                               source->listVal.slice(start, source->listVal.size()));
        }
        const auto start = n - 1;
        if (start >= source->listVal.size()) return list(func.token, {});
        auto count = source->listVal.size() - start;
        if (args.size() == 3) {
            const auto* countValue = requireMinNumber(prop, func, args, 2, 0, mode, errors);
            if (!countValue) return list(func.token, {});
            count = std::min(count, checkedTruncatedInteger<std::size_t>(countValue->numVal));
        }
        return std::make_shared<ListValue>(func.token,
                                           source->listVal.slice(start, start + count));
    }
    if (name == "partymoney" || name == "battleround") {
        if (!argCount(prop, func, args, 0, 0, errors)) return number(func.token, 0);
        return number(func.token, 0);
    }
    if (name == "partynumber") {
        if (!argCount(prop, func, args, 0, 0, errors)) return number(func.token, 0);
        // Dはここでpropを参照する。nullのプロパティに対して、勝手なパーティ人数を返さない。
        if (!prop) throw std::invalid_argument("PARTYNUMBER requires editor properties");
        return number(func.token, static_cast<double>(prop->looks().partyMax()));
    }
    if (name == "yadoname" || name == "skintype" || name == "scenarioname") {
        if (!argCount(prop, func, args, 0, 0, errors)) return string(func.token, {});
        if (mode == EvalMode::TypeCheck) return string(func.token, {});
        if (name == "skintype") return string(func.token, {});
        const auto* getter = name == "yadoname" ? &info.yadoName : &info.scenarioName;
        if (!*getter) throw std::runtime_error(upper(func.funcName) + " is not callable.");
        return string(func.token, (*getter)());
    }
    if (name == "castlevel" || name == "couponvalue" || name == "liferatio" ||
        name == "statusvalue" || name == "statusround") {
        if (name == "castlevel" || name == "liferatio") {
            if (!argCount(prop, func, args, 1, 1, errors)) return number(func.token, 0);
            const auto* index = requireMinNumber(prop, func, args, 0, 0, mode, errors);
            if (!index || (name == "castlevel" && std::trunc(index->numVal) == 0)) return number(func.token, 0);
            return number(func.token, 1);
        }
        if (!argCount(prop, func, args, 2, 2, errors)) return number(func.token, 0);
        if (!requireMinNumber(prop, func, args, 0, 0, mode, errors)) return number(func.token, 0);
        if (name == "couponvalue") {
            if (!requireString(prop, func, args, 1, errors)) return number(func.token, 0);
            return number(func.token, 0);
        }
        if (!requireMinNumber(prop, func, args, 1, 0, mode, errors)) return number(func.token, 0);
        return number(func.token, 1);
    }
    if (name == "selectedcard") {
        if (!argCount(prop, func, args, 0, 0, errors)) {
            // Dは引数個数のエラーを報告するが、既定
            // のcardinfo構造体は作成する。
        }
        PartList values{number(func.token, 0), number(func.token, 0), number(func.token, -2)};
        return std::make_shared<StructureValue>(func.token, "cardinfo", std::move(values));
    }
    if (name == "cardname" || name == "cardtype" || name == "cardrarity" ||
        name == "cardprice" || name == "cardlevel" || name == "cardcount") {
        if (!argCount(prop, func, args, 1, 1, errors)) {
            return name == "cardname" ? string(func.token, {})
                 : number(func.token, (name == "cardrarity" || name == "cardprice" ||
                                       name == "cardlevel" || name == "cardcount") ? -1 : 0);
        }
        const auto* card = requireStructure(prop, func, args, 0, "cardinfo", errors);
        if (!card) return name == "cardname" ? string(func.token, {}) : number(func.token, 0);
        if (name == "cardname") {
            const auto* castIndex = asNumber(card->structVal[0]);
            const auto* cardIndex = asNumber(card->structVal[1]);
            if (!castIndex || !cardIndex || mode == EvalMode::TypeCheck)
                return string(func.token, {});
            const auto cast = truncatedUint32Bits(castIndex->numVal);
            const auto card = truncatedUint32Bits(cardIndex->numVal);
            if (!cast || !card || *cast == 0 || *card == 0) return string(func.token, {});
            if (!info.cardName) throw std::runtime_error(upper(func.funcName) + " is not callable.");
            return string(func.token, info.cardName(std::bit_cast<std::int32_t>(*cast), *card));
        }
        return number(func.token, 0);
    }
    if (name == "findkeycode" || name == "keycodetext") {
        if (name == "findkeycode") {
            if (!argCount(prop, func, args, 2, 3, errors)) return number(func.token, 0);
            const auto* card = requireStructure(prop, func, args, 0, "cardinfo", errors);
            if (!card) return number(func.token, 0);
            const auto* pattern = requireString(prop, func, args, 1, errors);
            if (!card || !pattern) return number(func.token, 0);
            std::uint32_t start = 1;
            if (args.size() == 3) {
                const auto* position = requireMinNumber(prop, func, args, 2, 0, mode, errors);
                if (!position) return number(func.token, 0);
                const auto index = finiteTruncatedInteger<std::uint32_t>(position->numVal);
                if (!index || *index == 0) return number(func.token, 0);
                start = *index;
            }
            if (mode == EvalMode::TypeCheck || !info.findKeyCode) return number(func.token, 0);
            return number(func.token, info.findKeyCode(*card, pattern->strVal, start));
        }
        if (!argCount(prop, func, args, 2, 2, errors)) return string(func.token, {});
        const auto* card = requireStructure(prop, func, args, 0, "cardinfo", errors);
        if (!card) return string(func.token, {});
        const auto* index = requireMinNumber(prop, func, args, 1, 0, mode, errors);
        if (!card || !index || mode == EvalMode::TypeCheck || !info.KeyCodeText) return string(func.token, {});
        return string(func.token, info.KeyCodeText(*card, checkedTruncatedInteger<std::uint32_t>(index->numVal)));
    }
    if (name == "floor" || name == "ceil") {
        if (!argCount(prop, func, args, 1, 1, errors)) return number(func.token, 0);
        const auto* value = requireNumber(prop, func, args, 0, errors);
        return value ? number(func.token, name == "floor" ? std::floor(value->numVal) : std::ceil(value->numVal))
                     : number(func.token, 0);
    }
    if (name == "sum") {
        if (!argCount(prop, func, args, 1, 2, errors)) return number(func.token, 0);
        const auto* source = requireList(prop, func, args, 0, errors);
        if (!source) return number(func.token, 0);
        bool convert = false;
        if (args.size() == 2) {
            const auto* value = requireBoolean(prop, func, args, 1, errors);
            if (!value) return number(func.token, 0);
            convert = value->boolVal;
        }
        double total = 0;
        std::size_t itemIndex{};
        for (const auto& item : source->listVal) {
            ++itemIndex;
            const auto errorCount = errors.size();
            const auto value = evaluate(prop, mode, info, item, errors);
            if (errors.size() != errorCount) return number(func.token, 0);
            if (const auto* n = asNumber(value)) total += n->numVal;
            else if (convert) {
                ExprErrors conversion_errors;
                const auto converted = functionValue(prop, func, ConstPartList{value}, mode, conversion_errors);
                if (!conversion_errors.empty()) {
                    addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorArgumentIsNotNumber,
                        "SUM list element is not number: %1$s, %2$s", upper(func.funcName), itemIndex), *value);
                    return number(func.token, 0);
                }
                total += asNumber(converted)->numVal;
            } else if (isUnknown(value) && mode == EvalMode::TypeCheck) {
                return number(func.token, 0);
            } else {
                addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorArgumentIsNotNumber,
                    "SUM list element is not number: %1$s, %2$s", upper(func.funcName), itemIndex), *value);
                return number(func.token, 0);
            }
        }
        return number(func.token, total);
    }
    if (name == "eval") return functionEval(prop, mode, info, func, args, errors);
    if (name == "format") {
        if (!argCount(prop, func, args, 1, std::numeric_limits<std::size_t>::max(), errors)) return string(func.token, {});
        const auto* formatValue = requireString(prop, func, args, 0, errors);
        if (!formatValue) return string(func.token, {});
        if (formatValue->strVal.find("{}") == String::npos) return string(func.token, formatValue->strVal);
        std::vector<String> replacements;
        for (std::size_t i = 1; i < args.size(); ++i) {
            const auto before = errors.size();
            const auto value = evalValueDeep(prop, mode, info, args[i], errors);
            if (before < errors.size()) return string(func.token, {});
            replacements.push_back(value->stringValue());
        }
        String result;
        std::size_t cursor = 0;
        std::size_t argIndex = 0;
        while (cursor < formatValue->strVal.size()) {
            const auto marker = formatValue->strVal.find("{}", cursor);
            if (marker == String::npos) {
                result += formatValue->strVal.substr(cursor);
                break;
            }
            result += formatValue->strVal.substr(cursor, marker - cursor);
            if (argIndex < replacements.size()) {
                result += replacements[argIndex++];
            } else {
                result += "{}";
            }
            cursor = marker + 2;
        }
        return string(func.token, std::move(result));
    }
    if (name == "sind" || name == "cosd" || name == "tand") {
        if (!argCount(prop, func, args, 1, 1, errors)) return number(func.token, 0);
        const auto* value = requireNumber(prop, func, args, 0, errors);
        if (!value) return number(func.token, 0);
        // Phobosはランタイムの基本関数でdoubleのsin/cosを提供する。
        // ネイティブlibmは言語側の境界とし、Dのdouble演算の順序を維持する。
        const double radians = value->numVal * (3.14159265358979323846 / 180.0);
        if (name == "sind") return number(func.token, std::sin(radians));
        if (name == "cosd") return number(func.token, std::cos(radians));
        const double cosine = std::cos(radians);
        if (std::abs(cosine) < 1e-12) {
            if (mode != EvalMode::TypeCheck) addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorCosineIsZero, "Cosine is zero."), *args[0]);
            return number(func.token, 0);
        }
        return number(func.token, std::sin(radians) / cosine);
    }
    if (name == "distance") {
        if (!argCount(prop, func, args, 4, 4, errors)) return number(func.token, 0);
        std::array<double, 4> values{};
        bool valid = true;
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto* value = requireNumber(prop, func, args, i, errors);
            if (!value) valid = false;
            else values[i] = value->numVal;
        }
        if (!valid) return number(func.token, 0);
        const double dx = values[0] - values[2];
        const double dy = values[1] - values[3];
        return number(func.token, std::sqrt(dx * dx + dy * dy));
    }
    if (name == "parsefloat" || name == "parseint") {
        if (!argCount(prop, func, args, 2, 2, errors)) return number(func.token, 0);
        double value{};
        if (const auto* n = asNumber(args[0])) value = n->numVal;
        else if (const auto* s = asString(args[0]); s && parseNumber(s->strVal, value)) {}
        else return args[1];
        return number(func.token, name == "parseint" ? std::trunc(value) : value);
    }
    if (name == "cardmaxuselimit") {
        if (!argCount(prop, func, args, 1, 1, errors)) return number(func.token, 0);
        (void)requireStructure(prop, func, args, 0, "cardinfo", errors);
        return number(func.token, 0);
    }
    if (name == "datetime") {
        if (!argCount(prop, func, args, 0, 0, errors)) return list(func.token, {});
        if (mode == EvalMode::TypeCheck) return list(func.token, {});
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        PartList values{number(func.token, tm.tm_year + 1900), number(func.token, tm.tm_mon + 1),
                        number(func.token, tm.tm_mday), number(func.token, tm.tm_hour),
                        number(func.token, tm.tm_min), number(func.token, tm.tm_sec)};
        return list(func.token, std::move(values));
    }
    if (name == "unixtime") {
        if (!argCount(prop, func, args, 0, 0, errors)) return number(func.token, 0);
        if (mode == EvalMode::TypeCheck) return number(func.token, 0);
        return number(func.token, static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()));
    }
    if (name == "getvariantvalueofcard" || name == "getflagvalueofcard" ||
        name == "getstepvalueofcard") {
        const auto invalidResult = [&]() -> PartPtr {
            return name == "getflagvalueofcard" ? number(func.token, 0)
                 : number(func.token, 0);
        };
        if (!argCount(prop, func, args, 2, 2, errors)) return invalidResult();
        const auto* card = requireStructure(prop, func, args, 0, "cardinfo", errors);
        if (!card) return invalidResult();
        const auto* variable = requireString(prop, func, args, 1, errors);
        if (!card || !variable) return invalidResult();
        const auto* castIndex = asNumber(card->structVal[0]);
        const auto* cardIndex = asNumber(card->structVal[1]);
        if (!castIndex || !cardIndex || mode == EvalMode::TypeCheck) {
            return name == "getflagvalueofcard" ? boolean(func.token, false)
                 : name == "getstepvalueofcard" ? number(func.token, 0)
                                                  : unknown(func.token);
        }
        const auto castBits = truncatedUint32Bits(castIndex->numVal);
        const auto cardBits = truncatedUint32Bits(cardIndex->numVal);
        if (!castBits || !cardBits) return invalidResult();
        const auto castNumber = std::bit_cast<std::int32_t>(*castBits);
        const auto cardNumber = *cardBits;
        if (name == "getvariantvalueofcard") {
            if (!info.variantValueOfCard) return number(func.token, 0);
            return variantValToValueObj(func.token,
                                        info.variantValueOfCard(castNumber, cardNumber,
                                                                variable->strVal));
        }
        if (name == "getflagvalueofcard") {
            return boolean(func.token, info.flagValueOfCard &&
                                      info.flagValueOfCard(castNumber, cardNumber, variable->strVal));
        }
        return number(func.token, info.stepValueOfCard
                                 ? info.stepValueOfCard(castNumber, cardNumber, variable->strVal)
                                 : 0);
    }
    if (name == "has_variantvalue" || name == "has_step" || name == "has_flag") {
        if (!argCount(prop, func, args, 2, 2, errors)) return boolean(func.token, false);
        const auto* card = requireStructure(prop, func, args, 0, "cardinfo", errors);
        if (!card) return boolean(func.token, false);
        const auto* variable = requireString(prop, func, args, 1, errors);
        return boolean(func.token, card != nullptr && variable != nullptr);
    }
    if (name == "setvariantvalueofcard" || name == "setstepvalueofcard" ||
        name == "setflagvalueofcard") {
        if (!argCount(prop, func, args, 3, 3, errors)) return boolean(func.token, false);
        const auto* card = requireStructure(prop, func, args, 0, "cardinfo", errors);
        if (!card) return boolean(func.token, false);
        const auto* variable = requireString(prop, func, args, 1, errors);
        if (!card || !variable) return boolean(func.token, false);
        if (name == "setstepvalueofcard" && !requireNumber(prop, func, args, 2, errors)) return boolean(func.token, false);
        if (name == "setflagvalueofcard" && !requireBoolean(prop, func, args, 2, errors)) return boolean(func.token, false);
        return boolean(func.token, true);
    }
    if (name == "mousex" || name == "mousey") {
        if (!argCount(prop, func, args, 0, 0, errors)) return number(func.token, 0);
        if (mode == EvalMode::TypeCheck) return number(func.token, 0);
        const auto& getter = name == "mousex" ? info.mouseX : info.mouseY;
        return number(func.token, getter ? getter() : -1);
    }
    if (name == "getrandomlist") {
        if (!argCount(prop, func, args, 3, 3, errors)) return list(func.token, {});
        const auto* first = requireNumber(prop, func, args, 0, errors);
        const auto* last = requireNumber(prop, func, args, 1, errors);
        const auto* count = requireNumber(prop, func, args, 2, errors);
        if (!first || !last || !count) return list(func.token, {});
        if (mode == EvalMode::TypeCheck) return list(func.token, {});
        const auto start = static_cast<std::int64_t>(std::floor(first->numVal));
        const auto end = static_cast<std::int64_t>(std::floor(last->numVal));
        if (end < start) {
            addError(errors, "GETRANDOMLIST end value must be greater than or equal to start value.",
                     *args[1]);
            return list(func.token, {});
        }
        const auto amount = count->numVal < 0
                              ? std::size_t{0}
                              : static_cast<std::size_t>(std::floor(count->numVal));
        const auto range = static_cast<std::uint64_t>(end - start + 1);
        if (amount > range) {
            addError(errors, "GETRANDOMLIST requested count exceeds the available range.", *args[2]);
            return list(func.token, {});
        }
        std::vector<std::int64_t> values;
        values.reserve(static_cast<std::size_t>(range));
        for (std::int64_t value = start; value <= end; ++value) {
            values.push_back(value);
            if (value == std::numeric_limits<std::int64_t>::max()) break;
        }
        static thread_local std::mt19937_64 generator(std::random_device{}());
        std::shuffle(values.begin(), values.end(), generator);
        PartList result;
        for (std::size_t i = 0; i < amount; ++i) {
            result.push_back(number(func.token, static_cast<double>(values[i])));
        }
        return list(func.token, std::move(result));
    }
    if (name == "concatstrings") {
        if (!argCount(prop, func, args, 1, 2, errors)) return string(func.token, {});
        const auto* source = requireList(prop, func, args, 0, errors);
        if (!source) return string(func.token, {});
        String separator;
        if (args.size() == 2) {
            const auto* value = requireString(prop, func, args, 1, errors);
            if (!value) return string(func.token, {});
            separator = value->strVal;
        }
        if (mode == EvalMode::TypeCheck) return string(func.token, {});
        String result;
        for (std::size_t i = 0; i < source->listVal.size(); ++i) {
            if (i) result += separator;
            result += evalValueDeep(prop, mode, info, source->listVal[i], errors)->stringValue();
        }
        return string(func.token, std::move(result));
    }
    if (name == "pickbyindices") {
        if (!argCount(prop, func, args, 2, 2, errors)) return list(func.token, {});
        const auto* source = requireList(prop, func, args, 0, errors);
        if (!source) return list(func.token, {});
        const auto* indices = requireList(prop, func, args, 1, errors);
        if (!source || !indices) return list(func.token, {});
        if (mode == EvalMode::TypeCheck) return list(func.token, {});
        ConstPartList result;
        for (const auto& raw : indices->listVal) {
            const auto value = evaluate(prop, mode, info, raw, errors);
            const auto* position = requireMinNumber(prop, func, ConstPartList{value}, 0, 1, mode, errors);
            if (!position || !std::isfinite(position->numVal)) return list(func.token, {});
            // size_tへの変換前に1始まりのインデックスを検査する。D
            // は先に切り下げたdoubleをキャストするが、size_t範
            // 囲外では安定した互換結果がないため、一定の範囲エラーを返す。
            if (std::floor(position->numVal) > static_cast<double>(source->listVal.size())) {
                addError(errors, "PICKBYINDICES index is out of range.", *value);
                return list(func.token, {});
            }
            result.push_back(source->listVal[checkedTruncatedInteger<std::size_t>(position->numVal) - 1]);
        }
        return listConst(func.token, std::move(result));
    }
    if (name == "inpolygon") {
        if (!argCount(prop, func, args, 2, 2, errors)) return boolean(func.token, false);
        const auto* polygon = asList(args[0]);
        const auto* point = asList(args[1]);
        if (!polygon || !point) return boolean(func.token, false);
        if (mode == EvalMode::TypeCheck) return boolean(func.token, false);
        const auto evaluatedPolygon = evalListValue(prop, mode, info, *polygon, errors);
        const auto evaluatedPoint = evalListValue(prop, mode, info, *point, errors);
        const auto* polygonValue = asList(evaluatedPolygon);
        const auto* pointValue = asList(evaluatedPoint);
        if (!polygonValue || !pointValue) return boolean(func.token, false);
        const auto numberValue = [&](const ConstPartPtr& value) {
            const auto converted = functionValue(prop, func, ConstPartList{value}, mode, errors);
            return asNumber(converted)->numVal;
        };
        const double px = pointValue->listVal.empty()
                              ? 0.0 : numberValue(pointValue->listVal[0]);
        const double py = pointValue->listVal.size() < 2
                              ? 0.0 : numberValue(pointValue->listVal[1]);
        std::vector<std::pair<double, double>> points;
        if (!polygonValue->listVal.empty() &&
            asNumber(polygonValue->listVal.front())) {
            if (polygonValue->listVal.size() % 2 != 0) return boolean(func.token, false);
            points.reserve(polygonValue->listVal.size() / 2);
            for (std::size_t i = 0; i < polygonValue->listVal.size(); i += 2) {
                points.emplace_back(numberValue(polygonValue->listVal[i]),
                                    numberValue(polygonValue->listVal[i + 1]));
            }
        } else {
            points.reserve(polygonValue->listVal.size());
            for (const auto& raw : polygonValue->listVal) {
                const auto* pairValue = asList(raw);
                if (!pairValue) return boolean(func.token, false);
                points.emplace_back(pairValue->listVal.empty()
                                        ? 0.0 : numberValue(pairValue->listVal[0]),
                                    pairValue->listVal.size() < 2
                                        ? 0.0 : numberValue(pairValue->listVal[1]));
            }
        }
        if (points.empty()) return boolean(func.token, false);
        bool inside = false;
        constexpr double epsilon = 1e-9;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto [x1, y1] = points[i];
            const auto [x2, y2] = points[(i + 1) % points.size()];
            if (std::abs(px - x1) < epsilon && std::abs(py - y1) < epsilon) return boolean(func.token, true);
            const double cross = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
            const double dot = (px - x1) * (px - x2) + (py - y1) * (py - y2);
            if (std::abs(cross) < epsilon && dot <= epsilon) return boolean(func.token, true);
            if ((y1 > py) != (y2 > py)) {
                const double x = x1 + (x2 - x1) * (py - y1) / (y2 - y1);
                if (px < x) inside = !inside;
            }
        }
        return boolean(func.token, inside);
    }
    if (name == "lgetrandom") {
        if (!argCount(prop, func, args, 1, 1, errors)) return unknown(func.token);
        const auto* source = requireList(prop, func, args, 0, errors);
        if (!source) return unknown(func.token);
        if (mode == EvalMode::TypeCheck) return unknown(func.token);
        if (source->listVal.empty()) {
            addError(errors, "LGETRANDOM list is empty.", *args[0]);
            return unknown(func.token);
        }
        static thread_local std::mt19937_64 generator(std::random_device{}());
        std::uniform_int_distribution<std::size_t> distribution(0, source->listVal.size() - 1);
        return evaluate(prop, mode, info, source->listVal[distribution(generator)], errors);
    }
    if (name == "lcount") {
        if (!argCount(prop, func, args, 2, 2, errors)) return number(func.token, 0);
        const auto* source = asList(args[0]);
        if (!source) return number(func.token, 0);
        std::size_t count = 0;
        const auto target = evalValueDeep(prop, mode, info, args[1], errors);
        for (const auto& raw : source->listVal) {
            const auto value = evalValueDeep(prop, mode, info, raw, errors);
            ExprErrors comparison_errors;
            if (partEqual(prop, mode, info, value, target, comparison_errors)) ++count;
        }
        return number(func.token, static_cast<double>(count));
    }
    if (name == "lunique") {
        if (!argCount(prop, func, args, 1, 1, errors)) return list(func.token, {});
        const auto* source = asList(args[0]);
        if (!source) {
            if (!requireString(prop, func, args, 0, errors)) return boolean(func.token, false);
            return boolean(func.token, mode != EvalMode::TypeCheck);
        }
        if (mode == EvalMode::TypeCheck) return list(func.token, {});
        ConstPartList result;
        std::set<String> seen;
        for (const auto& raw : source->listVal) {
            const auto value = evaluate(prop, mode, info, raw, errors);
            if (asList(value) || asStructure(value) || isUnknown(value)) {
                result.push_back(raw);
                continue;
            }
            String key;
            if (const auto* numberValue = asNumber(value)) {
                // DはnumVal.to!string（有効数字6桁の%g）で
                // 数値のキーを作る。NumberValue::stringValue
                // は小数点以下8桁に丸めたプレビュー文字列なので、Dが区
                // 別する値（1e-9と2e-9など）を同じにしてしまう。
                key = "N" + lib::scalar_format::floating_to_string(numberValue->numVal);
            } else {
                key = (asString(value) ? "S" : "B") + value->stringValue();
            }
            if (seen.insert(std::move(key)).second) result.push_back(raw);
        }
        return listConst(func.token, std::move(result));
    }
    if (name == "contain") {
        if (!argCount(prop, func, args, 2, 2, errors)) return boolean(func.token, false);
        const auto* rectangle = requireList(prop, func, args, 0, errors);
        const auto* point = requireList(prop, func, args, 1, errors);
        if (!rectangle || !point || rectangle->listVal.size() < 4 || point->listVal.size() < 2) return boolean(func.token, false);
        if (mode == EvalMode::TypeCheck) return boolean(func.token, false);
        std::array<double, 6> values{};
        ConstPartList rectangle_values, point_values;
        for (std::size_t i = 0; i < 4; ++i)
            rectangle_values.push_back(evaluate(prop, mode, info, rectangle->listVal[i], errors));
        for (std::size_t i = 0; i < 2; ++i)
            point_values.push_back(evaluate(prop, mode, info, point->listVal[i], errors));
        bool valid = true;
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto* value = requireNumber(prop, func, i < 4 ? rectangle_values : point_values,
                i < 4 ? i : i - 4, errors);
            if (!value) valid = false;
            else values[i] = value->numVal;
        }
        if (!valid) return boolean(func.token, false);
        return boolean(func.token, values[0] <= values[4] && values[4] <= values[2] &&
                                  values[1] <= values[5] && values[5] <= values[3]);
    }
    // Editorには変更可能なリスト変数の保存領域がな
    // い。全引数を検証し、外部状態を変えずに仕様上の成功
    // フラグを返すことで、Dモジュールの契約を維持する。
    if (name == "lsetmulti") {
        if (!argCount(prop, func, args, 3, 3, errors)) return boolean(func.token, false);
        const auto* variable = requireString(prop, func, args, 0, errors);
        const auto* indices = requireList(prop, func, args, 1, errors);
        const auto* values = requireList(prop, func, args, 2, errors);
        if (!variable || !indices || !values || indices->listVal.size() != values->listVal.size()) {
            return boolean(func.token, false);
        }
        if (mode == EvalMode::TypeCheck) return boolean(func.token, false);
        for (const auto& raw : indices->listVal) {
            const auto value = evaluate(prop, mode, info, raw, errors);
            const auto converted = functionValue(prop, func, ConstPartList{value}, mode, errors);
            const auto* number = asNumber(converted);
            if (!number) return boolean(func.token, false);
            const double numberValue = number->numVal;
            if (numberValue <= 0) return boolean(func.token, false);
        }
        return boolean(func.token, true);
    }
    if (name == "lremove") {
        if (!argCount(prop, func, args, 2, 2, errors)) return boolean(func.token, false);
        const auto* variable = requireString(prop, func, args, 0, errors);
        if (!variable) return boolean(func.token, false);
        const auto validateIndex = [&](const ConstPartPtr& raw) {
            const auto value = evaluate(prop, mode, info, raw, errors);
            const auto converted = functionValue(prop, func, ConstPartList{value}, mode, errors);
            return asNumber(converted)->numVal > 0;
        };
        if (const auto* indices = asList(args[1])) {
            for (const auto& raw : indices->listVal) {
                if (!validateIndex(raw)) return boolean(func.token, false);
            }
        } else if (!requireMinNumber(prop, func, args, 1, 1, mode, errors)) {
            return boolean(func.token, false);
        }
        return boolean(func.token, mode != EvalMode::TypeCheck);
    }
    if (name == "lremovemulti") {
        if (!argCount(prop, func, args, 2, 2, errors)) return boolean(func.token, false);
        const auto* variable = requireString(prop, func, args, 0, errors);
        const auto* indices = requireList(prop, func, args, 1, errors);
        if (!variable || !indices) return boolean(func.token, false);
        if (mode == EvalMode::TypeCheck) return boolean(func.token, false);
        for (const auto& raw : indices->listVal) {
            const auto value = evaluate(prop, mode, info, raw, errors);
            const auto converted = functionValue(prop, func, ConstPartList{value}, mode, errors);
            if (asNumber(converted)->numVal <= 0) return boolean(func.token, false);
        }
        return boolean(func.token, true);
    }
    if (name == "linsert") {
        if (!argCount(prop, func, args, 3, 3, errors)) return boolean(func.token, false);
        const auto* variable = requireString(prop, func, args, 0, errors);
        const auto* position = requireMinNumber(prop, func, args, 1, 1, mode, errors);
        if (!variable || !position) {
            return boolean(func.token, false);
        }
        return boolean(func.token, mode != EvalMode::TypeCheck);
    }
    if (name == "lremoveall") {
        if (!argCount(prop, func, args, 2, 2, errors)) return boolean(func.token, false);
        const auto* variable = requireString(prop, func, args, 0, errors);
        if (!variable) return boolean(func.token, false);
        return boolean(func.token, mode != EvalMode::TypeCheck);
    }
    if (name == "lset") {
        // DのEditor実装は意図的に検証だけを行い、コモン
        // へ書き込まない。式の配送処理でも同じ動作を共有する。
        if (!argCount(prop, func, args, 3, 3, errors)) return boolean(func.token, false);
        const auto* variable = requireString(prop, func, args, 0, errors);
        if (!variable) return boolean(func.token, false);
        const auto validateIndex = [&](const ConstPartPtr& raw) {
            const auto value = evaluate(prop, mode, info, raw, errors);
            const auto* number = asNumber(value);
            return number && !(number->numVal < 1);
        };
        if (const auto* indices = asList(args[1])) {
            for (const auto& raw : indices->listVal) {
                if (!validateIndex(raw)) return boolean(func.token, false);
            }
        } else if (!requireMinNumber(prop, func, args, 1, 1, mode, errors)) {
            return boolean(func.token, false);
        }
        return boolean(func.token, mode != EvalMode::TypeCheck);
    }
    addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorFunctionIsNotDefined, "Function is not defined: %1$s", func.funcName), func);
    return number(func.token, 0);
}

}  // namespace

ConstPartPtr Function::call(const CProps* prop, EvalMode mode,
                           const VariableInfo& info, ExprErrors& errors) const {
    const auto name = lower(funcName);
    // Dの評価器ではLISTを遅延評価する。結果のリス
    // トを使うまで、引数は式のPartのまま保持する。
    if (name == "list") {
        return dispatchFunction(prop, mode, info, *this, args, errors, funcName);
    }
    if (name == "if") return callIfLazy(prop, mode, info, *this, args, errors);
    if (name == "and_d") return callAndDLazy(prop, mode, info, *this, args, errors);
    if (name == "or_d") return callOrDLazy(prop, mode, info, *this, args, errors);
    if (name == "switch") return callSwitchLazy(prop, mode, info, *this, args, errors);
    if (name == "for") return callForLazy(prop, mode, info, *this, args, errors);
    ConstPartList evaluated;
    evaluated.reserve(args.size());
    for (const auto& argument : args) {
        const auto errorCount = errors.size();
        evaluated.push_back(evaluate(prop, mode, info, argument, errors));
        if (mode == EvalMode::All && errorCount < errors.size()) {
            return unknown(token);
        }
    }
    return dispatchFunction(prop, mode, info, *this, evaluated, errors, funcName);
}

ConstPartPtr evalFunctionPart(const CProps* prop, EvalMode mode,
                              const VariableInfo& info, ConstPartPtr arg,
                              ExprErrors& errors) {
    if (const auto function = std::dynamic_pointer_cast<const Function>(arg)) return function->call(prop, mode, info, errors);
    if (const auto expression = std::dynamic_pointer_cast<const Expr>(arg)) return calculate(prop, mode, info, expression->parts, errors);
    // Dはほかの全引数をPart.toValueObjで処理する。この補助処理では、単独
    // のネイティブOperator/UnaryOperatorを式の木とはみなさない。
    return requiredPart(arg).toValueObj(prop, errors);
}

ConstPartPtr evalListElement(const CProps* prop, EvalMode mode,
                             const VariableInfo& info, const ListValue& source,
                             std::size_t index, ExprErrors& errors) {
    return evalFunctionPart(prop, mode, info, source.listVal.at(index), errors);
}

ConstPartPtr evalListValue(const CProps* prop, EvalMode mode,
                           const VariableInfo& info, const ListValue& source,
                           ExprErrors& errors) {
    ConstPartList values;
    for (std::size_t i = 0; i < source.listVal.size(); ++i) {
        auto value = evalListElement(prop, mode, info, source, i, errors);
        if (const auto* nested = asList(value)) {
            values.push_back(evalListValue(prop, mode, info, *nested, errors));
        } else {
            values.push_back(std::move(value));
        }
    }
    return std::make_shared<ListValue>(source.token, std::move(values));
}

ConstPartPtr evalValueDeep(const CProps* prop, EvalMode mode,
                           const VariableInfo& info, ConstPartPtr value,
                           ExprErrors& errors) {
    auto evaluated = evaluate(prop, mode, info, std::move(value), errors);
    if (const auto* source = asList(evaluated)) return evalListValue(prop, mode, info, *source, errors);
    if (const auto* source = asStructure(evaluated)) {
        ConstPartList values;
        for (const auto& item : source->structVal) values.push_back(evalValueDeep(prop, mode, info, item, errors));
        return std::make_shared<StructureValue>(source->token, source->structName, std::move(values));
    }
    return evaluated;
}

bool equalsFunctionValue(const CProps* prop, EvalMode mode,
                         const VariableInfo& info, ConstPartPtr lhs, ConstPartPtr rhs,
                         ExprErrors& errors) {
    return partEqual(prop, mode, info, std::move(lhs), std::move(rhs), errors);
}

ConstPartPtr calculate(const CProps* prop, EvalMode mode, const VariableInfo& info,
                       ConstPartView expr, ExprErrors& errors) {
    if (expr.empty()) return number({}, 0);
    if (expr.size() == 1) return evaluate(prop, mode, info, expr.front(), errors);
    // 解析済みの式はDの後置表現を使う。明示的に構築されたネイティブのOperator/UnaryOperator
    // の木のノードにも、引き続き対応する。
    ConstPartList stack;
    for (const auto& item : expr) {
        if (const auto* unary = dynamic_cast<const UnaryOperator*>(item.get())) {
            if (stack.empty()) { addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorInvalidSemantics, "Invalid semantics."), *item); return number(item->token, 0); }
            auto rhs = stack.back();
            stack.pop_back();
            stack.push_back(unary->call(prop, evaluate(prop, mode, info, rhs, errors), errors));
        } else if (const auto* operation = dynamic_cast<const Operator*>(item.get())) {
            if (operation->lhs || operation->rhs) {
                stack.push_back(evaluate(prop, mode, info, item, errors));
            } else if (stack.size() >= 2) {
                auto rhs = stack.back(); stack.pop_back();
                auto lhs = stack.back(); stack.pop_back();
                lhs = evaluate(prop, mode, info, lhs, errors);
                if (operation->operator_ != ".") rhs = evaluate(prop, mode, info, rhs, errors);
                stack.push_back(operation->call(prop, mode, info, lhs, rhs, errors));
            } else {
                addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorInvalidSemantics, "Invalid semantics."), *item);
                return number(item->token, 0);
            }
        } else {
            stack.push_back(dynamic_cast<const Symbol*>(item.get()) ? item : evaluate(prop, mode, info, item, errors));
        }
    }
    return stack.empty() ? number({}, 0) : evaluate(prop, mode, info, stack.back(), errors);
}

VariantVal eval(const CProps* prop, EvalMode mode, const VariableInfo& info,
                StringView text) {
    ExprErrors errors;
    const auto parsed = parseExpression(prop, text, errors);
    if (!errors.empty()) throw ExprException(__FILE__, __LINE__, String(text), std::move(errors));
    if (parsed.empty()) return VariantVal::invalidValue();
    auto result = calculate(prop, mode, info, parsed, errors);
    if (!errors.empty()) {
        throw ExprException(__FILE__, __LINE__, String(text), std::move(errors));
    }
    result = evalValueDeep(prop, mode, info, std::move(result), errors);
    if (!errors.empty()) {
        throw ExprException(__FILE__, __LINE__, String(text), std::move(errors));
    }
    return partToVariantVal(std::move(result));
}

ConstPartPtr callIfLazy(const CProps* prop, EvalMode mode, const VariableInfo& info,
                        const Function& func, ConstPartView args, ExprErrors& errors) {
    if (!argCount(prop, func, args, 3, 3, errors)) return number(func.token, 0);
    auto condition = evaluate(prop, mode, info, args[0], errors);
    const auto* value = asBoolean(condition);
    if (!value) {
        if (isUnknown(condition)) {
            return evaluate(prop, mode, info, args[2], errors);
        }
        addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorArgumentIsNotBoolean,
            "Argument is not boolean: %1$s, %2$s", upper(func.funcName), 1), *args[0]);
        return unknown(func.token);
    }
    return evaluate(prop, mode, info, args[value->boolVal ? 1 : 2], errors);
}

ConstPartPtr callAndDLazy(const CProps* prop, EvalMode mode, const VariableInfo& info,
                          const Function& func, ConstPartView args, ExprErrors& errors) {
    if (!argCount(prop, func, args, 1, std::numeric_limits<std::size_t>::max(), errors)) return boolean(func.token, false);
    for (std::size_t index = 0; index < args.size(); ++index) {
        const auto& raw = args[index];
        const auto value = evaluate(prop, mode, info, raw, errors);
        const auto* booleanValue = asBoolean(value);
        if (!booleanValue) {
            if (isUnknown(value)) return boolean(func.token, false);
            addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorArgumentIsNotBoolean,
                "Argument is not boolean: %1$s, %2$s", upper(func.funcName), index + 1), *raw);
            return boolean(func.token, false);
        }
        if (!booleanValue->boolVal) return boolean(func.token, false);
    }
    return boolean(func.token, true);
}

ConstPartPtr callOrDLazy(const CProps* prop, EvalMode mode, const VariableInfo& info,
                         const Function& func, ConstPartView args, ExprErrors& errors) {
    if (!argCount(prop, func, args, 1, std::numeric_limits<std::size_t>::max(), errors)) return boolean(func.token, false);
    for (std::size_t index = 0; index < args.size(); ++index) {
        const auto& raw = args[index];
        const auto value = evaluate(prop, mode, info, raw, errors);
        const auto* booleanValue = asBoolean(value);
        if (!booleanValue) {
            if (isUnknown(value)) continue;
            addError(errors, expressionMessage(prop, &msgs::Msgs::expressionErrorArgumentIsNotBoolean,
                "Argument is not boolean: %1$s, %2$s", upper(func.funcName), index + 1), *raw);
            return boolean(func.token, false);
        }
        if (booleanValue->boolVal) return boolean(func.token, true);
    }
    return boolean(func.token, false);
}

ConstPartPtr callSwitchLazy(const CProps* prop, EvalMode mode, const VariableInfo& info,
                            const Function& func, ConstPartView args, ExprErrors& errors) {
    if (!argCount(prop, func, args, 3, 3, errors)) return unknown(func.token);
    const auto control = [&](const ConstPartPtr& value) -> ConstPartPtr {
        ExprErrors localErrors;
        try {
            auto result = evaluate(prop, mode, info, value, localErrors);
            return localErrors.empty() ? result : unknown(value->token);
        } catch (const std::bad_alloc&) { throw; }
          catch (const std::exception&) { return unknown(value->token); }
    };
    const auto index = control(args[0]);
    const auto* numberValue = asNumber(index);
    if (!numberValue || !std::isfinite(numberValue->numVal) || numberValue->numVal < 1 ||
        numberValue->numVal != std::floor(numberValue->numVal))
        return evaluate(prop, mode, info, args[1], errors);
    const auto source = control(args[2]);
    const auto* listValue = asList(source);
    if (!listValue || numberValue->numVal > static_cast<double>(listValue->listVal.size()))
        return evaluate(prop, mode, info, args[1], errors);
    const auto position = checkedTruncatedInteger<std::size_t>(numberValue->numVal) - 1;
    return evalListElement(prop, mode, info, *listValue, position, errors);
}

ConstPartPtr callForLazy(const CProps* prop, EvalMode mode, const VariableInfo& info,
                         const Function& func, ConstPartView args, ExprErrors& errors) {
    // DはVariableInfoを値で渡す。コールバック前に全デリゲート項目を保持する。
    const VariableInfo context = info;
    if (!argCount(prop, func, args, 3, 3, errors)) return list(func.token, {});
    const auto sourceErrorCount = errors.size();
    const auto source = evalFunctionPart(prop, mode, context, args[0], errors);
    if (sourceErrorCount < errors.size()) return list(func.token, {});
    const auto specificationErrorCount = errors.size();
    const auto specification = evalFunctionPart(prop, mode, context, args[1], errors);
    if (specificationErrorCount < errors.size()) return list(func.token, {});
    std::vector<String> variableNames;
    bool multipleVariables = false;
    if (const auto* name = asString(specification)) variableNames.push_back(name->strVal);
    else if (const auto* names = asList(specification)) {
        multipleVariables = true;
        const auto nameCount = names->listVal.size();
        for (std::size_t index = 0; index < nameCount; ++index) {
            const auto value = evalListElement(prop, mode, context, *names, index, errors);
            const auto* elementName = requireString(prop, func, ConstPartList{value}, 0, errors);
            if (!elementName) return list(func.token, {});
            variableNames.push_back(elementName->strVal);
        }
    } else {
        (void)requireString(prop, func, ConstPartList{specification}, 0, errors);
        return list(func.token, {});
    }
    for (const auto& name : variableNames) {
        if (enumutils::stripWhiteSpace(name).empty()) {
            addError(errors, "FOR variable name is empty.", requiredPart(specification));
            return list(func.token, {});
        }
    }
    std::shared_ptr<ListValue> generatedList;
    const ListValue* loopList = nullptr;
    if (!multipleVariables && asNumber(source)) {
        const auto* count = asNumber(source);
        if (count->numVal < 0) {
            addError(errors, "FOR count must be zero or greater.", requiredPart(source));
            return list(func.token, {});
        }
        const auto countValue = checkedTruncatedInteger<std::int64_t>(std::floor(count->numVal));
        PartList numbers;
        for (std::int64_t i = 0; i < countValue; ++i) {
            numbers.push_back(number(func.token, static_cast<double>(i + 1)));
        }
        generatedList = std::make_shared<ListValue>(func.token, std::move(numbers));
        loopList = generatedList.get();
    }
    if (!loopList) {
        loopList = requireList(prop, func, ConstPartList{source}, 0, errors);
        if (!loopList) return list(func.token, {});
    }
    // Dは配列記述子を取得し、共有する要素の領域を保持する。source/generatedList
    // は、この同期巡回中のネイティブ領域を保持する。公開されたネイティブ
    // vectorのサイズ変更は、Dのconstなリスト項目の契約外。
    const ConstPartView loopValues = loopList->listVal;
    // コールバック前にDのデリゲートハンドルを固定する。キャプチャされたネイテ
    // ィブ所有元は共有したまま。コールバックは入力infoの項目を置換し得る。
    const auto originalExistsVariant = context.existsVariant;
    const auto originalVariantValue = context.variantValue;
    std::unordered_map<String, VariantVal> temporary;
    std::unordered_map<String, bool> writable;
    for (const auto& name : variableNames) {
        writable[name] = originalExistsVariant && originalExistsVariant(name);
    }
    VariableInfo loopInfo(context,
        [&temporary, originalExistsVariant](const String& name) {
            return temporary.find(name) != temporary.end() ||
                   (originalExistsVariant && originalExistsVariant(name));
        },
        [&temporary, originalVariantValue](const String& name) {
            const auto found = temporary.find(name);
            if (found != temporary.end()) return found->second;
            return originalVariantValue ? originalVariantValue(name) : VariantVal::invalidValue();
        });
    ConstPartList result;
    for (std::size_t i = 0; i < loopValues.size(); ++i) {
        // DはString指定とList指定を区別し、LIST("i")もList
        // として扱う。一要素のListでも各入力行を分解して検証する。
        if (!multipleVariables) {
            if (!variableNames.empty() && writable[variableNames[0]]) {
                const auto before = errors.size();
                const auto value = evalValueDeep(prop, mode, loopInfo, loopValues[i], errors);
                if (before < errors.size()) return list(func.token, {});
                temporary[variableNames[0]] = partToVariantVal(value);
            }
        } else {
            const auto before = errors.size();
            const auto rowValue = evalListElement(prop, mode, loopInfo,
                                                  *loopList, i, errors);
            if (before < errors.size()) return list(func.token, {});
            const auto* tuple = asList(rowValue);
            if (!tuple) {
                (void)requireList(prop, func, ConstPartList{rowValue}, 0, errors);
                return list(func.token, {});
            }
            const auto assignCount = std::min(tuple->listVal.size(), variableNames.size());
            const ConstPartView rowValues = tuple->listVal;
            for (std::size_t j = 0; j < assignCount; ++j) {
                if (!writable[variableNames[j]]) continue;
                const auto itemBefore = errors.size();
                const auto item = evalValueDeep(prop, mode, loopInfo, rowValues[j], errors);
                if (itemBefore < errors.size()) return list(func.token, {});
                temporary[variableNames[j]] = partToVariantVal(item);
            }
        }
        const auto resultBefore = errors.size();
        result.push_back(evalFunctionPart(prop, mode, loopInfo, args[2], errors));
        if (resultBefore < errors.size()) return list(func.token, {});
    }
    return std::make_shared<ListValue>(func.token, std::move(result));
}

namespace {
template <class Message>
bool checkWsnVersionImpl(const VariableInfo& info, const Part* part,
                         StringView version, const Message& message, ExprErrors& errors) {
    // ネイティブの呼び出し側向けにboolも返す。共通検査が
    // 診断を出すかどうかは変えず、Dの診断条件を報告する。
    if (!info.isTargetWsnVersion || info.isTargetWsnVersion(String(version))) return true;
    const auto text = message();
    // Dは診断が必要な場合だけpart.tokenを読む。
    if (!part) throw std::invalid_argument("null expression version diagnostic part");
    addError(errors, text, *part);
    return false;
}
} // namespace

bool checkWsnVersion(const VariableInfo& info, ConstPartPtr part, StringView version,
                     StringView message, ExprErrors& errors) {
    return checkWsnVersionImpl(info, part.get(), version,
                              [&] { return String(message); }, errors);
}

bool checkWsnVersionForFunctionExists(const CProps* prop, const VariableInfo& info,
                                      const Function& func, StringView version,
                                      ExprErrors& errors) {
    return checkWsnVersionImpl(info, &func, version, [&] {
        return expressionMessage(prop, &msgs::Msgs::expressionErrorFunctionIsNotExistsInTargetVersion,
                                 "Function %1$s is not exists in Wsn.%2$s.", func.funcName, version);
    }, errors);
}

#define CWX_EXPRESSION_FUNCTIONS(X) \
    X(Max) X(Min) X(Len) X(Find) X(Left) X(Right) X(Mid) X(Str) X(Value) X(Int) \
    X(IsStr) X(IsNumber) X(IsList) X(Var) X(FlagValue) X(FlagText) X(StepValue) \
    X(StepText) X(StepMax) X(Dice) X(Selected) X(LatestUseCardOwner) X(CharExists) \
    X(CastType) X(CastName) X(FindCoupon) X(CouponText) X(FindGossip) X(GossipText) \
    X(PartyName) X(List) X(List_e) X(At) X(LLen) X(LFind) X(LLeft) X(LRight) X(LMid) \
    X(PartyMoney) X(PartyNumber) X(YadoName) X(SkinType) X(BattleRound) X(CastLevel) \
    X(CouponValue) X(LifeRatio) X(StatusValue) X(StatusRound) X(SelectedCard) \
    X(CardName) X(CardType) X(CardRarity) X(CardPrice) X(CardLevel) X(CardCount) \
    X(FindKeyCode) X(KeyCodeText) X(Floor) X(Ceil) X(Sum) X(Eval) X(Format) X(Sind) \
    X(Cosd) X(Tand) X(Distance) X(ParseFloat) X(ParseInt) X(ScenarioName) \
    X(CardMaxUseLimit) X(Datetime) X(Unixtime) X(GetVariantValueOfCard) \
    X(GetFlagValueOfCard) X(GetStepValueOfCard) X(HasVariantValue) X(HasStep) X(HasFlag) \
    X(SetVariantValueOfCard) X(SetStepValueOfCard) X(SetFlagValueOfCard) X(MouseX) X(MouseY) \
    X(MakeList) X(LFindRand) X(GetRandomList) X(ConcatStrings) X(PickByIndices) \
    X(InPolygon) X(LGetRandom) X(LSetMulti) X(LRemove) X(LRemoveMulti) X(LInsert) \
    X(LUnique) X(LRemoveAll) X(LCount) X(LSet) X(Contain)

// 名前付きの入口は、対応するDの関数を明示的に選ぶ。Function
// の表記は診断の情報として維持する。評価も同じ配送
// 実装を使い、別の組み込み関数の実装を作らない。
#define CWX_DEFINE_EXPRESSION_FUNCTION(name) \
    ConstPartPtr func##name(const CProps* prop, EvalMode mode, const VariableInfo& info, \
                            const Function& func, ConstPartView args, ExprErrors& errors) { \
        return dispatchFunction(prop, mode, info, func, args, errors, #name); \
    }
CWX_EXPRESSION_FUNCTIONS(CWX_DEFINE_EXPRESSION_FUNCTION)
#undef CWX_DEFINE_EXPRESSION_FUNCTION
#undef CWX_EXPRESSION_FUNCTIONS

}  // namespace cwxeditor::cwx::expression
