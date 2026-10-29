#pragma once

#include "cw/lib/exception_origin.hpp"
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace cw::lib {

// 言語境界。Python の finally は後処理の失敗を伝播し、
// 保留中の本体の例外を置き換えられるが、C++ のデストラクターで例外
// を投げると終了する。早期 return も含め、本体の後に後処理を明
// 示的に実行し、スタック巻き戻し中に投げずに同じ例外の優先順位を保つ。
template<class Body, class Cleanup>
std::invoke_result_t<Body> finally_call(Body&& body, Cleanup&& cleanup)
{
    using Result = std::invoke_result_t<Body>;
    static_assert(!std::is_reference_v<Result>, "finally_call returns values");
    ::cw::lib::ExceptionPtr pending;
    if constexpr (std::is_void_v<Result>) {
        try { std::forward<Body>(body)(); }
        catch (...) { pending = ::cw::lib::capture_exception(); }
        std::forward<Cleanup>(cleanup)();
        if (pending) ::cw::lib::rethrow_exception(pending);
    } else {
        std::optional<Result> result;
        try { result.emplace(std::forward<Body>(body)()); }
        catch (...) { pending = ::cw::lib::capture_exception(); }
        std::forward<Cleanup>(cleanup)();
        if (pending) ::cw::lib::rethrow_exception(pending);
        return std::move(*result);
    }
}

}
