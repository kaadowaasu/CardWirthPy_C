#include "cwxeditor_src/core/cwx/coupon.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"

#include "cwxeditor_src/core/cwx/usecounter.hpp"

#include <algorithm>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace cwxeditor::cwx::coupon {
namespace {

using CouponUser = usecounter::CouponUser;
using UseCounter = usecounter::UseCounter;
using CwxPath = path::CWXPath;
using CouponIndex = lib::Callback<std::size_t(std::string_view)>;

// これらの利用者は集合が所有する。公開処理は既存のContentの分
// 岐ラベルのID全体用フックとは別に行い、項目の破棄時に無効化する。
class NamedCouponUser final : public CouponUser {
public:
    using CouponUser::CouponUser;
    lib::Callback<void(const lib::String&)> publish;
    void acceptedValue(const lib::String& value) override { if (publish) publish(value); }
    void acceptedTextValue(const lib::String& value) override { if (publish) publish(value); }
};
void retireNameUser(const lib::Owner<CouponUser>& user) noexcept {
    if (!user) return;
    if (auto named = std::dynamic_pointer_cast<NamedCouponUser>(user)) named->publish = {};
    user->removeUseCounter();
    // これはネイティブ側の最終的な破棄であり、通常のDの一覧置換ではな
    // い。一覧の置換は登録だけを外し、古い利用者は引き続き使用できる。
    user->retireTextProducer(true);
}

[[nodiscard]] lib::Owner<CwxPath> asCwxPath(
    const lib::Owner<CouponsOwner>& owner) {
    if (!owner) return lib::Owner<CwxPath>{};
    return std::dynamic_pointer_cast<CwxPath>(owner);
}

[[nodiscard]] lib::Owner<CwxPath> lockCwxPath(
    const lib::Observer<CwxPath>& path) {
    return path.lock();
}

// CouponUserはUseCounterとともに移植している
// 。この小さなアダプターは二種類の構築方法に対応する。Coupon
// の構築中は生の自己参照が必要で、集合側の利用者は所有する共有
// オブジェクトを渡せる。分岐の選択はコンパイル時だけに行う。
template <typename FindIndex>
[[nodiscard]] lib::Owner<CouponUser> makeCouponUser(
    CwxPath* rawOwner, const lib::Owner<CwxPath>& sharedOwner, bool callback,
    FindIndex findIndex) {
    using ValueIndex = lib::Callback<std::size_t(lib::String)>;
    using ReferenceIndex = lib::Callback<std::size_t(const lib::String&)>;
    ValueIndex valueIndex = [findIndex](lib::String text) {
        return findIndex(text);
    };
    ReferenceIndex referenceIndex = [findIndex](const lib::String& text) {
        return findIndex(text);
    };
    if constexpr (requires {
                      std::make_shared<CouponUser>(rawOwner, callback,
                                                    findIndex);
                  }) {
        return std::make_shared<CouponUser>(rawOwner, callback,
                                            std::move(findIndex));
    } else if constexpr (requires {
                             std::make_shared<CouponUser>(sharedOwner,
                                                           callback, findIndex);
                         }) {
        return std::make_shared<CouponUser>(sharedOwner, callback,
                                            std::move(findIndex));
    } else if constexpr (requires {
                             std::make_shared<CouponUser>(rawOwner, callback,
                                                           valueIndex);
                         }) {
        return std::make_shared<CouponUser>(rawOwner, callback,
                                            std::move(valueIndex));
    } else if constexpr (requires {
                             std::make_shared<CouponUser>(sharedOwner,
                                                           callback, valueIndex);
                         }) {
        return std::make_shared<CouponUser>(sharedOwner, callback,
                                            std::move(valueIndex));
    } else if constexpr (requires {
                             std::make_shared<CouponUser>(rawOwner, callback,
                                                           referenceIndex);
                         }) {
        return std::make_shared<CouponUser>(rawOwner, callback,
                                            std::move(referenceIndex));
    } else if constexpr (requires {
                             std::make_shared<CouponUser>(sharedOwner,
                                                           callback,
                                                           referenceIndex);
                         }) {
        return std::make_shared<CouponUser>(sharedOwner, callback,
                                            std::move(referenceIndex));
    } else if constexpr (requires {
                             std::make_shared<CouponUser>(rawOwner, callback);
                         }) {
        return std::make_shared<CouponUser>(rawOwner, callback);
    } else if constexpr (requires {
                             std::make_shared<CouponUser>(sharedOwner,
                                                           callback);
                         }) {
        return std::make_shared<CouponUser>(sharedOwner, callback);
    } else if constexpr (requires { std::make_shared<CouponUser>(rawOwner); }) {
        return std::make_shared<CouponUser>(rawOwner);
    } else if constexpr (requires {
                             std::make_shared<CouponUser>(sharedOwner);
                         }) {
        return std::make_shared<CouponUser>(sharedOwner);
    } else {
        static_assert(sizeof(FindIndex) == 0,
                      "CouponUser has no supported concrete constructor");
    }
}

template <typename UserValue>
void setCouponUserOwner(const lib::Owner<UserValue>& user, CwxPath* rawOwner,
                        const lib::Owner<CwxPath>& sharedOwner) {
    if (!user) return;
    if constexpr (requires(UserValue& value) { value.owner(rawOwner); }) {
        user->owner(rawOwner);
    } else if constexpr (requires(UserValue& value) {
                             value.owner(sharedOwner);
                         }) {
        user->owner(sharedOwner);
    } else if constexpr (requires(UserValue& value) {
                             value.owner(lib::Observer<CwxPath>{sharedOwner});
                         }) {
        user->owner(lib::Observer<CwxPath>{sharedOwner});
    }
}

template <typename UserValue>
void appendTextItems(lib::Array<lib::String>& result,
                     const lib::Array<lib::Owner<UserValue>>& users,
                     auto (UserValue::*items)() const) {
    for (const auto& user : users) {
        if (!user) continue;
        const auto values = (user.get()->*items)();
        result.insert(result.end(), values.begin(), values.end());
    }
}

}  // namespace

