#pragma once

#include <atomic>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

namespace cwxeditor::lib {

// 全プラットフォームで共有する、プロジェクト内の小さな協調停止API。Android
// NDKのlibc++は既定でC++20のstop_tokenを公開しない
// ため、stdへ代替実装を追加したり、libc++の実験機能を全体で有効に
// したりしない。必要なtoken/source/callback操作だけを
// 提供し、スレッドの所有と終了待ちはTaskGroupが明示的に担当する。
namespace task_stop_detail {
struct Callback {
    virtual ~Callback() = default;
    virtual void call() noexcept = 0;
    bool running{};
    std::thread::id runner{};
    std::condition_variable finished;
};
struct State {
    std::atomic_bool requested{};
    std::mutex mutex;
    std::list<std::shared_ptr<Callback>> callbacks;
};
inline void invoke(const std::shared_ptr<State>& state,
                   const std::shared_ptr<Callback>& callback) noexcept {
    // 利用側のコードを呼ぶ間は状態ロックを保持しない。コールバックから停止要求、コー
    // ルバックの登録・削除、所有元のタスクグループへの問い合わせを行えるようにする。
    callback->call(); // 停止コールバックが例外を送出した場合は、std::stop_callbackと同様にプログラムを終了する。
    {
        std::lock_guard lock(state->mutex);
        callback->running = false;
        callback->runner = {};
    }
    callback->finished.notify_all();
}
template<class Function>
struct CallbackNode final : Callback {
    explicit CallbackNode(Function fn) : function(std::move(fn)) {}
    void call() noexcept override { function(); }
    Function function;
};
} // namespace task_stop_detail

class TaskStopSource;
template<class Function> class TaskStopCallback;

class TaskStopToken final {
public:
    TaskStopToken() noexcept = default;
    [[nodiscard]] bool stop_requested() const noexcept {
        return state_ && state_->requested.load(std::memory_order_acquire);
    }
private:
    friend class TaskStopSource;
    template<class Function> friend class TaskStopCallback;
    explicit TaskStopToken(std::shared_ptr<task_stop_detail::State> state) noexcept
        : state_(std::move(state)) {}
    std::shared_ptr<task_stop_detail::State> state_;
};

class TaskStopSource final {
public:
    TaskStopSource() : state_(std::make_shared<task_stop_detail::State>()) {}
    [[nodiscard]] TaskStopToken get_token() const noexcept { return TaskStopToken(state_); }
    [[nodiscard]] bool stop_requested() const noexcept { return get_token().stop_requested(); }
    bool request_stop() noexcept {
        const auto state = state_;
        if (!state) return false;
        std::unique_lock lock(state->mutex);
        if (state->requested.exchange(true, std::memory_order_acq_rel)) return false;
        while (!state->callbacks.empty()) {
            const auto callback = state->callbacks.front();
            state->callbacks.pop_front();
            callback->running = true;
            callback->runner = std::this_thread::get_id();
            lock.unlock();
            task_stop_detail::invoke(state, callback);
            lock.lock();
        }
        return true;
    }
private:
    std::shared_ptr<task_stop_detail::State> state_;
};

template<class Function>
class TaskStopCallback final {
public:
    TaskStopCallback(TaskStopToken token, Function function)
        : state_(std::move(token.state_)),
          callback_(std::make_shared<task_stop_detail::CallbackNode<Function>>(std::move(function))) {
        if (!state_) return;
        {
            std::lock_guard lock(state_->mutex);
            if (!state_->requested.load(std::memory_order_acquire)) {
                state_->callbacks.push_back(callback_);
                return;
            }
            callback_->running = true;
            callback_->runner = std::this_thread::get_id();
        }
        // 停止後に登録した場合は、登録したスレッドで直ちに呼び出す。
        task_stop_detail::invoke(state_, callback_);
    }
    ~TaskStopCallback() {
        if (!state_) return;
        std::unique_lock lock(state_->mutex);
        state_->callbacks.remove(callback_);
        // 別スレッドがキャプチャした値を使って実行中の場合、登録解除はその完了ま
        // で戻らない。自身の登録解除は許可し、invokeがノードを所有する。
        if (callback_->runner != std::this_thread::get_id())
            callback_->finished.wait(lock, [&] { return !callback_->running; });
    }
    TaskStopCallback(const TaskStopCallback&) = delete;
    TaskStopCallback& operator=(const TaskStopCallback&) = delete;
    TaskStopCallback(TaskStopCallback&&) = delete;
    TaskStopCallback& operator=(TaskStopCallback&&) = delete;
private:
    std::shared_ptr<task_stop_detail::State> state_;
    std::shared_ptr<task_stop_detail::Callback> callback_;
};
template<class Function>
TaskStopCallback(TaskStopToken, Function) -> TaskStopCallback<std::decay_t<Function>>;

} // namespace cwxeditor::lib

