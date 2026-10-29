#pragma once

// cwx/usecounter.dのC++20移
// 植。D版はUserをカウンターの外で所有し、HashSet
// へ登録する。 C++も同じ所有規則に従い、
// カウンターは管理用コンテナーだけを所有して
// Userを弱参照する。 公開の取得結果は、
// 取得後にも使えるよう共有所有で返す。 カウ
// ンターからUserへの所有循環は作らない。

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/nullable_string.hpp"

namespace cwxeditor::cwx::coupon {
class CouponNamesUser;
}  // namespace cwxeditor::cwx::coupon

namespace cwxeditor::cwx::textholder {
class SimpleTextHolder;
}  // namespace cwxeditor::cwx::textholder

namespace cwxeditor::cwx::usecounter {

using CwxPath = path::CWXPath;
using String = lib::String;
using NullableString = lib::NullableString;
using CounterOwner = lib::Owner<class UseCounter>;

class UseCounter;
struct PathId;
template <typename T> class UserObserver;
template <typename T> class UserLeaseState;

namespace detail {

// DのGCではネイティブツリーの確定的な破棄は行われず、直接User.change()
// を呼ぶと 契約上、旧キーの下に登録が残る場合がある。ネイティ
// ブの破棄・終了時だけ Userの同一性で登録を解除し、通常
// のremove()はDのキー欠落時の失敗と配送規則を保つ。
class NativeCounterRetirement final {
    inline static thread_local bool active_ = false;
    bool previous_;
public:
    NativeCounterRetirement() noexcept : previous_(active_) { active_ = true; }
    ~NativeCounterRetirement() noexcept { active_ = previous_; }
    NativeCounterRetirement(const NativeCounterRetirement&) = delete;
    NativeCounterRetirement& operator=(const NativeCounterRetirement&) = delete;
    [[nodiscard]] static bool active() noexcept { return active_; }
};

template <typename T>
[[nodiscard]] inline lib::Owner<T> nonOwning(T* pointer) {
    if (pointer == nullptr) return lib::Owner<T>{};
    return lib::Owner<T>(pointer, [](T*) {});
}

// DのUsersのスナップショットは、各Userのクラス型の所有者も保持する
// 。 ネイティブでは循環防止の弱参照を使うため、同じコールバック期間だけ有効
// な所有者を保持する。 追加の汎用User型には所有者の公開を要求しない。
template <typename Users>
[[nodiscard]] auto retainUserOwners(const Users& users) {
    std::vector<lib::Owner<CwxPath>> owners;
    owners.reserve(users.size());
    for (const auto& user : users) {
        if constexpr (requires { user->owner(); }) {
            using Owner = decltype(user->owner());
            if constexpr (std::is_convertible_v<Owner, lib::Owner<CwxPath>>) {
                if (user) owners.push_back(path::retainCallbackOwner(user->owner()));
            }
        }
    }
    return owners;
}

template <typename T>
struct IdHash {
    std::size_t operator()(const T& value) const noexcept {
        return static_cast<std::size_t>(value.toHash());
    }
};

template <typename T>
struct WeakHash {
    std::size_t operator()(const lib::Observer<T>& value) const noexcept {
        return std::hash<T*>{}(value.lock().get());
    }
};

template <typename T>
struct WeakEqual {
    bool operator()(const lib::Observer<T>& lhs,
                    const lib::Observer<T>& rhs) const noexcept {
        return lhs.lock().get() == rhs.lock().get();
    }
};

template <typename T>
class WeakUserSet {
public:
    using Observer = UserObserver<T>;
    using Storage = std::vector<Observer>;
    using iterator = typename Storage::iterator;
    using const_iterator = typename Storage::const_iterator;

    [[nodiscard]] static bool isNullReference(const Observer& value) noexcept {
        const Observer empty;
        return !value.owner_before(empty) && !empty.owner_before(value);
    }

    void insert(Observer value) {
        const auto retained = value.lockCanonical();
        const auto pointer = retained.get();
        const bool nullValue = isNullReference(value) || (!value.expired() && !pointer);
        // {}の代入では弱参照版とコピー版が曖昧になるため、明示的にリセットする。
        if (nullValue) value.reset();
        values_.erase(std::remove_if(values_.begin(), values_.end(),
            [pointer, nullValue](const Observer& current) {
                if (isNullReference(current)) return nullValue;
                const auto locked = current.lockCanonical();
                return !locked || (!nullValue && locked.get() == pointer);
            }), values_.end());
        // DのHashSetはnullのクラス参照も実際の要素として受け付ける。
        // GCの循環を弱参照へ移す際、期限切れのネイティブ参照とは区別する。
        if (nullValue || pointer) values_.push_back(std::move(value));
    }
    void erase(const Observer& value) {
        const auto retained = value.lockCanonical();
        const auto pointer = retained.get();
        const bool nullValue = isNullReference(value) || (!value.expired() && !pointer);
        values_.erase(std::remove_if(values_.begin(), values_.end(),
            [pointer, nullValue](const Observer& current) {
                if (isNullReference(current)) return nullValue;
                const auto locked = current.lockCanonical();
                return !locked || (!nullValue && locked.get() == pointer);
            }), values_.end());
    }
    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
    iterator begin() noexcept { return values_.begin(); }
    iterator end() noexcept { return values_.end(); }
    const_iterator begin() const noexcept { return values_.begin(); }
    const_iterator end() const noexcept { return values_.end(); }

private:
    Storage values_;
};

[[nodiscard]] String pathString(const PathId& value);
[[nodiscard]] bool wildcardEqual(std::string_view value,
                                 std::string_view pattern,
                                 bool caseInsensitive);
[[nodiscard]] int lexicalCompare(std::string_view lhs,
                                 std::string_view rhs,
                                 bool caseInsensitive);

}  // namespace detail

/// ID形式の文字列を、対応するID型へ変換する。
template <typename ID>
struct ToID;

/// 特定のIDを使う利用元。下の具体的なUserクラスが、値
/// の保持・カウンター登録・ D向けのプロパティを追加する。
template <typename K>
class User : public CwxPath {
public:
    ~User() override = default;
    [[nodiscard]] virtual bool change(const K& newValue) = 0;
};

/// 通知先の所有者はIDの置換を拒否できる。 UseCounterがロ
/// ーカルIDを移す間も、自身のカウンターとの関連付けを保持できる。
template <typename T>
class TChgCallback {
public:
    virtual ~TChgCallback() = default;
    [[nodiscard]] virtual bool changeCallback(const T& oldValue,
                                              const T& newValue) = 0;
    [[nodiscard]] virtual lib::Owner<CwxPath> ucOwner() = 0;
    virtual void setUseCounter(const CounterOwner& counter,
                               const lib::Owner<CwxPath>& counterOwner) = 0;
    virtual void removeUseCounter() = 0;
    // ネイティブの寿命監視から、この版番号を参照できる。通常のローカルID
    // の改名では 版番号で通知を抑止せず、Dと同じUserごとの切り離し
    // ・変更・再接続を行う。 スナップショットは有効な所有者を保持する。
    [[nodiscard]] virtual std::uint64_t useCounterRevision() const noexcept { return 0; }
    // ローカル改名では別の世代番号を使い、コールバック内の明示的な関連付
    // け変更を検出する。 通常の文字列再解析は、その変更とはみなさない。
    [[nodiscard]] virtual std::uint64_t useCounterBindingRevision() const noexcept { return 0; }
};

namespace detail {

/// 通常のローカルFlag/StepのUser変更で使う、任意の所有者保持。 User::change
/// 全体を対象にする一方、Contentは最初のchanged()で一度限り
/// の印を消費し、 コールバックの再入時もchanged()の規則に従う。
struct OrdinaryFlagStepChangeMarker {
    OrdinaryFlagStepChangeMarker* previous{};
    bool consumed{};
};

class OrdinaryFlagStepChangeOwner {
public:
    virtual ~OrdinaryFlagStepChangeOwner() = default;
    virtual void beginOrdinaryFlagStepChange(
        OrdinaryFlagStepChangeMarker& marker) noexcept = 0;
    virtual void endOrdinaryFlagStepChange(
        OrdinaryFlagStepChangeMarker& marker) noexcept = 0;
};

class OrdinaryFlagStepChangeScope {
public:
    explicit OrdinaryFlagStepChangeScope(const lib::Owner<CwxPath>& path)
        : owner_(std::dynamic_pointer_cast<OrdinaryFlagStepChangeOwner>(path)) {
        if (owner_) owner_->beginOrdinaryFlagStepChange(marker_);
    }
    OrdinaryFlagStepChangeScope(const OrdinaryFlagStepChangeScope&) = delete;
    OrdinaryFlagStepChangeScope& operator=(const OrdinaryFlagStepChangeScope&) = delete;
    ~OrdinaryFlagStepChangeScope() {
        if (owner_) owner_->endOrdinaryFlagStepChange(marker_);
    }

private:
    lib::Owner<OrdinaryFlagStepChangeOwner> owner_;
    OrdinaryFlagStepChangeMarker marker_;
};

}  // namespace detail

struct FlagId;
struct StepId;
struct VariantId;
struct AreaId;
struct BattleId;
struct PackageId;
struct CastId;
struct SkillId;
struct ItemId;
struct BeastId;
struct InfoId;
struct CouponId;
struct GossipId;
struct CompleteStampId;
struct KeyCodeId;
struct CellNameId;
struct CardGroupId;
struct StartId;

using ChgAreaCallback = TChgCallback<AreaId>;
using ChgBattleCallback = TChgCallback<BattleId>;
using ChgFlagCallback = TChgCallback<FlagId>;
using ChgStepCallback = TChgCallback<StepId>;
using ChgVariantCallback = TChgCallback<VariantId>;
using ChgPathCallback = TChgCallback<PathId>;
using ChgCouponCallback = TChgCallback<CouponId>;
using ChgGossipCallback = TChgCallback<GossipId>;

/// コールバックへ渡すUserハンドルに、必要な寿命を追加保持するための任意の
/// 手順。 Userと監視側は手順を弱参照し、所有ハンドルを渡すときだけ一時
/// 的な関連オブジェクト群を保持できる。 カウンターの所属情報は変更しない。
template <typename T>
class UserLeaseState {
public:
    virtual ~UserLeaseState() = default;
    [[nodiscard]] virtual lib::Owner<T> leaseOwner(
        const lib::Owner<T>& canonical) const = 0;
};

/// UserBase用の公開の弱参照。std::weak_ptr::lock()
/// と異なり、 対応するUserの一時的な寿命保持をlock()で再構成できる。
template <typename T>
class UserObserver {
    using Value = std::remove_const_t<T>;
    using LeaseState = UserLeaseState<Value>;

public:
    UserObserver() noexcept = default;
    UserObserver(std::nullptr_t) noexcept {}

