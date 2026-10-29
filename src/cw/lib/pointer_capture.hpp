#pragma once
#include <cstdint>
#include <optional>

namespace cw::lib {
// ジェスチャーは開始したデバイス・指が所有する。追加の指は無視し、所有者が
// 終了しても引き継がせない。SDL/OS への依存や UI は持たない。
class PointerCapture {
public:
    bool begin(std::uint64_t device, std::uint64_t finger) noexcept {
        if (owner_) return false;
        owner_ = Owner{device, finger};
        return true;
    }
    bool owns(std::uint64_t device, std::uint64_t finger) const noexcept {
        return owner_ && owner_->device == device && owner_->finger == finger;
    }
    bool end(std::uint64_t device, std::uint64_t finger) noexcept {
        if (!owns(device, finger)) return false;
        cancel();
        return true;
    }
    void cancel() noexcept { owner_.reset(); }
    bool active() const noexcept { return owner_.has_value(); }
private:
    struct Owner { std::uint64_t device, finger; };
    std::optional<Owner> owner_;
};

// プロセスのイベントキューで、ウィンドウごとに所有権を管理する。二つのネイティブゲー
// ム入力処理は同じインスタンスを共有し、外部ダイアログからその操作を終了させない。
class WindowPointerCapture {
public:
    bool begin(std::uint32_t window, std::uint64_t device, std::uint64_t finger) noexcept {
        if (!pointer_.begin(device, finger)) return false;
        window_ = window;
        return true;
    }
    bool owns(std::uint32_t window, std::uint64_t device, std::uint64_t finger) const noexcept {
        return window == window_ && pointer_.owns(device, finger);
    }
    bool end(std::uint32_t window, std::uint64_t device, std::uint64_t finger) noexcept {
        if (!owns(window, device, finger)) return false;
        pointer_.cancel();
        return true;
    }
    bool cancel_window(std::uint32_t window) noexcept {
        if (window != window_) return false;
        return cancel();
    }
    bool cancel() noexcept {
        const bool was_active = pointer_.active();
        pointer_.cancel();
        return was_active;
    }
private:
    PointerCapture pointer_;
    std::uint32_t window_{};
};
}
