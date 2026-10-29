#include "portable_async.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <future>
#include <utility>
#include <vector>

namespace cwxeditor::lib {
namespace {

struct PortableTaskSupervisor {
    std::mutex mutex;
    std::condition_variable changed;
    std::size_t active{};
    bool stopping{};
};

class PortableTaskHandle final : public TaskHandle {
public:
    struct State {
        std::atomic_bool cancelled{};
        std::atomic_bool finished{};
    };

    PortableTaskHandle(std::shared_ptr<PortableTaskSupervisor> supervisor,
                       std::function<void()> task)
        : state_(std::make_shared<State>()), supervisor_(std::move(supervisor)) {
        {
            std::scoped_lock lock(supervisor_->mutex);
            if (supervisor_->stopping) {
                state_->cancelled.store(true, std::memory_order_release);
                state_->finished.store(true, std::memory_order_release);
                return;
            }
            ++supervisor_->active;
        }
        worker_ = std::thread([state = state_, supervisor = supervisor_,
                               task = std::move(task)]() mutable {
              try {
                  if (!state->cancelled.load(std::memory_order_acquire) && task) task();
              } catch (...) {
                  // バックグラウンドの失敗は操作自身が報告する。 例外を
                  // ワーカースレッドの外へ出してエディタを終了させない。
              }
              state->finished.store(true, std::memory_order_release);
              {
                  std::scoped_lock lock(supervisor->mutex);
                  if (supervisor->active != 0U) --supervisor->active;
              }
              supervisor->changed.notify_all();
          });
    }

    ~PortableTaskHandle() override {
        cancel();
        if (!worker_.joinable()) return;
        if (worker_.get_id() == std::this_thread::get_id()) {
            worker_.detach();
            return;
        }
        if (finished()) worker_.join();
        else worker_.detach();
    }
    void cancel() noexcept override {
        state_->cancelled.store(true, std::memory_order_release);
    }
    bool finished() const noexcept override {
        return state_->finished.load(std::memory_order_acquire);
    }
    void wait() override {
        if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id())
            worker_.join();
    }

private:
    std::shared_ptr<State> state_;
    std::shared_ptr<PortableTaskSupervisor> supervisor_;
    std::thread worker_;
};

class PortableTasks final : public TaskBackend {
public:
    ~PortableTasks() override {
        std::unique_lock lock(supervisor_->mutex);
        supervisor_->stopping = true;
        // タスクは呼び出し可能オブジェクトに取り込んだ全データを所有する。 短い処理
        // には完了の機会を与えるが、TaskBackend に協調的な停止トークン
        // を持たない 任意のアプリ処理のために、GUIの終了を無期限に待たせない。
        (void)supervisor_->changed.wait_for(lock, std::chrono::milliseconds(250),
                                            [this] { return supervisor_->active == 0U; });
    }

    std::shared_ptr<TaskHandle> run(std::function<void()> task) override {
        return std::make_shared<PortableTaskHandle>(supervisor_, std::move(task));
    }

private:
    std::shared_ptr<PortableTaskSupervisor> supervisor_{
        std::make_shared<PortableTaskSupervisor>()};
};

}  // namespace

struct EventLoopDispatcher::Timer {
    ~Timer() {
        cancel();
        if (worker.joinable() && worker.get_id() != std::this_thread::get_id())
            worker.join();
    }

    void cancel() noexcept {
        cancelled.store(true, std::memory_order_release);
        wait.notify_all();
    }

    std::atomic_bool cancelled{};
    std::atomic_bool finished{};
    std::mutex wait_mutex;
    std::condition_variable wait;
    std::thread worker;
};

struct EventLoopDispatcher::SuccessfulTaskDispatchObservers final
    : std::enable_shared_from_this<SuccessfulTaskDispatchObservers> {
    std::function<void()> subscribe(std::function<void()> listener) {
        if (!listener) return {};
        std::uint64_t id{};
        {
            std::scoped_lock lock(mutex);
            id = next_id++;
            if (id == 0U) id = next_id++;
            listeners.emplace(id, std::move(listener));
        }
        const std::weak_ptr<SuccessfulTaskDispatchObservers> weak = shared_from_this();
        return [weak, id] {
            if (const auto state = weak.lock()) {
                std::scoped_lock lock(state->mutex);
                state->listeners.erase(id);
            }
        };
    }

    void notify() {
        std::vector<std::uint64_t> ids;
        {
            std::scoped_lock lock(mutex);
            ids.reserve(listeners.size());
            for (const auto& [id, _] : listeners) ids.push_back(id);
        }
        for (const auto id : ids) {
            std::function<void()> listener;
            {
                std::scoped_lock lock(mutex);
                if (const auto found = listeners.find(id); found != listeners.end())
                    listener = found->second;
            }
            if (listener) listener();
        }
    }

    std::mutex mutex;
    std::uint64_t next_id{1U};
    std::unordered_map<std::uint64_t, std::function<void()>> listeners;
};

