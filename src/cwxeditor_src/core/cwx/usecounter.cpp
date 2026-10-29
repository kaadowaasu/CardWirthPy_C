#include "cwxeditor_src/core/cwx/usecounter.hpp"

#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/textholder.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

#include <cctype>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace cwxeditor::cwx::usecounter {
namespace {

[[nodiscard]] bool startsWith(std::string_view value,
                              std::string_view prefix) noexcept {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] String normalizeForCompare(std::string_view value,
                                          const bool insensitive) {
    String result;
    result.reserve(value.size());
    for (const char character : value) {
        const char slash =
#ifdef _WIN32
            character == '\\' ? '/' :
#endif
            character;
        result.push_back(slash);
    }
    return insensitive ? enumutils::simpleLowerCase(result, true) : result;
}

template <typename ID>
[[nodiscard]] bool callbackAllows(const lib::Owner<CwxPath>& owner,
                                  const ID& oldValue, const ID& newValue,
                                  const bool enabled = true) {
    if (!enabled) return true;
    if (const auto text = std::dynamic_pointer_cast<background::TextCell>(owner)) {
        if constexpr (std::is_same_v<ID, FlagId>)
            return text->renameFlagInText(oldValue.id, newValue.id);
        else if constexpr (std::is_same_v<ID, StepId>)
            return text->renameStepInText(oldValue.id, newValue.id);
        else if constexpr (std::is_same_v<ID, VariantId>)
            return text->renameVariantInText(oldValue.id, newValue.id);
        else if constexpr (std::is_same_v<ID, PathId>)
            return text->renameFontInText(String(oldValue), String(newValue));
    }
    const auto callback =
        std::dynamic_pointer_cast<TChgCallback<ID>>(owner);
    return !callback || callback->changeCallback(oldValue, newValue);
}

template <typename ID, typename U, typename Valid>
void updateCounter(CounterOwner& slot,
                   const CounterOwner& desired, const ID& oldValue,
                   const ID& newValue, const lib::Owner<U>& user,
                   Valid valid) {
    const auto previous = slot;
    if (previous.get() == desired.get()) return;
    if (previous && valid(oldValue)) previous->remove(oldValue, user);
    if (desired && valid(newValue)) desired->add(newValue, user);
    slot = desired;
}

template <typename ID, typename U, typename Valid>
void clearCounter(CounterOwner& slot, const ID& value,
                  const lib::Owner<U>& user, Valid valid) {
    if (const auto previous = slot; previous &&
        (valid(value) || detail::NativeCounterRetirement::active())) {
        previous->remove(value, user);
    }
    slot.reset();
}

template <typename U>
[[nodiscard]] lib::Owner<CwxPath> userPathOwner(
    const lib::Owner<U>& user) {
    return user ? std::static_pointer_cast<CwxPath>(user)
                : lib::Owner<CwxPath>{};
}

template <typename Id, typename U>
void registerTextUsers(const CounterOwner& counter,
                       const lib::Owner<CwxPath>& textOwner,
                       const lib::Array<String>& flags,
                       const lib::Array<String>& steps,
                       const lib::Array<String>& variants,
                       lib::Array<lib::Owner<FlagUser>>& flagUsers,
                       lib::Array<lib::Owner<StepUser>>& stepUsers,
                       lib::Array<lib::Owner<VariantUser>>& variantUsers) {
    (void)sizeof(Id);
    (void)sizeof(U);
    flagUsers.reserve(flags.size());
    for (const auto& value : flags) {
        auto user = std::make_shared<FlagUser>(textOwner);
        user->flag(value);
        user->setUseCounter(counter);
        flagUsers.push_back(std::move(user));
    }
    stepUsers.reserve(steps.size());
    for (const auto& value : steps) {
        auto user = std::make_shared<StepUser>(textOwner);
        user->step(value);
        user->setUseCounter(counter);
        stepUsers.push_back(std::move(user));
    }
    variantUsers.reserve(variants.size());
    for (const auto& value : variants) {
        auto user = std::make_shared<VariantUser>(textOwner);
        user->variant(value);
        user->setUseCounter(counter);
        variantUsers.push_back(std::move(user));
    }
}

template <typename U>
void clearTextUsers(lib::Array<lib::Owner<U>>& users) {
    for (const auto& user : users) {
        if (user) user->removeUseCounter();
    }
    users.clear();
}

}  // namespace

namespace detail {

[[nodiscard]] bool wildcardEqual(const std::string_view value,
                                 const std::string_view pattern,
                                 const bool caseInsensitive) {
    // utilsのcfnmatchはファイル名比較であり、グロブで
    // はない。二つのプラットフォームの区切り文字を同等に扱い、その
    // プラットフォームのファイル名の大文字小文字の規則を適用する。
    return normalizeForCompare(value, caseInsensitive) ==
           normalizeForCompare(pattern, caseInsensitive);
}

[[nodiscard]] int lexicalCompare(const std::string_view lhs,
                                 const std::string_view rhs,
                                 const bool caseInsensitive) {
    const auto leftPath = utils::encodePath(String(lhs));
    const auto rightPath = utils::encodePath(String(rhs));
    // Dのstd.string.icmpは単純なcasefold済み文字列の辞書順とは異なる。
    return caseInsensitive ? enumutils::compareDStringIgnoreCase(leftPath, rightPath)
                           : leftPath.compare(rightPath);
}

[[nodiscard]] String pathString(const PathId& value) {
    if (value.isBinData()) return value.binData;
    String result = value.id;
#ifdef _WIN32
    std::replace(result.begin(), result.end(), '/', '\\');
#endif
    return result;
}

}  // namespace detail

