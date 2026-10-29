#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <tuple>

#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/array_slice.hpp"
#include "cwxeditor_src/lib/scalar_conversion.hpp"
#include "cwxeditor_src/lib/scalar_format.hpp"
#include "cwxeditor_src/lib/utf8.hpp"
#include "cwxeditor_src/lib/xml.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

namespace cwxeditor::cwx::settings {

namespace detail {

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T>
struct is_vector : std::false_type {};

template <typename T, typename Allocator>
struct is_vector<std::vector<T, Allocator>> : std::true_type {
    using value_type = T;
};

template <typename T>
inline constexpr bool is_vector_v = is_vector<remove_cvref_t<T>>::value;

template <typename T>
struct is_shared_slice : std::false_type {};

template <typename T>
struct is_shared_slice<::cwxeditor::lib::SharedSlice<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_shared_slice_v = is_shared_slice<remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool is_dynamic_array_v = is_vector_v<T> || is_shared_slice_v<T>;

// 両方のネイティブ変換で同じXML配列の規則を使う。 共有スライスは
// 構造変更時に自身の記述子を付け替え、D形式の他の参照を維持する。
template <typename Sequence>
    requires is_dynamic_array_v<Sequence>
void clear_dynamic_array(Sequence& value) {
    if constexpr (is_shared_slice_v<Sequence>) value = {};
    else value.clear();
}

template <typename Sequence>
    requires is_dynamic_array_v<Sequence>
void append_dynamic_array(Sequence& value,
                          typename remove_cvref_t<Sequence>::value_type item) {
    if constexpr (is_shared_slice_v<Sequence>)
        ::cwxeditor::lib::appendToSlice(value, std::move(item));
    else value.push_back(std::move(item));
}

template <typename T>
struct is_array : std::false_type {};

template <typename T, std::size_t N>
struct is_array<std::array<T, N>> : std::true_type {
    using value_type = T;
    static constexpr std::size_t size = N;
};

template <typename T>
inline constexpr bool is_array_v = is_array<remove_cvref_t<T>>::value;

template <typename T>
struct is_optional : std::false_type {};

template <typename T>
struct is_optional<std::optional<T>> : std::true_type {
    using value_type = T;
};

template <typename T>
inline constexpr bool is_optional_v = is_optional<remove_cvref_t<T>>::value;

template <typename T>
struct is_non_owning_read_value
    : std::bool_constant<std::is_pointer_v<remove_cvref_t<T>> ||
                         std::is_array_v<remove_cvref_t<T>> ||
                         std::ranges::view<remove_cvref_t<T>>> {};

template <typename T, typename Allocator>
struct is_non_owning_read_value<std::vector<T, Allocator>>
    : is_non_owning_read_value<remove_cvref_t<T>> {};

template <typename T>
struct is_non_owning_read_value<::cwxeditor::lib::SharedSlice<T>>
    : is_non_owning_read_value<remove_cvref_t<T>> {};

template <typename T, std::size_t N>
struct is_non_owning_read_value<std::array<T, N>>
    : is_non_owning_read_value<remove_cvref_t<T>> {};

template <typename T>
struct is_non_owning_read_value<std::optional<T>>
    : is_non_owning_read_value<remove_cvref_t<T>> {};

template <typename Key, typename Value, typename Compare, typename Allocator>
struct is_non_owning_read_value<std::map<Key, Value, Compare, Allocator>>
    : std::bool_constant<is_non_owning_read_value<remove_cvref_t<Key>>::value ||
                         is_non_owning_read_value<remove_cvref_t<Value>>::value> {};

template <typename T>
inline constexpr bool is_non_owning_read_value_v =
    is_non_owning_read_value<remove_cvref_t<T>>::value;

// XML読み込みでは変更可能な要素を構築する。const要素のスラ
// イスも所有権は持つが、 読み取り専用の参照であって、この変換処理
// の出力先にはしない。 オーバーロードの候補から明示的に除外する。
template <typename T>
struct supported_slice_read_value : std::true_type {};

template <typename T>
struct supported_slice_read_value<::cwxeditor::lib::SharedSlice<T>>
    : std::bool_constant<!std::is_const_v<T>> {};

template <typename T>
concept owning_read_value = !is_non_owning_read_value_v<T> &&
    supported_slice_read_value<remove_cvref_t<T>>::value;

template <typename T>
concept string_like = std::same_as<remove_cvref_t<T>, ::cwxeditor::lib::String> ||
                      std::same_as<remove_cvref_t<T>, ::cwxeditor::lib::StringView> ||
                      std::same_as<remove_cvref_t<T>, const char*> ||
                      std::same_as<remove_cvref_t<T>, char*>;

template <typename Node, typename T>
concept member_to_node_with_key = requires(const T& value, Node& node) {
    value.toNode(node, ::cwxeditor::lib::String{});
};

template <typename Node, typename T>
concept member_to_node = requires(const T& value, Node& node) {
    value.toNode(node);
};

template <typename Node, typename T>
concept member_from_node = requires(T& value, Node& node) {
    value.fromNode(node);
};

// XMLFuncsのプロパティ分類の判定は、プロパティ型に依存させる
// 。 汎用タプルラムダの中へ直接置くと、MSVCがKEY/INIT
// の検査を捨て、 Propの要素フィールドを黙って省く場合がある。
template <typename T>
concept has_property_storage = requires(const T& property) {
    property.storage;
};

template <typename T>
concept has_attribute_key = requires(const T& property) {
    property.ATTR_KEY;
};

template <typename T>
concept has_element_key = requires(const T& property) {
    property.KEY;
};

template <typename T>
concept has_initialized_element_key = requires(const T& property) {
    property.KEY;
    property.INIT;
};

template <typename T>
concept has_associative_array_key = requires(const T& property) {
    property.AA_KEY;
};

template <typename T>
::cwxeditor::lib::String string_value(const T& value);

template <typename T>
    requires owning_read_value<T>
T value_from_string(::cwxeditor::lib::StringView value);

template <typename T>
::cwxeditor::lib::String string_value(const T& value) {
    using U = remove_cvref_t<T>;
    if constexpr (std::same_as<U, ::cwxeditor::lib::String>) {
        return value;
    } else if constexpr (std::same_as<U, ::cwxeditor::lib::StringView>) {
        return ::cwxeditor::lib::String(value);
    } else if constexpr (std::same_as<U, char*> || std::same_as<U, const char*>) {
        return value == nullptr ? ::cwxeditor::lib::String{} : ::cwxeditor::lib::String(value);
    } else if constexpr (std::same_as<U, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::same_as<U, char>) {
        // DのcharはUTF-8の1コード単位であり、Unicodeのスカラー値ではない。
        return ::cwxeditor::lib::String(1, value);
    } else if constexpr (std::same_as<U, char16_t>) {
        return ::cwxeditor::lib::utf8::encode_scalar(static_cast<std::uint32_t>(value));
    } else if constexpr (std::same_as<U, char32_t>) {
        return ::cwxeditor::lib::utf8::encode_scalar(static_cast<std::uint32_t>(value));
    } else if constexpr (std::same_as<U, wchar_t>) {
        return ::cwxeditor::lib::utf8::encode_scalar(static_cast<std::uint32_t>(value));
    } else if constexpr (std::is_enum_v<U>) {
        return enumutils::enumToStringExact(value);
    } else if constexpr (std::is_integral_v<U>) {
        return std::to_string(value);
    } else if constexpr (::cwxeditor::lib::is_real_number_v<U>) {
        return ::cwxeditor::lib::scalar_format::floating_to_string(value);
    } else if constexpr (std::same_as<U, std::filesystem::path>) {
        const auto utf8 = value.u8string();
        return ::cwxeditor::lib::String(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    } else if constexpr (is_optional_v<U>) {
        return value ? string_value(*value) : ::cwxeditor::lib::String{};
    } else if constexpr (requires { value.toString(); }) {
        return string_value(value.toString());
    } else if constexpr (requires { value.string(); }) {
        return string_value(value.string());
    } else if constexpr (requires { std::declval<std::ostringstream&>() << value; }) {
        std::ostringstream stream;
        stream << value;
        return stream.str();
    } else {
        static_assert(std::is_same_v<U, void>, "settings value is not string-convertible");
    }
}

template <typename T>
    requires owning_read_value<T>
T value_from_string(::cwxeditor::lib::StringView value) {
    using U = remove_cvref_t<T>;
    if constexpr (std::same_as<U, ::cwxeditor::lib::String>) {
        return ::cwxeditor::lib::String(value);
    } else if constexpr (std::same_as<U, bool>) {
        // Dのstd.convと同じく、true/falseは大文字・小文字を区別
        // しない。 既存のC++設定の拡張として、数値の1/0も引き続き認める。
        const auto matches = [value](::cwxeditor::lib::StringView expected) {
            if (value.size() != expected.size()) return false;
            for (std::size_t index = 0; index < value.size(); ++index) {
                auto character = value[index];
                if (character >= 'A' && character <= 'Z')
                    character = static_cast<char>(character - 'A' + 'a');
                if (character != expected[index]) return false;
            }
            return true;
        };
        if (matches("true") || value == "1") return true;
        if (matches("false") || value == "0") return false;
        throw std::invalid_argument("invalid boolean property value: " + ::cwxeditor::lib::String(value));
    } else if constexpr (std::same_as<U, char>) {
        if (value.size() != 1U) {
            throw std::invalid_argument("character property value must contain exactly one UTF-8 code unit");
        }
        return value.front();
    } else if constexpr (std::same_as<U, char16_t>) {
        const auto scalar = ::cwxeditor::lib::utf8::decode_one_scalar(value);
        if (scalar > 0xFFFF) {
            throw std::invalid_argument("character property value does not fit one UTF-16 code unit");
        }
        return static_cast<char16_t>(scalar);
    } else if constexpr (std::same_as<U, char32_t>) {
        return ::cwxeditor::lib::utf8::decode_one_scalar(value);
    } else if constexpr (std::same_as<U, wchar_t>) {
        const auto scalar = ::cwxeditor::lib::utf8::decode_one_scalar(value);
        if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
            if (scalar > 0xFFFF) {
                throw std::invalid_argument("character property value does not fit one UTF-16 code unit");
            }
        }
        return static_cast<wchar_t>(scalar);
    } else if constexpr (std::is_enum_v<U>) {
        return enumutils::stringToEnumExact<U>(value);
    } else if constexpr (std::is_integral_v<U>) {
        try {
            return ::cwxeditor::lib::scalar_conversion::integral_from_string<U>(value);
        } catch (const std::invalid_argument&) {
            throw std::invalid_argument("invalid integral property value: " +
                                        ::cwxeditor::lib::String(value));
        }
    } else if constexpr (::cwxeditor::lib::is_real_number_v<U>) {
        try {
            return ::cwxeditor::lib::scalar_conversion::floating_from_string<U>(value);
        } catch (const std::invalid_argument&) {
            throw std::invalid_argument("invalid floating-point property value: " +
                                        ::cwxeditor::lib::String(value));
        }
    } else if constexpr (std::same_as<U, std::filesystem::path>) {
        std::u8string utf8;
        utf8.reserve(value.size());
        for (const char character : value) utf8.push_back(static_cast<char8_t>(character));
        return std::filesystem::path(utf8);
    } else if constexpr (requires { U::fromString(value); }) {
        return U::fromString(value);
    } else {
        static_assert(std::is_same_v<U, void>, "settings value is not string-constructible");
    }
}

template <typename Node>
::cwxeditor::lib::String node_text(Node& node) {
    if constexpr (requires { node.value(); }) {
        return string_value(node.value());
    } else if constexpr (requires { node.value; }) {
        return string_value(node.value);
    } else if constexpr (requires { node.text(); }) {
        return string_value(node.text());
    } else {
        static_assert(std::is_same_v<Node, void>, "node does not expose a value/text accessor");
    }
}

template <typename Node>
void set_node_text(Node& node, ::cwxeditor::lib::StringView value) {
    if constexpr (requires { node.value(::cwxeditor::lib::String{}); }) {
        node.value(::cwxeditor::lib::String(value));
    } else if constexpr (requires { node.setValue(::cwxeditor::lib::String{}); }) {
        node.setValue(::cwxeditor::lib::String(value));
    } else if constexpr (requires { node.value = ::cwxeditor::lib::String{}; }) {
        node.value = ::cwxeditor::lib::String(value);
    } else {
        static_assert(std::is_same_v<Node, void>, "node does not expose a mutable value accessor");
    }
}

template <typename Node>
::cwxeditor::lib::String node_name(Node& node) {
    if constexpr (requires { node.name(); }) {
        return string_value(node.name());
    } else if constexpr (requires { node.name; }) {
        return string_value(node.name);
    } else {
        return {};
    }
}

template <typename Node>
decltype(auto) new_element(Node& node, ::cwxeditor::lib::StringView key) {
    if constexpr (requires { node.newElement(::cwxeditor::lib::String{}); }) {
        return node.newElement(::cwxeditor::lib::String(key));
    } else if constexpr (requires { node.new_element(::cwxeditor::lib::String{}); }) {
        return node.new_element(::cwxeditor::lib::String(key));
    } else {
        static_assert(std::is_same_v<Node, void>, "node does not support creating child elements");
    }
}

template <typename Node>
decltype(auto) new_element(Node& node,
                           ::cwxeditor::lib::StringView key,
                           ::cwxeditor::lib::StringView value) {
    if constexpr (requires {
                      node.newElement(::cwxeditor::lib::String{}, ::cwxeditor::lib::String{});
                  }) {
        return node.newElement(::cwxeditor::lib::String(key), ::cwxeditor::lib::String(value));
    } else if constexpr (requires {
                             node.new_element(::cwxeditor::lib::String{}, ::cwxeditor::lib::String{});
                         }) {
        return node.new_element(::cwxeditor::lib::String(key), ::cwxeditor::lib::String(value));
    } else {
        using ChildResult = decltype(new_element(node, key));
        auto&& child = new_element(node, key);
        set_node_text(child, value);
        // DのXNodeは共有ノードを指す値型ハンドルだが、C++
        // 側は親が所有する領域への参照を返す場合がある。 参照の結
        // 果はそのまま保持し、値・右辺値のハンドルは実体化して、
        // この代替処理が局所変数への参照を返さないようにする。
        if constexpr (std::is_lvalue_reference_v<ChildResult>) {
            return child;
        } else {
            return remove_cvref_t<ChildResult>(std::forward<ChildResult>(child));
        }
    }
}

template <typename Node>
void new_attr(Node& node,
              ::cwxeditor::lib::StringView key,
              ::cwxeditor::lib::StringView value) {
    if constexpr (requires {
                      node.newAttr(::cwxeditor::lib::String{}, ::cwxeditor::lib::String{});
                  }) {
        node.newAttr(::cwxeditor::lib::String(key), ::cwxeditor::lib::String(value));
    } else if constexpr (requires {
                             node.new_attr(::cwxeditor::lib::String{}, ::cwxeditor::lib::String{});
                         }) {
        node.new_attr(::cwxeditor::lib::String(key), ::cwxeditor::lib::String(value));
    } else {
        static_assert(std::is_same_v<Node, void>, "node does not support attributes");
    }
}

template <typename Node>
std::optional<::cwxeditor::lib::String> attr(Node& node, ::cwxeditor::lib::StringView key) {
    if constexpr (requires {
                      node.template attr<::cwxeditor::lib::String>(
                          ::cwxeditor::lib::String{}, false, ::cwxeditor::lib::String{});
                  }) {
        if constexpr (requires { node.hasAttr(::cwxeditor::lib::String{}); }) {
            if (!node.hasAttr(::cwxeditor::lib::String(key))) return std::nullopt;
        }
        return node.template attr<::cwxeditor::lib::String>(
            ::cwxeditor::lib::String(key), false, ::cwxeditor::lib::String{});
    } else if constexpr (requires {
                             node.attr(::cwxeditor::lib::String{}, false, ::cwxeditor::lib::String{});
                         }) {
        return node.attr(::cwxeditor::lib::String(key), false, ::cwxeditor::lib::String{});
    } else if constexpr (requires { node.attributes; }) {
        auto found = node.attributes.find(::cwxeditor::lib::String(key));
        if (found == node.attributes.end()) return std::nullopt;
        return string_value(found->second);
    } else {
        return std::nullopt;
    }
}

template <typename Node, typename Function>
void for_each_child(Node& node, Function&& function) {
    // DのXNode.parse()はコメント・CDATAを含むすべての保
    // 存済みの子を代替ハンドラーへ渡し、 通常の文字列はvalueに保持す
    // る。情報を保持したXmlDocumentの参照が渡された場合は、 通
    // 常のテキストノードだけを除外し、コメント・CDATAは文書順で残す。
    if constexpr (requires { node.children(); }) {
        for (auto child : node.children()) {
            if constexpr (requires {
                              child.type() == ::cwxeditor::lib::XmlNodeType::Text;
                          }) {
                if (child.type() == ::cwxeditor::lib::XmlNodeType::Text) continue;
            }
            function(child);
        }
    } else if constexpr (requires { node.element_children(); }) {
        for (auto child : node.element_children()) function(child);
    } else if constexpr (requires { node.children; }) {
        for (auto& child : node.children) function(child);
    } else if constexpr (requires { node.forEachChild(std::forward<Function>(function)); }) {
        node.forEachChild(std::forward<Function>(function));
    } else if constexpr (requires { node.onTag[::cwxeditor::lib::String{}]; node.parse(); }) {
        node.onTag[::cwxeditor::lib::String{}] =
            [&function](auto& child) { function(child); };
        node.parse();
    } else {
        throw std::logic_error("node does not expose child traversal");
    }
}

}  // namespace detail

/** settings.dのtoNodeと同じく、値を子要素として保存する。 */
template <typename Node, typename T>
void toNode(Node& node, ::cwxeditor::lib::StringView key, const T& value) {
    using U = detail::remove_cvref_t<T>;
    if constexpr (detail::member_to_node_with_key<Node, T>) {
        value.toNode(node, ::cwxeditor::lib::String(key));
    } else if constexpr (detail::member_to_node<Node, T>) {
        value.toNode(node);
    } else if constexpr (detail::is_dynamic_array_v<U> || detail::is_array_v<U>) {
        auto&& element = detail::new_element(node, key);
        for (const auto& item : value) {
            if constexpr (detail::member_to_node<detail::remove_cvref_t<decltype(element)>, decltype(item)>) {
                item.toNode(element);
            } else {
                detail::new_element(element, "value", detail::string_value(item));
            }
        }
    } else {
        detail::new_element(node, key, detail::string_value(value));
    }
}

/** settings.dのfromNodeと同じく、ノードから値を読み戻す。 */
template <typename Node, typename T>
    requires detail::owning_read_value<T>
void fromNode(Node& node, ::cwxeditor::lib::StringView, T& value) {
    using U = detail::remove_cvref_t<T>;
    if constexpr (detail::member_from_node<Node, T>) {
        value.fromNode(node);
    } else if constexpr (detail::is_dynamic_array_v<U>) {
        detail::clear_dynamic_array(value);
        detail::for_each_child(node, [&value](auto& child) {
            using Element = typename U::value_type;
            if constexpr (std::same_as<Element, ::cwxeditor::lib::String>) {
                // 文字列はノードの単一の値なので、子のテキストを変換
                // する前に 入れ子の設定オブジェクトとして調べない。
                detail::append_dynamic_array(value, detail::value_from_string<Element>(detail::node_text(child)));
            } else if constexpr (detail::member_from_node<detail::remove_cvref_t<decltype(child)>, Element>) {
                Element item{};
                item.fromNode(child);
                detail::append_dynamic_array(value, std::move(item));
            } else {
                detail::append_dynamic_array(value, detail::value_from_string<Element>(detail::node_text(child)));
            }
        });
    } else {
        value = detail::value_from_string<U>(detail::node_text(node));
    }
}

/// 型付きのXML子要素プロパティ。
template <typename T, bool ReadOnly = false, bool LayoutValue = false>
struct Prop {
    ::cwxeditor::lib::String KEY;
    T INIT{};
    static constexpr bool READ_ONLY = ReadOnly;
    static constexpr bool LAYOUT_VALUE = LayoutValue;
    std::uint64_t CHG_VERSION{};
    T value{};
    bool readValue{};

