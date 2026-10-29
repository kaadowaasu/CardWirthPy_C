#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cwxeditor::lib {

using String = std::string;
using StringView = std::string_view;
using Byte = std::uint8_t;
using Bytes = std::vector<Byte>;
using ByteView = std::span<const Byte>;
using MutableByteView = std::span<Byte>;

template <typename T>
using Array = std::vector<T>;

template <typename Key, typename Value>
using Dictionary = std::unordered_map<Key, Value>;

template <typename Key, typename Value>
using OrderedDictionary = std::map<Key, Value>;

template <typename T>
using Owner = std::shared_ptr<T>;

template <typename T>
using Observer = std::weak_ptr<T>;

template <typename T>
using Optional = std::optional<T>;

template <typename Signature>
using Callback = std::function<Signature>;

template <typename... Values>
using Variant = std::variant<Values...>;

}  // namespace cwxeditor::lib