FlagId toFlagId(const std::string_view id) { return FlagId(id); }
StepId toStepId(const std::string_view id) { return StepId(id); }
VariantId toVariantId(const std::string_view id) { return VariantId(id); }
AreaId toAreaId(const std::uint64_t id) { return AreaId(id); }
BattleId toBattleId(const std::uint64_t id) { return BattleId(id); }
PackageId toPackageId(const std::uint64_t id) { return PackageId(id); }
CastId toCastId(const std::uint64_t id) { return CastId(id); }
SkillId toSkillId(const std::uint64_t id) { return SkillId(id); }
ItemId toItemId(const std::uint64_t id) { return ItemId(id); }
BeastId toBeastId(const std::uint64_t id) { return BeastId(id); }
InfoId toInfoId(const std::uint64_t id) { return InfoId(id); }
CouponId toCouponId(const std::string_view id) { return CouponId(id); }
GossipId toGossipId(const std::string_view id) { return GossipId(id); }
CompleteStampId toCompleteStampId(const std::string_view id) {
    return CompleteStampId(id);
}
KeyCodeId toKeyCodeId(const std::string_view id) { return KeyCodeId(id); }
CellNameId toCellNameId(const std::string_view id) { return CellNameId(id); }
CardGroupId toCardGroupId(const std::string_view id) {
    return CardGroupId(id);
}
StartId toStartId(const std::string_view id) { return StartId(id); }

PathId::operator String() const { return detail::pathString(*this); }

std::size_t PathId::toHash() const noexcept {
    const bool insensitive = utils::fncmp("A", "a") == 0;
    // Dは未正規化の表記をハッシュするため、Windowsで同じファイル名
    // のパスに異なるハッシュを生成し得る。等価判定と同じ区切り・大文字小
    // 文字の形式でハッシュし、順序なしコンテナーのキーの不変条件を守る。
    // 不正UTF-8も等価判定と同じ置換規則で正規化し、noexceptから終了しない。
    const String value = isBinData() ? binData
                                     : normalizeForCompare(id, insensitive);
    std::size_t hash = 0;
    for (const unsigned char character : value) hash = hash * 9U + character;
    return hash;
}

String PathId::toString() const {
    String result = "PathId {";
    if (isBinData()) {
        result += "BinaryData, hash: ";
        result += std::to_string(toHash());
    } else {
        result += id;
    }
    result += '}';
    return result;
}

bool operator==(const PathId& lhs, const PathId& rhs) {
    if (lhs.isBinData() || rhs.isBinData()) {
        return lhs.binData == rhs.binData;
    }
    return utils::cfnmatch(lhs.id, rhs.id);
}

bool operator<(const PathId& lhs, const PathId& rhs) {
    if (lhs.isBinData() != rhs.isBinData()) return lhs.isBinData();
    if (lhs.isBinData()) {
        // 承認済み仕様差: D版の接頭辞比較での範囲外例外を再現せず、長さを含む全順序を保つ。
        if (lhs.binData < rhs.binData) return true;
        return false;
    }
    return detail::lexicalCompare(lhs.id, rhs.id, utils::fncmp("A", "a") == 0) < 0;
}

PathId toPathId(const std::string_view value) {
    if (startsWith(value, "binaryimage://")) {
        return PathId(BI_PATH_ID, value);
    }
    if (startsWith(value, "binarysound://")) {
        return PathId(BS_PATH_ID, value);
    }
    String normalized(value);
#ifdef _WIN32
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
#endif
    return PathId(normalized);
}

FlagUser::FlagUser(const lib::Owner<CwxPath>& owner)
    : UserBase(owner) {}
FlagUser::FlagUser(CwxPath* owner) : UserBase(owner) {}

void FlagUser::id(const FlagId& value) {
    const auto replacement = value; // DはID構造体を値で渡す。
    const auto keepAlive = selfOwner();
    const auto ownerAnchor = owner();
    if (!callbackAllows(ownerAnchor, toFlagId(flag_), replacement)) return;
    flag(replacement.id);
}

void FlagUser::flag(NullableString incoming) {
    // 記述子は、コールバックをまたいで不変の入力とnullの同一性
    // を保持する。非nullの空値もDのカウンターの対象になる。
    const auto user = selfOwner();
    [[maybe_unused]] const auto ownerAnchor = owner();
    if (flag_ != incoming) changed();
    const auto previous = counter_;
    if (previous && !flag_.isNull()) previous->remove(toFlagId(flag_), user);
    const auto desired = counter_;
    if (desired && !incoming.isNull()) desired->add(toFlagId(incoming), user);
    flag_ = incoming;
}

NullableString FlagUser::flag() const { return flag_; }
CounterOwner FlagUser::useCounter() const { return counter_; }

void FlagUser::setUseCounter(const CounterOwner& counter) {
    const auto previous = counter_;
    if (previous.get() == counter.get()) return;
    const auto user = selfOwner();
    if (previous && !flag_.isNull()) previous->remove(toFlagId(flag_), user);
    if (counter && !flag_.isNull()) counter->add(toFlagId(flag_), user);
    counter_ = counter;
}

void FlagUser::removeUseCounter() {
    clearCounter(counter_, toFlagId(flag_), selfOwner(),
                 [](const FlagId& value) { return !value.id.isNull(); });
}

bool FlagUser::change(const FlagId& value) {
    const auto replacement = value;
    const auto keepAlive = selfOwner();
    [[maybe_unused]] const auto ownerAnchor = owner();
    if (flag_ != replacement.id) changed();
    if (!callbackAllows(owner(), toFlagId(flag_), replacement)) return false;
    // 追加のネイティブの受理値フックはモデルのキャッシュを同期する。そ
    // の前にDの拒否判定、その後にフィールド代入を行う順序を維持する。
    if (handle_change_) handle_change_(replacement);
    flag_ = replacement.id;
    return true;
}

void FlagUser::handleChange(lib::Callback<void(FlagId)> callback) {
    handle_change_ = std::move(callback);
}

StepUser::StepUser(const lib::Owner<CwxPath>& owner)
    : UserBase(owner) {}
StepUser::StepUser(CwxPath* owner) : UserBase(owner) {}

void StepUser::id(const StepId& value) {
    const auto replacement = value;
    const auto keepAlive = selfOwner();
    const auto ownerAnchor = owner();
    if (!callbackAllows(ownerAnchor, toStepId(step_), replacement)) return;
    step(replacement.id);
}

void StepUser::step(NullableString incoming) {
    // 同期コールバックをまたいで保持される、Dのnullを許容する文字列に合わせる。
    const auto user = selfOwner();
    [[maybe_unused]] const auto ownerAnchor = owner();
    if (step_ != incoming) changed();
    const auto previous = counter_;
    if (previous && !step_.isNull()) previous->remove(toStepId(step_), user);
    if (const auto desired = counter_; desired && !incoming.isNull()) {
        desired->add(toStepId(incoming), user);
    }
    step_ = incoming;
}

