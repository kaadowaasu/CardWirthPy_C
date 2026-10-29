#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "cwxeditor_src/lib/utf8.hpp"

namespace cwxeditor::cwx::enumutils {

/**
 * 任意の列挙型メタデータ。  C++20には列挙型
 * のリフレクションがないため、この特性を特殊化し
 * て宣言順を明示できる。      enum class
 * State { Ready, Busy };
 * template<> struct
 * enum_traits<State> {
 * inline static constexpr
 * std::array values{
 * State::Ready,
 * State::Busy};     };
 * names配列は省略でき、その場合はコン
 * パイラーの列挙名を使う。 未登録の列挙型で
 * もenum_range<E>内の値について
 * 、可能な範囲で同じコンパイラー表記を使う。
 */
template <typename E>
struct enum_traits {
    static_assert(std::is_enum_v<E>, "enum_traits requires an enum type");
    inline static constexpr std::array<E, 0> values{};
};

/** enum_traits<E>が未指定の場合に、列挙情報の代替取得で使う範囲。 */
template <typename E>
struct enum_range {
    static_assert(std::is_enum_v<E>, "enum_range requires an enum type");

    // テンプレートの実体化が過大にならないよう、既定の範囲は小さ
    // くする。 値が飛び飛びの列挙や大きな列挙では、enum_traits
    // またはenum_rangeを明示的に特殊化する。
    inline static constexpr std::intmax_t min =
        std::is_signed_v<std::underlying_type_t<E>> ? -128 : 0;
    inline static constexpr std::intmax_t max =
        std::is_signed_v<std::underlying_type_t<E>> ? 128 : 256;
};

template <typename E>
struct enum_member {
    E value{};
    std::string_view name{};
};

namespace detail {

constexpr bool identifier_start(char value) noexcept {
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z') || value == '_';
}

constexpr bool identifier_continue(char value) noexcept {
    return identifier_start(value) || (value >= '0' && value <= '9');
}

template <typename E, E Value>
consteval std::string_view compiler_enum_name() noexcept {
#if defined(_MSC_VER)
    constexpr std::string_view function = __FUNCSIG__;
    // MSVCはGCC/ClangのValue = ...ではなく、 compiler_enum_name<enum
    // ns::Kind,ns::Kind::Member>(void)の
    // ような テンプレート引数表記を使うため、2番目の引数を直接解析する。
    constexpr std::string_view template_name = "compiler_enum_name<";
    const auto template_begin = function.find(template_name);
    if (template_begin == std::string_view::npos) return {};
    const auto begin = template_begin + template_name.size();
    auto end = function.find(">(void", begin);
    if (end == std::string_view::npos) end = function.find(">(", begin);
    if (end == std::string_view::npos || end <= begin) return {};

    // Eがテンプレート内の型なら、その内部のカンマはEとValueの区切りではない。
    std::size_t separator = std::string_view::npos;
    std::size_t depth = 0;
    for (std::size_t index = begin; index < end; ++index) {
        if (function[index] == '<') {
            ++depth;
        } else if (function[index] == '>' && depth != 0) {
            --depth;
        } else if (function[index] == ',' && depth == 0) {
            separator = index;
            break;
        }
    }
    if (separator == std::string_view::npos || separator + 1 >= end) return {};
    auto spelling = function.substr(separator + 1, end - separator - 1);
#else
    constexpr std::string_view function = __PRETTY_FUNCTION__;
    auto marker = function.find("Value = ");
    std::size_t marker_length = 8;
    if (marker == std::string_view::npos) {
        marker = function.find("Value=");
        marker_length = 6;
    }
    if (marker == std::string_view::npos) return {};

    const std::size_t begin = marker + marker_length;
    std::size_t end = function.find_first_of("];,", begin);
    if (end == std::string_view::npos) end = function.size();
    auto spelling = function.substr(begin, end - begin);
#endif
    while (!spelling.empty() && spelling.front() == ' ') spelling.remove_prefix(1);
    while (!spelling.empty() && spelling.back() == ' ') spelling.remove_suffix(1);

#if defined(_MSC_VER)
    // MSVCのシグネチャーには、非型の列挙引数の前にキャストが残る場合
    // がある。 それを除いた後も数値表記なら、識別子の検査で拒否する。
    while (!spelling.empty() && spelling.front() == '(') {
        const auto cast_end = spelling.find(')');
        if (cast_end == std::string_view::npos) return {};
        spelling.remove_prefix(cast_end + 1);
        while (!spelling.empty() && spelling.front() == ' ') spelling.remove_prefix(1);
    }
#endif

    // 無効な値はコンパイラーが(En)3などと表す。 有効な
    // 列挙名の修飾は除くが、このような数値表記は拒否する。
    const std::size_t qualifier = spelling.rfind("::");
    if (qualifier != std::string_view::npos) {
        const auto member_begin = qualifier + 2;
        if (member_begin > spelling.size()) return {};
        spelling = spelling.substr(member_begin);
    }
    if (spelling.empty() || !identifier_start(spelling.front())) return {};
    for (const char value : spelling) {
        if (!identifier_continue(value)) return {};
    }
    return spelling;
}

template <typename E>
concept has_trait_values = requires {
    std::size(enum_traits<E>::values);
};

template <typename E>
concept has_trait_names = requires {
    std::size(enum_traits<E>::names);
};

template <typename E, std::size_t... Index>
consteval auto make_trait_members(std::index_sequence<Index...>) {
    constexpr std::size_t count = sizeof...(Index);
    std::array<enum_member<E>, count> result{};
    std::size_t output = 0;
    ((result[output++] = enum_member<E>{
          enum_traits<E>::values[Index],
          [&] {
              if constexpr (has_trait_names<E>) {
                  if constexpr (std::size(enum_traits<E>::names) == count) {
                      return std::string_view(enum_traits<E>::names[Index]);
                  } else {
                      return compiler_enum_name<E, enum_traits<E>::values[Index]>();
                  }
              } else {
                  return compiler_enum_name<E, enum_traits<E>::values[Index]>();
              }
          }()}), ...);
    return result;
}

template <typename E, std::intmax_t Minimum, std::size_t... Index>
consteval auto make_reflected_members(std::index_sequence<Index...>) {
    constexpr std::size_t count = sizeof...(Index);
    constexpr auto all = std::array{
        enum_member<E>{
            static_cast<E>(Minimum + static_cast<std::intmax_t>(Index)),
            compiler_enum_name<E, static_cast<E>(Minimum + static_cast<std::intmax_t>(Index))>()}...};

    constexpr std::size_t valid_count = [&] {
        std::size_t count_value = 0;
        for (const auto& member : all) {
            if (!member.name.empty()) ++count_value;
        }
        return count_value;
    }();

    std::array<enum_member<E>, valid_count> result{};
    std::size_t output = 0;
    for (const auto& member : all) {
        if (!member.name.empty()) result[output++] = member;
    }
    return result;
}

template <typename E>
consteval auto make_fallback_members() {
    constexpr auto minimum = enum_range<E>::min;
    constexpr auto maximum = enum_range<E>::max;
    static_assert(maximum >= minimum, "enum_range<E>::max must not precede min");
    constexpr auto width = static_cast<std::uintmax_t>(maximum - minimum) + 1;
    static_assert(width <= 4096, "enum_range<E> is too wide");
    return make_reflected_members<E, minimum>(std::make_index_sequence<width>{});
}

template <typename E>
consteval auto make_members() {
    static_assert(std::is_enum_v<E>, "enum utilities require an enum type");
    if constexpr (has_trait_values<E>) {
        if constexpr (std::size(enum_traits<E>::values) != 0) {
            return make_trait_members<E>(
                std::make_index_sequence<std::size(enum_traits<E>::values)>{});
        } else {
            return make_fallback_members<E>();
        }
    } else {
        return make_fallback_members<E>();
    }
}

template <typename E>
inline constexpr auto members_storage = make_members<E>();

// Dのstd.convは列挙の基底値を通常の値の書式で表す。 char
// はUTF-8のコード単位、wchar/dcharはUTF-8のスカラ
// ー値、boolは単語、byte/ubyteは十進数となる。 C++の
// 数値としての文字型とは区別し、名前のない列挙値もDと同じ表記にする。
template <typename Underlying>
void append_enum_base(std::string& output, const Underlying value) {
    if constexpr (std::is_same_v<Underlying, bool>) {
        output += value ? "true" : "false";
    } else if constexpr (std::is_same_v<Underlying, char>) {
        output.push_back(value);
#if defined(__cpp_char8_t)
    } else if constexpr (std::is_same_v<Underlying, char8_t>) {
        output.push_back(static_cast<char>(value));
#endif
    } else if constexpr (std::is_same_v<Underlying, char16_t> ||
                         std::is_same_v<Underlying, char32_t>) {
        ::cwxeditor::lib::utf8::append_scalar(
            output, static_cast<std::uint32_t>(value));
    } else if constexpr (std::is_same_v<Underlying, wchar_t>) {
        if constexpr (sizeof(Underlying) == sizeof(char16_t)) {
            // Windowsのwchar_tは、Dのwcharと同じUTF-16のコード単位。
            const auto unit = static_cast<std::uint16_t>(
                static_cast<std::make_unsigned_t<Underlying>>(value));
            ::cwxeditor::lib::utf8::append_scalar(output, unit);
        } else if constexpr (sizeof(Underlying) == sizeof(char32_t)) {
            // wchar_tが32ビットの環境では、Dのdcharのスカラー値として扱う。
            const auto scalar = static_cast<std::uint32_t>(
                static_cast<std::make_unsigned_t<Underlying>>(value));
            ::cwxeditor::lib::utf8::append_scalar(output, scalar);
        } else {
            static_assert(sizeof(Underlying) == sizeof(char16_t) ||
                              sizeof(Underlying) == sizeof(char32_t),
                          "wchar_t must have a supported Unicode width");
        }
    } else if constexpr (std::is_signed_v<Underlying>) {
        output += std::to_string(static_cast<std::intmax_t>(value));
    } else {
        output += std::to_string(static_cast<std::uintmax_t>(value));
    }
}

template <typename E>
std::string make_case(std::string_view enum_name,
                      std::string_view prefix,
                      std::size_t index,
                      bool assign,
                      std::string_view value_name) {
    const auto member = members_storage<E>[index];
    std::string result{"\tcase "};
    result += enum_name;
    result += '.';
    result += member.name;
    result += assign ? ": " : ": return ";
    result += prefix;
    result += member.name;
    if (assign) {
        result += " = ";
        result += value_name;
        result += "; break;\n";
    } else {
        result += ";\n";
    }
    return result;
}

template <typename E>
std::string make_switch(std::string_view enum_name,
                        std::string_view prefix,
                        std::string_view value_name) {
    std::string result{"final switch (id) {\n"};
    for (std::size_t index = 0; index < members_storage<E>.size(); ++index) {
        result += make_case<E>(enum_name, prefix, index, !value_name.empty(), value_name);
    }
    result += '}';
    return result;
}

template <typename E>
std::string make_method(std::string_view enum_name,
                        std::string_view method_name,
                        std::string_view prefix,
                        std::string_view value_name) {
    std::string result{"const string "};
    result += method_name;
    result += '(';
    result += enum_name;
    result += " id) {\n\tfinal switch (id) {\n";
    for (std::size_t index = 0; index < members_storage<E>.size(); ++index) {
        result += make_case<E>(enum_name, prefix, index, !value_name.empty(), value_name);
    }
    result += "\t}\n}";
    return result;
}

template <typename E>
consteval std::string_view compiler_type_name() noexcept {
#if defined(_MSC_VER)
    constexpr std::string_view function = __FUNCSIG__;
    const auto begin = function.find("compiler_type_name<") + 19;
    const auto end = function.find(">(void)", begin);
#else
    constexpr std::string_view function = __PRETTY_FUNCTION__;
    const auto marker = function.find("E = ");
    if (marker == std::string_view::npos) return {};
    const auto begin = marker + 4;
    const auto end = function.find_first_of(";]", begin);
#endif
    if (begin == std::string_view::npos || end == std::string_view::npos || end <= begin) return {};
    return function.substr(begin, end - begin);
}

template <typename E>
consteval std::string_view enum_type_identifier() noexcept {
    auto name = compiler_type_name<E>();
    // C++の型情報には言語のキーワードや名前空間の修飾が含ま
    // れる場合がある。 DのType.stringofは、これ
    // らの列挙ではMTypeなど宣言された識別子だけを返す。
    if (name.starts_with("enum class ")) name.remove_prefix(11);
    else if (name.starts_with("enum ")) name.remove_prefix(5);
    else if (name.starts_with("class ")) name.remove_prefix(6);
    else if (name.starts_with("struct ")) name.remove_prefix(7);
    const auto qualifier = name.rfind("::");
    if (qualifier != std::string_view::npos) name.remove_prefix(qualifier + 2);
    return name;
}

template <typename C, typename E>
concept has_static_create = requires(E value) {
    C::create(value);
};

template <typename C, typename E>
concept has_static_apply = requires(E value) {
    C::apply(value);
};

template <typename C, typename E>
concept has_call_operator = requires(C creator, E value) {
    creator(value);
};

template <typename Value>
std::string creator_result(Value&& value) {
    return std::string(std::string_view(std::forward<Value>(value)));
}

template <typename C, typename E>
std::string invoke_creator(E value) {
    if constexpr (has_static_create<C, E>) {
        return creator_result(C::create(value));
    } else if constexpr (has_static_apply<C, E>) {
        return creator_result(C::apply(value));
    } else if constexpr (has_call_operator<C, E>) {
        return creator_result(C{}(value));
    } else {
        static_assert(has_static_create<C, E> || has_static_apply<C, E> ||
                          has_call_operator<C, E>,
                      "EnumToMembers creator must expose create, apply, or operator()");
    }
}

template <typename E, typename C>
std::vector<std::string> make_created_members() {
    std::vector<std::string> result;
    result.reserve(members_storage<E>.size());
    for (const auto member : detail::members_storage<E>) {
        result.push_back(invoke_creator<C>(member.value));
    }
    return result;
}

inline std::string join(const std::vector<std::string>& values) {
    std::string result;
    for (const auto& value : values) result += value;
    return result;
}

}  // namespace detail

/**
 * 宣言済みの列挙名の先頭ASCII文字を小文字にして返す。 名前のない
 * 数値はこのAPIの対象外として拒否する。 Dのcast(Type)形
 * 式への代替保存が必要ならenumToStringExactを使う。
 */
template <typename E>
std::string enumToString(E value) {
    static_assert(std::is_enum_v<E>, "enumToString requires an enum type");
    std::string name;
    for (const auto member : detail::members_storage<E>) {
        if (member.value == value) {
            name.assign(member.name);
            break;
        }
    }
    if (name.empty()) throw std::invalid_argument("value is not an enum member");
    if (name.front() >= 'A' && name.front() <= 'Z') {
        name.front() = static_cast<char>(name.front() - 'A' + 'a');
    }
    return name;
}

/** 先頭が小文字のキャメルケースの列挙名を、列挙値へ戻す。 */
template <typename E>
E stringToEnum(std::string_view name) {
    static_assert(std::is_enum_v<E>, "stringToEnum requires an enum type");
    if (name.empty()) throw std::invalid_argument("enum name is empty");

    std::string converted{name};
    if (converted.front() >= 'a' && converted.front() <= 'z') {
        converted.front() = static_cast<char>(converted.front() - 'a' + 'A');
    }
    for (const auto member : detail::members_storage<E>) {
        if (member.name == converted) return member.value;
    }
    throw std::invalid_argument("unknown enum member: " + converted);
}

/**
 * XMLや設定値に使う、Dのstd.conv互換の列挙表記。 enumToString
 * と異なり宣言どおりの名前を保ち、名前のない値はcast(TypeName)
 * の後に基底値を続ける。 同じ値が複数ある場合はDのto!string
 * と同じく最初のメンバーを使う。 厳密な解析で別名を残す
 * にはenum_traitsを特殊化する。 コンパイラー
 * の情報だけでは、別名を持つ値の代表名しか取得できない。
 */
template <typename E>
std::string enumToStringExact(E value) {
    static_assert(std::is_enum_v<E>, "enumToStringExact requires an enum type");
    using Underlying = std::underlying_type_t<E>;
    for (const auto member : detail::members_storage<E>) {
        if (member.value != value || member.name.empty()) continue;
        if constexpr (std::is_same_v<Underlying, bool>) {
            // MSVCは名前のないbool基底の値をtrue/falseと表す
            // 場合がある。 これらはcast(Type)形式の代替表記に残す。
            if (member.name == "true" || member.name == "false") continue;
        }
        return std::string(member.name);
    }

    std::string result{"cast("};
    result += detail::enum_type_identifier<E>();
    result += ')';
    detail::append_enum_base(result, static_cast<Underlying>(value));
    return result;
}

/** Dの大文字・小文字を区別するstd.conv.toと同じく、宣言どおりの列挙名を解析する。 */
template <typename E>
E stringToEnumExact(std::string_view name) {
    static_assert(std::is_enum_v<E>, "stringToEnumExact requires an enum type");
    using Underlying = std::underlying_type_t<E>;
    for (const auto member : detail::members_storage<E>) {
        if (member.name.empty() || member.name != name) continue;
        if constexpr (std::is_same_v<Underlying, bool>) {
            // MSVCは名前のないbool基底をリテラルとして公開する場合があるが、 Dのstd.conv
            // は列挙の識別子だけを受け付け、true/falseは認めない。
            if (member.name == "true" || member.name == "false") continue;
        }
        return member.value;
    }
    throw std::invalid_argument("unknown enum member: " + std::string(name));
}

/** 宣言順、または特性で指定した順の列挙メンバー。 */
template <typename E>
constexpr std::span<const enum_member<E>> enumMembers() noexcept {
    return std::span<const enum_member<E>>{detail::members_storage<E>.data(),
                                           detail::members_storage<E>.size()};
}

template <typename E>
inline constexpr std::size_t enumMemberCount = detail::members_storage<E>.size();

/** DのEnumMembers!Eによるコンパイル時のメンバー列に対応するC++表現。 */
template <typename E>
inline constexpr auto EnumMembers = detail::members_storage<E>;

/** 後続のソース生成テンプレートで使う固定文字列。 */
template <std::size_t N>
struct fixed_string {
    char value[N]{};

