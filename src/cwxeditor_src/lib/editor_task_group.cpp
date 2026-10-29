#include "editor_task_group.hpp"

#include <condition_variable>
#include <exception>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace cwxeditor::lib {
namespace {

[[nodiscard]] EditorTaskResult cancelled_result() {
    return EditorTaskResult::stopped("editor task UI dispatch was cancelled");
}

}  // namespace

EditorTaskResult EditorTaskResult::completed() { return {}; }
EditorTaskResult EditorTaskResult::stopped(std::string diagnostic) {
    return {EditorTaskTerminal::Stopped, std::move(diagnostic)};
}
EditorTaskResult EditorTaskResult::failed(std::string diagnostic) {
    return {EditorTaskTerminal::Failed, std::move(diagnostic)};
}
EditorTaskResult EditorTaskResult::rejected(std::string diagnostic) {
    return {EditorTaskTerminal::Rejected, std::move(diagnostic)};
}

struct EditorTaskUi::SharedState {
    struct Ticket {
        enum class Phase : std::uint8_t { Queued, Running, Cancelled, Finished };
        std::mutex mutex;
        Phase phase{Phase::Queued};
        std::shared_ptr<std::promise<EditorTaskResult>> completion;
    };

    explicit SharedState(std::shared_ptr<MainDispatcher> borrowed,
                         const EditorSessionGeneration value)
        : dispatcher(std::move(borrowed)), generation(value) {}

    mutable std::mutex mutex;
    std::shared_ptr<MainDispatcher> dispatcher;
    EditorSessionGeneration generation{};
    bool accepting{true};
    std::unordered_map<Ticket*, std::shared_ptr<Ticket>> pending;
};

namespace {

void fulfill(const std::shared_ptr<EditorTaskUi::SharedState::Ticket>& ticket,
             EditorTaskResult result) noexcept {
    if (!ticket || !ticket->completion) return;
    try {
        ticket->completion->set_value(std::move(result));
    } catch (const std::future_error&) {
        // キャンセルとUIコールバックは競合する場合がある。
        // 先に成功した状態遷移だけがPromiseを完了する。
    }
}

[[nodiscard]] bool remove_ticket(const std::shared_ptr<EditorTaskUi::SharedState>& state,
                                  const std::shared_ptr<EditorTaskUi::SharedState::Ticket>& ticket) {
    std::scoped_lock lock(state->mutex);
    return state->pending.erase(ticket.get()) != 0U;
}

[[nodiscard]] bool claim_ticket(const std::shared_ptr<EditorTaskUi::SharedState>& state,
                                const std::shared_ptr<EditorTaskUi::SharedState::Ticket>& ticket,
                                const EditorSessionGeneration generation) {
    std::scoped_lock state_lock(state->mutex);
    std::scoped_lock ticket_lock(ticket->mutex);
    if (!state->accepting || state->generation != generation ||
        ticket->phase != EditorTaskUi::SharedState::Ticket::Phase::Queued)
        return false;
    ticket->phase = EditorTaskUi::SharedState::Ticket::Phase::Running;
    return true;
}

void finish_ticket(const std::shared_ptr<EditorTaskUi::SharedState>& state,
                   const std::shared_ptr<EditorTaskUi::SharedState::Ticket>& ticket,
                   EditorTaskResult result) noexcept {
    {
        std::scoped_lock ticket_lock(ticket->mutex);
        if (ticket->phase == EditorTaskUi::SharedState::Ticket::Phase::Cancelled) {
            result = cancelled_result();
        }
        ticket->phase = EditorTaskUi::SharedState::Ticket::Phase::Finished;
    }
    (void)remove_ticket(state, ticket);
    fulfill(ticket, std::move(result));
}

}  // namespace

EditorTaskUi::EditorTaskUi(std::shared_ptr<MainDispatcher> dispatcher,
                           const EditorSessionGeneration generation)
    : state_(std::make_shared<SharedState>(std::move(dispatcher), generation)) {
    if (!state_->dispatcher) throw std::invalid_argument("EditorTaskUi requires a dispatcher");
}

EditorTaskUi::~EditorTaskUi() {
    // post() を意図的に破棄する通知処理の中で、有効な要求が待機してい
    // る場合がある。 要求はUI状態を所有しないため、ここで解放してよい。
    (void)cancel_pending_on_ui_thread();
}