NullableString StepUser::step() const { return step_; }
CounterOwner StepUser::useCounter() const { return counter_; }

void StepUser::setUseCounter(const CounterOwner& counter) {
    const auto previous = counter_;
    if (previous.get() == counter.get()) return;
    const auto user = selfOwner();
    if (previous && !step_.isNull()) previous->remove(toStepId(step_), user);
    if (counter && !step_.isNull()) counter->add(toStepId(step_), user);
    counter_ = counter;
}

void StepUser::removeUseCounter() {
    clearCounter(counter_, toStepId(step_), selfOwner(),
                 [](const StepId& value) { return !value.id.isNull(); });
}

bool StepUser::change(const StepId& value) {
    const auto replacement = value;
    const auto keepAlive = selfOwner();
    [[maybe_unused]] const auto ownerAnchor = owner();
    if (step_ != replacement.id) changed();
    if (!callbackAllows(owner(), toStepId(step_), replacement)) return false;
    // FlagUser::changeと同様に、ネイティブのキャッシュ同期はDの拒否判定の後に行う。
    if (handle_change_) handle_change_(replacement);
    step_ = replacement.id;
    return true;
}

void StepUser::handleChange(lib::Callback<void(StepId)> callback) {
    handle_change_ = std::move(callback);
}

VariantUser::VariantUser(const lib::Owner<CwxPath>& owner)
    : UserBase(owner) {}
VariantUser::VariantUser(CwxPath* owner) : UserBase(owner) {}

void VariantUser::id(const VariantId& value) {
    const auto replacement = value;
    const auto keepAlive = selfOwner();
    const auto ownerAnchor = owner();
    if (!callbackAllows(ownerAnchor, toVariantId(variant_), replacement)) return;
    variant(replacement.id);
}

void VariantUser::variant(NullableString incoming) {
    // 同期コールバックをまたいで保持される、Dのnullを許容する文字列に合わせる。
    const auto user = selfOwner();
    [[maybe_unused]] const auto ownerAnchor = owner();
    if (variant_ != incoming) changed();
    const auto previous = counter_;
    if (previous && !variant_.isNull()) {
        previous->remove(toVariantId(variant_), user);
    }
    if (const auto desired = counter_; desired && !incoming.isNull()) {
        desired->add(toVariantId(incoming), user);
    }
    variant_ = incoming;
}

NullableString VariantUser::variant() const { return variant_; }
CounterOwner VariantUser::useCounter() const { return counter_; }

void VariantUser::setUseCounter(const CounterOwner& counter) {
    const auto previous = counter_;
    if (previous.get() == counter.get()) return;
    const auto user = selfOwner();
    if (previous && !variant_.isNull()) {
        previous->remove(toVariantId(variant_), user);
    }
    if (counter && !variant_.isNull()) {
        counter->add(toVariantId(variant_), user);
    }
    counter_ = counter;
}

void VariantUser::removeUseCounter() {
    clearCounter(counter_, toVariantId(variant_), selfOwner(),
                 [](const VariantId& value) { return !value.id.isNull(); });
}

bool VariantUser::change(const VariantId& value) {
    const auto replacement = value;
    const auto keepAlive = selfOwner();
    [[maybe_unused]] const auto ownerAnchor = owner();
    if (variant_ != replacement.id) changed();
    if (!callbackAllows(owner(), toVariantId(variant_), replacement)) return false;
    // FlagUser::changeと同様に、ネイティブのキャッシュ同期はDの拒否判定の後に行う。
    if (handle_change_) handle_change_(replacement);
    variant_ = replacement.id;
    return true;
}

void VariantUser::handleChange(lib::Callback<void(VariantId)> callback) {
    handle_change_ = std::move(callback);
}

AreaUser::AreaUser(const lib::Owner<CwxPath>& owner, const bool callback)
    : UserBase(owner), callback_(callback) {}
AreaUser::AreaUser(CwxPath* owner, const bool callback)
    : UserBase(owner), callback_(callback) {}

void AreaUser::id(const AreaId& value) { area(value.id); }

void AreaUser::area(const std::uint64_t value) {
    if (id_ == value) return;
    changed();
    if (!callbackAllows(owner(), toAreaId(id_), toAreaId(value), callback_)) {
        return;
    }
    const auto user = selfOwner();
    const auto previous = counter_;
    if (previous && id_ > 0) previous->remove(toAreaId(id_), user);
    if (const auto desired = counter_; desired && value > 0) {
        desired->add(toAreaId(value), user);
    }
    id_ = value;
    acceptedNumericValue(id_);
}

std::uint64_t AreaUser::area() const noexcept { return id_; }
CounterOwner AreaUser::useCounter() const { return counter_; }

void AreaUser::setUseCounter(const CounterOwner& counter) {
    const auto previous = counter_;
    if (previous.get() == counter.get()) return;
    const auto user = selfOwner();
    if (previous && id_ > 0) previous->remove(toAreaId(id_), user);
    if (counter && id_ > 0) counter->add(toAreaId(id_), user);
    counter_ = counter;
}

void AreaUser::removeUseCounter() {
    clearCounter(counter_, toAreaId(id_), selfOwner(),
                 [](const AreaId& value) { return value.id > 0; });
}

bool AreaUser::change(const AreaId& input) {
    // D版の値渡しと同じく、通知中に呼出し元のIDが変わっても元の値を使う。
    const auto value = input;
    if (id_ == value.id) return true;
    changed();
    if (!callbackAllows(owner(), toAreaId(id_), value, callback_)) return false;
    if (handle_change_) handle_change_(value);
    id_ = value.id;
    acceptedNumericValue(id_);
    return true;
}

void AreaUser::handleChange(lib::Callback<void(AreaId)> callback) {
    handle_change_ = std::move(callback);
}

BattleUser::BattleUser(const lib::Owner<CwxPath>& owner, const bool callback)
    : UserBase(owner), callback_(callback) {}