Coupon::Coupon(std::string_view name, std::int32_t value)
    : coupon_(makeCouponUser(static_cast<CwxPath*>(this), {}, false,
                             CouponIndex{})),
      value_(value) {
    if (!coupon_) {
        throw std::runtime_error("unable to construct CouponUser");
    }
    coupon_->coupon(lib::String(name));
}

Coupon::Coupon(const Coupon& other)
    : coupon_(makeCouponUser(static_cast<CwxPath*>(this), {}, false,
                             CouponIndex{})),
      value_(other.value_) {
    if (!coupon_) {
        throw std::runtime_error("unable to construct CouponUser");
    }
    coupon_->coupon(other.coupon());
}

lib::Owner<Coupon> Coupon::dup() const {
    return std::make_shared<Coupon>(*this);
}

void Coupon::owner(const lib::Owner<CouponsOwner>& owner) {
    const perf::TraceScope traceCouponD50(__FILE__, __LINE__); // D coupon.d:50
    owner_ = owner;
    const auto ownerPath = asCwxPath(owner);
    setCouponUserOwner(coupon_, ownerPath ? ownerPath.get() : this, ownerPath);
}

lib::Observer<UseCounter> Coupon::useCounter() {
    const perf::TraceScope traceCouponD62(__FILE__, __LINE__); // D coupon.d:62
    if (!coupon_) return lib::Observer<UseCounter>{};
    return lib::Observer<UseCounter>(coupon_->useCounter());
}

void Coupon::setUseCounter(const lib::Owner<UseCounter>& counter,
                           const lib::Owner<CwxPath>& counterOwner) {
    const perf::TraceScope traceCouponD67(__FILE__, __LINE__); // D coupon.d:67
    if (coupon_) coupon_->setUseCounter(counter, counterOwner);
}

