#pragma once

#include "cwxeditor_src/core/cwx/filesync.hpp"
#include "cwxeditor_src/core/cwx/exception_diagnostics.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/array_slice.hpp"
#include "cwxeditor_src/lib/real80.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <sstream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <tuple>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::binary { class RawFile; }

namespace cwxeditor::cwx::utils {

using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;
using Bytes = ::cwxeditor::lib::Bytes;
using ByteView = ::cwxeditor::lib::ByteView;
using Path = std::filesystem::path;
using c_int = std::int32_t;
using c_uint = std::uint32_t;

inline const String DR =
#if defined(NDEBUG)
    "Release";
#else
    "Debug";
#endif
inline const String SYMBOL_INCLUDE{};
inline const String APP_BUILD = String("Build: ") + __DATE__ + " " + __TIME__;

inline String debugLog = "cwxeditor_error.log";
inline std::ofstream debugLogFile;
inline bool initDebugLogFile = false;
#if defined(_WIN32)
inline const String altDirSeparator = "/";
#else
inline const String altDirSeparator = " ";
#endif
inline constexpr StringView curdir = ".";
inline constexpr StringView pardir = "..";

[[nodiscard]] String printStackTrace();
void clearStackTrace() noexcept;
[[nodiscard]] String printStackTraceNoError();

namespace detail {

template <typename Integer>
[[nodiscard]] std::optional<Integer> parseDInteger(StringView value) {
    static_assert(std::is_integral_v<Integer> && !std::is_same_v<Integer, bool>);
    // Phobosのstd.conv.to!intは先頭の符号を一つ認めるが、符号なし変換は数
    // 字だけを受け付ける。 どちらも空白や未消費の接尾部は許さない。 https://github.com/dlang/phobos/blob/v2.070.0/std/conv.d
    //
    if (value.empty()) return std::nullopt;
    std::size_t digits = 0;
    if constexpr (std::is_signed_v<Integer>) {
        if (value.front() == '+' || value.front() == '-') digits = 1;
    }
    if (digits == value.size() || !std::all_of(value.begin() + digits, value.end(),
            [](char c) { return c >= '0' && c <= '9'; })) return std::nullopt;
    if (value.front() == '+') value.remove_prefix(1);
    Integer result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) return std::nullopt;
    return result;
}

[[nodiscard]] inline std::optional<bool> parseDBool(StringView value) {
    // std.conv.to!boolは大文字・小文字を区別しないtrue/falseを受け付ける。
    // Dの別関数cwx.utils.parseBoolは0/1も認めるため、二つの契約を分ける。
    const auto matches = [value](StringView spelling) {
        return value.size() == spelling.size() && std::equal(value.begin(), value.end(), spelling.begin(),
            [](char input, char expected) {
                if (input >= 'A' && input <= 'Z') input = static_cast<char>(input + ('a' - 'A'));
                return input == expected;
            });
    };
    if (matches("true")) return true;
    if (matches("false")) return false;
    return std::nullopt;
}

[[nodiscard]] inline std::recursive_mutex& debugMutex() {
    // Dのsynchronizedモニターは再入可能。外側のfdebuglnがモ
    // ニターを保持中でも、 独自の書式処理が入れ子の診断を出力できるようにする。
    static std::recursive_mutex mutex;
    return mutex;
}

[[nodiscard]] String debugValue(StringView value);
[[nodiscard]] String debugValue(const char* value);
[[nodiscard]] String debugValue(char value);
[[nodiscard]] String debugValue(bool value);

template <typename T>
[[nodiscard]] String debugValue(const T& value) {
    if constexpr (std::is_convertible_v<const T&, const std::exception&>) {
        // 呼び出し元が基底クラス参照で保持している場合も、実際の例外型の情報を残す。
        return debugExceptionValue(value);
    } else {
        std::ostringstream stream;
        stream << value;
        return stream.str();
    }
}

template <typename T>
[[nodiscard]] String debugValue(T* value) {
    std::ostringstream stream;
    stream << static_cast<const void*>(value);
    return stream.str();
}

template <typename... Values>
[[nodiscard]] String concatDebugValues(const Values&... values) {
    String result;
    (result.append(debugValue(values)), ...);
    return result;
}

template <typename T>
[[nodiscard]] String stringFrom(const T& value) {
    if constexpr (std::is_array_v<T> &&
                  std::is_same_v<std::remove_cv_t<std::remove_extent_t<T>>, char>) {
        return String(value);
    } else if constexpr (requires { value.data(); value.size(); }) {
        using Element = std::remove_cv_t<std::remove_reference_t<decltype(*value.data())>>;
        if constexpr (std::is_same_v<Element, char>) {
            return String(value.data(), value.size());
        } else {
            return String(std::begin(value), std::end(value));
        }
    } else {
        return String(std::begin(value), std::end(value));
    }
}

template <typename C>
[[nodiscard]] std::u32string naturalCodepointsView(std::basic_string_view<C> value) {
    static_assert(std::is_same_v<C, char> || std::is_same_v<C, char8_t> ||
                  std::is_same_v<C, char16_t> || std::is_same_v<C, char32_t> ||
                  std::is_same_v<C, wchar_t>, "natural comparison requires a character range");
    if constexpr (sizeof(C) == 1) {
        return enumutils::toUtf32(StringView(reinterpret_cast<const char*>(value.data()), value.size()));
    } else if constexpr (sizeof(C) == 2) {
        // wchar_tはOS依存の文字型だが、16ビットの二つの型には同じ厳密なUTF-16
        // デコーダーを使う。 wchar_tの格納領域を別の型で参照しない。
        return enumutils::toUtf32(std::u16string(value.begin(), value.end()));
    } else {
        // Dの同じ幅のconst dchar[]からdstringへの変換はコー
        // ド単位をコピーするだけで、 UTF-32入力への追加の検証は行わない。
        return std::u32string(value.begin(), value.end());
    }
}

template <typename T>
[[nodiscard]] std::u32string naturalCodepoints(const T& value) {
    if constexpr (std::is_array_v<T>) {
        using C = std::remove_cv_t<std::remove_extent_t<T>>;
        auto count = std::extent_v<T>;
        // ネイティブの文字列リテラルは、最後のC終端だけを除く
        // 。 内部のNULはD形式の文字範囲の一部として残す。
        if (count && value[count - 1U] == C{}) --count;
        return naturalCodepointsView(std::basic_string_view<C>(value, count));
    } else if constexpr (std::is_pointer_v<T>) {
        using C = std::remove_cv_t<std::remove_pointer_t<T>>;
        if (!value) throw std::invalid_argument("null natural-comparison string");
        return naturalCodepointsView(std::basic_string_view<C>(value));
    } else {
        using C = std::remove_cv_t<std::remove_reference_t<decltype(*value.data())>>;
        return naturalCodepointsView(std::basic_string_view<C>(value.data(), value.size()));
    }
}

[[nodiscard]] int naturalCompare(std::u32string_view first, std::u32string_view second,
                                 bool ignore_case, bool filename_case);

template <typename T>
[[nodiscard]] String formatValue(const T& value) {
    return debugValue(value);
}

[[nodiscard]] String formatCharacter(char32_t codepoint);

[[nodiscard]] inline String formatValue(char8_t value) {
    return String(1U, static_cast<char>(value));
}

[[nodiscard]] inline String formatValue(char16_t value) {
    return formatCharacter(static_cast<char32_t>(value));
}

[[nodiscard]] inline String formatValue(char32_t value) {
    return formatCharacter(value);
}

[[nodiscard]] inline String formatValue(wchar_t value) {
    return formatCharacter(static_cast<char32_t>(value));
}

[[nodiscard]] inline String formatValue(StringView value) {
    return String(value);
}

[[nodiscard]] inline String formatValue(const char* value) {
    return value == nullptr ? String{} : String(value);
}

template <typename Integer>
    requires (std::is_integral_v<Integer> && !std::is_same_v<Integer, bool>)
[[nodiscard]] String formatBinaryInteger(Integer value) {
    // Dの%bは、書式処理の最大整数型へ符号拡張せ
    // ず、 元の整数幅の符号なしビット表現を使う。
    using Unsigned = std::make_unsigned_t<Integer>;
    const auto bits = static_cast<std::uint64_t>(static_cast<Unsigned>(value));
    std::array<char, std::numeric_limits<std::uint64_t>::digits> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), bits, 2);
    if (converted.ec != std::errc{}) throw std::runtime_error("Binary format conversion failed");
    return String(buffer.data(), static_cast<std::size_t>(converted.ptr - buffer.data()));
}

[[nodiscard]] inline String formatBinaryInteger(bool value) {
    return value ? "1" : "0";
}

template <typename Character>
inline constexpr bool isRawFormatCharacter =
    std::is_same_v<std::remove_cv_t<Character>, char> ||
    std::is_same_v<std::remove_cv_t<Character>, char8_t> ||
    std::is_same_v<std::remove_cv_t<Character>, char16_t> ||
    std::is_same_v<std::remove_cv_t<Character>, char32_t> ||
    std::is_same_v<std::remove_cv_t<Character>, wchar_t>;

template <typename T>
inline constexpr bool isRawFormatScalarValue =
    std::is_arithmetic_v<std::remove_cv_t<T>> ||
    std::is_enum_v<std::remove_cv_t<T>> ||
    std::is_same_v<std::remove_cv_t<T>, ::cwxeditor::lib::Real80>;

template <typename Character>
void appendRawCharacterUnits(String& output, const Character* value,
                             const std::size_t count) {
    static_assert(isRawFormatCharacter<Character>);
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(Character))
        throw std::length_error("Raw format value is too large");
    if (count != 0U)
        output.append(reinterpret_cast<const char*>(value), count * sizeof(Character));
}

template <typename Integer>
void appendRawLittleEndian(String& output, Integer value) {
    static_assert(std::is_unsigned_v<Integer>);
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        output.push_back(static_cast<char>(value & static_cast<Integer>(0xffU)));
        value >>= 8U;
    }
}

template <typename T>
void appendRawScalarValue(String& output, const T& value) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, ::cwxeditor::lib::Real80>) {
        // Real80はEditorで明示的に表したDのrealであり、ネイティブABI
        // の構造体のダンプではない。 DMD 2.097 Win64の%rは、仮数、
        // 符号・指数の順にリトルエンディアンで80ビットを出力し、詰め物は含めない。
        const auto bits = value.bits();
        appendRawLittleEndian(output, bits.significand);
        appendRawLittleEndian(output, bits.signExp);
    } else if constexpr (std::is_enum_v<U>) {
        using Underlying = std::underlying_type_t<U>;
        const auto underlying = static_cast<Underlying>(value);
        output.append(reinterpret_cast<const char*>(&underlying), sizeof(underlying));
    } else if constexpr (std::is_arithmetic_v<U> && std::is_trivially_copyable_v<U>) {
        // Dの%rは、算術型引数のネイティブのバイトを出力する。呼び出し元のABIに
        // 従い、 wchar_tがDのwcharに対応するのは2バイトの環境だけ。
        output.append(reinterpret_cast<const char*>(&value), sizeof(U));
    } else {
        throw std::invalid_argument("D %r requires a native scalar value");
    }
}

template <typename Character, typename Traits, typename Allocator>
    requires isRawFormatCharacter<Character>
void appendRawFormattedValue(String& output,
                             const std::basic_string<Character, Traits, Allocator>& value) {
    appendRawCharacterUnits(output, value.data(), value.size());
}

template <typename Character, typename Traits>
    requires isRawFormatCharacter<Character>
void appendRawFormattedValue(String& output,
                             const std::basic_string_view<Character, Traits>& value) {
    appendRawCharacterUnits(output, value.data(), value.size());
}