    UserObserver(const lib::Observer<T>& observer) noexcept {
        if (const auto user = observer.lock()) capture(user);
        else canonical_ = observer;
    }
    UserObserver(lib::Observer<T> canonical,
                 lib::Observer<LeaseState> leaseState) noexcept
        : canonical_(std::move(canonical)), lease_state_(std::move(leaseState)) {}
    UserObserver& operator=(const lib::Observer<T>& observer) noexcept {
        canonical_ = observer;
        lease_state_.reset();
        if (const auto user = observer.lock()) capture(user);
        return *this;
    }

    template <typename U>
        requires std::is_convertible_v<U*, T*>
    UserObserver(const lib::Owner<U>& user) {
        capture(user);
    }

    template <typename U>
        requires std::is_convertible_v<U*, T*>
    UserObserver(const lib::Observer<U>& observer) noexcept {
        if (const auto user = observer.lock()) capture(user);
        else canonical_ = observer;
    }

    template <typename U>
        requires std::is_convertible_v<U*, T*>
    UserObserver& operator=(const lib::Observer<U>& observer) noexcept {
        if (const auto user = observer.lock()) capture(user);
        else {
            canonical_ = observer;
            lease_state_.reset();
        }
        return *this;
    }

    [[nodiscard]] lib::Owner<T> lock() const {
        auto canonical = canonical_.lock();
        if (!canonical) return {};
        const auto state = lease_state_.lock();
        if (!state) return canonical;
        if constexpr (std::is_const_v<T>) {
            return state->leaseOwner(std::const_pointer_cast<Value>(canonical));
        } else {
            return state->leaseOwner(canonical);
        }
    }

    [[nodiscard]] bool expired() const noexcept { return canonical_.expired(); }
    [[nodiscard]] long use_count() const noexcept { return canonical_.use_count(); }
    void reset() noexcept {
        canonical_.reset();
        lease_state_.reset();
    }

    [[nodiscard]] bool owner_before(const UserObserver& other) const noexcept {
        return canonical_.owner_before(other.canonical_);
    }
    template <typename U>
    [[nodiscard]] bool owner_before(const lib::Observer<U>& other) const noexcept {
        return canonical_.owner_before(other);
    }
    template <typename U>
    [[nodiscard]] bool owner_before(const lib::Owner<U>& other) const noexcept {
        return canonical_.owner_before(other);
    }

    // std::weak_ptrとして明示的に保存するコードとのソー
    // ス互換性を保つ。 その変換では正式な弱参照の識別だけを残すため
    // 、追加の寿命保持が必要ならこのラッパーのlock()を使う。
    [[nodiscard]] operator lib::Observer<T>() const noexcept { return canonical_; }

private:
    template <typename> friend class detail::WeakUserSet;

    [[nodiscard]] lib::Owner<T> lockCanonical() const {
        return canonical_.lock();
    }

    template <typename U>
    void capture(const lib::Owner<U>& user) {
        if (!user) {
            canonical_.reset();
            lease_state_.reset();
            return;
        }
        if constexpr (requires {
                          user->canonicalWeakFromThis();
                          user->userLeaseState();
                      }) {
            canonical_ = user->canonicalWeakFromThis();
            // スタック上や生のthisのUserは、selfOwner()の削除しな
            // いアダプターを使う。 その弱い制御ブロックが、唯一の正式な参照になる。
            if (canonical_.expired()) canonical_ = user;
            lease_state_ = user->userLeaseState();
        } else {
            canonical_ = user;
            lease_state_.reset();
        }
    }

    lib::Observer<T> canonical_;
    lib::Observer<LeaseState> lease_state_;
};

/// すべての*Userで使うCWXPath転送ミックスインの共通
/// 実装。 shared_ptrで確保された場合、CRTPの型
/// から具体的なUserの所有ハンドルをカウンター操作へ渡す。
template <typename Derived, typename K>
class UserBase : public User<K>,
                 public std::enable_shared_from_this<Derived> {
    using EnableShared = std::enable_shared_from_this<Derived>;

public:
    using UserType = Derived;

    explicit UserBase(const lib::Owner<CwxPath>& owner = {})
        : owner_(owner) {}
    explicit UserBase(CwxPath* owner)
        : UserBase(detail::nonOwning(owner)) {
        owner_anchor_ = detail::nonOwning(owner);
        owner_ = owner_anchor_;
    }
    ~UserBase() override = default;

    // 公開ハンドルはshared_from_this/weak_from_thisから任意の関
    // 連オブジェクト保持を復元できる。 カウンター登録には、下の正式な所有・識別の入口を使う。
    [[nodiscard]] lib::Owner<Derived> shared_from_this() {
        auto retained = weak_from_this().lock();
        if (!retained) throw std::bad_weak_ptr{};
        return retained;
    }
    [[nodiscard]] lib::Owner<const Derived> shared_from_this() const {
        auto retained = weak_from_this().lock();
        if (!retained) throw std::bad_weak_ptr{};
        return retained;
    }
    [[nodiscard]] UserObserver<Derived> weak_from_this() noexcept {
        return {canonicalWeakFromThis(), lease_state_};
    }
    [[nodiscard]] UserObserver<const Derived> weak_from_this() const noexcept {
        return {canonicalWeakFromThis(), lease_state_};
    }

private:
    template <typename> friend class UserObserver;
    friend class ::cwxeditor::cwx::coupon::CouponNamesUser;
    friend class ::cwxeditor::cwx::textholder::SimpleTextHolder;

    // 公開の別名ハンドルが存在しても、内部の所有権と識別には enable_shared_from_this
    // の元の制御ブロックを使う。
    [[nodiscard]] lib::Owner<Derived> canonicalOwner() {
        return EnableShared::shared_from_this();
    }
    [[nodiscard]] lib::Owner<const Derived> canonicalOwner() const {
        return EnableShared::shared_from_this();
    }
    [[nodiscard]] lib::Observer<Derived> canonicalWeakFromThis() noexcept {
        return EnableShared::weak_from_this();
    }
    [[nodiscard]] lib::Observer<const Derived> canonicalWeakFromThis() const noexcept {
        return EnableShared::weak_from_this();
    }
    [[nodiscard]] lib::Observer<UserLeaseState<Derived>> userLeaseState() const noexcept {
        return lease_state_;
    }
    void setUserLeaseState(
        const lib::Owner<UserLeaseState<Derived>>& state) noexcept {
        lease_state_ = state;
    }

public:
    [[nodiscard]] lib::Owner<CwxPath> retainForCallback() override {
        return std::static_pointer_cast<CwxPath>(this->weak_from_this().lock());
    }

    // 受け付け済みの保持値だけを公開し、所属情報は各呼び出し元が管理する。
    virtual void acceptedValue(const String&) {}
    virtual void acceptedNumericValue(std::uint64_t) {}

    [[nodiscard]] lib::Owner<CwxPath> owner() const {
        // コンストラクターで作った生のthisの関連付けは、 モデルがツリー
        // に管理されるようになってから本来の共有制御ブロックを取得できる。
        return path::retainCallbackOwner(owner_anchor_ ? owner_anchor_ : owner_.lock());
    }
    void owner(const lib::Owner<CwxPath>& owner) {
        owner_anchor_.reset();
        owner_ = owner;
    }
    void owner(CwxPath* owner) {
        owner_anchor_ = detail::nonOwning(owner);
        owner_ = owner_anchor_;
    }

    [[nodiscard]] String cwxPath(bool id) override {
        const auto owner = this->owner();
        return owner ? owner->cwxPath(id) : String{};
    }

    [[nodiscard]] lib::Owner<CwxPath> findCWXPath(
        std::string_view value) override {
        const auto owner = this->owner();
        return owner ? owner->findCWXPath(value) : lib::Owner<CwxPath>{};
    }

    [[nodiscard]] lib::Array<lib::Observer<CwxPath>> cwxChilds() override {
        // 言語差: 列挙結果の配列は値コピーだが、各要素が指す同じ子オブジェクトは共有する。
        // 配列自体の書換えによる所有者の子一覧の変更は、この列挙APIでは行わない。
        const auto owner = this->owner();
        return owner ? owner->cwxChilds()
                      : lib::Array<lib::Observer<CwxPath>>{};
    }

    [[nodiscard]] lib::Observer<CwxPath> cwxParent() override {
        const auto owner = this->owner();
        return owner ? owner->cwxParent()
                      : lib::Observer<CwxPath>{};
    }

    void changed() override {
        if (const auto owner = this->owner()) owner->changed();
    }

protected:
    [[nodiscard]] lib::Owner<Derived> selfOwner() {
        try {
            return canonicalOwner();
        } catch (const std::bad_weak_ptr&) {
            // ネイティブではスタック上のオブジェクトも使うため、呼び出し元が生か
            // している間は 弱参照の項目も有効になる、所有しない安定したアダプタ
            // ーを保つ。DのUserはGC参照に当たる。 UserBaseのコン
            // ストラクターでは作らない。そこで作るとmake_sharedが本来
            // の所有ブロックを 設定する前にenable_shared_from_this
            // が初期化され、通知時のスナップショットが所有権を失う。
            if (!self_anchor_)
                self_anchor_ = lib::Owner<Derived>(static_cast<Derived*>(this),
                                                  [](Derived*) noexcept {});
            return self_anchor_;
        }
    }

    lib::Observer<CwxPath> owner_;
    lib::Owner<CwxPath> owner_anchor_;
    lib::Owner<Derived> self_anchor_;
    lib::Observer<UserLeaseState<Derived>> lease_state_;
};

// 文字列IDには、DのStringIdミックスインのバイト単位のハッシュと辞書順を使う。
template <typename Tag>
struct StringIdBase {
    // これらのIDはnullを区別するUserとの受け渡しに使う。 記述子
    // のコピーやID→取得→設定の往復でも、同じ不変の確保領域を維持する。
    // D版の全ての文字列IDと同じく、空文字列でもnullとの区別を保持する。
    using Storage = NullableString;
    Storage id{};

