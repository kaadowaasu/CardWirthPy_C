#pragma once

#include <exception>
#include <array>
#include <atomic>
#include <cstddef>
#include <utility>
#include <string>
#include <memory>

namespace cw::lib {

// C++ が例外を複製・転送しても、診断用の発生元情報を保持する。MSVC
// の exception_ptr は捕捉と再送出の両方でオブジェクトを複製す
// るため、ネイティブの送出監視だけでは新しい識別子同士を結び付けられない。
class ExceptionPtr;
ExceptionPtr capture_exception() noexcept;
[[noreturn]] void rethrow_exception(const ExceptionPtr& exception);

class ExceptionPtr {
    std::exception_ptr exception_;
    std::array<void*,48> frames_{};
    std::size_t count_{};
    std::shared_ptr<const std::string> symbols_;
    friend ExceptionPtr capture_exception() noexcept;
    friend void rethrow_exception(const ExceptionPtr& exception);
public:
    ExceptionPtr() noexcept=default;
    ExceptionPtr(std::nullptr_t) noexcept{}
    ExceptionPtr(std::exception_ptr exception) noexcept:exception_(std::move(exception)){}
    explicit operator bool() const noexcept{return static_cast<bool>(exception_);}
};

}

namespace cw::thread {
// Python 形式の print_ex の診断処理用に保持する、例外の送出元。
std::string current_exception_stacktrace();

// Editor の診断は、現在整形している例外オブジェクト自身
// の取得済みトレースを要求できる。Engine が ABI 固
// 有の取得処理を登録する。その実行環境をリンクしないホストには
// 、ログ処理箇所のスタックではなく、取得不能を明示して返す。
using ExceptionOriginProvider = std::string (*)(const std::exception&);
inline std::atomic<ExceptionOriginProvider> exception_origin_provider{nullptr};
inline constexpr char kExceptionOriginUnavailable[] =
    "Exception origin stack trace is unavailable.\r\n";

[[nodiscard]] inline bool install_exception_origin_provider(
    ExceptionOriginProvider provider) noexcept {
    if (!provider) return false;
    ExceptionOriginProvider expected = nullptr;
    return exception_origin_provider.compare_exchange_strong(
        expected, provider, std::memory_order_release,
        std::memory_order_relaxed);
}

[[nodiscard]] inline std::string exception_stacktrace(
    const std::exception& exception) {
    const auto provider = exception_origin_provider.load(std::memory_order_acquire);
    if (!provider) return kExceptionOriginUnavailable;
    try {
        return provider(exception);
    } catch (...) {
        // 診断処理が報告対象の例外を置き換えてはならない。
        return kExceptionOriginUnavailable;
    }
}
}
