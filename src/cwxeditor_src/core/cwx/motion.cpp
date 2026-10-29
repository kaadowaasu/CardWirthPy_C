#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"

// Motionとカードは相互参照するため、これ
// らのヘッダーは実装ファイルに置く。公開motion
// ヘッダーには、nullを許容するカード・XML
// の宣言だけが必要。下記の検出用補助処理は
// 、関連クラスの具体的なメソッドの有無に応
// じて、対応する分岐をコンパイル時に選ぶ。
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace cwxeditor::cwx::motion {
namespace {

using BeastCard = card::BeastCard;
using XMLOption = card::XMLOption;
using XMLInfo = system::XMLInfo;
using UseCounter = usecounter::UseCounter;
using XNode = xml::XNode;

lib::Dictionary<MType, MDetail> motion_details;
lib::Dictionary<lib::String, MType> motion_types;

MDetail detail(lib::String name,
               std::initializer_list<std::pair<MArg, lib::String>> values = {}) {
    MDetail result = MDetail::opCall(std::move(name));
    for (const auto& [key, value] : values) result.args[key] = value;
    return result;
}

[[nodiscard]] lib::String makeObjectId(const Motion* motion,
                                       std::uint64_t serial) {
    // Dはコンパイラー固有の型ラベル、objectIDValue、コンス
    // トラクター内の連番を組み合わせる。EventTreeと同様にネイ
    // ティブの型表記を維持し、同じ時刻・アドレスの識別処理を共有する。
    return "Motion-" + utils::objectIDValue(motion) + '-' + std::to_string(serial);
}

template <typename B>
[[nodiscard]] std::uint64_t beastId(const lib::Owner<B>& beast) {
    if (!beast) return 0U;
    if constexpr (requires(const B& value) {
                      { value.id() } -> std::convertible_to<std::uint64_t>;
                  }) {
        return static_cast<std::uint64_t>(beast->id());
    } else if constexpr (requires(const B& value) {
                             { value.id } -> std::convertible_to<std::uint64_t>;
                         }) {
        return static_cast<std::uint64_t>(beast->id);
    }
    return 0U;
}

template <typename B>
[[nodiscard]] std::uint64_t beastLinkId(const lib::Owner<B>& beast) {
    if (!beast) return 0U;
    if constexpr (requires(const B& value) {
                      { value.linkId() } -> std::convertible_to<std::uint64_t>;
                  }) {
        return static_cast<std::uint64_t>(beast->linkId());
    } else if constexpr (requires(const B& value) {
                             { value.linkId } -> std::convertible_to<std::uint64_t>;
                         }) {
        return static_cast<std::uint64_t>(beast->linkId);
    }
    return 0U;
}

template <typename B>
void setBeastId(const lib::Owner<B>& beast, std::uint64_t id) {
    if (!beast) return;
    if constexpr (requires(B& value, std::uint64_t newId) { value.id(newId); }) {
        beast->id(id);
    } else if constexpr (requires(B& value, std::uint64_t newId) {
                             value.id = newId;
                         }) {
        beast->id = id;
    }
}

template <typename B>
void setBeastChangeHandler(const lib::Owner<B>& beast,
                           const ChangeHandler& change) {
    if (!beast) return;
    if constexpr (requires(B& value, const ChangeHandler& handler) {
                      value.changeHandler(handler);
                  }) {
        beast->changeHandler(change);
    }
}

template <typename B>
void setBeastUseCounter(const lib::Owner<B>& beast,
                        const lib::Owner<UseCounter>& counter,
                        const lib::Owner<CwxPath>& owner) {
    if (!beast) return;
    if constexpr (requires(B& value, const lib::Owner<UseCounter>& uc,
                           const lib::Owner<CwxPath>& path) {
                      value.setUseCounter(uc, path);
                  }) {
        beast->setUseCounter(counter, owner);
    } else if constexpr (requires(B& value, UseCounter* uc, CwxPath* path) {
                             value.setUseCounter(uc, path);
                         }) {
        beast->setUseCounter(counter.get(), owner.get());
    }
}

template <typename B>
void removeBeastUseCounter(const lib::Owner<B>& beast) {
    if (!beast) return;
    if constexpr (requires(B& value) { value.removeUseCounter(); }) {
        beast->removeUseCounter();
    }
}

template <typename B>
[[nodiscard]] lib::Owner<std::remove_const_t<B>> duplicateBeast(
    const lib::Owner<B>& beast) {
    using MutableBeast = std::remove_const_t<B>;
    if (!beast) return {};
    if constexpr (requires(const MutableBeast& value) {
                      { value.dupBeast() } -> std::same_as<lib::Owner<MutableBeast>>;
                  }) {
        return beast->dupBeast();
    } else if constexpr (requires(const MutableBeast& value) {
                      { value.dup() } -> std::same_as<lib::Owner<MutableBeast>>;
                  }) {
        return beast->dup();
    }
    // 入れ子の元カードを共有すると、取消・元に戻す操作の独立性が壊れ
    // る。将来のBeast表現にD互換の深いコピーAPIがなければ
    // 、編集可能なオブジェクトを黙って共有せず明示的に失敗させる。
    throw std::logic_error("nested Beast card does not support deep copy");
}

template <typename B>
[[nodiscard]] bool equalBeast(const lib::Owner<B>& left,
                              const lib::Owner<B>& right) {
    if (!left || !right) return !left && !right;
    if constexpr (requires(const B& a, const B& b) {
                      { a.opEquals(b) } -> std::convertible_to<bool>;
                  }) {
        return static_cast<bool>(left->opEquals(*right));
    } else if constexpr (requires(const B& a, const B& b) {
                             { a == b } -> std::convertible_to<bool>;
                         }) {
        return static_cast<bool>(*left == *right);
    }
    return left.get() == right.get();
}

template <typename B>
void setBeastOwner(const lib::Owner<B>& beast, Motion* owner) {
    if (!beast) return;
    if constexpr (requires(B& value, Motion* motion) { value.owner(motion); }) {
        beast->owner(owner);
    } else if constexpr (requires(B& value, CwxPath* path) {
                             value.owner(path);
                         }) {
        beast->owner(static_cast<CwxPath*>(owner));
    } else if constexpr (requires(B& value, Motion* motion) {
                             value.owner = motion;
                         }) {
        beast->owner = owner;
    } else if constexpr (requires(B& value, CwxPath* path) {
                             value.owner = path;
                         }) {
        beast->owner = static_cast<CwxPath*>(owner);
    }
}

template <typename B>
void beastToNode(const lib::Owner<B>& beast, XNode& parent,
                 const lib::Owner<XMLOption>& opt) {
    if (!beast) return;
    const XMLOption defaultOption{nullptr, versioninfo::LATEST_VERSION};
    const auto& option = opt ? *opt : defaultOption;
    if constexpr (requires(const B& value, XNode& node,
                           const XMLOption& xmlOption) {
                      value.toNode(node, xmlOption);
                  }) {
        static_cast<void>(beast->toNode(parent, option));
    } else if constexpr (requires(const B& value, const XMLOption& xmlOption) {
                             parent.append(value.toNode(xmlOption));
                         }) {
        parent.append(beast->toNode(option));
    } else {
        throw std::logic_error("nested Beast card does not support XML output");
    }
}

template <typename B>
[[nodiscard]] lib::Owner<B> beastFromNode(XNode& node, const XMLInfo& ver) {
    if constexpr (requires(XNode& value, const XMLInfo& version) {
                      B::createFromNode(value, version);
                  }) {
        using Result = decltype(B::createFromNode(node, ver));
        if constexpr (std::same_as<Result, lib::Owner<B>>) {
            return B::createFromNode(node, ver);
        }
    }
    return {};
}

template <typename B>
[[nodiscard]] lib::Owner<CwxPath> beastFindPath(
    const lib::Owner<B>& beast, std::string_view value) {
    if (!beast) return {};
    if constexpr (requires(B& object, std::string_view pathValue) {
                      object.findCWXPath(pathValue);
                  }) {
        using Result = decltype(beast->findCWXPath(value));
        if constexpr (std::convertible_to<Result, lib::Owner<CwxPath>>) {
            return beast->findCWXPath(value);
        }
    } else if constexpr (requires(B& object, const lib::String& pathValue) {
                             object.findCWXPath(pathValue);
                         }) {
        using Result = decltype(beast->findCWXPath(lib::String(value)));
        if constexpr (std::convertible_to<Result, lib::Owner<CwxPath>>) {
            return beast->findCWXPath(lib::String(value));
        }
    }
    return {};
}

template <typename B>
[[nodiscard]] lib::Owner<CwxPath> beastPathOwner(
    const lib::Owner<B>& beast) {
    if (!beast) return {};
    if constexpr (std::is_base_of_v<CwxPath, B>) {
        return std::static_pointer_cast<CwxPath>(beast);
    }
    return {};
}

template <typename O>
[[nodiscard]] bool includeCard(const O* option) {
    if (!option) return false;
    if constexpr (requires(const O& value) {
                      static_cast<bool>(value.includeCard);
                  }) {
        return static_cast<bool>(option->includeCard);
    }
    return false;
}

template <typename O>
[[nodiscard]] std::uint32_t nestCount(const O* option,
                                      std::uint64_t linkId) {
    if (!option) return 0U;
    if constexpr (requires(const O& value, std::uint64_t key) {
                      value.nestCount.find(key);
                      value.nestCount.end();
                  }) {
        const auto found = option->nestCount.find(linkId);
        if (found != option->nestCount.end()) {
            return static_cast<std::uint32_t>(found->second);
        }
    }
    return 0U;
}

template <typename O>
void setNestCount(O* option, std::uint64_t linkId, std::uint32_t value) {
    if (!option) return;
    if constexpr (requires(O& object, std::uint64_t key,
                           std::uint32_t count) {
                      object.nestCount[key] = count;
                  }) {
        option->nestCount[linkId] = value;
    }
}

template <typename O>
void eraseNestCount(O* option, std::uint64_t linkId) {
    if (!option) return;
    if constexpr (requires(O& object, std::uint64_t key) {
                      object.nestCount.erase(key);
                  }) {
        option->nestCount.erase(linkId);
    }
}

}  // namespace

void static_this() {
    perf::TraceScope traceMotionD18(__FILE__, __LINE__);
    // Dは完了フラグを設定せず、検索ごとにこの表を作り直す。返されたMDetail
    // は自身のコピーと連想配列を共有する一方、後続のMotion.detail
    // は新しい定義から始まるため、この違いは外から観測できる。

    motion_details.clear();
    motion_types.clear();

    const auto add = [](MType type, MDetail value) {
        { perf::TraceScope traceMotionD70(__FILE__, __LINE__);
        motion_types[value.name] = type;
        }
        motion_details[type] = std::move(value);
    };
    add(MType::Heal, detail("Heal", {{MArg::ValueType, "damagetype"},
                                      {MArg::UValue, "value"}}));
    add(MType::Damage, detail("Damage", {{MArg::ValueType, "damagetype"},
                                          {MArg::UValue, "value"}}));
    add(MType::Absorb, detail("Absorb", {{MArg::ValueType, "damagetype"},
                                          {MArg::UValue, "value"}}));
    add(MType::Paralyze, detail("Paralyze", {{MArg::ValueType, "damagetype"},
                                              {MArg::UValue, "value"}}));
    add(MType::DisParalyze,
        detail("DisParalyze", {{MArg::ValueType, "damagetype"},
                                {MArg::UValue, "value"}}));
    add(MType::Poison, detail("Poison", {{MArg::ValueType, "damagetype"},
                                          {MArg::UValue, "value"}}));
    add(MType::DisPoison,
        detail("DisPoison", {{MArg::ValueType, "damagetype"},
                              {MArg::UValue, "value"}}));
    add(MType::GetSkillPower,
        detail("GetSkillPower", {{MArg::ValueType, "damagetype"},
                                  {MArg::UValue, "value"}}));
    add(MType::LoseSkillPower,
        detail("LoseSkillPower", {{MArg::ValueType, "damagetype"},
                                   {MArg::UValue, "value"}}));
    add(MType::Sleep, detail("Sleep", {{MArg::Round, "duration"}}));
    add(MType::Confuse, detail("Confuse", {{MArg::Round, "duration"}}));
    add(MType::Overheat, detail("Overheat", {{MArg::Round, "duration"}}));
    add(MType::Brave, detail("Brave", {{MArg::Round, "duration"}}));
    add(MType::Panic, detail("Panic", {{MArg::Round, "duration"}}));
    add(MType::Normal, detail("Normal"));
    add(MType::Bind, detail("Bind", {{MArg::Round, "duration"}}));
    add(MType::DisBind, detail("DisBind"));
    add(MType::Silence, detail("Silence", {{MArg::Round, "duration"}}));
    add(MType::DisSilence, detail("DisSilence"));
    add(MType::FaceUp, detail("FaceUp", {{MArg::Round, "duration"}}));
    add(MType::FaceDown, detail("FaceDown"));
    add(MType::AntiMagic,
        detail("AntiMagic", {{MArg::Round, "duration"}}));
    add(MType::DisAntiMagic, detail("DisAntiMagic"));
    add(MType::EnhanceAction,
        detail("EnhanceAction", {{MArg::Round, "duration"},
                                  {MArg::AValue, "value"}}));
    add(MType::EnhanceAvoid,
        detail("EnhanceAvoid", {{MArg::Round, "duration"},
                                 {MArg::AValue, "value"}}));
    add(MType::EnhanceResist,
        detail("EnhanceResist", {{MArg::Round, "duration"},
                                  {MArg::AValue, "value"}}));
    add(MType::EnhanceDefense,
        detail("EnhanceDefense", {{MArg::Round, "duration"},
                                   {MArg::AValue, "value"}}));
    add(MType::VanishTarget, detail("VanishTarget"));
    add(MType::VanishCard, detail("VanishCard"));
    add(MType::VanishBeast, detail("VanishBeast"));
    add(MType::DealAttackCard, detail("DealAttackCard"));
    add(MType::DealPowerfulAttackCard, detail("DealPowerfulAttackCard"));
    add(MType::DealCriticalAttackCard, detail("DealCriticalAttackCard"));
    add(MType::DealFeintCard, detail("DealFeintCard"));
    add(MType::DealDefenseCard, detail("DealDefenseCard"));
    add(MType::DealDistanceCard, detail("DealDistanceCard"));
    add(MType::DealConfuseCard, detail("DealConfuseCard"));
    add(MType::DealSkillCard, detail("DealSkillCard"));
    add(MType::SummonBeast, detail("SummonBeast", {{MArg::Beast, {}}}));
    add(MType::CancelAction, detail("CancelAction"));
    add(MType::NoEffect, detail("NoEffect"));

}

bool MDetail::use(MArg arg) const { return args.find(arg) != args.end(); }

lib::String MDetail::attr(MArg arg) const {
    // Dの連想配列検索はキーがなければ失敗する。存在する空の属性は
    // 別扱いであり、Beastは属性ではなく子要素を意図的に使う。
    return args.at(arg);
}

MDetail MDetail::opCall(lib::String name) {
    MDetail result;
    result.name = std::move(name);
    return result;
}

MDetail MDetail::opCall(lib::String name,
                        lib::SharedDictionary<MArg, lib::String> args) {
    MDetail result;
    result.name = std::move(name);
    result.args = std::move(args);
    return result;
}

lib::Dictionary<MType, MDetail> MOTION_DETAILS() {
    perf::TraceScope traceMotionD77(__FILE__, __LINE__);
    static_this();
    return motion_details;
}

lib::Dictionary<lib::String, MType> MTYPE_MAP() {
    perf::TraceScope traceMotionD83(__FILE__, __LINE__);
    static_this();
    return motion_types;
}

MType mTypeFromName(lib::StringView name) {
    perf::TraceScope traceMotionD89(__FILE__, __LINE__);
    static_this();
    const auto found = motion_types.find(lib::String(name));
    if (found == motion_types.end()) {
        // Dの連想配列の右辺値検索は、キーがなければRangeError
        // を起こす。不正なXMLを黙って回復効果へ変えない。
        throw std::invalid_argument("unknown motion type: " + lib::String(name));
    }
    return found->second;
}

lib::String mTypeToName(MType type) {
    perf::TraceScope traceMotionD93(__FILE__, __LINE__);
    static_this();
    return motion_details.at(type).name;
}

MotionUser::MotionUser(const lib::Owner<MotionOwner>& cwxPath)
    : cwx_path_(cwxPath), cwx_path_owner_(cwxPath), cwx_path_raw_(cwxPath.get()) {
    if (cwxPath) {
        cwx_path_lifetime_ = cwxPath->motionLifetimeToken();
    }
}

MotionUser::MotionUser(MotionOwner* cwxPath) noexcept
    : cwx_path_raw_(cwxPath) {
    if (cwxPath) {
        cwx_path_lifetime_ = cwxPath->motionLifetimeToken();
    }
}

MotionUser::~MotionUser() {
    const usecounter::detail::NativeCounterRetirement native_retirement;
    // DのGCはMotionを通じて親を保持できる。C++の子は弱参照
    // で接続されるため、親の解放時に構築中の借用所有元を無効にする。
    for (const auto& motion : motions_) {
        if (!motion || motion->owner_raw_ != cwx_path_raw_) continue;
        motion->changeHandler({});
        motion->removeUseCounter();
        motion->attachOwner(nullptr, {});
    }
}

lib::String MotionUser::cwxPath(bool id) const {
    const auto lifetime = cwx_path_lifetime_.lock();
    if (!lifetime || !lifetime->alive.load(std::memory_order_acquire) || !cwx_path_raw_) {
        // Dの必須の所有元参照に対応し、ネイティブでは動作が定義された失敗にする。
        throw std::invalid_argument("MotionUser path requires an owner");
    }
    if (const auto owner = cwx_path_.lock()) return owner->cwxPath(id);
    if (const auto owner = cwx_path_raw_->retainForCallback()) {
        return owner->cwxPath(id);
    }
    // スタック上や構築中の所有元には共有制御ブロックがな
    // い。寿命トークンが有効な間は、同期的に使用できる。
    return cwx_path_raw_->cwxPath(id);
}

void MotionUser::changeHandler(ChangeHandler change) {
    perf::TraceScope traceMotionD138(__FILE__, __LINE__);
    change_ = std::move(change);
}

void MotionUser::changed() {
    perf::TraceScope traceMotionD142(__FILE__, __LINE__);
    if (change_) change_();
}

lib::Observer<UseCounter> MotionUser::useCounter() { return use_counter_; }

void MotionUser::setUseCounter(const lib::Owner<UseCounter>& uc,
                               const lib::Owner<CwxPath>& ucOwner) {
    perf::TraceScope traceMotionD152(__FILE__, __LINE__);
    for (const auto& motion : motions_) {
        perf::TraceScope traceMotionD153(__FILE__, __LINE__);
        if (motion) motion->setUseCounter(uc, ucOwner);
    }
    use_counter_ = uc;
    use_counter_owner_ = usecounter::renameInfo(ucOwner);
}

void MotionUser::removeUseCounter() {
    perf::TraceScope traceMotionD160(__FILE__, __LINE__);
    for (const auto& motion : motions_) {
        perf::TraceScope traceMotionD161(__FILE__, __LINE__);
        if (motion) motion->removeUseCounter();
    }
    use_counter_.reset();
    use_counter_owner_.reset();
}

void MotionUser::motions(lib::SharedSlice<lib::Owner<Motion>> value) {
    perf::TraceScope traceMotionD169(__FILE__, __LINE__);
    const auto equals = [](const auto& left, const auto& right) {
        if (left.size() != right.size()) return false;
        for (std::size_t index = 0; index < left.size(); ++index) {
            if (!left[index] || !right[index]) {
                if (left[index] != right[index]) return false;
            } else if (!left[index]->opEquals(*right[index])) {
                return false;
            }
        }
        return true;
    };
    // Dの配列の等価比較は、クラス要素にMotion.opEquals
    // を適用する。ここでshared_ptrの同一性を比較する
    // と、値が等しい列を置換して不要な変更通知を出してしまう。
    if (equals(motions_, value)) return;

    // Dは設定関数のコールバック中も受信先を生存させる。EffectCard
    // のMotionUserは構築中に生の所有元だけを保持するため、changed()
    // と残りの変更処理を通じて実際の共有所有元を保持する。
    const auto lifetime = cwx_path_lifetime_.lock();
    if (!lifetime || !lifetime->alive.load(std::memory_order_acquire) || !cwx_path_raw_) {
        throw std::invalid_argument("MotionUser motions requires a live owner");
    }
    [[maybe_unused]] const auto keepAlive = cwx_path_raw_->retainForCallback();
    { perf::TraceScope traceMotionD170(__FILE__, __LINE__);
    changed();
    for (const auto& motion : motions_) {
        if (!motion) continue;
    perf::TraceScope traceMotionD172(__FILE__, __LINE__);
        motion->changeHandler({});
        motion->removeUseCounter();
        motion->attachOwner(nullptr, {});
    }
    const auto owner = cwx_path_.lock();
    const auto counter = use_counter_.lock();
    const auto counterOwner = use_counter_owner_.lock();
    for (const auto& motion : value) {
    perf::TraceScope traceMotionD177(__FILE__, __LINE__);
        if (!motion) continue;
        motion->changeHandler(change_);
        if (counter) motion->setUseCounter(counter, counterOwner);
        motion->attachOwner(cwx_path_raw_, owner);
    }
    motions_ = std::move(value);
    }
}

void MotionUser::motions(lib::Array<lib::Owner<Motion>> value) {
    motions(lib::SharedSlice<lib::Owner<Motion>>(std::move(value)));
}

void MotionUser::motions(std::initializer_list<lib::Owner<Motion>> value) {
    motions(lib::SharedSlice<lib::Owner<Motion>>(value));
}

lib::SharedSlice<lib::Owner<Motion>> MotionUser::motions() noexcept {
    perf::TraceScope traceMotionD188(__FILE__, __LINE__);
    return motions_;
}

lib::ConstOwnerSlice<Motion> MotionUser::motions() const noexcept {
    perf::TraceScope traceMotionD188(__FILE__, __LINE__);
    return lib::ConstOwnerSlice<Motion>(motions_);
}

Motion::Motion(MType type, Element element)
    : type_(type), element_(element) {
    static std::atomic<std::uint64_t> idCount{0U};
    object_id_ = makeObjectId(this, idCount.fetch_add(1U));
    if (type == MType::GetSkillPower || type == MType::LoseSkillPower) {
        damage_type_ = DamageType::Max;
    }
}

Motion::~Motion() {
    invalidateBeastOwnerLifetime();
}

lib::String Motion::objectId() const { return object_id_; }

MDetail Motion::detail() const {
    perf::TraceScope traceMotionD253(__FILE__, __LINE__);
    const auto& details = MOTION_DETAILS();
    return details.at(type_);
}

void Motion::changeHandler(ChangeHandler change) {
    perf::TraceScope traceMotionD260(__FILE__, __LINE__);
    if (beast_) setBeastChangeHandler(beast_, change);
    change_ = std::move(change);
}

lib::Observer<UseCounter> Motion::useCounter() { return use_counter_; }

void Motion::setUseCounter(const lib::Owner<UseCounter>& uc,
                           const lib::Owner<CwxPath>& ucOwner) {
    perf::TraceScope traceMotionD270(__FILE__, __LINE__);
    if (beast_) { perf::TraceScope traceMotionD271(__FILE__, __LINE__); setBeastUseCounter(beast_, uc, ucOwner); }
    use_counter_ = uc;
    use_counter_owner_ = usecounter::renameInfo(ucOwner);
}

void Motion::removeUseCounter() {
    perf::TraceScope traceMotionD278(__FILE__, __LINE__);
    if (beast_) { perf::TraceScope traceMotionD279(__FILE__, __LINE__); removeBeastUseCounter(beast_); }
    use_counter_.reset();
    use_counter_owner_.reset();
}

lib::Owner<Motion> Motion::dup() const {
    perf::TraceScope traceMotionD289(__FILE__, __LINE__);
    auto result = std::make_shared<Motion>(type_, element_);
    result->object_id_ = object_id_;
    result->damageType(damage_type_);
    result->uValue(static_cast<std::int32_t>(u_value_));
    result->aValue(a_value_);
    result->round(static_cast<std::int32_t>(round_));
    result->maxNest(max_nest_);
    if (beast_) { perf::TraceScope traceMotionD297(__FILE__, __LINE__); result->newBeast(duplicateBeast(beast_)); }
    result->comment(comment_);
    return result;
}

bool Motion::opEquals(const Motion& other) const {
    perf::TraceScope traceMotionD304(__FILE__, __LINE__);
    if (type_ != other.type_ || element_ != other.element_ ||
        damage_type_ != other.damage_type_ || u_value_ != other.u_value_ ||
        a_value_ != other.a_value_ || round_ != other.round_ ||
        max_nest_ != other.max_nest_ || comment_ != other.comment_) {
        return false;
    }
    return equalBeast(beast_, other.beast_);
}

std::int32_t Motion::roundValue(std::int32_t value, std::int32_t max,
                                std::int32_t min) noexcept {
    perf::TraceScope traceMotionD324(__FILE__, __LINE__);
    if (value > max) return max;
    if (value < min) return min;
    return value;
}

void Motion::element(Element value) {
    perf::TraceScope traceMotionD336(__FILE__, __LINE__);
    if (element_ != value) changed();
    element_ = value;
}

void Motion::damageType(DamageType value) {
    perf::TraceScope traceMotionD347(__FILE__, __LINE__);
    if (damage_type_ != value) changed();
    damage_type_ = value;
}

void Motion::uValue(std::int32_t value) {
    perf::TraceScope traceMotionD361(__FILE__, __LINE__);
    value = roundValue(value, uValue_max, uValue_min);
    if (u_value_ != static_cast<std::uint32_t>(value)) changed();
    u_value_ = static_cast<std::uint32_t>(value);
}

void Motion::aValue(std::int32_t value) {
    perf::TraceScope traceMotionD376(__FILE__, __LINE__);
    value = roundValue(value, aValue_max, aValue_min);
    if (a_value_ != value) changed();
    a_value_ = value;
}

void Motion::round(std::int32_t value) {
    perf::TraceScope traceMotionD387(__FILE__, __LINE__);
    value = roundValue(value, round_max, round_min);
    if (round_ != static_cast<std::uint32_t>(value)) changed();
    round_ = static_cast<std::uint32_t>(value);
}

lib::Owner<BeastCard> Motion::beast() { return beast_; }
lib::Owner<const BeastCard> Motion::beast() const { return beast_; }

lib::Owner<BeastCard> Motion::beast(std::uint64_t id) {
    return hasBeastId(id) ? beast_ : lib::Owner<BeastCard>{};
}
lib::Owner<const BeastCard> Motion::beast(std::uint64_t id) const {
    return hasBeastId(id) ? beast_ : lib::Owner<const BeastCard>{};
}

bool Motion::hasBeastId(const std::uint64_t id) const {
    return beast_ && beastId(beast_) == id;
}

lib::SharedSlice<lib::Owner<BeastCard>> Motion::beasts() {
    return beastSnapshot();
}

lib::ConstOwnerSlice<BeastCard> Motion::beasts() const {
    return lib::ConstOwnerSlice<BeastCard>(beastSnapshot());
}

lib::SharedSlice<lib::Owner<BeastCard>> Motion::beastSnapshot() const {
    if (!beast_) return {};
    // Dは新しい一要素配列を返す。取得結果の項目の領域を独立させ、その置換
    // でMotion所有のBeastCardを置き換えられないようにする。
    return lib::SharedSlice<lib::Owner<BeastCard>>(
        std::vector<lib::Owner<BeastCard>>{beast_});
}

void Motion::beast(const lib::Owner<const BeastCard>& value) {
    perf::TraceScope traceMotionD410(__FILE__, __LINE__);
    if (beast_) {
        perf::TraceScope traceMotionD411(__FILE__, __LINE__);
        setBeastChangeHandler(beast_, {});
        removeBeastUseCounter(beast_);
        setBeastOwner(beast_, nullptr);
    }
    if (value) {
        perf::TraceScope traceMotionD416(__FILE__, __LINE__);
        setBeastImpl(duplicateBeast(value));
    } else {
        perf::TraceScope traceMotionD418(__FILE__, __LINE__);
        beast_.reset();
    }
}

void Motion::newBeast(const lib::Owner<BeastCard>& value) {
    perf::TraceScope traceMotionD424(__FILE__, __LINE__);
    if (beast_) {
        perf::TraceScope traceMotionD425(__FILE__, __LINE__);
        setBeastChangeHandler(beast_, {});
        removeBeastUseCounter(beast_);
        setBeastOwner(beast_, nullptr);
    }
    if (value) {
        perf::TraceScope traceMotionD430(__FILE__, __LINE__);
        setBeastImpl(value);
    } else {
        perf::TraceScope traceMotionD432(__FILE__, __LINE__);
        beast_.reset();
    }
}

std::uint64_t Motion::setBeastFromNode(XNode& node, const XMLInfo& ver) {
    perf::TraceScope traceMotionD438(__FILE__, __LINE__);
    assert(node.name() == "BeastCard");
    auto value = beastFromNode<BeastCard>(node, ver);
    if (!value) {
        throw std::runtime_error("Unable to create BeastCard from XML node");
    }
    const auto id = beastId(value);
    setBeastImpl(value);
    return id;
}

void Motion::setBeastImpl(const lib::Owner<BeastCard>& value) {
    perf::TraceScope traceMotionD445(__FILE__, __LINE__);
    if (!value) return;
    changed();
    beast_ = value;
    setBeastId(beast_, 1U);
    setBeastChangeHandler(beast_, change_);
    if (const auto counter = use_counter_.lock()) {
        setBeastUseCounter(beast_, counter, use_counter_owner_.lock());
    }
    setBeastOwner(beast_, this);
}

void Motion::maxNest(std::uint32_t value) {
    perf::TraceScope traceMotionD460(__FILE__, __LINE__);
    // DはroundValue()でuintをintへ暗黙変換する
    // 。0～999へ制限する前に、2の補数での境界動作を維持する。
    const auto rounded = roundValue(static_cast<std::int32_t>(value),
                                    static_cast<std::int32_t>(maxNest_max),
                                    static_cast<std::int32_t>(maxNest_min));
    const auto result = static_cast<std::uint32_t>(rounded);
    if (max_nest_ != result) changed();
    max_nest_ = result;
}

void Motion::comment(lib::String value) {
    perf::TraceScope traceMotionD478(__FILE__, __LINE__);
    if (comment_ == value) return;
    changed();
    comment_ = std::move(value);
}

void Motion::changed() {
    perf::TraceScope traceMotionD484(__FILE__, __LINE__);
    if (change_) change_();
}

lib::String Motion::toXML(const lib::Owner<XMLOption>& opt) const {
    perf::TraceScope traceMotionD490(__FILE__, __LINE__);
    return toNode(opt).text();
}

XNode Motion::toNode(const lib::Owner<XMLOption>& opt) const {
    perf::TraceScope traceMotionD495(__FILE__, __LINE__);
    auto node = XNode::create(XML_NAME);
    static_cast<void>(toNodeImpl(node, opt));
    return node;
}

XNode Motion::toNode(XNode& node,
    const lib::Owner<XMLOption>& opt) const {
    perf::TraceScope traceMotionD503(__FILE__, __LINE__);
    auto child = node.newElement(XML_NAME);
    static_cast<void>(toNodeImpl(child, opt));
    return child;
}

XNode Motion::toNodeImpl(XNode& node,
                         const lib::Owner<XMLOption>& opt) const {
    perf::TraceScope traceMotionD509(__FILE__, __LINE__);
    const auto d = detail();
    node.newAttr("type", d.name);
    node.newAttr("element", types::fromElement(element_));
    if (!comment_.empty()) node.newAttr("comment", comment_);
    if (d.use(MArg::ValueType)) {
        node.newAttr(d.attr(MArg::ValueType), types::fromDamageType(damage_type_));
    }
    if (d.use(MArg::UValue)) node.newAttr(d.attr(MArg::UValue), u_value_);
    if (d.use(MArg::AValue)) node.newAttr(d.attr(MArg::AValue), a_value_);
    if (d.use(MArg::Round)) node.newAttr(d.attr(MArg::Round), round_);
    if (d.use(MArg::Beast)) {
        perf::TraceScope traceMotionD518(__FILE__, __LINE__);
        auto beastsNode = node.newElement("Beasts");
        if (includeCard(opt.get())) beastsNode.newAttr("maxNest", max_nest_);
        if (beast_) {
        perf::TraceScope traceMotionD521(__FILE__, __LINE__);
            const auto linkId = beastLinkId(beast_);
            if (includeCard(opt.get()) && linkId != 0U) {
        perf::TraceScope traceMotionD522(__FILE__, __LINE__);
                const auto previousCount = nestCount(opt.get(), linkId);
                const auto count = previousCount + 1U;
                setNestCount(opt.get(), linkId, count);
                struct NestCountRestore final {
                    XMLOption* option{};
                    std::uint64_t linkId{};
                    std::uint32_t previous{};
                    ~NestCountRestore() {
                        if (previous == 0U) eraseNestCount(option, linkId);
                        else setNestCount(option, linkId, previous);
                    }
                } restore{opt.get(), linkId, previousCount};
                if (count <= max_nest_) { perf::TraceScope traceMotionD530(__FILE__, __LINE__); beastToNode(beast_, beastsNode, opt); }
            } else {
        perf::TraceScope traceMotionD538(__FILE__, __LINE__);
                beastToNode(beast_, beastsNode, opt);
            }
        }
    }
    return node;
}

lib::Owner<Motion> Motion::createFromNode(XNode& node, const XMLInfo& ver) {
    perf::TraceScope traceMotionD547(__FILE__, __LINE__);
    static_this();
    const auto type = mTypeFromName(node.attr("type", true));
    auto result = std::make_shared<Motion>(
        type, types::toElement(node.attr("element", true)));
    result->comment(node.attr("comment", false, lib::String{}));
    const auto d = result->detail();
    if (d.use(MArg::ValueType)) {
        result->damageType(types::toDamageType(
            node.attr(d.attr(MArg::ValueType), true)));
    }
    if (d.use(MArg::UValue)) {
        const auto value = node.attr<std::uint32_t>(d.attr(MArg::UValue), true);
        result->uValue(static_cast<std::int32_t>(value));
    }
    if (d.use(MArg::AValue)) {
        result->aValue(node.attr<std::int32_t>(d.attr(MArg::AValue), true));
    }
    if (d.use(MArg::Round)) {
        const auto value = node.attr<std::uint32_t>(d.attr(MArg::Round), true);
        result->round(static_cast<std::int32_t>(value));
    }
    if (d.use(MArg::Beast)) {
        perf::TraceScope traceMotionD558(__FILE__, __LINE__);
        node.onTag["Beasts"] = [result, ver](XNode& beastsNode) { // 保持するD形式のコールバックは、ファクトリーの状態を所有する。ver.sysは借用のまま。
    perf::TraceScope traceMotionD559(__FILE__, __LINE__);
            // DはここでmaxNestInitからintを推論
            // する。負値は受け付け、uintの設定関数で0へ丸
            // める。int32範囲外は不正なXMLとして扱う。
            const auto maxNest = beastsNode.attr<std::int32_t>(
                "maxNest", false,
                static_cast<std::int32_t>(Motion::maxNest_init));
            result->maxNest(static_cast<std::uint32_t>(maxNest));
            beastsNode.onTag["BeastCard"] = [result, ver](XNode& beastNode) { // 外側のコールバックの寿命とは独立して、両方の値を所有する。
    perf::TraceScope traceMotionD562(__FILE__, __LINE__);
                static_cast<void>(result->setBeastFromNode(beastNode, ver));
            };
            beastsNode.parse();
        };
        node.parse();
    }
    return result;
}

lib::String Motion::cwxPath(bool id) {
    perf::TraceScope traceMotionD573(__FILE__, __LINE__);
    const auto lifetime = owner_lifetime_.lock();
    if (!lifetime || !lifetime->alive.load(std::memory_order_acquire)) return {};
    auto owner = owner_.lock();
    if (!owner && owner_raw_) {
        const auto retainedPath = owner_raw_->retainForCallback();
        owner = std::dynamic_pointer_cast<MotionOwner>(retainedPath);
    }
    MotionOwner* ownerRaw = owner ? owner.get() : owner_raw_;
    if (!ownerRaw) return {};

    const auto allMotions = ownerRaw->motions();
    std::size_t index = 0U;
    for (; index < allMotions.size(); ++index) {
        if (allMotions[index].get() == this) break;
    }
    // DのcountUntilは見つからなければ-1を返し、cpjoinがsize_t
    // へ変換する。有効な配列末尾のインデックスに置き換えず、その番兵値を維持する。
    if (index == allMotions.size()) index = (std::numeric_limits<std::size_t>::max)();
    return path::cpjoin(ownerRaw->cwxPath(id), "motion", index);
}

lib::Owner<CwxPath> Motion::findCWXPath(std::string_view value) {
    perf::TraceScope traceMotionD576(__FILE__, __LINE__);
    if (path::cpempty(value)) {
        try {
            return std::static_pointer_cast<CwxPath>(shared_from_this());
        } catch (const std::bad_weak_ptr&) {
            return {};
        }
    }

    const auto category = path::cpcategory(value);
    if (category == "beastcard") {
    perf::TraceScope traceMotionD580(__FILE__, __LINE__);
        return path::cpindex(value) == 0U && beast_
                   ? beastFindPath(beast_, path::cpbottom(value))
                   : lib::Owner<CwxPath>{};
    }
    if (category == "beastcard:id") {
    perf::TraceScope traceMotionD584(__FILE__, __LINE__);
        return beast_ && beastId(beast_) == path::cpindex(value)
                   ? beastFindPath(beast_, path::cpbottom(value))
                   : lib::Owner<CwxPath>{};
    }
    return {};
}

lib::Array<lib::Observer<CwxPath>> Motion::cwxChilds() {
    perf::TraceScope traceMotionD593(__FILE__, __LINE__);
    lib::Array<lib::Observer<CwxPath>> result;
    if (const auto child = beastPathOwner(beast_)) result.emplace_back(child);
    return result;
}

lib::Observer<CwxPath> Motion::cwxParent() {
    const auto lifetime = owner_lifetime_.lock();
    if (!lifetime || !lifetime->alive.load(std::memory_order_acquire)) return {};
    if (const auto owner = owner_.lock()) return owner;
    if (owner_raw_) {
        // 共有モデルは既存の制御ブロックを提供する。生ポインター
        // ・スタック上の所有元は、親への弱い監視参照を返さない。
        if (const auto retainedPath = owner_raw_->retainForCallback()) {
            return retainedPath;
        }
    }
    return {};
}

void Motion::attachOwner(MotionOwner* raw,
                          const lib::Owner<MotionOwner>& owner) {
    owner_raw_ = raw;
    auto sharedOwner = owner;
    if (!sharedOwner && raw) {
        const auto retainedPath = raw->retainForCallback();
        sharedOwner = std::dynamic_pointer_cast<MotionOwner>(retainedPath);
    }
    owner_ = sharedOwner;
    owner_lifetime_ = raw ? raw->motionLifetimeToken()
                          : std::weak_ptr<MotionOwnerLifetime>{};
}

}  // namespace cwxeditor::cwx::motion