template <typename Element, std::size_t Count>
void appendRawFormattedValue(String& output, const Element (&value)[Count]) {
    using Character = std::remove_cv_t<Element>;
    if constexpr (isRawFormatCharacter<Character>) {
        auto size = Count;
        // Dの文字列には、ネイティブの文字列リテラルにあるCの終端を含めない。
        if (size != 0U && value[size - 1U] == Character{}) --size;
        appendRawCharacterUnits(output, value, size);
    } else if constexpr (isRawFormatScalarValue<Character> &&
                         std::is_trivially_copyable_v<Character>) {
        for (const auto& element : value)
            appendRawScalarValue(output, element);
    } else {
        // %r以外の書式でも、他の配列引数で実体化できるようにする。 Dの
        // 集成体に対する%rの動作は未確認なので、直接の実行結果が得られる
        // までは 通常のtryFormatの例外時の代替処理に合わせる。
        throw std::invalid_argument("D %r requires a string or native scalar value");
    }
}

template <typename Value, typename Allocator>
void appendRawFormattedValue(String& output,
                             const std::vector<Value, Allocator>& values) {
    using Element = std::remove_cv_t<Value>;
    if constexpr (isRawFormatScalarValue<Element> &&
                  std::is_trivially_copyable_v<Element>) {
        for (const auto& element : values)
            appendRawScalarValue(output, static_cast<Element>(element));
    } else {
        // スカラー以外の要素を持つ集成体に対する、Dの%rの動作は未確認。
        throw std::invalid_argument("D %r requires a string or native scalar array");
    }
}

template <typename Value, std::size_t Count>
void appendRawFormattedValue(String& output, const std::array<Value, Count>& values) {
    using Element = std::remove_cv_t<Value>;
    if constexpr (isRawFormatScalarValue<Element> &&
                  std::is_trivially_copyable_v<Element>) {
        for (const auto& element : values)
            appendRawScalarValue(output, element);
    } else {
        // スカラー以外の要素を持つ集成体に対する、Dの%rの動作は未確認。
        throw std::invalid_argument("D %r requires a string or native scalar array");
    }
}

template <typename Value, std::size_t Extent>
void appendRawFormattedValue(String& output, const std::span<Value, Extent> values) {
    using Element = std::remove_cv_t<Value>;
    if constexpr (isRawFormatScalarValue<Element> &&
                  std::is_trivially_copyable_v<Element>) {
        for (const auto& element : values)
            appendRawScalarValue(output, element);
    } else {
        // スカラー以外の要素を持つ集成体に対する、Dの%rの動作は未確認。
        throw std::invalid_argument("D %r requires a string or native scalar array");
    }
}

template <typename T>
    requires (!std::is_array_v<std::remove_reference_t<T>>)
void appendRawFormattedValue(String& output, const T& value) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_pointer_v<U>) {
        using Character = std::remove_cv_t<std::remove_pointer_t<U>>;
        if constexpr (isRawFormatCharacter<Character>) {
            if (!value) throw std::invalid_argument("Null raw format string");
            appendRawCharacterUnits(output, value,
                std::char_traits<Character>::length(value));
        } else {
            throw std::invalid_argument("D %r requires a string or native scalar value");
        }
    } else {
        appendRawScalarValue(output, value);
    }
}

struct FormatRenderOptions {
    bool center{};
    bool grouping{};
    int groupingWidth{3};
    String groupingSeparator{","};
    StringView nested{};
    StringView rangeSeparator{};
    bool hasRangeSeparator{};
    bool suppressElementEscaping{};
    bool escapeStringArguments{};
};

template <typename T> struct is_format_range : std::false_type {};
template <typename T, typename Allocator>
struct is_format_range<std::vector<T, Allocator>> : std::true_type {};
template <typename C, typename Traits, typename Allocator>
struct is_format_range<std::basic_string<C, Traits, Allocator>> : std::true_type {};
template <typename C, typename Traits>
struct is_format_range<std::basic_string_view<C, Traits>> : std::true_type {};
template <typename T, std::size_t Count>
struct is_format_range<std::array<T, Count>> : std::true_type {};
template <typename T, std::size_t Extent>
struct is_format_range<std::span<T, Extent>> : std::true_type {};
template <typename Key, typename Value, typename Compare, typename Allocator>
struct is_format_range<std::map<Key, Value, Compare, Allocator>> : std::true_type {};
template <typename Key, typename Value, typename Hash, typename Equal, typename Allocator>
struct is_format_range<std::unordered_map<Key, Value, Hash, Equal, Allocator>>
    : std::true_type {};

template <typename T>
inline constexpr bool is_format_range_v = is_format_range<std::remove_cvref_t<T>>::value;
template <typename T>
inline constexpr bool is_format_range_argument_v =
    is_format_range_v<T> || std::is_array_v<std::remove_cvref_t<T>>;

template <typename T> struct is_format_map : std::false_type {};
template <typename K, typename V, typename C, typename A>
struct is_format_map<std::map<K, V, C, A>> : std::true_type {};
template <typename K, typename V, typename H, typename E, typename A>
struct is_format_map<std::unordered_map<K, V, H, E, A>> : std::true_type {};
template <typename T>
inline constexpr bool is_format_map_v = is_format_map<std::remove_cvref_t<T>>::value;

template <typename T, bool = std::is_array_v<T>>
struct format_range_element { using type = typename T::value_type; };
template <typename T>
struct format_range_element<T, true> { using type = std::remove_extent_t<T>; };

template <typename T> struct is_basic_string_format : std::false_type {};
template <typename C, typename Traits, typename A>
struct is_basic_string_format<std::basic_string<C, Traits, A>> : std::true_type {};
template <typename C, typename Traits>
struct is_basic_string_format<std::basic_string_view<C, Traits>> : std::true_type {};
template <typename T>
inline constexpr bool is_basic_string_format_v =
    is_basic_string_format<std::remove_cvref_t<T>>::value;
template <typename T>
inline constexpr bool is_text_format_value_v = [] {
    using U = std::remove_cvref_t<T>;
    if constexpr (is_basic_string_format_v<U>) return true;
    else if constexpr (std::is_pointer_v<U>)
        return std::is_same_v<std::remove_cv_t<std::remove_pointer_t<U>>, char>;
    else if constexpr (std::is_array_v<U>)
        return std::is_same_v<std::remove_cv_t<std::remove_extent_t<U>>, char>;
    else return false;
}();

template <typename Tuple>
void appendDFormatted(String& output, StringView format, const Tuple& values,
                      bool escapeStringArguments = false);

template <typename T>
[[nodiscard]] String renderRangeFormatted(StringView spec, const T& value,
                                          const FormatRenderOptions& options);

