#include "lifecycle.hpp"

#include <algorithm>

namespace cwxeditor::lib {

LifecycleState LifecycleService::state() const noexcept {
    std::scoped_lock lock(mutex_);
    return state_;
}

std::uint64_t LifecycleService::add_listener(Listener listener) {
    if (!listener) return 0U;
    std::scoped_lock lock(mutex_);
    const auto token = next_token_++;
    listeners_.push_back({token, std::move(listener)});
    return token;
}

void LifecycleService::remove_listener(const std::uint64_t token) noexcept {
    std::scoped_lock lock(mutex_);
    std::erase_if(listeners_, [token](const Entry& entry) { return entry.token == token; });
}

void LifecycleService::on_persist(Action callback) {
    std::scoped_lock lock(mutex_);
    persist_ = std::move(callback);
}

void LifecycleService::on_release_resources(Action callback) {
    std::scoped_lock lock(mutex_);
    release_ = std::move(callback);
}

void LifecycleService::dispatch(const LifecycleEvent event) {
    std::vector<Entry> listeners;
    Action persist;
    Action release;
    LifecycleState next;
    {
        std::scoped_lock lock(mutex_);
        state_ = transition(state_, event);
        next = state_;
        listeners = listeners_;
        if (event == LifecycleEvent::EnterBackground || event == LifecycleEvent::Suspend ||
            event == LifecycleEvent::PageHidden || event == LifecycleEvent::StopRequested)
            persist = persist_;
        if (event == LifecycleEvent::MemoryPressure || event == LifecycleEvent::StopRequested)
            release = release_;
    }
    if (persist) persist();
    if (release) release();
    for (const auto& entry : listeners)
        if (entry.listener) entry.listener(event, next);
}

LifecycleState LifecycleService::transition(const LifecycleState current,
                                            const LifecycleEvent event) noexcept {
    switch (event) {
    case LifecycleEvent::Ready:
    case LifecycleEvent::EnterForeground:
    case LifecycleEvent::Resume: return LifecycleState::Foreground;
    case LifecycleEvent::EnterBackground:
    case LifecycleEvent::PageHidden: return LifecycleState::Background;
    case LifecycleEvent::Suspend: return LifecycleState::Suspended;
    case LifecycleEvent::StopRequested: return LifecycleState::Stopping;
    case LifecycleEvent::Stopped: return LifecycleState::Stopped;
    case LifecycleEvent::MemoryPressure: return current;
    }
    return current;
}

}  // namespace cwxeditor::lib
