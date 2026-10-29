#pragma once

#include "threading.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace cwxeditor::lib {

class DispatcherStopped final : public std::runtime_error {
public:
    DispatcherStopped() : std::runtime_error("main dispatcher has stopped") {}
};

/// SDL、モバイル、ブラウザのEditorフロントエンドで共有するメインスレッド用キュー。
class EventLoopDispatcher final : public MainDispatcher {
public:
    explicit EventLoopDispatcher(std::function<void()> wake = {});
    ~EventLoopDispatcher() override;

    [[nodiscard]] bool is_main_thread() const noexcept override;
    void post(std::function<void()> task) override;
    void invoke(std::function<void()> task) override;
    TimerId post_delayed(std::chrono::milliseconds delay,
                         std::function<void()> task) override;
    void cancel_timer(TimerId timer) noexcept override;
    [[nodiscard]] std::function<void()> subscribe_successful_task_dispatch(
        std::function<void()> listener) override;

    /// 所有元のプラットフォームのイベントループがUIスレッドで呼ぶ。
    std::size_t drain(std::size_t maximum = 0U);
    void wake_callback(std::function<void()> callback);
    void shutdown() noexcept override;
    [[nodiscard]] bool stopped() const noexcept;

private:
    struct QueuedTask {
        std::function<void()> run;
        std::function<void()> cancel;
    };
    struct Timer;
    struct SuccessfulTaskDispatchObservers;
    bool enqueue(std::function<void()> task, std::function<void()> cancel = {});
    std::thread::id main_thread_;
    mutable std::mutex mutex_;
    std::deque<QueuedTask> queue_;
    std::unordered_map<TimerId, std::shared_ptr<Timer>> timers_;
    std::function<void()> wake_;
    std::shared_ptr<SuccessfulTaskDispatchObservers> successful_task_dispatch_observers_;
    TimerId next_timer_{1U};
    bool stopping_{};
};

[[nodiscard]] std::shared_ptr<TaskBackend> make_portable_task_backend();

}  // namespace cwxeditor::lib