    constexpr fixed_string(const char (&text)[N]) noexcept {
        for (std::size_t index = 0; index < N; ++index) value[index] = text[index];
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return {value, N - 1};
    }
};

template <std::size_t N>
fixed_string(const char (&)[N]) -> fixed_string<N>;

template <typename E,
          fixed_string EName,
          fixed_string Prefix,
          std::size_t Index>
struct EnumToStringCase {
    static_assert(Index < enumMemberCount<E>, "EnumToStringCase index is out of range");
    inline static const std::string value =
        detail::make_case<E>(EName.view(), Prefix.view(), Index, false, {});
    inline static const std::string text = value;
};

template <typename E,
          fixed_string EName,
          fixed_string Prefix,
          fixed_string ValueName,
          std::size_t Index>
struct EnumToStringCaseSet {
    static_assert(Index < enumMemberCount<E>, "EnumToStringCaseSet index is out of range");
    inline static const std::string value =
        detail::make_case<E>(EName.view(), Prefix.view(), Index, true, ValueName.view());
    inline static const std::string text = value;
};

template <typename E,
          fixed_string EName,
          fixed_string Prefix,
          fixed_string ValueName = "">
struct EnumToStringSwitch2 {
    inline static const std::string value =
        detail::make_switch<E>(EName.view(), Prefix.view(), ValueName.view());
    inline static const std::string text = value;