void Coupon::removeUseCounter() {
    const perf::TraceScope traceCouponD71(__FILE__, __LINE__); // D coupon.d:71
    if (coupon_) coupon_->removeUseCounter();
}

bool Coupon::change(const usecounter::CouponId& id) {
    const perf::TraceScope traceCouponD75(__FILE__, __LINE__); // D coupon.d:75
    return coupon_ ? coupon_->change(id) : false;
}

void Coupon::changed() {}

lib::String Coupon::cwxPath(bool id) {
    const perf::TraceScope traceCouponD82(__FILE__, __LINE__); // D coupon.d:82
    const auto owner = owner_.lock();
    if (!owner) return lib::String{};

    const auto ownerPath = asCwxPath(owner);
    if (!ownerPath) return lib::String{};

    const auto allCoupons = owner->coupons();
    std::size_t index = 0;
    for (; index < allCoupons.size(); ++index) {
        if (allCoupons[index].get() == this) break;
    }
    return path::cpjoin(ownerPath, "coupon", index < allCoupons.size() ? index : static_cast<std::size_t>(-1), id); // DのcCountUntilは、存在しない場合にsize_t(-1)を返す。
}

lib::Owner<CwxPath> Coupon::findCWXPath(std::string_view pathValue) {
    const perf::TraceScope traceCouponD86(__FILE__, __LINE__); // D coupon.d:86
    if (!path::cpempty(pathValue)) return lib::Owner<CwxPath>{};
    try {
        return std::static_pointer_cast<CwxPath>(shared_from_this());
    } catch (const std::bad_weak_ptr&) {
        // Dのクラス参照は所有ハンドルへ入る前でも使えるが
        // 、C++ではその状態で所有する結果を安全に作れな
        // い。無効参照にならない表現として空の結果を返す。
        return lib::Owner<CwxPath>{};
    }
}

lib::Array<lib::Observer<CwxPath>> Coupon::cwxChilds() {
    return lib::Array<lib::Observer<CwxPath>>{};
}

lib::Observer<CwxPath> Coupon::cwxParent() {
    const auto owner = owner_.lock();
    const auto ownerPath = asCwxPath(owner);
    return ownerPath;
}

lib::String Coupon::coupon() const {
    const perf::TraceScope traceCouponD99(__FILE__, __LINE__); // D coupon.d:99
    return coupon_ ? lib::String(coupon_->coupon()) : lib::String{};
}

bool Coupon::opEquals(const Coupon& other) const {
    const perf::TraceScope traceCouponD111(__FILE__, __LINE__); // D coupon.d:111
    return coupon() == other.coupon() && value_ == other.value_;
}

lib::Owner<Coupon> Coupon::fromNode(const xml::XNode& node,
                                    const system::XMLInfo& version) {
    const perf::TraceScope traceCouponD117(__FILE__, __LINE__); // D coupon.d:117
    (void)version;
    assert(node.name() == XML_NAME);
    return std::make_shared<Coupon>(node.value(),
                                    node.attr<std::int32_t>("value", false,
                                                            0));
}

xml::XNode Coupon::toNode() const {
    const perf::TraceScope traceCouponD123(__FILE__, __LINE__); // D coupon.d:123
    auto node = xml::XNode::create(XML_NAME, coupon());
    node.newAttr("value", value_);
    return node;
}

xml::XNode Coupon::toNode(xml::XNode& parent) const {
    const perf::TraceScope traceCouponD130(__FILE__, __LINE__); // D coupon.d:130
    auto node = parent.newElement(XML_NAME, coupon());
    node.newAttr("value", value_);
    return node;
}

lib::String Coupon::toString() const {
    const perf::TraceScope traceCouponD136(__FILE__, __LINE__); // D coupon.d:136
    lib::String result = coupon();
    result.append(" (");
    if (value_ >= 0) result.push_back('+');
    result.append(std::to_string(value_));
    result.push_back(')');
    return result;
}