    Prop() = default;
    Prop(::cwxeditor::lib::String key, T firstValue, std::uint64_t chgVersion = 0)
        : KEY(std::move(key)), INIT(firstValue), CHG_VERSION(chgVersion), value(std::move(firstValue)) {}

    Prop& operator=(const T& next) {
        value = next;
        return *this;
    }
    operator const T&() const noexcept { return value; }
    operator T&() noexcept { return value; }

    template <typename Node>
    void toNode(Node& node) const {
        settings::toNode(node, KEY, value);
    }

    template <typename Node>
        requires detail::owning_read_value<T>
    void fromNode(Node& node, std::uint64_t dataVersion) {
        if (CHG_VERSION <= dataVersion) {
            settings::fromNode(node, KEY, value);
            readValue = true;
        }
    }
};

/// 通常はXML属性で表す、型付きのプロパティ。
template <typename T, bool ReadOnly = false, bool LayoutValue = false>
struct PropAttr {
    ::cwxeditor::lib::String ATTR_KEY;
    T INIT{};
    static constexpr bool READ_ONLY = ReadOnly;
    static constexpr bool LAYOUT_VALUE = LayoutValue;
    std::uint64_t CHG_VERSION{};
    T value{};
    bool readValue{};

    PropAttr() = default;
    PropAttr(::cwxeditor::lib::String key, T firstValue, std::uint64_t chgVersion = 0)
        : ATTR_KEY(std::move(key)), INIT(firstValue), CHG_VERSION(chgVersion), value(std::move(firstValue)) {}