template <typename T>
[[nodiscard]] String renderFormattedValue(StringView spec, const T& value,
                                          const FormatRenderOptions& options = {}) {
    using U = std::remove_cvref_t<T>;
    if (spec.empty() || spec.front() != '%') {
        if constexpr (requires(std::ostringstream& stream) { stream << value; })
            return formatValue(value);
        else
            return String(spec);
    }
    if (spec.size() < 2U) {
        throw std::invalid_argument("Incomplete format specifier");
    }
    std::size_t index = 1U;
    bool left = false, show_sign = false, alternate = false, zero = false;
    bool center = options.center;
    bool space_sign = false;
    while (index < spec.size()) {
        switch (spec[index]) {
        case '-': left = true; ++index; continue;
        case '=': center = true; ++index; continue;
        case '+': show_sign = true; ++index; continue;
        case '#': alternate = true; ++index; continue;
        case '0': zero = true; ++index; continue;
        case ' ': space_sign = true; ++index; continue;
        default: break;
        }
        break;
    }
    int width = 0;
    while (index < spec.size() && std::isdigit(static_cast<unsigned char>(spec[index])))
        width = width * 10 + (spec[index++] - '0');
    int precision = -1;
    if (index < spec.size() && spec[index] == '.') {
        ++index;
        precision = 0;
        while (index < spec.size() && std::isdigit(static_cast<unsigned char>(spec[index])))
            precision = precision * 10 + (spec[index++] - '0');
    }
    if (index >= spec.size()) {
        throw std::invalid_argument("Incomplete format specifier");
    }
    const char conversion = spec[index];
    // DのformattedWriteは未知の変換文字にFormatException
    // を投げ、tryFormatは元の書式を返す。 %を削除したり値を黙っ
    // て表示したりしない。Dの整数変換にはprintfの%iは含まれない。
    if (conversion == 'i' ||
        (conversion != '(' && std::strchr("scduboxXeEfFgGaAr", conversion) == nullptr)) {
        throw std::invalid_argument("Unsupported format specifier");
    }

    if (conversion == 'r') {
        String result;
        appendRawFormattedValue(result, value);
        return result;
    }

    if (conversion == '(' || (conversion == 's' && is_format_range_argument_v<U>)) {
        if constexpr (is_format_range_argument_v<U>) {
            auto rendered = renderRangeFormatted(spec, value, options);
            if constexpr (is_format_range_argument_v<U>) {
                using Element = typename format_range_element<U>::type;
                if constexpr (std::is_same_v<std::remove_cv_t<Element>, char> ||
                              std::is_same_v<std::remove_cv_t<Element>, char8_t> ||
                              std::is_same_v<std::remove_cv_t<Element>, char16_t> ||
                              std::is_same_v<std::remove_cv_t<Element>, char32_t> ||
                              std::is_same_v<std::remove_cv_t<Element>, wchar_t>) {
                    if (conversion == 's' && precision >= 0 &&
                        rendered.size() > static_cast<std::size_t>(precision))
                        rendered.resize(static_cast<std::size_t>(precision));
                }
            }
            if (width > 0 && rendered.size() < static_cast<std::size_t>(width)) {
                const auto padding = static_cast<std::size_t>(width) - rendered.size();
                if (center) {
                    const auto leftPadding = left ? padding / 2U : (padding + 1U) / 2U;
                    rendered.insert(0U, leftPadding, ' ');
                    rendered.append(padding - leftPadding, ' ');
                } else if (left) rendered.append(padding, ' ');
                else rendered.insert(0U, padding, ' ');
            }
            return rendered;
        } else {
            throw std::invalid_argument("D compound format requires a range");
        }
    }

    std::ostringstream stream;
    if (left) stream << std::left;
    if (show_sign) stream << std::showpos;
    // Dの空白フラグは、非負の十進数・浮動小数の前に空白を付ける。 iostream
    // には対応する指定がないため、+を出力してから空白へ置き換える。
    const bool blank_sign = space_sign && !show_sign &&
        std::strchr("dieEfFgGaA", conversion) != nullptr;
    if (blank_sign) stream << std::showpos;
    if (alternate) stream << std::showbase;
    if (zero && !left) stream << std::setfill('0') << std::internal;
    // 幅は変換後に適用し、Dの中央揃えと整数の精度
    // も 他のスカラー値の書式と同じ経路で扱う。
    if (precision >= 0) stream << std::setprecision(precision);
    if (conversion == 'X' || conversion == 'E' || conversion == 'F' ||
        conversion == 'G' || conversion == 'A')
        stream << std::uppercase;
    if (conversion == 'x' || conversion == 'X') stream << std::hex;
    else if (conversion == 'o') stream << std::oct;
    else if (conversion == 'f' || conversion == 'F') stream << std::fixed;
    else if (conversion == 'e' || conversion == 'E') stream << std::scientific;
    // DのformattedWriteでは、%a/%Aは十六進の指数表記。
    else if (conversion == 'a' || conversion == 'A') stream << std::hexfloat;
    else if (conversion == 'g' || conversion == 'G') stream << std::defaultfloat;

    if (conversion == 'c') {
        if constexpr (std::is_same_v<U, char> || std::is_same_v<U, char8_t>) {
            stream << static_cast<char>(value);
        } else if constexpr (std::is_same_v<U, wchar_t> ||
                             std::is_same_v<U, char16_t> ||
                             std::is_same_v<U, char32_t>) {
            // Dはwchar/dcharをUTF-8で表示する。 charへ
            // キャストすると、非ASCIIの文字で残りのバイトが失われる。
            stream << formatCharacter(static_cast<char32_t>(value));
        } else {
            throw std::invalid_argument("D %c format requires a character value");
        }
        return stream.str();
    }

    if constexpr (std::is_same_v<U, std::nullptr_t>) {
        if (conversion != 's')
            throw std::invalid_argument("D null value only supports %s");
        stream << "null";
    } else if constexpr (std::is_enum_v<U>) {
        using Underlying = std::underlying_type_t<U>;
        const auto integer = static_cast<Underlying>(value);
        if (conversion == 's') {
            stream << ::cwxeditor::cwx::enumutils::enumToStringExact(value);
        } else if (conversion == 'b') {
            auto binary = formatBinaryInteger(integer);
            // Dの%bの整数精度は、最小の桁数を表す。
            if (precision >= 0 && binary.size() < static_cast<std::size_t>(precision))
                binary.insert(0U, static_cast<std::size_t>(precision) - binary.size(), '0');
            if (show_sign) binary.insert(0U, "+");
            stream << binary;
        } else if (conversion == 'u' || conversion == 'x' || conversion == 'X' ||
            conversion == 'o') {
            // Dの符号なし変換は引数の幅で行う。 負の列挙
            // 値は符号拡張せず、元の幅の下位ビットを保つ。
            using Unsigned = std::make_unsigned_t<Underlying>;
            stream << static_cast<unsigned long long>(
                static_cast<Unsigned>(integer));
        } else {
            stream << integer;
        }
    } else if constexpr (std::is_arithmetic_v<U>) {
        if (conversion == 's') stream << formatValue(value);
        else if constexpr (std::is_integral_v<U> &&
                           !std::is_same_v<U, bool>) {
            if (conversion == 'b') {
                auto binary = formatBinaryInteger(value);
                // Dの%bの整数精度は、最小の桁数を表す。
                if (precision >= 0 && binary.size() < static_cast<std::size_t>(precision))
                    binary.insert(0U, static_cast<std::size_t>(precision) - binary.size(), '0');
                if (show_sign) binary.insert(0U, "+");
                stream << binary;
            } else if (conversion == 'u' || conversion == 'x' || conversion == 'X' ||
                conversion == 'o') {
                // DのformattedWriteは%u/%x/%oを元の整数幅へ適用する。
                // 例えばint(-1)は4294967295またはffffffffとなる。
                using Unsigned = std::make_unsigned_t<U>;
                stream << static_cast<unsigned long long>(
                    static_cast<Unsigned>(value));
            } else if (conversion == 'd' || conversion == 'i') {
                stream << static_cast<long long>(value);
            } else if (std::strchr("eEfFgG", conversion) != nullptr &&
                       !std::is_same_v<U, char8_t> && !std::is_same_v<U, char16_t> &&
                       !std::is_same_v<U, char32_t> && !std::is_same_v<U, wchar_t>) {
                // Dは整数引数への浮動小数書式をrealとして処理す
                // る。42に対する%.2fは42.00、 %eは最短
                // の指数表記4.2e+01となり、精度なしの%f/%g
                // は整数のままになる。 通常のストリームでは整数
                // 値に変換指定が適用されないため、明示的に扱う。
                if (precision >= 0) {
                    stream << static_cast<long double>(value);
                } else if (conversion == 'e' || conversion == 'E') {
                    std::array<char, 64> scientific{};
                    const auto converted = std::to_chars(
                        scientific.data(), scientific.data() + scientific.size(),
                        static_cast<double>(value), std::chars_format::scientific);
                    if (converted.ec != std::errc{})
                        throw std::runtime_error("Scientific format conversion failed");
                    String text(scientific.data(), converted.ptr);
                    if (conversion == 'E')
                        for (auto& character : text)
                            character = static_cast<char>(std::toupper(
                                static_cast<unsigned char>(character)));
                    stream << text;
                } else if constexpr (std::is_same_v<U, char8_t> ||
                                     std::is_same_v<U, char16_t> ||
                                     std::is_same_v<U, char32_t> ||
                                     std::is_same_v<U, wchar_t>) {
                    stream << static_cast<unsigned long long>(value);
                } else {
                    stream << value;
                }
            } else {
                if constexpr (std::is_same_v<U, char8_t> ||
                              std::is_same_v<U, char16_t> ||
                              std::is_same_v<U, char32_t> ||
                              std::is_same_v<U, wchar_t>) {
                    stream << static_cast<unsigned long long>(value);
                } else {
                    stream << value;
                }
            }
        } else if (conversion == 'b') {
            // Dは浮動小数引数への%bを拒否し、tryFormatは書式をそのまま返す。
            if constexpr (std::is_floating_point_v<U>)
                throw std::invalid_argument("Binary format for a floating value");
            auto binary = formatBinaryInteger(value);
            // Dの%bの整数精度は、最小の桁数を表す。
            if (precision >= 0 && binary.size() < static_cast<std::size_t>(precision))
                binary.insert(0U, static_cast<std::size_t>(precision) - binary.size(), '0');
            if (show_sign) binary.insert(0U, "+");
            stream << binary;
        } else if (conversion == 'd' || conversion == 'u' ||
                   conversion == 'x' || conversion == 'X' || conversion == 'o') {
            // DのformattedWriteは浮動小数引数への整数変換をFormatException
            // で拒否し、 tryFormatは書式文字列を返
            // す。ここでキャストして任意の整数を表示しない。
            if constexpr (std::is_floating_point_v<U>)
                throw std::invalid_argument("Integer format for a floating value");
            else
                stream << static_cast<long long>(value);
        } else {
            stream << value;
        }
    } else if constexpr (std::is_pointer_v<U> &&
                         std::is_same_v<std::remove_cv_t<std::remove_pointer_t<U>>, char>) {
        if (conversion != 's')
            throw std::invalid_argument("Non-string format for a text value");
        const String text = value == nullptr ? String("null") : String(value);
        if (precision >= 0 && text.size() > static_cast<std::size_t>(precision))
            stream << text.substr(0U, static_cast<std::size_t>(precision));
        else
            stream << text;
    } else if constexpr (requires(std::ostringstream& output) { output << value; }) {
        // Dは文字列型の引数に対して、%s以外の変換をすべて拒否する。
        if (conversion != 's')
            throw std::invalid_argument("Non-string format for a text value");
        auto text = formatValue(value);
        if (conversion == 's' && precision >= 0) {
            // DのformattedWriteの精度は、UTF-8文字列をバイト単位で切
            // る。 複数バイトのコードポイントの途中になる場合も、その動作に合わせる。
            const auto byteLimit = static_cast<std::size_t>(precision);
            if (text.size() > byteLimit) text.resize(byteLimit);
        }
        stream << text;
    } else {
        // ストリームへ出力できない集成体も、ネイティブの%r処理用に実体化で
        // きるようにする。 生バイト以外の変換は、通常のDの代替動作に従う。
        throw std::invalid_argument("D formattedWrite requires a streamable value");
    }
    auto formatted = stream.str();
    if (blank_sign) {
        const auto plus = formatted.find('+');
        if (plus != String::npos) formatted[plus] = ' ';
    }
    if ((conversion == 'a' || conversion == 'A') && precision < 0) {
        // DのformattedWriteは、精度未指定の%a/%Aで
        // 小数部末尾の0を省く。 std::hexfloatは正確に復
        // 元できる桁まで出すため、指数の前の不要な0だけを削除する。
        const auto exponent = formatted.find(conversion == 'A' ? 'P' : 'p');
        const auto point = formatted.find('.');
        if (point != String::npos && exponent != String::npos && point < exponent) {
            auto trimmedExponent = exponent;
            while (trimmedExponent > point + 1U && formatted[trimmedExponent - 1U] == '0')
                --trimmedExponent;
            if (trimmedExponent == point + 1U) --trimmedExponent;
            formatted.erase(trimmedExponent, exponent - trimmedExponent);
        }
    }

    const bool integerConversion = std::strchr("duboxX", conversion) != nullptr;
    const auto numericPrefixEnd = [&] {
        std::size_t start = 0U;
        if (start < formatted.size() &&
            (formatted[start] == '+' || formatted[start] == '-' || formatted[start] == ' '))
            ++start;
        if (start + 1U < formatted.size() && formatted[start] == '0' &&
            std::strchr("xXb", formatted[start + 1U]) != nullptr)
            start += 2U;
        return start;
    }();
    if (integerConversion && precision >= 0) {
        const auto digits = numericPrefixEnd;
        std::size_t digitEnd = digits;
        while (digitEnd < formatted.size() &&
               std::strchr("0123456789abcdefABCDEF", formatted[digitEnd]) != nullptr)
            ++digitEnd;
        if (digitEnd - digits < static_cast<std::size_t>(precision))
            formatted.insert(digits, static_cast<std::size_t>(precision) - (digitEnd - digits), '0');
    }
    if (options.grouping && options.groupingWidth > 0 &&
        std::strchr("duboxXeEfFgGaA", conversion) != nullptr) {
        const auto exponent = formatted.find_first_of("eEpP");
        const auto decimal = formatted.find('.');
        auto digitEnd = std::min(exponent == String::npos ? formatted.size() : exponent,
                                 decimal == String::npos ? formatted.size() : decimal);
        const auto digitStart = numericPrefixEnd;
        if (digitStart != String::npos && digitStart < digitEnd) {
            for (std::size_t at = digitEnd; at > digitStart + static_cast<std::size_t>(options.groupingWidth); ) {
                at -= static_cast<std::size_t>(options.groupingWidth);
                formatted.insert(at, options.groupingSeparator);
            }
        }
    }
    if (width > 0 && formatted.size() < static_cast<std::size_t>(width)) {
        const auto padding = static_cast<std::size_t>(width) - formatted.size();
        if (center) {
            const auto leftPadding = left ? padding / 2U : (padding + 1U) / 2U;
            formatted.insert(0U, leftPadding, ' ');
            formatted.append(padding - leftPadding, ' ');
        } else if (left) {
            formatted.append(padding, ' ');
        } else if (zero && std::strchr("duboxXeEfFgGaA", conversion) != nullptr &&
                   precision < 0) {
            const auto insertion = numericPrefixEnd;
            formatted.insert(insertion, padding, '0');
        } else {
            formatted.insert(0U, padding, ' ');
        }
    }
    return formatted;
}

inline String quoteDFormatString(StringView text) {
    static constexpr char hex[] = "0123456789ABCDEF";
    String quoted{"\""};
    for (const unsigned char character : text) {
        switch (character) {
        case '\\': quoted.append("\\\\"); break;
        case '"': quoted.append("\\\""); break;
        case '\0': quoted.append("\\0"); break;
        case '\a': quoted.append("\\a"); break;
        case '\b': quoted.append("\\b"); break;
        case '\f': quoted.append("\\f"); break;
        case '\n': quoted.append("\\n"); break;
        case '\r': quoted.append("\\r"); break;
        case '\t': quoted.append("\\t"); break;
        case '\v': quoted.append("\\v"); break;
        default:
            if (character < 0x20U || character == 0x7fU) {
                quoted.append("\\x");
                quoted.push_back(hex[character >> 4U]);
                quoted.push_back(hex[character & 0x0fU]);
            } else quoted.push_back(static_cast<char>(character));
            break;
        }
    }
    quoted.push_back('"');
    return quoted;
}