    StringIdBase() = default;
    explicit StringIdBase(std::string_view value) : id(value) {}
    explicit StringIdBase(const String& value) : id(value) {}
    explicit StringIdBase(const NullableString& value)
        requires std::is_same_v<Storage, NullableString> : id(value) {}
    explicit StringIdBase(std::nullptr_t)
        requires std::is_same_v<Storage, NullableString> : id(nullptr) {}
    explicit StringIdBase(const char* value)
        requires std::is_same_v<Storage, NullableString> : id(value) {}

    [[nodiscard]] operator String() const { return id; }
    [[nodiscard]] operator std::string_view() const noexcept { return id; }
    [[nodiscard]] std::size_t toHash() const noexcept {
        std::size_t hash = 0;
        for (const unsigned char value : id) hash = (hash * 9U) + value;
        return hash;
    }
    [[nodiscard]] String toString() const { return id; }

    friend bool operator==(const StringIdBase& lhs,
                           const StringIdBase& rhs) noexcept {
        return lhs.id == rhs.id;
    }
    friend bool operator!=(const StringIdBase& lhs,
                           const StringIdBase& rhs) noexcept {
        return !(lhs == rhs);
    }
    friend bool operator<(const StringIdBase& lhs,
                          const StringIdBase& rhs) noexcept {
        return lhs.id < rhs.id;
    }
};

#define CWX_STRING_ID(NAME)                                                     \
    struct NAME : StringIdBase<NAME> {                                          \
        using StringIdBase<NAME>::StringIdBase;                                 \
        NAME() = default;                                                       \
    }

CWX_STRING_ID(FlagId);
CWX_STRING_ID(StepId);
CWX_STRING_ID(VariantId);
CWX_STRING_ID(CouponId);
CWX_STRING_ID(GossipId);
CWX_STRING_ID(CompleteStampId);
CWX_STRING_ID(KeyCodeId);
CWX_STRING_ID(CellNameId);
CWX_STRING_ID(CardGroupId);
CWX_STRING_ID(StartId);

#undef CWX_STRING_ID

// 数値IDはDのulongを明示的に表し、ホストのlong幅に依存しない。
template <typename Tag>
struct NumericIdBase {
    std::uint64_t id{};

    constexpr NumericIdBase() = default;
    constexpr explicit NumericIdBase(std::uint64_t value) : id(value) {}
    constexpr operator std::uint64_t() const noexcept { return id; }
    [[nodiscard]] constexpr std::size_t toHash() const noexcept {
        return std::hash<std::uint64_t>{}(id);
    }
    [[nodiscard]] String toString() const { return std::to_string(id); }

    friend constexpr bool operator==(const NumericIdBase& lhs,
                                     const NumericIdBase& rhs) noexcept {
        return lhs.id == rhs.id;
    }
    friend constexpr bool operator!=(const NumericIdBase& lhs,
                                     const NumericIdBase& rhs) noexcept {
        return !(lhs == rhs);
    }
    friend constexpr bool operator<(const NumericIdBase& lhs,
                                    const NumericIdBase& rhs) noexcept {
        return lhs.id < rhs.id;
    }
};

#define CWX_NUMERIC_ID(NAME)                                                    \
    struct NAME : NumericIdBase<NAME> {                                         \
        using NumericIdBase<NAME>::NumericIdBase;                               \
        NAME() = default;                                                       \
    }

CWX_NUMERIC_ID(AreaId);
CWX_NUMERIC_ID(BattleId);
CWX_NUMERIC_ID(PackageId);
CWX_NUMERIC_ID(CastId);
CWX_NUMERIC_ID(SkillId);
CWX_NUMERIC_ID(ItemId);
CWX_NUMERIC_ID(BeastId);
CWX_NUMERIC_ID(InfoId);

#undef CWX_NUMERIC_ID

[[nodiscard]] FlagId toFlagId(std::string_view id);
[[nodiscard]] StepId toStepId(std::string_view id);
[[nodiscard]] VariantId toVariantId(std::string_view id);
// Dの文字列記述子ではnullとしての同一性を保つ。 型を厳密に制限し、既存のリテラ
// ル・std::stringからstring_viewへの呼び出しを曖昧にしない。
template <typename T> requires (std::is_same_v<std::remove_cvref_t<T>, NullableString> || std::is_same_v<std::remove_cvref_t<T>, std::nullptr_t>)
[[nodiscard]] FlagId toFlagId(T&& value) { return FlagId(NullableString(std::forward<T>(value))); }
template <typename T> requires (std::is_same_v<std::remove_cvref_t<T>, NullableString> || std::is_same_v<std::remove_cvref_t<T>, std::nullptr_t>)
[[nodiscard]] StepId toStepId(T&& value) { return StepId(NullableString(std::forward<T>(value))); }
template <typename T> requires (std::is_same_v<std::remove_cvref_t<T>, NullableString> || std::is_same_v<std::remove_cvref_t<T>, std::nullptr_t>)
[[nodiscard]] VariantId toVariantId(T&& value) { return VariantId(NullableString(std::forward<T>(value))); }
[[nodiscard]] AreaId toAreaId(std::uint64_t id);
[[nodiscard]] BattleId toBattleId(std::uint64_t id);
[[nodiscard]] PackageId toPackageId(std::uint64_t id);
[[nodiscard]] CastId toCastId(std::uint64_t id);
[[nodiscard]] SkillId toSkillId(std::uint64_t id);
[[nodiscard]] ItemId toItemId(std::uint64_t id);
[[nodiscard]] BeastId toBeastId(std::uint64_t id);
[[nodiscard]] InfoId toInfoId(std::uint64_t id);
[[nodiscard]] CouponId toCouponId(std::string_view id);
[[nodiscard]] GossipId toGossipId(std::string_view id);
[[nodiscard]] CompleteStampId toCompleteStampId(std::string_view id);
[[nodiscard]] KeyCodeId toKeyCodeId(std::string_view id);
[[nodiscard]] CellNameId toCellNameId(std::string_view id);
[[nodiscard]] CardGroupId toCardGroupId(std::string_view id);
[[nodiscard]] StartId toStartId(std::string_view id);

/// ファイルパスのID。通常パスは/区切りで保存し、 バイナリ
/// ーの画像・音声パスはbinDataへ内容全体を保持する。
struct PathId {
    String id{};
    String binData{};

    PathId() = default;
    explicit PathId(std::string_view value) : id(value) {}
    PathId(std::string_view value, std::string_view binary)
        : id(value), binData(binary) {}

    [[nodiscard]] bool isBinData() const noexcept { return !binData.empty(); }
    [[nodiscard]] bool valid() const noexcept {
        return !id.empty() || !binData.empty();
    }
    [[nodiscard]] operator String() const;
    [[nodiscard]] std::size_t toHash() const noexcept;
    [[nodiscard]] String toString() const;

    friend bool operator==(const PathId& lhs, const PathId& rhs);
    friend bool operator!=(const PathId& lhs, const PathId& rhs) {
        return !(lhs == rhs);
    }
    friend bool operator<(const PathId& lhs, const PathId& rhs);
};

inline constexpr std::string_view BI_PATH_ID = "binaryimage://Binary:Image";
inline constexpr std::string_view BS_PATH_ID = "binarysound://Binary:Sound";

[[nodiscard]] PathId toPathId(std::string_view id);

class FlagUser;
class StepUser;
class VariantUser;
class AreaUser;
class BattleUser;
class PackageUser;
class PathUser;
class CastUser;
class SkillUser;
class ItemUser;
class BeastUser;
class InfoUser;
class CouponUser;
class GossipUser;
class CompleteStampUser;
class KeyCodeUser;
class CellNameUser;
class CardGroupUser;
class StartUser;

template <typename ID>
struct IDToUser;

template <>
struct IDToUser<FlagId> { using type = FlagUser; };
template <>
struct IDToUser<StepId> { using type = StepUser; };
template <>
struct IDToUser<VariantId> { using type = VariantUser; };
template <>
struct IDToUser<AreaId> { using type = AreaUser; };
template <>
struct IDToUser<BattleId> { using type = BattleUser; };
template <>
struct IDToUser<PackageId> { using type = PackageUser; };
template <>
struct IDToUser<PathId> { using type = PathUser; };
template <>
struct IDToUser<CastId> { using type = CastUser; };
template <>
struct IDToUser<SkillId> { using type = SkillUser; };
template <>
struct IDToUser<ItemId> { using type = ItemUser; };
template <>
struct IDToUser<BeastId> { using type = BeastUser; };
template <>
struct IDToUser<InfoId> { using type = InfoUser; };
template <>
struct IDToUser<CouponId> { using type = CouponUser; };
template <>
struct IDToUser<GossipId> { using type = GossipUser; };
template <>
struct IDToUser<CompleteStampId> { using type = CompleteStampUser; };
template <>
struct IDToUser<KeyCodeId> { using type = KeyCodeUser; };
template <>
struct IDToUser<CellNameId> { using type = CellNameUser; };
template <>
struct IDToUser<CardGroupId> { using type = CardGroupUser; };
template <>
struct IDToUser<StartId> { using type = StartUser; };

template <typename ID>
using IDToUser_t = typename IDToUser<ID>::type;

class FlagUser : public UserBase<FlagUser, FlagId> {
public:
    explicit FlagUser(const lib::Owner<CwxPath>& owner = {});
    explicit FlagUser(CwxPath* owner);