    PropAttr& operator=(const T& next) {
        value = next;
        return *this;
    }
    operator const T&() const noexcept { return value; }
    operator T&() noexcept { return value; }

    template <typename Node>
    void toNode(Node& node) const {
        settings::toNode(node, ATTR_KEY, value);
    }

    template <typename Node>
        requires detail::owning_read_value<T>
    void fromNode(Node& node, std::uint64_t dataVersion) {
        if (CHG_VERSION <= dataVersion) {
            settings::fromNode(node, ATTR_KEY, value);
            readValue = true;
        }
    }
};

/// キー付きの値を持つ親要素として保存する、マップのプロパティ。
template <typename Key, typename Value>
struct AAProp {
    using map_type = std::map<Key, Value>;

    ::cwxeditor::lib::String AA_KEY;
    ::cwxeditor::lib::String KEY_NAME;
    ::cwxeditor::lib::String VALUE_NAME;
    std::uint64_t CHG_VERSION{};
    map_type value;

    AAProp() = default;
    AAProp(::cwxeditor::lib::String key,
           ::cwxeditor::lib::String keyName,
           ::cwxeditor::lib::String valueName,
           std::uint64_t chgVersion = 0)
        : AA_KEY(std::move(key)),
          KEY_NAME(std::move(keyName)),
          VALUE_NAME(std::move(valueName)),
          CHG_VERSION(chgVersion) {}