inline String quoteDFormatCharacter(StringView text) {
    String quoted{"'"};
    for (const char character : text) {
        if (character == '\\' || character == '\'') quoted.push_back('\\');
        switch (character) {
        case '\0': quoted.append("\\0"); break;
        case '\a': quoted.append("\\a"); break;
        case '\b': quoted.append("\\b"); break;
        case '\f': quoted.append("\\f"); break;
        case '\n': quoted.append("\\n"); break;
        case '\r': quoted.append("\\r"); break;
        case '\t': quoted.append("\\t"); break;
        case '\v': quoted.append("\\v"); break;
        default: quoted.push_back(character); break;
        }
    }
    quoted.push_back('\'');
    return quoted;
}

template <typename T>
String rangeElementToString(const T& value, const bool escape) {
    using U = std::remove_cvref_t<T>;
    if constexpr (isRawFormatCharacter<U>) {
        const auto result = renderFormattedValue("%c", value);
        if (escape) return quoteDFormatCharacter(result);
        return result;
    } else {
        String result = renderFormattedValue("%s", value);
        if constexpr (is_text_format_value_v<U>) {
        if (escape) return quoteDFormatString(result);
        }
        return result;
    }
}

struct ParsedDFormat {
    std::size_t end{};
    std::size_t argument{};
    std::size_t rangeEnd{};
    bool positional{};
    String flags;
    int width{};
    int widthArgument{-1};
    int precision{-1};
    int precisionArgument{-1};
    bool grouping{};
    int groupingWidth{3};
    int groupingArgument{-1};
    bool separatorCharacterArgument{};
    int separatorArgument{-1};
    char conversion{};
    StringView nested{};
    StringView rangeSeparator{};
    bool hasRangeSeparator{};
    bool suppressElementEscaping{};
};

inline bool isFormatDigit(const char value) noexcept {
    return value >= '0' && value <= '9';
}

inline std::size_t parseFormatUnsigned(StringView text, std::size_t& cursor) {
    if (cursor >= text.size() || !isFormatDigit(text[cursor]))
        throw std::invalid_argument("Expected a number in D format specifier");
    std::size_t value = 0U;
    while (cursor < text.size() && isFormatDigit(text[cursor])) {
        const auto digit = static_cast<std::size_t>(text[cursor++] - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10U)
            throw std::out_of_range("D format number is too large");
        value = value * 10U + digit;
    }
    return value;
}

inline int checkedFormatInt(const std::size_t value) {
    if (value > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::out_of_range("D format width or precision is too large");
    return static_cast<int>(value);
}

inline ParsedDFormat parseDFormat(StringView format, const std::size_t marker) {
    ParsedDFormat parsed;
    std::size_t cursor = marker + 1U;
    if (cursor >= format.size()) throw std::invalid_argument("Incomplete format specifier");
    if (isFormatDigit(format[cursor])) {
        const auto saved = cursor;
        const auto index = parseFormatUnsigned(format, cursor);
        if (cursor < format.size() && format[cursor] == '$') {
            if (index == 0U) throw std::invalid_argument("Positional indexes start at one");
            parsed.positional = true;
            parsed.argument = index - 1U;
            parsed.rangeEnd = parsed.argument;
            ++cursor;
        } else if (cursor < format.size() && format[cursor] == ':') {
            if (index == 0U) throw std::invalid_argument("Positional indexes start at one");
            parsed.positional = true;
            parsed.argument = index - 1U;
            ++cursor;
            if (cursor >= format.size() || format[cursor] == '$')
                // DMD 2.097は%m:$を終端なしの範囲として解析す
                // るが、その後に番兵の添字まで進み、 不足する引数で例外を
                // 投げる。そのためtryFormatは元の文字列を返す。
                throw std::invalid_argument("Pinned D parser cannot complete an open-ended positional range");
            const auto end = parseFormatUnsigned(format, cursor);
            if (end < index) throw std::invalid_argument("Positional range is reversed");
            parsed.rangeEnd = end - 1U;
            if (cursor >= format.size() || format[cursor] != '$')
                throw std::invalid_argument("Missing positional range terminator");
            ++cursor;
        } else {
            cursor = saved;
        }
    }
    while (cursor < format.size() && std::strchr("-+=#0 ", format[cursor]) != nullptr)
        parsed.flags.push_back(format[cursor++]);
    parsed.suppressElementEscaping = parsed.flags.find('-') != String::npos;

    if (cursor < format.size() && format[cursor] == '*') {
        ++cursor;
        if (cursor < format.size() && isFormatDigit(format[cursor])) {
            // 対象のDMD/Phobos 2.097では、この表記の$がparse!int
            // へ渡されConvExceptionとなり、 tryFormat
            // は元の書式を返す。その確認済みの動作を保つ。
            throw std::invalid_argument("Pinned D parser rejects positional dynamic width");
        } else parsed.widthArgument = -2;
    } else if (cursor < format.size() && isFormatDigit(format[cursor])) {
        parsed.width = checkedFormatInt(parseFormatUnsigned(format, cursor));
    }

    if (cursor < format.size() && format[cursor] == ',') {
        parsed.grouping = true;
        ++cursor;
        if (cursor < format.size() && format[cursor] == '*') {
            ++cursor;
            parsed.groupingArgument = -2;
        } else if (cursor < format.size() && isFormatDigit(format[cursor])) {
            parsed.groupingWidth = checkedFormatInt(parseFormatUnsigned(format, cursor));
        }
        if (cursor < format.size() && format[cursor] == '?') {
            parsed.separatorCharacterArgument = true;
            ++cursor;
        }
    }

    if (cursor < format.size() && format[cursor] == '.') {
        ++cursor;
        if (cursor < format.size() && format[cursor] == '*') {
            ++cursor;
            if (cursor < format.size() && isFormatDigit(format[cursor])) {
                // 位置指定の幅と同じ、対象Phobos版の代替動作を維持する。
                throw std::invalid_argument("Pinned D parser rejects positional dynamic precision");
            } else parsed.precisionArgument = -2;
        } else if (cursor < format.size() && format[cursor] == '-') {
            parsed.precision = 0;
            ++cursor;
            while (cursor < format.size() && isFormatDigit(format[cursor])) ++cursor;
        } else if (cursor < format.size() && isFormatDigit(format[cursor])) {
            parsed.precision = checkedFormatInt(parseFormatUnsigned(format, cursor));
        } else parsed.precision = 0;
    }
    if (cursor >= format.size()) throw std::invalid_argument("Incomplete format specifier");
    parsed.conversion = format[cursor++];
    if (parsed.conversion == '(') {
        const auto innerStart = cursor;
        std::size_t depth = 0U;
        std::size_t separator = StringView::npos;
        std::size_t close = StringView::npos;
        while (cursor + 1U < format.size()) {
            if (format[cursor] != '%') { ++cursor; continue; }
            const char next = format[cursor + 1U];
            if (next == '%') { cursor += 2U; continue; }
            if (next == '(') { ++depth; cursor += 2U; continue; }
            if (next == '-' && cursor + 2U < format.size() && format[cursor + 2U] == ')') {
                if (depth == 0U) { close = cursor; break; }
                --depth; cursor += 3U; continue;
            }
            if (next == ')') {
                if (depth == 0U) { close = cursor; break; }
                --depth; cursor += 2U; continue;
            }
            if (next == '|' && depth == 0U && separator == StringView::npos)
                separator = cursor;
            cursor += 2U;
        }
        if (close == StringView::npos) throw std::invalid_argument("Unterminated compound format");
        if (separator != StringView::npos) {
            parsed.nested = format.substr(innerStart, separator - innerStart);
            const auto sepStart = separator + 2U;
            parsed.rangeSeparator = format.substr(sepStart, close - sepStart);
            parsed.hasRangeSeparator = true;
        } else {
            parsed.nested = format.substr(innerStart, close - innerStart);
            std::size_t nestedCursor = 0U;
            std::size_t lastSpecEnd = StringView::npos;
            while (nestedCursor < parsed.nested.size()) {
                const auto nestedMarker = parsed.nested.find('%', nestedCursor);
                if (nestedMarker == StringView::npos) break;
                if (nestedMarker + 1U < parsed.nested.size() &&
                    parsed.nested[nestedMarker + 1U] == '%') {
                    nestedCursor = nestedMarker + 2U;
                    continue;
                }
                const auto nestedSpec = parseDFormat(parsed.nested, nestedMarker);
                lastSpecEnd = nestedSpec.end;
                nestedCursor = nestedSpec.end;
            }
            if (lastSpecEnd != StringView::npos && lastSpecEnd < parsed.nested.size()) {
                parsed.rangeSeparator = parsed.nested.substr(lastSpecEnd);
                parsed.nested = parsed.nested.substr(0U, lastSpecEnd);
                parsed.hasRangeSeparator = true;
            }
        }
        parsed.end = close + (format[close + 1U] == '-' ? 3U : 2U);
    } else {
        parsed.end = cursor;
    }
    return parsed;
}

template <std::size_t Index = 0U, typename Tuple, typename Callback>
void visitFormatArgument(const Tuple& values, const std::size_t selected,
                         Callback&& callback) {
    if constexpr (Index < std::tuple_size_v<Tuple>) {
        if (Index == selected) callback(std::get<Index>(values));
        else visitFormatArgument<Index + 1U>(values, selected,
                                             std::forward<Callback>(callback));
    } else {
        throw std::out_of_range("Format argument index is out of range");
    }
}

template <typename Tuple>
int readFormatInteger(const Tuple& values, const std::size_t selected) {
    int result = 0;
    visitFormatArgument(values, selected, [&](const auto& value) {
        using U = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_integral_v<U> && !std::is_same_v<U, bool>) {
            if constexpr (std::is_signed_v<U>) {
                const auto wide = static_cast<long long>(value);
                if (wide < std::numeric_limits<int>::min() || wide > std::numeric_limits<int>::max())
                    throw std::out_of_range("Dynamic format integer is outside int range");
                result = static_cast<int>(wide);
            } else {
                const auto wide = static_cast<unsigned long long>(value);
                if (wide > static_cast<unsigned long long>(std::numeric_limits<int>::max()))
                    throw std::out_of_range("Dynamic format integer is outside int range");
                result = static_cast<int>(wide);
            }
        } else if constexpr (std::is_enum_v<U>) {
            using B = std::underlying_type_t<U>;
            const auto raw = static_cast<B>(value);
            if constexpr (std::is_signed_v<B>) result = static_cast<int>(raw);
            else if (raw <= static_cast<B>(std::numeric_limits<int>::max())) result = static_cast<int>(raw);
            else throw std::out_of_range("Dynamic format integer is outside int range");
        } else {
            throw std::invalid_argument("Dynamic D format width or precision requires an integer");
        }
    });
    return result;
}

template <typename Tuple>
String readFormatCharacter(const Tuple& values, const std::size_t selected) {
    String result;
    visitFormatArgument(values, selected, [&](const auto& value) {
        using U = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<U, char> || std::is_same_v<U, char8_t>)
            result.push_back(static_cast<char>(value));
        else if constexpr (std::is_same_v<U, char16_t> || std::is_same_v<U, char32_t> ||
                           std::is_same_v<U, wchar_t>)
            result = formatCharacter(static_cast<char32_t>(value));
        else throw std::invalid_argument("Dynamic D separator requires a character");
    });
    return result;
}