    void id(const FlagId& value);
    void flag(NullableString value);
    template <typename T> requires std::is_same_v<std::remove_cvref_t<T>, String>
    void flag(T&& value) { flag(NullableString(std::forward<T>(value))); }
    void flag(const FlagId& value) { flag(value.id); }
    [[nodiscard]] NullableString flag() const;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter);
    void removeUseCounter();
    [[nodiscard]] bool change(const FlagId& value) override;
    void handleChange(lib::Callback<void(FlagId)> callback);

private:
    CounterOwner counter_;
    NullableString flag_;
    lib::Callback<void(FlagId)> handle_change_;
};

class StepUser : public UserBase<StepUser, StepId> {
public:
    explicit StepUser(const lib::Owner<CwxPath>& owner = {});
    explicit StepUser(CwxPath* owner);

    void id(const StepId& value);
    void step(NullableString value);
    template <typename T> requires std::is_same_v<std::remove_cvref_t<T>, String>
    void step(T&& value) { step(NullableString(std::forward<T>(value))); }
    void step(const StepId& value) { step(value.id); }
    [[nodiscard]] NullableString step() const;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter);
    void removeUseCounter();
    [[nodiscard]] bool change(const StepId& value) override;
    void handleChange(lib::Callback<void(StepId)> callback);

private:
    CounterOwner counter_;
    NullableString step_;
    lib::Callback<void(StepId)> handle_change_;
};

class VariantUser : public UserBase<VariantUser, VariantId> {
public:
    explicit VariantUser(const lib::Owner<CwxPath>& owner = {});
    explicit VariantUser(CwxPath* owner);

    void id(const VariantId& value);
    void variant(NullableString value);
    template <typename T> requires std::is_same_v<std::remove_cvref_t<T>, String>
    void variant(T&& value) { variant(NullableString(std::forward<T>(value))); }
    void variant(const VariantId& value) { variant(value.id); }
    [[nodiscard]] NullableString variant() const;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter);
    void removeUseCounter();
    [[nodiscard]] bool change(const VariantId& value) override;
    void handleChange(lib::Callback<void(VariantId)> callback);

private:
    CounterOwner counter_;
    NullableString variant_;
    lib::Callback<void(VariantId)> handle_change_;
};

class AreaUser : public UserBase<AreaUser, AreaId> {
public:
    explicit AreaUser(const lib::Owner<CwxPath>& owner = {},
                      bool callback = false);
    AreaUser(CwxPath* owner, bool callback = false);

    void id(const AreaId& value);
    void area(std::uint64_t value);
    void area(const AreaId& value) { area(value.id); }
    [[nodiscard]] std::uint64_t area() const noexcept;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter);
    void removeUseCounter();
    [[nodiscard]] bool change(const AreaId& value) override;
    void handleChange(lib::Callback<void(AreaId)> callback);

private:
    CounterOwner counter_;
    std::uint64_t id_{};
    bool callback_{};
    lib::Callback<void(AreaId)> handle_change_;
};

class BattleUser : public UserBase<BattleUser, BattleId> {
public:
    explicit BattleUser(const lib::Owner<CwxPath>& owner = {},
                        bool callback = false);
    BattleUser(CwxPath* owner, bool callback = false);

    void id(const BattleId& value);
    void battle(std::uint64_t value);
    void battle(const BattleId& value) { battle(value.id); }
    [[nodiscard]] std::uint64_t battle() const noexcept;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter);
    void removeUseCounter();
    [[nodiscard]] bool change(const BattleId& value) override;
    void handleChange(lib::Callback<void(BattleId)> callback);

private:
    CounterOwner counter_;
    std::uint64_t id_{};
    bool callback_{};
    lib::Callback<void(BattleId)> handle_change_;
};

class PackageUser : public UserBase<PackageUser, PackageId> {
public:
    explicit PackageUser(const lib::Owner<CwxPath>& owner = {});
    explicit PackageUser(CwxPath* owner);

    void id(const PackageId& value);
    void packages(std::uint64_t value);
    void packages(const PackageId& value) { packages(value.id); }
    [[nodiscard]] std::uint64_t packages() const noexcept;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter);
    void removeUseCounter();
    [[nodiscard]] bool change(const PackageId& value) override;
    void handleChange(lib::Callback<void(PackageId)> callback);

private:
    CounterOwner counter_;
    std::uint64_t id_{};
    lib::Callback<void(PackageId)> handle_change_;
};

class StartUser : public UserBase<StartUser, StartId> {
public:
    explicit StartUser(const lib::Owner<CwxPath>& owner = {});
    explicit StartUser(CwxPath* owner);
    void id(const StartId& value) { start(value.id); }
    void start(std::string_view value);
    [[nodiscard]] String start() const;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter);
    void removeUseCounter();
    [[nodiscard]] bool change(const StartId& value) override;
    void handleChange(lib::Callback<void(const String&)> callback);

private:
    CounterOwner counter_;
    String start_;
    lib::Callback<void(const String&)> handle_change_;
};

class PathUser : public UserBase<PathUser, PathId> {
public:
    explicit PathUser(const lib::Owner<CwxPath>& owner = {});
    explicit PathUser(CwxPath* owner);

    [[nodiscard]] bool id(const PathId& value);
    void path(std::string_view value);
    void path(const PathId& value) { path(String(value)); }
    [[nodiscard]] String path() const;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter);
    void removeUseCounter();
    [[nodiscard]] bool change(const PathId& value) override;
    [[nodiscard]] std::uint64_t pathRevision() const noexcept { return path_revision_; }

private:
    CounterOwner counter_;
    std::uint64_t path_revision_{};
    PathId path_;
};

class CastUser : public UserBase<CastUser, CastId> {
public:
    explicit CastUser(const lib::Owner<CwxPath>& owner = {});
    explicit CastUser(CwxPath* owner);

    void id(const CastId& value);
    void casts(std::uint64_t value);
    void casts(const CastId& value) { casts(value.id); }
    [[nodiscard]] std::uint64_t casts() const noexcept;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter);
    void removeUseCounter();
    [[nodiscard]] bool change(const CastId& value) override;

private:
    CounterOwner counter_;
    std::uint64_t id_{};
};

class SkillUser : public UserBase<SkillUser, SkillId> {
public:
    explicit SkillUser(const lib::Owner<CwxPath>& owner = {});
    explicit SkillUser(CwxPath* owner);

    void id(const SkillId& value);
    void skill(std::uint64_t value);
    void skill(const SkillId& value) { skill(value.id); }
    [[nodiscard]] std::uint64_t skill() const noexcept;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter);
    void removeUseCounter();
    [[nodiscard]] bool change(const SkillId& value) override;

private:
    CounterOwner counter_;
    std::uint64_t id_{};
};

class ItemUser : public UserBase<ItemUser, ItemId> {
public:
    explicit ItemUser(const lib::Owner<CwxPath>& owner = {});
    explicit ItemUser(CwxPath* owner);

    void id(const ItemId& value);
    void item(std::uint64_t value);
    void item(const ItemId& value) { item(value.id); }
    [[nodiscard]] std::uint64_t item() const noexcept;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter);
    void removeUseCounter();
    [[nodiscard]] bool change(const ItemId& value) override;

private:
    CounterOwner counter_;
    std::uint64_t id_{};
};

class BeastUser : public UserBase<BeastUser, BeastId> {
public:
    explicit BeastUser(const lib::Owner<CwxPath>& owner = {});
    explicit BeastUser(CwxPath* owner);

    void id(const BeastId& value);
    void beast(std::uint64_t value);
    void beast(const BeastId& value) { beast(value.id); }
    [[nodiscard]] std::uint64_t beast() const noexcept;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter);
    void removeUseCounter();
    [[nodiscard]] bool change(const BeastId& value) override;

private:
    CounterOwner counter_;
    std::uint64_t id_{};
};

class InfoUser : public UserBase<InfoUser, InfoId> {
public:
    explicit InfoUser(const lib::Owner<CwxPath>& owner = {});
    explicit InfoUser(CwxPath* owner);

    void id(const InfoId& value);
    void info(std::uint64_t value);
    void info(const InfoId& value) { info(value.id); }
    [[nodiscard]] std::uint64_t info() const noexcept;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter);
    void removeUseCounter();
    [[nodiscard]] bool change(const InfoId& value) override;

private:
    CounterOwner counter_;
    std::uint64_t id_{};
};

class CouponUser : public UserBase<CouponUser, CouponId> {
public:
    using FindIndex = lib::Callback<std::size_t(std::string_view)>;
    using FindIndexString = lib::Callback<std::size_t(String)>;
    using FindIndexReference = lib::Callback<std::size_t(const String&)>;

    explicit CouponUser(const lib::Owner<CwxPath>& owner = {},
                        bool callback = false, FindIndex findIndex = {});
    CouponUser(const lib::Owner<CwxPath>& owner, bool callback,
               FindIndexString findIndex);
    CouponUser(const lib::Owner<CwxPath>& owner, bool callback,
               FindIndexReference findIndex);
    CouponUser(CwxPath* owner, bool callback = false,
               FindIndex findIndex = {});
    CouponUser(CwxPath* owner, bool callback, FindIndexString findIndex);
    CouponUser(CwxPath* owner, bool callback, FindIndexReference findIndex);

    [[nodiscard]] lib::Owner<CwxPath> owner() const;
    void owner(const lib::Owner<CwxPath>& owner);
    void owner(CwxPath* owner);

    void coupon(std::string_view value);
    void coupon(const CouponId& value) { coupon(value.id); }
    [[nodiscard]] String coupon() const;
    ~CouponUser() override;
    void changed() override;
    // 変更用の状態を終了するとき、寿命が安全な索引ク
    // ロージャーを渡した呼び出し元だけが、CWX階層
    // をたどるための弱い読み取り専用の関連を残せる。
    void retireTextProducer(bool preserveNavigation = false,
                            bool preserveOwner = false) noexcept;
    // 内部トークンの編集結果は既に受け付け済み。クーポン全体の改名には 別のacceptedValue
    // 通知があるため、分岐ラベルのUserで混同しない。
    virtual void acceptedTextValue(const String&) {}
    [[nodiscard]] bool expandSPChars() const noexcept;
    void expandSPChars(bool value);
    [[nodiscard]] lib::Array<String> flagsInText() const;
    [[nodiscard]] lib::Array<String> stepsInText() const;
    [[nodiscard]] lib::Array<String> variantsInText() const;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter,
                       const lib::Owner<CwxPath>& counterOwner);
    void setUseCounter(const CounterOwner& counter, CwxPath* counterOwner);
    void removeUseCounter();
    [[nodiscard]] lib::Owner<CwxPath> ucOwner();
    [[nodiscard]] bool change(const CouponId& value) override;

