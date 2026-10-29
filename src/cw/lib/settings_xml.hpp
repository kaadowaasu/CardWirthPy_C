#pragma once
#include "cw/data.hpp"
#include "cw/lib/python_integer.hpp"
#include <cmath>
#include <stdexcept>

namespace cw::setting {
inline ::cw::lib::Integer integer_or_initial(const ::cw::lib::String& text,
    ::cw::lib::Integer initial, ::cw::lib::Integer low, ::cw::lib::Integer high) {
    const auto value = ::cw::lib::parse_python_integer(text);
    if (value && low <= *value && *value <= high) return *value;
    return initial;
}
// 設定の不正なスカラー値には、渡された初期値を使う。 シナリ
// オ・コンテンツ用の厳密なXML読み込み処理は変更しない。
class XmlReader {
    ::cw::data::_CWPyElementInterface& source;
public:
    explicit XmlReader(::cw::data::_CWPyElementInterface& value) : source(value) {}
    ::cw::lib::Integer getint(::cw::lib::String path,
        std::optional<::cw::lib::Value> attr, ::cw::lib::Integer initial) {
        try { return source.getint(path, attr, initial); }
        catch (const std::invalid_argument&) { return initial; }
        catch (const std::out_of_range&) { return initial; }
    }
    ::cw::lib::Real getfloat(::cw::lib::String path,
        std::optional<::cw::lib::Value> attr, ::cw::lib::Real initial) {
        try { const auto value = source.getfloat(path, attr, initial);
            return std::isfinite(value) ? value : initial; }
        catch (const std::invalid_argument&) { return initial; }
        catch (const std::out_of_range&) { return initial; }
    }
    bool getbool(::cw::lib::String path, std::optional<::cw::lib::Value> attr, bool initial) {
        try { return source.getbool(path, attr, initial); }
        catch (const std::invalid_argument&) { return initial; }
        catch (const std::out_of_range&) { return initial; }
    }
};
}