EventLoopDispatcher::EventLoopDispatcher(std::function<void()> wake)
    : main_thread_(std::this_thread::get_id()), wake_(std::move(wake)),
      successful_task_dispatch_observers_(
          std::make_shared<SuccessfulTaskDispatchObservers>()) {}

EventLoopDispatcher::~EventLoopDispatcher() { shutdown(); }

bool EventLoopDispatcher::is_main_thread() const noexcept {
    return std::this_thread::get_id() == main_thread_;
}

bool EventLoopDispatcher::enqueue(std::function<void()> task,
                                  std::function<void()> cancel) {
    if (!task) return false;
    std::function<void()> wake;
    {
        std::scoped_lock lock(mutex_);
        if (stopping_) return false;
        queue_.push_back({std::move(task), std::move(cancel)});
        wake = wake_;
    }
    if (wake) wake();
    return true;
}

void EventLoopDispatcher::post(std::function<void()> task) {
    (void)enqueue(std::move(task));
}

void EventLoopDispatcher::invoke(std::function<void()> task) {
    if (!task) return;
    if (is_main_thread()) {
        {
            std::scoped_lock lock(mutex_);
            if (stopping_) throw DispatcherStopped{};
        }
        task();
        return;
    }
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    const bool accepted = enqueue(
        [task = std::move(task), promise] {
            try {
                task();
                promise->set_value();
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        },
        [promise] {
            try {
                throw DispatcherStopped{};
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
    if (!accepted) throw DispatcherStopped{};
    future.get();
}

TimerId EventLoopDispatcher::post_delayed(const std::chrono::milliseconds delay,
                                          std::function<void()> task) {
    if (!task) return 0U;
    auto timer = std::make_shared<Timer>();
    TimerId id;
    {
        std::scoped_lock lock(mutex_);
        if (stopping_) return 0U;
        std::erase_if(timers_, [](const auto& entry) {
            return entry.second->finished.load(std::memory_order_acquire);
        });
        id = next_timer_++;
        timers_[id] = timer;
    }
    Timer* const timer_state = timer.get();
    timer->worker = std::thread(
        [this, timer_state, delay = std::max(delay, std::chrono::milliseconds{}),
         task = std::move(task)]() mutable {
            {
                std::unique_lock lock(timer_state->wait_mutex);
                if (timer_state->wait.wait_for(lock, delay, [timer_state] {
                        return timer_state->cancelled.load(std::memory_order_acquire);
                    })) {
                    timer_state->finished.store(true, std::memory_order_release);
                    return;
                }
            }
            if (!timer_state->cancelled.load(std::memory_order_acquire))
                post(std::move(task));
            timer_state->finished.store(true, std::memory_order_release);
        });
    return id;
}

void EventLoopDispatcher::cancel_timer(const TimerId timer) noexcept {
    try {
        std::shared_ptr<Timer> state;
        {
            std::scoped_lock lock(mutex_);
            const auto found = timers_.find(timer);
            if (found == timers_.end()) return;
            state = found->second;
            timers_.erase(found);
        }
        state->cancel();
    } catch (...) {
    }
}

std::function<void()> EventLoopDispatcher::subscribe_successful_task_dispatch(
    std::function<void()> listener) {
    return successful_task_dispatch_observers_->subscribe(std::move(listener));
}

std::size_t EventLoopDispatcher::drain(const std::size_t maximum) {
    if (!is_main_thread()) return 0U;
    std::size_t count{};
    for (;;) {
        QueuedTask queued;
        {
            std::scoped_lock lock(mutex_);
            if (queue_.empty() || (maximum != 0U && count >= maximum)) break;
            queued = std::move(queue_.front());
            queue_.pop_front();
        }
        if (queued.run) queued.run();
        successful_task_dispatch_observers_->notify();
        ++count;
    }
    return count;
}

void EventLoopDispatcher::wake_callback(std::function<void()> callback) {
    std::scoped_lock lock(mutex_);
    wake_ = std::move(callback);
}

bool EventLoopDispatcher::stopped() const noexcept {
    std::scoped_lock lock(mutex_);
    return stopping_;
}

void EventLoopDispatcher::shutdown() noexcept {
    try {
        std::unordered_map<TimerId, std::shared_ptr<Timer>> timers;
        std::deque<QueuedTask> queued;
        {
            std::scoped_lock lock(mutex_);
            if (stopping_) return;
            stopping_ = true;
            timers.swap(timers_);
            queued.swap(queue_);
        }
        for (auto& task : queued) if (task.cancel) task.cancel();
        for (auto& [id, timer] : timers) {
            (void)id;
            timer->cancel();
        }
    } catch (...) {
    }
}

std::shared_ptr<TaskBackend> make_portable_task_backend() {
    return std::make_shared<PortableTasks>();
}

}  // namespace cwxeditor::lib