private:
    void registerTextCounter(const CounterOwner& counter,
                             const lib::Owner<CwxPath>& owner);
    void unregisterTextCounter();
    void setOwnerInternal(const lib::Owner<CwxPath>& owner);

    CounterOwner counter_;
    lib::Observer<CwxPath> counter_owner_;
    lib::Owner<CwxPath> counter_owner_anchor_;
    lib::Owner<textholder::SimpleTextHolder> coupon_;
    lib::Array<lib::Owner<FlagUser>> flag_users_;
    lib::Array<lib::Owner<StepUser>> step_users_;
    lib::Array<lib::Owner<VariantUser>> variant_users_;
    bool expand_sp_chars_{};
    bool callback_{};
    bool preserve_retired_navigation_{};
    lib::Observer<CwxPath> retired_change_owner_;
};

class GossipUser : public UserBase<GossipUser, GossipId> {
public:
    explicit GossipUser(const lib::Owner<CwxPath>& owner = {},
                        bool callback = false);
    GossipUser(CwxPath* owner, bool callback = false);
    ~GossipUser() override;
    void retireTextProducer() noexcept;

    [[nodiscard]] lib::Owner<CwxPath> owner() const;
    void owner(const lib::Owner<CwxPath>& owner);
    void owner(CwxPath* owner);

    void gossip(std::string_view value);
    void gossip(const GossipId& value) { gossip(value.id); }
    [[nodiscard]] String gossip() const;
    [[nodiscard]] bool expandSPChars() const noexcept;
    void expandSPChars(bool value);
    [[nodiscard]] lib::Array<String> flagsInText() const;
    [[nodiscard]] lib::Array<String> stepsInText() const;
    [[nodiscard]] lib::Array<String> variantsInText() const;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter,
                       const lib::Owner<CwxPath>& counterOwner);
    void setUseCounter(const CounterOwner& counter, CwxPath* counterOwner);
    void removeUseCounter();
    [[nodiscard]] lib::Owner<CwxPath> ucOwner();
    [[nodiscard]] bool change(const GossipId& value) override;

private:
    void registerTextCounter(const CounterOwner& counter,
                             const lib::Owner<CwxPath>& owner);
    void unregisterTextCounter();
    void setOwnerInternal(const lib::Owner<CwxPath>& owner);

    CounterOwner counter_;
    lib::Observer<CwxPath> counter_owner_;
    lib::Owner<CwxPath> counter_owner_anchor_;
    lib::Owner<textholder::SimpleTextHolder> gossip_;
    lib::Array<lib::Owner<FlagUser>> flag_users_;
    lib::Array<lib::Owner<StepUser>> step_users_;
    lib::Array<lib::Owner<VariantUser>> variant_users_;
    bool expand_sp_chars_{};
    bool callback_{};
};

class CompleteStampUser
    : public UserBase<CompleteStampUser, CompleteStampId> {
public:
    explicit CompleteStampUser(const lib::Owner<CwxPath>& owner = {});
    explicit CompleteStampUser(CwxPath* owner);

    void completeStamp(std::string_view value);
    void completeStamp(const CompleteStampId& value) { completeStamp(value.id); }
    [[nodiscard]] String completeStamp() const;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter,
                       const lib::Owner<CwxPath>& counterOwner);
    void setUseCounter(const CounterOwner& counter, CwxPath* counterOwner);
    void setUseCounter(const CounterOwner& counter) {
        setUseCounter(counter, {});
    }
    void removeUseCounter();
    [[nodiscard]] lib::Owner<CwxPath> ucOwner();
    [[nodiscard]] bool change(const CompleteStampId& value) override;

private:
    CounterOwner counter_;
    lib::Observer<CwxPath> counter_owner_;
    lib::Owner<CwxPath> counter_owner_anchor_;
    String value_;
};

class KeyCodeUser : public UserBase<KeyCodeUser, KeyCodeId> {
public:
    explicit KeyCodeUser(const lib::Owner<CwxPath>& owner = {});
    explicit KeyCodeUser(CwxPath* owner);

    void keyCode(std::string_view value);
    void keyCode(const KeyCodeId& value) { keyCode(value.id); }
    [[nodiscard]] String keyCode() const;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter,
                       const lib::Owner<CwxPath>& counterOwner);
    void setUseCounter(const CounterOwner& counter, CwxPath* counterOwner);
    void setUseCounter(const CounterOwner& counter) { setUseCounter(counter, {}); }
    void removeUseCounter();
    [[nodiscard]] lib::Owner<CwxPath> ucOwner();
    [[nodiscard]] bool change(const KeyCodeId& value) override;

private:
    CounterOwner counter_;
    lib::Observer<CwxPath> counter_owner_;
    lib::Owner<CwxPath> counter_owner_anchor_;
    String value_;
};

class CellNameUser : public UserBase<CellNameUser, CellNameId> {
public:
    explicit CellNameUser(const lib::Owner<CwxPath>& owner = {});
    explicit CellNameUser(CwxPath* owner);

    void cellName(std::string_view value);
    void cellName(const CellNameId& value) { cellName(value.id); }
    [[nodiscard]] String cellName() const;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter,
                       const lib::Owner<CwxPath>& counterOwner);
    void setUseCounter(const CounterOwner& counter, CwxPath* counterOwner);
    void setUseCounter(const CounterOwner& counter) { setUseCounter(counter, {}); }
    void removeUseCounter();
    [[nodiscard]] lib::Owner<CwxPath> ucOwner();
    [[nodiscard]] bool change(const CellNameId& value) override;

private:
    CounterOwner counter_;
    lib::Observer<CwxPath> counter_owner_;
    lib::Owner<CwxPath> counter_owner_anchor_;
    String value_;
};

class CardGroupUser : public UserBase<CardGroupUser, CardGroupId> {
public:
    explicit CardGroupUser(const lib::Owner<CwxPath>& owner = {});
    explicit CardGroupUser(CwxPath* owner);

    void cardGroup(std::string_view value);
    void cardGroup(const CardGroupId& value) { cardGroup(value.id); }
    [[nodiscard]] String cardGroup() const;
    [[nodiscard]] CounterOwner useCounter() const;
    void setUseCounter(const CounterOwner& counter,
                       const lib::Owner<CwxPath>& counterOwner);
    void setUseCounter(const CounterOwner& counter, CwxPath* counterOwner);
    void setUseCounter(const CounterOwner& counter) { setUseCounter(counter, {}); }
    void removeUseCounter();
    [[nodiscard]] lib::Owner<CwxPath> ucOwner();
    [[nodiscard]] bool change(const CardGroupId& value) override;

private:
    CounterOwner counter_;
    lib::Observer<CwxPath> counter_owner_;
    lib::Owner<CwxPath> counter_owner_anchor_;
    String value_;
};

/// IDを、現在そのIDを持つ具体的なUserの弱参照集合へ対応
/// 付ける表。 Dの連想配列と同様に、空になった集合は削除する。
template <typename K, typename U>
class UCCont {
public:
    using UserOwner = lib::Owner<U>;
    using UserObserver = ::cwxeditor::cwx::usecounter::UserObserver<U>;
    // DのGCはUserとCounterの双方向参照を保持で
    // きるが、ネイティブでは弱参照で循環を避ける。 通知中だ
    // け有効なUserを保持し、弱参照は小さな配列で管理する
    // 。 生のオブジェクトの同一性で比較して、所有しないshared_ptr
    // でもDと同じ識別を保ち、 unordered_set
    // の途中でハッシュが変わる問題を避ける。
    using UserSet = detail::WeakUserSet<U>;
    using UserSetOwner = lib::Owner<UserSet>;
    using Map = std::unordered_map<K, UserSetOwner, detail::IdHash<K>>;

    UCCont() = default;
    UCCont(const UCCont& other) {
        for (const auto& [key, users] : other.cont_)
            cont_.emplace(key, std::make_shared<UserSet>(*users));
    }
    UCCont& operator=(const UCCont& other) {
        if (this != &other) { UCCont copy(other); cont_.swap(copy.cont_); }
        return *this;
    }
    UCCont(UCCont&&) noexcept = default;
    UCCont& operator=(UCCont&&) noexcept = default;

    [[nodiscard]] std::uint32_t get(const K& key) const {
        const auto found = cont_.find(key);
        if (found == cont_.end()) return 0;
        std::uint32_t result = 0;
        for (const auto& observer : *found->second) {
            if (UserSet::isNullReference(observer) || !observer.expired()) ++result;
        }
        return result;
    }

    [[nodiscard]] lib::Array<K> keys() const {
        lib::Array<K> result;
        result.reserve(cont_.size());
        for (const auto& [key, users] : cont_) {
            // D版は共有集合が直接空にされた場合も、残っているキーを列挙する。
            if (users->empty()) {
                result.push_back(key);
                continue;
            }
            bool live = false;
            for (const auto& observer : *users) {
                if (UserSet::isNullReference(observer) || !observer.expired()) {
                    live = true;
                    break;
                }
            }
            if (live) result.push_back(key);
        }
        return result;
    }

    [[nodiscard]] lib::Array<UserOwner> values(const K& key) const {
        lib::Array<UserOwner> result;
        const auto found = cont_.find(key);
        if (found == cont_.end()) return result;
        result.reserve(found->second->size());
        for (const auto& observer : *found->second) {
            if (UserSet::isNullReference(observer)) result.push_back({});
            else if (auto user = observer.lock()) result.push_back(std::move(user));
        }
        return result;
    }

