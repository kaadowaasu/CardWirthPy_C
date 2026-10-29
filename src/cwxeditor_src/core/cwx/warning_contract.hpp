#pragma once

#include <cstddef>
#include <cstdlib>

namespace cwxeditor::cwx::warning {

// DのアサーションErrorに対応するネイティブの型。通常のstd::exception
// とは分ける。 D実行環境のABI・ハンドラー・表示までは再現しない。
struct DAssertError {
    const char* file;
    std::size_t line;
    const char* condition;
};

namespace detail {

// 翻訳単位内のリンケージにし、テストごとのNDEBUGの違いで失敗形式が混ざらないようにする。
[[noreturn]] static inline void failDAssertion(const char* file, const std::size_t line,
                                       const char* condition) {
#ifndef NDEBUG
    throw DAssertError{file, line, condition};
#else
    // DMD 2.097はreleaseでもassert(0)を常時停止へ変換する。 abort
    // で戻らず巻き戻さない動作を保つが、元の環境のトラップ信号や表示までは再現しない。
    (void)file;
    (void)line;
    (void)condition;
    std::abort();
#endif
}

}  // namespace detail
}  // namespace cwxeditor::cwx::warning

// Dの-releaseは通常のアサーションを条件の評価ごと除去す
// る。 上の常時到達不能な呼び出しは、このマクロと独立させる。
#ifndef NDEBUG
#define CWX_WARNING_D_ASSERT(condition) \
    ((condition) ? static_cast<void>(0) : \
     ::cwxeditor::cwx::warning::detail::failDAssertion(__FILE__, __LINE__, #condition))
#else
#define CWX_WARNING_D_ASSERT(condition) (static_cast<void>(0))
#endif
