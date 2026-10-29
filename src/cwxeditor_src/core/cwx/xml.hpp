#pragma once

// cwx/xml.dの移植。Dは共有ノードのツリーを小さな
// XNode値型で包む。 共通実装をlib/xml.hpp
// に置き、Editorコアに別のパーサー・書き込み器やdxml
// 依存を持たせない。 下の別名で元の名前空間とD向けのAPI
// 名を保つ。 create/parse、value/valueTo
// 、newElement/newAttr、attr/hasAttr/removeAttrRecursive
// 、 child/childText、
// onTag、valid、isRoot
// 、textを同じ窓口から使える。

#include "cwxeditor_src/lib/xml.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"

#include <stdexcept>

namespace cwxeditor::lib {

namespace xml_enum_detail {

template <typename E>
consteval bool has_enum_names() {
    if constexpr (std::is_enum_v<E>) {
        return ::cwxeditor::cwx::enumutils::enumMemberCount<E> != 0U;
    } else {
        return false;
    }
}

template <typename E, typename = void>
struct has_complete_enum_names : std::false_type {};

template <typename E>
struct has_complete_enum_names<E, std::void_t<decltype(sizeof(E))>>
    : std::bool_constant<has_enum_names<E>()> {};

}  // namespace xml_enum_detail

// enumutilsは明示的なメタデータとコンパイ
// ラーの列挙情報から、Dのstd.convと同じ表
// 記を返す。 名前のない値のcast表記も含む。
template <typename E>
struct XmlEnumTraits<E, std::enable_if_t<xml_enum_detail::has_complete_enum_names<E>::value>> {
    static std::string to_string(const E value) {
        return ::cwxeditor::cwx::enumutils::enumToStringExact(value);
    }

    static E from_string(const std::string_view text) {
        try {
            return ::cwxeditor::cwx::enumutils::stringToEnumExact<E>(text);
        } catch (const std::invalid_argument&) {
            throw XmlValueError("Invalid XML enum: " + std::string(text));
        }
    }
};

}  // namespace cwxeditor::lib

namespace cwxeditor::cwx::xml {

using XNode = ::cwxeditor::lib::XmlNode;
using XmlNode = XNode;
using XmlAttribute = ::cwxeditor::lib::XmlAttribute;
using XmlDocument = ::cwxeditor::lib::XmlDocument;
using XmlParseError = ::cwxeditor::lib::XmlParseError;
using XmlParseOptions = ::cwxeditor::lib::XmlParseOptions;
using XmlParser = ::cwxeditor::lib::XmlParser;
using XmlValueError = ::cwxeditor::lib::XmlValueError;
using XmlWriter = ::cwxeditor::lib::XmlWriter;
using XmlWriterOptions = ::cwxeditor::lib::XmlWriterOptions;

}  // namespace cwxeditor::cwx::xml