    operator const map_type&() const noexcept { return value; }
    operator map_type&() noexcept { return value; }

    template <typename Node>
    void toNode(Node& node) const {
        auto&& element = detail::new_element(node, AA_KEY);
        for (const auto& [key, item] : value) {
            auto&& child = detail::new_element(element, VALUE_NAME, detail::string_value(item));
            detail::new_attr(child, KEY_NAME, detail::string_value(key));
        }
    }

    template <typename Node>
        requires detail::owning_read_value<Key> && detail::owning_read_value<Value>
    void fromNode(Node& node, std::uint64_t dataVersion) {
        if (CHG_VERSION > dataVersion) return;
        detail::for_each_child(node, [this](auto& child) {
            if (detail::node_name(child) != VALUE_NAME) return;
            const auto key = detail::attr(child, KEY_NAME);
            if (key) value[detail::value_from_string<Key>(*key)] =
                detail::value_from_string<Value>(detail::node_text(child));
        });
    }
};

/// Properties::Propertyが使う、内部のプロパティ保持先。
template <typename T>
struct PropValue {
    ::cwxeditor::lib::String KEY;
    T INIT{};
    bool READ_ONLY{};
    std::uint64_t CHG_VERSION{};
    T value{};

    PropValue() = default;
    PropValue(::cwxeditor::lib::String key,
              T firstValue,
              T defaultValue,
              bool readOnly,
              std::uint64_t chgVersion)
        : KEY(std::move(key)),
          INIT(std::move(defaultValue)),
          READ_ONLY(readOnly),
          CHG_VERSION(chgVersion),
          value(std::move(firstValue)) {}