BattleUser::BattleUser(CwxPath* owner, const bool callback)
    : UserBase(owner), callback_(callback) {}

void BattleUser::id(const BattleId& value) { battle(value.id); }

void BattleUser::battle(const std::uint64_t value) {
    if (id_ == value) return;
    changed();
    if (!callbackAllows(owner(), toBattleId(id_), toBattleId(value),
                        callback_)) {
        return;
    }
    const auto user = selfOwner();
    const auto previous = counter_;
    if (previous && id_ > 0) previous->remove(toBattleId(id_), user);
    if (const auto desired = counter_; desired && value > 0) {
        desired->add(toBattleId(value), user);
    }
    id_ = value;
    acceptedNumericValue(id_);
}

std::uint64_t BattleUser::battle() const noexcept { return id_; }
CounterOwner BattleUser::useCounter() const { return counter_; }

void BattleUser::setUseCounter(const CounterOwner& counter) {
    const auto previous = counter_;
    if (previous.get() == counter.get()) return;
    const auto user = selfOwner();
    if (previous && id_ > 0) previous->remove(toBattleId(id_), user);
    if (counter && id_ > 0) counter->add(toBattleId(id_), user);
    counter_ = counter;
}

void BattleUser::removeUseCounter() {
    clearCounter(counter_, toBattleId(id_), selfOwner(),
                 [](const BattleId& value) { return value.id > 0; });
}

bool BattleUser::change(const BattleId& input) {
    const auto value = input;
    if (id_ == value.id) return true;
    changed();
    if (!callbackAllows(owner(), toBattleId(id_), value, callback_)) return false;
    if (handle_change_) handle_change_(value);
    id_ = value.id;
    acceptedNumericValue(id_);
    return true;
}

void BattleUser::handleChange(lib::Callback<void(BattleId)> callback) {
    handle_change_ = std::move(callback);
}

#define CWX_DEFINE_NUMERIC_USER(CLASS, ID, PROPERTY, TO_ID)                     \
    CLASS::CLASS(const lib::Owner<CwxPath>& owner) : UserBase(owner) {}         \
    CLASS::CLASS(CwxPath* owner) : UserBase(owner) {}                            \
    void CLASS::id(const ID& value) { PROPERTY(value.id); }                     \
    void CLASS::PROPERTY(const std::uint64_t value) {                           \
        if (id_ != value) changed();                                             \
        const auto user = selfOwner();                                           \
        const auto previous = counter_;                                          \
        if (previous && id_ > 0) previous->remove(TO_ID(id_), user);             \
        if (const auto desired = counter_; desired && value > 0)                  \
            desired->add(TO_ID(value), user);                                   \
        id_ = value;                                                             \
        acceptedNumericValue(id_);                                                \
    }                                                                            \
    std::uint64_t CLASS::PROPERTY() const noexcept { return id_; }               \
    CounterOwner CLASS::useCounter() const { return counter_; }                  \
    void CLASS::setUseCounter(const CounterOwner& counter) {                     \
        const auto previous = counter_;                                          \
        if (previous.get() == counter.get()) return;                             \
        const auto user = selfOwner();                                           \
        if (previous && id_ > 0) previous->remove(TO_ID(id_), user);             \
        if (counter && id_ > 0) counter->add(TO_ID(id_), user);                  \
        counter_ = counter;                                                       \
    }                                                                            \
    void CLASS::removeUseCounter() {                                             \
        clearCounter(counter_, TO_ID(id_), selfOwner(),                          \
                     [](const ID& value) { return value.id > 0; });              \
    }                                                                            \
    bool CLASS::change(const ID& input) {                                        \
        const auto value = input;                                               \
        if (id_ != value.id) changed();                                          \
        id_ = value.id;                                                          \
        acceptedNumericValue(id_);                                                \
        return true;                                                              \
    }

PackageUser::PackageUser(const lib::Owner<CwxPath>& owner) : UserBase(owner) {}
PackageUser::PackageUser(CwxPath* owner) : UserBase(owner) {}
void PackageUser::id(const PackageId& value) { packages(value.id); }
void PackageUser::packages(const std::uint64_t value) {
    if (id_ != value) changed();
    const auto user = selfOwner();
    const auto previous = counter_;
    if (previous && id_ > 0) previous->remove(toPackageId(id_), user);
    if (const auto desired = counter_; desired && value > 0) desired->add(toPackageId(value), user);
    id_ = value;
}
std::uint64_t PackageUser::packages() const noexcept { return id_; }
CounterOwner PackageUser::useCounter() const { return counter_; }
void PackageUser::setUseCounter(const CounterOwner& counter) {
    const auto previous = counter_;
    if (previous.get() == counter.get()) return;
    const auto user = selfOwner();
    if (previous && id_ > 0) previous->remove(toPackageId(id_), user);
    if (counter && id_ > 0) counter->add(toPackageId(id_), user);
    counter_ = counter;
}
void PackageUser::removeUseCounter() {
    clearCounter(counter_, toPackageId(id_), selfOwner(),
                 [](const PackageId& value) { return value.id > 0; });
}
bool PackageUser::change(const PackageId& input) {
    const auto value = input;
    if (id_ != value.id) changed();
    if (handle_change_) handle_change_(value);
    id_ = value.id;
    return true;
}
void PackageUser::handleChange(lib::Callback<void(PackageId)> callback) {
    handle_change_ = std::move(callback);
}

StartUser::StartUser(const lib::Owner<CwxPath>& owner) : UserBase(owner) {}
StartUser::StartUser(CwxPath* owner) : UserBase(owner) {}
void StartUser::start(const std::string_view value) {
    if (start_ != value) changed();
    const auto user = selfOwner();
    const auto previous = counter_;
    if (previous && !start_.empty()) previous->remove(toStartId(start_), user);
    if (const auto desired = counter_; desired && !value.empty()) desired->add(toStartId(value), user);
    start_.assign(value);
}
String StartUser::start() const { return start_; }
CounterOwner StartUser::useCounter() const { return counter_; }
void StartUser::setUseCounter(const CounterOwner& counter) {
    const auto previous = counter_;
    if (previous.get() == counter.get()) return;
    const auto user = selfOwner();
    if (previous && !start_.empty()) previous->remove(toStartId(start_), user);
    if (counter && !start_.empty()) counter->add(toStartId(start_), user);
    counter_ = counter;
}
void StartUser::removeUseCounter() {
    clearCounter(counter_, toStartId(start_), selfOwner(),
                 [](const StartId& value) { return !value.id.empty(); });
}
bool StartUser::change(const StartId& value) {
    if (start_ == value.id) return true;
    changed();
    if (handle_change_) handle_change_(value.id);
    start_ = value.id;
    return true;
}
void StartUser::handleChange(lib::Callback<void(const String&)> callback) {
    handle_change_ = std::move(callback);
}

