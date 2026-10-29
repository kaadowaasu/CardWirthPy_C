#pragma once

#include <array>
#include <chrono>
#include <exception>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cwxeditor_src/lib/portable.hpp"

namespace cwxeditor::cwx::perf {

// Dのstringは所有権のないUTF-8の文字列スライス。 スタ
// ック項目はputStackが戻った後も保持するため、C++側では
// 文字列を所有し、 一時文字列が渡されても無効な参照を残さない。
struct StackTrace {
    cwxeditor::lib::String file;
    std::size_t line{};
};

// Dの通常のモジュール変数に対応するため、スレッドローカルにする
// 。 tStackの長さは確保容量（8・16・32…）であり有効
// フレーム数ではないので、 実際のスタックの長さを別に保持する。
inline thread_local cwxeditor::lib::Array<StackTrace> tStack;
inline thread_local cwxeditor::lib::Array<StackTrace> stStack;
inline thread_local std::size_t tStackLen = 0;

// Dのmixin(S_TRACE)で使う文字列。C++ではputStack
// と対応するスコープガードを使える。 ソース文字列も残
// し、これを参照するツールへ宣言上の契約を提供する。
inline constexpr std::string_view S_TRACE =
    ".putStack(__FILE__, __LINE__);\n"
    "\tscope (exit) .tStackLen--;\n"
    "\tscope (failure) .saveStack();\n";

void putStack(std::string_view file, std::size_t line);
void saveStack();

/// Dのmixin(S_TRACE)に対応するC++のRAII
/// 。フレームを記録し、 例外で巻き戻す際に最初の失敗スタッ
/// クを取得して、必ずスレッドローカルの深さを戻す。 各スコ
/// ープで巻き戻しが一度だけ行われるよう、コピー不可とする。
class TraceScope {
public:
    TraceScope(std::string_view file, std::size_t line) noexcept;
    ~TraceScope() noexcept;
    TraceScope(const TraceScope&) = delete;
    TraceScope& operator=(const TraceScope&) = delete;

private:
    int uncaught_{};
    bool armed_{};
};

// Dはデバッグ・コンソールビルドだけで時間計測を有効にする。
// MSVCのコンソール定義と、共通テストビルド用のCWXEDITOR_CONSOLE/CONSOLE
// の両方を受け付ける。
#if !defined(NDEBUG) && \
    (defined(_CONSOLE) || defined(CWXEDITOR_CONSOLE) || defined(CONSOLE))
inline constexpr bool kPerfEnabled = true;
#else
inline constexpr bool kPerfEnabled = false;
#endif

using PerfClock = std::chrono::steady_clock;
using PerfTimePoint = PerfClock::time_point;

// Dの__gshared ulongに対応する、プロ
// セス共通の64ビットカウンター。 元の同期なしの
// +=操作に合わせて通常のカウンターとし、 D版と
// 同じく一つの集計実行元から使うことを前提にする。
inline std::uint64_t utperf = 0;
inline std::array<std::uint64_t, 1024> t{};

// D版は静的コンストラクターでこの時計を開始
// する。 OS専用のタイマーを設けず、開始
// 時点を保持して同じ初期化時の動作を保つ。
inline thread_local PerfTimePoint initTimer = PerfClock::now();

namespace detail {

[[nodiscard]] inline std::uint64_t elapsedMilliseconds(PerfTimePoint start,
                                                         PerfTimePoint end) noexcept {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    return elapsed.count() < 0 ? 0u : static_cast<std::uint64_t>(elapsed.count());
}

template <int I>
inline constexpr std::size_t timerIndex = [] {
    static_assert(I >= 0, "performance timer index must not be negative");
    static_assert(static_cast<std::size_t>(I) < t.size(),
                  "performance timer index must be less than 1024");
    return static_cast<std::size_t>(I);
}();

inline void addElapsed(std::size_t index, PerfTimePoint start, PerfTimePoint end) noexcept {
    if constexpr (kPerfEnabled) {
        t[index] += elapsedMilliseconds(start, end);
    } else {
        (void)index;
        (void)start;
        (void)end;
    }
}

}  // namespace detail

// mixin(FPerf!I)に対応するC++の処理。スコープの
// 先頭で作り、 デストラクターが経過ミリ秒をt[I]へ記録する。
template <int I>
class FPerf {
public:
    FPerf() noexcept : started_(PerfClock::now()) {}
    ~FPerf() { stop(); }

    FPerf(const FPerf&) = delete;
    FPerf& operator=(const FPerf&) = delete;

    void stop() noexcept {
        if (!running_) {
            return;
        }
        running_ = false;
        detail::addElapsed(detail::timerIndex<I>, started_, PerfClock::now());
    }

    [[nodiscard]] bool running() const noexcept { return running_; }

private:
    PerfTimePoint started_;
    bool running_ = true;
};

// Dのブロック時間計測mixinのソース表現。 下のBPerf
// が、生成される文と同じ停止・加算・リセット・開始を行う。
inline constexpr std::string_view BPerfS =
    "static import std.datetime.stopwatch;"
    "auto b_timer = std.datetime.stopwatch.StopWatch(std.datetime.stopwatch.AutoStart.yes);";

template <int I>
class BPerf {
public:
    BPerf() noexcept : started_(PerfClock::now()) {}

    void stop() noexcept {
        if (!running_) {
            return;
        }
        detail::addElapsed(detail::timerIndex<I>, started_, PerfClock::now());
        running_ = false;
    }

    void reset() noexcept {
        started_ = PerfClock::now();
        running_ = false;
    }

    void start() noexcept {
        started_ = PerfClock::now();
        running_ = true;
    }

    [[nodiscard]] bool running() const noexcept { return running_; }

private:
    PerfTimePoint started_;
    bool running_ = true;
};

// Dの単体テスト用の時間計測mixinに対応するC++実装。 プ
// ロセス共通のutperfを更新するため、FPerfとは分ける。
class UTPerfScope {
public:
    UTPerfScope() noexcept : started_(PerfClock::now()) {}
    ~UTPerfScope() { stop(); }

    UTPerfScope(const UTPerfScope&) = delete;
    UTPerfScope& operator=(const UTPerfScope&) = delete;

    void stop() noexcept {
        if (!running_) {
            return;
        }
        running_ = false;
        if constexpr (kPerfEnabled) {
            utperf += detail::elapsedMilliseconds(started_, PerfClock::now());
        }
    }

private:
    PerfTimePoint started_;
    bool running_ = true;
};

// UTPerfはDのソースmixin文字列。デバッグでもコンソールでもないC++
// ビルドでは、 version(debug)の無効時と同じく空にする。
#if !defined(NDEBUG) && \
    (defined(_CONSOLE) || defined(CWXEDITOR_CONSOLE) || defined(CONSOLE))
inline constexpr std::string_view UTPerf =
    "static import std.datetime.stopwatch;"
    "auto f_timer = std.datetime.stopwatch.StopWatch(std.datetime.stopwatch.AutoStart.yes);"
    "scope (exit) {"
    "f_timer.stop();"
    ".utperf += f_timer.peek().total!\"msecs\";"
    "}";
#else
inline constexpr std::string_view UTPerf{};
#endif

}  // namespace cwxeditor::cwx::perf