    operator const T&() const noexcept { return value; }

    template <typename Node>
    void toNode(Node& node) const {
        settings::toNode(node, KEY, value);
    }

    template <typename Node>
        requires detail::owning_read_value<T>
    void fromNode(Node& node, std::uint64_t dataVersion) {
        if (CHG_VERSION <= dataVersion) settings::fromNode(node, KEY, value);
    }
};

/// Properties::PropertyAttrが使う、内部の属性保持先。
template <typename T>
struct PropValueAttr {
    ::cwxeditor::lib::String ATTR_KEY;
    T INIT{};
    bool READ_ONLY{};
    std::uint64_t CHG_VERSION{};
    T value{};

    PropValueAttr() = default;
    PropValueAttr(::cwxeditor::lib::String key,
                  T firstValue,
                  T defaultValue,
                  bool readOnly,
                  std::uint64_t chgVersion)
        : ATTR_KEY(std::move(key)),
          INIT(std::move(defaultValue)),
          READ_ONLY(readOnly),
          CHG_VERSION(chgVersion),
          value(std::move(firstValue)) {}

    operator const T&() const noexcept { return value; }

    template <typename Node>
    void toNode(Node& node) const {
        settings::toNode(node, ATTR_KEY, value);
    }

    template <typename Node>
        requires detail::owning_read_value<T>
    void fromNode(Node& node, std::uint64_t dataVersion) {
        if (CHG_VERSION <= dataVersion) settings::fromNode(node, ATTR_KEY, value);
    }
};

/// Properties::AAPropertyが使う、内部の連想配列保持先。
template <typename Key, typename Value>
struct AAProperty {
    using map_type = std::map<Key, Value>;