CWX_DEFINE_NUMERIC_USER(CastUser, CastId, casts, toCastId)
CWX_DEFINE_NUMERIC_USER(SkillUser, SkillId, skill, toSkillId)
CWX_DEFINE_NUMERIC_USER(ItemUser, ItemId, item, toItemId)
CWX_DEFINE_NUMERIC_USER(BeastUser, BeastId, beast, toBeastId)
CWX_DEFINE_NUMERIC_USER(InfoUser, InfoId, info, toInfoId)

#undef CWX_DEFINE_NUMERIC_USER

PathUser::PathUser(const lib::Owner<CwxPath>& owner) : UserBase(owner) {}
PathUser::PathUser(CwxPath* owner) : UserBase(owner) {}

bool PathUser::id(const PathId& input) {
    const auto value = input;
    if (!callbackAllows(owner(), path_, value)) return false;
    path(String(value));
    return true;
}

void PathUser::path(const std::string_view value) {
    const auto next = toPathId(value);
    if (path_ != next) changed();
    const auto user = selfOwner();
    const auto previous = counter_;
    if (previous && path_.valid()) previous->remove(path_, user);
    if (const auto desired = counter_; desired && !value.empty()) {
        desired->add(next, user);
    }
    path_ = next;
    ++path_revision_;
}

String PathUser::path() const { return String(path_); }
CounterOwner PathUser::useCounter() const { return counter_; }

void PathUser::setUseCounter(const CounterOwner& counter) {
    const auto previous = counter_;
    if (previous.get() == counter.get()) return;
    const auto user = selfOwner();
    if (previous && path_.valid()) previous->remove(path_, user);
    if (counter && path_.valid()) counter->add(path_, user);
    counter_ = counter;
}

void PathUser::removeUseCounter() {
    clearCounter(counter_, path_, selfOwner(),
                 [](const PathId& value) { return value.valid(); });
}

bool PathUser::change(const PathId& input) {
    const auto value = input;
    if (path_ != value) changed();
    if (!callbackAllows(owner(), path_, value)) return false;
    path_ = value;
    ++path_revision_;
    return true;
}

CouponUser::CouponUser(const lib::Owner<CwxPath>& owner, const bool callback,
                       FindIndex findIndex)
    : UserBase(owner), callback_(callback) {
    coupon_ = std::make_shared<textholder::SimpleTextHolder>(
        detail::nonOwning(static_cast<CwxPath*>(this)),
        textholder::TextHolderType::Coupon, "coupon", std::move(findIndex));
    coupon_->changeHandler([this] { acceptedTextValue(coupon()); changed(); });
    setOwnerInternal(owner);
}

CouponUser::CouponUser(const lib::Owner<CwxPath>& owner, const bool callback,
                       FindIndexString findIndex)
    : CouponUser(owner, callback,
                 FindIndex([findIndex = std::move(findIndex)](
                               std::string_view value) {
                     return findIndex ? findIndex(String(value)) : 0U;
                 })) {}

CouponUser::CouponUser(const lib::Owner<CwxPath>& owner, const bool callback,
                       FindIndexReference findIndex)
    : CouponUser(owner, callback,
                 FindIndex([findIndex = std::move(findIndex)](
                               std::string_view value) {
                     return findIndex ? findIndex(String(value)) : 0U;
                 })) {}

CouponUser::CouponUser(CwxPath* owner, const bool callback, FindIndex findIndex)
    : CouponUser(detail::nonOwning(owner), callback, std::move(findIndex)) {
    UserBase<CouponUser, CouponId>::owner(owner);
    if (coupon_) coupon_->owner(detail::nonOwning(owner));
}

CouponUser::CouponUser(CwxPath* owner, const bool callback,
                       FindIndexString findIndex)
    : CouponUser(detail::nonOwning(owner), callback, std::move(findIndex)) {
    UserBase<CouponUser, CouponId>::owner(owner);
    if (coupon_) coupon_->owner(detail::nonOwning(owner));
}

CouponUser::CouponUser(CwxPath* owner, const bool callback,
                       FindIndexReference findIndex)
    : CouponUser(detail::nonOwning(owner), callback, std::move(findIndex)) {
    UserBase<CouponUser, CouponId>::owner(owner);
    if (coupon_) coupon_->owner(detail::nonOwning(owner));
}

CouponUser::~CouponUser() { retireTextProducer(); }
void CouponUser::changed() {
    if (preserve_retired_navigation_) {
        if (const auto owner = retired_change_owner_.lock()) owner->changed();
        return;
    }
    UserBase<CouponUser, CouponId>::changed();
}

void CouponUser::retireTextProducer(const bool preserveNavigation,
                                    const bool preserveOwner) noexcept {
    preserve_retired_navigation_ = preserve_retired_navigation_ || preserveNavigation;
    if (preserveNavigation && retired_change_owner_.expired())
        retired_change_owner_ = owner();
    if (coupon_) coupon_->retire(preserve_retired_navigation_);
    if (preserve_retired_navigation_ && !preserveOwner) {
        // 最終的な破棄では所有元の弱参照を無効にする。通常の名前付き利用者の置換
        // では弱参照を維持し、利用権によってDの参照関係を保持できるようにする。
        UserBase<CouponUser, CouponId>::owner(static_cast<CwxPath*>(nullptr));
    }
}

lib::Owner<CwxPath> CouponUser::owner() const {
    return UserBase<CouponUser, CouponId>::owner();
}