struct CouponNamesUser::SharedState final
    : usecounter::UserLeaseState<CouponUser>,
      std::enable_shared_from_this<CouponNamesUser::SharedState> {
    struct LeaseBundle {
        lib::Owner<SharedState> producer;
        lib::Owner<CwxPath> owner;
        lib::Owner<CwxPath> counter_owner;
        lib::Owner<CouponUser> user;
    };

    lib::Array<lib::Owner<CouponUser>> coupons;
    // Dの保持されたインデックスデリゲートは、生成元の現在の集合を参照する。こ
    // の弱参照の登録表は、利用者インスタンスを所有せずに古い利用者と共有する。
    lib::Owner<lib::Array<lib::Observer<CouponUser>>> index_users =
        std::make_shared<lib::Array<lib::Observer<CouponUser>>>();
    bool expand_sp_chars{};
    lib::Callback<void()> change;
    using AcceptedHandler = lib::Callback<void(std::size_t, const lib::String&)>;
    lib::Owner<AcceptedHandler> accepted = std::make_shared<AcceptedHandler>();
    lib::Owner<UseCounter> use_counter;
    lib::Observer<CwxPath> use_counter_owner;
    lib::Owner<CwxPath> owned_counter_owner;
    lib::Observer<CwxPath> cwx_path;
    lib::Owner<CwxPath> owned_path;
    bool borrowed_owner{};
    bool terminal{};

    // CouponUserの直接の利用権は、この生成元の参照関係を生存させる
    // 。入れ子のテキスト所有元の利用権は、正規のUserと移動先の所有元だけ
    // を保持する。最後の直接利用権がなくなるまで最終的な後処理を遅らせる。
    ~SharedState() override { retire(); }

    [[nodiscard]] lib::Owner<CouponUser> leaseOwner(
        const lib::Owner<CouponUser>& canonical) const override {
        if (!canonical || terminal) return canonical;
        auto producer = std::const_pointer_cast<SharedState>(
            std::enable_shared_from_this<SharedState>::shared_from_this());
        auto owner = path::retainCallbackOwner(
            owned_path ? owned_path : lockCwxPath(cwx_path));
        auto counterOwner = path::retainCallbackOwner(
            owned_counter_owner ? owned_counter_owner
                                : use_counter_owner.lock());
        // この別名は外部の利用者向けに公開する。共有状態と所有元を生存させ、
        // Dの束縛されたcCountUntilの生成元の参照関係を維持する。
        // カウンターの弱参照の同一性は正規のUserの制御ブロックに保つ。
        auto bundle = std::make_shared<LeaseBundle>(LeaseBundle{
            std::move(producer), std::move(owner), std::move(counterOwner),
            canonical});
        return lib::Owner<CouponUser>(std::move(bundle), canonical.get());
    }

    void retire() noexcept {
        if (terminal) return;
        const usecounter::detail::NativeCounterRetirement native_retirement;
        terminal = true;
        change = {};
        *accepted = {};
        for (const auto& user : coupons) retireNameUser(user);
        index_users->clear();
        coupons.clear();
        use_counter.reset();
        use_counter_owner.reset();
        owned_counter_owner.reset();
        cwx_path.reset();
        owned_path.reset();
    }
};

CouponNamesUser::CouponNamesUser(const lib::Owner<CwxPath>& cwxPath)
    : state_(std::make_shared<SharedState>()) {
    state_->cwx_path = cwxPath;
    state_->owned_path = cwxPath;
}
CouponNamesUser::CouponNamesUser(NativeBorrowedOwner,
                                 const lib::Owner<CwxPath>& cwxPath)
    : state_(std::make_shared<SharedState>()) {
    state_->cwx_path = cwxPath;
    state_->borrowed_owner = true;
}
// 入れ子のテキスト保持部は、正規の親Userと移動先の所有元を保持する。ラッパー解放
// 後もSharedStateを保持するのは、CouponUserの直接利用権だけ。
CouponNamesUser::~CouponNamesUser() = default;
void CouponNamesUser::owner(const lib::Owner<CwxPath>& value) {
    state_->cwx_path = value;
    if (!state_->borrowed_owner) state_->owned_path = value;
    for (const auto& user : state_->coupons) if (user) user->owner(value);
}
void CouponNamesUser::retire() noexcept { state_->retire(); }