    ::cwxeditor::lib::String AA_KEY;
    ::cwxeditor::lib::String KEY_NAME;
    ::cwxeditor::lib::String VALUE_NAME;
    std::uint64_t CHG_VERSION{};
    map_type value;

    AAProperty() = default;
    AAProperty(::cwxeditor::lib::String key,
               ::cwxeditor::lib::String keyName,
               ::cwxeditor::lib::String valueName,
               std::uint64_t chgVersion = 0)
        : AA_KEY(std::move(key)),
          KEY_NAME(std::move(keyName)),
          VALUE_NAME(std::move(valueName)),
          CHG_VERSION(chgVersion) {}

    operator const map_type&() const noexcept { return value; }
    operator map_type&() noexcept { return value; }

    template <typename Node>
    void toNode(Node& node) const {
        auto&& element = detail::new_element(node, AA_KEY);
        for (const auto& [key, item] : value) {
            auto&& child = detail::new_element(element, VALUE_NAME, detail::string_value(item));
            detail::new_attr(child, KEY_NAME, detail::string_value(key));
        }
    }

    template <typename Node>
        requires detail::owning_read_value<Key> && detail::owning_read_value<Value>
    void fromNode(Node& node, std::uint64_t dataVersion) {
        if (CHG_VERSION > dataVersion) return;
        detail::for_each_child(node, [this](auto& child) {
            if (detail::node_name(child) != VALUE_NAME) return;
            const auto key = detail::attr(child, KEY_NAME);
            if (key) value[detail::value_from_string<Key>(*key)] =
                detail::value_from_string<Value>(detail::node_text(child));
        });
    }
};

/// 設定プロパティを組み合わせたクラスの基底。
class Properties {
protected:
    template <typename VType>
    struct Property {
        PropValue<VType> storage;

        Property() = default;
        Property(::cwxeditor::lib::String name,
                 VType defaultValue,
                 bool readOnly = false,
                 std::uint64_t chgVersion = 0)
            : storage(std::move(name), defaultValue, defaultValue, readOnly, chgVersion) {}

        const VType& get() const noexcept { return storage.value; }
        operator const VType&() const noexcept { return storage.value; }
        Property& operator=(const VType& value) {
            set(value);
            return *this;
        }
        void set(const VType& value) {
            if (storage.READ_ONLY) throw std::logic_error("attempt to modify a read-only property");
            storage.value = value;
        }
        void reset() { storage.value = storage.INIT; }

