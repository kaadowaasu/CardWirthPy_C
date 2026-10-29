#pragma once

#include "cw/lib/exception_origin.hpp"
#include "cw/lib/compat.hpp"
#include <exception>
#include <stdexcept>

namespace cw::lib {

// 言語境界。Python は Exception インスタンスを Value に
// 保存できるが、ネイティブ例外は Object の派生型ではない。共通メッセージ
// ループが結果を送出するまで、exception_ptr で具象型と内容を保つ。
class ExceptionResult final : public Object {
public:
    explicit ExceptionResult(::cw::lib::ExceptionPtr error) : m_error(std::move(error)) {
        if (!m_error) throw std::invalid_argument("empty message exception result");
    }
    [[noreturn]] void rethrow() const { ::cw::lib::rethrow_exception(m_error); }
private:
    ::cw::lib::ExceptionPtr m_error;
};

}
