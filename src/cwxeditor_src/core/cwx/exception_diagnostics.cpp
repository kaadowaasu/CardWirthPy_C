#include "cwxeditor_src/core/cwx/exception_diagnostics.hpp"

#include "cwxeditor_src/core/cwx/script.hpp"
#include "cw/lib/exception_origin.hpp"

namespace cwxeditor::cwx::utils::detail {

std::string debugExceptionValue(const std::exception& exception) {
    const auto* message = exception.what();
    std::string result = "[";
    if (message) result += message;
    result += "] ";

    if (const auto* script_exception = dynamic_cast<const script::CWXScriptException*>(&exception)) {
        // コンパイラーは実際の例外送出箇所を記録する。呼び出し側がstd::exception
        // への参照しか持たない場合も、その情報を保持する。
        result += script_exception->sourceFile();
        result += ", ";
        result += std::to_string(script_exception->sourceLine());
        result += ": ";
    } else {
        // DのThrowableはfile/line/infoを持つが、標準C++例外
        // はwhat()しか持たない。発生元の代わりに診断の呼び出し位置を使わない。
        result += "<unknown>, ?: ";
    }
    // 現在のこの例外そのものに結び付いた追跡情報だけを解決する。プロバイ
    // ダーは無関係な例外や単に構築された例外を拒否し、記録箇所やEditor
    // の関連付けられていない性能計測用TLSの状態で代用しない。
    result += cw::thread::exception_stacktrace(exception);
    return result;
}

}  // namespace cwxeditor::cwx::utils::detail