bool EditorTaskUi::post(const EditorSessionGeneration generation,
                        std::function<void()> callback) {
    if (!callback) return false;
    const auto state = state_;
    {
        std::scoped_lock lock(state->mutex);
        if (!state->accepting || state->generation != generation) return false;
    }
    state->dispatcher->post([weak = std::weak_ptr<SharedState>(state), generation,
                             callback = std::move(callback)]() mutable {
        const auto shared = weak.lock();
        if (!shared) return;
        {
            std::scoped_lock lock(shared->mutex);
            if (!shared->accepting || shared->generation != generation) return;
        }
        callback();
    });
    return true;
}

EditorTaskResult EditorTaskUi::invoke(const EditorSessionGeneration generation,
                                      std::function<void()> callback) {
    if (!callback) return EditorTaskResult::rejected("empty editor task UI callback");
    const auto state = state_;
    if (state->dispatcher->is_main_thread()) {
        {
            std::scoped_lock lock(state->mutex);
            if (state->generation != generation)
                return EditorTaskResult::rejected("stale editor task session generation");
            if (!state->accepting) return cancelled_result();
        }
        // コールバックはこの TaskUi への問い合わせ・キャンセルや、入れ子の invoke
        // を行う場合がある。 そのため、受け付け用のロックを解除してから実行する。
        try {
            callback();
            return EditorTaskResult::completed();
        } catch (const std::exception& error) {
            return EditorTaskResult::failed(error.what());
        } catch (...) {
            return EditorTaskResult::failed("unknown exception from editor task UI callback");
        }
    }

    const auto ticket = std::make_shared<SharedState::Ticket>();
    ticket->completion = std::make_shared<std::promise<EditorTaskResult>>();
    auto future = ticket->completion->get_future();
    {
        std::scoped_lock lock(state->mutex);
        if (state->generation != generation)
            return EditorTaskResult::rejected("stale editor task session generation");
        if (!state->accepting) return cancelled_result();
        state->pending.emplace(ticket.get(), ticket);
    }
    try {
    state->dispatcher->post([weak = std::weak_ptr<SharedState>(state), ticket, generation,
                             callback = std::move(callback)]() mutable {
        const auto shared = weak.lock();
        if (!shared) {
            fulfill(ticket, cancelled_result());
            return;
        }
        if (!claim_ticket(shared, ticket, generation)) {
            (void)remove_ticket(shared, ticket);
            fulfill(ticket, cancelled_result());
            return;
        }
        try {
            callback();
            finish_ticket(shared, ticket, EditorTaskResult::completed());
        } catch (const std::exception& error) {
            finish_ticket(shared, ticket, EditorTaskResult::failed(error.what()));
        } catch (...) {
            finish_ticket(shared, ticket,
                          EditorTaskResult::failed("unknown exception from editor task UI callback"));
        }
    });
    } catch (...) {
        // 通知側は、クロージャーを保持した後でも確保・登録を拒否する
        // 場合がある。 待機を解除する前に、まだ待機中の要求を取り消
        // し、遅れた通知が失敗した呼び出しを実行しないようにする。
        EditorTaskResult failure;
        try { throw; }
        catch (const std::exception& error) { failure=EditorTaskResult::failed(error.what()); }
        catch (...) { failure=EditorTaskResult::failed("editor task UI enqueue failed"); }
        bool revoked{};
        {
            std::scoped_lock lock(ticket->mutex);
            if(ticket->phase==SharedState::Ticket::Phase::Queued) {
                ticket->phase=SharedState::Ticket::Phase::Cancelled;
                revoked=true;
            }
        }
        if(revoked) {
            (void)remove_ticket(state,ticket);
            fulfill(ticket,std::move(failure));
        }
        // 通知が始まっていれば、その完了処理が結果を管理する。 コールバッ
        // クの実行中に、呼び出し元が取り込んだ値を破棄しないようにする。
    }
    return future.get();
}

