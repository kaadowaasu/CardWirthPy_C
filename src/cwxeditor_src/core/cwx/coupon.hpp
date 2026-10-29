#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/array_slice.hpp"
#include "cwxeditor_src/lib/portable.hpp"

namespace cwxeditor::cwx::system {
class XMLInfo;
}

namespace cwxeditor::cwx::usecounter {
class CouponUser;
class UseCounter;
struct CouponId;
}  // namespace cwxeditor::cwx::usecounter

namespace cwxeditor::cwx::coupon {

class Coupon;

/// クーポンのコレクションを所有するためのインターフェース。
class CouponsOwner {
public:
    virtual ~CouponsOwner() = default;

    /// 所有者の現在のクーポン格納先を参照する記述子を、値で
    /// 返す。 格納先の置き換えは共有されるが、構造の変更
    /// ではDの動的配列の取得と同様に新しい記述子を返す。
    [[nodiscard]] virtual lib::SharedSlice<lib::Owner<Coupon>> coupons() = 0;
    /// 同じクーポン格納先に対して、要素も変更できない記述子を返す。
    [[nodiscard]] virtual lib::ConstOwnerSlice<Coupon> coupons() const = 0;
};

/// 名前と数値を持つクーポン。
class Coupon final : public path::CWXPath,
                     public std::enable_shared_from_this<Coupon> {
private:
    lib::Observer<CouponsOwner> owner_;
    lib::Owner<usecounter::CouponUser> coupon_;
    std::int32_t value_{};

public:
    inline static constexpr std::string_view XML_NAME = "Coupon";
    inline static constexpr std::string_view XML_NAME_M = "Coupons";

    /// クーポンを作り、このオブジェクトを変更通知・パ
    /// スの所有者として補助オブジェクトを初期化する。
    Coupon(std::string_view name, std::int32_t value);

    /// コピーを作る。最初は所有者に接続されていない。
    Coupon(const Coupon& other);

    [[nodiscard]] lib::Owner<Coupon> dup() const;

    /// クーポンを所有コレクションへ接続する。所有者は保持せず参照するだけ
    /// にし、 コレクションがクーポンを所有しても循環しないようにする。
    void owner(const lib::Owner<CouponsOwner>& owner);

    [[nodiscard]] lib::Observer<usecounter::UseCounter> useCounter();
    void setUseCounter(const lib::Owner<usecounter::UseCounter>& counter,
                       const lib::Owner<path::CWXPath>& counterOwner);
    void removeUseCounter();
    bool change(const usecounter::CouponId& id);

    void changed() override;

    [[nodiscard]] lib::String cwxPath(bool id) override;
    [[nodiscard]] lib::Owner<path::CWXPath> findCWXPath(
        std::string_view path) override;
    [[nodiscard]] lib::Array<lib::Observer<path::CWXPath>> cwxChilds() override;
    [[nodiscard]] lib::Observer<path::CWXPath> cwxParent() override;

    [[nodiscard]] lib::String coupon() const;
    [[nodiscard]] lib::String name() const { return coupon(); }
    [[nodiscard]] std::int32_t value() const noexcept { return value_; }

    [[nodiscard]] bool opEquals(const Coupon& other) const;
    [[nodiscard]] bool opEquals(const Coupon* other) const {
        return other != nullptr && opEquals(*other);
    }
    [[nodiscard]] bool operator==(const Coupon& other) const {
        return opEquals(other);
    }

    [[nodiscard]] static lib::Owner<Coupon> fromNode(
        const xml::XNode& node, const system::XMLInfo& version);
    [[nodiscard]] xml::XNode toNode() const;
    [[nodiscard]] xml::XNode toNode(xml::XNode& parent) const;
    [[nodiscard]] lib::String toString() const;
};

/// クーポン名のコレクションを管理する、所有者側の再利用可能な補助クラス。
class CouponNamesUser {
private:
    using AcceptedHandler = lib::Callback<void(std::size_t, const lib::String&)>;
    struct SharedState;
    // CouponUsersと弱参照の監視側は、外向きの所有者ハンドルを所有参照へ変換
    // している間だけこの状態を保持する。 状態自体が持つ所有者への参照は弱参照とする。
    std::shared_ptr<SharedState> state_;

public:
    // Dの通常のクラス引数は保持する。Contentの非公開メンバーでは、
    // shared_ptrの循環を避けるため、この追加の所有権の区別を
    // 使う。 包含モデルが補助オブジェクトを保持し、有効な所有者を渡す。
    struct NativeBorrowedOwner {};
    explicit CouponNamesUser(const lib::Owner<path::CWXPath>& cwxPath);
    CouponNamesUser(NativeBorrowedOwner, const lib::Owner<path::CWXPath>& cwxPath);
    ~CouponNamesUser();
    void owner(const lib::Owner<path::CWXPath>& value);
    void retire() noexcept;
    void acceptedValueHandler(
        lib::Callback<void(std::size_t, const lib::String&)> value);

    [[nodiscard]] lib::String cwxPath(bool id) const;

    void changeHandler(lib::Callback<void()> change);

private:
    void changed();

public:
    [[nodiscard]] lib::Owner<usecounter::UseCounter> useCounter();
    [[nodiscard]] lib::Owner<const usecounter::UseCounter> useCounter() const;
    void setUseCounter(const lib::Owner<usecounter::UseCounter>& counter,
                       const lib::Owner<path::CWXPath>& counterOwner);
    void removeUseCounter();

    void couponNames(lib::SharedSlice<lib::String> coupons);
    [[nodiscard]] lib::Array<lib::String> couponNames() const;

    [[nodiscard]] bool expandSPChars() const noexcept;
    void expandSPChars(bool value);

    [[nodiscard]] lib::Array<lib::String> flagsInText() const;
    [[nodiscard]] lib::Array<lib::String> stepsInText() const;
    [[nodiscard]] lib::Array<lib::String> variantsInText() const;
};

}  // namespace cwxeditor::cwx::coupon