        template <typename Node>
        void toNode(Node& node) const {
            storage.toNode(node);
        }
        template <typename Node>
            requires detail::owning_read_value<VType>
        void fromNode(Node& node, std::uint64_t version) {
            storage.fromNode(node, version);
        }
    };

    template <typename VType>
    struct PropertyAttr {
        PropValueAttr<VType> storage;

        PropertyAttr() = default;
        PropertyAttr(::cwxeditor::lib::String name,
                     VType defaultValue,
                     bool readOnly = false,
                     std::uint64_t chgVersion = 0)
            : storage(std::move(name), defaultValue, defaultValue, readOnly, chgVersion) {}

        const VType& get() const noexcept { return storage.value; }
        operator const VType&() const noexcept { return storage.value; }
        PropertyAttr& operator=(const VType& value) {
            set(value);
            return *this;
        }
        void set(const VType& value) {
            if (storage.READ_ONLY) throw std::logic_error("attempt to modify a read-only property");
            storage.value = value;
        }
        void reset() { storage.value = storage.INIT; }

        template <typename Node>
        void toNode(Node& node) const {
            storage.toNode(node);
        }
        template <typename Node>
            requires detail::owning_read_value<VType>
        void fromNode(Node& node, std::uint64_t version) {
            storage.fromNode(node, version);
        }
    };

    template <typename Key, typename Value>
    struct AAProperty {
        ::cwxeditor::cwx::settings::AAProperty<Key, Value> storage;

        AAProperty() = default;
        AAProperty(::cwxeditor::lib::String name,
                   ::cwxeditor::lib::String keyName,
                   ::cwxeditor::lib::String valueName,
                   std::uint64_t chgVersion = 0)
            : storage(std::move(name), std::move(keyName), std::move(valueName), chgVersion) {}

        const auto& get() const noexcept { return storage.value; }
        auto& get() noexcept { return storage.value; }
        void init() { storage.value.clear(); }

        template <typename Node>
        void toNode(Node& node) const {
            storage.toNode(node);
        }
        template <typename Node>
        void fromNode(Node& node, std::uint64_t version) {
            storage.fromNode(node, version);
        }
    };

    /**
     * 派生クラスはproperties
     * ()で項目参照のタプルを返す
     * か、writeProperties/readProperties
     * を実装する。properties
     * 方式で項目がな
     * い場合は空タプ
     * ルを明示する。
     */
    template <typename SubClass, typename RootTag = void>
    struct XMLFuncs {
        template <typename Property>
        static decltype(auto) propertyStorage(Property& property) {
            if constexpr (detail::has_property_storage<detail::remove_cvref_t<Property>>)
                return (property.storage);
            else return (property);
        }
        static ::cwxeditor::lib::String rootName() {
            if constexpr (requires { RootTag::value; }) return ::cwxeditor::lib::String(RootTag::value);
            else if constexpr (requires { RootTag::ROOT; }) return ::cwxeditor::lib::String(RootTag::ROOT);
            else if constexpr (requires { SubClass::XML_NAME; }) return ::cwxeditor::lib::String(SubClass::XML_NAME);
            else return {};
        }
        template <typename Node>
        void toNodeImpl(Node& node, bool writeAll) const {
            const auto& self = static_cast<const SubClass&>(*this);
            if constexpr (requires { self.properties(); }) {
                // C++20にはDのtupleofがないため、リフレクションを明示的な項目登録で代替する。
                std::apply([&](const auto&... field) {
                    const auto write = [&](const auto& field) {
                        const auto& property = propertyStorage(field);
                        using Property = detail::remove_cvref_t<decltype(property)>;
                        if constexpr (detail::has_attribute_key<Property>) {
                            if (writeAll || property.value != property.INIT)
                                node.newAttr(property.ATTR_KEY, detail::string_value(property.value));
                        } else if constexpr (detail::has_initialized_element_key<Property>) {
                            if (writeAll || property.value != property.INIT) property.toNode(node);
                        } else if constexpr (detail::has_associative_array_key<Property>) property.toNode(node);
                    };
                    (write(field), ...);
                }, self.properties());
            } else if constexpr (requires { self.writeProperties(node, writeAll); }) {
                self.writeProperties(node, writeAll);
            } else if constexpr (requires { self.toProperties(node, writeAll); }) {
                self.toProperties(node, writeAll);
            } else {
                static_assert(!std::is_same_v<SubClass, SubClass>,
                    "XMLFuncs requires properties() or a property-writing hook");
            }
        }

        template <typename Node>
        void toNode(Node& node, bool writeAll) const {
            const auto root = rootName();
            if (root.empty()) toNodeImpl(node, writeAll);
            else { auto child = node.newElement(root); toNodeImpl(child, writeAll); }
        }