bool EditorTaskUi::cancel_pending_on_ui_thread() noexcept {
    const auto state = state_;
    if (!state || !state->dispatcher || !state->dispatcher->is_main_thread()) return false;
    {
        std::scoped_lock lock(state->mutex);
        state->accepting = false;
        for (auto iterator = state->pending.begin(); iterator != state->pending.end();) {
            // erase() をまたぐ間、所有権を持つ複製を保持する。
            // マップ内の値への参照は、ノードを除いた時点で無効になる。
            const auto ticket = iterator->second;
            bool cancel{};
            {
                std::scoped_lock ticket_lock(ticket->mutex);
                if (ticket->phase == SharedState::Ticket::Phase::Queued) {
                    ticket->phase = SharedState::Ticket::Phase::Cancelled;
                    cancel = true;
                }
            }
            if (cancel) {
                iterator = state->pending.erase(iterator);
                fulfill(ticket, cancelled_result());
            } else {
                ++iterator;
            }
        }
    }
    return true;
}

bool EditorTaskUi::accepting() const noexcept {
    std::scoped_lock lock(state_->mutex);
    return state_->accepting;
}
bool EditorTaskUi::is_ui_thread() const noexcept { return state_->dispatcher->is_main_thread(); }
EditorSessionGeneration EditorTaskUi::generation() const noexcept { return state_->generation; }

EditorTaskContext::EditorTaskContext(const TaskStopToken stop,
                                     std::shared_ptr<EditorTaskUi> ui,
                                     const EditorSessionGeneration generation)
    : stop_(stop), ui_(std::move(ui)), generation_(generation) {}
TaskStopToken EditorTaskContext::stop_token() const noexcept { return stop_; }
bool EditorTaskContext::stop_requested() const noexcept { return stop_.stop_requested(); }
EditorSessionGeneration EditorTaskContext::generation() const noexcept { return generation_; }
bool EditorTaskContext::post_ui(std::function<void()> callback) const {
    return ui_ && ui_->post(generation_, std::move(callback));
}
EditorTaskResult EditorTaskContext::invoke_ui(std::function<void()> callback) const {
    return ui_ ? ui_->invoke(generation_, std::move(callback))
               : EditorTaskResult::rejected("editor task has no UI bridge");
}

struct EditorTaskHandle::State {
    explicit State(const EditorSessionGeneration value) : generation(value) {}
    TaskStopSource stop;
    EditorSessionGeneration generation{};
    std::atomic_bool finished{};
    mutable std::mutex mutex;
    EditorTaskResult result{};
};

EditorTaskHandle::EditorTaskHandle(std::shared_ptr<State> state) : state_(std::move(state)) {}
void EditorTaskHandle::request_stop() noexcept { if (state_) (void)state_->stop.request_stop(); }
EditorTaskSnapshot EditorTaskHandle::snapshot() const {
    if (!state_) return {0U, false, true, EditorTaskResult::rejected("invalid editor task handle")};
    std::scoped_lock lock(state_->mutex);
    return {state_->generation, state_->stop.stop_requested(),
            state_->finished.load(std::memory_order_acquire), state_->result};
}
bool EditorTaskHandle::valid() const noexcept { return static_cast<bool>(state_); }

struct EditorTaskGroup::SharedState {
    struct Worker {
        std::shared_ptr<EditorTaskHandle::State> state;
        // グループの終了には、UI外での明示的な終了待ちが必要。 jthread の暗
        // 黙の停止・終了待ちは使わず、Androidのlibc++でも利用できない。
        std::thread thread;
    };
    mutable std::mutex mutex;
    bool accepting{true};
    bool joined{};
    std::vector<std::unique_ptr<Worker>> workers;
};

EditorTaskGroup::EditorTaskGroup(std::shared_ptr<MainDispatcher> dispatcher,
                                 const EditorSessionGeneration generation)
    : state_(std::make_shared<SharedState>()),
      ui_(std::shared_ptr<EditorTaskUi>(new EditorTaskUi(std::move(dispatcher), generation))) {}

EditorTaskGroup::~EditorTaskGroup() {
    bool safe{};
    {
        std::scoped_lock lock(state_->mutex);
        safe = state_->joined || (state_->workers.empty() && !ui_->accepting());
    }
    if (!safe) std::terminate();
}