void CouponUser::setOwnerInternal(const lib::Owner<CwxPath>& owner) {
    UserBase<CouponUser, CouponId>::owner(owner);
    if (coupon_) coupon_->owner(owner);
}

void CouponUser::owner(const lib::Owner<CwxPath>& owner) {
    setOwnerInternal(owner);
}

void CouponUser::owner(CwxPath* owner) {
    UserBase<CouponUser, CouponId>::owner(owner);
    if (coupon_) coupon_->owner(detail::nonOwning(owner));
}

void CouponUser::coupon(const std::string_view value) {
    const auto keepAlive = weak_from_this().lock();
    // Dの文字列値は、呼び出し側の変数を置き換えるコールバックをまたいでも有効。
    const String input(value);
    if (coupon() == input) return;
    changed();
    const CouponId oldId(coupon());
    const CouponId newId(input);
    if (callback_ && !callbackAllows(owner(), oldId, newId)) return;

    const auto user = selfOwner();
    const auto previous = counter_;
    if (previous && !coupon().empty()) previous->remove(oldId, user);
    if (previous && !input.empty()) previous->add(newId, user);
    // Dと同様に、SimpleTextHolderが記号の登録と失敗順を管理
    // する。パーサーが新しいテキストを受け付ける前に、それらを解除しない。
    coupon_->text(input);
    acceptedValue(coupon());
}

String CouponUser::coupon() const {
    return coupon_ ? coupon_->text() : String{};
}

bool CouponUser::expandSPChars() const noexcept { return expand_sp_chars_; }

void CouponUser::expandSPChars(const bool value) {
    if (value == expand_sp_chars_) return;
    const auto keepAlive = weak_from_this().lock();
    changed();
    expand_sp_chars_ = value;
    if (value && counter_) registerTextCounter(counter_, counter_owner_.lock());
    else unregisterTextCounter();
}

lib::Array<String> CouponUser::flagsInText() const {
    return expand_sp_chars_ && coupon_ ? coupon_->flagsInText()
                                       : lib::Array<String>{};
}
lib::Array<String> CouponUser::stepsInText() const {
    return expand_sp_chars_ && coupon_ ? coupon_->stepsInText()
                                       : lib::Array<String>{};
}
lib::Array<String> CouponUser::variantsInText() const {
    return expand_sp_chars_ && coupon_ ? coupon_->variantsInText()
                                       : lib::Array<String>{};
}

void CouponUser::registerTextCounter(
    const CounterOwner& counter, const lib::Owner<CwxPath>& owner) {
    if (!coupon_) return;
    coupon_->setUseCounter(counter, owner);
}

void CouponUser::unregisterTextCounter() {
    clearTextUsers(flag_users_);
    clearTextUsers(step_users_);
    clearTextUsers(variant_users_);
    if (coupon_) coupon_->removeUseCounter();
}

CounterOwner CouponUser::useCounter() const { return counter_; }

void CouponUser::setUseCounter(const CounterOwner& counter,
                               const lib::Owner<CwxPath>& counterOwner) {
    const auto desired = counter;
    const auto desiredOwner = counterOwner;
    const auto previous = counter_;
    if (previous.get() == desired.get()) return;
    const auto user = selfOwner();
    if (previous && !coupon().empty()) {
        previous->remove(toCouponId(coupon()), user);
    }
    if (desired && !coupon().empty()) {
        desired->add(toCouponId(coupon()), user);
    }
    if (expand_sp_chars_) registerTextCounter(desired, desiredOwner);
    // Dは外側の状態を公開する前に、既存の保持オブジェクトを接続する。
    counter_ = desired;
    counter_owner_anchor_.reset();
    counter_owner_ = renameInfo(desiredOwner);
    counter_owner_anchor_ = counter_owner_.lock();
}

void CouponUser::setUseCounter(const CounterOwner& counter,
                               CwxPath* counterOwner) {
    const auto anchor = detail::nonOwning(counterOwner);
    setUseCounter(counter, anchor);
}

void CouponUser::removeUseCounter() {
    // 状態の解放で所有元が解放される場合も、Dの有効なthis参照に合わせて生存させる。
    const auto keepAlive = weak_from_this().lock();
    const auto previous = counter_;
    if (previous && (!coupon().empty() || detail::NativeCounterRetirement::active())) {
        previous->remove(toCouponId(coupon()), selfOwner());
    }
    unregisterTextCounter();
    counter_.reset();
    counter_owner_anchor_.reset();
    counter_owner_.reset();
}

lib::Owner<CwxPath> CouponUser::ucOwner() {
    return counter_owner_.lock();
}

bool CouponUser::change(const CouponId& value) {
    const auto keepAlive = weak_from_this().lock();
    const auto replacement = value;
    if (coupon() == replacement.id) return true;
    changed();
    if (callback_ &&
        !callbackAllows(owner(), CouponId(coupon()), replacement)) {
        return false;
    }
    coupon_->text(replacement.id);
    acceptedValue(coupon());
    return true;
}

GossipUser::GossipUser(const lib::Owner<CwxPath>& owner, const bool callback)
    : UserBase(owner), callback_(callback) {
    gossip_ = std::make_shared<textholder::SimpleTextHolder>(
        detail::nonOwning(static_cast<CwxPath*>(this)),
        textholder::TextHolderType::Gossip, "gossip");
    gossip_->changeHandler([this] { acceptedValue(gossip()); changed(); });
    setOwnerInternal(owner);
}

GossipUser::GossipUser(CwxPath* owner, const bool callback)
    : GossipUser(detail::nonOwning(owner), callback) {
    UserBase<GossipUser, GossipId>::owner(owner);
    if (gossip_) gossip_->owner(UserBase<GossipUser, GossipId>::owner());
}

GossipUser::~GossipUser() { retireTextProducer(); }
void GossipUser::retireTextProducer() noexcept {
    if (gossip_) gossip_->retire();
}

lib::Owner<CwxPath> GossipUser::owner() const {
    return UserBase<GossipUser, GossipId>::owner();
}

void GossipUser::setOwnerInternal(const lib::Owner<CwxPath>& owner) {
    UserBase<GossipUser, GossipId>::owner(owner);
    if (gossip_) gossip_->owner(owner);
}