    // Dは見つからなければ独立した空集合を返し、マップに新しいキーは作らない。
    [[nodiscard]] UserSetOwner valueSet(const K& key) {
        const auto found = cont_.find(key);
        return found == cont_.end() ? std::make_shared<UserSet>() : found->second;
    }
    [[nodiscard]] lib::Owner<const UserSet> valueSet(const K& key) const {
        const auto found = cont_.find(key);
        return found == cont_.end() ? std::make_shared<UserSet>() : found->second;
    }

    void add(const K& key, const UserOwner& user) {
        ensureSet(key).insert(user ? UserObserver(user) : UserObserver{});
    }

    void remove(const K& key, const UserOwner& user) {
        // Dの連想配列はキーがなければnullのクラス参照を返
        // し、set.remove(user)はそこで失敗す
        // る。 at()で同じ箇所を確定的な例外にする。 既
        // 存の集合に対象メンバーがないだけなら、何もしない。
        const auto set = cont_.at(key);
        set->erase(user ? UserObserver(user) : UserObserver{});
        if (set->empty()) cont_.erase(key);
    }

    void retireNativeUser(const UserOwner& user) noexcept {
        const UserObserver observed = user ? UserObserver(user) : UserObserver{};
        for (auto entry = cont_.begin(); entry != cont_.end();) {
            if (entry->second) entry->second->erase(observed);
            if (!entry->second || entry->second->empty()) entry = cont_.erase(entry);
            else ++entry;
        }
    }

    void change(const K& oldKey, const K& newKey, bool duplicate = false) {
        // Dはキーを値で受け取る。ネイティブのコールバックは呼び出し元の変数や
        // マップ項目を置き換えられるため、呼び出す前に両方の入力を保持する。
        const K oldValue = oldKey;
        const K newValue = newKey;
        if (oldValue == newValue) return;
        const auto found = cont_.find(oldValue);
        if (found == cont_.end()) return;
        // Dのduplicateはコンパイラーのデバッグ診断にだけ使う。
        // ネイティブの汎用コンテナーは表示書式をホストへ任せ、任意のIDに
        // 文字列化を要求せず、 どちらのフラグでもDと同じ統合規則を使う。
        (void)duplicate;
        auto oldSet = std::make_shared<UserSet>();
        const auto destinationAtStart = cont_.find(newValue);
        auto newSet = destinationAtStart != cont_.end()
                          ? destinationAtStart->second
                          : std::make_shared<UserSet>();
        // DではUCCont.changeが旧集合を反復中に、Expression.text
        // の通知から 元と移動先のHashSetが変更・破棄され
        // ることがあり、その場合の反復動作は未規定。 ネイティ
        // ブではUserを保持してマップの反映を遅らせ、 通知
        // による走査の無効化や、破棄済みの弱参照の残存を防ぐ。
        const auto users = values(oldValue);
        [[maybe_unused]] const auto ownerAnchors = detail::retainUserOwners(users);
        std::unordered_map<const CwxPath*, std::uint64_t> callbackRevisionsAtStart;
        for (const auto& user : users) {
            if (!user) throw std::invalid_argument("Null counter user");
            const auto owner = user->owner();
            if (owner) {
                if (const auto callback = std::dynamic_pointer_cast<TChgCallback<K>>(owner)) {
                    callbackRevisionsAtStart.try_emplace(
                        owner.get(), callback->useCounterRevision());
                }
            }
        }
        for (const auto& user : users) {
            const bool migrated = user->change(newValue);
            if (migrated) {
                // Expressionの改名通知は文字列を再解
                // 析し、元のUserを全カウンターから外して新
                // しいUserを登録する。 所有者が登録を作り
                // 直した後は、移動済みの古いスナップショットの
                // Userを戻さない。 新しいUserは下で通
                // 知が作った集合から統合する。 最初の通知が再
                // 解析した後、同じ所有者の後続Userも古いと
                // 分かるよう、変更開始時の版番号と比較する。
                const auto callbackOwner = user->owner();
                if (callbackOwner) {
                    const auto callback =
                        std::dynamic_pointer_cast<TChgCallback<K>>(callbackOwner);
                    const auto revision =
                        callbackRevisionsAtStart.find(callbackOwner.get());
                    if (callback && revision != callbackRevisionsAtStart.end() &&
                        callback->useCounterRevision() != revision->second) {
                        continue;
                    }
                }
            }
            (migrated ? newSet : oldSet)->insert(UserObserver(user));
        }
        // DのHashSetは移動したUserを強く保
        // 持するが、ネイティブのカウンターは弱参照を保
        // 存する。 Expressionの通知は文字列
        // を再解析してUserを作り直すため、通知前の
        // 移動先をそのまま反映すると 新しい参照が失わ
        // れる。Expressionの版が変わった場合
        // だけ、再作成された参照を残す。 Dで未規定と
        // なる反復中の変更をそのまま再現せず、書き換え
        // 後の式の全参照を保持する仕様とする。 移動先
        // は通知中に消去・再作成される場合もあるため、
        // 開始時のマップへの所属だけでは判定しない。
        if (!callbackRevisionsAtStart.empty()) {
            const auto callbackSet = cont_.find(newValue);
            if (callbackSet != cont_.end()) {
                for (const auto& observer : *callbackSet->second) {
                    const auto callbackUser = observer.lock();
                    if (!callbackUser) continue;
                    const auto callbackOwner = callbackUser->owner();
                    if (!callbackOwner) continue;
                    const auto callback =
                        std::dynamic_pointer_cast<TChgCallback<K>>(callbackOwner);
                    const auto revision = callbackRevisionsAtStart.find(callbackOwner.get());
                    if (callback && revision != callbackRevisionsAtStart.end() &&
                        callback->useCounterRevision() != revision->second) {
                        newSet->insert(observer);
                    }
                }
            }
        }
        if (newSet->empty()) cont_.erase(newValue);
        else cont_[newValue] = std::move(newSet);
        if (oldSet->empty()) cont_.erase(oldValue);
        else cont_[oldValue] = std::move(oldSet);
    }

private:
    UserSet& ensureSet(const K& key) {
        auto found = cont_.find(key);
        if (found == cont_.end())
            found = cont_.emplace(key, std::make_shared<UserSet>()).first;
        return *found->second;
    }
    Map cont_;
};

/// IDコンテナー一式。子は変更を反映するが、
/// Undo・編集の処理では 主集合の件数に
/// 含めずに参照を移すため、子を選択できる。
class SingleUseCounter : public std::enable_shared_from_this<SingleUseCounter> {
public:
    SingleUseCounter();
    explicit SingleUseCounter(bool useChild);

    [[nodiscard]] lib::Owner<SingleUseCounter> sub();

    template <typename ID>
    void change(const ID& oldId, const ID& newId, bool duplicate = false) {
        container<ID>().change(oldId, newId, duplicate);
        if (child_) child_->change(oldId, newId, duplicate);
    }

    template <typename ID>
    [[nodiscard]] std::uint32_t get(const ID& id) const {
        return container<ID>().get(id);
    }

    template <typename ID>
    [[nodiscard]] lib::Array<ID> keys() const {
        return container<ID>().keys();
    }

    template <typename ID>
    [[nodiscard]] lib::Array<lib::Owner<IDToUser_t<ID>>> values(
        const ID& id) const {
        return container<ID>().values(id);
    }

    template <typename ID>
    [[nodiscard]] auto valueSet(const ID& id) {
        return container<ID>().valueSet(id);
    }

    template <typename ID>
    [[nodiscard]] auto valueSet(const ID& id) const {
        return container<ID>().valueSet(id);
    }

    template <typename ID>
    void add(const ID& id, const lib::Owner<IDToUser_t<ID>>& user) {
        container<ID>().add(id, user);
    }

    template <typename ID>
    void remove(const ID& id, const lib::Owner<IDToUser_t<ID>>& user) {
        if (detail::NativeCounterRetirement::active()) {
            retireNativeUser<ID>(user);
            return;
        }
        container<ID>().remove(id, user);
    }

    template <typename ID>
    void retireNativeUser(const lib::Owner<IDToUser_t<ID>>& user) noexcept {
        container<ID>().retireNativeUser(user);
    }

private:
    template <typename ID>
    [[nodiscard]] auto& container() {
        using U = IDToUser_t<ID>;
        if constexpr (std::is_same_v<ID, FlagId>) return flag_;
        else if constexpr (std::is_same_v<ID, StepId>) return step_;
        else if constexpr (std::is_same_v<ID, VariantId>) return variant_;
        else if constexpr (std::is_same_v<ID, AreaId>) return area_;
        else if constexpr (std::is_same_v<ID, BattleId>) return battle_;
        else if constexpr (std::is_same_v<ID, PackageId>) return package_;
        else if constexpr (std::is_same_v<ID, PathId>) return path_;
        else if constexpr (std::is_same_v<ID, CastId>) return cast_;
        else if constexpr (std::is_same_v<ID, SkillId>) return skill_;
        else if constexpr (std::is_same_v<ID, ItemId>) return item_;
        else if constexpr (std::is_same_v<ID, BeastId>) return beast_;
        else if constexpr (std::is_same_v<ID, InfoId>) return info_;
        else if constexpr (std::is_same_v<ID, CouponId>) return coupon_;
        else if constexpr (std::is_same_v<ID, GossipId>) return gossip_;
        else if constexpr (std::is_same_v<ID, CompleteStampId>) return complete_stamp_;
        else if constexpr (std::is_same_v<ID, KeyCodeId>) return key_code_;
        else if constexpr (std::is_same_v<ID, CellNameId>) return cell_name_;
        else if constexpr (std::is_same_v<ID, CardGroupId>) return card_group_;
        else if constexpr (std::is_same_v<ID, StartId>) return start_;
        else {
            static_assert(!sizeof(U), "unsupported UseCounter ID");
        }
    }

    template <typename ID>
    [[nodiscard]] const auto& container() const {
        return const_cast<SingleUseCounter*>(this)->container<ID>();
    }