template <typename Tuple>
void appendDFormatted(String& output, StringView format, const Tuple& values,
                      const bool escapeStringArguments) {
    std::size_t currentArgument = 0U;
    std::size_t offset = 0U;
    while (offset < format.size()) {
        const auto marker = format.find('%', offset);
        if (marker == StringView::npos) { output.append(format.substr(offset)); return; }
        output.append(format.substr(offset, marker - offset));
        if (marker + 1U < format.size() && format[marker + 1U] == '%') {
            output.push_back('%'); offset = marker + 2U; continue;
        }
        auto parsed = parseDFormat(format, marker);
        FormatRenderOptions options;
        options.center = parsed.flags.find('=') != String::npos;
        options.grouping = parsed.grouping;
        options.groupingWidth = parsed.groupingWidth;
        options.nested = parsed.nested;
        options.rangeSeparator = parsed.rangeSeparator;
        options.hasRangeSeparator = parsed.hasRangeSeparator;
        options.suppressElementEscaping = parsed.suppressElementEscaping;
        options.escapeStringArguments = escapeStringArguments;

        const auto resolveDynamic = [&](const int encoded, const bool width) {
            if (encoded == -1) return std::size_t{};
            const bool sequential = encoded == -2;
            const auto selected = sequential ? currentArgument : static_cast<std::size_t>(encoded);
            const auto value = readFormatInteger(values, selected);
            if (sequential) ++currentArgument;
            else currentArgument = std::max(currentArgument, selected + 1U);
            if (width && value < 0) {
                if (value == std::numeric_limits<int>::min())
                    throw std::out_of_range("Negative dynamic width is too small");
                parsed.flags.push_back('-');
                parsed.width = -value;
            } else if (width) parsed.width = value;
            else parsed.precision = value < 0 ? -1 : value;
            return selected;
        };
        if (parsed.widthArgument != -1) (void)resolveDynamic(parsed.widthArgument, true);
        if (parsed.precisionArgument != -1) (void)resolveDynamic(parsed.precisionArgument, false);
        if (parsed.groupingArgument != -1) {
            const auto selected = parsed.groupingArgument == -2
                ? currentArgument : static_cast<std::size_t>(parsed.groupingArgument);
            parsed.groupingWidth = readFormatInteger(values, selected);
            if (parsed.groupingWidth < 0)
                throw std::invalid_argument("D separator width cannot be negative");
            options.groupingWidth = parsed.groupingWidth;
            if (parsed.groupingArgument == -2) ++currentArgument;
            else currentArgument = std::max(currentArgument, selected + 1U);
        }
        if (parsed.separatorCharacterArgument) {
            const auto selected = parsed.separatorArgument >= 0
                ? static_cast<std::size_t>(parsed.separatorArgument) : currentArgument;
            options.groupingSeparator = readFormatCharacter(values, selected);
            ++currentArgument;
        }

        String spec{"%"};
        spec.append(parsed.flags);
        if (parsed.width > 0) spec.append(std::to_string(parsed.width));
        if (parsed.precision >= 0) {
            spec.push_back('.');
            spec.append(std::to_string(parsed.precision));
        }
        spec.push_back(parsed.conversion);
        const auto firstValue = parsed.positional ? parsed.argument : currentArgument;
        const auto lastValue = parsed.positional
            ? (parsed.rangeEnd == std::numeric_limits<std::size_t>::max()
                ? std::tuple_size_v<Tuple> - 1U : parsed.rangeEnd)
            : firstValue;
        if (!parsed.positional) ++currentArgument;
        else currentArgument = std::max(currentArgument, lastValue + 1U);
        if (firstValue > lastValue || lastValue >= std::tuple_size_v<Tuple>)
            throw std::out_of_range("D format argument range is out of bounds");
        for (std::size_t selected = firstValue; selected <= lastValue; ++selected) {
            visitFormatArgument(values, selected, [&](const auto& value) {
                auto rendered = renderFormattedValue(spec, value, options);
                using Value = std::remove_cvref_t<decltype(value)>;
                if (escapeStringArguments && parsed.conversion == 's') {
                    if constexpr (is_text_format_value_v<Value>)
                        rendered = quoteDFormatString(rendered);
                    else if constexpr (isRawFormatCharacter<Value>)
                        rendered = quoteDFormatCharacter(rendered);
                }
                output.append(rendered);
            });
        }
        offset = parsed.end;
    }
}

inline void validateDMapNestedFormat(const StringView format) {
    std::size_t count = 0U;
    std::size_t cursor = 0U;
    bool keyFirst = true;
    while (cursor < format.size()) {
        const auto marker = format.find('%', cursor);
        if (marker == StringView::npos) break;
        if (marker + 1U < format.size() && format[marker + 1U] == '%') {
            cursor = marker + 2U;
            continue;
        }
        const auto parsed = parseDFormat(format, marker);
        if (count == 0U) {
            if (parsed.positional) {
                if (parsed.argument > 1U)
                    throw std::invalid_argument("D associative-array nested index exceeds two");
                keyFirst = parsed.argument == 0U;
            }
        } else if (count == 1U && parsed.positional) {
            if (parsed.argument > 1U || ((parsed.argument == 1U) != keyFirst))
                throw std::invalid_argument("D associative-array nested indexes must select key and value");
        }
        ++count;
        if (count > 2U)
            throw std::invalid_argument("D associative-array nested format has more than two specs");
        cursor = parsed.end;
    }
    if (count != 2U)
        throw std::invalid_argument("D associative-array nested format requires two specs");
}

template <typename T>
String renderRangeFormatted(StringView spec, const T& value,
                            const FormatRenderOptions& options) {
    using U = std::remove_cvref_t<T>;
    using Element = typename format_range_element<U>::type;
    const bool compound = !spec.empty() && spec.back() == '(';
    if (compound && options.nested.empty() && std::begin(value) != std::end(value))
        throw std::invalid_argument("D compound format has no nested specifier");

    if constexpr (std::is_same_v<std::remove_cv_t<Element>, char> ||
                  std::is_same_v<std::remove_cv_t<Element>, char8_t>) {
        if (!compound) {
            String text;
            auto size = static_cast<std::size_t>(std::distance(std::begin(value), std::end(value)));
            if constexpr (std::is_array_v<U>) {
                if (size != 0U && value[size - 1U] == Element{}) --size;
            }
            for (std::size_t index = 0U; index < size; ++index)
                text.push_back(static_cast<char>(value[index]));
            return text;
        }
    } else if constexpr (std::is_same_v<std::remove_cv_t<Element>, char16_t> ||
                         std::is_same_v<std::remove_cv_t<Element>, char32_t> ||
                         std::is_same_v<std::remove_cv_t<Element>, wchar_t>) {
        if (!compound) {
            String text;
            for (const auto character : value)
                text.append(formatCharacter(static_cast<char32_t>(character)));
            return text;
        }
    }

    String result;
    if (!compound) result.push_back('[');
    bool first = true;
    const auto appendSeparator = [&] {
        if (first) return;
        if (compound) {
            if (options.hasRangeSeparator) result.append(options.rangeSeparator);
        } else result.append(", ");
    };
    if constexpr (is_format_map_v<U>) {
        const bool defaultMapSpec = options.nested.empty();
        const auto nested = defaultMapSpec ? StringView("%s:%s") : options.nested;
        if (!defaultMapSpec) validateDMapNestedFormat(nested);
        for (const auto& item : value) {
            appendSeparator();
            if (defaultMapSpec) {
                result.append(rangeElementToString(item.first, true));
                result.push_back(':');
                result.append(rangeElementToString(item.second, true));
            } else {
                String rendered;
                appendDFormatted(rendered, nested, std::tie(item.first, item.second),
                                 !options.suppressElementEscaping);
                result.append(rendered);
            }
            first = false;
        }
    } else {
        for (const auto& element : value) {
            appendSeparator();
            if (compound) {
                const auto elementValue = element;
                String rendered;
                appendDFormatted(rendered, options.nested, std::tie(elementValue),
                                 !options.suppressElementEscaping);
                result.append(rendered);
            } else {
                result.append(rangeElementToString(element, true));
            }
            first = false;
        }
    }
    if (!compound) result.push_back(']');
    return result;
}

}  // namespace detail

template <typename... Values>
[[nodiscard]] String debugString(const Values&... values) {
    return detail::concatDebugValues(values...);
}

struct DebugArgument {
    String text;
    std::source_location location;
    template <typename T>
    DebugArgument(const T& value, std::source_location source = std::source_location::current())
        : text(detail::concatDebugValues(value)), location(source) {}
};
[[nodiscard]] String decorateDebugln(String value, std::source_location location, bool buildInfo = true);
template <bool BuildInfo = true>
[[nodiscard]] inline String createDebugln(std::source_location source = std::source_location::current()) {
    return decorateDebugln({}, source, BuildInfo);
}

template <bool BuildInfo = true, typename... Values>
[[nodiscard]] String createDebugln(const DebugArgument& first, const Values&... values) {
    return decorateDebugln(first.text + detail::concatDebugValues(values...), first.location, BuildInfo);
}

// fdebuglnの捕捉・同期処理より前に書式化せず、公開
// の呼び出し位置を記録する。 参照する引数は一時値でも呼び
// 出し中は有効で、この借用の保持型はメモリを確保しない。
struct FileDebugArgument {
    const void* value;
    String (*render)(const void*);
    std::source_location location;
    template <typename T>
    FileDebugArgument(const T& argument,
                      std::source_location source = std::source_location::current()) noexcept
        : value(std::addressof(argument)),
          render([](const void* pointer) {
#if defined(CWXEDITOR_FILE_LOGGING)
              return detail::concatDebugValues(*static_cast<const T*>(pointer));
#else
              (void)pointer;
              return String{};
#endif
          }), location(source) {}
};

namespace detail {
inline void reportFileDebugFailure(const std::source_location source, const char* message) noexcept {
    // Dは書式化やログのオープンに失敗しても、失敗内容と元の呼び
    // 出し位置を標準出力へ報告する。 代替出力は別のcatch内
    // で行い、診断ストリームの失敗で元のエラーを置き換えない。
    try {
        std::cout << __FILE__ << ' ' << __LINE__ << ' ' << message << '\n'
                  << source.file_name() << ' ' << source.line() << " \n";
    } catch (...) {}
}

template <typename Formatter>
void writeFileDebugln(const std::source_location source, Formatter&& format) {
#if !defined(CWXEDITOR_FILE_LOGGING)
    // 製品のファイルログを有効にするのは、Windowsの単独起動Releaseターゲットだけ。
    (void)source;
    (void)format;
    return;
#else
    try {
        // Dはメッセージの書式化とログ出力の両方の失敗を捕捉する。 独
        // 自の引数書式処理からの例外も、ログ呼び出しの外へ漏らさない。
        std::scoped_lock lock(detail::debugMutex());
        const auto message = decorateDebugln(format(), source);
        std::cerr << message << '\n';
        const Path path(debugLog);
        if (!path.parent_path().empty()) {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
        }
        if (!debugLogFile.is_open()) {
            debugLogFile.open(path, std::ios::out | std::ios::app);
            initDebugLogFile = debugLogFile.is_open();
        }
        if (debugLogFile.is_open()) debugLogFile << message << '\n' << std::flush;
        if (!debugLogFile) throw std::runtime_error("Can't write debug log");
    } catch (const std::exception& error) {
        reportFileDebugFailure(source, error.what());
    } catch (...) {
        reportFileDebugFailure(source, "Unknown logging failure");
    }
#endif
}
}  // namespace detail

inline void fdebugln(std::source_location source = std::source_location::current()) {
    detail::writeFileDebugln(source, [] { return String{}; });
}