    [[nodiscard]] static const std::string& get() noexcept { return value; }
};

template <typename E, fixed_string Prefix, fixed_string ValueName = "">
struct EnumToStringSwitch {
    inline static const std::string value =
        detail::make_switch<E>(detail::compiler_type_name<E>(), Prefix.view(), ValueName.view());
    inline static const std::string text = value;

    [[nodiscard]] static const std::string& get() noexcept { return value; }
};

template <typename E,
          fixed_string EName,
          fixed_string MethodName,
          fixed_string Prefix,
          fixed_string ValueName = "">
struct EnumToStringMethod2 {
    inline static const std::string value = detail::make_method<E>(
        EName.view(), MethodName.view(), Prefix.view(), ValueName.view());
    inline static const std::string text = value;

    [[nodiscard]] static const std::string& get() noexcept { return value; }
};

template <typename E, fixed_string MethodName, fixed_string Prefix>
struct EnumToStringMethod {
    inline static const std::string value = detail::make_method<E>(
        detail::compiler_type_name<E>(), MethodName.view(), Prefix.view(), {});
    inline static const std::string text = value;

    [[nodiscard]] static const std::string& get() noexcept { return value; }
};

/**
 * Dのmixinを生成するEnumToMembersテンプレートに対応
 * するC++表現。 generatedはCreateString(member)
 * の結果を連結し、members()は各断片を個別に返す。 CreateString
 * は静的なcreate/apply関数、または既定構築可
 * 能で 文字列相当の値を返す呼び出し演算子を提供できる。
 */
template <typename E, typename CreateString>
struct EnumToMembers {
    using creator_type = CreateString;