void CouponNamesUser::acceptedValueHandler(
    lib::Callback<void(std::size_t, const lib::String&)> value) {
    *state_->accepted = std::move(value);
}

lib::String CouponNamesUser::cwxPath(bool id) const {
    const auto owner = path::retainCallbackOwner(
        state_->owned_path ? state_->owned_path : lockCwxPath(state_->cwx_path));
    if (!owner) throw std::invalid_argument("Null coupon-name path owner");
    return owner->cwxPath(id);
}

void CouponNamesUser::changeHandler(lib::Callback<void()> change) {
    const perf::TraceScope traceCouponD158(__FILE__, __LINE__); // D coupon.d:158
    state_->change = std::move(change);
}

void CouponNamesUser::changed() {
    const perf::TraceScope traceCouponD162(__FILE__, __LINE__); // D coupon.d:162
    if (state_->change) state_->change();
}

lib::Owner<UseCounter> CouponNamesUser::useCounter() {
    return state_->use_counter;
}
lib::Owner<const UseCounter> CouponNamesUser::useCounter() const {
    return state_->use_counter;
}

void CouponNamesUser::setUseCounter(
    const lib::Owner<UseCounter>& counter,
    const lib::Owner<CwxPath>& counterOwner) {
    const perf::TraceScope traceCouponD172(__FILE__, __LINE__); // D coupon.d:172
    const auto desired = counter;
    const auto desired_owner = counterOwner;
    const auto users = state_->coupons;
    for (const auto& user : users) {
        const perf::TraceScope traceCouponD173(__FILE__, __LINE__); // D coupon.d:173
        if (user) user->setUseCounter(desired, desired_owner);
    }
    state_->use_counter = desired;
    const auto scope = usecounter::renameInfo(desired_owner);
    state_->use_counter_owner = scope;
    if (!state_->borrowed_owner) state_->owned_counter_owner = scope;
}

void CouponNamesUser::removeUseCounter() {
    const perf::TraceScope traceCouponD180(__FILE__, __LINE__); // D coupon.d:180
    const auto users = state_->coupons;
    for (const auto& user : users) {
        const perf::TraceScope traceCouponD181(__FILE__, __LINE__); // D coupon.d:181
        if (user) user->removeUseCounter();
    }
    state_->use_counter.reset();
    state_->use_counter_owner.reset();
    state_->owned_counter_owner.reset();
}