void GossipUser::owner(const lib::Owner<CwxPath>& owner) {
    setOwnerInternal(owner);
}

void GossipUser::owner(CwxPath* owner) {
    UserBase<GossipUser, GossipId>::owner(owner);
    if (gossip_) gossip_->owner(UserBase<GossipUser, GossipId>::owner());
}

void GossipUser::gossip(const std::string_view value) {
    const auto keepAlive = weak_from_this().lock();
    // コールバックが呼び出し側の文字列を置き換えても、Dの値引数を維持する。
    const String input(value);
    if (gossip() == input) return;
    changed();
    const GossipId oldId(gossip());
    const GossipId newId(input);
    if (callback_ && !callbackAllows(owner(), oldId, newId)) return;

    const auto user = selfOwner();
    const auto previous = counter_;
    if (previous && !gossip().empty()) previous->remove(oldId, user);
    if (previous && !input.empty()) previous->add(newId, user);
    // テキスト利用者の置換と解析失敗の扱いは、保持オブジェクト自身へ委ねる。
    gossip_->text(input);
    acceptedValue(gossip());
}

String GossipUser::gossip() const {
    return gossip_ ? gossip_->text() : String{};
}
bool GossipUser::expandSPChars() const noexcept { return expand_sp_chars_; }

void GossipUser::expandSPChars(const bool value) {
    if (value == expand_sp_chars_) return;
    const auto keepAlive = weak_from_this().lock();
    changed();
    expand_sp_chars_ = value;
    if (value && counter_) registerTextCounter(counter_, counter_owner_.lock());
    else unregisterTextCounter();
}

lib::Array<String> GossipUser::flagsInText() const {
    return expand_sp_chars_ && gossip_ ? gossip_->flagsInText()
                                       : lib::Array<String>{};
}
lib::Array<String> GossipUser::stepsInText() const {
    return expand_sp_chars_ && gossip_ ? gossip_->stepsInText()
                                       : lib::Array<String>{};
}
lib::Array<String> GossipUser::variantsInText() const {
    return expand_sp_chars_ && gossip_ ? gossip_->variantsInText()
                                       : lib::Array<String>{};
}

void GossipUser::registerTextCounter(
    const CounterOwner& counter, const lib::Owner<CwxPath>& owner) {
    if (!gossip_) return;
    gossip_->setUseCounter(counter, owner);
}

void GossipUser::unregisterTextCounter() {
    clearTextUsers(flag_users_);
    clearTextUsers(step_users_);
    clearTextUsers(variant_users_);
    if (gossip_) gossip_->removeUseCounter();
}

CounterOwner GossipUser::useCounter() const { return counter_; }

void GossipUser::setUseCounter(const CounterOwner& counter,
                               const lib::Owner<CwxPath>& counterOwner) {
    const auto desired = counter;
    const auto desiredOwner = counterOwner;
    const auto previous = counter_;
    if (previous.get() == desired.get()) return;
    const auto user = selfOwner();
    if (previous && !gossip().empty()) {
        previous->remove(toGossipId(gossip()), user);
    }
    if (desired && !gossip().empty()) {
        desired->add(toGossipId(gossip()), user);
    }
    if (expand_sp_chars_) registerTextCounter(desired, desiredOwner);
    counter_ = desired;
    counter_owner_anchor_.reset();
    counter_owner_ = renameInfo(desiredOwner);
    counter_owner_anchor_ = counter_owner_.lock();
}

void GossipUser::setUseCounter(const CounterOwner& counter,
                               CwxPath* counterOwner) {
    const auto anchor = detail::nonOwning(counterOwner);
    setUseCounter(counter, anchor);
}

void GossipUser::removeUseCounter() {
    const auto keepAlive = weak_from_this().lock();
    const auto previous = counter_;
    if (previous && (!gossip().empty() || detail::NativeCounterRetirement::active())) {
        previous->remove(toGossipId(gossip()), selfOwner());
    }
    unregisterTextCounter();
    counter_.reset();
    counter_owner_anchor_.reset();
    counter_owner_.reset();
}

lib::Owner<CwxPath> GossipUser::ucOwner() {
    return counter_owner_.lock();
}

bool GossipUser::change(const GossipId& value) {
    const auto keepAlive = weak_from_this().lock();
    const auto replacement = value;
    if (gossip() == replacement.id) return true;
    changed();
    if (callback_ &&
        !callbackAllows(owner(), GossipId(gossip()), replacement)) {
        return false;
    }
    gossip_->text(replacement.id);
    acceptedValue(gossip());
    return true;
}

#define CWX_DEFINE_TEXT_USER(CLASS, ID, SETTER, GETTER, TO_ID)                  \
    CLASS::CLASS(const lib::Owner<CwxPath>& owner) : UserBase(owner) {}         \
    CLASS::CLASS(CwxPath* owner) : UserBase(owner) {}                            \
    void CLASS::SETTER(const std::string_view input) {                          \
        const String value(input);                                              \
        if (value_ != value) changed();                                          \
        const auto user = selfOwner();                                          \
        const auto previous = counter_;                                          \
        if (previous && !value_.empty()) previous->remove(TO_ID(value_), user);  \
        if (const auto desired = counter_; desired && !value.empty())              \
            desired->add(TO_ID(value), user);                                   \
        value_.assign(value);                                                    \
        acceptedValue(value_);                                                    \
    }                                                                            \
    String CLASS::GETTER() const { return value_; }                              \
    CounterOwner CLASS::useCounter() const { return counter_; }                  \
    void CLASS::setUseCounter(const CounterOwner& counter,                      \
                              const lib::Owner<CwxPath>& counterOwner) {         \
        const auto previous = counter_;                                          \
        if (previous.get() == counter.get()) return;                             \
        const auto user = selfOwner();                                           \
        if (previous && !value_.empty()) previous->remove(TO_ID(value_), user);   \
        if (counter && !value_.empty()) counter->add(TO_ID(value_), user);       \
        counter_ = counter;                                                       \
        counter_owner_anchor_.reset();                                             \
        counter_owner_ = renameInfo(counterOwner);                               \
        counter_owner_anchor_ = counter_owner_.lock();                            \
    }                                                                            \
    void CLASS::setUseCounter(const CounterOwner& counter, CwxPath* owner) {      \
        const auto anchor = detail::nonOwning(owner);                             \
        setUseCounter(counter, anchor);                                           \
    }                                                                            \
    void CLASS::removeUseCounter() {                                             \
        const auto previous = counter_;                                           \
        if (previous && !value_.empty()) previous->remove(TO_ID(value_),         \
                                                           selfOwner());           \
        counter_.reset();                                                         \
        counter_owner_anchor_.reset();                                             \
        counter_owner_.reset();                                                   \
    }                                                                            \
    lib::Owner<CwxPath> CLASS::ucOwner() { return counter_owner_.lock(); }       \
    bool CLASS::change(const ID& input) {                                        \
        const auto value = input;                                               \
        if (value_ != value.id) changed();                                       \
        value_ = value.id;                                                       \
        acceptedValue(value_);                                                       \
        return true;                                                              \
    }