template <typename... Values>
void fdebugln(const FileDebugArgument& first, const Values&... values) {
#if defined(CWXEDITOR_FILE_LOGGING)
    detail::writeFileDebugln(first.location, [&] {
        return first.render(first.value) + detail::concatDebugValues(values...);
    });
#else
    (void)first;
    (static_cast<void>(values), ...);
#endif
}

inline void debugln(std::source_location source = std::source_location::current()) {
    fdebugln(source);
}

template <typename... Values>
void debugln(const FileDebugArgument& first, const Values&... values) {
    fdebugln(first, values...);
}

void cwriteln(StringView value);

template <typename... Values>
void cdebugln(const Values&... values) {
#if !defined(NDEBUG) && \
    (defined(_CONSOLE) || defined(CWXEDITOR_CONSOLE) || defined(CONSOLE))
    // DはcreateDebugln!(false)の後、cwritelnと同じネイテ
    // ィブのコンソール文字コードで標準出力へ書く。 両APIを同じ出力経路に揃える。
    cwriteln(createDebugln<false>(values...));
#else
    (static_cast<void>(values), ...);
#endif
}

template <typename S>
void validate(const S& value) {
    const auto* first = value.data();
    const auto* last = first + value.size();
    while (first != last) {
        const auto byte = static_cast<unsigned char>(*first++);
        if (byte < 0x80U) continue;
        std::size_t continuation = 0;
        char32_t codepoint = 0;
        if ((byte & 0xe0U) == 0xc0U) {
            continuation = 1;
            codepoint = byte & 0x1fU;
            if (codepoint == 0) throw std::invalid_argument("invalid UTF-8");
        } else if ((byte & 0xf0U) == 0xe0U) {
            continuation = 2;
            codepoint = byte & 0x0fU;
        } else if ((byte & 0xf8U) == 0xf0U) {
            continuation = 3;
            codepoint = byte & 0x07U;
        } else {
            throw std::invalid_argument("invalid UTF-8");
        }
        for (std::size_t i = 0; i < continuation; ++i) {
            if (first == last) throw std::invalid_argument("invalid UTF-8");
            const auto next = static_cast<unsigned char>(*first++);
            if ((next & 0xc0U) != 0x80U) throw std::invalid_argument("invalid UTF-8");
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if ((continuation == 1 && codepoint < 0x80U) ||
            (continuation == 2 && codepoint < 0x800U) ||
            (continuation == 3 && codepoint < 0x10000U) ||
            codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            throw std::invalid_argument("invalid UTF-8");
        }
    }
}

[[nodiscard]] void* dlopen(StringView library);
[[nodiscard]] void* dlsym(void* library, StringView symbol);
void dlclose(void*& library) noexcept;
[[nodiscard]] Path appDataDir(const Path& application_path);
[[nodiscard]] String objectIDValue(const void* object);

template <typename T>
[[nodiscard]] String objectIDValue(const T& object) {
    return objectIDValue(static_cast<const void*>(&object));
}

inline constexpr StringView B_IMG = "binaryimage://";
[[nodiscard]] bool isBinImg(StringView path) noexcept;
[[nodiscard]] Bytes strToBImg(StringView value);
[[nodiscard]] String bImgToStr(ByteView value);
[[nodiscard]] String bImgToStr(const Bytes& value);

inline constexpr StringView B_SND = "binarysound://";
[[nodiscard]] bool isBinSnd(StringView path) noexcept;
[[nodiscard]] Bytes strToBSnd(StringView value);
[[nodiscard]] String bSndToStr(ByteView value);
[[nodiscard]] String bSndToStr(const Bytes& value);

[[nodiscard]] String getNormalizedSoundExt(const Path& file);
[[nodiscard]] String getNormalizedSoundExt(ByteView bytes);
[[nodiscard]] String getNormalizedSoundExt(binary::RawFile& file);
[[nodiscard]] String getNormalizedSoundExt(std::istream& stream);
[[nodiscard]] bool isSoundExt(StringView path);
// Phobosの拡張子判定。ネイティブの区切り・ドライブ区切り・先頭のドットの規則を使う。
[[nodiscard]] StringView pathExtension(StringView path) noexcept;
[[nodiscard]] StringView pathBaseName(StringView path) noexcept;
[[nodiscard]] StringView pathDirName(StringView path) noexcept;
[[nodiscard]] bool pathIsAbsolute(StringView path) noexcept;
[[nodiscard]] String setPathExtension(StringView path, StringView extension);
// Phobosのexistsと同じく、ネイティブの問い合わせ失敗はfalseとする。POSIXのリンクにはlstatを使う。
[[nodiscard]] bool pathExists(const Path& path) noexcept;
[[nodiscard]] bool pathIsDirectory(const Path& path);
// Dのスキン事前確認。Win32では属性を取得できるか調べ、POSIXでは問い合わせない。
[[nodiscard]] bool canAccessPath(StringView path);
[[nodiscard]] String normalizedSoundExt(StringView extension);
[[nodiscard]] String soundType(ByteView bytes) noexcept;

template <typename... Values>
[[nodiscard]] String tryFormat(StringView format, const Values&... values) {
    try {
        String result;
        const auto tuple = std::tie(values...);
        detail::appendDFormatted(result, format, tuple);
        return result;
    } catch (const std::bad_alloc&) {
        // DのtryFormatはExceptionを捕捉するが、OutOfMemoryErrorはErrorの派生なので捕捉
        // しない。 std::bad_allocがstd::exceptionの派生であっても、同じ致命的エラーの区別を保つ。
        throw;
    } catch (const std::exception& error) {
        fdebugln(error.what());
        return String(format);
    }
}

[[nodiscard]] String formatReal(long double value, std::uint32_t max_len);
[[nodiscard]] String formatReal(lib::Real80 value, std::uint32_t max_len);

class Wildcard {
public:
    explicit Wildcard(StringView pattern = {}, bool ignore_case = false);
    // Dはコンパイル済みのクラスオブジェクトを共有する。 ネイティブでも右辺値を
    // 含むコピーで不変のパターンを共有し、移動元がnullにならないようにする。
    Wildcard(const Wildcard&) = default;
    Wildcard& operator=(const Wildcard&) = default;

    [[nodiscard]] std::ptrdiff_t find(StringView value) const;
    [[nodiscard]] bool match(StringView value) const;
    [[nodiscard]] String replace(StringView value, StringView replacement) const;
    [[nodiscard]] std::size_t count(StringView value) const;

    [[nodiscard]] static Wildcard opCall(StringView pattern,
                                          bool ignore_case = false) {
        return Wildcard(pattern, ignore_case);
    }

private:
    struct Character {
        char32_t value{};
        bool question{};
        [[nodiscard]] bool match(char32_t candidate, bool ignore_case) const noexcept;
    };
    // 各区間がDの_leftに対応し、次の区間が_rightのオブジェクトに対応する。
    using Pattern = std::vector<std::vector<Character>>;
    [[nodiscard]] static std::shared_ptr<const Pattern> compile(StringView pattern);
    [[nodiscard]] std::ptrdiff_t findWords(std::u32string_view source,
                                          std::size_t segment, bool reverse) const;
    [[nodiscard]] std::ptrdiff_t findRange(std::u32string_view& source,
                                          std::size_t segment, bool next,
                                          std::size_t& length) const;
    std::shared_ptr<const Pattern> pattern_;
    bool ignore_case_{};
};

[[nodiscard]] bool simpleGlob(StringView value, StringView pattern);
[[nodiscard]] String simpleGlobTransrate(StringView pattern);

template <typename S, typename P>
[[nodiscard]] bool simpleGlob(const S& value, const P& pattern) {
    const auto text = detail::stringFrom(value);
    const auto glob = detail::stringFrom(pattern);
    // 文字列引数はStringViewの非テンプレート実装へ変換されず、
    // 完全一致するこのテンプレートを再選択するため、明示的に転送する。
    return simpleGlob(StringView(text), StringView(glob));
}

template <typename C>
[[nodiscard]] std::basic_string<C> simpleGlobTransrate(
    std::basic_string_view<C> pattern) {
    const auto translated = simpleGlobTransrate(String(pattern.begin(), pattern.end()));
    return std::basic_string<C>(translated.begin(), translated.end());
}

template <typename C, std::size_t N>
[[nodiscard]] std::basic_string<C> simpleGlobTransrate(const C (&pattern)[N]) {
    return simpleGlobTransrate(std::basic_string_view<C>(pattern, N > 0U ? N - 1U : 0U));
}

[[nodiscard]] Bytes readBinary(const Path& file);

template <typename T>
[[nodiscard]] std::vector<T> readBinaryFrom(const Path& file, T*& pointer) {
    // D版の不具合との差異。POSIX・32ビットWindowsでは
    // 、Win64の配列キャストと異なり バイト数をTの要素数として
    // 使っている。全環境で型付きの要素数へ統一する。 GC/malloc
    // のバッファーは所有vectorへ置き換えるため、解放用ポイン
    // ターで公開する生の確保領域はない。 DのPOSIX実装のよう
    // に未読み込みの末尾を0のまま返さず、短い読み込みも拒否する。
    pointer = nullptr;
    auto bytes = readBinary(file);
    if constexpr (std::is_same_v<T, ::cwxeditor::lib::Byte>) {
        return bytes;
    } else {
        const auto count = bytes.size() / sizeof(T);
        std::vector<T> result(count);
        if (count != 0) std::memcpy(result.data(), bytes.data(), count * sizeof(T));
        return result;
    }
}

template <typename T>
void freeAll(T* data) noexcept {
    delete[] data;
}

template <typename T>
void freeAll(std::vector<T*>& data) noexcept {
    for (auto* value : data) delete[] value;
    data.clear();
}

[[nodiscard]] String readTextFile(const Path& file, bool& is_sjis);
void writeTextFile(const Path& file, StringView value, bool is_sjis,
                   const lib::Owner<filesync::FileSync>& sync = {});
[[nodiscard]] String md5Digest(ByteView data);
[[nodiscard]] String md5Digest(const Bytes& data);
[[nodiscard]] String fileToMD5Digest(const Path& file);

[[nodiscard]] String buildPath(StringView left, StringView right);
[[nodiscard]] String normpath(StringView path);
[[nodiscard]] Path normpath(const Path& path);
[[nodiscard]] String nabs(StringView path);
[[nodiscard]] Path nabs(const Path& path);
[[nodiscard]] String abs2rel(StringView first, StringView second);
[[nodiscard]] Path abs2rel(const Path& first, const Path& second);
[[nodiscard]] bool isSubDirectory(StringView path, StringView parent);
[[nodiscard]] bool isSubDirectory(const Path& path, const Path& parent);
[[nodiscard]] bool isOuterPath(StringView path);
[[nodiscard]] bool istartsWith(StringView value, StringView prefix);
[[nodiscard]] bool iendsWith(StringView value, StringView suffix);
[[nodiscard]] bool containsPath(const lib::Array<String>& paths,
                                StringView path);
[[nodiscard]] bool fnstartsWith(StringView value, StringView prefix);
[[nodiscard]] bool fnendsWith(StringView value, StringView suffix);
[[nodiscard]] String encodePath(String value);
[[nodiscard]] String decodePath(String value);
[[nodiscard]] String lastRet(String value);

namespace detail {
template <typename Iterator, typename Compare>
[[nodiscard]] std::vector<std::iter_value_t<Iterator>> sortAndCopy(
    Iterator first, Iterator last, Compare compare) {
    // Dは保持中の入力範囲を並べ替え、array()で要素の格納先をコピーする
    // 。 空のSharedSliceでは二つのnullポインターを減算しない。
    if (first == last) return {};
    std::sort(first, last, std::ref(compare));
    return std::vector<std::iter_value_t<Iterator>>(first, last);
}

template <typename Compare>
[[nodiscard]] auto sortPredicate(Compare compare) {
    return [compare = std::move(compare)](const auto& first, const auto& second) mutable {
        // Dの別名の比較関数は3方向の結果を返す。ネイテ
        // ィブのbool述語の便利な形式も残すが、 正
        // を含む0以外の全結果を「小さい」と扱わない。
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(compare(first, second))>, bool>)
            return compare(first, second);
        else
            return compare(first, second) < 0;
    };
}
} // namespace detail

