#pragma once

namespace cardwirth::platform {

// 非表示→最小化、表示→復元など、独立した状態通知は重なる場
// 合がある。 一つの理由を解除しても、他の理由は解除しない。
class AppVisibility {
public:
    enum Reason : unsigned { Background = 1, Hidden = 2, Minimized = 4, Covered = 8 };
    bool set(Reason reason, bool active) noexcept {
        const bool before = suspended();
        if (active) reasons_ |= reason;
        else reasons_ &= ~static_cast<unsigned>(reason);
        return before != suspended();
    }
    bool suspended() const noexcept { return reasons_ != 0; }
private:
    unsigned reasons_{};
};

} // namespace cardwirth::platform