CWX_DEFINE_TEXT_USER(CompleteStampUser, CompleteStampId, completeStamp,
                     completeStamp, toCompleteStampId)
CWX_DEFINE_TEXT_USER(KeyCodeUser, KeyCodeId, keyCode, keyCode, toKeyCodeId)
CWX_DEFINE_TEXT_USER(CellNameUser, CellNameId, cellName, cellName,
                     toCellNameId)
CWX_DEFINE_TEXT_USER(CardGroupUser, CardGroupId, cardGroup, cardGroup,
                     toCardGroupId)

#undef CWX_DEFINE_TEXT_USER

SingleUseCounter::SingleUseCounter() : SingleUseCounter(true) {}

SingleUseCounter::SingleUseCounter(const bool useChild) {
    if (useChild) child_ = std::make_shared<SingleUseCounter>(false);
}

lib::Owner<SingleUseCounter> SingleUseCounter::sub() {
    if (child_) return child_;
    try {
        return shared_from_this();
    } catch (const std::bad_weak_ptr&) {
        return detail::nonOwning(this);
    }
}

UseCounter::UseCounter(const lib::Owner<CwxPath>& owner)
    : UseCounter(owner, this, std::make_shared<SingleUseCounter>(), {}, true) {}

UseCounter::UseCounter(CwxPath* owner)
    : UseCounter(detail::nonOwning(owner)) {
    owner_anchor_ = detail::nonOwning(owner);
    owner_ = owner_anchor_;
    if (sub_) sub_->owner_ = owner_anchor_;
}

UseCounter::UseCounter(const lib::Owner<CwxPath>& owner,
                       const CounterOwner& global)
    : UseCounter(owner, global.get(), global ? global->global_ : nullptr,
                 std::make_shared<SingleUseCounter>(), true, global) {
    if (!global) throw std::invalid_argument("global UseCounter is null");
}

UseCounter::UseCounter(CwxPath* owner, const CounterOwner& global)
    : UseCounter(detail::nonOwning(owner), global) {
    owner_anchor_ = detail::nonOwning(owner);
    owner_ = owner_anchor_;
    if (sub_) sub_->owner_ = owner_anchor_;
}

UseCounter::UseCounter(const lib::Owner<CwxPath>& owner,
                       UseCounter* globalCounter,
                       const lib::Owner<SingleUseCounter>& global,
                       const lib::Owner<SingleUseCounter>& local,
                       const bool createSub, CounterOwner globalOwner)
    : owner_(owner),
      global_counter_(globalCounter ? globalCounter : this),
      global_owner_(std::move(globalOwner)),
      global_(global),
      local_(local) {
    if (!global_) global_ = std::make_shared<SingleUseCounter>();
    if (createSub) {
        const auto globalSub = global_->sub();
        CounterOwner counterSub;
        if (global_counter_ != this && global_counter_) {
            counterSub = global_counter_->sub();
        }
        const auto localSub = local_ ? local_->sub() : lib::Owner<SingleUseCounter>{};
        sub_ = std::shared_ptr<UseCounter>(new UseCounter(
            owner_.lock(), counterSub ? counterSub.get() : nullptr, globalSub, localSub,
            false, counterSub));
        if (!sub_->global_counter_) sub_->global_counter_ = sub_.get();
    }
}

lib::Owner<CwxPath> UseCounter::owner() const {
    return path::retainCallbackOwner(owner_.lock());
}

CounterOwner UseCounter::selfOwner() {
    try {
        return shared_from_this();
    } catch (const std::bad_weak_ptr&) {
        // 追加のネイティブのスタック・生カウンターAPIは、同じカ
        // ウンター実装を共有しつつ、寿命を呼び出し側から借用する。
        return detail::nonOwning(this);
    }
}

CounterOwner UseCounter::global() const {
    if (global_counter_ == this) {
        return const_cast<UseCounter*>(this)->selfOwner();
    }
    if (global_owner_) return global_owner_;
    return global_counter_ ? global_counter_->selfOwner() : CounterOwner{};
}

CounterOwner UseCounter::sub() const {
    return sub_ ? sub_ : const_cast<UseCounter*>(this)->selfOwner();
}

lib::Owner<CwxPath> renameInfo(const lib::Owner<CwxPath>& value) {
    if (!value) return lib::Owner<CwxPath>{};
    auto current = value;
    while (current) {
        const auto parentObserver = current->cwxParent();
        const auto parent = parentObserver.lock();
        if (!parent) break;

        const bool stop =
            dynamic_cast<summary::Summary*>(current.get()) != nullptr ||
            dynamic_cast<card::CastCard*>(current.get()) != nullptr ||
            dynamic_cast<area::AbstractArea*>(current.get()) != nullptr ||
            (dynamic_cast<card::EffectCard*>(current.get()) != nullptr &&
             dynamic_cast<motion::Motion*>(current->cwxParent().lock().get()) == nullptr);
        if (stop) break;
        // D版は所属先へ進む直前にも親を取得する。getterの通知による更新を反映する。
        current = current->cwxParent().lock();
    }
    return current;
}

lib::Owner<CwxPath> renameInfo(CwxPath* value) {
    return renameInfo(detail::nonOwning(value));
}

}  // namespace cwxeditor::cwx::usecounter