template <typename T, typename Compare>
[[nodiscard]] std::vector<T> sortDlg(std::vector<T>& values, Compare compare) {
    // ネイティブのvectorは固定の反復子を借用するため、比較中の構造変更には対応
    // しない。 下のSharedSliceではDと同様に配列の確保領域を保持する。
    return detail::sortAndCopy(values.begin(), values.end(), std::move(compare));
}

template <typename T, typename Compare>
[[nodiscard]] lib::SharedSlice<T> sortDlg(lib::SharedSlice<T> values, Compare compare) {
    return lib::SharedSlice<T>(detail::sortAndCopy(values.begin(), values.end(), std::move(compare)));
}

template <typename T, typename Compare = std::less<T>>
[[nodiscard]] std::vector<T> sort(std::vector<T>& values,
                                 Compare compare = {}) {
    return sortDlg(values, detail::sortPredicate(std::move(compare)));
}

template <typename T, typename Compare = std::less<T>>
[[nodiscard]] lib::SharedSlice<T> sort(lib::SharedSlice<T> values, Compare compare = {}) {
    return sortDlg(std::move(values), detail::sortPredicate(std::move(compare)));
}

template <typename T, typename Compare = std::less<T>>
[[nodiscard]] std::vector<T> sort(std::vector<T>&& values, Compare compare = {}) {
    return sort(values, std::move(compare));
}

template <typename T, typename Compare = std::less<T>>
[[nodiscard]] std::vector<T> sort(const std::vector<T>& values, Compare compare = {}) {
    // constのC++コンテナーからDの可変スライスは公開できないため、コピーを並べ替える。
    auto copy = values;
    return sort(copy, std::move(compare));
}

namespace detail {
struct QSearchCompare {
    template <typename A, typename B>
    [[nodiscard]] int operator()(const A& first, const B& second) const {
        return first < second ? -1 : (first > second ? 1 : 0);
    }
};

template <typename T>
struct QSearchVectorView {
    const std::vector<T>* values;
    [[nodiscard]] std::size_t size() const noexcept { return values->size(); }
    [[nodiscard]] decltype(auto) operator[](std::size_t index) const { return (*values)[index]; }
};

template <typename Range, typename Key, typename Compare>
[[nodiscard]] std::ptrdiff_t qsearchRange(Range values, Key& key, Compare& compare) {
    std::size_t first = 0;
    std::size_t last = values.size();
    while (first < last) {
        const auto length = last - first;
        const auto index = length / 2U;
        const auto middle = first + index;
        // Dは3方向の比較関数を呼ぶ前に、選んだ要素をコピーする。
        // 通知中に別の参照からその格納先が置き換わる場合がある。
        const auto current = values[middle];
        const auto relation = std::invoke(compare, current, key);
        if (relation > 0) {
            if (index == 0) return -1;
            last = middle;
        } else if (relation < 0) {
            if (index + 1U == length) return -1;
            first = middle + 1U;
        } else {
            return static_cast<std::ptrdiff_t>(middle);
        }
    }
    return -1;
}
} // namespace detail

template <typename T, typename Key, typename Compare = detail::QSearchCompare>
[[nodiscard]] std::ptrdiff_t qsearch(lib::SharedSlice<T> values, Key key,
                                      Compare compare = {}) {
    // 記述子と値のスナップショットで、通知中もDの配列領
    // 域とキーを保持する。 再帰検索の各段階で共有要素や
    // 比較関数をコピーし直さない。 このD互換の入力経路
    // では、boolの比較結果も数値の0/1として扱う。
    return detail::qsearchRange(std::move(values), key, compare);
}

template <typename T, typename Key, typename Compare = detail::QSearchCompare>
[[nodiscard]] std::ptrdiff_t qsearch(const std::vector<T>& values, Key key,
                                      Compare compare = {}) {
    // 追加のネイティブ借用コンテナー用の変換。通常のC++範囲アルゴリズムと
    // 同じく、 コールバック中にvectorの構造を変えてはならない。 Dの
    // 確保領域を保持する動作が必要なら、上のSharedSliceを使う。
    const auto view = detail::QSearchVectorView<T>{&values};
    if constexpr (std::is_same_v<std::remove_cv_t<
                      std::invoke_result_t<Compare&, const T&, Key&>>, bool>) {
        // 従来のネイティブの「厳密に小さい」比較APIも残し、結果だけ
        // を変換する。 両方の公開形式は同じ3方向の検索ループを使う。
        auto adapted = [&](const T& first, Key& second) {
            if (std::invoke(compare, first, second)) return -1;
            if (std::invoke(compare, second, first)) return 1;
            return 0;
        };
        return detail::qsearchRange(view, key, adapted);
    } else {
        return detail::qsearchRange(view, key, compare);
    }
}

template <typename A, typename B>
[[nodiscard]] int dcmp(A first, B second) {
    return first < second ? -1 : (first > second ? 1 : 0);
}

template <typename T>
inline constexpr bool isVArray =
    !std::is_convertible_v<T, StringView> && requires { typename T::value_type; };

[[nodiscard]] String createNewName(
    String base, lib::Callback<bool(String)> use, bool space = true,
    const lib::Array<String>& start_names = {});
[[nodiscard]] String createNewName(
    String base, lib::Callback<bool(String)> use, bool space,
    lib::SharedSlice<const String> start_names);
[[nodiscard]] Path createNewFileName(const Path& path, bool is_directory);
// 仮想・Storage上の保存先にも同じDの命名規則を使う。 同期的な述語は
// 、利用可能な完全パスにtrueを返し、I/Oエラーでは例外を投げられる。
[[nodiscard]] Path createNewFileName(const Path& path, bool is_directory,
                                     const lib::Callback<bool(const Path&)>& available);
[[nodiscard]] Path createFileI(const Path& parent, StringView name,
                               StringView extension, StringView prefix,
                               bool ignore_exists_file);
[[nodiscard]] Path createFolder(const Path& parent, StringView name,
                                bool ignore_exists_file);

struct ScaledPathInfo {
    Path path;
    std::uint32_t scale{};
};

[[nodiscard]] ScaledPathInfo scaledImageInfo(const Path& path);
[[nodiscard]] Path noScaledPath(const Path& path);
[[nodiscard]] ScaledPathInfo findScaledImage(const Path& path,
                                              std::uint32_t target_scale);
[[nodiscard]] String singleLine(StringView value);
[[nodiscard]] String encodeLf(String value);
[[nodiscard]] String encodeXmlAndLf(String value);
[[nodiscard]] String encodeLf(const lib::Array<String>& values,
                              bool last_lf = true);
[[nodiscard]] lib::Array<String> splitLines(StringView value, bool keepTerminator = false);
[[nodiscard]] String chompLine(StringView value);
[[nodiscard]] String decodeLf2(StringView value);
[[nodiscard]] lib::Array<String> decodeLf(StringView value,
                                          bool use_empty = false);
[[nodiscard]] String fromBool(bool value);
[[nodiscard]] bool parseBool(StringView value);

template <typename Map>
void removeAll(Map& table) {
    table.clear();
}

template <typename Range>
[[nodiscard]] bool isSorted(const Range& values) {
    auto previous = values.begin();
    if (previous == values.end()) return true;
    for (auto current = std::next(previous); current != values.end();
         ++previous, ++current) {
        if (!std::less<>{}(*previous, *current)) return false;
    }
    return true;
}

template <typename Range, typename Compare>
[[nodiscard]] bool isSortedDlg(const Range& values, Compare compare) {
    auto previous = values.begin();
    if (previous == values.end()) return true;
    for (auto current = std::next(previous); current != values.end();
         ++previous, ++current) {
        if (!compare(*previous, *current)) return false;
    }
    return true;
}

[[nodiscard]] String toHex(StringView value);
[[nodiscard]] String caseFold(StringView value);
[[nodiscard]] String getenv(StringView variable);
[[nodiscard]] String cleanFileName(String value);
void preRemove(const Path& path);
[[nodiscard]] Path copyTo(const Path& scenario_path, const Path& path,
                          const Path& added, bool bin_img_to_ref,
                          StringView exported_image_name = {});
void copyAll(const Path& source, const Path& destination, bool overwrite = false);
void renameFile(const Path& from, const Path& to);
// PhobosのdirEntries(..., SpanMode.shallow)相当。コピー間で遅
// 延評価のカーソルを共有する。 返した項目は、それぞれ自分の名前と属性・問い合わせ状態を保持する。
class ShallowDirectory {
    struct Impl;
public:
    class Entry {
    public:
        [[nodiscard]] const String& name() const noexcept { return name_; }
        [[nodiscard]] bool isFile() const;
        [[nodiscard]] bool isDirectory() const;
    private:
        friend struct Impl;
        Entry() = default;
        void loadType() const;
        String name_;
        std::uint32_t attributes_{};
        mutable std::optional<bool> is_file_;
        mutable std::optional<bool> is_directory_;
        mutable bool did_lstat_{};
    };

    explicit ShallowDirectory(Path directory);
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] Entry front() const;
    void popFront();
private:
    std::shared_ptr<Impl> impl_;
};

// PhobosのdirEntries(..., SpanMode.depth)相当。ディレ
// クトリのリンクをたどり、子を先に列挙する。 列挙とOSの基本操作は浅い走査と共用する。
class DepthDirectory {
    struct Impl;
public:
    using Entry = ShallowDirectory::Entry;
    explicit DepthDirectory(Path directory);
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] Entry front() const;
    void popFront();
private:
    std::shared_ptr<Impl> impl_;
};

[[nodiscard]] lib::Array<String> clistdir(const Path& directory);
void delAll(const Path& path, bool force = true);

template <typename Range, typename Value>
[[nodiscard]] bool contains(const Range& range, const Value& value) {
    return std::find(std::begin(range), std::end(range), value) != std::end(range);
}

template <typename Range, typename Value, typename Predicate>
[[nodiscard]] bool contains(const Range& range, const Value& value,
                            Predicate predicate) {
    for (const auto& item : range) {
        if (predicate(value, item)) return true;
    }
    return false;
}

[[nodiscard]] int fncmp(StringView first, StringView second);

[[nodiscard]] inline int icmp(StringView first, StringView second) {
    return enumutils::compareIgnoreCase(first, second);
}

template <typename A, typename B>
[[nodiscard]] int ncmpImpl(const A& first, const B& second,
                           bool ignore_case, bool filename_case);

template <typename A, typename B>
[[nodiscard]] int ncmp(const A& first, const B& second) {
    return ncmpImpl(first, second, false, false);
}

template <typename A, typename B>
[[nodiscard]] int incmp(const A& first, const B& second) {
    return ncmpImpl(first, second, true, false);
}

template <typename A, typename B>
[[nodiscard]] int fnncmp(const A& first, const B& second) {
    return ncmpImpl(first, second, false, true);
}

enum class NCompareType { str, dec };

struct NCompareItem {
    NCompareType type{NCompareType::str};
    String str;
    String number;

    [[nodiscard]] bool operator==(const NCompareItem& other) const noexcept;
    [[nodiscard]] int compare(const NCompareItem& other) const noexcept;
    [[nodiscard]] int opCmp(const NCompareItem& other) const noexcept {
        return compare(other);
    }
    [[nodiscard]] bool operator<(const NCompareItem& other) const noexcept {
        return compare(other) < 0;
    }
};

