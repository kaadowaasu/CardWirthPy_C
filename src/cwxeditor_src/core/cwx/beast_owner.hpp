#pragma once

#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/lib/array_slice.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <cstdint>
#include <memory>

namespace cwxeditor::cwx::card {

class BeastCard;

/// 召喚獣カードを所有するオブジェクトのインターフェース。
class BeastOwner : public virtual path::CWXPath {
    std::shared_ptr<path::OwnerLifetimeToken> beast_owner_lifetime_{
        std::make_shared<path::OwnerLifetimeToken>()};

public:
    BeastOwner() = default;
    // 召喚獣の所有者は固有の識別子を持つツリーノード。 コピー時
    // は別オブジェクトとトークンを共有せず、新しい所有者を作る。
    BeastOwner(const BeastOwner&) = delete;
    BeastOwner& operator=(const BeastOwner&) = delete;
    BeastOwner(BeastOwner&&) = delete;
    BeastOwner& operator=(BeastOwner&&) = delete;
    ~BeastOwner() override { invalidateBeastOwnerLifetime(); }

    [[nodiscard]] std::weak_ptr<path::OwnerLifetimeToken>
    beastOwnerLifetimeToken() const noexcept {
        return beast_owner_lifetime_;
    }
    void invalidateBeastOwnerLifetime() noexcept {
        if (beast_owner_lifetime_)
            beast_owner_lifetime_->alive.store(false, std::memory_order_release);
        beast_owner_lifetime_.reset();
    }

    [[nodiscard]] virtual lib::SharedSlice<lib::Owner<BeastCard>> beasts() = 0;
    [[nodiscard]] virtual lib::ConstOwnerSlice<BeastCard> beasts() const = 0;
    [[nodiscard]] virtual lib::Owner<BeastCard> beast(std::uint64_t id) = 0;
    [[nodiscard]] virtual lib::Owner<const BeastCard> beast(
        std::uint64_t id) const = 0;
};

}  // namespace cwxeditor::cwx::card