EditorTaskHandle EditorTaskGroup::start(EditorTaskFunction task) {
    const auto handle_state = std::make_shared<EditorTaskHandle::State>(ui_->generation());
    if (!task) {
        std::scoped_lock lock(handle_state->mutex);
        handle_state->result = EditorTaskResult::rejected("empty editor task");
        handle_state->finished.store(true, std::memory_order_release);
        return EditorTaskHandle(std::move(handle_state));
    }
    {
        std::scoped_lock lock(state_->mutex);
        if (!state_->accepting) {
            std::scoped_lock result_lock(handle_state->mutex);
            handle_state->result = EditorTaskResult::rejected("editor task group is closing");
            handle_state->finished.store(true, std::memory_order_release);
            return EditorTaskHandle(std::move(handle_state));
        }
        // ワーカーを作る前に領域を確保する。確保失敗時にはまだスレッドがなく、
        // 開始後は確保済みの領域へ unique_ptr を例外なしで移せ
        // るため、 このミューテックスの保持中に破棄・終了待ちを起こさない。
        state_->workers.reserve(state_->workers.size() + 1U);
        // 受け付けとワーカーの登録を一つの排他区間にまとめる。 スレッド開始から所
        // 有権の登録までの間に、終了側が Drained と判断しないようにする。
        auto worker = std::make_unique<SharedState::Worker>();
        worker->state = handle_state;
        const auto ui = ui_;
        worker->thread = std::thread([handle_state, ui, task = std::move(task)]() mutable {
            EditorTaskResult result;
            try {
                EditorTaskContext context(handle_state->stop.get_token(), ui, handle_state->generation);
                result = task(context);
            } catch (const std::exception& error) {
                result = EditorTaskResult::failed(error.what());
            } catch (...) {
                result = EditorTaskResult::failed("unknown exception from editor task");
            }
            {
                std::scoped_lock result_lock(handle_state->mutex);
                handle_state->result = std::move(result);
                handle_state->finished.store(true, std::memory_order_release);
            }
        });
        state_->workers.push_back(std::move(worker));
    }
    return EditorTaskHandle(std::move(handle_state));
}

void EditorTaskGroup::close_submission() noexcept {
    std::scoped_lock lock(state_->mutex);
    state_->accepting = false;
}
void EditorTaskGroup::request_stop_all() {
    std::vector<TaskStopSource> stops;
    {
        std::scoped_lock lock(state_->mutex);
        state_->accepting = false;
        stops.reserve(state_->workers.size());
        for (const auto& worker : state_->workers) stops.push_back(worker->state->stop);
    }
    // 停止コールバックは利用側のコードであり、このグループへ問い合わせる
    // 場合がある。 グループのミューテックスを保持したまま実行しない。
    for (auto& stop : stops) (void)stop.request_stop();
}

EditorTaskGroupSnapshot EditorTaskGroup::snapshot() const {
    EditorTaskGroupSnapshot result;
    result.pending_ui = 0U;
    {
        const auto ui_state = ui_->state_;
        std::scoped_lock lock(ui_state->mutex);
        result.pending_ui = ui_state->pending.size();
    }
    std::scoped_lock lock(state_->mutex);
    result.state = state_->accepting ? EditorTaskGroupState::Accepting : EditorTaskGroupState::Stopping;
    for (const auto& worker : state_->workers) {
        EditorTaskHandle handle(worker->state);
        const auto task = handle.snapshot();
        result.tasks.push_back(task);
        if (!task.finished) ++result.active_workers;
    }
    if (!state_->accepting && result.active_workers == 0U && result.pending_ui == 0U)
        result.state = EditorTaskGroupState::Drained;
    return result;
}

EditorTaskUi& EditorTaskGroup::ui() noexcept { return *ui_; }
const EditorTaskUi& EditorTaskGroup::ui() const noexcept { return *ui_; }

bool EditorTaskGroup::join_all_after_ui_quiesced() {
    if (ui_->is_ui_thread() || ui_->accepting()) return false;
    const auto state = snapshot();
    if (state.state != EditorTaskGroupState::Drained) return false;
    std::vector<std::unique_ptr<SharedState::Worker>> workers;
    {
        std::scoped_lock lock(state_->mutex);
        if (state_->joined) return true;
        workers.swap(state_->workers);
        state_->joined = true;
    }
    for (const auto& worker : workers)
        if (worker->thread.joinable()) worker->thread.join();
    return true;
}

std::shared_ptr<EditorTaskGroup> make_editor_task_group(
    std::shared_ptr<MainDispatcher> dispatcher,
    const EditorSessionGeneration generation) {
    return std::make_shared<EditorTaskGroup>(std::move(dispatcher), generation);
}

}  // namespace cwxeditor::lib