struct NCompare {
    String value;
    std::vector<NCompareItem> items;

    NCompare() = default;
    explicit NCompare(String value, bool ignore_case = false,
                      bool logical_sort = true);
    [[nodiscard]] static NCompare opCall(String value, bool ignore_case = false,
                                          bool logical_sort = true);
    [[nodiscard]] bool operator==(const NCompare& other) const noexcept;
    [[nodiscard]] int compare(const NCompare& other) const noexcept;
    [[nodiscard]] int opCmp(const NCompare& other) const noexcept {
        return compare(other);
    }
    [[nodiscard]] bool operator<(const NCompare& other) const noexcept {
        return compare(other) < 0;
    }
    [[nodiscard]] bool operator>(const NCompare& other) const noexcept {
        return compare(other) > 0;
    }
    [[nodiscard]] bool operator<=(const NCompare& other) const noexcept {
        return compare(other) <= 0;
    }
    [[nodiscard]] bool operator>=(const NCompare& other) const noexcept {
        return compare(other) >= 0;
    }
};

template <typename A, typename B>
[[nodiscard]] int ncmpImpl(const A& first, const B& second,
                           bool ignore_case,
                           bool filename_case) {
    const auto left = detail::naturalCodepoints(first);
    const auto right = detail::naturalCodepoints(second);
    return detail::naturalCompare(left, right, ignore_case, filename_case);
}

template <typename C>
[[nodiscard]] std::basic_string<C> zfill_(std::basic_string_view<C> value,
                                           std::size_t width) {
    std::basic_string<C> result;
    if (value.size() < width) result.assign(width - value.size(), static_cast<C>('0'));
    result.append(value);
    return result;
}

template <typename C, std::size_t N>
[[nodiscard]] std::basic_string<C> zfill_(const C (&value)[N], std::size_t width) {
    return zfill_(std::basic_string_view<C>(value, N > 0U ? N - 1U : 0U), width);
}

template <typename T>
[[nodiscard]] std::vector<T>& remove(std::vector<T>& values, const T& value) {
    const auto it = std::find(values.begin(), values.end(), value);
    if (it != values.end()) values.erase(it);
    return values;
}

template <typename T, typename Predicate>
[[nodiscard]] std::vector<T>& remove(std::vector<T>& values, const T& value,
                                     Predicate predicate) {
    const auto it = std::find_if(values.begin(), values.end(),
                                 [&](const T& item) { return predicate(value, item); });
    if (it != values.end()) values.erase(it);
    return values;
}

template <typename T>
class HashSet {
public:
    void add(const T& value) { values_.insert(value); }
    void remove(const T& value) { values_.erase(value); }
    void clear() noexcept { values_.clear(); }
    [[nodiscard]] bool contains(const T& value) const {
        return values_.find(value) != values_.end();
    }
    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
    [[nodiscard]] bool isEmpty() const noexcept { return values_.empty(); }
    template <typename Function>
    int forEach(Function function) const {
        for (auto value : values_) {
            if constexpr (std::is_void_v<std::invoke_result_t<Function, T&>>) function(value);
            else if (const int result = function(value)) return result;
        }
        return 0;
    }
    [[nodiscard]] std::vector<T> toArray() const {
        return std::vector<T>(values_.begin(), values_.end());
    }

private:
    std::unordered_set<T> values_;
};

inline constexpr int STARTF_USESHOWWINDOW = 0x01;
inline constexpr int CREATE_NEW_CONSOLE = 0x10;
[[nodiscard]] bool exec(StringView process, const Path& work_dir = {},
                        bool console = true, bool wait = false);
[[nodiscard]] bool hasPath(const Path& root, const Path& path);
[[nodiscard]] bool hasPath(StringView root, StringView path);

// 公開の集計型はsize_tのまま保ち、FileCacheの並
// 行更新に使うatomic_refが必要とする境界へ配置する。
alignas(std::atomic_ref<std::size_t>::required_alignment)
inline std::size_t cacheSize = 0;
// 64ビット環境でもDの32ビット中間演算があふれる不具合を修
// 正する。 広い整数で意図した32GiBの上限を保ち、0へ巻
// き戻って正のサイズのキャッシュがすべて無効になるのを防ぐ。
inline constexpr std::size_t CACHE_SIZE_MAX = sizeof(void*) == 8
                                                  ? 32ULL * 1024ULL * 1024ULL * 1024ULL
                                                  : 512ULL * 1024ULL * 1024ULL;

namespace detail {
[[nodiscard]] bool cachePathExists(const Path& path) noexcept;
[[nodiscard]] Path fileCacheKey(const Path& path);
[[nodiscard]] std::int64_t cacheModifiedTime(const Path& path);
[[nodiscard]] inline std::atomic_ref<std::size_t> cacheSizeRef() noexcept {
    return std::atomic_ref<std::size_t>(cacheSize);
}
}

template <typename T>
struct FileCache {
    struct Cache {
        // DのSysTimeのUTC stdTime。西暦1年1月1日から100ns単位で表
        // す。 この刻みを保ち、ネイティブのファイル時計の起点・範囲・精度の違いを避ける。
        std::int64_t ftm{};
        T value{};
        std::size_t size{};
    };

    static constexpr std::size_t CACHE_MAX = 1024;
    // Dの可変の静的キャッシュはスレッドローカル。 __gshared
    // のcacheSize集計だけを全スレッドで共有する。
    inline static thread_local std::map<Path, Cache> caches{};
    inline static thread_local std::vector<Path> cachePaths{};

    [[nodiscard]] static Path cacheKey(const Path& path) {
        return detail::fileCacheKey(path);
    }

    [[nodiscard]] static Path cacheKey(const Path& path, std::error_code& error) {
        // 追加のネイティブのエラーコード版は残す。D互換のキャッシュ操作は例外
        // を投げる版を使い、 nabsの失敗をキャッシュ未登録とみなさない。
        error.clear();
        try {
            return cacheKey(path);
        } catch (const std::filesystem::filesystem_error& failure) {
            error = failure.code();
            return {};
        }
    }

    void putCache(const Path& path, T value, std::size_t size) {
        if (!detail::cachePathExists(path)) return;
        if (size > CACHE_SIZE_MAX) return;
        const auto key = cacheKey(path);
        while (!cachePaths.empty() &&
               (cachePaths.size() >= CACHE_MAX ||
                (size != 0 && detail::cacheSizeRef().load(std::memory_order_relaxed) +
                                  size > CACHE_SIZE_MAX))) {
            const auto old = cachePaths.front();
            const auto found = caches.find(old);
            // Dの連想配列の欠落やアサーション違反は、ネイティブの不
            // 変条件違反の例外へ変換する。 破損を黙って修復しない。
            if (found == caches.end()) throw std::logic_error("Missing cache eviction entry");
            detail::cacheSizeRef().fetch_sub(found->second.size,
                                             std::memory_order_relaxed);
            caches.erase(found);
            cachePaths.erase(cachePaths.begin());
        }
        const auto update_size = [key](std::size_t old_size, std::size_t new_size) {
            auto entry = caches.find(key);
            if (entry == caches.end() || entry->second.size != old_size)
                throw std::logic_error("Invalid cached size update");
            detail::cacheSizeRef().fetch_add(new_size, std::memory_order_relaxed);
            detail::cacheSizeRef().fetch_sub(old_size, std::memory_order_relaxed);
            entry->second.size = new_size;
        };
        if constexpr (requires { value->updateSizeEvent.push_back(update_size); }) {
            // Dのnullオブジェクトへのメンバーアクセスは失敗する。C++のnull参照による未定義動作は避ける。
            if constexpr (requires { static_cast<bool>(value); }) {
                if (!value) throw std::logic_error("Null cached size-event owner");
            }
            value->updateSizeEvent.push_back(update_size);
        } else if constexpr (requires { value.updateSizeEvent.push_back(update_size); })
            value.updateSizeEvent.push_back(update_size);
        const auto found = caches.find(key);
        if (found != caches.end())
            detail::cacheSizeRef().fetch_sub(found->second.size,
                                             std::memory_order_relaxed);
        else cachePaths.push_back(key);
        caches[key] = Cache{detail::cacheModifiedTime(key),
                            std::move(value), size};
        detail::cacheSizeRef().fetch_add(size, std::memory_order_relaxed);
    }

    [[nodiscard]] Cache* cache(const Path& path) {
        if (!detail::cachePathExists(path)) return nullptr;
        const auto key = cacheKey(path);
        const auto found = caches.find(key);
        if (found == caches.end()) return nullptr;
        return found->second.ftm == detail::cacheModifiedTime(key)
                   ? &found->second
                   : nullptr;
    }
};

[[nodiscard]] bool hasParDir(StringView path);
[[nodiscard]] std::size_t icount(StringView value, StringView sub);
[[nodiscard]] String ireplace(String value, StringView from, StringView to);

template <typename Number, std::size_t Count = 3>
[[nodiscard]] String formatNum(Number value, StringView separator = ",") {
    std::ostringstream stream;
    stream << value;
    String text = stream.str();
    static_assert(Count > 0);
    const std::size_t start = 0;
    String result;
    for (std::size_t end = text.size(); end > start;) {
        const auto begin = end >= Count + start ? end - Count : start;
        if (!result.empty()) result.insert(0, separator);
        result.insert(0, text.substr(begin, end - begin));
        end = begin;
    }
    return result;
}

template <typename Map>
[[nodiscard]] Map dupAssocArray(const Map& values) {
    return values;
}

[[nodiscard]] std::size_t lengthJ(StringView text);
[[nodiscard]] std::size_t lineCount(const lib::Array<String>& lines);

template <typename Range, typename Value>
[[nodiscard]] std::ptrdiff_t cCountUntil(const Range& range, const Value& value) {
    std::ptrdiff_t index = 0;
    for (const auto& current : range) {
        if (current == value) return index;
        ++index;
    }
    return -1;
}

template <typename Range, typename Value, typename Predicate>
[[nodiscard]] std::ptrdiff_t cCountUntil(const Range& range, const Value& value,
                                          Predicate predicate) {
    std::ptrdiff_t index = 0;
    for (const auto& current : range) {
        if (predicate(current, value)) return index;
        ++index;
    }
    return -1;
}

[[nodiscard]] String sliceJ(StringView text, std::size_t from, std::size_t to);
[[nodiscard]] String astrip(String value);
[[nodiscard]] String astripl(String value);
// Dのstd.string.isNumeric相当。bAllowSepは既定のfalseとする。
[[nodiscard]] bool isNumeric(StringView value);
[[nodiscard]] String astripr(String value);
[[nodiscard]] bool cglobMatch(StringView value, StringView pattern);
[[nodiscard]] bool cfnmatch(StringView first, StringView second);
[[nodiscard]] bool isVariableName(StringView name) noexcept;
[[nodiscard]] String variableName(StringView name);
[[nodiscard]] bool isFileNameChar(char32_t value) noexcept;
[[nodiscard]] String toFileName(String value, char32_t replacement = U'_');
[[nodiscard]] Path exeName(const Path& args0);
[[nodiscard]] std::size_t workingMemory() noexcept;

template <typename Iterator>
[[nodiscard]] bool isUniform(Iterator first, Iterator last) {
    if (first == last) return true;
    const auto value = *first++;
    for (; first != last; ++first) {
        if (!(*first == value)) return false;
    }
    return true;
}

template <typename Range>
[[nodiscard]] bool isUniform(const Range& range) {
    return isUniform(std::begin(range), std::end(range));
}

}  // namespace cwxeditor::cwx::utils