        template <typename Node>
        void toNode(Node& node) const {
            toNode(node, false);
        }

        template <typename Node>
        static SubClass fromNodeImpl(Node& node, std::uint64_t dataVersion) {
            SubClass result{};
            if constexpr (requires { result.properties(); }) {
                if (!node.valid()) return result;
                std::apply([&](auto&... field) {
                    const auto read = [&](auto& field) {
                        auto& property = propertyStorage(field);
                        using Property = detail::remove_cvref_t<decltype(property)>;
                        if constexpr (detail::has_attribute_key<Property>) {
                            // Dはto!string(INIT)をXNode.attr
                            // の遅延評価の既定値として渡すため、 属性がない場合だ
                            // け文字列へ変換する。 これにより、未使用の不正なス
                            // カラーINITで正常な属性が拒否されることも防ぐ。
                            if (const auto value = detail::attr(node, property.ATTR_KEY)) {
                                property.value = detail::value_from_string<
                                    detail::remove_cvref_t<decltype(property.value)>>(*value);
                            } else {
                                property.value = detail::value_from_string<
                                    detail::remove_cvref_t<decltype(property.value)>>(
                                        detail::string_value(property.INIT));
                            }
                        } else if constexpr (detail::has_element_key<Property> ||
                                             detail::has_associative_array_key<Property>) {
                            const auto key = [&] {
                                if constexpr (detail::has_element_key<Property>) return property.KEY;
                                else return property.AA_KEY;
                            }();
                            auto child = node.child(key, false);
                            if (!child.valid()) return;
                            try { property.fromNode(child, dataVersion); }
                            catch (const std::exception& error) {
                                (void)utils::printStackTrace();
                                utils::fdebugln(error.what());
                            }
                        }
                    };
                    (read(field), ...);
                }, result.properties());
            } else if constexpr (requires { result.readProperties(node, dataVersion); }) {
                result.readProperties(node, dataVersion);
            } else if constexpr (requires { result.fromProperties(node, dataVersion); }) {
                result.fromProperties(node, dataVersion);
            } else {
                static_assert(!std::is_same_v<SubClass, SubClass>,
                    "XMLFuncs requires properties() or a property-reading hook");
            }
            return result;
        }

        template <typename Node>
        static SubClass fromNode(Node& node, std::uint64_t dataVersion) {
            const auto root = rootName();
            if (root.empty()) return fromNodeImpl(node, dataVersion);
            auto child = node.child(root, false);
            return fromNodeImpl(child, dataVersion);
        }

        ::cwxeditor::lib::String toXML() const {
            return toXML(false);
        }

        ::cwxeditor::lib::String serializeProperties(bool writeAll) const {
            auto node = ::cwxeditor::lib::XmlNode::create(rootName());
            toNodeImpl(node, writeAll);
            return node.text();
        }

        ::cwxeditor::lib::String toXML(bool writeAll) const {
            if constexpr (requires(const SubClass& value) { value.serializeXML(writeAll); }) {
                return static_cast<const SubClass&>(*this).serializeXML(writeAll);
            } else if constexpr (requires(const SubClass& value) { value.serializeXML(); }) {
                if (!writeAll) return static_cast<const SubClass&>(*this).serializeXML();
                // 従来の引数なしの保存関数ではwriteAllを指定できない。
                // 登録済みの項目があればそちらを使い、指定を黙って無視しない。
                if constexpr (!(requires(const SubClass& value) { value.properties(); } ||
                                requires(const SubClass& value, ::cwxeditor::lib::XmlNode& node) { value.writeProperties(node, true); } ||
                                requires(const SubClass& value, ::cwxeditor::lib::XmlNode& node) { value.toProperties(node, true); })) {
                    throw std::logic_error("XMLFuncs serializer requires writeAll support");
                } else {
                    return serializeProperties(writeAll);
                }
            } else {
                return serializeProperties(writeAll);
            }
        }

        static SubClass fromXML(::cwxeditor::lib::String xml, std::uint64_t dataVersion) {
            try {
                if constexpr (requires { SubClass::parseXML(xml, dataVersion); }) {
                    return SubClass::parseXML(xml, dataVersion);
                } else {
                    auto node = ::cwxeditor::lib::XmlNode::parse(xml);
                    return fromNodeImpl(node, dataVersion);
                }
            } catch (const std::exception& error) {
                (void)utils::printStackTrace();
                utils::fdebugln(error.what());
                // Dはnullのクラス参照を返すが、この既存のC++
                // APIは値で返すため、 失敗時は既定値を返す。
                return SubClass{};
            }
        }
    };

public:
    virtual ~Properties() = default;
    Properties() = default;
    Properties(const Properties&) = default;
    Properties(Properties&&) noexcept = default;
    Properties& operator=(const Properties&) = default;
    Properties& operator=(Properties&&) noexcept = default;
};

}  // namespace cwxeditor::cwx::settings
