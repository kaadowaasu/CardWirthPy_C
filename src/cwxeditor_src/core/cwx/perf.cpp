#include "cwxeditor_src/core/cwx/perf.hpp"

#include <iomanip>
#include <iostream>

namespace cwxeditor::cwx::perf {

void putStack(std::string_view file, std::size_t line) {
    if (tStack.empty()) {
        tStack.resize(8);
    } else if (tStack.size() <= tStackLen) {
        tStack.resize(tStack.size() * 2);
    }

    // Dは不変のファイル名スライスをコピーする。C++では文字列を所有し
    // 、各項目の確保済み最大容量を再利用して、開始ごとの確保を避ける。
    auto& slot = tStack[tStackLen];
    if (slot.file != file) slot.file.assign(file);
    slot.line = line;
    ++tStackLen;
}

void saveStack() {
    if (!stStack.empty()) {
        return;
    }

    stStack.assign(tStack.begin(), tStack.begin() + tStackLen);
}

TraceScope::TraceScope(const std::string_view file, const std::size_t line) noexcept
    : uncaught_(std::uncaught_exceptions()) {
    try {
        const auto depth = tStackLen;
        putStack(file, line);
        armed_ = tStackLen == depth + 1;
    } catch (...) {
        // 追跡処理で、保護対象の操作の結果を変えてはならない。
    }
}

TraceScope::~TraceScope() noexcept {
    if (std::uncaught_exceptions() > uncaught_) {
        try {
            saveStack();
        } catch (...) {
            // 例外に伴う終了処理中は、元の例外を保持する。
        }
    }
    if (armed_ && tStackLen > 0) --tStackLen;
}

namespace {

struct PerfReporter {
    ~PerfReporter() {
        if constexpr (kPerfEnabled) {
            for (std::size_t index = 0; index < t.size(); ++index) {
                if (t[index] == 0u) {
                    continue;
                }

                std::cout << std::setfill('0') << std::setw(4) << index
                          << " = " << t[index] << '\n';
            }
        }
    }
};

// Dの共有静的デストラクターは、プロセス終了時に累積カウンターを報告する。無
// 効化したビルドにもオブジェクトを置くが、デストラクターはコンパイル時に何
// もしない処理となる。これにより全構成でヘッダーのリンケージを同じにする。
inline PerfReporter perfReporter;

}  // namespace

}  // namespace cwxeditor::cwx::perf
