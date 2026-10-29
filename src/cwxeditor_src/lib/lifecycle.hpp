#pragma once

#include "events.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace cwxeditor::lib {

enum class LifecycleState : std::uint8_t {
    Starting,
    Foreground,
    Background,
    Suspended,
    Stopping,
    Stopped,
};

enum class LifecycleEvent : std::uint8_t {
    Ready,
    EnterForeground,
    EnterBackground,
    Suspend,
    Resume,
    MemoryPressure,
    PageHidden,
    StopRequested,
    Stopped,
};

class LifecycleService final {
public:
    using Listener = std::function<void(LifecycleEvent, LifecycleState)>;

    [[nodiscard]] LifecycleState state() const noexcept;
    [[nodiscard]] std::uint64_t add_listener(Listener listener);
    void remove_listener(std::uint64_t token) noexcept;
    void dispatch(LifecycleEvent event);

    void on_persist(Action callback);
    void on_release_resources(Action callback);

private:
    struct Entry { std::uint64_t token{}; Listener listener; };
    [[nodiscard]] static LifecycleState transition(LifecycleState current,
                                                    LifecycleEvent event) noexcept;

    mutable std::mutex mutex_;
    std::vector<Entry> listeners_;
    Action persist_;
    Action release_;
    LifecycleState state_{LifecycleState::Starting};
    std::uint64_t next_token_{1U};
};

}  // namespace cwxeditor::lib