void CouponNamesUser::couponNames(lib::SharedSlice<lib::String> names) {
    const perf::TraceScope traceCouponD189(__FILE__, __LINE__); // Dのcoupon.d:189に対応。非公開のネイティブ
    // 生成元はこのモデルに含まれる。書き込み前のハンドラーが
    // 外部の所有元を解放しても、包含するモデルを生存させる。
    const auto parent_alive = path::retainCallbackOwner(
        state_->owned_path ? state_->owned_path : lockCwxPath(state_->cwx_path));
    if (names == couponNames()) return;
    {
    const perf::TraceScope traceCouponD190(__FILE__, __LINE__); // D coupon.d:190

    changed();
    const auto previous_users = state_->coupons;
    for (const auto& user : previous_users) {
        const perf::TraceScope traceCouponD192(__FILE__, __LINE__); // D coupon.d:192
        if (user) {
            user->removeUseCounter();
            // 一覧からCouponUserを除いた後も、保持済み
            // のオブジェクトではパス参照用の索引取得コールバック
            // を維持する。変更通知経路とテキスト登録は切り離す。
            user->retireTextProducer(true, true);
        }
    }
    state_->coupons.clear();
    state_->index_users->clear();

    const auto owner = lockCwxPath(state_->cwx_path);
    for (const auto& name : names) {
        const perf::TraceScope traceCouponD196(__FILE__, __LINE__); // D coupon.d:196
        if (name.empty()) continue;

        CouponIndex findIndex = [users = state_->index_users](std::string_view text) {
            const perf::TraceScope traceCouponD198(__FILE__, __LINE__); // D coupon.d:198
            for (std::size_t index = 0; index < users->size(); ++index) {
                const auto user = (*users)[index].lock();
                if (user && user->coupon() == text) {
                    return index;
                }
            }
            return static_cast<std::size_t>(-1); // DのcCountUntilは、存在しない場合にsize_t(-1)を返す。
        };

        auto user = std::make_shared<NamedCouponUser>(owner, false, std::move(findIndex));
        if (!user) {
            throw std::runtime_error("unable to construct CouponUser");
        }
        user->setUserLeaseState(state_);
        user->owner(owner);
        user->expandSPChars(state_->expand_sp_chars);
        user->coupon(name);
        if (const auto counter = state_->use_counter) {
            const auto scope = state_->owned_counter_owner
                ? state_->owned_counter_owner
                : state_->use_counter_owner.lock();
            user->setUseCounter(counter, scope);
        }
        const auto index = state_->coupons.size();
        const lib::Observer<CouponUser> identity = user->weak_from_this();
        user->publish = [users = state_->index_users,
                         accepted = state_->accepted, index, identity](const lib::String& value) {
            const auto source = identity.lock();
            if (source && index < users->size() && (*users)[index].lock() == source) {
                const auto handler = *accepted;
                if (handler) handler(index, value);
            }
        };
        state_->index_users->push_back(identity);
        state_->coupons.push_back(std::move(user));
    }
    } // D の coupon.d:190 に対応する範囲の終端。
}

lib::Array<lib::String> CouponNamesUser::couponNames() const {
    const perf::TraceScope traceCouponD212(__FILE__, __LINE__); // D coupon.d:212
    lib::Array<lib::String> result;
    result.reserve(state_->coupons.size());
    for (const auto& user : state_->coupons) {
        if (user) result.emplace_back(user->coupon());
    }
    return result;
}

bool CouponNamesUser::expandSPChars() const noexcept {
    return state_->expand_sp_chars;
}

void CouponNamesUser::expandSPChars(bool value) {
    const perf::TraceScope traceCouponD224(__FILE__, __LINE__); // D coupon.d:224
    if (value == state_->expand_sp_chars) return;
    {
    const perf::TraceScope traceCouponD225(__FILE__, __LINE__); // D coupon.d:225

    changed();
    state_->expand_sp_chars = value;
    for (const auto& coupon : state_->coupons) {
        const perf::TraceScope traceCouponD228(__FILE__, __LINE__); // D coupon.d:228
        if (coupon) coupon->expandSPChars(value);
    }
    } // D の coupon.d:225 に対応する範囲の終端。
}

lib::Array<lib::String> CouponNamesUser::flagsInText() const {
    lib::Array<lib::String> result;
    // Dは変更コールバック中も含め、各子の状態を集計する。
    appendTextItems(result, state_->coupons, &CouponUser::flagsInText);
    return result;
}

lib::Array<lib::String> CouponNamesUser::stepsInText() const {
    lib::Array<lib::String> result;
    appendTextItems(result, state_->coupons, &CouponUser::stepsInText);
    return result;
}

lib::Array<lib::String> CouponNamesUser::variantsInText() const {
    lib::Array<lib::String> result;
    appendTextItems(result, state_->coupons, &CouponUser::variantsInText);
    return result;
}

}  // namespace cwxeditor::cwx::coupon