    template <std::size_t Index>
    struct EnumToMember {
        static_assert(Index < enumMemberCount<E>, "EnumToMember index is out of range");
        inline static constexpr E value = detail::members_storage<E>[Index].value;
        inline static constexpr std::string_view name = detail::members_storage<E>[Index].name;
        inline static const std::string generated = detail::invoke_creator<CreateString>(value);
        inline static const std::string text = generated;
    };

    inline static const std::vector<std::string> members_storage =
        detail::make_created_members<E, CreateString>();
    inline static const std::string generated = detail::join(members_storage);
    inline static const std::string value = generated;

    [[nodiscard]] static std::span<const std::string> members() noexcept {
        return {members_storage.data(), members_storage.size()};
    }

    [[nodiscard]] static const std::string& get() noexcept { return generated; }
};

/** 入力がすべて大文字の場合に限り、先頭のASCII文字だけを変換する。 */
std::string upperToCap(std::string_view value);
/** 先頭のUnicodeコードポイントを小文字にする。 */
std::string capLower(std::string_view value);
/** 先頭のUnicodeコードポイントを大文字にする。 */
std::string capUpper(std::string_view value);
// Dのstd.uniの文字列変換。byDcharの置換付き復号、元の
// バイト接頭部分、 先頭位置での変換境界をすべてのOSで共通に扱う。
std::string lowerCase(std::string_view value);
// DのindexOf(CaseSensitive.no)はdcharのtoLowerで比較し、文字列への展開は行わない。
std::string simpleLowerCase(std::string_view value, bool replaceInvalid = false);
std::string upperCase(std::string_view value);
void validateUtf8(std::string_view value);
struct Utf8Scalar {
    char32_t value{};
    std::size_t next{};
};
// offset位置のスカラー値だけを復号する。途中で探索を終える場合は、
// Dのforeachが訪れない後方の不正データまで検査して拒否しない。
[[nodiscard]] Utf8Scalar decodeUtf8Scalar(std::string_view value, std::size_t offset);
std::u32string toUtf32(std::string_view value);
// PhobosのtoUTF32はbyUTF!dcharの既定の置換規則を使う。 Dのforeach
// とstd.conv.to!dstringには、上の厳密なデコーダーを使う。
std::u32string toUtf32Replacing(std::string_view value);
std::u32string toUtf32(std::u16string_view value);
// Phobosのicmpと同じく、ASCIIの短縮処理後に、順序付きの候補を使うfullCasedCmpを行う。
int compareIgnoreCase(std::string_view first, std::string_view second);
int compareIgnoreCase(std::u32string_view first, std::u32string_view second);
// PathIdの順序に使う、Dのstd.string.icmpと同じ候補列の厳密な照合。
int compareDStringIgnoreCase(std::string_view first, std::string_view second);
// 走査と前後の空白除去で共用する、PhobosのWhite_Space判定。
[[nodiscard]] constexpr bool isWhiteSpace(char32_t c) noexcept {
    return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xa0 ||
           c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
           c == 0x2028 || c == 0x2029 || c == 0x202f ||
           c == 0x205f || c == 0x3000;
}
// Dのstd.string.stripと同じく、両端を復号してstd.uni.isWhiteを使う。
std::string stripWhiteSpace(std::string_view value);
char32_t lowerScalar(char32_t value) noexcept;
char32_t upperScalar(char32_t value) noexcept;

}  // namespace cwxeditor::cwx::enumutils
