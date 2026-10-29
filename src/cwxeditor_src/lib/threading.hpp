#pragma once

#include "types.hpp"

#include <chrono>
#include <functional>
#include <memory>

namespace cwxeditor::lib {

class TaskHandle {
public:
    virtual ~TaskHandle() = default;
    virtual void cancel() noexcept = 0;
    [[nodiscard]] virtual bool finished() const noexcept = 0;
    virtual void wait() = 0;
};

class MainDispatcher {
public:
    virtual ~MainDispatcher() = default;
    [[nodiscard]] virtual bool is_main_thread() const noexcept = 0;
    virtual void post(std::function<void()> task) = 0;
    virtual void invoke(std::function<void()> task) = 0;
    virtual TimerId post_delayed(std::chrono::milliseconds delay, std::function<void()> task) = 0;
    virtual void cancel_timer(TimerId timer) noexcept = 0;
    // キュー内の各タスクが正常終了するたびにUIの所有元へ通知する。
    // 一回のイベントループで複数の独立したUI配送を実行することがあ
    // るため、ループの境界だけではリセットのタイミングとして不十分。
    [[nodiscard]] virtual std::function<void()> subscribe_successful_task_dispatch(
        std::function<void()>) { return {}; }
    // 距離や時間に基づく入力向けの、共通の単調増加時計。テスト用のキューデ
    // ィスパッチャーでは、待機やネイティブUIなしで論理時刻を提供できる。
    [[nodiscard]] virtual std::chrono::steady_clock::time_point now()const {
        return std::chrono::steady_clock::now();
    }
    /// プラットフォームのイベントループを破棄する前に、待
    /// 機中の同期呼び出しをすべて完了または取り消す。キ
    /// ューを持たない実装は、何もしない既定実装でよい。
    virtual void shutdown() noexcept {}
};

class TaskBackend {
public:
    virtual ~TaskBackend() = default;
    [[nodiscard]] virtual std::shared_ptr<TaskHandle> run(std::function<void()> task) = 0;
};

}  // namespace cwxeditor::lib