    UCCont<FlagId, FlagUser> flag_;
    UCCont<StepId, StepUser> step_;
    UCCont<VariantId, VariantUser> variant_;
    UCCont<AreaId, AreaUser> area_;
    UCCont<BattleId, BattleUser> battle_;
    UCCont<PackageId, PackageUser> package_;
    UCCont<PathId, PathUser> path_;
    UCCont<CastId, CastUser> cast_;
    UCCont<SkillId, SkillUser> skill_;
    UCCont<ItemId, ItemUser> item_;
    UCCont<BeastId, BeastUser> beast_;
    UCCont<InfoId, InfoUser> info_;
    UCCont<CouponId, CouponUser> coupon_;
    UCCont<GossipId, GossipUser> gossip_;
    UCCont<CompleteStampId, CompleteStampUser> complete_stamp_;
    UCCont<KeyCodeId, KeyCodeUser> key_code_;
    UCCont<CellNameId, CellNameUser> cell_name_;
    UCCont<CardGroupId, CardGroupUser> card_group_;
    UCCont<StartId, StartUser> start_;
    lib::Owner<SingleUseCounter> child_;
};

/// Editorの資源ツリーで使う、グローバル・ローカルのカウンターの組。
class UseCounter : public std::enable_shared_from_this<UseCounter> {
public:
    explicit UseCounter(const lib::Owner<CwxPath>& owner = {});
    explicit UseCounter(CwxPath* owner);
    explicit UseCounter(const lib::Owner<CwxPath>& owner,
                        const CounterOwner& global);
    UseCounter(CwxPath* owner, const CounterOwner& global);
    ~UseCounter() = default;

    UseCounter(const UseCounter&) = delete;
    UseCounter& operator=(const UseCounter&) = delete;

    [[nodiscard]] lib::Owner<CwxPath> owner() const;
    [[nodiscard]] CounterOwner global() const;
    [[nodiscard]] CounterOwner sub() const;

    template <typename ID>
    [[nodiscard]] bool hasID(const ID& id) const {
        return local_ && localIds<ID>().find(id) != localIds<ID>().end();
    }

    template <typename ID>
    void createID(ID id) {
        // Dは公開関数と実装関数の両方の入口で事前条件を検査
        // する。 ローカル・グローバルの登録を変える前に、違
        // 反をネイティブの例外へ変換し、IDの値を保持する。
        if (local_ && hasID(id)) throw std::logic_error("local ID already exists");
        createIDImpl(id, true);
    }

    template <typename ID>
    void createIDImpl(ID id, bool propagate) {
        if (local_ && hasID(id)) throw std::logic_error("local ID already exists");
        if (local_) {
            auto& ids = localIds<ID>();
            ids.insert(id);
            const auto users = global_->values(id);
            for (const auto& user : users) {
                // Dはスナップショットの全メンバーを、nullも含めて参照する。
                if (!user) throw std::invalid_argument("null ID user");
                const auto current = user->useCounter();
                if (current.get() != this) continue;
                global_->remove(id, user);
                local_->add(id, user);
            }
        }
        if (propagate && sub_) sub_->createID(id);
    }

    template <typename ID>
    void deleteID(ID id) {
        // Dの事前条件と同じく、変更前にローカルIDであることを要求する。
        // グローバルカウンターにはこの制限を設けず、子へも変更を伝える。
        if (local_ && !hasID(id)) throw std::logic_error("local ID does not exist");
        deleteIDImpl(id, true);
    }

    template <typename ID>
    void deleteIDImpl(ID id, bool propagate) {
        if (local_ && !hasID(id)) throw std::logic_error("local ID does not exist");
        if (local_) {
            auto& ids = localIds<ID>();
            ids.erase(id);
            const auto users = local_->values(id);
            for (const auto& user : users) {
                if (!user) throw std::invalid_argument("null ID user");
                const auto current = user->useCounter();
                if (current.get() != this) continue;
                local_->remove(id, user);
                global_->add(id, user);
            }
            // Dは子IDの削除前に、全登録が移動済みであることを検査す
            // る。 一部の登録が残るローカルIDのまま処理を続けない。
            if (local_->get(id) != 0) throw std::logic_error("deleted local ID still has users");
        }
        if (propagate && sub_) sub_->deleteID(id);
    }

    template <typename ID>
    void change(ID oldId, ID newId, bool duplicate = false) {
        if (oldId == newId) return;
        const auto counter = selfOwner();
        // Dのthis/_owner/_globalUCのクラス参照は、通知中
        // も保持される。 このスコープではネイティブの弱い所有関係を保持するが
        // 、 下の実際の再接続で使う各通知先のucOwnerの値は固定しない。
        [[maybe_unused]] const auto ownerAnchor = owner();
        const auto globalCounterAnchor = global();
        [[maybe_unused]] const auto globalOwnerAnchor = globalCounterAnchor->owner();
        if (local_ && hasID(oldId)) {
            // Dのアサーションと同じ段階で、ネイティブの失敗に変換する。
            if (local_->get(newId) != 0) throw std::logic_error("replacement local ID already has users");
            using U = IDToUser_t<ID>;
            const auto users = local_->values(oldId);
            [[maybe_unused]] const auto ownerAnchors = detail::retainUserOwners(users);
            struct CallbackState {
                std::uint64_t bindingRevision{};
                std::size_t snapshotUsers{};
                bool observedBindingRevision{};
                bool superseded{};
            };
            struct CallbackEntry {
                lib::Owner<TChgCallback<ID>> callback;
                const CwxPath* owner{};
            };
            std::unordered_map<const CwxPath*, CallbackState> callbackStates;
            std::vector<CallbackEntry> callbacks;
            std::vector<lib::Owner<U>> ordinary;
            callbacks.reserve(users.size());
            ordinary.reserve(users.size());

            for (const auto& user : users) {
                if (!user) throw std::invalid_argument("null local ID user");
                const auto current = user->useCounter();
                if (current && current.get() != this)
                    throw std::logic_error("local ID user belongs to another counter");
                const auto callbackOwner = user->owner();
                auto callback = std::dynamic_pointer_cast<TChgCallback<ID>>(callbackOwner);
                if (callback) {
                    auto& state = callbackStates[callbackOwner.get()];
                    ++state.snapshotUsers;
                    if (state.observedBindingRevision &&
                        callback->useCounterBindingRevision() != state.bindingRevision) {
                        state.superseded = true;
                    }
                    // 通常の改名ではUserごとの処理を繰り返す。
                    // ネイティブの再入保護の拡張として、Dの無条件
                    // の再接続より明示的な関連付け変更を優先する。
                    if (state.superseded) continue;
                    // Dは所有者が重複していてもUserごとに通知するため
                    // 、 切り離し・再接続の呼び出しをまとめて省略しない。
                    callback->removeUseCounter();
                    const auto detachedBindingRevision =
                        callback->useCounterBindingRevision();
                    state.bindingRevision = detachedBindingRevision;
                    state.observedBindingRevision = true;
                    callbacks.push_back({callback, callbackOwner.get()});
                    (void)user->change(newId);
                    if (callback->useCounterBindingRevision() != detachedBindingRevision)
                        state.superseded = true;
                } else {
                    user->removeUseCounter();
                    ordinary.push_back(user);
                    if constexpr (std::is_same_v<ID, FlagId> ||
                                  std::is_same_v<ID, StepId>) {
                        // Contentの通常のFlag/StepのUser
                        // は、書き込み前の通知後に新しい値を受け付ける
                        // 。 通知中に旧IDを再登録させず、ローカル
                        // の改名処理が後でこのUserを再接続する。
                        detail::OrdinaryFlagStepChangeScope changeScope(user->owner());
                        (void)user->change(newId);
                    } else {
                        (void)user->change(newId);
                    }
                }
            }
            if (local_->get(oldId) != 0) throw std::logic_error("old local ID still has users");
            deleteIDImpl(oldId, false);
            createIDImpl(newId, false);
            std::size_t supersededRegisteredUsers = 0;
            for (const auto& registered : local_->values(newId)) {
                if (!registered)
                    throw std::logic_error("new local ID has a null user before reattachment");
                const auto registeredOwner = registered->owner();
                const auto state = callbackStates.find(registeredOwner.get());
                if (state == callbackStates.end() || !state->second.superseded)
                    throw std::logic_error("new local ID already has users before reattachment");
                ++supersededRegisteredUsers;
            }
            for (const auto& entry : callbacks) {
                const auto state = callbackStates.find(entry.owner);
                if (state != callbackStates.end() && state->second.superseded) continue;
                const auto callbackOwner = entry.callback->ucOwner();
                entry.callback->setUseCounter(counter, callbackOwner);
            }
            for (const auto& user : ordinary) {
                if constexpr (requires(U& value) { value.ucOwner(); }) {
                    const auto userOwner = user->ucOwner();
                    user->setUseCounter(counter, userOwner);
                } else {
                    user->setUseCounter(counter);
                }
            }
            std::size_t expectedUsers = ordinary.size() + supersededRegisteredUsers;
            for (const auto& entry : callbackStates) {
                if (!entry.second.superseded)
                    expectedUsers += entry.second.snapshotUsers;
            }
            if (local_->get(newId) != expectedUsers)
                throw std::logic_error("local ID user count changed during replacement");
        } else if (local_) {
            local_->change(oldId, newId, duplicate);
        } else {
            global_->change(oldId, newId, duplicate);
        }
        if (sub_) sub_->change(oldId, newId, duplicate);
    }

    template <typename ID, typename U>
    static void replaceID(const ID& id, const lib::Owner<U>& user) {
        if (user) replaceID(id, *user);
    }

    template <typename ID, typename U>
        requires std::is_base_of_v<IDToUser_t<ID>, std::remove_cv_t<U>>
    static void replaceID(const ID& id, U& user) {
        static_assert(std::is_base_of_v<IDToUser_t<ID>, std::remove_cv_t<U>>,
                      "ID and user types must match");
        if constexpr (std::is_same_v<ID, FlagId>) user.flag(id.id);
        else if constexpr (std::is_same_v<ID, StepId>) user.step(id.id);
        else if constexpr (std::is_same_v<ID, VariantId>) user.variant(id.id);
        else if constexpr (std::is_same_v<ID, AreaId>) user.area(id.id);
        else if constexpr (std::is_same_v<ID, BattleId>) user.battle(id.id);
        else if constexpr (std::is_same_v<ID, PackageId>) user.packages(id.id);
        else if constexpr (std::is_same_v<ID, PathId>) user.path(String(id));
        else if constexpr (std::is_same_v<ID, CastId>) user.casts(id.id);
        else if constexpr (std::is_same_v<ID, SkillId>) user.skill(id.id);
        else if constexpr (std::is_same_v<ID, ItemId>) user.item(id.id);
        else if constexpr (std::is_same_v<ID, BeastId>) user.beast(id.id);
        else if constexpr (std::is_same_v<ID, InfoId>) user.info(id.id);
        else if constexpr (std::is_same_v<ID, CouponId>) user.coupon(id.id);
        else if constexpr (std::is_same_v<ID, GossipId>) user.gossip(id.id);
        else if constexpr (std::is_same_v<ID, CompleteStampId>) user.completeStamp(id.id);
        else if constexpr (std::is_same_v<ID, KeyCodeId>) user.keyCode(id.id);
        else if constexpr (std::is_same_v<ID, CellNameId>) user.cellName(id.id);
        else if constexpr (std::is_same_v<ID, CardGroupId>) user.cardGroup(id.id);
    }

    template <typename ID>
    [[nodiscard]] std::uint32_t get(const ID& id) const {
        return local_ ? local_->get(id) : global_->get(id);
    }

    template <typename ID>
    [[nodiscard]] lib::Array<ID> keys() const {
        return local_ ? local_->template keys<ID>()
                      : global_->template keys<ID>();
    }

    template <typename ID>
    [[nodiscard]] lib::Array<lib::Owner<IDToUser_t<ID>>> values(
        const ID& id) const {
        return local_ ? local_->values(id) : global_->values(id);
    }

    template <typename ID>
    [[nodiscard]] auto valueSet(const ID& id) {
        return local_ ? local_->valueSet(id) : global_->valueSet(id);
    }

    template <typename ID>
    [[nodiscard]] auto valueSet(const ID& id) const {
        return local_ ? std::as_const(*local_).valueSet(id)
                      : std::as_const(*global_).valueSet(id);
    }

    template <typename ID>
    void add(const ID& id, const lib::Owner<IDToUser_t<ID>>& user) {
        // Dはキー付きのUser集合にもnullのクラス参照を追加する。
        if (local_ && hasID(id)) local_->add(id, user);
        else global_->add(id, user);
    }

    template <typename ID>
    void remove(const ID& id, const lib::Owner<IDToUser_t<ID>>& user) {
        if (detail::NativeCounterRetirement::active()) {
            // 値の直接編集でローカル・グローバルの接続先が変わる場合もある。 ネイテ
            // ィブの終了処理では他のUserへ触れず、この識別子を両方から削除する。
            if (local_) local_->retireNativeUser<ID>(user);
            if (global_) global_->retireNativeUser<ID>(user);
            return;
        }
        if (local_ && hasID(id)) local_->remove(id, user);
        else global_->remove(id, user);
    }

private:
    UseCounter(const lib::Owner<CwxPath>& owner, UseCounter* globalCounter,
               const lib::Owner<SingleUseCounter>& global,
               const lib::Owner<SingleUseCounter>& local, bool createSub,
               CounterOwner globalOwner = {});

    template <typename ID>
    [[nodiscard]] auto& localIds() {
        if constexpr (std::is_same_v<ID, FlagId>) return local_flag_;
        else if constexpr (std::is_same_v<ID, StepId>) return local_step_;
        else if constexpr (std::is_same_v<ID, VariantId>) return local_variant_;
        else if constexpr (std::is_same_v<ID, AreaId>) return local_area_;
        else if constexpr (std::is_same_v<ID, BattleId>) return local_battle_;
        else if constexpr (std::is_same_v<ID, PackageId>) return local_package_;
        else if constexpr (std::is_same_v<ID, PathId>) return local_path_;
        else if constexpr (std::is_same_v<ID, CastId>) return local_cast_;
        else if constexpr (std::is_same_v<ID, SkillId>) return local_skill_;
        else if constexpr (std::is_same_v<ID, ItemId>) return local_item_;
        else if constexpr (std::is_same_v<ID, BeastId>) return local_beast_;
        else if constexpr (std::is_same_v<ID, InfoId>) return local_info_;
        else if constexpr (std::is_same_v<ID, CouponId>) return local_coupon_;
        else if constexpr (std::is_same_v<ID, GossipId>) return local_gossip_;
        else if constexpr (std::is_same_v<ID, CompleteStampId>) return local_complete_stamp_;
        else if constexpr (std::is_same_v<ID, KeyCodeId>) return local_key_code_;
        else if constexpr (std::is_same_v<ID, CellNameId>) return local_cell_name_;
        else if constexpr (std::is_same_v<ID, CardGroupId>) return local_card_group_;
        else if constexpr (std::is_same_v<ID, StartId>) return local_start_;
        else {
            static_assert(!sizeof(ID), "unsupported local ID");
        }
    }

    template <typename ID>
    [[nodiscard]] const auto& localIds() const {
        return const_cast<UseCounter*>(this)->localIds<ID>();
    }

    [[nodiscard]] CounterOwner selfOwner();

    lib::Observer<CwxPath> owner_;
    // 生の所有者を受け取るコンストラクターは、所有しない
    // 制御ブロックを保持する。 外部が所有するパスを削除
    // せず、カウンターの寿命中は弱参照を取得可能にする。
    lib::Owner<CwxPath> owner_anchor_;
    UseCounter* global_counter_{};
    CounterOwner global_owner_;
    lib::Owner<SingleUseCounter> global_;
    lib::Owner<SingleUseCounter> local_;
    lib::Owner<UseCounter> sub_;

    std::unordered_set<FlagId, detail::IdHash<FlagId>> local_flag_;
    std::unordered_set<StepId, detail::IdHash<StepId>> local_step_;
    std::unordered_set<VariantId, detail::IdHash<VariantId>> local_variant_;
    std::unordered_set<AreaId, detail::IdHash<AreaId>> local_area_;
    std::unordered_set<BattleId, detail::IdHash<BattleId>> local_battle_;
    std::unordered_set<PackageId, detail::IdHash<PackageId>> local_package_;
    std::unordered_set<PathId, detail::IdHash<PathId>> local_path_;
    std::unordered_set<CastId, detail::IdHash<CastId>> local_cast_;
    std::unordered_set<SkillId, detail::IdHash<SkillId>> local_skill_;
    std::unordered_set<ItemId, detail::IdHash<ItemId>> local_item_;
    std::unordered_set<BeastId, detail::IdHash<BeastId>> local_beast_;
    std::unordered_set<InfoId, detail::IdHash<InfoId>> local_info_;
    std::unordered_set<CouponId, detail::IdHash<CouponId>> local_coupon_;
    std::unordered_set<GossipId, detail::IdHash<GossipId>> local_gossip_;
    std::unordered_set<CompleteStampId, detail::IdHash<CompleteStampId>> local_complete_stamp_;
    std::unordered_set<KeyCodeId, detail::IdHash<KeyCodeId>> local_key_code_;
    std::unordered_set<CellNameId, detail::IdHash<CellNameId>> local_cell_name_;
    std::unordered_set<CardGroupId, detail::IdHash<CardGroupId>> local_card_group_;
    std::unordered_set<StartId, detail::IdHash<StartId>> local_start_;
};

/// 名前・クーポンの置き換えに使う資源の範囲を返す
/// 。 実行時の型の印で区別し、カウンターの組を
/// area/cardの翻訳単位へ依存させない。
[[nodiscard]] lib::Owner<CwxPath> renameInfo(
    const lib::Owner<CwxPath>& value);
[[nodiscard]] lib::Owner<CwxPath> renameInfo(CwxPath* value);

template <typename ID>
[[nodiscard]] ID toID(std::string_view value) {
    return ToID<ID>::convert(value);
}

template <>
struct ToID<FlagId> { static FlagId convert(std::string_view value) { return toFlagId(value); } };
template <>
struct ToID<StepId> { static StepId convert(std::string_view value) { return toStepId(value); } };
template <>
struct ToID<VariantId> { static VariantId convert(std::string_view value) { return toVariantId(value); } };
template <>
struct ToID<CouponId> { static CouponId convert(std::string_view value) { return toCouponId(value); } };
template <>
struct ToID<GossipId> { static GossipId convert(std::string_view value) { return toGossipId(value); } };
template <>
struct ToID<CompleteStampId> { static CompleteStampId convert(std::string_view value) { return toCompleteStampId(value); } };
template <>
struct ToID<KeyCodeId> { static KeyCodeId convert(std::string_view value) { return toKeyCodeId(value); } };
template <>
struct ToID<CellNameId> { static CellNameId convert(std::string_view value) { return toCellNameId(value); } };
template <>
struct ToID<CardGroupId> { static CardGroupId convert(std::string_view value) { return toCardGroupId(value); } };
template <>
struct ToID<StartId> { static StartId convert(std::string_view value) { return toStartId(value); } };

}  // namespace cwxeditor::cwx::usecounter

namespace std {
#define CWX_HASH_ID(NAME)                                                        \
    template <>                                                                  \
    struct hash<::cwxeditor::cwx::usecounter::NAME> {                            \
        size_t operator()(const ::cwxeditor::cwx::usecounter::NAME& value)      \
            const noexcept {                                                    \
            return value.toHash();                                              \
        }                                                                        \
    }

CWX_HASH_ID(FlagId);
CWX_HASH_ID(StepId);
CWX_HASH_ID(VariantId);
CWX_HASH_ID(AreaId);
CWX_HASH_ID(BattleId);
CWX_HASH_ID(PackageId);
CWX_HASH_ID(PathId);
CWX_HASH_ID(CastId);
CWX_HASH_ID(SkillId);
CWX_HASH_ID(ItemId);
CWX_HASH_ID(BeastId);
CWX_HASH_ID(InfoId);
CWX_HASH_ID(CouponId);
CWX_HASH_ID(GossipId);
CWX_HASH_ID(CompleteStampId);
CWX_HASH_ID(KeyCodeId);
CWX_HASH_ID(CellNameId);
CWX_HASH_ID(CardGroupId);
CWX_HASH_ID(StartId);

#undef CWX_HASH_ID
}  // namespace std
