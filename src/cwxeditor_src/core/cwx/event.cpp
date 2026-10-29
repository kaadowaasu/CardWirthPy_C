#include "cwxeditor_src/core/cwx/event.hpp"

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/expression.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

#include <atomic>
#include <bit>
#include <charconv>
#include <cctype>
#include <initializer_list>
#include <limits>
#include <set>
#include <sstream>
#include <tuple>

namespace cwxeditor::cwx::event {
namespace {

using types::AbsorbTo;

EventTreePtr checkedEventTree(const EventTreePtr& value) {
    // Dはこれらのイベント参照を参照する。nullのネイティブポインターは
    // その時点で失敗させ、空のツリーや何もしない成功に変えてはならない。
    if (!value) throw std::invalid_argument("Null event tree");
    return value;
}

lib::Array<EventTreePtr> snapshotEventTrees(
    const lib::SharedSlice<EventTreePtr>& trees) {
    lib::Array<EventTreePtr> result;
    result.reserve(trees.size());
    for (const auto& tree : trees) result.push_back(tree);
    return result;
}

ChangeHandler eventTreeOwnerChangeHandler(EventTreeOwner* owner) {
    if (!owner) return {};
    // 共有モデルは実際の制御ブロックを公開する。Dのデリゲートが呼び出し中の
    // 受信先を保持するのと同様に、コールバックから所有元を生存させられる。
    if (auto retained = std::dynamic_pointer_cast<EventTreeOwner>(
            owner->retainForCallback())) {
        lib::Observer<EventTreeOwner> observer = retained;
        return [observer] {
            if (const auto live = observer.lock()) live->changed();
        };
    }
    // スタック上のモデルは、自身の寿命トークンが有効な間は同期動作を維持する。取
    // り外されたツリーはこのクロージャーを保持するが、無効な所有元は参照しない。
    const auto lifetime = owner->eventTreeLifetimeToken();
    return [owner, lifetime] {
        const auto live = lifetime.lock();
        if (live && live->alive.load(std::memory_order_acquire)) owner->changed();
    };
}

/// スタック所有のEventTreeOwner用のパスハンドル。
/// 所有元トークンが有効な間の同期転送を維持し、生アドレスに対し
/// て何もしない解放処理を持つshared_ptrを公開しない。
class EventTreeOwnerPathProxy final : public CWXPath {
public:
    EventTreeOwnerPathProxy(
        CWXPath* target,
        std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept
        : target_(target), lifetime_(std::move(lifetime)) {}

    [[nodiscard]] lib::Owner<CWXPath> retainForCallback() override {
        const auto live = access();
        return live ? live.target->retainForCallback() : lib::Owner<CWXPath>{};
    }
    [[nodiscard]] String cwxPath(bool id) override {
        const auto live = access();
        return live ? live.target->cwxPath(id) : String{};
    }
    [[nodiscard]] lib::Owner<CWXPath> findCWXPath(StringView value) override {
        const auto live = access();
        return live ? live.target->findCWXPath(value) : lib::Owner<CWXPath>{};
    }
    [[nodiscard]] lib::Array<lib::Observer<CWXPath>> cwxChilds() override {
        const auto live = access();
        return live ? live.target->cwxChilds()
                    : lib::Array<lib::Observer<CWXPath>>{};
    }
    [[nodiscard]] lib::Observer<CWXPath> cwxParent() override {
        const auto live = access();
        return live ? live.target->cwxParent() : lib::Observer<CWXPath>{};
    }
    void changed() override {
        const auto live = access();
        if (live) live.target->changed();
    }

private:
    struct LiveAccess {
        std::shared_ptr<path::OwnerLifetimeToken> lifetime;
        lib::Owner<CWXPath> retained;
        CWXPath* target{};
        [[nodiscard]] explicit operator bool() const noexcept {
            return target != nullptr;
        }
    };

    [[nodiscard]] LiveAccess access() const {
        if (!target_) return {};
        auto lifetime = lifetime_.lock();
        if (!lifetime || !lifetime->alive.load(std::memory_order_acquire)) return {};
        // このパスが共有された外側の所有元に属する場合は、転送中に実際の制
        // 御ブロックを保持する。スタック上の所有元はトークンで保護する。
        auto retained = target_->retainForCallback();
        if (!lifetime->alive.load(std::memory_order_acquire)) return {};
        return {std::move(lifetime), std::move(retained), target_};
    }

    CWXPath* target_{};
    std::weak_ptr<path::OwnerLifetimeToken> lifetime_;
};

ContentPtr checkedContent(const ContentPtr& value) {
    // Dは子の参照をたどり、null項目を黙って飛ばさない。
    if (!value) throw EventException("Null content");
    return value;
}

void validateContentParent(const Content& child, const ContentPtr& parent) {
    if (parent && parent.get() == &child) throw EventException("Content cannot be its own parent");
    if (parent && !parent->detail().owner) throw EventException("Content parent cannot own children");
    if (child.type() == CType::Start) throw EventException("Start content cannot have a parent");
}

template <typename FirstRange, typename SecondRange>
bool sameObjectValues(const FirstRange& first, const SecondRange& second) {
    if (first.size() != second.size()) return false;
    for (std::size_t i = 0; i < first.size(); ++i) {
        if (!first[i] || !second[i]) { if (first[i] != second[i]) return false; continue; }
        if constexpr (requires { first[i]->opEquals(*second[i]); }) {
            if (!first[i]->opEquals(*second[i])) return false;
        } else if (!first[i]->equals(*second[i])) return false;
    }
    return true;
}

background::BgImageArray snapshotBackgroundImages(
    const lib::SharedSlice<background::BgImagePtr>& images) {
    background::BgImageArray result;
    result.reserve(images.size());
    for (const auto& image : images) result.push_back(image);
    return result;
}

// DのSDialog.changed()はクーポンの利用者に対して何もしない。この動
// 作を維持しつつ、C++の値を扱うAPIでは名前変更の成功後に値をキャッシュする。
class DialogCouponUser final : public usecounter::CouponUser {
public:
    using usecounter::CouponUser::CouponUser;
    lib::Callback<void(const String&)> synchronize;
    void changed() override {}
    void acceptedValue(const String& value) override {
        if (synchronize) synchronize(value);
    }
};

// 単一値の名前変更とその取り消しをまたいで、登録された同一性を維持する。弱参照
// のコールバックはContentを生存させず、置換された所有元にも到達しない。
template<class User>
class ContentStringUser final : public User {
public:
    using User::User;
    lib::Callback<void()> notify;
    lib::Callback<void(const String&)> publish;
    void changed() override { if (notify) notify(); }
    void acceptedValue(const String& value) override { if (publish) publish(value); }
};

// 単一値・配列のCouponは内部のテキスト編集を公開する。分岐ラベルはContentStringUser
// 固有の、内部キャッシュとID全体・名前との契約の違いを維持する。
class ContentCouponProducer final : public usecounter::CouponUser {
public:
    using usecounter::CouponUser::CouponUser;
    lib::Callback<void()> notify;
    lib::Callback<void(const String&)> publish;
    void changed() override { if (notify) notify(); }
    void acceptedValue(const String& value) override { if (publish) publish(value); }
    void acceptedTextValue(const String& value) override {
        if (publish) publish(value);
    }
};

template<class User, class ID>
class ContentNumericUser final : public User {
public:
    using User::User;
    lib::Callback<void()> notify;
    lib::Callback<void(std::uint64_t)> publish;
    void changed() override { if (notify) notify(); }
    void acceptedNumericValue(std::uint64_t value) override {
        if (publish) publish(value);
    }
};

template <typename T>
String numberText(T value) {
    if constexpr (std::is_same_v<T, bool>) return value ? "true" : "false";
    else if constexpr (std::is_same_v<T, String>) return value;
    else if constexpr (std::is_convertible_v<T, StringView>) return String(value);
    else return std::to_string(value);
}

// Dのイベント書き込み処理はfromBoolを明示的に使う。汎用の型付きXMLのboolは小文字のままにする。
String boolText(bool value) { return value ? "True" : "False"; }

// Dは直接入力されたラベルも含め、分岐の番兵値を大文字小文字を区別せずに扱う。
bool isDefaultBranchName(StringView value) {
    constexpr StringView sentinel = "default";
    return value.size() == sentinel.size() && std::equal(value.begin(), value.end(), sentinel.begin(),
        [](unsigned char left, char right) { return std::tolower(left) == right; });
}

bool parseBool(StringView value, bool = false) {
    String lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "true" || lower == "1") return true;
    if (lower == "false" || lower == "0") return false;
    throw std::invalid_argument("invalid boolean value: " + String(value));
}

template <typename T>
T integerText(StringView value, T = {}) {
    T result{};
    const auto* first = value.data();
    const auto* last = first + value.size();
    const auto converted = std::from_chars(first, last, result);
    if (converted.ec != std::errc{} || converted.ptr != last) {
        throw std::invalid_argument("invalid integer value: " + String(value));
    }
    return result;
}

std::uint64_t branchLabelId(StringView value) {
    // Dのto!ulongは先頭のプラスを許可するが、from_charsは許可しない。
    if (value.starts_with('+')) value.remove_prefix(1);
    return integerText<std::uint64_t>(value);
}

template <typename T>
String enumText(T value) {
    return numberText(static_cast<std::underlying_type_t<T>>(value));
}
String enumText(types::Mental value) { return types::fromMental(value); }
String enumText(types::Physical value) { return types::fromPhysical(value); }
String enumText(types::Status value) { return types::fromStatus(value); }
String enumText(types::Range value) { return types::fromRange(value); }
String enumText(types::CardVisual value) { return types::fromCardVisual(value); }
String enumText(types::EffectType value) { return types::fromEffectType(value); }
String enumText(types::Resist value) { return types::fromResist(value); }
String enumText(types::Transition value) { return types::fromTransition(value); }
String enumText(types::Comparison4 value) { return types::fromComparison4(value); }
String enumText(types::Comparison3 value) { return types::fromComparison3(value); }
String enumText(types::CoordinateType value) { return types::fromCoordinateType(value); }
String enumText(types::SelectionMethod value) { return types::fromSelectionMethod(value); }
String enumText(types::StartAction value) { return types::fromStartAction(value); }
String enumText(types::MatchingType value) { return types::fromMatchingType(value); }
String enumText(types::MatchingCondition value) { return types::fromMatchingCondition(value); }
String enumText(types::EnvironmentStatus value) { return types::fromEnvironmentStatus(value); }
String enumText(types::AbsorbTo value) { return types::fromAbsorbTo(value); }
String enumText(types::CastRange value) { return numberText(static_cast<int>(value)); }
String enumText(types::Target value) { return types::fromTarget(value); }

template <typename T>
T enumParse(StringView value, T fallback) {
    using U = std::underlying_type_t<T>;
    (void)fallback;
    return static_cast<T>(integerText<U>(value));
}
template <typename T>
T enumParse(StringView value, T fallback, T (*parser)(StringView)) {
    (void)fallback;
    return parser(value);
}

String talkerText(types::Talker value) { return types::fromTalker(value); }
types::Talker talkerParse(StringView value) {
    if (value == "Selected") return types::Talker::Selected;
    if (value == "Unselected") return types::Talker::Unselected;
    if (value == "Random") return types::Talker::Random;
    if (value == "Card") return types::Talker::Card;
    if (value == "Valued") return types::Talker::Valued;
    return types::Talker::Selected;
}

struct DetailTable {
    lib::OrderedDictionary<CType, CDetail> values;
};

DetailTable makeDetails() {
    DetailTable table;
    const auto add = [&](CType c, std::initializer_list<String> names,
                         String type, CNextType next, bool owner,
                         std::initializer_list<std::pair<CArg, String>> args = {}) {
        CDetail d;
        d.names.assign(names.begin(), names.end());
        d.type = std::move(type);
        d.nextType = next;
        d.owner = owner;
        for (const auto& [arg, attr] : args) d.args.emplace(arg, attr);
        table.values.emplace(c, std::move(d));
    };
    add(CType::Start, {"Start"}, "", CNextType::None, true);
    add(CType::StartBattle, {"Start"}, "Battle", CNextType::None, false, {{CArg::Battle, "id"}});
    add(CType::End, {"End"}, "", CNextType::None, false, {{CArg::Complete, "complete"}});
    add(CType::EndBadEnd, {"End"}, "BadEnd", CNextType::None, false);
    add(CType::ChangeArea, {"Change"}, "Area", CNextType::None, false,
        {{CArg::Area, "id"}, {CArg::Transition, "transition"}, {CArg::TransitionSpeed, "transitionspeed"}});
    add(CType::ChangeBgImage, {"Change"}, "BgImage", CNextType::None, true,
        {{CArg::BgImages, ""}, {CArg::Transition, "transition"}, {CArg::TransitionSpeed, "transitionspeed"}});
    add(CType::Effect, {"Effect"}, "", CNextType::None, true,
        {{CArg::SignedLevel, "level"}, {CArg::Range, "targetm"}, {CArg::EffectType, "effecttype"},
         {CArg::Resist, "resisttype"}, {CArg::SuccessRate, "successrate"}, {CArg::CardVisual, "visual"},
         {CArg::SoundPath, "sound"}, {CArg::SoundVolume, "volume"}, {CArg::SoundLoopCount, "loopcount"},
         {CArg::InitialEffect, "initialeffect"}, {CArg::InitialSoundPath, "initialsound"},
         {CArg::InitialSoundVolume, "initialvolume"}, {CArg::InitialSoundLoopCount, "initialloopcount"},
         {CArg::Ignite, "ignite"}, {CArg::HoldingCoupon, "holdingcoupon"}, {CArg::RefAbility, "refability"},
         {CArg::Physical, "physical"}, {CArg::Mental, "mental"}, {CArg::CardSpeed, "cardspeed"},
         {CArg::OverrideCardSpeed, "overridecardspeed"}, {CArg::AbsorbTo, "absorbto"},
         {CArg::KeyCodes, ""}, {CArg::Motions, ""}});
    add(CType::EffectBreak, {"Effect"}, "Break", CNextType::None, false, {{CArg::ConsumeCard, "consumecard"}});
    add(CType::LinkStart, {"Link"}, "Start", CNextType::None, false, {{CArg::Start, "link"}});
    add(CType::LinkPackage, {"Link"}, "Package", CNextType::None, false, {{CArg::Package, "link"}});
    add(CType::TalkMessage, {"Talk"}, "Message", CNextType::Text, true,
        {{CArg::TalkerC, ""}, {CArg::Text, ""}, {CArg::SelectionColumns, "columns"},
         {CArg::BoundaryCheck, "boundarycheck"}, {CArg::CenteringX, "centeringx"},
         {CArg::CenteringY, "centeringy"}, {CArg::SelectTalker, "selecttalker"}, {CArg::SingleLine, "singleline"}});
    add(CType::TalkDialog, {"Talk"}, "Dialog", CNextType::Text, true,
        {{CArg::TalkerNC, "targetm"}, {CArg::Dialogs, ""}, {CArg::Coupons, ""}, {CArg::InitValue, "initialValue"},
         {CArg::SelectionColumns, "columns"}, {CArg::BoundaryCheck, "boundarycheck"},
         {CArg::CenteringX, "centeringx"}, {CArg::CenteringY, "centeringy"}, {CArg::SelectTalker, "selecttalker"}});
    add(CType::PlayBgm, {"Play"}, "Bgm", CNextType::None, true,
        {{CArg::BgmPath, "path"}, {CArg::BgmChannel, "channel"}, {CArg::BgmVolume, "volume"},
         {CArg::BgmLoopCount, "loopcount"}, {CArg::BgmFadeIn, "fadein"}});
    add(CType::PlaySound, {"Play"}, "Sound", CNextType::None, true,
        {{CArg::SoundPath, "path"}, {CArg::SoundChannel, "channel"}, {CArg::SoundVolume, "volume"},
         {CArg::SoundLoopCount, "loopcount"}, {CArg::SoundFadeIn, "fadein"}});
    add(CType::Wait, {"Wait"}, "", CNextType::None, true, {{CArg::Wait, "value"}});
    add(CType::ElapseTime, {"Elapse"}, "Time", CNextType::None, true);
    add(CType::CallStart, {"Call"}, "Start", CNextType::None, true, {{CArg::Start, "call"}});
    add(CType::CallPackage, {"Call"}, "Package", CNextType::None, true, {{CArg::Package, "call"}});
    add(CType::BranchFlag, {"Branch"}, "Flag", CNextType::Bool, true, {{CArg::Flag, "flag"}});
    add(CType::BranchMultiStep, {"Branch"}, "MultiStep", CNextType::Step, true, {{CArg::Step, "step"}});
    add(CType::BranchStep, {"Branch"}, "Step", CNextType::Bool, true, {{CArg::Step, "step"}, {CArg::StepValue, "value"}});
    add(CType::BranchSelect, {"Branch"}, "Select", CNextType::Bool, true,
        {{CArg::TargetAll, "targetall"}, {CArg::SelectionMethod, ""}, {CArg::Coupons, ""}, {CArg::InitValue, "initialValue"}});
    add(CType::BranchAbility, {"Branch"}, "Ability", CNextType::Bool, true,
        {{CArg::TargetS, "targetm"}, {CArg::Mental, "mental"}, {CArg::Physical, "physical"},
         {CArg::SignedLevel, "value"}, {CArg::InvertResult, "invert"}});
    add(CType::BranchRandom, {"Branch"}, "Random", CNextType::Bool, true, {{CArg::Percent, "value"}});
    add(CType::BranchLevel, {"Branch"}, "Level", CNextType::Bool, true, {{CArg::Average, "average"}, {CArg::UnsignedLevel, "value"}});
    add(CType::BranchStatus, {"Branch"}, "Status", CNextType::Bool, true, {{CArg::Range, "targetm"}, {CArg::Status, "status"}, {CArg::InvertResult, "invert"}});
    add(CType::BranchPartyNumber, {"Branch"}, "PartyNumber", CNextType::Bool, true, {{CArg::PartyNumber, "value"}});
    add(CType::BranchArea, {"Branch"}, "Area", CNextType::IdArea, true);
    add(CType::BranchBattle, {"Branch"}, "Battle", CNextType::IdBattle, true);
    add(CType::BranchIsBattle, {"Branch"}, "IsBattle", CNextType::Bool, true);
    add(CType::BranchCast, {"Branch"}, "Cast", CNextType::Bool, true, {{CArg::Cast, "id"}});
    add(CType::BranchItem, {"Branch"}, "Item", CNextType::Bool, true,
        {{CArg::Item, "id"}, {CArg::Range, "targets"}, {CArg::CardNumber, "number"}, {CArg::SelectCard, "selectcard"}, {CArg::InvertResult, "invert"}});
    add(CType::BranchSkill, {"Branch"}, "Skill", CNextType::Bool, true,
        {{CArg::Skill, "id"}, {CArg::Range, "targets"}, {CArg::CardNumber, "number"}, {CArg::SelectCard, "selectcard"}, {CArg::InvertResult, "invert"}});
    add(CType::BranchInfo, {"Branch"}, "Info", CNextType::Bool, true, {{CArg::Info, "id"}});
    add(CType::BranchBeast, {"Branch"}, "Beast", CNextType::Bool, true,
        {{CArg::Beast, "id"}, {CArg::Range, "targets"}, {CArg::CardNumber, "number"}, {CArg::SelectCard, "selectcard"}, {CArg::InvertResult, "invert"}});
    add(CType::BranchMoney, {"Branch"}, "Money", CNextType::Bool, true, {{CArg::Money, "value"}});
    add(CType::BranchCoupon, {"Branch"}, "Coupon", CNextType::Bool, true,
        {{CArg::Range, "targets"}, {CArg::CouponNames, ""}, {CArg::MatchingType, "matchingtype"}, {CArg::InvertResult, "invert"}, {CArg::ExpandSPChars, "spchars"}});
    add(CType::BranchCompleteStamp, {"Branch"}, "CompleteStamp", CNextType::Bool, true, {{CArg::CompleteStamp, "scenario"}});
    add(CType::BranchGossip, {"Branch"}, "Gossip", CNextType::Bool, true, {{CArg::Gossip, "gossip"}, {CArg::ExpandSPChars, "spchars"}});
    add(CType::SetFlag, {"Set"}, "Flag", CNextType::None, true, {{CArg::Flag, "flag"}, {CArg::FlagValue, "value"}, {CArg::CardSpeed, "cardspeed"}, {CArg::OverrideCardSpeed, "overridecardspeed"}});
    add(CType::SetStep, {"Set"}, "Step", CNextType::None, true, {{CArg::Step, "step"}, {CArg::StepValue, "value"}});
    add(CType::SetStepUp, {"Set"}, "StepUp", CNextType::None, true, {{CArg::Step, "step"}});
    add(CType::SetStepDown, {"Set"}, "StepDown", CNextType::None, true, {{CArg::Step, "step"}});
    add(CType::ReverseFlag, {"Reverse"}, "Flag", CNextType::None, true, {{CArg::Flag, "flag"}, {CArg::CardSpeed, "cardspeed"}, {CArg::OverrideCardSpeed, "overridecardspeed"}});
    add(CType::CheckFlag, {"Check"}, "Flag", CNextType::None, true, {{CArg::Flag, "flag"}});
    add(CType::GetCast, {"Get"}, "Cast", CNextType::None, true, {{CArg::Cast, "id"}, {CArg::StartAction, "startaction"}});
    add(CType::GetItem, {"Get"}, "Item", CNextType::None, true, {{CArg::Item, "id"}, {CArg::Range, "targets"}, {CArg::CardNumber, "number"}});
    add(CType::GetSkill, {"Get"}, "Skill", CNextType::None, true, {{CArg::Skill, "id"}, {CArg::Range, "targets"}, {CArg::CardNumber, "number"}});
    add(CType::GetInfo, {"Get"}, "Info", CNextType::None, true, {{CArg::Info, "id"}});
    add(CType::GetBeast, {"Get"}, "Beast", CNextType::None, true, {{CArg::Beast, "id"}, {CArg::Range, "targets"}, {CArg::CardNumber, "number"}});
    add(CType::GetMoney, {"Get"}, "Money", CNextType::None, true, {{CArg::Money, "value"}});
    add(CType::GetCoupon, {"Get"}, "Coupon", CNextType::None, true, {{CArg::Coupon, "coupon"}, {CArg::Range, "targets"}, {CArg::CouponValue, "value"}, {CArg::HoldingCoupon, "holdingcoupon"}, {CArg::ExpandSPChars, "spchars"}});
    add(CType::GetCompleteStamp, {"Get"}, "CompleteStamp", CNextType::None, true, {{CArg::CompleteStamp, "scenario"}});
    add(CType::GetGossip, {"Get"}, "Gossip", CNextType::None, true, {{CArg::Gossip, "gossip"}, {CArg::ExpandSPChars, "spchars"}});
    add(CType::LoseCast, {"Lose"}, "Cast", CNextType::None, true, {{CArg::Cast, "id"}});
    add(CType::LoseItem, {"Lose"}, "Item", CNextType::None, true, {{CArg::Item, "id"}, {CArg::Range, "targets"}, {CArg::CardNumber, "number"}});
    add(CType::LoseSkill, {"Lose"}, "Skill", CNextType::None, true, {{CArg::Skill, "id"}, {CArg::Range, "targets"}, {CArg::CardNumber, "number"}});
    add(CType::LoseInfo, {"Lose"}, "Info", CNextType::None, true, {{CArg::Info, "id"}});
    add(CType::LoseBeast, {"Lose"}, "Beast", CNextType::None, true, {{CArg::Beast, "id"}, {CArg::Range, "targets"}, {CArg::CardNumber, "number"}});
    add(CType::LoseMoney, {"Lose"}, "Money", CNextType::None, true, {{CArg::Money, "value"}});
    add(CType::LoseCoupon, {"Lose"}, "Coupon", CNextType::None, true, {{CArg::Coupon, "coupon"}, {CArg::Range, "targets"}, {CArg::HoldingCoupon, "holdingcoupon"}, {CArg::ExpandSPChars, "spchars"}});
    add(CType::LoseCompleteStamp, {"Lose"}, "CompleteStamp", CNextType::None, true, {{CArg::CompleteStamp, "scenario"}});
    add(CType::LoseGossip, {"Lose"}, "Gossip", CNextType::None, true, {{CArg::Gossip, "gossip"}, {CArg::ExpandSPChars, "spchars"}});
    add(CType::ShowParty, {"Show"}, "Party", CNextType::None, true, {{CArg::CardSpeed, "cardspeed"}});
    add(CType::HideParty, {"Hide"}, "Party", CNextType::None, true, {{CArg::CardSpeed, "cardspeed"}});
    add(CType::Redisplay, {"Redisplay"}, "", CNextType::None, true, {{CArg::Transition, "transition"}, {CArg::TransitionSpeed, "transitionspeed"}});
    add(CType::SubstituteStep, {"Substitute", "Sbustitute"}, "Step", CNextType::None, true, {{CArg::Step, "from"}, {CArg::Step2, "to"}});
    add(CType::SubstituteFlag, {"Substitute", "Sbustitute"}, "Flag", CNextType::None, true, {{CArg::Flag, "from"}, {CArg::Flag2, "to"}, {CArg::CardSpeed, "cardspeed"}, {CArg::OverrideCardSpeed, "overridecardspeed"}});
    add(CType::BranchStepCmp, {"Branch"}, "StepValue", CNextType::Trio, true, {{CArg::Step, "from"}, {CArg::Step2, "to"}});
    add(CType::BranchFlagCmp, {"Branch"}, "FlagValue", CNextType::Bool, true, {{CArg::Flag, "from"}, {CArg::Flag2, "to"}});
    add(CType::BranchRandomSelect, {"Branch"}, "RandomSelect", CNextType::Bool, true, {{CArg::CastRange, ""}, {CArg::LevelMin, "minLevel"}, {CArg::LevelMax, "maxLevel"}, {CArg::Status, "status"}, {CArg::InvertResult, "invert"}});
    add(CType::BranchKeyCode, {"Branch"}, "KeyCode", CNextType::Bool, true, {{CArg::KeyCodeRange, "targetkc"}, {CArg::TargetIsSkill, "skill"}, {CArg::TargetIsItem, "item"}, {CArg::TargetIsBeast, "beast"}, {CArg::TargetIsHand, "hand"}, {CArg::KeyCode, "keyCode"}, {CArg::SelectCard, "selectcard"}, {CArg::InvertResult, "invert"}, {CArg::MatchingCondition, "condition"}});
    add(CType::CheckStep, {"Check"}, "Step", CNextType::None, true, {{CArg::Step, "step"}, {CArg::StepValue, "value"}, {CArg::Comparison4, "comparison"}});
    add(CType::BranchRound, {"Branch"}, "Round", CNextType::Bool, true, {{CArg::Round, "round"}, {CArg::Comparison3, "comparison"}});
    add(CType::MoveBgImage, {"Move"}, "BgImage", CNextType::None, true, {{CArg::CellName, "cellname"}, {CArg::PositionType, "positiontype"}, {CArg::X, "x"}, {CArg::Y, "y"}, {CArg::SizeType, "sizetype"}, {CArg::Width, "width"}, {CArg::Height, "height"}, {CArg::Transition, "transition"}, {CArg::TransitionSpeed, "transitionspeed"}, {CArg::DoAnime, "doanime"}, {CArg::IgnoreEffectBooster, "ignoreeffectbooster"}});
    add(CType::ReplaceBgImage, {"Replace"}, "BgImage", CNextType::None, true, {{CArg::CellName, "cellname"}, {CArg::BgImages, ""}, {CArg::Transition, "transition"}, {CArg::TransitionSpeed, "transitionspeed"}, {CArg::DoAnime, "doanime"}, {CArg::IgnoreEffectBooster, "ignoreeffectbooster"}});
    add(CType::LoseBgImage, {"Lose"}, "BgImage", CNextType::None, true, {{CArg::CellName, "cellname"}, {CArg::Transition, "transition"}, {CArg::TransitionSpeed, "transitionspeed"}, {CArg::DoAnime, "doanime"}, {CArg::IgnoreEffectBooster, "ignoreeffectbooster"}});
    add(CType::BranchMultiCoupon, {"Branch"}, "MultiCoupon", CNextType::Coupon, true, {{CArg::Range, "targets"}, {CArg::ExpandSPChars, "spchars"}});
    add(CType::BranchMultiRandom, {"Branch"}, "MultiRandom", CNextType::None, true);
    add(CType::MoveCard, {"Move"}, "Card", CNextType::None, true, {{CArg::CardGroup, "cardgroup"}, {CArg::PositionType, "positiontype"}, {CArg::X, "x"}, {CArg::Y, "y"}, {CArg::Scale, "scale"}, {CArg::Layer, "layer"}, {CArg::CardSpeed, "cardspeed"}, {CArg::OverrideCardSpeed, "overridecardspeed"}});
    add(CType::ChangeEnvironment, {"Change"}, "Environment", CNextType::None, true, {{CArg::BackpackEnabled, "backpack"}, {CArg::GameOverEnabled, "gameover"}, {CArg::RunAwayEnabled, "runaway"}});
    add(CType::BranchVariant, {"Branch"}, "Variant", CNextType::Bool, true, {{CArg::Expression, ""}});
    add(CType::SetVariant, {"Set"}, "Variant", CNextType::None, true, {{CArg::Variant, "variant"}, {CArg::Flag, "flag"}, {CArg::Step, "step"}, {CArg::Expression, ""}});
    add(CType::CheckVariant, {"Check"}, "Variant", CNextType::None, true, {{CArg::Expression, ""}});
    add(CType::CustomXmlContent, {"XmlContent"}, "Custom", CNextType::None, true, {{CArg::XmlType, "xmlType"}, {CArg::XmlStr, "str"}, {CArg::XmlAssignment, "assignment"}});
    return table;
}

const DetailTable& detailTable() {
    static const DetailTable table = makeDetails();
    return table;
}

const lib::OrderedDictionary<CTypeGroup, lib::Array<CType>>& groupTable() {
    static const lib::OrderedDictionary<CTypeGroup, lib::Array<CType>> table = [] {
        lib::OrderedDictionary<CTypeGroup, lib::Array<CType>> result;
        const auto add = [&](CTypeGroup group, std::initializer_list<CType> values) {
            result.emplace(group, lib::Array<CType>(values));
        };
        add(CTypeGroup::Terminal, {CType::Start, CType::StartBattle, CType::End, CType::EndBadEnd, CType::ChangeArea, CType::EffectBreak, CType::LinkStart, CType::LinkPackage});
        add(CTypeGroup::Standard, {CType::TalkMessage, CType::TalkDialog, CType::PlayBgm, CType::PlaySound, CType::Wait, CType::ElapseTime, CType::Effect, CType::ChangeEnvironment, CType::CallStart, CType::CallPackage});
        add(CTypeGroup::Data, {CType::BranchFlag, CType::SetFlag, CType::ReverseFlag, CType::BranchMultiStep, CType::BranchStep, CType::SetStep, CType::SetStepUp, CType::SetStepDown, CType::SubstituteStep, CType::SubstituteFlag, CType::BranchStepCmp, CType::BranchFlagCmp, CType::CheckFlag, CType::CheckStep});
        add(CTypeGroup::Utility, {CType::BranchSelect, CType::BranchAbility, CType::BranchRandom, CType::BranchMultiRandom, CType::BranchLevel, CType::BranchStatus, CType::BranchPartyNumber, CType::BranchArea, CType::BranchBattle, CType::BranchIsBattle, CType::BranchRandomSelect, CType::BranchRound});
        add(CTypeGroup::Branch, {CType::BranchCast, CType::BranchItem, CType::BranchSkill, CType::BranchInfo, CType::BranchBeast, CType::BranchMoney, CType::BranchCoupon, CType::BranchMultiCoupon, CType::BranchCompleteStamp, CType::BranchGossip, CType::BranchKeyCode});
        add(CTypeGroup::Get, {CType::GetCast, CType::GetItem, CType::GetSkill, CType::GetInfo, CType::GetBeast, CType::GetMoney, CType::GetCoupon, CType::GetCompleteStamp, CType::GetGossip});
        add(CTypeGroup::Lost, {CType::LoseCast, CType::LoseItem, CType::LoseSkill, CType::LoseInfo, CType::LoseBeast, CType::LoseMoney, CType::LoseCoupon, CType::LoseCompleteStamp, CType::LoseGossip});
        add(CTypeGroup::Visual, {CType::ShowParty, CType::HideParty, CType::MoveCard, CType::ChangeBgImage, CType::MoveBgImage, CType::ReplaceBgImage, CType::LoseBgImage, CType::Redisplay});
        add(CTypeGroup::Variant, {CType::BranchVariant, CType::SetVariant, CType::CheckVariant, CType::CustomXmlContent});
        return result;
    }();
    return table;
}

const lib::Array<CTypeGroup>& groupOrder() {
    static const lib::Array<CTypeGroup> result{CTypeGroup::Terminal, CTypeGroup::Visual, CTypeGroup::Standard, CTypeGroup::Data, CTypeGroup::Utility, CTypeGroup::Branch, CTypeGroup::Get, CTypeGroup::Lost, CTypeGroup::Variant};
    return result;
}

const lib::OrderedDictionary<String, lib::Dictionary<String, CType>>& typeMap() {
    static const lib::OrderedDictionary<String, lib::Dictionary<String, CType>> result = [] {
        lib::OrderedDictionary<String, lib::Dictionary<String, CType>> map;
        for (const auto& [type, detail] : detailTable().values) {
            for (const auto& name : detail.names) map[name][detail.type] = type;
        }
        return map;
    }();
    return result;
}

String attrName(const CDetail& detail, CArg arg) {
    // XML項目の配送は省略可能だが、公開されたD互換の検索自体は厳
    // 密に行う。引数が存在しない場合を空の属性に変えてはならない。
    return detail.use(arg) ? detail.attr(arg) : String{};
}

template <typename T>
void putAttr(XNode& node, const CDetail& detail, CArg arg, const T& value,
             bool omitDefault = false, const T& defaultValue = T{}) {
    const auto name = attrName(detail, arg);
    if (name.empty() || (omitDefault && value == defaultValue)) return;
    if constexpr (std::is_same_v<T, bool>) {
        // 先頭を大文字にするのは、DでfromBoolを明示的に使う書き込み処理だけ。RefAbility
        // と今後の通常の型付きbool属性は、汎用XMLの表記を維持する。
        switch (arg) {
        case CArg::InitialEffect:
        case CArg::TargetAll:
        case CArg::Average:
        case CArg::Complete:
        case CArg::FlagValue:
        case CArg::DoAnime:
        case CArg::IgnoreEffectBooster:
        case CArg::CenteringX:
        case CArg::CenteringY:
        case CArg::BoundaryCheck:
        case CArg::Ignite:
        case CArg::SelectCard:
        case CArg::SelectTalker:
        case CArg::InvertResult:
        case CArg::ConsumeCard:
        case CArg::OverrideCardSpeed:
        case CArg::ExpandSPChars:
        case CArg::SingleLine:
        case CArg::TargetIsSkill:
        case CArg::TargetIsItem:
        case CArg::TargetIsBeast:
        case CArg::TargetIsHand:
            node.newAttr(name, boolText(value)); break;
        default:
            node.newAttr(name, numberText(value)); break;
        }
    }
    else if constexpr (std::is_enum_v<T>) node.newAttr(name, enumText(value));
    else node.newAttr(name, numberText(value));
}

void putAttr(XNode& node, const CDetail& detail, CArg arg, StringView value,
             bool omitDefault = false, StringView defaultValue = {}) {
    const auto name = attrName(detail, arg);
    if (name.empty() || (omitDefault && value == defaultValue)) return;
    node.newAttr(name, value);
}

template <typename T, typename Parser, typename Setter>
bool readProperty(const XNode& node, const CDetail& detail, CArg arg,
                  Parser parser, Setter setter, const T* defaultValue) {
    if (!detail.use(arg)) return false;
    const auto name = detail.attr(arg);
    if (node.hasAttr(name)) {
        setter(parser(node.attr(name, true)));
        return true;
    }
    if (!defaultValue) return false;
    auto value = *defaultValue;
    setter(std::move(value));
    return true;
}

template <typename T, typename Setter, typename Parser>
bool readAttr(const XNode& node, const CDetail& detail, CArg arg, const T&,
              Setter setter, Parser parser, T defaultValue, bool defaultWhenAbsent = false) {
    return readProperty<T>(node, detail, arg, parser, setter,
                           defaultWhenAbsent ? &defaultValue : nullptr);
}

template <typename T, typename Setter>
bool readNumeric(const XNode& node, const CDetail& detail, CArg arg, const T&,
                 Setter setter, T defaultValue = {}) {
    // DのcfnPutDは、XML属性がない場合もこれらの既定値を渡す。
    bool hasDefault = true;
    switch (arg) {
    case CArg::InitialSoundVolume: defaultValue = T{100}; break;
    case CArg::InitialSoundLoopCount:
    case CArg::SelectionColumns: defaultValue = T{1}; break;
    case CArg::Scale: case CArg::Layer: defaultValue = static_cast<T>(-1); break;
    case CArg::InitialSoundChannel: case CArg::InitialSoundFadeIn:
    case CArg::X: case CArg::Y: case CArg::Width: case CArg::Height: defaultValue = T{}; break;
    default: hasDefault = false; break;
    }
    return readProperty<T>(node, detail, arg, [](StringView value) {
        const auto parsed = utils::detail::parseDInteger<T>(value);
        if (!parsed) throw std::invalid_argument("Invalid D integer attribute");
        return *parsed;
    }, setter, hasDefault ? &defaultValue : nullptr);
}

template <typename Current, typename Setter>
bool readString(const XNode& node, const CDetail& detail, CArg arg, const Current&,
                Setter setter, StringView defaultValue = {}, bool defaultWhenAbsent = false) {
    if (arg == CArg::CellName || arg == CArg::CardGroup ||
        ((arg == CArg::Flag || arg == CArg::Step || arg == CArg::Variant) &&
         detail.use(CArg::Flag) && detail.use(CArg::Step) && detail.use(CArg::Variant)))
        defaultWhenAbsent = true;
    using Value = std::conditional_t<std::is_same_v<Current, lib::NullableString>,
                                     lib::NullableString, String>;
    // Dの三種類の変数の読み込みは、非nullの空文字列リテラルを明示的に渡
    // す。汎用の欠落値・既定値のビューは、それぞれのnull状態を維持する。
    const auto fallbackView = [&] {
        if constexpr (std::is_same_v<Value, lib::NullableString>) {
            if ((arg == CArg::Flag || arg == CArg::Step || arg == CArg::Variant) &&
                detail.use(CArg::Flag) && detail.use(CArg::Step) && detail.use(CArg::Variant))
                return StringView("");
        }
        return defaultValue;
    }();
    const Value fallback(fallbackView);
    return readProperty<Value>(node, detail, arg, [arg](StringView value) -> Value {
        if (arg == CArg::BgmPath || arg == CArg::SoundPath || arg == CArg::InitialSoundPath)
            return Value(utils::decodePath(String(value)));
        return Value(value);
    }, setter, defaultWhenAbsent ? &fallback : nullptr);
}

}  // namespace

namespace {

template <typename Receiver, typename Scenario>
auto resolveConnectedResource(Receiver& content, Scenario* scenario)
    -> lib::Owner<std::conditional_t<std::is_const_v<Receiver> || std::is_const_v<Scenario>,
                                     const CWXPath, CWXPath>> {
    using Result = lib::Owner<std::conditional_t<std::is_const_v<Receiver> || std::is_const_v<Scenario>,
                                                const CWXPath, CWXPath>>;
    // Dのクラス値の引数を、ネイティブの共有制御ブロックで保持する。
    // 追加のスタック・生ポインターの引数は、呼び出し側が生存させる。
    [[maybe_unused]] const auto receiver_alive = content.weak_from_this().lock();
    [[maybe_unused]] const auto scenario_alive = scenario
        ? scenario->weak_from_this().lock() : lib::Owner<Scenario>{};
    const auto detail = content.detail();
    const auto requiredScenario = [&]() -> Scenario& {
        if (!scenario) throw std::invalid_argument("Null connected-resource summary");
        return *scenario;
    };
    const auto variable = [&]<typename T>(auto readName) -> Result {
        // C++の引数評価順に依存せず、Dの明示的
        // な引数順とクラス参照の寿命を維持する。
        const auto root = requiredScenario().flagDirRoot();
        const auto counter = content.useCounter();
        const auto name = readName();
        return flag::findVar<T>(root, counter, name);
    };
    if (detail.use(CArg::Area)) return requiredScenario().area(content.area());
    if (detail.use(CArg::Battle)) return requiredScenario().battle(content.battle());
    if (detail.use(CArg::Package)) return requiredScenario().cwPackage(content.packages());
    if (detail.use(CArg::Flag))
        return variable.template operator()<flag::Flag>([&] { return content.flag(); });
    if (detail.use(CArg::Step))
        return variable.template operator()<flag::Step>([&] { return content.step(); });
    if (detail.use(CArg::Variant))
        return variable.template operator()<flag::Variant>([&] { return content.variant(); });
    if (detail.use(CArg::Cast)) return requiredScenario().cwCast(content.casts());
    if (detail.use(CArg::Item)) return requiredScenario().item(content.item());
    if (detail.use(CArg::Skill)) return requiredScenario().skill(content.skill());
    if (detail.use(CArg::Beast)) return requiredScenario().beast(content.beast());
    if (detail.use(CArg::Info)) return requiredScenario().info(content.info());
    if (detail.use(CArg::Start)) {
        const auto tree = content.tree();
        return tree ? Result(tree->start(content.start())) : Result{};
    }
    if (detail.use(CArg::Flag2))
        return variable.template operator()<flag::Flag>([&] { return content.flag2(); });
    if (detail.use(CArg::Step2))
        return variable.template operator()<flag::Step>([&] { return content.step2(); });
    return {};
}

} // namespace

class Content::StartReferenceUser final : public usecounter::StartUser {
public:
    using usecounter::StartUser::StartUser;
    void assign(StringView value) {
        // DではContent自身が開始カウンターの利用者で、設定関数は
        // 通知済み。C++アダプターの登録を維持しつつ、二重通知しない。
        struct Restore { bool& flag; bool previous; ~Restore() { flag = previous; } } restore{assigning_, assigning_};
        assigning_ = true;
        start(value);
    }
    void changed() override { if (!assigning_) usecounter::StartUser::changed(); }
private:
    bool assigning_{};
};

bool CDetail::use(CArg arg) const { return args.find(arg) != args.end(); }
String CDetail::attr(CArg arg) const { return args.at(arg); }
CDetail CDetail::opCall(String name, String type, CNextType nextType, bool owner) {
    return opCall(lib::Array<String>{std::move(name)}, std::move(type), nextType, owner, {});
}
CDetail CDetail::opCall(String name, String type, CNextType nextType, bool owner,
                        lib::Dictionary<CArg, String> args) {
    return opCall(lib::Array<String>{std::move(name)}, std::move(type), nextType, owner, std::move(args));
}
CDetail CDetail::opCall(lib::Array<String> names, String type, CNextType nextType,
                        bool owner, lib::Dictionary<CArg, String> args) {
    return CDetail{std::move(names), std::move(type), nextType, owner, std::move(args)};
}
CDetail CDetail::fromType(CType type) { return contentDetail(type); }

const lib::Array<CTypeGroup>& CTYPE_GROUPS_LIST() { return groupOrder(); }
const lib::OrderedDictionary<CTypeGroup, lib::Array<CType>>& CTYPE_GROUP() { return groupTable(); }
const lib::OrderedDictionary<CType, CDetail>& CONTENT_DETAILS() { return detailTable().values; }
CDetail contentDetail(CType type) {
    const auto found = detailTable().values.find(type);
    if (found == detailTable().values.end()) throw EventException("Invalid content type");
    return found->second;
}
const lib::OrderedDictionary<String, lib::Dictionary<String, CType>>& CTYPE_MAP() { return typeMap(); }
CType cTypeFrom(StringView name, StringView type) {
    const auto foundName = typeMap().find(String(name));
    if (foundName == typeMap().end()) throw EventException("Invalid content name: " + String(name));
    const auto foundType = foundName->second.find(String(type));
    if (foundType == foundName->second.end()) throw EventException("Invalid content type: " + String(name) + String(type));
    return foundType->second;
}
bool isBranchContent(CType type) {
    if (type == CType::BranchMultiRandom) return true;
    const auto d = contentDetail(type);
    return d.nextType != CNextType::None && d.nextType != CNextType::Text;
}
StartId toStartId(StringView start) { return usecounter::toStartId(start); }

SDialog::SDialog(String text, lib::Array<String> requiredCoupons)
    : text_(std::move(text)),
      text_holder_(std::make_shared<textholder::TextHolder>(
          usecounter::detail::nonOwning(static_cast<CWXPath*>(this)))),
      required_coupons_(std::move(requiredCoupons)) {
    text_holder_->changeHandler([this] {
        // Dは保持オブジェクトを直接読む。通常のカウンター名変更でも保存されるよ
        // うに、親へ通知する前にC++の値・インデックスのキャッシュを同期する。
        text_ = text_holder_->text();
        reindexText();
        changed();
    });
    text_holder_->text(text_);
    reindexText();
    rebuildCouponUsers(required_coupons_);
}
SDialog::SDialog(const SDialog& other)
    : text_(other.text_),
      text_holder_(std::make_shared<textholder::TextHolder>(
          usecounter::detail::nonOwning(static_cast<CWXPath*>(this)))),
      required_coupons_(other.required_coupons_) {
    text_holder_->changeHandler([this] {
        // Dは保持オブジェクトを直接読む。通常のカウンター名変更でも保存されるよ
        // うに、親へ通知する前にC++の値・インデックスのキャッシュを同期する。
        text_ = text_holder_->text();
        reindexText();
        changed();
    });
    text_holder_->text(text_);
    reindexText();
    rebuildCouponUsers(required_coupons_);
}
SDialog::~SDialog() {
    const usecounter::detail::NativeCounterRetirement native_retirement;
    if (text_holder_) text_holder_->changeHandler({});
    for (const auto& user : coupon_users_)
        if (auto coupon = std::dynamic_pointer_cast<DialogCouponUser>(user))
            coupon->synchronize = {};
    removeUseCounter();
}

void SDialog::rebuildCouponUsers(const lib::Array<String>& values) {
    lib::Array<lib::Owner<usecounter::CouponUser>> next;
    next.reserve(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        auto user = std::make_shared<DialogCouponUser>(this);
        user->coupon(values[index]);
        user->synchronize = [this, index](const String& value) {
            required_coupons_[index] = value;
        };
        next.push_back(std::move(user));
    }
    // Dは置換内容を構築する前に古いクーポンを解除し、不正なテキ
    // ストで利用者が一部だけ残る場合がある。C++では意図的に置
    // 換一覧を先に検証し、既存の値とカウンターの寿命を維持する。
    for (const auto& user : coupon_users_) if (user) {
        if (auto coupon = std::dynamic_pointer_cast<DialogCouponUser>(user))
            coupon->synchronize = {};
        user->removeUseCounter();
    }
    required_coupons_ = values;
    coupon_users_ = std::move(next);
    for (const auto& user : coupon_users_)
        if (use_counter_) user->setUseCounter(use_counter_, uc_owner_);
}

ContentPtr SDialog::parent() const { return parent_.lock(); }
void SDialog::parent(const ContentPtr& value) { parent_ = value; }
void SDialog::setUseCounter(const lib::Owner<UseCounter>& counter,
                            const lib::Owner<CWXPath>& owner) {
    if (text_holder_) text_holder_->setUseCounter(counter, owner);
    use_counter_ = counter;
    for (const auto& user : coupon_users_) if (user) user->setUseCounter(counter, owner);
    uc_owner_ = usecounter::renameInfo(owner);
}
void SDialog::removeUseCounter() noexcept {
    if (text_holder_) text_holder_->removeUseCounter();
    for (const auto& user : coupon_users_) if (user) user->removeUseCounter();
    use_counter_.reset();
    uc_owner_.reset();
}
lib::Owner<SDialog> SDialog::dup() const { return std::make_shared<SDialog>(*this); }
bool SDialog::opEquals(const SDialog& other) const noexcept {
    return text_ == other.text_ && required_coupons_ == other.required_coupons_;
}
void SDialog::text(StringView value) {
    if (text_ == value) return;
    String next(value);
    // Dは保持オブジェクトの検証前に親へ通知する。解析失敗の場合も同様。
    if (auto p = parent()) p->changed();
    if (text_holder_) text_holder_->text(next);
    text_ = std::move(next);
    reindexText();
}
void SDialog::rCoupons(lib::Array<String> value) {
    if (required_coupons_ == value) return;
    if (auto p = parent()) p->changed();
    rebuildCouponUsers(value);
}
void SDialog::changed() {}
void SDialog::reindexText() {
    if (!text_holder_) {
        text_flags_.clear();
        text_steps_.clear();
        text_variants_.clear();
        text_fonts_.clear();
        text_colors_.clear();
        msgutils::textUseItems(text_, text_flags_, text_steps_, text_variants_,
                               text_fonts_, text_colors_);
        return;
    }
    text_flags_ = text_holder_->flagsInText();
    text_steps_ = text_holder_->stepsInText();
    text_variants_ = text_holder_->variantsInText();
    text_fonts_ = text_holder_->fontsInText();
    text_colors_ = text_holder_->colorsInText();
}
lib::Array<String> SDialog::fontsInText() const { return text_holder_ ? text_holder_->fontsInText() : text_fonts_; }
lib::Array<String> SDialog::flagsInText() const { return text_holder_ ? text_holder_->flagsInText() : text_flags_; }
lib::Array<String> SDialog::stepsInText() const { return text_holder_ ? text_holder_->stepsInText() : text_steps_; }
lib::Array<String> SDialog::variantsInText() const { return text_holder_ ? text_holder_->variantsInText() : text_variants_; }
std::vector<char> SDialog::colorsInText() const { return text_holder_ ? text_holder_->colorsInText() : text_colors_; }
void SDialog::changeInText(std::size_t index, StringView id) {
    changePathInText(index, id);
}
void SDialog::changeInText(std::size_t index, const usecounter::PathId& id) {
    if (text_holder_) text_holder_->changeInText(index, id);
    text_ = text_holder_ ? text_holder_->text() : text_;
    reindexText();
}
void SDialog::changeFlagInText(std::size_t index, StringView id) {
    if (text_holder_) text_holder_->changeFlagInText(index, id);
    text_ = text_holder_ ? text_holder_->text() : text_;
    reindexText();
}
void SDialog::changeStepInText(std::size_t index, StringView id) {
    if (text_holder_) text_holder_->changeStepInText(index, id);
    text_ = text_holder_ ? text_holder_->text() : text_;
    reindexText();
}
void SDialog::changeVariantInText(std::size_t index, StringView id) {
    if (text_holder_) text_holder_->changeVariantInText(index, id);
    text_ = text_holder_ ? text_holder_->text() : text_;
    reindexText();
}
void SDialog::changePathInText(std::size_t index, StringView id) {
    if (text_holder_) text_holder_->changePathInText(index, id);
    text_ = text_holder_ ? text_holder_->text() : text_;
    reindexText();
}
XNode SDialog::toNode() const {
    auto node = XNode::create(XML_NAME);
    toNodeImpl(node);
    return node;
}
void SDialog::toNode(XNode& dialogsNode) const {
    // Dは親がDialogsであることを必要とする。任意のルートを
    // 親として扱わず、ネイティブの例外で同じ失敗の境界を維持する。
    if (dialogsNode.name() != "Dialogs") throw EventException("Expected Dialogs parent");
    auto node = dialogsNode.newElement(XML_NAME);
    toNodeImpl(node);
}
void SDialog::toNodeImpl(XNode& node) const {
    if (node.name() != XML_NAME) throw EventException("Expected Dialog node");
    node.newElement("RequiredCoupons", utils::encodeLf(required_coupons_, true));
    node.newElement("Text", utils::encodeLf(text_));
}
lib::Owner<SDialog> SDialog::createFromNode(XNode& source, const system::XMLInfo*) {
    // Dはタグハンドラーを登録する前に、ルートがDialogであることをアサートする。
    if (source.name() != XML_NAME) throw EventException("Expected Dialog node");
    struct Parsed { String text; lib::Array<String> coupons; };
    // Dのクロージャーはこれらのローカル値を保持する。例外で解析が終わり、呼び
    // 出し側の再試行用に有効な登録が残った場合も、共有状態を同様に有効に保つ。
    const auto parsed = std::make_shared<Parsed>();
    source.setLiveHandler("RequiredCoupons", [parsed](XNode& child) {
        parsed->coupons = utils::decodeLf(child.value());
    });
    source.setLiveHandler("Text", [parsed](XNode& child) {
        parsed->text = utils::decodeLf2(child.value());
    });
    source.parseLive();
    return std::make_shared<SDialog>(parsed->text, parsed->coupons);
}
lib::Owner<SDialog> SDialog::createFromNode(const XNode& source, const system::XMLInfo* version) {
    // 追加のネイティブ読み取り専用APIの呼び出し側では、ノードハンド
    // ルを適応させる。解析規則は同じ可変のD互換登録経路に集約する。
    auto node = source;
    return createFromNode(node, version);
}
String SDialog::cwxPath(bool id) {
    if (auto p = parent()) {
        const auto& ds = p->dialogs();
        // DのcCountUntilは存在しない場合に-1を返し、cpjoinはsize_tを受け取る。
        const auto index = static_cast<std::size_t>(utils::cCountUntil(ds, this,
            [](const auto& value, const auto* self) { return value.get() == self; }));
        return path::cpjoin(p, "dialog", index, id);
    }
    return {};
}
lib::Owner<CWXPath> SDialog::findCWXPath(StringView value) {
    if (path::cpempty(value))
        return std::static_pointer_cast<CWXPath>(shared_from_this());
    if (path::cpcategory(value) == "text") {
        if (path::cpindex(value) > 0 || !text_holder_) return {};
        return text_holder_->findCWXPath(path::cpbottom(value));
    }
    return {};
}
lib::Array<lib::Observer<CWXPath>> SDialog::cwxChilds() {
    return text_holder_ ? text_holder_->cwxChilds()
                        : lib::Array<lib::Observer<CWXPath>>{};
}
lib::Observer<CWXPath> SDialog::cwxParent() { return parent(); }

Content::Content(CType type, String name) {
    // Dはコンストラクターでthisを渡せる。C++では共有所有権
    // が成立した後の公開接続・移動処理で、実際の弱参照の所有元を設
    // 定する。解放しない別のContent制御ブロックは作らない。
    static std::atomic<std::uint64_t> nextId{0};
    // DのIDはobjectIDValueに、コンストラクター内のulongカウンター
    // を続けたもの。ほかの所有元と同じく、共通のネイティブ時刻・アドレス表現を使う。
    id_ = utils::objectIDValue(this) + "-" +
        std::to_string(nextId.fetch_add(1, std::memory_order_relaxed));
    type_ = type;
    name_holder_ = std::make_shared<textholder::SimpleTextHolder>(
        lib::Owner<CWXPath>{}, textholder::TextHolderType::SimpleText, "name");
    name_holder_->changeHandler([this] {
        name_ = name_holder_->text();
        // トークンのコールバックで、現在名前変更中の利用者を作り直してはならない。
        notifyBeforeAssignment();
    });
    // Dは初期テキストの代入前にchangedを接続する。ネイティブの書き込み前
    // コールバックが保持オブジェクトの旧テキストを公開する間、入力を保持する。
    name_holder_->text(name);
    name_ = name_holder_->text();
    if (type_ == CType::TalkDialog) initValue_ = 1;
    if (contentDetail(type_).use(CArg::Coupon) ||
        contentDetail(type_).use(CArg::CouponNames) ||
        type_ == CType::BranchMultiCoupon) {
        range_ = types::Range::Selected;
    }
    validate();
}
Content::~Content() {
    const usecounter::detail::NativeCounterRetirement native_retirement;
    invalidateMotionLifetime();
    if (background_lifetime_)
        background_lifetime_->alive.store(false, std::memory_order_release);
    callback_lifetime_.reset();
    detachMotionPayloads(); detachBackgroundImages(); removeUseCounter();
    retireUseCounterUsers();
    removeStartUseCounter();
    name_holder_->retire();
    if (message_holder_) message_holder_->retire();
    // C++の名前変更の取り消しは、Contentの破棄後もこの保持オブジェクトを残せる
    // 。DではGCが所有元を生存させるが、C++ではその境界でコールバックを無効にする。
    if (xml_text_holder_) xml_text_holder_->retire();
    const auto retireCoupon = [](const lib::Owner<usecounter::CouponUser>& user) {
        if (!user) return;
        if (auto typed = std::dynamic_pointer_cast<ContentCouponProducer>(user)) {
            typed->publish = {}; typed->notify = {};
        }
        user->retireTextProducer();
        user->owner(static_cast<CWXPath*>(nullptr));
    };
    retireCoupon(coupon_user_);
    if (coupon_names_producer_) coupon_names_producer_->retire();
    if (expression_tracker_) {
        expression_tracker_->changeHandler({});
        expression_tracker_->weakOwner({});
    }
    if (gossip_user_) {
        if (auto user = std::dynamic_pointer_cast<ContentStringUser<usecounter::GossipUser>>(gossip_user_)) {
            user->notify = {};
            user->publish = {};
        }
        gossip_user_->retireTextProducer();
        gossip_user_->owner(static_cast<CWXPath*>(nullptr));
    }
}

void Content::assignName(String value) {
    name_holder_->text(value);
    name_ = std::move(value);
}
void Content::bindTextProducerOwners() const {
    if (auto self = weak_from_this().lock()) {
        auto owner = std::static_pointer_cast<CWXPath>(std::const_pointer_cast<Content>(self));
        name_holder_->owner(owner);
        if (message_holder_) message_holder_->owner(owner);
        if (gossip_user_) gossip_user_->owner(owner);
        if (coupon_user_) coupon_user_->owner(owner);
        if (coupon_names_producer_) coupon_names_producer_->owner(owner);
        if (expression_tracker_) expression_tracker_->weakOwner(owner);
    }
}
void Content::bindNameCounter() {
    bindNameCounter(use_counter_, uc_owner_);
}
void Content::bindNameCounter(const lib::Owner<UseCounter>& bindingCounter,
                              const lib::Owner<CWXPath>& bindingOwner) {
    bindTextProducerOwners();
    const auto p = parent();
    if (!bindingCounter) name_holder_->removeUseCounter();
    else if (p && (p->detail().nextType == CNextType::Text ||
        (p->detail().nextType == CNextType::Coupon && p->expandSPChars())))
        name_holder_->setUseCounter(bindingCounter, bindingOwner);
    // Dは非nullへの再接続で、通知のない保持オブジェクトを飛ばす。このContent
    // に新しく渡されたカウンターと異なる場合も、従来のカウンターと範囲を維持する。
}
bool Content::readsNameTokens() const {
    const auto p = parent();
    return p && (p->detail().nextType == CNextType::Text ||
        (p->type() == CType::CustomXmlContent &&
         (p->xmlType() == "TalkMessage" || p->xmlType() == "TalkDialog")));
}
std::vector<char> Content::namesInName() const {
    ::cwxeditor::cwx::perf::TraceScope trace_event_d2726(__FILE__, __LINE__);
    bindTextProducerOwners();
    return readsNameTokens() ? name_holder_->namesInText() : std::vector<char>{};
}
lib::Array<String> Content::flagsInName() const {
    ::cwxeditor::cwx::perf::TraceScope trace_event_d2732(__FILE__, __LINE__);
    bindTextProducerOwners();
    return readsNameTokens() ? name_holder_->flagsInText() : lib::Array<String>{};
}
lib::Array<String> Content::stepsInName() const {
    ::cwxeditor::cwx::perf::TraceScope trace_event_d2738(__FILE__, __LINE__);
    bindTextProducerOwners();
    return readsNameTokens() ? name_holder_->stepsInText() : lib::Array<String>{};
}
lib::Array<String> Content::variantsInName() const {
    ::cwxeditor::cwx::perf::TraceScope trace_event_d2744(__FILE__, __LINE__);
    bindTextProducerOwners();
    return readsNameTokens() ? name_holder_->variantsInText() : lib::Array<String>{};
}
void Content::ensureCouponProducer() {
    if (coupon_user_) return;
    auto user = std::make_shared<ContentCouponProducer>();
    user->publish = [this](const String& value) { coupon_ = value; };
    user->notify = [this] {
        if (!rebuilding_use_counter_ && change_) change_();
    };
    coupon_user_ = user;
    bindTextProducerOwners();
    user->expandSPChars(expandSPChars_);
    if (use_counter_) user->setUseCounter(use_counter_, uc_owner_);
}
void Content::coupon(String value) {
    PropertyValidation validate{*this};
    ensureCouponProducer();
    if (coupon_user_->coupon() == value) return;
    // Prop(New CouponUser)は、生成元自身の書き込み前通知より先に通知する。
    if (!rebuilding_use_counter_ && change_) change_();
    coupon_user_->coupon(value);
    coupon_ = coupon_user_->coupon();
}
void Content::synchronizeCouponNames() {
    const auto keep_alive = weak_from_this().lock();
    if (!coupon_names_producer_) {
        coupon_names_producer_ = std::make_shared<coupon::CouponNamesUser>(
            coupon::CouponNamesUser::NativeBorrowedOwner{}, lib::Owner<CWXPath>{});
        coupon_names_producer_->changeHandler([this] {
            if (!rebuilding_use_counter_ && change_) change_();
        });
        coupon_names_producer_->acceptedValueHandler([this](std::size_t index, const String& value) {
            if (index < coupon_names_.size()) coupon_names_[index] = value;
        });
        coupon_names_producer_->expandSPChars(expandSPChars_);
        bindTextProducerOwners();
        if (use_counter_) coupon_names_producer_->setUseCounter(use_counter_, uc_owner_);
    }
    // コンストラクターの検証で、共有所有権の成立前にこの生成元が作られる場合があ
    // る。後の変更境界で接続し直し、子の書き込み前通知がここへ届くようにする。
    bindTextProducerOwners();
    coupon_names_producer_->couponNames(lib::SharedSlice<String>(coupon_names_));
}
void Content::couponNames(lib::SharedSlice<String> value) {
    const auto keep_alive = weak_from_this().lock();
    synchronizeCouponNames();
    // Dは集合から空項目を除く前に、生の入力を比較する。
    if (value == couponNames()) return;
    if (!rebuilding_use_counter_ && change_) change_();
    coupon_names_producer_->couponNames(value);
    coupon_names_ = coupon_names_producer_->couponNames();
}
void Content::ensureExpressionProducer() {
    if (expression_tracker_) return;
    expression_tracker_ = std::make_shared<expression::Expression>();
    expression_tracker_->changeHandler([this] {
        const auto value = expression_tracker_->text();
        // Expression.textは書き込み前の変更を通知し、参照
        // のコールバックも受理した書き込み後の変更を通知する。等価なら
        // キャッシュの公開だけを抑止し、必要な二つの通知は省略しない。
        if (expression_ != value) expression_ = value;
        // 参照更新で、実行中の生成元を作り直してはならない。
        if (!rebuilding_use_counter_ && change_) change_();
    });
    bindTextProducerOwners();
    if (use_counter_) expression_tracker_->setUseCounter(use_counter_, uc_owner_);
}
void Content::expression(String value) {
    PropertyValidation validate{*this};
    ensureExpressionProducer();
    // DのProp(Expression)は、Expression.text
    // の通知や再構築された利用者の通知に加えて、書き込み前通知を持つ。
    if (expression_ != value && !rebuilding_use_counter_ && change_) change_();
    expression_tracker_->text(value);
    expression_ = expression_tracker_->text();
}
const expression::Expression& Content::checkedExpression() const {
    // Dは遅延生成のNewプロパティを直接参照する。未設定の式を、暗黙に生
    // 成した空の保持オブジェクトとして扱わず、ネイティブの失敗を報告する。
    if (!expression_tracker_) throw EventException("Expression has not been initialized");
    return *expression_tracker_;
}
void Content::synchronizeValueProducers() {
    // XML、コピー、型変換の経路で、変更の境界に行う正規
    // 化。追跡対象の読み取り専用の取得処理からは呼ばない。
    if (coupon_user_ || !coupon_.empty()) {
        const auto text = coupon_;
        ensureCouponProducer();
        coupon_user_->coupon(text);
    }
    // Dは使わないNewプロパティを未生成のままにする。検証中に無関係
    // なクーポン集合を作ると展開の変更通知が発生するため、作成しない。
    if (coupon_names_producer_ || !coupon_names_.empty()) synchronizeCouponNames();
    if (expression_tracker_ || !expression_.empty()) {
        const auto text = expression_;
        ensureExpressionProducer();
        expression_tracker_->text(text);
        expression_ = expression_tracker_->text();
    }
}
lib::Array<String> Content::flagsInCoupons() const {
    ::cwxeditor::cwx::perf::TraceScope trace_event_d2751(__FILE__, __LINE__);
    bindTextProducerOwners();
    lib::Array<String> result;
    const auto append = [&](const lib::Array<String>& values) {
        result.insert(result.end(), values.begin(), values.end());
    };
    const auto p = parent();
    if (p && p->detail().nextType == CNextType::Coupon && p->expandSPChars())
        append(name_holder_->flagsInText());
    if (expandSPChars_ && coupon_user_) append(coupon_user_->flagsInText());
    if (expandSPChars_ && coupon_names_producer_)
        append(coupon_names_producer_->flagsInText());
    return result;
}
lib::Array<String> Content::stepsInCoupons() const {
    ::cwxeditor::cwx::perf::TraceScope trace_event_d2761(__FILE__, __LINE__);
    bindTextProducerOwners();
    lib::Array<String> result;
    const auto append = [&](const lib::Array<String>& values) {
        result.insert(result.end(), values.begin(), values.end());
    };
    const auto p = parent();
    if (p && p->detail().nextType == CNextType::Coupon && p->expandSPChars())
        append(name_holder_->stepsInText());
    if (expandSPChars_ && coupon_user_) append(coupon_user_->stepsInText());
    if (expandSPChars_ && coupon_names_producer_)
        append(coupon_names_producer_->stepsInText());
    return result;
}
lib::Array<String> Content::variantsInCoupons() const {
    ::cwxeditor::cwx::perf::TraceScope trace_event_d2771(__FILE__, __LINE__);
    bindTextProducerOwners();
    lib::Array<String> result;
    const auto append = [&](const lib::Array<String>& values) {
        result.insert(result.end(), values.begin(), values.end());
    };
    const auto p = parent();
    if (p && p->detail().nextType == CNextType::Coupon && p->expandSPChars())
        append(name_holder_->variantsInText());
    if (expandSPChars_ && coupon_user_) append(coupon_user_->variantsInText());
    if (expandSPChars_ && coupon_names_producer_)
        append(coupon_names_producer_->variantsInText());
    return result;
}
lib::Array<expression::ExprError> Content::getExpressionErrors(
    const props::CProps* prop, const expression::VariableInfo& info) const {
    ::cwxeditor::cwx::perf::TraceScope trace_event_d2828(__FILE__, __LINE__);
    return checkedExpression().getExpressionErrors(prop, info);
}
expression::VariantVal Content::getExpressionReturnValue(const props::CProps* prop) const {
    ::cwxeditor::cwx::perf::TraceScope trace_event_d2832(__FILE__, __LINE__);
    return checkedExpression().returnValue(prop);
}
lib::Array<String> Content::flagsInExpression() const {
    return checkedExpression().flagsInText();
}
lib::Array<String> Content::stepsInExpression() const {
    return checkedExpression().stepsInText();
}
lib::Array<String> Content::variantsInExpression() const {
    return checkedExpression().variantsInText();
}

lib::Array<String> Content::flagsInGossip() const {
    ::cwxeditor::cwx::perf::TraceScope trace_event_d2782(__FILE__, __LINE__);
    bindTextProducerOwners();
    if (!expandSPChars_) return {};
    if (!gossip_user_) throw EventException("Gossip producer is not initialized");
    return gossip_user_->flagsInText();
}
lib::Array<String> Content::stepsInGossip() const {
    ::cwxeditor::cwx::perf::TraceScope trace_event_d2788(__FILE__, __LINE__);
    bindTextProducerOwners();
    if (!expandSPChars_) return {};
    if (!gossip_user_) throw EventException("Gossip producer is not initialized");
    return gossip_user_->stepsInText();
}
lib::Array<String> Content::variantsInGossip() const {
    ::cwxeditor::cwx::perf::TraceScope trace_event_d2794(__FILE__, __LINE__);
    bindTextProducerOwners();
    if (!expandSPChars_) return {};
    if (!gossip_user_) throw EventException("Gossip producer is not initialized");
    return gossip_user_->variantsInText();
}
void Content::ensureGossipProducer() {
    if (!gossip_user_) {
        auto user = std::make_shared<ContentStringUser<usecounter::GossipUser>>();
        user->publish = [this](const String& value) { gossip_ = value; };
        user->notify = [this] {
            if (!rebuilding_use_counter_ && change_) change_();
        };
        gossip_user_ = user;
        bindTextProducerOwners();
        if (use_counter_) user->setUseCounter(use_counter_, uc_owner_);
    }
}
void Content::gossip(String value) {
    struct ValidateOnExit { Content& content; ~ValidateOnExit() { content.validate(); } } validate{*this};
    ensureGossipProducer();
    if (gossip_ == value) return;
    changed();
    gossip_user_->expandSPChars(expandSPChars_);
    gossip_user_->gossip(value);
}
bool Content::checkExpandSPChars(bool value) {
    if (coupon_user_) coupon_user_->expandSPChars(value);
    if (gossip_user_) gossip_user_->expandSPChars(value);
    if (coupon_names_producer_) coupon_names_producer_->expandSPChars(value);
    // Dのforeachは、生成元のコールバックがネイティブの子配列を置
    // 換しても入力スライスを保持する。この巡回中もその参照を維持する。
    const auto retained = next_;
    for (const auto& entry : retained) {
        const auto child = checkedContent(entry);
        if (child->branch_coupon_user_) child->branch_coupon_user_->expandSPChars(value);
    }
    return true;
}
void Content::expandSPChars(bool value) {
    struct ValidateOnExit { Content& content; ~ValidateOnExit() { content.validate(); } } validate{*this};
    // Dはフラグが未変更でも、等価比較より先にプロパティの検査を行う。
    checkExpandSPChars(value);
    if (expandSPChars_ == value) return;
    if (!rebuilding_use_counter_) notifyBeforeAssignment();
    expandSPChars_ = value;
    // 検査した生成元はDと同様に自身の登録を更新済み。このプ
    // ロパティ変更に対して、無関係な利用者を再登録しない。
}

void Content::changeHandler(ChangeHandler value) {
    // Dは受け取ったデリゲートを単一子の連鎖へ渡す。分岐では、再帰呼び
    // 出しに最初の呼び出し元のchangedデリゲートを渡す。明示的な
    // スタックで、ネイティブの再帰深度を増やさずにこの順序を維持する。
    struct Work { Content* root; ContentPtr retained; ChangeHandler handler; };
    lib::Array<Work> pending{{this, weak_from_this().lock(), std::move(value)}};
    while (!pending.empty()) {
        auto work = std::move(pending.back());
        pending.pop_back();
        if (!work.root) throw EventException("Null content in change handler traversal");
        auto* current = work.root;
        ContentPtr retained;
        while (true) {
            current->change_ = work.handler;
            if (current->next_.size() == 1) {
                retained = checkedContent(current->next_.front());
                current = retained.get();
            } else {
                const auto handler = work.root->changeHandler();
                for (auto it = current->next_.rbegin(); it != current->next_.rend(); ++it)
                    pending.push_back({it->get(), *it, handler});
                break;
            }
        }
    }
}

void Content::notifyBeforeAssignment() {
    // UserBase::setUseCounter()は接
    // 続の変更を所有元のContentへ通知する。シナリオを
    // UseCounterへ接続中に、接続の再構築が再帰的な
    // 再構築や大量のシナリオ変更通知を起こさないようにする。
    if (rebuilding_use_counter_) return;
    const auto keep_alive = weak_from_this().lock();
    if (change_) change_();
    if (type_ == CType::Start) ++update_counter_;
}

void Content::beginOrdinaryFlagStepChange(
    usecounter::detail::OrdinaryFlagStepChangeMarker& marker) noexcept {
    marker.previous = ordinary_flag_step_change_;
    marker.consumed = false;
    ordinary_flag_step_change_ = &marker;
}

void Content::endOrdinaryFlagStepChange(
    usecounter::detail::OrdinaryFlagStepChangeMarker& marker) noexcept {
    if (ordinary_flag_step_change_ == &marker) {
        ordinary_flag_step_change_ = marker.previous;
    } else {
        for (auto* current = ordinary_flag_step_change_; current;
             current = current->previous) {
            if (current->previous == &marker) {
                current->previous = marker.previous;
                break;
            }
        }
    }
    marker.previous = nullptr;
    marker.consumed = false;
}

bool Content::consumeOrdinaryFlagStepChange() noexcept {
    if (!ordinary_flag_step_change_ || ordinary_flag_step_change_->consumed)
        return false;
    ordinary_flag_step_change_->consumed = true;
    return true;
}

void Content::changed() {
    const auto keep_alive = weak_from_this().lock();
    // Userの最初の書き込み前通知だけでこれを消費する。コール
    // バックの再入時は、通常の再構築動作で同じメソッドへ入る。
    const bool suppressRebuild = consumeOrdinaryFlagStepChange();
    notifyBeforeAssignment();
    if (!suppressRebuild && !rebuilding_use_counter_ && use_counter_)
        rebuildUseCounterUsers();
}

void Content::setNameInternal(String value) {
    if (name_ == value) return;
    changed();
    if (type_ == CType::Start) {
        if (auto tree = resolveTree()) {
            tree->start_names_.erase(name_);
            tree->start_names_[value] = shared_from_this();
            tree->start_use_counter_->change(toStartId(name_), toStartId(value), true);
        }
    }
    assignName(std::move(value));
}
void Content::setName(String value) {
    setNameInternal(std::move(value));
    if (auto p = parent()) {
        const auto d = p->detail();
        if (d.nextType == CNextType::IdArea)
            branch_area_condition_ = isDefaultBranchName(name_) ? 0U : branchLabelId(name_);
        if (d.nextType == CNextType::IdBattle)
            branch_battle_condition_ = isDefaultBranchName(name_) ? 0U : branchLabelId(name_);
        if (use_counter_ && (d.nextType == CNextType::IdArea || d.nextType == CNextType::IdBattle))
            rebuildUseCounterUsers();
        if (d.nextType == CNextType::Coupon) {
            branch_coupon_condition_ = name_;
            // setNameInternalで通知済み。安定した分岐項目を同期する。
            if (use_counter_) rebuildUseCounterUsers();
        }
    }
}
void Content::setName(const props::CProps* prop, String value) {
    setName(std::move(value));
    if (prop) if (auto p = parent()) p->normalizeChildName(*prop, shared_from_this());
}
void Content::keyCodes(lib::Array<String> value) {
    if (key_codes_ == value) return;
    [[maybe_unused]] const auto keep_alive = weak_from_this().lock();
    // Dでは外側のProp(New)設定関数と内側のKeyCodesUser設定関
    // 数が、どちらも値の代入前に通知する。そのため監視側には旧一覧が二度見える。
    changed();
    changed();
    key_codes_ = std::move(value);
    if (use_counter_) rebuildUseCounterUsers();
}
void Content::start(String value) {
    if (start_ == value) return;
    changed();
    if (start_user_) start_user_->assign(value);
    start_ = std::move(value);
}
void Content::percent(int value) {
    value = std::clamp(value, 0, 100);
    if (percent_ == value) return;
    changed();
    percent_ = value;
}
void Content::talkerNC(types::Talker value) {
    if (value == types::Talker::Card) throw EventException("Invalid talkerNC");
    if (talkerNC_ == value) return;
    changed();
    talkerNC_ = value;
}
void Content::comment(String value) {
    if (comment_ == value) return;
    changed();
    comment_ = std::move(value);
}
void Content::ensureMessageProducer() {
    if (message_holder_) return;
    message_holder_ = std::make_shared<textholder::TextHolder>(lib::Owner<CWXPath>{}, "text");
    message_holder_->changeHandler([this] {
        text_ = message_holder_->text();
        // Dのトークンコールバックは、受理したテキストを公開する。保持オブジェクト自身の
        // 利用者を再構築せず、Contentの二つの生成元を扱う直接APIも呼ばない。
        if (!rebuilding_use_counter_) {
            if (type_ == CType::Start) ++update_counter_;
            if (change_) change_();
        }
    });
    bindTextProducerOwners();
    if (use_counter_) message_holder_->setUseCounter(use_counter_, uc_owner_);
}
void Content::text(StringView value) {
    struct ValidateOnExit { Content& content; ~ValidateOnExit() { content.validate(); } } validate{*this};
    // 空でも明示的に代入されたプロパティでは、Dの生成元を作成する。
    ensureMessageProducer();
    if (text_ == value) return;
    String next(value); // Dの文字列引数は、書き込み前の監視通知による変更をまたいで有効。
    changed();
    message_holder_->text(next);
    text_ = message_holder_->text();
}
void Content::dialogs(lib::Array<lib::Owner<SDialog>> value) {
    if (sameObjectValues(dialogs_, value)) return;
    // Dは書き込み前コールバックと再帰的なsetValUCsの巡回をまたいで、Content
    // の受信先を生存させる。後で親の競合例外が発生する場合も同様。
    [[maybe_unused]] const auto keepAlive = weak_from_this().lock();
    changed();
    for (auto& dialog : dialogs_) {
        // DのsetValUCs(old)は各SDialogを順に参
        // 照する。先行する各台詞の後処理を維持したうえで、null項
        // 目のアクセス違反を、制御されたネイティブの拒否へ変換する。
        if (!dialog) throw std::invalid_argument("Null dialog in Content.");
        dialog->removeUseCounter();
        // Prop!(SDialog[])はsetValUCs(old)
        // を呼び、古いカウンターの接続を外してから以前の親を解除する。
        dialog->parent({});
    }
    for (auto& dialog : value) {
        // Dの新しい値の順次巡回に合わせる。最初のnull項目で失敗する前
        // に、有効な先頭部分は再接続するが、dialogs_は変更しない。
        if (!dialog) throw std::invalid_argument("Null dialog in Content.");
        // event.dの再帰的なsetValUCsの順序に合わせる。既
        // 存の接続を外し（接続先にカウンターがなければ全接続を外す）、接
        // 続先のカウンターへ接続してから、すでに親を持つ台詞を拒否する。
        if (dialog->useCounter() || !use_counter_) dialog->removeUseCounter();
        if (use_counter_) dialog->setUseCounter(use_counter_, uc_owner_);
        if (dialog->parent()) throw EventException("used other event.");
        dialog->parent(keepAlive);
    }
    dialogs_ = std::move(value);
}
void Content::cardPaths(lib::Array<String> value) {
    lib::Array<lib::Owner<card::CardImage>> images;
    for (const auto& path : value) {
        const auto talker = [&](types::Talker value) {
            const auto suffix = String("??") + types::fromTalker(value);
            return path.size() >= suffix.size() &&
                   path.ends_with(suffix);
        };
        if (talker(types::Talker::Selected))
            images.push_back(std::make_shared<card::CardImage>(types::Talker::Selected));
        else if (talker(types::Talker::Unselected))
            images.push_back(std::make_shared<card::CardImage>(types::Talker::Unselected));
        else if (talker(types::Talker::Random))
            images.push_back(std::make_shared<card::CardImage>(types::Talker::Random));
        else if (talker(types::Talker::Card))
            images.push_back(std::make_shared<card::CardImage>(types::Talker::Card));
        else if (talker(types::Talker::Valued))
            images.push_back(std::make_shared<card::CardImage>(types::Talker::Valued));
        else if (path.starts_with("PC#")) {
            try {
                images.push_back(std::make_shared<card::CardImage>(
                    static_cast<std::uint32_t>(std::stoul(path.substr(3)))));
            } catch (...) {
                images.push_back(std::make_shared<card::CardImage>(path));
            }
        } else {
            images.push_back(std::make_shared<card::CardImage>(path));
        }
    }
    // 追加の文字列表現も、Dの型付き画像設定と同じ規則を使う。
    cardImages(std::move(images));
}
void Content::cardImages(lib::Array<lib::Owner<card::CardImage>> value) {
    const auto previousValues = cardImages();
    if (value.size() == previousValues.size() && std::equal(value.begin(), value.end(), previousValues.begin(),
        [](const auto& a, const auto& b) { return (!a && !b) || (a && b && a->opEquals(*b)); })) return;
    changed();
    struct ValidateOnExit { Content& content; ~ValidateOnExit() { content.validate(); } } validate_on_exit{*this};
    const auto retained = card_images_;
    for (const auto& image : retained) {
        if (!image) throw EventException("Null card image");
        image->removeUseCounter();
        // DのsetValUCs(old)は、置換内容を作る前に
        // 旧CardImageの単一のパス利用者を外す。ネイティ
        // ブのキャッシュアダプターも同じ境界で外し、置換用コンス
        // トラクターの通知から古いパス登録が見えないようにする。
        const auto key = "image/" + std::to_string(
            reinterpret_cast<std::uintptr_t>(image.get()));
        if (const auto adapter = path_users_.find(key); adapter != path_users_.end()) {
            adapter->second->removeUseCounter();
            adapter->second->owner(static_cast<CWXPath*>(nullptr));
            path_users_.erase(adapter);
        }
        path_image_owners_.erase(key);
    }
    card_images_.clear();
    card_paths_.clear();
    card_images_.reserve(value.size());
    for (const auto& image : value) {
        // Dは不正なnull入力も含め、すべての要素でコピーコ
        // ンストラクターを呼ぶ。結果の一覧を黙って短くしない。
        if (!image) throw EventException("Null card image");
        auto copy = std::make_shared<card::CardImage>(static_cast<CWXPath*>(this), *image);
        if (auto self = weak_from_this().lock())
            copy->path_->owner(std::static_pointer_cast<CWXPath>(self));
        if (use_counter_) copy->setUseCounter(use_counter_);
        else copy->removeUseCounter();
        // 追加のネイティブ文字列表現は逐次更新し、後のD形式のコ
        // ピーが失敗した場合も、途中までの一覧を表すようにする。
        if (copy->type == types::CardImageType::File)
            card_paths_.push_back(copy->path());
        else if (copy->type == types::CardImageType::PCNumber)
            card_paths_.push_back("PC#" + std::to_string(copy->pcNumber()));
        else
            card_paths_.push_back("Material/??" + types::fromTalker(copy->talker()));
        card_images_.push_back(std::move(copy));
    }
}
lib::Array<lib::Owner<card::CardImage>> Content::cardImages() const {
    lib::Array<lib::Owner<card::CardImage>> result;
    result.reserve(card_images_.size());
    for (const auto& image : card_images_) {
        if (!image) throw EventException("Null card image");
        result.push_back(std::make_shared<card::CardImage>(static_cast<CWXPath*>(nullptr), *image));
    }
    return result;
}
void Content::backs(lib::Array<String> value) {
    changed();
    unbindBackgroundImagesForReplacement();
    backs_ = std::move(value);
    background::BgImageArray images;
    images.reserve(backs_.size());
    for (const auto& path : backs_) {
        auto image = std::make_shared<background::ImageCell>();
        image->path(path);
        images.push_back(std::move(image));
    }
    backgrounds_ = lib::SharedSlice<background::BgImagePtr>(std::move(images));
    bindBackgroundImages();
}
void Content::backgroundImages(background::BgImageArray value) {
    if (sameObjectValues(backgrounds_, value)) return;
    changed();
    unbindBackgroundImagesForReplacement();
    // DのsetValUCsは、プロパティ代入前に入力の各BgImageを
    // 接続する。この順序を維持し、後続のnullで拒否する場合も、旧配列が
    // 見える状態で有効な先頭部分には新しい所有元・カウンターを設定する。
    for (const auto& image : value)
        bindBackgroundImage(image, use_counter_, uc_owner_);
    backgrounds_ = lib::SharedSlice<background::BgImagePtr>(std::move(value));
    backs_.clear();
    for (const auto& image : backgrounds_) {
        if (const auto cell = std::dynamic_pointer_cast<background::ImageCell>(image))
            backs_.push_back(cell->path());
    }
}
void Content::detachBackgroundImages() noexcept {
    for (const auto& image : backgrounds_) if (image) {
        // SharedSliceの項目を直接置き換えると、旧セルに元の所有
        // 元トークンが残る場合がある。現在もこのContentに属する接続
        // だけを解放し、親が変わったセルには新しい所有元の状態を維持する。
        if (!image->ownedBy(this, backgroundLifetimeToken())) continue;
        image->changeHandler({});
        image->owner(nullptr);
        image->removeUseCounter();
    }
}
void Content::unbindBackgroundImagesForReplacement() {
    for (const auto& image : backgrounds_) {
        // Dの旧値のsetValUCs巡回は、null要素も順に参照する
        // 。先行する旧セルの接続を外した後で、その境界の失敗を報告する。
        if (!image) throw std::invalid_argument("Null background image in Content.");
        // DのsetValUCs(old)はowner/changeHandlerを更新し、
        // カウンターだけを外す。このContentが生存する間は保持された旧セルの接続を維持
        // する。ただしSharedSliceから直接親を変更したセルは新しい所有元に属する。
        if (!image->ownedBy(this, backgroundLifetimeToken())) continue;
        image->owner(this, backgroundLifetimeToken());
        image->changeHandler(changeHandler());
        image->removeUseCounter();
    }
}
void Content::bindBackgroundImages() {
    bindBackgroundImages(use_counter_, uc_owner_);
}
void Content::bindBackgroundImages(
    const lib::Owner<UseCounter>& bindingCounter,
    const lib::Owner<CWXPath>& bindingOwner) {
    for (const auto& image : backgrounds_) if (image) {
        bindBackgroundImage(image, bindingCounter, bindingOwner);
    }
}
void Content::bindBackgroundImage(
    const background::BgImagePtr& image,
    const lib::Owner<UseCounter>& bindingCounter,
    const lib::Owner<CWXPath>& bindingOwner) {
    // setUseCounterImplの再接続経路は、配列のnull
    // 項目を意図的に飛ばす。プロパティの置換は、DのsetValUCs
    // と同じ要素順でnull検査を済ませた後だけ、この補助処理へ進む。
    if (!image) throw std::invalid_argument("Null background image in Content.");
    image->owner(this, backgroundLifetimeToken());
    image->changeHandler(changeHandler());
    if (image->useCounter() || !bindingCounter) image->removeUseCounter();
    if (bindingCounter) image->setUseCounter(bindingCounter, bindingOwner.get());
}
void Content::bindMotionPayloads() {
    bindMotionPayloads(use_counter_, uc_owner_);
}
void Content::bindMotionPayloads(
    const lib::Owner<UseCounter>& bindingCounter,
    const lib::Owner<CWXPath>& bindingOwner) {
    // 利用可能なら、接続先の実際の共有寿命を使う。コンストラクター内やスタック
    // 上の所有元は、同期利用のためにトークンで保護した生の経路だけを保持する。
    const auto self = weak_from_this().lock();
    const auto owner = self ? std::static_pointer_cast<motion::MotionOwner>(self)
                            : lib::Owner<motion::MotionOwner>{};
    for (const auto& item : motions_) if (item) {
        item->attachOwner(this, owner);
        item->changeHandler(changeHandler());
        item->setUseCounter(bindingCounter, bindingOwner);
    }
}

void Content::detachMotionPayloads() noexcept {
    // この配列を作った後でMotionが別の所有元へ移った可能性がある。このContent
    // の所有関係だけを解除し、別の有効な所有元の関係は解除しない。
    for (const auto& item : motions_) {
        if (!item || item->owner_raw_ != this) continue;
        item->changeHandler({});
        item->removeUseCounter();
        item->attachOwner(nullptr, {});
    }
}

void Content::motions(lib::Array<lib::Owner<motion::Motion>> value) {
    motions(lib::SharedSlice<lib::Owner<motion::Motion>>(std::move(value)));
}

void Content::motions(lib::SharedSlice<lib::Owner<motion::Motion>> value) {
    if (sameObjectValues(motions_, value)) return;
    // Dはプロパティ設定全体でContentの受信先を生存させる。呼
    // び出し側の最後の共有所有元をコールバックが解放する場合も含む。
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    changed();
    // DのContentのPropは、MotionUserの内側の設定関数
    // を呼ぶ前に入力のMotion[]を巡回する。この段階では汎用の補助処
    // 理はMotionの接続を変更しない。ここで順に失敗させ、null項
    // 目があれば旧MotionUser配列と現在の所有関係を変更しない。
    for (const auto& item : value) {
        if (!item) throw std::invalid_argument("Null motion in Content.");
    }
    // DのProp設定関数は先にContentへ通知する。その後、内側のMotionUser
    // 設定関数が、旧Motionの接続を外す前に自身の変更コールバックを呼ぶ。
    notifyBeforeAssignment();
    detachMotionPayloads();
    motions_ = std::move(value);
    bindMotionPayloads();
}

void Content::motions(std::initializer_list<lib::Owner<motion::Motion>> value) {
    motions(lib::SharedSlice<lib::Owner<motion::Motion>>(value));
}

void Content::setNumericBranchCondition(std::uint64_t& field, std::uint64_t value, CNextType kind) {
    const auto keep_alive = weak_from_this().lock();
    struct ValidateOnExit { Content& content; ~ValidateOnExit() { content.validate(); } } validate_on_exit{*this};
    if (field == value) return;
    // DのPropと内側の参照Userは、それぞれUserのコールバックより前に通知する。
    notifyBeforeAssignment();
    // Dは外側のコールバック後にPropを再開する。再入ですでに値が設定されていれば
    // 、内側のArea/BattleUserの等価検査がそのコールバックを抑止する。
    if (field == value) return;
    notifyBeforeAssignment();
    changeNumericBranchName(field, value, kind);
    const bool nested_set_requested_value = field == value;
    field = value;
    if (use_counter_) {
        // Dでは二回目のコールバック時点でUserの等価検査を通過済み。外
        // 側の継続処理は、内側の登録の削除・追加を引き続き行う必要がある。
        if (nested_set_requested_value) {
            if (kind == CNextType::IdArea && branch_area_user_)
                rebindCounterUserAlias(usecounter::toAreaId(value), branch_area_user_);
            else if (kind == CNextType::IdBattle && branch_battle_user_)
                rebindCounterUserAlias(usecounter::toBattleId(value), branch_battle_user_);
        }
        rebuildUseCounterUsers();
    }
}
void Content::branchAreaCondition(std::uint64_t value) {
    setNumericBranchCondition(branch_area_condition_, value, CNextType::IdArea);
}
void Content::branchBattleCondition(std::uint64_t value) {
    setNumericBranchCondition(branch_battle_condition_, value, CNextType::IdBattle);
}
void Content::branchCouponCondition(String value) {
    const auto keep_alive = weak_from_this().lock();
    struct ValidateOnExit { Content& content; ~ValidateOnExit() { content.validate(); } } validate_on_exit{*this};
    if (branch_coupon_condition_ == value) return;
    notifyBeforeAssignment();
    // CouponUserも数値分岐と同じ、Dの入口での等価検査を持つ。
    if (branch_coupon_condition_ == value) return;
    notifyBeforeAssignment();
    changeCallback(usecounter::CouponId(branch_coupon_condition_), usecounter::CouponId(value));
    const bool nested_set_requested_value = branch_coupon_condition_ == value;
    branch_coupon_condition_ = std::move(value);
    if (use_counter_) {
        if (nested_set_requested_value && branch_coupon_user_)
            rebindCounterUserAlias(usecounter::toCouponId(branch_coupon_condition_), branch_coupon_user_);
        rebuildUseCounterUsers();
    }
}
void Content::parent(const ContentPtr& value) {
    const auto old = parent_.lock();
    if (old.get() == value.get()) return;
    validateContentParent(*this, value);
    const auto hasKind = [](const ContentPtr& p, CNextType kind) {
        return p && p->detail().nextType == kind;
    };
    const auto branchId = [this] {
        try { return branchLabelId(name_); }
        catch (const std::exception&) {
            (void)utils::printStackTrace();
            // Dは数値ラベル変換の失敗をすべて捕捉し、0を使う。
            return std::uint64_t{};
        }
    };
    // 参照Userのコールバックから旧親が見えるようにする。Dでは接続解除中の
    // コールバックで、分岐名をDefaultや空文字列へ変更する場合がある。
    const auto transition = [&](CNextType kind, auto set) {
        if (!hasKind(old, kind) && hasKind(value, kind)) {
            if (isDefaultBranchName(name_)) set(0U);
            else if (utils::isNumeric(name_)) set(branchId());
        } else if (hasKind(old, kind) && !hasKind(value, kind)) set(0U);
    };
    transition(CNextType::IdArea, [this](std::uint64_t id) { branchAreaCondition(id); });
    transition(CNextType::IdBattle, [this](std::uint64_t id) { branchBattleCondition(id); });
    if (!hasKind(old, CNextType::Coupon) && hasKind(value, CNextType::Coupon))
        branchCouponCondition(name_);
    else if (hasKind(old, CNextType::Coupon) && !hasKind(value, CNextType::Coupon))
        branchCouponCondition({});
    if (branch_coupon_user_) branch_coupon_user_->expandSPChars(value && value->expandSPChars());
    parent_ = value;
    if (use_counter_) rebuildUseCounterUsers();
}
path::OwnerLink<EventTree>::Access Content::resolveTree() const {
    auto link = tree_.lock();
    if (!link || link.shared()) return link;
    auto* owner = link.get();
    if (!owner) return {};
    // コンストラクター内の保護された借用は、make_sharedがEventTree
    // の制御ブロックを設定した後、実際の共有所有元への弱参照に切り替わる。
    if (auto shared = owner->weak_from_this().lock()) {
        tree_.setShared(shared);
        return tree_.lock();
    }
    return link;
}
EventTreePtr Content::tree() const {
    const auto root = parentStart();
    const auto link = root ? root->resolveTree() : resolveTree();
    return link.shared();
}
ContentPtr Content::parentStart() const {
    // Dの最寄りのStartまでの巡回を維持する。長い単一子のイベント
    // 連鎖でネイティブのスタックを使い切らないよう、ループで処理する。
    auto current = std::const_pointer_cast<Content>(shared_from_this());
    while (current) {
        if (current->type_ == CType::Start) return current;
        current = current->parent_.lock();
    }
    return {};
}
lib::Array<std::size_t> Content::ctPath() const {
    lib::Array<std::size_t> result;
    const bool hadParent = static_cast<bool>(parent());
    auto current = std::const_pointer_cast<Content>(shared_from_this());
    while (auto p = current->parent()) {
        const auto index = utils::cCountUntil(p->next_, current);
        // Dは親子の各所属関係をアサートする。不整合
        // な関係で、途中まで構築したパスを返さない。
        if (index < 0) throw EventException("Content is absent from its parent");
        result.push_back(static_cast<std::size_t>(index));
        current = p;
    }
    const auto rootTree = current->resolveTree();
    if (!rootTree) throw EventException("Content has no event tree");
    const auto index = utils::cCountUntil(rootTree->starts(), current);
    // Dは親がない分岐だけでルートへの所属を検査する。親がある分岐でも、
    // countUntilの-1をsize_tへ変換した結果を維持する。
    if (!hadParent && index < 0) throw EventException("Start is absent from its event tree");
    result.push_back(static_cast<std::size_t>(index));
    std::reverse(result.begin(), result.end());
    return result;
}
ContentPtr Content::fromPath(const lib::Array<std::size_t>& pathValue) {
    auto current = std::const_pointer_cast<Content>(shared_from_this());
    for (const auto index : pathValue) {
        // Dは途中のノードを参照し、各インデックスの範囲を検査する。
        // 最後の要素がnullの場合だけ、正常なnull結果とする。
        if (!current) throw EventException("Null intermediate content in path");
        current = current->next_.at(index);
    }
    return current;
}
bool Content::isDescendant(const ContentPtr& value) const {
    auto self = std::const_pointer_cast<Content>(shared_from_this());
    auto current = self;
    while (current) {
        if (current.get() == value.get()) return true;
        current = current->parent();
    }
    return false;
}
String Content::cwxPath(bool id) {
    auto current = shared_from_this();
    lib::Array<std::size_t> indices;
    while (auto p = current->parent()) {
        indices.push_back(static_cast<std::size_t>(utils::cCountUntil(p->next_, current)));
        current = p;
    }
    String suffix;
    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        suffix = path::cpjoin2(suffix, {}, *it);
    String rootPath;
    if (const auto rootTree = current->resolveTree()) {
        const auto index = static_cast<std::size_t>(utils::cCountUntil(rootTree->starts(), current));
        rootPath = path::cpjoin(rootTree->cwxPath(id), {}, index);
    }
    return indices.empty() ? rootPath : path::cpjoin(rootPath, suffix);
}
lib::Owner<CWXPath> Content::findCWXPath(StringView value) {
    if (path::cpempty(value)) return std::static_pointer_cast<CWXPath>(shared_from_this());
    auto current = std::static_pointer_cast<Content>(shared_from_this());
    auto pathValue = String(value);
    while (!path::cpempty(pathValue) && path::cpcategory(pathValue).empty()) {
        const auto index = path::cpindex(pathValue);
        if (index >= checkedContent(current)->next_.size()) return {};
        current = current->next_[index];
        pathValue = path::cpbottom(pathValue);
    }
    if (path::cpempty(pathValue)) return current;
    current = checkedContent(current);
    const auto category = path::cpcategory(pathValue);
    const auto index = path::cpindex(pathValue);
    if (category == "dialog" && index < current->dialogs_.size()) {
        if (!current->dialogs_[index]) throw EventException("Null dialog in path");
        return current->dialogs_[index]->findCWXPath(path::cpbottom(pathValue));
    }
    if (category == "motion" && index < current->motions_.size()) {
        if (!current->motions_[index]) throw EventException("Null motion in path");
        return current->motions_[index]->findCWXPath(path::cpbottom(pathValue));
    }
    if (category == "background" && index < current->backgrounds_.size()) return current->backgrounds_[index];
    if (category == "coupon" && index == 0) {
        // Dは既存の遅延生成フィールドを参照する。パス検索で未生成の
        // 生成元を初期化したり、その失敗をnullへ変えたりしない。
        if (!current->coupon_user_) throw EventException("Coupon has not been initialized");
        return current->coupon_user_->findCWXPath(path::cpbottom(pathValue));
    }
    current->bindTextProducerOwners();
    if (category == "name" && index == 0) return current->name_holder_->findCWXPath(path::cpbottom(pathValue));
    if (category == "gossip" && index == 0) {
        if (!current->gossip_user_) throw EventException("Gossip has not been initialized");
        return current->gossip_user_->findCWXPath(path::cpbottom(pathValue));
    }
    if (category == "text" && index == 0) {
        if (!current->message_holder_) throw EventException("Message has not been initialized");
        return current->message_holder_->findCWXPath(path::cpbottom(pathValue));
    }
    return {};
}
lib::Array<lib::Observer<CWXPath>> Content::cwxChilds() {
    bindTextProducerOwners();
    lib::Array<lib::Observer<CWXPath>> result;
    result.reserve(next_.size() + dialogs_.size() + motions_.size() +
                   backgrounds_.size() + coupon_records_.size() + (message_holder_ ? 1U : 0U));
    for (const auto& child : next_) result.emplace_back(std::static_pointer_cast<CWXPath>(child));
    for (const auto& dialog : dialogs_) result.emplace_back(std::static_pointer_cast<CWXPath>(dialog));
    if (message_holder_) result.emplace_back(std::static_pointer_cast<CWXPath>(message_holder_));
    for (const auto& motion : motions_) result.emplace_back(std::static_pointer_cast<CWXPath>(motion));
    for (const auto& image : backgrounds_) result.emplace_back(std::static_pointer_cast<CWXPath>(image));
    for (const auto& coupon : coupon_records_) result.emplace_back(std::static_pointer_cast<CWXPath>(coupon));
    return result;
}
lib::Observer<CWXPath> Content::cwxParent() {
    if (auto p = parent()) return std::static_pointer_cast<CWXPath>(p);
    const auto tree = resolveTree();
    if (tree.shared()) return std::static_pointer_cast<CWXPath>(tree.shared());
    return {};
}

std::size_t Content::countChildren() const {
    std::size_t result = 0;
    auto current = std::const_pointer_cast<Content>(shared_from_this());
    while (!checkedContent(current)->next_.empty()) {
        result += current->next_.size();
        if (current->next_.size() == 1) current = current->next_.front();
        else {
            for (const auto& child : current->next_) result += checkedContent(child)->countChildren();
            break;
        }
    }
    return result;
}
lib::Array<ContentPtr> Content::allChildren() const {
    lib::Array<ContentPtr> result;
    auto current = std::const_pointer_cast<Content>(shared_from_this());
    while (!checkedContent(current)->next_.empty()) {
        if (current->next_.size() == 1) {
            result.push_back(current->next_.front());
            current = current->next_.front();
        } else {
            for (const auto& child : current->next_) {
                result.push_back(child);
                auto nested = checkedContent(child)->allChildren();
                result.insert(result.end(), nested.begin(), nested.end());
            }
            break;
        }
    }
    return result;
}
void Content::swapContent(std::size_t first, std::size_t second) {
    if (first != second) changed();
    const auto temporary = next_.at(first);
    next_.at(first) = next_.at(second);
    next_.at(second) = temporary;
}
void Content::add(const ContentPtr& value) {
    add(nullptr, value);
}
void Content::prepareChild(const props::CProps* prop, const ContentPtr& value) {
    const auto child = checkedContent(value);
    const auto self = shared_from_this();
    // Dは代入時にこれらの親の前提条件をアサートする。ネイティブの例外境
    // 界では、切り離す前に不正な移動を拒否して、元の参照関係を維持する。
    validateContentParent(*child, self);
    if (auto old = child->parent()) old->remove(child);
    if (prop) normalizeChildName(*prop, child);
    child->parent(self);
    if (const auto counter = use_counter_) {
        const auto owner = uc_owner_;
        child->setUseCounter(counter, owner);
    }
    if (const auto counter = start_use_counter_) child->setStartUseCounter(counter);
    child->changeHandler(changeHandler());
}
void Content::add(const props::CProps* prop, const ContentPtr& value) {
    // Dのクラス引数は値で渡される。呼び出し側が参照を得た配列項目
    // をコールバックが変更しても、ネイティブの所有元を保持する。
    const auto child = checkedContent(value);
    prepareChild(prop, child);
    next_.push_back(child);
    changed();
}
void Content::insert(std::size_t index, const ContentPtr& value) {
    insert(nullptr, index, value);
}
void Content::insert(const props::CProps* prop, std::size_t index, const ContentPtr& value) {
    const auto child = checkedContent(value);
    utils::cwriteln("LOG: Content.insert: parent='" + name() + "' index=" + std::to_string(index) +
                   " content='" + child->name() + "' xmlStr.len=" + std::to_string(child->xmlStr().size()));
    if (index == next_.size()) return add(prop, child);
    prepareChild(prop, child);
    // Dは子を切り離して準備した後に、挿入用スライスを作る。
    if (index > next_.size()) throw std::out_of_range("Content insertion index");
    next_.insert(next_.begin() + static_cast<std::ptrdiff_t>(index), child);
    changed();
}
void Content::remove(std::size_t index) {
    if (use_counter_) checkedContent(next_.at(index))->removeUseCounter();
    if (start_use_counter_) checkedContent(next_.at(index))->removeStartUseCounter();
    checkedContent(next_.at(index))->changeHandler({});
    checkedContent(next_.at(index))->parent({});
    // 親の遷移は通知を伴う場合がある。Dのスライスと同様に、現在の配列を使う。
    if (index >= next_.size()) throw std::out_of_range("Content removal index");
    next_.erase(next_.begin() + static_cast<std::ptrdiff_t>(index));
    changed();
}
void Content::remove(const ContentPtr& value) {
    const auto it = std::find(next_.begin(), next_.end(), value);
    if (it != next_.end()) return remove(static_cast<std::size_t>(std::distance(next_.begin(), it)));
    // Dは未登録の参照に対して、黙って成功せずにアサートする。
    throw EventException("Content is not registered");
}
void Content::setUseCounter(const lib::Owner<UseCounter>& counter,
                            const lib::Owner<CWXPath>& owner) {
    [[maybe_unused]] const auto root_alive = weak_from_this().lock();
    ContentPtr current_alive;
    Content* current = this;
    while (true) {
        current->setOwnUseCounter(counter, owner);
        // Dの反復による単一子の巡回に合わせる。後の接続コールバックがツ
        // リーを変更しても、共有状態の取得によりC++の子を生存させる。
        auto children = current->next_;
        if (children.size() == 1 && children.front()) {
            current_alive = children.front();
            current = current_alive.get();
        } else {
            for (const auto& child : children) if (child) child->setUseCounter(counter, owner);
            break;
        }
    }
}
void Content::setOwnUseCounter(const lib::Owner<UseCounter>& counter,
                               const lib::Owner<CWXPath>& owner) {
    // ネイティブのコールバックの再入で、このContentが明示的に切り離されたり再
    // 接続されたりする場合がある。すでに切り離されたContentへの再度のremove
    // も含め、外側の名前変更の一時的な切り離しをまたいで、その判断を維持する。
    ++use_counter_revision_;
    ++use_counter_binding_revision_;
    // 再接続は破棄ではない。DのsetUseCounterImpl
    // と同様に、保持された各Userがnullも含む新旧のカウンタ
    // ーを扱う。Content自身の状態はDの公開時点まで維持し、
    // 予定する接続先をネイティブのキャッシュと子の生成元へ明示的
    // に渡す。途中のフィールドで例外が出た場合は、完了済みの変更
    // を残してこの状態は変更せず、Dの順次タプル巡回に合わせる。
    if (message_holder_) message_holder_->setUseCounter(counter, owner);
    if (gossip_user_) gossip_user_->setUseCounter(counter, owner);
    if (coupon_user_) coupon_user_->setUseCounter(counter, owner);
    if (coupon_names_producer_) coupon_names_producer_->setUseCounter(counter, owner);
    if (expression_tracker_) expression_tracker_->setUseCounter(counter, owner);
    rebuildUseCounterUsers(counter, owner);
    // 汎用カウンターの再接続では、Start専用の同一性を変更しない。
    for (auto& dialog : dialogs_) if (dialog) dialog->setUseCounter(counter, owner);
    bindMotionPayloads(counter, owner);
    bindBackgroundImages(counter, owner);
    const auto self = std::static_pointer_cast<coupon::CouponsOwner>(shared_from_this());
    for (auto& value : coupon_records_) {
        if (!value) continue;
        value->owner(self);
        value->setUseCounter(counter, owner);
    }
    bindNameCounter(counter, owner);
    use_counter_ = counter;
    uc_owner_ = usecounter::renameInfo(owner);
}

bool Content::changeNumericBranchName(std::uint64_t oldValue, std::uint64_t newValue,
                                      CNextType kind) {
    const auto keep_alive = weak_from_this().lock();
    // Dは無関係な親や不一致の旧IDの場合も、親を検査
    // する前にラベルを解析する。この失敗順を維持する。
    const auto id = isDefaultBranchName(name_) ? 0U : branchLabelId(name_);
    if (const auto p = parent(); p && p->detail().nextType == kind && id == oldValue)
        setNameInternal(newValue == 0U ? "Default" : std::to_string(newValue));
    return true;
}
bool Content::changeCallback(const usecounter::AreaId& oldValue,
                              const usecounter::AreaId& newValue) {
    return changeNumericBranchName(oldValue.id, newValue.id, CNextType::IdArea);
}
bool Content::changeCallback(const usecounter::BattleId& oldValue,
                              const usecounter::BattleId& newValue) {
    return changeNumericBranchName(oldValue.id, newValue.id, CNextType::IdBattle);
}
bool Content::changeCallback(const usecounter::CouponId& oldValue,
                              const usecounter::CouponId& newValue) {
    const auto keep_alive = weak_from_this().lock();
    const auto old_id = oldValue.id;
    const auto replacement = newValue.id;
    if (const auto p = parent(); p && p->detail().nextType == CNextType::Coupon && name_ == old_id)
        setNameInternal(replacement);
    return true;
}

bool Content::changeCallback(const usecounter::VariantId& oldValue,
                              const usecounter::VariantId& newValue) {
    const auto keep_alive = weak_from_this().lock();
    const auto old_id = oldValue.id;
    const auto replacement = newValue.id;
    const auto trace = [this](StringView phase) {
        // Dはクラス参照を出力する。診断上の識別情報にはネイティブオ
        // ブジェクトのアドレスを使い、メッセージの順序は維持する。
        std::ostringstream message;
        message << "TRACE: replTextUseVariant " << phase << " this=" << this
                << " xmlStr.len=" << xmlStr().size();
        utils::cwriteln(message.str());
    };
    trace("BEFORE");
    xmlStr(msgutils::replTextUseVariant(xmlStr(), old_id, replacement));
    trace("AFTER");
    // Dは同じIDや一致する記号のない文字列でも通知する。まずプロ
    // パティ設定関数を通す。保持オブジェクトのコールバックだけで
    // は、Contentレベルのこの通知契約の代わりにならない。
    if (change_) change_();
    return true;
}

bool Content::changeCallback(const usecounter::PathId& oldValue,
                              const usecounter::PathId& newValue) {
    // PathUserがカウンター項目を所有し、このC++移植版ではイベントの
    // 項目もキャッシュする。Dのパス生成元は両方を更新するため、使用中の利用者
    // を再構築する前に、埋め込みのメッセージ画像も含めてキャッシュを同期する。
    const String replacement = newValue;
    bool modified = false;
    const auto replace = [&](String& field) {
        if (usecounter::toPathId(field) != oldValue) return;
        field = replacement;
        modified = true;
    };
    const auto& detail = contentDetail(type_);
    if (detail.use(CArg::TalkerC)) {
        for (auto& image : card_images_) {
            if (!image || image->type != types::CardImageType::File ||
                usecounter::toPathId(image->path()) != oldValue) continue;
            // 保持された画像へ反映する。ここで画像を置き換えると、カウンターの
            // 現在の名前変更・取り消しが保持するパス項目も置き換わってしまう。
            struct Restore { bool& flag; bool previous; ~Restore() { flag = previous; } } restore{
                rebuilding_use_counter_, rebuilding_use_counter_};
            rebuilding_use_counter_ = true;
            image->path_->path(replacement);
            modified = true;
        }
        for (auto& field : card_paths_) replace(field);
    }
    if (detail.use(CArg::BgmPath)) replace(bgmPath_);
    if (detail.use(CArg::SoundPath)) replace(soundPath_);
    if (detail.use(CArg::InitialSoundPath)) replace(initialSoundPath_);
    // この受理済み更新より前に、PathUser::changeが所有元へ通知済み
    // 。二度目の変更イベントを発生させずに、キャッシュされた生成元を再接続する。
    if (modified && use_counter_) rebuildUseCounterUsers();
    return true;
}

void Content::setStartUseCounter(const StartUseCounter& counter) {
    // Dは単一子の巡回を始める前に、受信先だけを検査する
    // 。特に同じカウンターなら、子孫を一切変更しない。
    if (start_use_counter_ == counter) return;

    [[maybe_unused]] const auto root_alive = weak_from_this().lock();
    ContentPtr current_alive;
    Content* current = this;
    while (true) {
        // 連鎖中のノードが要求されたカウンターをすでに持つ場合は、ネイティ
        // ブのアダプターの同一性を維持する。ただし反復の巡回は継続する。
        if (current->start_use_counter_ != counter) {
            lib::Owner<StartReferenceUser> replacement;
            if (counter && contentDetail(current->type_).use(CArg::Start)) {
                // DのsetSUseCounterと同じ順序で、古い登録を外す
                // 前に要求された登録を追加する。所有元とコールバックを接続す
                // る前に初期値を設定し、この再接続で編集通知を発生させない。
                replacement = std::make_shared<StartReferenceUser>();
                replacement->assign(current->start_);
                replacement->owner(current);
                replacement->handleChange(
                    [target = current](const String& value) { target->start_ = value; });
                replacement->setUseCounter(counter);
            }

            if (current->start_user_) {
                current->start_user_->handleChange({});
                current->start_user_->removeUseCounter();
                // 保持されたネイティブ利用者が、破棄済みのContentを経由して転送してはならない。
                current->start_user_->owner(static_cast<CWXPath*>(nullptr));
            }
            current->start_user_ = std::move(replacement);
            current->start_use_counter_ = counter;
        }

        // Dは単一子の連鎖を反復でたどり、各ノードでルート
        // の等価検査を繰り返さない。分岐では新しい呼び出
        // しを始めるため、それぞれ独立に等価検査を行う。
        auto children = current->next_;
        if (children.size() == 1 && children.front()) {
            current_alive = children.front();
            current = current_alive.get();
        } else {
            for (const auto& child : children)
                if (child) child->setStartUseCounter(counter);
            break;
        }
    }
}

void Content::removeStartUseCounter() noexcept {
    // DのremoveSUseCounterは、ルートに専用カウンターがな
    // ければ子孫を訪れる前に戻る。子が独立して接続されている場合も同様。
    if (!start_use_counter_) return;

    [[maybe_unused]] const auto root_alive = weak_from_this().lock();
    ContentPtr current_alive;
    Content* current = this;
    while (true) {
        if (current->start_user_) {
            current->start_user_->handleChange({});
            current->start_user_->removeUseCounter();
            // 実際の破棄では、生の所有元への転送も切断する。コールバックの解除後、保
            // 持されたネイティブ利用者がこのContentより長く生存し得るため。
            current->start_user_->owner(static_cast<CWXPath*>(nullptr));
        }
        current->start_user_.reset();
        current->start_use_counter_.reset();

        if (current->next_.size() == 1 && current->next_.front()) {
            current_alive = current->next_.front();
            current = current_alive.get();
        } else {
            for (const auto& child : current->next_)
                if (child) child->removeStartUseCounter();
            break;
        }
    }
}

void Content::rebuildUseCounterUsers() {
    rebuildUseCounterUsers(use_counter_, uc_owner_);
}
void Content::rebuildUseCounterUsers(
    const lib::Owner<UseCounter>& bindingCounter,
    const lib::Owner<CWXPath>& bindingOwner) {
    const auto counter = bindingCounter;
    if (rebuilding_use_counter_) return;
    struct RebuildGuard {
        bool& value;
        explicit RebuildGuard(bool& flag) : value(flag) { value = true; }
        ~RebuildGuard() { value = false; }
    } guard(rebuilding_use_counter_);

    const auto bind_user = [this, &counter](auto& slot, auto assign) {
        using User = typename std::remove_reference_t<decltype(slot)>::element_type;
        if (!slot) slot = std::make_shared<User>(this);
        // DのPropは、値の設定関数を呼ぶ前にUserからの取得値とキャッシュした項目を比較す
        // る。下記のカウンター・所有元の再接続は、キャッシュされた全Userに対して有効に保つ。
        assign(*slot);
        slot->setUseCounter(counter);
    };
    bind_user(area_user_, [this](usecounter::AreaUser& user) {
        if (user.area() != area_) user.area(area_);
        user.handleChange([this](const usecounter::AreaId value) { area(value.id); });
    });
    bind_user(battle_user_, [this](usecounter::BattleUser& user) {
        if (user.battle() != battle_) user.battle(battle_);
        user.handleChange([this](const usecounter::BattleId value) { battle(value.id); });
    });
    bind_user(package_user_, [this](usecounter::PackageUser& user) {
        if (user.packages() != packages_) user.packages(packages_);
        user.handleChange([this](const usecounter::PackageId value) {
            // Userは書き込み前の変更通知を一度送っている
            // 。通常のContent設定関数の二度目の通知を
            // 出さずに、単一値を公開して経路を整合させる。
            if (packages_ == value.id) return;
            packages_ = value.id;
            rebuildUseCounterUsers();
        });
    });
    bind_user(flag_user_, [this](usecounter::FlagUser& user) {
        if (user.flag() != flag_) user.flag(flag_);
        user.handleChange([this](const usecounter::FlagId& value) {
            // 通常のFlagUserはローカルIDの作成後に再接続する
            // 。新しいIDを早期に登録せず、受理した単一値を公開する。
            if (flag_ == value.id) return;
            flag_ = value.id;
        });
    });
    bind_user(flag2_user_, [this](usecounter::FlagUser& user) {
        if (user.flag() != flag2_) user.flag(flag2_);
        user.handleChange([this](const usecounter::FlagId& value) {
            // 通常のFlagUserはローカルIDの作成後に再接続する
            // 。新しいIDを早期に登録せず、受理した単一値を公開する。
            if (flag2_ == value.id) return;
            flag2_ = value.id;
        });
    });
    bind_user(step_user_, [this](usecounter::StepUser& user) {
        if (user.step() != step_) user.step(step_);
        user.handleChange([this](const usecounter::StepId& value) {
            // 通常のStepUserはローカルIDの作成後に再接続する
            // 。新しいIDを早期に登録せず、受理した単一値を公開する。
            if (step_ == value.id) return;
            step_ = value.id;
        });
    });
    bind_user(step2_user_, [this](usecounter::StepUser& user) {
        if (user.step() != step2_) user.step(step2_);
        user.handleChange([this](const usecounter::StepId& value) {
            // 通常のStepUserはローカルIDの作成後に再接続する
            // 。新しいIDを早期に登録せず、受理した単一値を公開する。
            if (step2_ == value.id) return;
            step2_ = value.id;
        });
    });
    bind_user(variant_user_, [this](usecounter::VariantUser& user) {
        if (user.variant() != variant_) user.variant(variant_);
        user.handleChange([this](const usecounter::VariantId& value) {
            // Userは書き込み前の変更通知を一度送っている
            // 。通常のContent設定関数の二度目の通知を
            // 出さずに、単一値を公開して経路を整合させる。
            if (variant_ == value.id) return;
            variant_ = value.id;
            rebuildUseCounterUsers();
        });
    });

    const auto bind_numeric = [this, &counter]<class ID>(auto& slot, std::uint64_t& field,
                                                       bool active, auto assign) {
        using User = typename std::remove_reference_t<decltype(slot)>::element_type;
        if (!active && field == 0 && !slot) return;
        if (!slot) {
            // Dの分岐条件のUserだけが、型付きの所有元コールバックを有効にする。
            auto bound = [&] {
                using Bound = ContentNumericUser<User, ID>;
                if constexpr (std::is_same_v<User, usecounter::AreaUser> ||
                              std::is_same_v<User, usecounter::BattleUser>)
                    return std::make_shared<Bound>(this, true);
                else
                    return std::make_shared<Bound>(this);
            }();
            assign(*bound, field);
            const std::weak_ptr<Content> weak = shared_from_this();
            const std::weak_ptr<User> identity = bound;
            bound->notify = [weak, identity] {
                auto self = weak.lock(); auto user = identity.lock();
                if (self && user && user->owner().get() == self.get() && !self->rebuilding_use_counter_)
                    if (self->change_) self->change_();
            };
            auto* target = &field;
            bound->publish = [weak, identity, target](std::uint64_t value) {
                auto self = weak.lock(); auto user = identity.lock();
                if (self && user && user->owner().get() == self.get()) {
                    *target = value;
                    // ラベル変更は型付きの書き込み前コールバックが担当する。受理
                    // 後の公開では、Userに保存された単一値を反映するだけ。
                }
            };
            slot = bound;
        } else assign(*slot, field);
        slot->setUseCounter(counter);
    };
    bind_numeric.template operator()<usecounter::CastId>(cast_user_, casts_, detail().use(CArg::Cast),
        [](auto& user, std::uint64_t value) { if (user.casts() != value) user.casts(value); });
    bind_numeric.template operator()<usecounter::SkillId>(skill_user_, skill_, detail().use(CArg::Skill),
        [](auto& user, std::uint64_t value) { if (user.skill() != value) user.skill(value); });
    bind_numeric.template operator()<usecounter::ItemId>(item_user_, item_, detail().use(CArg::Item),
        [](auto& user, std::uint64_t value) { if (user.item() != value) user.item(value); });
    bind_numeric.template operator()<usecounter::BeastId>(beast_user_, beast_, detail().use(CArg::Beast),
        [](auto& user, std::uint64_t value) { if (user.beast() != value) user.beast(value); });
    bind_numeric.template operator()<usecounter::InfoId>(info_user_, info_, detail().use(CArg::Info),
        [](auto& user, std::uint64_t value) { if (user.info() != value) user.info(value); });
    bind_numeric.template operator()<usecounter::AreaId>(branch_area_user_, branch_area_condition_,
        parent() && parent()->detail().nextType == CNextType::IdArea,
        [](auto& user, std::uint64_t value) { if (user.area() != value) user.area(value); });
    bind_numeric.template operator()<usecounter::BattleId>(branch_battle_user_, branch_battle_condition_,
        parent() && parent()->detail().nextType == CNextType::IdBattle,
        [](auto& user, std::uint64_t value) { if (user.battle() != value) user.battle(value); });

    // メッセージの置換記号・フォントの利用者は、保持されたTextHolderに属
    // する。ここでContent所有の重複コールバックを作らない。Contentの
    // 型付きの直接呼び出しには、元のメッセージ・名前の順で処理する別の契約がある。

    const auto detail = contentDetail(type_);
    const auto bind_string = [this, &counter, &bindingOwner](auto& slot, String& field, bool active, auto assign) {
        using User = typename std::remove_reference_t<decltype(slot)>::element_type;
        using Bound = ContentStringUser<User>;
        if (!active && field.empty() && !slot) return;
        if (!slot) {
            auto bound = [&] {
                if constexpr (std::is_same_v<User, usecounter::CouponUser>)
                    return std::make_shared<Bound>(this, true);
                else
                    return std::make_shared<Bound>(this);
            }();
            assign(*bound, field);
            const std::weak_ptr<Content> weak = shared_from_this();
            std::weak_ptr<User> identity = bound;
            auto* target = &field;
            bound->notify = [weak, identity, target] {
                const auto user = identity.lock();
                if (auto self = weak.lock(); self && user && user->owner().get() == self.get() && !self->rebuilding_use_counter_) {
                    // 分岐Coupon内部の変数変更は受理済み。この
                    // 受理した名前の元データを通知前に公開し、後の再
                    // 構築で古い分岐ラベルが復活しないようにする。
                    if constexpr (std::is_same_v<User, usecounter::CouponUser>) {
                        if (target == &self->branch_coupon_condition_ && *target != user->coupon()) {
                            *target = user->coupon();
                        }
                    }
                    // 通常のID全体の設定は、引き続き書き込み前に通知する。ここ
                    // で現在のコールバック利用者を再構築・破棄してはならない。
                    if (self->change_) self->change_();
                }
            };
            bound->publish = [weak, identity, target](const String& value) {
                if (auto self = weak.lock(); self) {
                    if (auto user = identity.lock(); user && user->owner().get() == self.get())
                        *target = value;
                }
            };
            slot = bound;
        } else assign(*slot, field);
        slot->setUseCounter(counter, bindingOwner);
    };

    bind_string(key_code_user_, keyCode_, detail.use(CArg::KeyCode),
        [](auto& user, const String& value) { if (user.keyCode() != value) user.keyCode(value); });

    bind_string(complete_stamp_user_, completeStamp_, detail.use(CArg::CompleteStamp),
        [](auto& user, const String& value) { if (user.completeStamp() != value) user.completeStamp(value); });
    bind_string(cell_name_user_, cellName_, detail.use(CArg::CellName),
        [](auto& user, const String& value) { if (user.cellName() != value) user.cellName(value); });
    bind_string(card_group_user_, cardGroup_, detail.use(CArg::CardGroup),
        [](auto& user, const String& value) { if (user.cardGroup() != value) user.cardGroup(value); });
    bind_string(branch_coupon_user_, branch_coupon_condition_,
        parent() && parent()->detail().nextType == CNextType::Coupon,
        [this](auto& user, const String& value) {
            if (user.coupon() != value) user.coupon(value);
            // キャッシュしたIDが変わらなくても、このID以外の同期は有効に保つ。
            user.expandSPChars(parent() && parent()->expandSPChars());
        });

    const bool active_keys = detail.use(CArg::KeyCodes) || !key_codes_.empty();
    bool same_keys = active_keys && key_code_users_.size() == key_codes_.size();
    for (std::size_t i = 0; same_keys && i < key_codes_.size(); ++i)
        same_keys = key_code_users_[i] && key_code_users_[i]->keyCode() == key_codes_[i];
    if (!same_keys) {
        for (auto& user : key_code_users_) if (user) { user->removeUseCounter(); user->owner(static_cast<CWXPath*>(nullptr)); }
        key_code_users_.clear();
        if (active_keys) for (std::size_t i = 0; i < key_codes_.size(); ++i) {
            auto user = std::make_shared<ContentStringUser<usecounter::KeyCodeUser>>(this);
            user->keyCode(key_codes_[i]);
            const std::weak_ptr<Content> weak = shared_from_this();
            const std::weak_ptr<usecounter::KeyCodeUser> identity = user;
            user->notify = [weak, identity] {
                auto self = weak.lock(); auto slot = identity.lock();
                if (self && slot && slot->owner().get() == self.get() && !self->rebuilding_use_counter_)
                    if (self->change_) self->change_();
            };
            user->publish = [weak, identity, i](const String& value) {
                auto self = weak.lock(); auto slot = identity.lock();
                if (self && slot && i < self->key_code_users_.size() && self->key_code_users_[i] == slot)
                    self->key_codes_[i] = value;
            };
            key_code_users_.push_back(user);
        }
    }
    for (const auto& user : key_code_users_) user->setUseCounter(counter, bindingOwner);

    bind_string(holding_coupon_user_, holdingCoupon_, !holdingCoupon_.empty(),
        [](auto& user, const String& value) { if (user.coupon() != value) user.coupon(value); });

    std::set<String> bound_paths;
    const auto add_path = [this, &counter, &bound_paths](String key, const String& value) {
        auto found = path_users_.find(key);
        if (value.empty() && found == path_users_.end()) return;
        bound_paths.insert(key);
        if (found == path_users_.end()) {
            // Dの項目は、カウンター変更をまたいでPathUserを保持する。安定した
            // 項目・画像のキーにより、C++の単一値キャッシュでも同じ同一性を保つ。
            auto user = std::make_shared<usecounter::PathUser>(
                std::static_pointer_cast<CWXPath>(shared_from_this()));
            found = path_users_.emplace(std::move(key), std::move(user)).first;
        }
        const String stored_path = usecounter::toPathId(value);
        if (found->second->path() != stored_path) found->second->path(value);
        found->second->setUseCounter(counter);
    };
    for (const auto& image : card_images_) if (image) {
        // Contentのパス値キャッシュは、画像ごとに一つの安定したアダプターを使
        // う。内部のPathUserも登録すると、Dの同じ項目を二重に数えてしまう。
        image->removeUseCounter();
        const auto key = "image/" + std::to_string(reinterpret_cast<std::uintptr_t>(image.get()));
        auto& previous_image = path_image_owners_[key];
        if (previous_image.lock() != image) {
            // 削除した画像のアドレスは、メモリ確保で再利用され得る。旧画像
            // の取り消し対象を、置換後の画像のパス項目にしてはならない。
            if (const auto old = path_users_.find(key); old != path_users_.end()) {
                old->second->removeUseCounter();
                old->second->owner(static_cast<CWXPath*>(nullptr));
                path_users_.erase(old);
            }
            previous_image = image;
        }
        add_path(key, image->path());
    }
    add_path("bgmPath", bgmPath_);
    add_path("soundPath", soundPath_);
    add_path("initialSoundPath", initialSoundPath_);
    for (auto it = path_users_.begin(); it != path_users_.end();) {
        if (bound_paths.contains(it->first)) { ++it; continue; }
        it->second->removeUseCounter();
        // このアダプターは削除済み画像に属し、同じ表記の別の現行項目
        // には属さない。現在のContentへの再適用を無効にする。
        it->second->owner(static_cast<CWXPath*>(nullptr));
        path_image_owners_.erase(it->first);
        it = path_users_.erase(it);
    }
    for (auto it = path_image_owners_.begin(); it != path_image_owners_.end();)
        if (!bound_paths.contains(it->first)) it = path_image_owners_.erase(it);
        else ++it;
    if (xml_text_holder_ || !xmlStr_.empty()) {
        // Dはカウンターや他の項目を変更しても、プロパティのTextHolder
        // を保持する。その同一性は、記録された名前変更の取り消し対象でもある。
        if (!xml_text_holder_) {
            xml_text_holder_ = std::make_shared<textholder::TextHolder>(
                std::static_pointer_cast<CWXPath>(shared_from_this()), "xmlStr");
            xml_text_holder_->changeHandler([this] {
                const auto value = xml_text_holder_->text();
                if (xmlStr_ == value) return;
                xmlStr_ = value;
                changed();
            });
        }
        xml_text_holder_->text(xmlStr_);
        xml_text_holder_->setUseCounter(counter, bindingOwner);
    }
}

void Content::rebindCounterPropertyAlias(CounterProperty property) {
    // Dは同じ対象への再入後も、具体的なUser設定関数を再開する。キャッシュ
    // されたUserを保持しつつ、設定関数の削除・追加の継続処理を維持する。
    switch (property) {
    case CounterProperty::area:
        if (area_user_)
            rebindCounterUserAlias(usecounter::toAreaId(area_user_->area()), area_user_);
        break;
    case CounterProperty::battle:
        if (battle_user_)
            rebindCounterUserAlias(usecounter::toBattleId(battle_user_->battle()), battle_user_);
        break;
    case CounterProperty::packages:
        if (package_user_)
            rebindCounterUserAlias(usecounter::toPackageId(package_user_->packages()), package_user_);
        break;
    case CounterProperty::casts:
        if (cast_user_)
            rebindCounterUserAlias(usecounter::toCastId(cast_user_->casts()), cast_user_);
        break;
    case CounterProperty::item:
        if (item_user_)
            rebindCounterUserAlias(usecounter::toItemId(item_user_->item()), item_user_);
        break;
    case CounterProperty::skill:
        if (skill_user_)
            rebindCounterUserAlias(usecounter::toSkillId(skill_user_->skill()), skill_user_);
        break;
    case CounterProperty::beast:
        if (beast_user_)
            rebindCounterUserAlias(usecounter::toBeastId(beast_user_->beast()), beast_user_);
        break;
    case CounterProperty::info:
        if (info_user_)
            rebindCounterUserAlias(usecounter::toInfoId(info_user_->info()), info_user_);
        break;
    case CounterProperty::bgmPath:
    case CounterProperty::soundPath:
    case CounterProperty::initialSoundPath: {
        const auto key = property == CounterProperty::bgmPath ? String("bgmPath")
            : property == CounterProperty::soundPath ? String("soundPath")
            : String("initialSoundPath");
        const auto found = path_users_.find(key);
        if (found != path_users_.end() && found->second)
            rebindCounterUserAlias(usecounter::toPathId(found->second->path()), found->second);
        break;
    }
    case CounterProperty::completeStamp:
        if (complete_stamp_user_)
            rebindCounterUserAlias(usecounter::toCompleteStampId(complete_stamp_user_->completeStamp()), complete_stamp_user_);
        break;
    case CounterProperty::keyCode:
        if (key_code_user_)
            rebindCounterUserAlias(usecounter::toKeyCodeId(key_code_user_->keyCode()), key_code_user_);
        break;
    case CounterProperty::cellName:
        if (cell_name_user_)
            rebindCounterUserAlias(usecounter::toCellNameId(cell_name_user_->cellName()), cell_name_user_);
        break;
    case CounterProperty::cardGroup:
        if (card_group_user_)
            rebindCounterUserAlias(usecounter::toCardGroupId(card_group_user_->cardGroup()), card_group_user_);
        break;
    case CounterProperty::holdingCoupon:
        if (holding_coupon_user_)
            rebindCounterUserAlias(usecounter::toCouponId(holding_coupon_user_->coupon()), holding_coupon_user_);
        break;
    }
}

void Content::removeUseCounter() noexcept {
    // DのsetUseCounter(null)はUserを保持する。C++ではデストラクタ
    // ーだけが所有元・コールバックの接続を解除し、再接続や名前変更の取り消しを有効に保つ。
    Content* current = this;
    while (true) {
        ++current->use_counter_revision_;
        ++current->use_counter_binding_revision_;
        const auto unbind = [](const auto& user) { if (user) user->removeUseCounter(); };
        std::apply([&](const auto&... user) { (unbind(user), ...); },
            std::tie(current->area_user_, current->battle_user_, current->package_user_,
                current->flag_user_, current->flag2_user_, current->step_user_, current->step2_user_,
                current->variant_user_, current->coupon_user_, current->branch_coupon_user_,
                current->holding_coupon_user_, current->key_code_user_, current->gossip_user_,
                current->cast_user_, current->skill_user_, current->item_user_, current->beast_user_,
                current->info_user_, current->branch_area_user_, current->branch_battle_user_,
                current->complete_stamp_user_, current->cell_name_user_, current->card_group_user_,
                current->message_holder_, current->coupon_names_producer_, current->expression_tracker_,
                current->xml_text_holder_));
        for (const auto& user : current->key_code_users_) unbind(user);
        for (const auto& [key, user] : current->path_users_) unbind(user);
        for (const auto& image : current->card_images_) unbind(image);
        for (const auto& dialog : current->dialogs_) unbind(dialog);
        for (const auto& motion : current->motions_)
            if (motion && motion->owner_raw_ == current) unbind(motion);
        for (const auto& image : current->backgrounds_) unbind(image);
        for (const auto& coupon : current->coupon_records_) unbind(coupon);
        current->name_holder_->removeUseCounter();
        current->use_counter_.reset();
        current->uc_owner_.reset();
        if (current->next_.size() == 1 && current->next_.front()) {
            current = current->next_.front().get();
        } else {
            for (const auto& child : current->next_) if (child) child->removeUseCounter();
            break;
        }
    }
}
void Content::retireUseCounterUsers() noexcept {
    ++use_counter_revision_;
    const auto remove_user = [](auto& user) {
        if (user) {
            user->removeUseCounter();
            // DのGCと異なり、保持されたC++のUserは、すでにデストラクタ
            // ーを実行中のContentへの生のコールバックを残してはならない。
            if constexpr (requires { user->handleChange({}); }) user->handleChange({});
            if constexpr (requires { user->retireTextProducer(); }) user->retireTextProducer();
            user->owner(static_cast<CWXPath*>(nullptr));
        }
        user.reset();
    };
    if (branch_coupon_user_) branch_coupon_user_->owner(static_cast<CWXPath*>(nullptr));
    if (gossip_user_) gossip_user_->removeUseCounter();
    if (message_holder_) message_holder_->removeUseCounter();
    if (key_code_user_) key_code_user_->owner(static_cast<CWXPath*>(nullptr));
    if (cast_user_) { cast_user_->owner(static_cast<CWXPath*>(nullptr)); remove_user(cast_user_); }
    if (skill_user_) { skill_user_->owner(static_cast<CWXPath*>(nullptr)); remove_user(skill_user_); }
    if (item_user_) { item_user_->owner(static_cast<CWXPath*>(nullptr)); remove_user(item_user_); }
    if (beast_user_) { beast_user_->owner(static_cast<CWXPath*>(nullptr)); remove_user(beast_user_); }
    if (info_user_) { info_user_->owner(static_cast<CWXPath*>(nullptr)); remove_user(info_user_); }
    if (branch_area_user_) { branch_area_user_->owner(static_cast<CWXPath*>(nullptr)); remove_user(branch_area_user_); }
    if (branch_battle_user_) { branch_battle_user_->owner(static_cast<CWXPath*>(nullptr)); remove_user(branch_battle_user_); }
    if (complete_stamp_user_) { complete_stamp_user_->owner(static_cast<CWXPath*>(nullptr)); remove_user(complete_stamp_user_); }
    if (cell_name_user_) { cell_name_user_->owner(static_cast<CWXPath*>(nullptr)); remove_user(cell_name_user_); }
    if (card_group_user_) { card_group_user_->owner(static_cast<CWXPath*>(nullptr)); remove_user(card_group_user_); }
    for (auto& user : key_code_users_) if (user) { user->owner(static_cast<CWXPath*>(nullptr)); user->removeUseCounter(); }
    key_code_users_.clear();
    remove_user(branch_coupon_user_); remove_user(key_code_user_);
    if (coupon_user_) coupon_user_->removeUseCounter();
    if (coupon_names_producer_) coupon_names_producer_->removeUseCounter();
    remove_user(area_user_); remove_user(battle_user_); remove_user(package_user_);
    remove_user(flag_user_); remove_user(flag2_user_); remove_user(step_user_);
    remove_user(step2_user_); remove_user(variant_user_);
    if (holding_coupon_user_) {
        holding_coupon_user_->owner(static_cast<CWXPath*>(nullptr));
        remove_user(holding_coupon_user_);
    }
    for (auto& [key, user] : path_users_) if (user) {
        user->removeUseCounter();
        user->owner(static_cast<CWXPath*>(nullptr));
    }
    for (auto& value : coupon_records_) if (value) value->removeUseCounter();
    if (expression_tracker_) expression_tracker_->removeUseCounter();
    if (xml_text_holder_) xml_text_holder_->removeUseCounter();
    path_users_.clear();
    path_image_owners_.clear();
    use_counter_.reset(); uc_owner_.reset();
    for (auto& dialog : dialogs_) if (dialog) dialog->removeUseCounter();
    for (auto& motion : motions_)
        if (motion && motion->owner_raw_ == this) motion->removeUseCounter();
    for (auto& background : backgrounds_) if (background) background->removeUseCounter();
}
lib::Array<String> Content::fontsInText() const { return message_holder_ ? message_holder_->fontsInText() : lib::Array<String>{}; }
lib::Array<String> Content::flagsInText() const { return message_holder_ ? message_holder_->flagsInText() : lib::Array<String>{}; }
lib::Array<String> Content::stepsInText() const { return message_holder_ ? message_holder_->stepsInText() : lib::Array<String>{}; }
lib::Array<String> Content::variantsInText() const { return message_holder_ ? message_holder_->variantsInText() : lib::Array<String>{}; }
std::vector<char> Content::colorsInText() const { return message_holder_ ? message_holder_->colorsInText() : std::vector<char>{}; }
void Content::changeInText(std::size_t index, StringView id) {
    changePathInText(index, id);
}
void Content::changeInText(std::size_t index, const usecounter::PathId& id) {
    if (message_holder_) message_holder_->changeInText(index, id);
}
void Content::changeFlagInText(std::size_t index, StringView id) {
    if (message_holder_) message_holder_->changeFlagInText(index, id);
    name_holder_->changeFlagInText(index, id);
}
void Content::changeStepInText(std::size_t index, StringView id) {
    if (message_holder_) message_holder_->changeStepInText(index, id);
    name_holder_->changeStepInText(index, id);
}
void Content::changeVariantInText(std::size_t index, StringView id) {
    if (message_holder_) message_holder_->changeVariantInText(index, id);
    name_holder_->changeVariantInText(index, id);
}
void Content::changePathInText(std::size_t index, StringView id) {
    if (message_holder_) message_holder_->changePathInText(index, id);
}

lib::Array<String> Content::valuedCouponNames() const {
    lib::Array<String> names;
    names.reserve(coupon_records_.size());
    for (const auto& record : coupon_records_)
        if (record) names.push_back(record->name());
    return names;
}

void Content::valuedCouponNames(lib::Array<String> value) {
    lib::Array<lib::Owner<coupon::Coupon>> records;
    records.reserve(value.size());
    for (const auto& name : value)
        records.push_back(std::make_shared<coupon::Coupon>(name, 0));
    coupons(lib::SharedSlice<lib::Owner<coupon::Coupon>>(std::move(records)));
}

void Content::coupons(lib::SharedSlice<lib::Owner<coupon::Coupon>> value) {
    PropertyValidation validate{*this};
    if (sameObjectValues(coupon_records_, value)) return;
    changed();
    const auto self = validate.retained
        ? std::static_pointer_cast<coupon::CouponsOwner>(validate.retained)
        : lib::Owner<coupon::CouponsOwner>{};
    for (auto& record : coupon_records_) {
        // DのsetValUCsは、カウンターを外しながら旧配列をこ
        // の所有元へ接続する。Dでもnull項目は参照できないため、
        // ネイティブAPIも同じ順次接続の境界で不正項目を報告する。
        if (!record) throw std::invalid_argument("Null coupon in Content.");
        record->owner(self);
        record->removeUseCounter();
    }
    for (const auto& record : value) {
        // DはsetValUCsで、最終代入の前に入力の各Couponを参
        // 照する。ネイティブの例外を使い、この途中までの接続順を維持する。
        if (!record) throw std::invalid_argument("Null coupon in Content.");
        record->owner(self);
        record->removeUseCounter();
        if (use_counter_) record->setUseCounter(use_counter_, uc_owner_);
    }
    coupon_records_ = std::move(value);
}

void Content::coupons(std::initializer_list<lib::Owner<coupon::Coupon>> value) {
    coupons(lib::SharedSlice<lib::Owner<coupon::Coupon>>(value));
}

void Content::shallowCopy(const Content& other) {
    if (this == &other) return;
    const auto keep_alive = weak_from_this().lock();
    const auto source_alive = other.weak_from_this().lock();
    // Dと同じ設定関数を同じ順に使い、接続と項目ごとの通知を維持する。
    comment(other.comment());
    area(other.area());
    battle(other.battle());
    packages(other.packages());
    flag(other.flag());
    step(other.step());
    variant(other.variant());
    expression(other.expression());
    cardImages(other.card_images_);
    bgmPath(other.bgmPath());
    bgmChannel(other.bgmChannel());
    bgmVolume(other.bgmVolume());
    bgmLoopCount(other.bgmLoopCount());
    bgmFadeIn(other.bgmFadeIn());
    soundPath(other.soundPath());
    soundChannel(other.soundChannel());
    soundVolume(other.soundVolume());
    soundLoopCount(other.soundLoopCount());
    soundFadeIn(other.soundFadeIn());
    initialEffect(other.initialEffect());
    initialSoundPath(other.initialSoundPath());
    initialSoundChannel(other.initialSoundChannel());
    initialSoundVolume(other.initialSoundVolume());
    initialSoundLoopCount(other.initialSoundLoopCount());
    initialSoundFadeIn(other.initialSoundFadeIn());
    casts(other.casts());
    item(other.item());
    skill(other.skill());
    beast(other.beast());
    info(other.info());
    start(other.start());
    coupon(other.coupon());
    gossip(other.gossip());
    completeStamp(other.completeStamp());
    mental(other.mental());
    physical(other.physical());
    status(other.status());
    range(other.range());
    cardVisual(other.cardVisual());
    effectType(other.effectType());
    resist(other.resist());
    transition(other.transition());
    targetAll(other.targetAll());
    selectionMethod(other.selectionMethod());
    average(other.average());
    complete(other.complete());
    unsignedLevel(other.unsignedLevel());
    signedLevel(other.signedLevel());
    successRate(other.successRate());
    transitionSpeed(other.transitionSpeed());
    cardSpeed(other.cardSpeed());
    overrideCardSpeed(other.overrideCardSpeed());
    percent(other.percent());
    flagValue(other.flagValue());
    stepValue(other.stepValue());
    couponValue(other.couponValue());
    partyNumber(other.partyNumber());
    cardNumber(other.cardNumber());
    money(other.money());
    wait(other.wait());
    flag2(other.flag2());
    step2(other.step2());
    castRange(other.castRange());
    levelMin(other.levelMin());
    levelMax(other.levelMax());
    keyCodeRange(other.keyCodeRange());
    targetIsSkill(other.targetIsSkill());
    targetIsItem(other.targetIsItem());
    targetIsBeast(other.targetIsBeast());
    targetIsHand(other.targetIsHand());
    keyCode(other.keyCode());
    initValue(other.initValue());
    comparison4(other.comparison4());
    comparison3(other.comparison3());
    round(other.round());
    cellName(other.cellName());
    cardGroup(other.cardGroup());
    positionType(other.positionType());
    x(other.x());
    y(other.y());
    sizeType(other.sizeType());
    width(other.width());
    height(other.height());
    scale(other.scale());
    layer(other.layer());
    doAnime(other.doAnime());
    ignoreEffectBooster(other.ignoreEffectBooster());
    singleLine(other.singleLine());
    selectionColumns(other.selectionColumns());
    centeringX(other.centeringX());
    centeringY(other.centeringY());
    boundaryCheck(other.boundaryCheck());
    startAction(other.startAction());
    ignite(other.ignite());
    keyCodes(other.keyCodes());
    couponNames(lib::SharedSlice<String>(other.couponNames()));
    matchingType(other.matchingType());
    expandSPChars(other.expandSPChars());
    holdingCoupon(other.holdingCoupon());
    refAbility(other.refAbility());
    absorbTo(other.absorbTo());
    selectCard(other.selectCard());
    selectTalker(other.selectTalker());
    invertResult(other.invertResult());
    matchingCondition(other.matchingCondition());
    consumeCard(other.consumeCard());
    backpackEnabled(other.backpackEnabled());
    gameOverEnabled(other.gameOverEnabled());
    runAwayEnabled(other.runAwayEnabled());
    lib::Array<lib::Owner<motion::Motion>> copied_motions;
    for (const auto& value : other.motions_) if (value) copied_motions.push_back(value->dup());
    motions(std::move(copied_motions));
    text(other.text());
    lib::Array<lib::Owner<SDialog>> copied_dialogs;
    for (const auto& value : other.dialogs_) if (value) copied_dialogs.push_back(value->dup());
    dialogs(std::move(copied_dialogs));
    targetS(other.targetS());
    talkerNC(other.talkerNC());
    background::BgImageArray copied_backgrounds;
    for (const auto& value : other.backgrounds_) if (value) copied_backgrounds.push_back(value->dup());
    backgroundImages(std::move(copied_backgrounds));
    lib::Array<lib::Owner<coupon::Coupon>> copied_coupons;
    for (const auto& value : other.coupon_records_) if (value) copied_coupons.push_back(value->dup());
    coupons(std::move(copied_coupons));
    xmlStr(other.xmlStr());
    xmlAssignment(other.xmlAssignment());
    xmlType(other.xmlType());
}
ContentPtr Content::dup() const {
    auto result = std::make_shared<Content>(type_, name_);
    result->shallowCopy(*this);
    utils::cwriteln("LOG: Content.dup: orig='" + name() + "' orig.xmlStr.len=" +
                   std::to_string(xmlStr().size()) + " copy='" + result->name() +
                   "' copy.xmlStr.len=" + std::to_string(result->xmlStr().size()));
    // Dのforeachは入力スライスを保持する。コピーの構築中はネイティ
    // ブの子を生存させ、nullはその子のdup呼び出し時にエラーとする。
    const auto retained = next_;
    for (const auto& child : retained) result->add(nullptr, checkedContent(child)->dup());
    return result;
}
bool Content::opEquals(const Content& other) const noexcept {
    if (type_ != other.type_ || name_ != other.name_ || comment_ != other.comment_) {
        return false;
    }
    const auto d = detail();
    const auto same = [&d](CArg arg, const auto& left, const auto& right) {
        return !d.use(arg) || left == right;
    };
    const auto same_card_images = [&d](const auto& left, const auto& right) {
        if (!d.use(CArg::TalkerC)) return true;
        if (left.size() != right.size()) return false;
        for (std::size_t index = 0; index < left.size(); ++index) {
            if (static_cast<bool>(left[index]) != static_cast<bool>(right[index]))
                return false;
            if (left[index] && !left[index]->opEquals(*right[index])) return false;
        }
        return true;
    };
    if (!same(CArg::Area, area_, other.area_) ||
        !same(CArg::Battle, battle_, other.battle_) ||
        !same(CArg::Package, packages_, other.packages_) ||
        !same(CArg::Flag, flag_, other.flag_) ||
        !same(CArg::Step, step_, other.step_) ||
        !same(CArg::Variant, variant_, other.variant_) ||
        !same(CArg::Expression, expression_, other.expression_) ||
        !same_card_images(card_images_, other.card_images_) ||
        !same(CArg::BgmPath, bgmPath_, other.bgmPath_) ||
        !same(CArg::BgmChannel, bgmChannel_, other.bgmChannel_) ||
        !same(CArg::BgmVolume, bgmVolume_, other.bgmVolume_) ||
        !same(CArg::BgmLoopCount, bgmLoopCount_, other.bgmLoopCount_) ||
        !same(CArg::BgmFadeIn, bgmFadeIn_, other.bgmFadeIn_) ||
        !same(CArg::SoundPath, soundPath_, other.soundPath_) ||
        !same(CArg::SoundChannel, soundChannel_, other.soundChannel_) ||
        !same(CArg::SoundVolume, soundVolume_, other.soundVolume_) ||
        !same(CArg::SoundLoopCount, soundLoopCount_, other.soundLoopCount_) ||
        !same(CArg::SoundFadeIn, soundFadeIn_, other.soundFadeIn_) ||
        !same(CArg::InitialEffect, initialEffect_, other.initialEffect_) ||
        !same(CArg::InitialSoundPath, initialSoundPath_, other.initialSoundPath_) ||
        !same(CArg::InitialSoundChannel, initialSoundChannel_, other.initialSoundChannel_) ||
        !same(CArg::InitialSoundVolume, initialSoundVolume_, other.initialSoundVolume_) ||
        !same(CArg::InitialSoundLoopCount, initialSoundLoopCount_, other.initialSoundLoopCount_) ||
        !same(CArg::InitialSoundFadeIn, initialSoundFadeIn_, other.initialSoundFadeIn_) ||
        !same(CArg::Cast, casts_, other.casts_) ||
        !same(CArg::Item, item_, other.item_) ||
        !same(CArg::Skill, skill_, other.skill_) ||
        !same(CArg::Beast, beast_, other.beast_) ||
        !same(CArg::Info, info_, other.info_) ||
        !same(CArg::Start, start_, other.start_) ||
        !same(CArg::Coupon, coupon_, other.coupon_) ||
        !same(CArg::Gossip, gossip_, other.gossip_) ||
        !same(CArg::CompleteStamp, completeStamp_, other.completeStamp_) ||
        !same(CArg::Mental, mental_, other.mental_) ||
        !same(CArg::Physical, physical_, other.physical_) ||
        !same(CArg::Status, status_, other.status_) ||
        !same(CArg::Range, range_, other.range_) ||
        !same(CArg::CardVisual, cardVisual_, other.cardVisual_) ||
        !same(CArg::EffectType, effectType_, other.effectType_) ||
        !same(CArg::Resist, resist_, other.resist_) ||
        !same(CArg::Transition, transition_, other.transition_) ||
        !same(CArg::TargetAll, targetAll_, other.targetAll_) ||
        !same(CArg::SelectionMethod, selectionMethod_, other.selectionMethod_) ||
        !same(CArg::Average, average_, other.average_) ||
        !same(CArg::Complete, complete_, other.complete_) ||
        !same(CArg::UnsignedLevel, unsignedLevel_, other.unsignedLevel_) ||
        !same(CArg::SignedLevel, signedLevel_, other.signedLevel_) ||
        !same(CArg::SuccessRate, successRate_, other.successRate_) ||
        !same(CArg::TransitionSpeed, transitionSpeed_, other.transitionSpeed_) ||
        !same(CArg::CardSpeed, cardSpeed_, other.cardSpeed_) ||
        !same(CArg::OverrideCardSpeed, overrideCardSpeed_, other.overrideCardSpeed_) ||
        !same(CArg::Percent, percent_, other.percent_) ||
        !same(CArg::FlagValue, flagValue_, other.flagValue_) ||
        !same(CArg::StepValue, stepValue_, other.stepValue_) ||
        !same(CArg::CouponValue, couponValue_, other.couponValue_) ||
        !same(CArg::PartyNumber, partyNumber_, other.partyNumber_) ||
        !same(CArg::CardNumber, cardNumber_, other.cardNumber_) ||
        !same(CArg::Money, money_, other.money_) ||
        !same(CArg::Wait, wait_, other.wait_) ||
        !same(CArg::Flag2, flag2_, other.flag2_) ||
        !same(CArg::Step2, step2_, other.step2_) ||
        !same(CArg::CastRange, cast_range_, other.cast_range_) ||
        !same(CArg::LevelMin, levelMin_, other.levelMin_) ||
        !same(CArg::LevelMax, levelMax_, other.levelMax_) ||
        !same(CArg::KeyCodeRange, keyCodeRange_, other.keyCodeRange_) ||
        !same(CArg::TargetIsSkill, targetIsSkill_, other.targetIsSkill_) ||
        !same(CArg::TargetIsItem, targetIsItem_, other.targetIsItem_) ||
        !same(CArg::TargetIsBeast, targetIsBeast_, other.targetIsBeast_) ||
        !same(CArg::TargetIsHand, targetIsHand_, other.targetIsHand_) ||
        !same(CArg::KeyCode, keyCode_, other.keyCode_) ||
        !same(CArg::InitValue, initValue_, other.initValue_) ||
        !same(CArg::Comparison4, comparison4_, other.comparison4_) ||
        !same(CArg::Comparison3, comparison3_, other.comparison3_) ||
        !same(CArg::Round, round_, other.round_) ||
        !same(CArg::CellName, cellName_, other.cellName_) ||
        !same(CArg::CardGroup, cardGroup_, other.cardGroup_) ||
        !same(CArg::PositionType, positionType_, other.positionType_) ||
        !same(CArg::X, x_, other.x_) || !same(CArg::Y, y_, other.y_) ||
        !same(CArg::SizeType, sizeType_, other.sizeType_) ||
        !same(CArg::Width, width_, other.width_) ||
        !same(CArg::Height, height_, other.height_) ||
        !same(CArg::Scale, scale_, other.scale_) ||
        !same(CArg::Layer, layer_, other.layer_) ||
        !same(CArg::DoAnime, doAnime_, other.doAnime_) ||
        !same(CArg::IgnoreEffectBooster, ignoreEffectBooster_, other.ignoreEffectBooster_) ||
        !same(CArg::SingleLine, singleLine_, other.singleLine_) ||
        !same(CArg::SelectionColumns, selectionColumns_, other.selectionColumns_) ||
        !same(CArg::CenteringX, centeringX_, other.centeringX_) ||
        !same(CArg::CenteringY, centeringY_, other.centeringY_) ||
        !same(CArg::BoundaryCheck, boundaryCheck_, other.boundaryCheck_) ||
        !same(CArg::StartAction, startAction_, other.startAction_) ||
        !same(CArg::Ignite, ignite_, other.ignite_) ||
        !same(CArg::KeyCodes, key_codes_, other.key_codes_) ||
        !same(CArg::CouponNames, coupon_names_, other.coupon_names_) ||
        !same(CArg::MatchingType, matchingType_, other.matchingType_) ||
        !same(CArg::ExpandSPChars, expandSPChars_, other.expandSPChars_) ||
        !same(CArg::HoldingCoupon, holdingCoupon_, other.holdingCoupon_) ||
        !same(CArg::RefAbility, refAbility_, other.refAbility_) ||
        !same(CArg::AbsorbTo, absorbTo_, other.absorbTo_) ||
        !same(CArg::SelectCard, selectCard_, other.selectCard_) ||
        !same(CArg::SelectTalker, selectTalker_, other.selectTalker_) ||
        !same(CArg::InvertResult, invertResult_, other.invertResult_) ||
        !same(CArg::MatchingCondition, matchingCondition_, other.matchingCondition_) ||
        !same(CArg::ConsumeCard, consumeCard_, other.consumeCard_) ||
        !same(CArg::TargetS, target_s_, other.target_s_) ||
        !same(CArg::TalkerNC, talkerNC_, other.talkerNC_) ||
        !same(CArg::BackpackEnabled, backpackEnabled_, other.backpackEnabled_) ||
        !same(CArg::GameOverEnabled, gameOverEnabled_, other.gameOverEnabled_) ||
        !same(CArg::RunAwayEnabled, runAwayEnabled_, other.runAwayEnabled_) ||
        !same(CArg::Text, text_, other.text_) ||
        !same(CArg::BgImages, backgrounds_.size(), other.backgrounds_.size()) ||
        !same(CArg::Motions, motions_.size(), other.motions_.size()) ||
        !same(CArg::Coupons, coupon_records_.size(), other.coupon_records_.size()) ||
        !same(CArg::Dialogs, dialogs_.size(), other.dialogs_.size()) ||
        next_.size() != other.next_.size()) {
        return false;
    }
    if (d.use(CArg::BgImages)) {
        for (std::size_t i = 0; i < backgrounds_.size(); ++i) {
            if (static_cast<bool>(backgrounds_[i]) != static_cast<bool>(other.backgrounds_[i])) return false;
            if (backgrounds_[i] && !backgrounds_[i]->equals(*other.backgrounds_[i])) return false;
        }
    }
    if (d.use(CArg::Coupons)) {
        for (std::size_t i = 0; i < coupon_records_.size(); ++i) {
            if (static_cast<bool>(coupon_records_[i]) != static_cast<bool>(other.coupon_records_[i])) return false;
            if (coupon_records_[i] && !coupon_records_[i]->opEquals(*other.coupon_records_[i])) return false;
        }
    }
    if (d.use(CArg::Motions)) {
        for (std::size_t i = 0; i < motions_.size(); ++i) {
            if (static_cast<bool>(motions_[i]) != static_cast<bool>(other.motions_[i])) return false;
            if (motions_[i] && !motions_[i]->opEquals(*other.motions_[i])) return false;
        }
    }
    if (d.use(CArg::Dialogs)) {
        for (std::size_t i = 0; i < dialogs_.size(); ++i) {
            if (static_cast<bool>(dialogs_[i]) != static_cast<bool>(other.dialogs_[i])) return false;
            if (dialogs_[i] && !dialogs_[i]->opEquals(*other.dialogs_[i])) return false;
        }
    }
    for (std::size_t i = 0; i < next_.size(); ++i) {
        if (static_cast<bool>(next_[i]) != static_cast<bool>(other.next_[i])) return false;
        if (next_[i] && !next_[i]->opEquals(*other.next_[i])) return false;
    }
    return true;
}
bool Content::canConvert(CType value) const {
    if (value == type_ || value == CType::Start || type_ == CType::Start) return false;
    return next_.empty() || contentDetail(value).owner;
}
void Content::normalizeChildName(const props::CProps& prop, const ContentPtr& child) {
    if (!child) return;
    const auto& sys = prop.sys();
    const auto choose = [&](const lib::Array<String>& values, const String& fallback) {
        for (const auto& value : values) {
            if (std::none_of(next_.begin(), next_.end(), [&](const auto& other) {
                return other && other != child && other->name() == value;
            })) return value;
        }
        return fallback;
    };
    const auto number = [](StringView text) -> std::optional<std::uint32_t> {
        if (text.starts_with('+')) text.remove_prefix(1);
        std::uint32_t result{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
        if (error != std::errc{} || end != text.data() + text.size()) return {};
        return result;
    };
    auto root = cwxParent().lock();
    while (root) {
        const auto parent = root->cwxParent().lock();
        if (!parent) break;
        root = parent;
    }
    const auto summ = std::dynamic_pointer_cast<summary::Summary>(root);
    auto kind = detail().nextType;
    if (kind == CNextType::None && type_ == CType::CustomXmlContent) {
        if (xmlType_ == "BranchStepValue") kind = CNextType::Trio;
        else {
            static const std::set<String> boolean_xml{
                "BranchFlag", "BranchFlagValue", "BranchStep", "BranchSelect",
                "BranchAbility", "BranchRandom", "BranchLevel", "BranchStatus",
                "BranchPartyNumber", "BranchRound", "BranchCast", "BranchItem",
                "BranchSkill", "BranchBeast", "BranchInfo", "BranchMoney",
                "BranchCoupon", "BranchCompleteStamp", "BranchGossip", "BranchKeyCode"};
            if (child->name().empty() && boolean_xml.contains(xmlType_))
                child->setName(choose({sys.evtChildTrue(), sys.evtChildFalse()}, sys.evtChildFalse()));
            return;
        }
    }
    switch (kind) {
    case CNextType::None: child->setName({}); break;
    case CNextType::Text:
    case CNextType::Coupon: break;
    case CNextType::Bool:
        if (child->name() != sys.evtChildTrue() && child->name() != sys.evtChildFalse())
            child->setName(choose({sys.evtChildTrue(), sys.evtChildFalse()}, sys.evtChildTrue()));
        break;
    case CNextType::Trio:
        if (child->name() != sys.evtChildGreater() && child->name() != sys.evtChildLesser() && child->name() != sys.evtChildEq())
            child->setName(choose({sys.evtChildGreater(), sys.evtChildLesser(), sys.evtChildEq()}, sys.evtChildGreater()));
        break;
    case CNextType::Step:
        if (child->name() != sys.evtChildDefault() && !utils::isNumeric(child->name())) {
            auto count = static_cast<std::uint64_t>(std::max(0, prop.looks().stepMaxCount()));
            if (summ) if (const auto step = flag::findVar<flag::Step>(summ->flagDirRoot(), use_counter_, step_)) count = step->count();
            std::uint64_t start{};
            for (const auto& other : next_) if (other) if (const auto value = number(other->name())) {
                start = std::max(start, static_cast<std::uint64_t>(static_cast<std::uint32_t>(*value + 1U)));
                count = std::max(count, static_cast<std::uint64_t>(*value));
            }
            // Dは候補範囲全体を生成する。巨大な配列を確
            // 保しないよう、最初の未使用項目を直接探す。
            String selected = sys.evtChildDefault();
            for (; start < count; ++start) {
                const auto candidate = std::to_string(start);
                if (std::none_of(next_.begin(), next_.end(), [&](const auto& other) {
                    return other && other != child && other->name() == candidate;
                })) { selected = candidate; break; }
            }
            child->setName(std::move(selected));
        }
        break;
    case CNextType::IdArea:
    case CNextType::IdBattle:
        if ((child->name() != sys.evtChildDefault() && !utils::isNumeric(child->name())) || child->name() == "0") {
            child->setName(sys.evtChildDefault());
            if (!summ) break;
            lib::Array<String> values;
            if (summ) {
                if (kind == CNextType::IdArea) {
                    for (const auto& area : summ->areas()) if (area) values.push_back(std::to_string(area->id()));
                } else {
                    for (const auto& battle : summ->battles()) if (battle) values.push_back(std::to_string(battle->id()));
                }
            }
            values.push_back(sys.evtChildDefault());
            child->setName(choose(values, sys.evtChildDefault()));
        }
        break;
    }
}
void Content::convertType(CType value, const ContentPtr& base,
                          const props::CProps* prop, const structs::DialogStatus* dialogStatus,
                          const skin::Skin* skinValue, StringView scenarioPath,
                          StringView wsnVersion) {
    if (!canConvert(value)) throw EventException("can not convert content type");
    if (value == type_) return;
    const auto keepAlive = weak_from_this().lock();
    const auto entryType = type_;
    // Dは各項目への書き込み前に、最初の変換の変更を公開する。
    changed();
    // 古い意図で処理を続けず、入れ子で行われた変換を維持する。
    if (type_ != entryType) return;
    const auto oldType = type_;
    const auto oldDetail = contentDetail(oldType);
    const auto newDetail = contentDetail(value);
    type_ = value;
    if (prop) for (const auto& child : next_) normalizeChildName(*prop, child);

    const auto oldCoupon = coupon_;
    const auto oldGossip = gossip_;
    const auto oldCompleteStamp = completeStamp_;
    const auto oldKeyCode = keyCode_;
    const auto oldCellName = cellName_;
    const auto oldCardGroup = cardGroup_;
    const auto oldCouponNames = coupon_names_;
    lib::Array<String> oldCoupons;
    for (const auto& record : coupon_records_) if (record) oldCoupons.push_back(record->name());
    const auto oldBgmPath = bgmPath_;
    const auto oldSoundPath = soundPath_;
    const auto oldInitialSoundPath = initialSoundPath_;
    const auto reset = [&](CArg arg, auto set, const auto& inherited,
                           const auto& initial) {
        const auto apply = [&](const auto& next) {
            // DのresetValueはProp設定関数を通り、そのscope(exit)
            // は同じ値でも検証する。各リセットの間のこの境界を維持する。
            struct ValidateOnExit { Content& content; ~ValidateOnExit() noexcept(false) { content.validate(); } } validate_on_exit{*this};
            set(next);
        };
        if (!newDetail.use(arg)) apply(initial);
        else if (base && !oldDetail.use(arg)) apply(inherited);
    };

    // Start専用の参照は、プロパティのリセットより前に登録を変更する。
    if (!newDetail.use(CArg::Start)) {
        if (start_user_) start_user_->removeUseCounter();
    } else if (start_use_counter_) {
        if (!start_user_) {
            start_user_ = std::make_shared<StartReferenceUser>(this);
            start_user_->assign(start_);
            start_user_->handleChange([this](const String& target) { start_ = target; });
        }
        start_user_->setUseCounter(start_use_counter_);
    }
    const auto canResolve = [&](StringView path, bool bgm) {
        if (path.empty()) return false;
        // DはSkinの状態を必要とする。このC++の簡易オーバーロードは未公
        // 開のモデル値にも使うため、その状態がなければ空でないパスを保持する。
        if (!skinValue) return true;
        return !skinValue->findPath(path, bgm ? skinValue->extBgm() : skinValue->extSound(),
                                    bgm ? skinValue->bgmDirs() : skinValue->seDirs(),
                                    scenarioPath, wsnVersion,
                                    bgm ? skinValue->wsnMusicDirs(wsnVersion)
                                        : skinValue->wsnSoundDirs(wsnVersion)).empty();
    };
    String oldPath;
    if (oldDetail.use(CArg::BgmPath) && !oldBgmPath.empty()) oldPath = oldBgmPath;
    else if (oldDetail.use(CArg::SoundPath) && !oldSoundPath.empty()) {
        oldPath = oldSoundPath;
        if (newDetail.use(CArg::InitialSoundPath) && !canResolve(oldPath, false))
            oldPath = oldInitialSoundPath;
    }
    String oldValue;
    if (oldDetail.use(CArg::Coupon) && !oldCoupon.empty()) oldValue = oldCoupon;
    else if (oldDetail.use(CArg::Gossip) && !oldGossip.empty()) oldValue = oldGossip;
    else if (oldDetail.use(CArg::CompleteStamp) && !oldCompleteStamp.empty()) oldValue = oldCompleteStamp;
    else if (oldDetail.use(CArg::KeyCode) && !oldKeyCode.empty()) oldValue = oldKeyCode;
    else if (const auto it = std::find_if(oldCoupons.begin(), oldCoupons.end(),
                                         [](const auto& value) { return !value.empty(); });
             oldDetail.use(CArg::Coupons) && it != oldCoupons.end()) oldValue = *it;
    else if (oldDetail.use(CArg::CellName) && !oldCellName.empty()) oldValue = oldCellName;
    else if (const auto it = std::find_if(oldCouponNames.begin(), oldCouponNames.end(),
                                         [](const auto& value) { return !value.empty(); });
             oldDetail.use(CArg::CouponNames) && it != oldCouponNames.end()) oldValue = *it;
    else if (oldDetail.use(CArg::CardGroup) && !oldCardGroup.empty()) oldValue = oldCardGroup;
    reset(CArg::Area, [this](auto next) { area(std::move(next)); }, base ? base->area_ : std::uint64_t{}, std::uint64_t{});
    reset(CArg::Battle, [this](auto next) { battle(std::move(next)); }, base ? base->battle_ : std::uint64_t{}, std::uint64_t{});
    reset(CArg::Package, [this](auto next) { packages(std::move(next)); }, base ? base->packages_ : std::uint64_t{}, std::uint64_t{});
    reset(CArg::Flag, [this](auto next) { flag(std::move(next)); }, base ? base->flag_ : String{}, String{});
    reset(CArg::Step, [this](auto next) { step(std::move(next)); }, base ? base->step_ : String{}, String{});
    reset(CArg::Variant, [this](auto next) { variant(std::move(next)); }, base ? base->variant_ : String{}, String{});
    reset(CArg::Expression, [this](auto next) { expression(std::move(next)); }, base ? base->expression_ : String{}, String{});
    if (!newDetail.use(CArg::TalkerC)) cardImages({});
    else if (base && !oldDetail.use(CArg::TalkerC)) {
        auto images = base->card_images_;
        if (images.empty()) for (const auto& path : base->card_paths_)
            images.push_back(std::make_shared<card::CardImage>(path));
        cardImages(std::move(images));
    }
    reset(CArg::BgmPath, [this](auto next) { bgmPath(std::move(next)); }, base ? base->bgmPath_ : String{}, String{});
    reset(CArg::BgmChannel, [this](auto next) { bgmChannel(std::move(next)); }, base ? base->bgmChannel_ : std::uint32_t{}, std::uint32_t{});
    reset(CArg::BgmVolume, [this](auto next) { bgmVolume(std::move(next)); }, base ? base->bgmVolume_ : std::uint32_t{100}, std::uint32_t{100});
    reset(CArg::BgmLoopCount, [this](auto next) { bgmLoopCount(std::move(next)); }, base ? base->bgmLoopCount_ : std::uint32_t{}, std::uint32_t{});
    reset(CArg::BgmFadeIn, [this](auto next) { bgmFadeIn(std::move(next)); }, base ? base->bgmFadeIn_ : std::uint32_t{}, std::uint32_t{});
    reset(CArg::SoundPath, [this](auto next) { soundPath(std::move(next)); }, base ? base->soundPath_ : String{}, String{});
    reset(CArg::SoundChannel, [this](auto next) { soundChannel(std::move(next)); }, base ? base->soundChannel_ : std::uint32_t{}, std::uint32_t{});
    reset(CArg::SoundVolume, [this](auto next) { soundVolume(std::move(next)); }, base ? base->soundVolume_ : std::uint32_t{100}, std::uint32_t{100});
    reset(CArg::SoundLoopCount, [this](auto next) { soundLoopCount(std::move(next)); }, base ? base->soundLoopCount_ : std::uint32_t{1}, std::uint32_t{1});
    reset(CArg::SoundFadeIn, [this](auto next) { soundFadeIn(std::move(next)); }, base ? base->soundFadeIn_ : std::uint32_t{}, std::uint32_t{});
    reset(CArg::InitialEffect, [this](auto next) { initialEffect(std::move(next)); }, base ? base->initialEffect_ : false, false);
    reset(CArg::InitialSoundPath, [this](auto next) { initialSoundPath(std::move(next)); }, base ? base->initialSoundPath_ : String{}, String{});
    reset(CArg::InitialSoundChannel, [this](auto next) { initialSoundChannel(std::move(next)); }, base ? base->initialSoundChannel_ : std::uint32_t{}, std::uint32_t{});
    reset(CArg::InitialSoundVolume, [this](auto next) { initialSoundVolume(std::move(next)); }, base ? base->initialSoundVolume_ : std::uint32_t{100}, std::uint32_t{100});
    reset(CArg::InitialSoundLoopCount, [this](auto next) { initialSoundLoopCount(std::move(next)); }, base ? base->initialSoundLoopCount_ : std::uint32_t{1}, std::uint32_t{1});
    reset(CArg::InitialSoundFadeIn, [this](auto next) { initialSoundFadeIn(std::move(next)); }, base ? base->initialSoundFadeIn_ : std::uint32_t{}, std::uint32_t{});
    reset(CArg::Cast, [this](auto next) { casts(std::move(next)); }, base ? base->casts_ : std::uint64_t{}, std::uint64_t{});
    reset(CArg::Item, [this](auto next) { item(std::move(next)); }, base ? base->item_ : std::uint64_t{}, std::uint64_t{});
    reset(CArg::Skill, [this](auto next) { skill(std::move(next)); }, base ? base->skill_ : std::uint64_t{}, std::uint64_t{});
    reset(CArg::Beast, [this](auto next) { beast(std::move(next)); }, base ? base->beast_ : std::uint64_t{}, std::uint64_t{});
    reset(CArg::Info, [this](auto next) { info(std::move(next)); }, base ? base->info_ : std::uint64_t{}, std::uint64_t{});
    reset(CArg::Start, [this](auto next) { start(std::move(next)); }, base ? base->start_ : String{}, String{});
    reset(CArg::Gossip, [this](auto next) { gossip(std::move(next)); }, base ? base->gossip_ : String{}, String{});
    reset(CArg::CompleteStamp, [this](auto next) { completeStamp(std::move(next)); }, base ? base->completeStamp_ : String{}, String{});
    reset(CArg::Mental, [this](auto next) { mental(std::move(next)); }, base ? base->mental_ : types::Mental{}, types::Mental{});
    reset(CArg::Physical, [this](auto next) { physical(std::move(next)); }, base ? base->physical_ : types::Physical{}, types::Physical{});
    reset(CArg::Status, [this](auto next) { status(std::move(next)); }, base ? base->status_ : types::Status::Active, types::Status::Active);
    const auto rangeDefault = (newDetail.use(CArg::Coupon) || value == CType::BranchMultiCoupon) ? types::Range::Selected : types::Range::Field;
    reset(CArg::Range, [this](auto next) { range(std::move(next)); }, base ? base->range_ : rangeDefault, rangeDefault);
    reset(CArg::CardVisual, [this](auto next) { cardVisual(std::move(next)); }, base ? base->cardVisual_ : types::CardVisual::None, types::CardVisual::None);
    reset(CArg::EffectType, [this](auto next) { effectType(std::move(next)); }, base ? base->effectType_ : types::EffectType::None, types::EffectType::None);
    reset(CArg::Resist, [this](auto next) { resist(std::move(next)); }, base ? base->resist_ : types::Resist::Unfail, types::Resist::Unfail);
    reset(CArg::Transition, [this](auto next) { transition(std::move(next)); }, base ? base->transition_ : types::Transition::Default, types::Transition::Default);
    reset(CArg::TargetAll, [this](auto next) { targetAll(std::move(next)); }, base ? base->targetAll_ : false, false);
    reset(CArg::SelectionMethod, [this](auto next) { selectionMethod(std::move(next)); }, base ? base->selectionMethod_ : types::SelectionMethod::Manual, types::SelectionMethod::Manual);
    reset(CArg::Average, [this](auto next) { average(std::move(next)); }, base ? base->average_ : false, false);
    reset(CArg::Complete, [this](auto next) { complete(std::move(next)); }, base ? base->complete_ : false, false);
    reset(CArg::UnsignedLevel, [this](auto next) { unsignedLevel(std::move(next)); }, base ? base->unsignedLevel_ : 1, 1);
    reset(CArg::SignedLevel, [this](auto next) { signedLevel(std::move(next)); }, base ? base->signedLevel_ : 0, 0);
    reset(CArg::SuccessRate, [this](auto next) { successRate(std::move(next)); }, base ? base->successRate_ : 5, 5);
    reset(CArg::TransitionSpeed, [this](auto next) { transitionSpeed(std::move(next)); }, base ? base->transitionSpeed_ : 5, 5);
    reset(CArg::CardSpeed, [this](auto next) { cardSpeed(std::move(next)); }, base ? base->cardSpeed_ : -1, -1);
    reset(CArg::OverrideCardSpeed, [this](auto next) { overrideCardSpeed(std::move(next)); }, base ? base->overrideCardSpeed_ : false, false);
    reset(CArg::Percent, [this](auto next) { percent(std::move(next)); }, base ? base->percent_ : 50, 50);
    reset(CArg::FlagValue, [this](auto next) { flagValue(std::move(next)); }, base ? base->flagValue_ : true, true);
    reset(CArg::StepValue, [this](auto next) { stepValue(std::move(next)); }, base ? base->stepValue_ : 0, 0);
    reset(CArg::CouponValue, [this](auto next) { couponValue(std::move(next)); }, base ? base->couponValue_ : 0, 0);
    reset(CArg::PartyNumber, [this](auto next) { partyNumber(std::move(next)); }, base ? base->partyNumber_ : 2, 2);
    reset(CArg::CardNumber, [this](auto next) { cardNumber(std::move(next)); }, base ? base->cardNumber_ : 1, 1);
    reset(CArg::Money, [this](auto next) { money(std::move(next)); }, base ? base->money_ : 0, 0);
    reset(CArg::Wait, [this](auto next) { wait(std::move(next)); }, base ? base->wait_ : 10, 10);
    reset(CArg::Flag2, [this](auto next) { flag2(std::move(next)); }, base ? base->flag2_ : String{}, String{});
    reset(CArg::Step2, [this](auto next) { step2(std::move(next)); }, base ? base->step2_ : String{}, String{});
    reset(CArg::CastRange, [this](auto next) { castRange(std::move(next)); }, base ? base->cast_range_ : lib::Array<types::CastRange>{types::CastRange::Party}, lib::Array<types::CastRange>{types::CastRange::Party});
    reset(CArg::LevelMin, [this](auto next) { levelMin(std::move(next)); }, base ? base->levelMin_ : 0, 0);
    reset(CArg::LevelMax, [this](auto next) { levelMax(std::move(next)); }, base ? base->levelMax_ : 0, 0);
    reset(CArg::KeyCodeRange, [this](auto next) { keyCodeRange(std::move(next)); }, base ? base->keyCodeRange_ : types::Range::PartyAndBackpack, types::Range::PartyAndBackpack);
    reset(CArg::TargetIsSkill, [this](auto next) { targetIsSkill(std::move(next)); }, base ? base->targetIsSkill_ : true, true);
    reset(CArg::TargetIsItem, [this](auto next) { targetIsItem(std::move(next)); }, base ? base->targetIsItem_ : true, true);
    reset(CArg::TargetIsBeast, [this](auto next) { targetIsBeast(std::move(next)); }, base ? base->targetIsBeast_ : true, true);
    reset(CArg::TargetIsHand, [this](auto next) { targetIsHand(std::move(next)); }, base ? base->targetIsHand_ : false, false);
    reset(CArg::KeyCode, [this](auto next) { keyCode(std::move(next)); }, base ? base->keyCode_ : String{}, String{});
    const auto initValueDefault = value == CType::TalkDialog ? 1 : 0;
    reset(CArg::InitValue, [this](auto next) { initValue(std::move(next)); }, base ? base->initValue_ : initValueDefault, initValueDefault);
    reset(CArg::Comparison4, [this](auto next) { comparison4(std::move(next)); }, base ? base->comparison4_ : types::Comparison4::Eq, types::Comparison4::Eq);
    reset(CArg::Comparison3, [this](auto next) { comparison3(std::move(next)); }, base ? base->comparison3_ : types::Comparison3::Eq, types::Comparison3::Eq);
    reset(CArg::Round, [this](auto next) { round(std::move(next)); }, base ? base->round_ : std::uint32_t{}, std::uint32_t{});
    reset(CArg::CellName, [this](auto next) { cellName(std::move(next)); }, base ? base->cellName_ : String{}, String{});
    reset(CArg::CardGroup, [this](auto next) { cardGroup(std::move(next)); }, base ? base->cardGroup_ : String{}, String{});
    reset(CArg::PositionType, [this](auto next) { positionType(std::move(next)); }, base ? base->positionType_ : types::CoordinateType::None, types::CoordinateType::None);
    reset(CArg::X, [this](auto next) { x(std::move(next)); }, base ? base->x_ : 0, 0);
    reset(CArg::Y, [this](auto next) { y(std::move(next)); }, base ? base->y_ : 0, 0);
    reset(CArg::SizeType, [this](auto next) { sizeType(std::move(next)); }, base ? base->sizeType_ : types::CoordinateType::None, types::CoordinateType::None);
    reset(CArg::Width, [this](auto next) { width(std::move(next)); }, base ? base->width_ : 0, 0);
    reset(CArg::Height, [this](auto next) { height(std::move(next)); }, base ? base->height_ : 0, 0);
    reset(CArg::Scale, [this](auto next) { scale(std::move(next)); }, base ? base->scale_ : -1, -1);
    reset(CArg::Layer, [this](auto next) { layer(std::move(next)); }, base ? base->layer_ : -1, -1);
    reset(CArg::DoAnime, [this](auto next) { doAnime(std::move(next)); }, base ? base->doAnime_ : false, false);
    reset(CArg::IgnoreEffectBooster, [this](auto next) { ignoreEffectBooster(std::move(next)); }, base ? base->ignoreEffectBooster_ : true, true);
    reset(CArg::SingleLine, [this](auto next) { singleLine(std::move(next)); }, base ? base->singleLine_ : false, false);
    reset(CArg::SelectionColumns, [this](auto next) { selectionColumns(std::move(next)); }, base ? base->selectionColumns_ : std::uint32_t{1}, std::uint32_t{1});
    reset(CArg::CenteringX, [this](auto next) { centeringX(std::move(next)); }, base ? base->centeringX_ : false, false);
    reset(CArg::CenteringY, [this](auto next) { centeringY(std::move(next)); }, base ? base->centeringY_ : false, false);
    reset(CArg::BoundaryCheck, [this](auto next) { boundaryCheck(std::move(next)); }, base ? base->boundaryCheck_ : false, false);
    reset(CArg::StartAction, [this](auto next) { startAction(std::move(next)); }, base ? base->startAction_ : types::StartAction::NextRound, types::StartAction::NextRound);
    reset(CArg::Ignite, [this](auto next) { ignite(std::move(next)); }, base ? base->ignite_ : false, false);
    reset(CArg::KeyCodes, [this](auto next) { keyCodes(std::move(next)); }, base ? base->key_codes_ : lib::Array<String>{}, lib::Array<String>{});
    reset(CArg::MatchingType, [this](auto next) { matchingType(std::move(next)); }, base ? base->matchingType_ : types::MatchingType::And, types::MatchingType::And);
    reset(CArg::ExpandSPChars, [this](auto next) { expandSPChars(std::move(next)); }, base ? base->expandSPChars_ : false, false);
    reset(CArg::HoldingCoupon, [this](auto next) { holdingCoupon(std::move(next)); }, base ? base->holdingCoupon_ : String{}, String{});
    reset(CArg::RefAbility, [this](auto next) { refAbility(std::move(next)); }, base ? base->refAbility_ : false, false);
    reset(CArg::AbsorbTo, [this](auto next) { absorbTo(std::move(next)); }, base ? base->absorbTo_ : AbsorbTo::None, AbsorbTo::None);
    reset(CArg::SelectCard, [this](auto next) { selectCard(std::move(next)); }, base ? base->selectCard_ : false, false);
    reset(CArg::SelectTalker, [this](auto next) { selectTalker(std::move(next)); }, base ? base->selectTalker_ : false, false);
    reset(CArg::InvertResult, [this](auto next) { invertResult(std::move(next)); }, base ? base->invertResult_ : false, false);
    reset(CArg::MatchingCondition, [this](auto next) { matchingCondition(std::move(next)); }, base ? base->matchingCondition_ : types::MatchingCondition::Has, types::MatchingCondition::Has);
    reset(CArg::BackpackEnabled, [this](auto next) { backpackEnabled(std::move(next)); }, base ? base->backpackEnabled_ : types::EnvironmentStatus::NotSet, types::EnvironmentStatus::NotSet);
    reset(CArg::RunAwayEnabled, [this](auto next) { runAwayEnabled(std::move(next)); }, base ? base->runAwayEnabled_ : types::EnvironmentStatus::NotSet, types::EnvironmentStatus::NotSet);
    reset(CArg::ConsumeCard, [this](auto next) { consumeCard(std::move(next)); }, base ? base->consumeCard_ : true, true);

    if (!newDetail.use(CArg::Motions)) motions({});
    else if (base && !oldDetail.use(CArg::Motions)) {
        lib::Array<lib::Owner<motion::Motion>> values;
        for (const auto& item : base->motions_) if (item) values.push_back(item->dup());
        motions(std::move(values));
    }

    bool resetText = true;
    bool resetDialogs = true;
    const auto status = dialogStatus ? *dialogStatus : structs::DialogStatus::Top;
    if (newDetail.use(CArg::Text) && !newDetail.use(CArg::Dialogs) && !dialogs_.empty()) {
        auto selected = dialogs_.front();
        if (status == structs::DialogStatus::Under) selected = dialogs_.back();
        else if (status == structs::DialogStatus::UnderWithCoupon) {
            selected = dialogs_.back();
            for (auto it = dialogs_.rbegin(); it != dialogs_.rend(); ++it)
                if (*it && !(*it)->rCoupons().empty()) { selected = *it; break; }
        }
        const auto message = selected ? selected->text() : String{};
        text(message);
        if (!message.empty()) resetText = false;
    } else if (newDetail.use(CArg::Dialogs) && !newDetail.use(CArg::Text)) {
        const auto message = text_;
        if (message.empty()) dialogs({std::make_shared<SDialog>()});
        else {
            lib::Array<lib::Owner<SDialog>> values;
            if (base) for (const auto& dialog : base->dialogs_) if (dialog) values.push_back(dialog->dup());
            // Dは初期値の一覧を直接添字で参照する。空配列を参照せ
            // ず、C++側の空の初期値に対する代替処理を維持する。
            if (values.empty()) values.push_back(std::make_shared<SDialog>());
            dialogs(std::move(values));
            auto selected = dialogs_.front();
            if (status == structs::DialogStatus::Under) selected = dialogs_.back();
            else if (status == structs::DialogStatus::UnderWithCoupon) {
                selected = dialogs_.back();
                for (auto it = dialogs_.rbegin(); it != dialogs_.rend(); ++it)
                    if (*it && !(*it)->rCoupons().empty()) { selected = *it; break; }
            }
            selected->text(message);
            resetDialogs = false;
        }
    }
    if (resetText) reset(CArg::Text, [this](auto next) { text(std::move(next)); }, base ? base->text_ : String{}, String{});
    if (resetDialogs) {
        if (!newDetail.use(CArg::Dialogs)) dialogs({});
        else if (base && !oldDetail.use(CArg::Dialogs)) {
            lib::Array<lib::Owner<SDialog>> values;
            for (const auto& dialog : base->dialogs_) if (dialog) values.push_back(dialog->dup());
            dialogs(std::move(values));
        }
    }
    reset(CArg::TargetS, [this](auto next) { targetS(std::move(next)); }, base ? base->target_s_ : types::Target{}, types::Target{});
    reset(CArg::TalkerNC, [this](auto next) { talkerNC(std::move(next)); }, base ? base->talkerNC_ : types::Talker::Selected, types::Talker::Selected);
    if (!newDetail.use(CArg::BgImages)) backgroundImages({});
    else if (base && !oldDetail.use(CArg::BgImages)) {
        background::BgImageArray values;
        for (const auto& image : base->backgrounds_) if (image) values.push_back(image->dup());
        backgroundImages(std::move(values));
    }
    if (!newDetail.use(CArg::Coupons)) coupons({});
    else if (base && !oldDetail.use(CArg::Coupons)) {
        lib::Array<lib::Owner<coupon::Coupon>> values;
        for (const auto& item : base->coupon_records_) if (item) values.push_back(item->dup());
        coupons(std::move(values));
    }
    if ((type_ == CType::GetCoupon || type_ == CType::LoseCoupon) &&
        (range_ == types::Range::Field || range_ == types::Range::Npc)) range(types::Range::Selected);
    if (type_ == CType::BranchCoupon && !oldDetail.use(CArg::Range)) range(types::Range::Selected);

    bool resetCoupon = true;
    bool resetCouponNames = true;
    if (oldDetail.use(CArg::Coupon) && newDetail.use(CArg::CouponNames)) {
        if (!coupon_.empty()) { couponNames({coupon_}); resetCouponNames = false; }
    } else if (oldDetail.use(CArg::CouponNames) && newDetail.use(CArg::Coupon)) {
        if (!coupon_names_.empty() && !coupon_names_.front().empty()) {
            coupon(coupon_names_.front());
            resetCoupon = false;
        }
    }
    if (resetCoupon) reset(CArg::Coupon, [this](auto next) { coupon(std::move(next)); }, base ? base->coupon_ : String{}, String{});
    if (resetCouponNames) reset(CArg::CouponNames, [this](auto next) { couponNames(lib::SharedSlice<String>(std::move(next))); }, base ? base->coupon_names_ : lib::Array<String>{}, lib::Array<String>{});
    if (newDetail.use(CArg::BgmPath) && canResolve(oldPath, true)) bgmPath(oldPath);
    if (newDetail.use(CArg::SoundPath) && canResolve(oldPath, false)) soundPath(oldPath);
    if (!oldValue.empty()) {
        if (newDetail.use(CArg::Coupon) && coupon_.empty()) coupon(oldValue);
        else if (newDetail.use(CArg::Gossip) && gossip_.empty()) gossip(oldValue);
        else if (newDetail.use(CArg::CompleteStamp) && completeStamp_.empty()) completeStamp(oldValue);
        else if (newDetail.use(CArg::KeyCode) && keyCode_.empty()) keyCode(oldValue);
        else if (newDetail.use(CArg::Coupons) && coupon_records_.empty())
            coupons({std::make_shared<coupon::Coupon>(oldValue, 0)});
        else if (newDetail.use(CArg::CellName) && cellName_.empty()) cellName(oldValue);
        else if (newDetail.use(CArg::CouponNames) && coupon_names_.empty()) couponNames({oldValue});
        else if (newDetail.use(CArg::CardGroup) && cardGroup_.empty()) cardGroup(oldValue);
    }
    if (!oldDetail.use(CArg::Range) && (oldDetail.use(CArg::Gossip) || oldDetail.use(CArg::CompleteStamp) ||
        oldDetail.use(CArg::CellName) || oldDetail.use(CArg::CardGroup))) {
        if (type_ == CType::BranchCoupon) range(types::Range::Random);
        else if (type_ == CType::GetCoupon || type_ == CType::LoseCoupon) range(types::Range::Party);
    }
    if (newDetail.use(CArg::Flag) && newDetail.use(CArg::Step) && newDetail.use(CArg::Variant)) {
        if (!variant_.empty()) { flag({}); step({}); }
        else if (!step_.empty()) { flag({}); variant({}); }
        else if (!flag_.empty()) {
            // Dはここで選択したフラグ自体を誤って解除するが、C++側ではフラグを保持し、ステップとコモンだけを解除する。
            step({}); variant({});
        }
    }
    validate();
    if (use_counter_) rebuildUseCounterUsers();
}
void Content::validate() {
    synchronizeValueProducers();
    if (type_ == CType::BranchStatus && status_ == types::Status::None) status_ = types::Status::Active;
    const auto in = [](types::Range value, std::initializer_list<types::Range> allowed) {
        return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
    };
    if (type_ == CType::Effect &&
        !in(range_, {types::Range::Selected, types::Range::Random, types::Range::Party,
                     types::Range::CouponHolder, types::Range::CardTarget})) {
        range_ = types::Range::Selected;
    }
    switch (type_) {
    case CType::BranchStatus:
        if (!in(range_, {types::Range::Selected, types::Range::Random, types::Range::Party}))
            range_ = types::Range::Selected;
        break;
    case CType::GetCoupon:
    case CType::LoseCoupon:
        if (!in(range_, {types::Range::Selected, types::Range::Random, types::Range::Party,
                         types::Range::CouponHolder}))
            range_ = types::Range::Selected;
        break;
    case CType::BranchCoupon:
    case CType::BranchMultiCoupon:
        if (!in(range_, {types::Range::Selected, types::Range::Random, types::Range::Party,
                         types::Range::Field, types::Range::Npc}))
            range_ = types::Range::Selected;
        break;
    case CType::BranchSkill:
    case CType::BranchItem:
    case CType::BranchBeast:
    case CType::GetSkill:
    case CType::GetItem:
    case CType::GetBeast:
    case CType::LoseSkill:
    case CType::LoseItem:
    case CType::LoseBeast:
        if (!in(range_, {types::Range::Selected, types::Range::Random, types::Range::Party,
                         types::Range::Backpack, types::Range::PartyAndBackpack,
                         types::Range::Field, types::Range::SelectedCard}))
            range_ = types::Range::Selected;
        break;
    default:
        break;
    }
    if (type_ == CType::BranchKeyCode &&
        !in(keyCodeRange_, {types::Range::Selected, types::Range::Random,
                            types::Range::Backpack, types::Range::PartyAndBackpack,
                            types::Range::SelectedCard})) {
        keyCodeRange_ = types::Range::PartyAndBackpack;
    }
    if (selectionColumns_ == 0) selectionColumns_ = 1;
}

lib::Owner<CWXPath> Content::connectedResource(summary::Summary* summ) {
    return resolveConnectedResource(*this, summ);
}

lib::Owner<const CWXPath> Content::connectedResource(const summary::Summary* summ) const {
    return resolveConnectedResource(*this, summ);
}

lib::Array<usecounter::CouponId> Content::connectedCoupons() const {
    lib::Array<usecounter::CouponId> result;
    const auto d = contentDetail(type_);
    if (d.use(CArg::Coupon)) result.push_back(usecounter::toCouponId(coupon()));
    if (d.use(CArg::Coupons)) {
        for (const auto& value : coupon_records_)
            if (!value) throw EventException("Null coupon");
            else result.push_back(usecounter::toCouponId(value->name()));
    }
    if (d.use(CArg::CouponNames))
        for (const auto& value : couponNames()) result.push_back(usecounter::toCouponId(value));
    return result;
}
lib::Array<usecounter::GossipId> Content::connectedGossips() const {
    if (contentDetail(type_).use(CArg::Gossip) && !gossip_.empty()) return {usecounter::toGossipId(gossip_)};
    return {};
}
lib::Array<usecounter::CompleteStampId> Content::connectedCompleteStamps() const {
    if (contentDetail(type_).use(CArg::CompleteStamp) && !completeStamp_.empty()) return {usecounter::toCompleteStampId(completeStamp_)};
    return {};
}
lib::Array<usecounter::KeyCodeId> Content::connectedKeyCodes() const {
    lib::Array<usecounter::KeyCodeId> result;
    const auto d = contentDetail(type_);
    if (d.use(CArg::KeyCode) && !keyCode_.empty()) result.push_back(usecounter::toKeyCodeId(keyCode_));
    if (d.use(CArg::KeyCodes)) for (const auto& value : key_codes_) if (!value.empty()) result.push_back(usecounter::toKeyCodeId(value));
    return result;
}
lib::Array<usecounter::CellNameId> Content::connectedCellNames() const {
    if (contentDetail(type_).use(CArg::CellName) && !cellName_.empty()) return {usecounter::toCellNameId(cellName_)};
    return {};
}
lib::Array<usecounter::CardGroupId> Content::connectedCardGroups() const {
    if (contentDetail(type_).use(CArg::CardGroup) && !cardGroup_.empty()) return {usecounter::toCardGroupId(cardGroup_)};
    return {};
}
String Content::connectedFile() const {
    const auto d = contentDetail(type_);
    if (d.use(CArg::TalkerC)) {
        for (const auto& image : card_images_)
            if (image && !image->path().empty() && !utils::isBinImg(image->path()))
                return image->path();
    }
    if (d.use(CArg::BgmPath) && !bgmPath_.empty()) return bgmPath_;
    if (d.use(CArg::SoundPath) && !soundPath_.empty()) return soundPath_;
    if (d.use(CArg::InitialSoundPath) && !initialSoundPath_.empty()) return initialSoundPath_;
    return {};
}

void Content::putNodeData(XNode& node, const card::XMLOption* option) const {
    const auto d = contentDetail(type_);
    if (!d.type.empty()) node.newAttr("type", d.type);
    String serializedName = name_;
    if (serializedName.empty()) {
        const auto owner = parent();
        if (owner && owner->type() == CType::CustomXmlContent) {
            // Dの直列化時の表現はリテラルのU+3007。設定
            // 可能な表示ラベルとは独立し、モデルも変更しない。
            static const std::set<String> blankNameParents{
                "BranchFlag", "BranchFlagValue", "BranchStep", "BranchStepValue",
                "BranchSelect", "BranchAbility", "BranchRandom", "BranchLevel",
                "BranchStatus", "BranchPartyNumber", "BranchRound", "BranchCast",
                "BranchItem", "BranchSkill", "BranchBeast", "BranchInfo",
                "BranchMoney", "BranchCoupon", "BranchCompleteStamp",
                "BranchGossip", "BranchKeyCode"};
            if (blankNameParents.contains(owner->xmlType())) serializedName = "〇";
        }
    }
    node.newAttr("name", serializedName);
    if (!comment_.empty()) node.newAttr("comment", comment_);

    putAttr(node, d, CArg::Area, area_); putAttr(node, d, CArg::Battle, battle_); putAttr(node, d, CArg::Package, packages_);
    const bool choose_variable = d.use(CArg::Flag) && d.use(CArg::Step) && d.use(CArg::Variant);
    putAttr(node, d, CArg::Flag, flag_, choose_variable, String{});
    putAttr(node, d, CArg::Step, step_, choose_variable, String{});
    putAttr(node, d, CArg::Variant, variant_, choose_variable, String{});
    putAttr(node, d, CArg::BgmPath, utils::encodePath(bgmPath_)); putAttr(node, d, CArg::BgmChannel, bgmChannel_); putAttr(node, d, CArg::BgmVolume, bgmVolume_); putAttr(node, d, CArg::BgmLoopCount, bgmLoopCount_); putAttr(node, d, CArg::BgmFadeIn, bgmFadeIn_);
    putAttr(node, d, CArg::SoundPath, utils::encodePath(soundPath_)); putAttr(node, d, CArg::SoundChannel, soundChannel_); putAttr(node, d, CArg::SoundVolume, soundVolume_); putAttr(node, d, CArg::SoundLoopCount, soundLoopCount_); putAttr(node, d, CArg::SoundFadeIn, soundFadeIn_);
    putAttr(node, d, CArg::InitialEffect, initialEffect_, true, false); putAttr(node, d, CArg::InitialSoundPath, utils::encodePath(initialSoundPath_), true, String{}); putAttr(node, d, CArg::InitialSoundChannel, initialSoundChannel_, true, std::uint32_t{}); putAttr(node, d, CArg::InitialSoundVolume, initialSoundVolume_, true, std::uint32_t{100}); putAttr(node, d, CArg::InitialSoundLoopCount, initialSoundLoopCount_, true, std::uint32_t{1}); putAttr(node, d, CArg::InitialSoundFadeIn, initialSoundFadeIn_, true, std::uint32_t{});
    putAttr(node, d, CArg::Cast, casts_); putAttr(node, d, CArg::Item, item_); putAttr(node, d, CArg::Skill, skill_); putAttr(node, d, CArg::Beast, beast_); putAttr(node, d, CArg::Info, info_);
    putAttr(node, d, CArg::Start, start_); putAttr(node, d, CArg::Coupon, coupon_); putAttr(node, d, CArg::Gossip, gossip_); putAttr(node, d, CArg::CompleteStamp, completeStamp_);
    putAttr(node, d, CArg::Mental, mental_); putAttr(node, d, CArg::Physical, physical_); putAttr(node, d, CArg::Status, status_); putAttr(node, d, CArg::Range, range_); putAttr(node, d, CArg::CardVisual, cardVisual_); putAttr(node, d, CArg::EffectType, effectType_); putAttr(node, d, CArg::Resist, resist_); putAttr(node, d, CArg::Transition, transition_);
    putAttr(node, d, CArg::TargetAll, targetAll_); putAttr(node, d, CArg::Average, average_); putAttr(node, d, CArg::Complete, complete_);
    putAttr(node, d, CArg::TargetS, types::fromTarget(target_s_));
    putAttr(node, d, CArg::TalkerNC, talkerText(talkerNC_));
    putAttr(node, d, CArg::UnsignedLevel, unsignedLevel_); putAttr(node, d, CArg::SignedLevel, signedLevel_); putAttr(node, d, CArg::SuccessRate, successRate_); putAttr(node, d, CArg::TransitionSpeed, transitionSpeed_); if (d.use(CArg::CardSpeed) && cardSpeed_ != -1) putAttr(node, d, CArg::CardSpeed, cardSpeed_); putAttr(node, d, CArg::Percent, percent_); putAttr(node, d, CArg::FlagValue, flagValue_); putAttr(node, d, CArg::StepValue, stepValue_); putAttr(node, d, CArg::CouponValue, couponValue_); putAttr(node, d, CArg::PartyNumber, partyNumber_); putAttr(node, d, CArg::CardNumber, cardNumber_); putAttr(node, d, CArg::Money, money_); putAttr(node, d, CArg::Wait, wait_);
    putAttr(node, d, CArg::Flag2, flag2_); putAttr(node, d, CArg::Step2, step2_); putAttr(node, d, CArg::LevelMin, levelMin_); putAttr(node, d, CArg::LevelMax, levelMax_); putAttr(node, d, CArg::KeyCodeRange, keyCodeRange_); putAttr(node, d, CArg::KeyCode, keyCode_); putAttr(node, d, CArg::InitValue, initValue_); putAttr(node, d, CArg::Comparison4, comparison4_); putAttr(node, d, CArg::Comparison3, comparison3_); putAttr(node, d, CArg::Round, round_);
    putAttr(node, d, CArg::CellName, cellName_, true, String{}); putAttr(node, d, CArg::CardGroup, cardGroup_, true, String{}); putAttr(node, d, CArg::PositionType, positionType_, true, types::CoordinateType::None); putAttr(node, d, CArg::X, x_, true, 0); putAttr(node, d, CArg::Y, y_, true, 0); putAttr(node, d, CArg::SizeType, sizeType_, true, types::CoordinateType::None); putAttr(node, d, CArg::Width, width_, true, 0); putAttr(node, d, CArg::Height, height_, true, 0); putAttr(node, d, CArg::Scale, scale_, true, -1); putAttr(node, d, CArg::Layer, layer_, true, -1);
    // Dのevent.dはdoanime属性の省略をtrueとする一方、
    // 新規Contentの初期値はfalse。そのためfalseは明示的
    // に書く必要があり、省略すると保存・読み込みで値が反転してしまう。
    putAttr(node, d, CArg::DoAnime, doAnime_, true, true); putAttr(node, d, CArg::IgnoreEffectBooster, ignoreEffectBooster_, true, false); putAttr(node, d, CArg::SelectionColumns, selectionColumns_, true, std::uint32_t{1}); putAttr(node, d, CArg::CenteringX, centeringX_, true, false); putAttr(node, d, CArg::CenteringY, centeringY_, true, false); putAttr(node, d, CArg::BoundaryCheck, boundaryCheck_, true, false); putAttr(node, d, CArg::StartAction, startAction_, true, types::StartAction::Now); putAttr(node, d, CArg::Ignite, ignite_, true, false);
    putAttr(node, d, CArg::HoldingCoupon, holdingCoupon_, true, StringView{}); /* D atnPutD: 空文字は書かない */ putAttr(node, d, CArg::RefAbility, refAbility_, true, false); putAttr(node, d, CArg::AbsorbTo, absorbTo_, true, AbsorbTo::None); putAttr(node, d, CArg::SelectCard, selectCard_, true, false); putAttr(node, d, CArg::SelectTalker, selectTalker_, true, false); putAttr(node, d, CArg::InvertResult, invertResult_, true, false); putAttr(node, d, CArg::MatchingCondition, matchingCondition_, true, types::MatchingCondition::Has); putAttr(node, d, CArg::BackpackEnabled, backpackEnabled_, true, types::EnvironmentStatus::NotSet); putAttr(node, d, CArg::GameOverEnabled, gameOverEnabled_, true, types::EnvironmentStatus::NotSet); putAttr(node, d, CArg::RunAwayEnabled, runAwayEnabled_, true, types::EnvironmentStatus::NotSet); putAttr(node, d, CArg::ConsumeCard, consumeCard_, true, true); putAttr(node, d, CArg::OverrideCardSpeed, overrideCardSpeed_, true, false); putAttr(node, d, CArg::ExpandSPChars, expandSPChars_, true, false);

    if (d.use(CArg::XmlType)) node.newAttr("xmltype", xmlType_);
    putAttr(node, d, CArg::XmlAssignment, xmlAssignment_);
    if (d.use(CArg::Motions)) {
        auto motions = node.newElement("Motions");
        for (const auto& item : motions_) if (item)
            static_cast<void>(item->toNode(motions, option
                ? lib::Owner<card::XMLOption>(const_cast<card::XMLOption*>(option), [](card::XMLOption*) {})
                : lib::Owner<card::XMLOption>{}));
    }
    if (d.use(CArg::Text)) node.newElement("Text", utils::encodeLf(text_));
    if (d.use(CArg::XmlStr)) { auto str = node.newElement("Str", utils::encodeLf(xmlStr_)); if (!name_.empty()) str.newAttr("name", name_); }
    if (d.use(CArg::Dialogs)) { auto dialogs = node.newElement("Dialogs"); for (const auto& dialog : dialogs_) if (dialog) dialog->toNode(dialogs); }
    if (d.use(CArg::KeyCodes)) node.newElement("KeyCodes", utils::encodeLf(key_codes_, false));
    if (d.use(CArg::Expression)) node.newElement("Expression", expression_);
    if (d.use(CArg::CouponNames)) {
        if (coupon_names_.size() > 1) { auto cs = node.newElement("Coupons"); for (const auto& value : coupon_names_) cs.newElement("Coupon", value); }
        else node.newAttr("coupon", coupon_names_.empty() ? String{} : coupon_names_.front());
    }
    if (d.use(CArg::Coupons)) {
        auto cs = node.newElement("Coupons");
        for (const auto& value : coupon_records_) if (value) value->toNode(cs);
    }
    if (d.use(CArg::MatchingType)) {
        if (coupon_names_.size() > 1) putAttr(node, d, CArg::MatchingType, matchingType_);
        else putAttr(node, d, CArg::MatchingType, matchingType_, true, MatchingType::And);
    }
    if (d.use(CArg::SelectionMethod)) {
        if ((option && option->isTargetVersion("1")) ||
            selectionMethod_ == types::SelectionMethod::Valued) {
            node.newAttr("method", enumText(selectionMethod_));
        } else {
            node.newAttr("random",
                         boolText(selectionMethod_ == types::SelectionMethod::Random));
        }
    }
    if (d.use(CArg::TalkerC)) {
        if (d.use(CArg::SingleLine) && singleLine_) {
            putAttr(node, d, CArg::SingleLine, singleLine_);
        } else {
            auto images = card_images_;
            if (images.empty())
                for (const auto& path : card_paths_)
                    images.push_back(std::make_shared<card::CardImage>(path));
            static_cast<void>(card::CardImage::toNode(node, images, true));
        }
    }
    if (d.use(CArg::BgImages)) {
        auto cells = snapshotBackgroundImages(backgrounds_);
        if (cells.empty()) for (const auto& path : backs_) {
            auto image = std::make_shared<background::ImageCell>();
            image->path(path);
            cells.push_back(std::move(image));
        }
        background::BgImage::toNode(cells, type_ == CType::ChangeBgImage, node);
    }
    if (d.use(CArg::CastRange)) {
        auto ranges = node.newElement("CastRanges");
        for (const auto value : cast_range_) ranges.newElement("CastRange", types::fromCastRange(value));
    }
    const int target_count = static_cast<int>(targetIsSkill_) +
        static_cast<int>(targetIsItem_) + static_cast<int>(targetIsBeast_);
    if (type_ == CType::BranchKeyCode && !targetIsHand_ &&
        (target_count == 1 || target_count == 3)) {
        node.newAttr("effectCardType", target_count == 3 ? "All" :
                     targetIsSkill_ ? "Skill" : targetIsItem_ ? "Item" : "Beast");
    } else {
        if (d.use(CArg::TargetIsSkill)) putAttr(node, d, CArg::TargetIsSkill, targetIsSkill_);
        if (d.use(CArg::TargetIsItem)) putAttr(node, d, CArg::TargetIsItem, targetIsItem_);
        if (d.use(CArg::TargetIsBeast)) putAttr(node, d, CArg::TargetIsBeast, targetIsBeast_);
        if (d.use(CArg::TargetIsHand)) putAttr(node, d, CArg::TargetIsHand, targetIsHand_);
    }
}

XNode Content::toNode(const card::XMLOption* option) const {
    auto holder = XNode::create("Contents");
    auto root = appendToNode(holder, option, true).clone(true);
    root.newAttr("contentId", id_);
    return root;
}
XNode Content::toNode(XNode& parent, const card::XMLOption* option) const {
    return appendToNode(parent, option, false);
}
XNode Content::appendToNode(XNode& parent, const card::XMLOption* option, bool standalone) const {
    // Dの単独オーバーロードは、浅い出力でもContentsLineを使う。
    // 親への追加用オーバーロードは浅い出力を除外するが、巡回処理は共用する。
    const bool line = (!option || option->isTargetVersion("1")) &&
                      (standalone || !option || !option->shallow) &&
                      detail().owner && next_.size() == 1;
    auto container = line ? parent.newElement("ContentsLine") : parent;
    XNode output = line ? container : XNode{};
    const Content* current = this;
    while (current) {
        auto node = container.newElement(contentDetail(current->type_).names.at(0));
        if (!output.valid()) output = node;
        current->putNodeData(node, option);
        // ネイティブの可変な子配列には、契約違反の接続が入る場
        // 合がある。この拡張は所有元の境界内で扱う。Dの通常
        // のモデル構築は、親の前提条件でその接続を拒否する。
        if (!current->detail().owner || current->next_.empty()) break;
        // ContentsLineを使わない場合、各接続は親のContents内に置く。
        auto children = line && current->next_.size() == 1
            ? container : node.newElement("Contents");
        if (option && option->shallow) break;
        if (current->next_.size() == 1) {
            container = children;
            current = checkedContent(current->next_.front()).get();
        } else {
            for (const auto& child : current->next_)
                checkedContent(child)->toNode(children, option);
            break;
        }
    }
    return output;
}
String Content::toXML(const card::XMLOption* option) const {
    // DのtoNodeは文書ルートを作る。共通のC++ノード構築は一時的な保
    // 持ノードの子を返すため、ルート専用のtext()を呼ぶ前に切り離す。
    return toNode(option).clone(true).text();
}

void Content::parseAttributes(const XNode& node, const system::XMLInfo* version) {
    const auto d = contentDetail(type_);
    if (node.hasAttr("comment")) comment_ = node.attr("comment", true);
    if (node.hasAttr("name")) assignName(node.attr("name", true));

    readNumeric(node, d, CArg::Area, area_, [this](auto parsed) { this->area(std::move(parsed)); }); readNumeric(node, d, CArg::Battle, battle_, [this](auto parsed) { this->battle(std::move(parsed)); }); readNumeric(node, d, CArg::Package, packages_, [this](auto parsed) { this->packages(std::move(parsed)); });
    readString(node, d, CArg::Flag, flag_, [this](auto parsed) { this->flag(std::move(parsed)); }); readString(node, d, CArg::Step, step_, [this](auto parsed) { this->step(std::move(parsed)); }); readString(node, d, CArg::Variant, variant_, [this](auto parsed) { this->variant(std::move(parsed)); });
    readString(node, d, CArg::BgmPath, bgmPath_, [this](auto parsed) { this->bgmPath(std::move(parsed)); }); readNumeric(node, d, CArg::BgmChannel, bgmChannel_, [this](auto parsed) { this->bgmChannel(std::move(parsed)); }); readNumeric(node, d, CArg::BgmVolume, bgmVolume_, [this](auto parsed) { this->bgmVolume(std::move(parsed)); }); readNumeric(node, d, CArg::BgmLoopCount, bgmLoopCount_, [this](auto parsed) { this->bgmLoopCount(std::move(parsed)); }); readNumeric(node, d, CArg::BgmFadeIn, bgmFadeIn_, [this](auto parsed) { this->bgmFadeIn(std::move(parsed)); });
    readString(node, d, CArg::SoundPath, soundPath_, [this](auto parsed) { this->soundPath(std::move(parsed)); }); readNumeric(node, d, CArg::SoundChannel, soundChannel_, [this](auto parsed) { this->soundChannel(std::move(parsed)); }); readNumeric(node, d, CArg::SoundVolume, soundVolume_, [this](auto parsed) { this->soundVolume(std::move(parsed)); }); readNumeric(node, d, CArg::SoundLoopCount, soundLoopCount_, [this](auto parsed) { this->soundLoopCount(std::move(parsed)); }); readNumeric(node, d, CArg::SoundFadeIn, soundFadeIn_, [this](auto parsed) { this->soundFadeIn(std::move(parsed)); });
    readAttr(node, d, CArg::InitialEffect, initialEffect_, [this](auto parsed) { this->initialEffect(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false, true); readString(node, d, CArg::InitialSoundPath, initialSoundPath_, [this](auto parsed) { this->initialSoundPath(std::move(parsed)); }, {}, true); readNumeric(node, d, CArg::InitialSoundChannel, initialSoundChannel_, [this](auto parsed) { this->initialSoundChannel(std::move(parsed)); }); readNumeric(node, d, CArg::InitialSoundVolume, initialSoundVolume_, [this](auto parsed) { this->initialSoundVolume(std::move(parsed)); }, std::uint32_t{100}); readNumeric(node, d, CArg::InitialSoundLoopCount, initialSoundLoopCount_, [this](auto parsed) { this->initialSoundLoopCount(std::move(parsed)); }, std::uint32_t{1}); readNumeric(node, d, CArg::InitialSoundFadeIn, initialSoundFadeIn_, [this](auto parsed) { this->initialSoundFadeIn(std::move(parsed)); });
    readNumeric(node, d, CArg::Cast, casts_, [this](auto parsed) { this->casts(std::move(parsed)); }); readNumeric(node, d, CArg::Item, item_, [this](auto parsed) { this->item(std::move(parsed)); }); readNumeric(node, d, CArg::Skill, skill_, [this](auto parsed) { this->skill(std::move(parsed)); }); readNumeric(node, d, CArg::Beast, beast_, [this](auto parsed) { this->beast(std::move(parsed)); }); readNumeric(node, d, CArg::Info, info_, [this](auto parsed) { this->info(std::move(parsed)); });
    readString(node, d, CArg::Start, start_, [this](auto parsed) { this->start(std::move(parsed)); }); if (d.use(CArg::Coupon) && node.hasAttr(attrName(d, CArg::Coupon))) coupon(node.attr(attrName(d, CArg::Coupon), true)); if (d.use(CArg::Gossip) && node.hasAttr(attrName(d, CArg::Gossip))) gossip(node.attr(attrName(d, CArg::Gossip), true)); readString(node, d, CArg::CompleteStamp, completeStamp_, [this](auto parsed) { this->completeStamp(std::move(parsed)); });
    if (d.use(CArg::Mental) && node.hasAttr(attrName(d, CArg::Mental))) mental_ = enumParse<types::Mental>(node.attr(attrName(d, CArg::Mental), true), mental_, types::toMental);
    if (d.use(CArg::Physical) && node.hasAttr(attrName(d, CArg::Physical))) physical_ = enumParse<types::Physical>(node.attr(attrName(d, CArg::Physical), true), physical_, types::toPhysical);
    if (d.use(CArg::Status) && node.hasAttr(attrName(d, CArg::Status))) status_ = enumParse<types::Status>(node.attr(attrName(d, CArg::Status), true), status_, types::toStatus);
    if (d.use(CArg::Range) && node.hasAttr(attrName(d, CArg::Range))) range_ = enumParse<types::Range>(node.attr(attrName(d, CArg::Range), true), range_, types::toRange);
    if (d.use(CArg::CardVisual) && node.hasAttr(attrName(d, CArg::CardVisual))) cardVisual_ = enumParse<types::CardVisual>(node.attr(attrName(d, CArg::CardVisual), true), cardVisual_, types::toCardVisual);
    if (d.use(CArg::EffectType) && node.hasAttr(attrName(d, CArg::EffectType))) effectType_ = enumParse<types::EffectType>(node.attr(attrName(d, CArg::EffectType), true), effectType_, types::toEffectType);
    if (d.use(CArg::Resist) && node.hasAttr(attrName(d, CArg::Resist))) resist_ = enumParse<types::Resist>(node.attr(attrName(d, CArg::Resist), true), resist_, types::toResist);
    if (d.use(CArg::Transition) && node.hasAttr(attrName(d, CArg::Transition))) transition_ = enumParse<types::Transition>(node.attr(attrName(d, CArg::Transition), true), transition_, types::toTransition);
    readNumeric(node, d, CArg::TransitionSpeed, transitionSpeed_, [this](auto parsed) { this->transitionSpeed(std::move(parsed)); }); readAttr(node, d, CArg::CardSpeed, cardSpeed_, [this](auto parsed) { this->cardSpeed(std::move(parsed)); },
        [](StringView value) { return value == "Default" ? -1 : integerText<int>(value); }, -1, true);
    readAttr(node, d, CArg::TargetAll, targetAll_, [this](auto parsed) { this->targetAll(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false); readAttr(node, d, CArg::Average, average_, [this](auto parsed) { this->average(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false); readAttr(node, d, CArg::Complete, complete_, [this](auto parsed) { this->complete(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false);
    if (d.use(CArg::TargetS) && node.hasAttr(attrName(d, CArg::TargetS))) {
        try { target_s_ = types::toTarget(node.attr(attrName(d, CArg::TargetS), true)); } catch (...) {}
    }
    readNumeric(node, d, CArg::UnsignedLevel, unsignedLevel_, [this](auto parsed) { this->unsignedLevel(std::move(parsed)); }); readNumeric(node, d, CArg::SignedLevel, signedLevel_, [this](auto parsed) { this->signedLevel(std::move(parsed)); }); readNumeric(node, d, CArg::SuccessRate, successRate_, [this](auto parsed) { this->successRate(std::move(parsed)); }); readNumeric(node, d, CArg::Percent, percent_, [this](auto parsed) { this->percent(std::move(parsed)); }); percent_ = std::clamp(percent_, 0, 100); readAttr(node, d, CArg::FlagValue, flagValue_, [this](auto parsed) { this->flagValue(std::move(parsed)); }, [](StringView v) { return parseBool(v, true); }, true); readNumeric(node, d, CArg::StepValue, stepValue_, [this](auto parsed) { this->stepValue(std::move(parsed)); }); readNumeric(node, d, CArg::CouponValue, couponValue_, [this](auto parsed) { this->couponValue(std::move(parsed)); }); readNumeric(node, d, CArg::PartyNumber, partyNumber_, [this](auto parsed) { this->partyNumber(std::move(parsed)); }); readNumeric(node, d, CArg::CardNumber, cardNumber_, [this](auto parsed) { this->cardNumber(std::move(parsed)); }); readNumeric(node, d, CArg::Money, money_, [this](auto parsed) { this->money(std::move(parsed)); }); readNumeric(node, d, CArg::Wait, wait_, [this](auto parsed) { this->wait(std::move(parsed)); });
    // XML読み込み後も項目ごとの範囲を維持する。WaitとStepValueは非負にし
    // 、SignedLevelやCouponValueなどの符号付き項目は符号を保つ。
    if (d.use(CArg::Wait) && wait_ < 0) wait_ = 0;
    if (d.use(CArg::StepValue) && stepValue_ < 0) stepValue_ = 0;
    readString(node, d, CArg::Flag2, flag2_, [this](auto parsed) { this->flag2(std::move(parsed)); }); readString(node, d, CArg::Step2, step2_, [this](auto parsed) { this->step2(std::move(parsed)); }); readNumeric(node, d, CArg::LevelMin, levelMin_, [this](auto parsed) { this->levelMin(std::move(parsed)); }); readNumeric(node, d, CArg::LevelMax, levelMax_, [this](auto parsed) { this->levelMax(std::move(parsed)); }); readString(node, d, CArg::KeyCode, keyCode_, [this](auto parsed) { this->keyCode(std::move(parsed)); }); readNumeric(node, d, CArg::InitValue, initValue_, [this](auto parsed) { this->initValue(std::move(parsed)); }); readNumeric(node, d, CArg::Round, round_, [this](auto parsed) { this->round(std::move(parsed)); });
    if (d.use(CArg::KeyCodeRange) && node.hasAttr(attrName(d, CArg::KeyCodeRange))) keyCodeRange_ = enumParse<types::Range>(node.attr(attrName(d, CArg::KeyCodeRange), true), keyCodeRange_, types::toRange);
    if (d.use(CArg::Comparison4) && node.hasAttr(attrName(d, CArg::Comparison4))) comparison4_ = enumParse<types::Comparison4>(node.attr(attrName(d, CArg::Comparison4), true), comparison4_, types::toComparison4);
    if (d.use(CArg::Comparison3) && node.hasAttr(attrName(d, CArg::Comparison3))) comparison3_ = enumParse<types::Comparison3>(node.attr(attrName(d, CArg::Comparison3), true), comparison3_, types::toComparison3);
    readString(node, d, CArg::CellName, cellName_, [this](auto parsed) { this->cellName(std::move(parsed)); }, {}, true); readString(node, d, CArg::CardGroup, cardGroup_, [this](auto parsed) { this->cardGroup(std::move(parsed)); }, {}, true); if (d.use(CArg::PositionType) && node.hasAttr(attrName(d, CArg::PositionType))) positionType_ = enumParse<types::CoordinateType>(node.attr(attrName(d, CArg::PositionType), true), positionType_, types::toCoordinateType); readNumeric(node, d, CArg::X, x_, [this](auto parsed) { this->x(std::move(parsed)); }); readNumeric(node, d, CArg::Y, y_, [this](auto parsed) { this->y(std::move(parsed)); }); if (d.use(CArg::SizeType) && node.hasAttr(attrName(d, CArg::SizeType))) sizeType_ = enumParse<types::CoordinateType>(node.attr(attrName(d, CArg::SizeType), true), sizeType_, types::toCoordinateType); readNumeric(node, d, CArg::Width, width_, [this](auto parsed) { this->width(std::move(parsed)); }); readNumeric(node, d, CArg::Height, height_, [this](auto parsed) { this->height(std::move(parsed)); }); readNumeric(node, d, CArg::Scale, scale_, [this](auto parsed) { this->scale(std::move(parsed)); }, -1); readNumeric(node, d, CArg::Layer, layer_, [this](auto parsed) { this->layer(std::move(parsed)); }, -1);
    readAttr(node, d, CArg::DoAnime, doAnime_, [this](auto parsed) { this->doAnime(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, true, true); readAttr(node, d, CArg::IgnoreEffectBooster, ignoreEffectBooster_, [this](auto parsed) { this->ignoreEffectBooster(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false, true); readAttr(node, d, CArg::SingleLine, singleLine_, [this](auto parsed) { this->singleLine(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false, true); readNumeric(node, d, CArg::SelectionColumns, selectionColumns_, [this](auto parsed) { this->selectionColumns(std::move(parsed)); }, std::uint32_t{1}); readAttr(node, d, CArg::CenteringX, centeringX_, [this](auto parsed) { this->centeringX(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false, true); readAttr(node, d, CArg::CenteringY, centeringY_, [this](auto parsed) { this->centeringY(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false, true); readAttr(node, d, CArg::BoundaryCheck, boundaryCheck_, [this](auto parsed) { this->boundaryCheck(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false, true); if (d.use(CArg::StartAction)) startAction_ = node.hasAttr(attrName(d, CArg::StartAction)) ? enumParse<types::StartAction>(node.attr(attrName(d, CArg::StartAction), true), types::StartAction::Now, types::toStartAction) : types::StartAction::Now; readAttr(node, d, CArg::Ignite, ignite_, [this](auto parsed) { this->ignite(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false, true);
    readString(node, d, CArg::HoldingCoupon, holdingCoupon_, [this](auto parsed) { this->holdingCoupon(std::move(parsed)); }, {}, true); readAttr(node, d, CArg::RefAbility, refAbility_, [this](auto parsed) { this->refAbility(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false, true); if (d.use(CArg::AbsorbTo) && node.hasAttr(attrName(d, CArg::AbsorbTo))) absorbTo_ = enumParse<AbsorbTo>(node.attr(attrName(d, CArg::AbsorbTo), true), absorbTo_, types::toAbsorbTo); readAttr(node, d, CArg::SelectCard, selectCard_, [this](auto parsed) { this->selectCard(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false, true); readAttr(node, d, CArg::SelectTalker, selectTalker_, [this](auto parsed) { this->selectTalker(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false, true); readAttr(node, d, CArg::InvertResult, invertResult_, [this](auto parsed) { this->invertResult(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false, true); if (d.use(CArg::MatchingCondition) && node.hasAttr(attrName(d, CArg::MatchingCondition))) matchingCondition_ = enumParse<types::MatchingCondition>(node.attr(attrName(d, CArg::MatchingCondition), true), matchingCondition_, types::toMatchingCondition); if (d.use(CArg::BackpackEnabled) && node.hasAttr(attrName(d, CArg::BackpackEnabled))) backpackEnabled_ = enumParse<types::EnvironmentStatus>(node.attr(attrName(d, CArg::BackpackEnabled), true), backpackEnabled_, types::toEnvironmentStatus); if (d.use(CArg::GameOverEnabled) && node.hasAttr(attrName(d, CArg::GameOverEnabled))) gameOverEnabled_ = enumParse<types::EnvironmentStatus>(node.attr(attrName(d, CArg::GameOverEnabled), true), gameOverEnabled_, types::toEnvironmentStatus); if (d.use(CArg::RunAwayEnabled) && node.hasAttr(attrName(d, CArg::RunAwayEnabled))) runAwayEnabled_ = enumParse<types::EnvironmentStatus>(node.attr(attrName(d, CArg::RunAwayEnabled), true), runAwayEnabled_, types::toEnvironmentStatus);
    readAttr(node, d, CArg::ConsumeCard, consumeCard_, [this](auto parsed) { this->consumeCard(std::move(parsed)); }, [](StringView v) { return parseBool(v, true); }, true, true); readAttr(node, d, CArg::OverrideCardSpeed, overrideCardSpeed_, [this](auto parsed) { this->overrideCardSpeed(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false, true); readAttr(node, d, CArg::ExpandSPChars, expandSPChars_, [this](auto parsed) { this->expandSPChars(std::move(parsed)); }, [](StringView v) { return parseBool(v); }, false, true); if (d.use(CArg::TalkerNC) && node.hasAttr(attrName(d, CArg::TalkerNC))) talkerNC(talkerParse(node.attr(attrName(d, CArg::TalkerNC), true)));
    if (d.use(CArg::MatchingType) && node.hasAttr(attrName(d, CArg::MatchingType))) matchingType_ = enumParse<types::MatchingType>(node.attr(attrName(d, CArg::MatchingType), true), matchingType_, types::toMatchingType);
    if (d.use(CArg::XmlType)) {
        if (node.hasAttr("xmltype")) xmlType_ = node.attr("xmltype", true);
        else if (node.hasAttr(attrName(d, CArg::XmlType))) xmlType_ = node.attr(attrName(d, CArg::XmlType), true);
    }
    readNumeric(node, d, CArg::XmlAssignment, xmlAssignment_, [this](auto parsed) { this->xmlAssignment(std::move(parsed)); });

    if (d.use(CArg::CouponNames)) {
        coupon_names_.clear();
        const auto legacy_coupon = node.attr("coupon", false);
        if (!legacy_coupon.empty()) coupon_names_.push_back(legacy_coupon);
    }
    for (const auto& child : node.element_children()) {
        if (child.name() == "Motions" && d.use(CArg::Motions)) {
            // Dに合わせ、いずれかのMotionの解析に失敗したら一覧全体を拒否する。
            lib::Array<lib::Owner<motion::Motion>> parsedMotions;
            system::System fallbackSystem;
            system::XMLInfo fallbackVersion(fallbackSystem, "");
            const auto& parseVersion = version ? *version : fallbackVersion;
            for (const auto& motionNode : child.element_children()) if (motionNode.name() == motion::Motion::XML_NAME) {
                auto mutableNode = motionNode;
                if (auto item = motion::Motion::createFromNode(mutableNode, parseVersion)) parsedMotions.push_back(std::move(item));
            }
            detachMotionPayloads();
            motions_ = lib::SharedSlice<lib::Owner<motion::Motion>>(std::move(parsedMotions));
            bindMotionPayloads();
        } else if (child.name() == "Text" && d.use(CArg::Text)) {
            auto message = utils::decodeLf2(child.value());
            if (singleLine_ && !message.empty()) {
                const auto newline = message.find('\n');
                if (newline != String::npos) message.erase(newline);
            }
            text(message);
        } else if (child.name() == "Str" && d.use(CArg::XmlStr)) {
            xmlStr_ = utils::decodeLf2(child.value());
            // Dは従来形式の内側の名前が空の場合、外側の名前を保持する。
            const auto innerName = child.attr("name", false);
            if (!innerName.empty()) assignName(innerName);
        }
        else if (child.name() == "Expression" && d.use(CArg::Expression)) expression(child.value());
        else if (child.name() == "KeyCodes" && d.use(CArg::KeyCodes)) key_codes_ = utils::decodeLf(child.value(), true);
        else if (child.name() == "Dialogs" && d.use(CArg::Dialogs)) {
            dialogs_.clear();
            for (const auto& dialog : child.element_children()) if (dialog.name() == SDialog::XML_NAME) dialogs_.push_back(SDialog::createFromNode(dialog));
            // Dが正規化するのは、明示的に存在する空のDialogs集合だけ。
            if (dialogs_.empty()) dialogs_.push_back(std::make_shared<SDialog>());
            for (auto& dialog : dialogs_) if (dialog) {
            dialog->parent(shared_from_this());
            dialog->setUseCounter(use_counter_, uc_owner_);
        }
        } else if (child.name() == "Coupons") {
            lib::Array<String> values;
            lib::Array<lib::Owner<coupon::Coupon>> records;
            std::unordered_map<String, bool> names;
            system::System fallbackSystem;
            system::XMLInfo fallbackVersion(fallbackSystem, "");
            const auto& parseVersion = version ? *version : fallbackVersion;
            for (const auto& couponNode : child.element_children()) if (couponNode.name() == "Coupon") {
                const auto name = couponNode.value().empty() ? couponNode.attr("name", false) : couponNode.value();
                values.push_back(name);
                if (d.use(CArg::Coupons) && !names.contains(name)) {
                    names.emplace(name, true);
                    records.push_back(coupon::Coupon::fromNode(couponNode, parseVersion));
                }
            }
            if (d.use(CArg::CouponNames)) {
                for (const auto& name : values)
                    if (!name.empty() && std::find(coupon_names_.begin(), coupon_names_.end(), name) == coupon_names_.end())
                        coupon_names_.push_back(name);
            }
            if (d.use(CArg::Coupons)) coupons(std::move(records));
        } else if (child.name() == "CastRanges" && d.use(CArg::CastRange)) {
            cast_range_.clear();
            for (const auto& range : child.element_children()) if (range.name() == "CastRange") {
                try { cast_range_.push_back(types::toCastRange(range.value())); }
                catch (...) { cast_range_.push_back(enumParse<types::CastRange>(range.value(), types::CastRange::Party)); }
            }
        } else if (child.name() == "BgImages" && d.use(CArg::BgImages)) {
            backs_.clear();
            backgrounds_ = {};
            auto mutableNode = child;
            try {
                // ChangeBgImageは先頭の空の632x420のImageCell
                // を継承の印として使う。Dは読み込み時にこれを除く。
                // 残すと保存・再読み込みのたびに空セルが増える。パス
                // だけにするとText/Color/PCセルの設定を
                // 失うため、型付きのセルオブジェクト全体も保持する。
                auto images = version
                    ? background::BgImage::bgImagesFromNode(
                          mutableNode, type_ == CType::ChangeBgImage, *version)
                    : background::BgImage::bgImagesFromNode(
                          mutableNode, type_ == CType::ChangeBgImage);
                backgrounds_ = lib::SharedSlice<background::BgImagePtr>(std::move(images));
                for (const auto& cell : backgrounds_) {
                    if (const auto image = std::dynamic_pointer_cast<background::ImageCell>(cell))
                        backs_.push_back(image->path());
                }
            } catch (...) {
                background::BgImageArray images;
                images.reserve(child.element_children().size());
                for (const auto& image : child.element_children()) {
                    backs_.push_back(image.value());
                    auto cell = std::make_shared<background::ImageCell>();
                    cell->path(image.value());
                    images.push_back(std::move(cell));
                }
                backgrounds_ = lib::SharedSlice<background::BgImagePtr>(std::move(images));
            }
        }
    }
    if (d.use(CArg::TalkerC)) {
        card_paths_.clear();
        card_images_.clear();
        // DのCardImage.setOnTagは、空の従来形式のパスを
        // 無視する。ここで保持すると空のFileの仮画像ができ、MessageDialog
        // の既定画像状態がXMLモデルと異なるものになる。
        if (node.hasAttr("path")) {
            const auto path = node.attr("path", true);
            if (!path.empty()) card_paths_.push_back(path);
        }
        auto mutableNode = node;
        try {
            auto images = card::CardImage::createFromNode(mutableNode);
            // Dは従来のTalk/Messageのパス属性をsetOnTag(...,
            // true)で読む。ファイルではなく実行時の話者参照を含む
            // 場合がある。新形式の子画像表現があれば、それを保持する。
            if (images.empty() && node.hasAttr("path"))
                card::CardImage::setOnTag(mutableNode, images, true);
            if (!images.empty()) {
                card_images_ = images;
                card_paths_.clear();
                for (const auto& image : images) if (image) {
                    if (image->type == types::CardImageType::File) card_paths_.push_back(image->path());
                    else if (image->type == types::CardImageType::PCNumber) card_paths_.push_back("PC#" + std::to_string(image->pcNumber()));
                    else card_paths_.push_back("Material/??" + types::fromTalker(image->talker()));
                }
            }
        } catch (...) {}
        if (card_images_.empty())
            for (const auto& path : card_paths_)
                card_images_.push_back(std::make_shared<card::CardImage>(path));
    }
    if (d.use(CArg::SelectionMethod)) {
        if (node.hasAttr("method")) selectionMethod_ = types::toSelectionMethod(node.attr("method", true));
        else if (node.hasAttr("random")) selectionMethod_ = parseBool(node.attr("random", true)) ? types::SelectionMethod::Random : types::SelectionMethod::Manual;
    }
    // Wsn.2以降はBranchKeyCodeの検索対象を個別に保存する。
    // Dの読み込みは、従来のeffectCardTypeによる代替より先にこ
    // の四属性を処理する。省くとXMLに明示的なフラグがあっても、新形式のフ
    // ァイルが再読み込み時にSkill+Item+Beastへ戻ってしまう。
    readAttr(node, d, CArg::TargetIsSkill, targetIsSkill_, [this](auto parsed) { this->targetIsSkill(std::move(parsed)); },
             [](StringView v) { return parseBool(v); }, true, true);
    readAttr(node, d, CArg::TargetIsItem, targetIsItem_, [this](auto parsed) { this->targetIsItem(std::move(parsed)); },
             [](StringView v) { return parseBool(v); }, true, true);
    readAttr(node, d, CArg::TargetIsBeast, targetIsBeast_, [this](auto parsed) { this->targetIsBeast(std::move(parsed)); },
             [](StringView v) { return parseBool(v); }, true, true);
    readAttr(node, d, CArg::TargetIsHand, targetIsHand_, [this](auto parsed) { this->targetIsHand(std::move(parsed)); },
             [](StringView v) { return parseBool(v); }, false, true);
    const bool hasExplicitKeyCodeTarget =
        node.hasAttr(attrName(d, CArg::TargetIsSkill)) ||
        node.hasAttr(attrName(d, CArg::TargetIsItem)) ||
        node.hasAttr(attrName(d, CArg::TargetIsBeast)) ||
        node.hasAttr(attrName(d, CArg::TargetIsHand));
    if (type_ == CType::BranchKeyCode && !hasExplicitKeyCodeTarget &&
        node.hasAttr("effectCardType")) {
        const auto cardType = node.attr("effectCardType", true);
        targetIsSkill_ = targetIsItem_ = targetIsBeast_ = targetIsHand_ = false;
        if (cardType == "All") targetIsSkill_ = targetIsItem_ = targetIsBeast_ = true;
        else if (cardType == "Skill") targetIsSkill_ = true;
        else if (cardType == "Item") targetIsItem_ = true;
        else if (cardType == "Beast") targetIsBeast_ = true;
        else if (cardType == "Hand") targetIsHand_ = true;
    }
    validate();
}

ContentPtr Content::createFromNode(const XNode& source, const system::XMLInfo* version, bool nextContent) {
    if (source.name() == "ContentsLine") {
        ContentPtr first;
        ContentPtr current;
        for (const auto& child : source.element_children()) {
            auto parsed = createFromNode(child, version, nextContent);
            if (!parsed) continue;
            if (!first) first = parsed;
            if (current) current->add(parsed);
            current = parsed;
            if (!nextContent) break;
        }
        return first;
    }
    CType ctype;
    try { ctype = cTypeFrom(source.name(), source.attr("type", false, "")); }
    catch (...) { return {}; }
    auto result = std::make_shared<Content>(ctype, source.attr("name", false, ""));
    result->parseAttributes(source, version);
    result->bindBackgroundImages();
    if (!nextContent) return result;
    const auto d = contentDetail(ctype);
    for (const auto& child : source.element_children()) {
        if (child.name() == "Contents") {
            if (!d.owner) continue;
            for (const auto& nested : child.element_children()) {
                if (auto parsed = createFromNode(nested, version, true)) result->add(parsed);
            }
            continue;
        }
        try {
            const auto nestedType = cTypeFrom(child.name(), child.attr("type", false, ""));
            if (d.owner) if (auto parsed = createFromNode(child, version, true)) result->add(parsed);
            (void)nestedType;
        } catch (...) {
            // Text、Dialogsなどのデータを持つ子は、parseAttributesで処理する。
        }
    }
    return result;
}
ContentPtr Content::createFromXML(StringView text, const system::XMLInfo* version, String& id) {
    // Dは出力変数とは独立した文字列値を受け取る。両方が同じ領域を指す
    // 場合も、idを消去する前にネイティブビューのバイト列を保持する。
    String input;
    try { input.assign(text); }
    catch (...) { id.clear(); throw; }
    id.clear();
    auto node = XNode::parse(input);
    id = node.attr("contentId", false, "");
    return createFromNode(node, version);
}
void Content::createContentsFromNode(const XNode& source, const system::XMLInfo* version,
                                     const std::function<void(ContentPtr)>& appender) {
    for (const auto& child : source.element_children()) if (auto value = createFromNode(child, version)) appender(value);
}

EventTree::~EventTree() {
    if (tree_lifetime_)
        tree_lifetime_->alive.store(false, std::memory_order_release);
    const usecounter::detail::NativeCounterRetirement native_retirement;
    detachUseCounter(true);
    retireKeyCodeUsers();
    for (const auto& observed : all_key_code_users_) {
        if (const auto user = observed.lock()) {
            user->removeUseCounter();
            user->owner(static_cast<CWXPath*>(nullptr));
        }
    }
}
void EventTree::retireKeyCodeUsers() noexcept {
    for (auto& user : key_code_users_) if (user) user->removeUseCounter();
    key_code_users_.clear();
    std::erase_if(all_key_code_users_, [](const auto& user) { return user.expired(); });
}
lib::Owner<usecounter::KeyCodeUser> EventTree::createKeyCodeUser() {
    auto user = std::make_shared<ContentStringUser<usecounter::KeyCodeUser>>(this);
    all_key_code_users_.emplace_back(user);
    const std::weak_ptr<usecounter::KeyCodeUser> identity = user;
    // EventTreeはスタック上にも構築できる。デストラクターは、以前に
    // 削除された利用者も含め、まだ生存する全利用者の借用所有元を無効にする。
    user->notify = [this, identity] {
        const auto slot = identity.lock();
        if (slot && slot->owner().get() == this) {
            const auto keepAlive = weak_from_this().lock();
            changed();
        }
    };
    user->publish = [this, identity](const String& value) {
        const auto slot = identity.lock();
        if (!slot || slot->owner().get() != this) return;
        const auto found = std::find(key_code_users_.begin(), key_code_users_.end(), slot);
        if (found != key_code_users_.end())
            key_codes_[static_cast<std::size_t>(found - key_code_users_.begin())].keyCode = value;
    };
    return user;
}
void EventTree::bindKeyCodeUsers() {
    if (key_code_users_.size() != key_codes_.size()) {
        const auto values = key_codes_;
        retireKeyCodeUsers();
        key_codes_.assign(values.size(), FKeyCode{});
        for (std::size_t i = 0; i < values.size(); ++i) {
            auto user = createKeyCodeUser();
            key_code_users_.push_back(user);
            key_codes_[i].kind = values[i].kind;
            user->keyCode(values[i].keyCode);
        }
    }
    for (auto& user : key_code_users_) {
        if (use_counter_) user->setUseCounter(use_counter_, uc_owner_);
        else user->removeUseCounter();
    }
}
EventTree::EventTree() {
    start_use_counter_ = std::make_shared<UseCounter>();
    static std::atomic<std::uint64_t> nextId{0};
    const auto identity = utils::objectIDValue(this);
    // Dはコンパイラー式の型ラベル、objectIDValue、コンスト
    // ラクター内のカウンターを組み合わせる。コンパイラーの型表記と単調時
    // 計・アドレス値は言語固有なので、ネイティブのクラス表記を維持する。
    object_id_ = "EventTree-" + identity + "-" +
        std::to_string(nextId.fetch_add(1, std::memory_order_relaxed));
}
EventTree::EventTree(String name) : EventTree() { add(std::make_shared<Content>(CType::Start, std::move(name))); }
EventTree::EventTree(const ContentPtr& start) : EventTree() {
    // Dの単一Startのコンストラクターは、接続済みのルートを複製する。保護
    // されたリンクを直接調べ、スタック所有の旧ツリーも接続済みとして認識する。
    if (start && start->type() == CType::Start) {
        const auto oldTree = start->resolveTree();
        add(oldTree ? start->dup() : start);
    }
}
EventTree::EventTree(lib::SharedSlice<ContentPtr> starts) : EventTree() {
    for (const auto& value : starts)
        if (checkedContent(value)->type() != CType::Start)
            throw EventException("EventTree requires Start content");
    starts_ = lib::SharedArray<ContentPtr>(std::move(starts));
    for (const auto& value : starts_) {
        // 公開された共有所有元の取得関数を必須とせず、以前の所有元を解決する
        // 。保護された借用でも、この移動に必要な有効な名前一覧を参照できる。
        const auto old = value->resolveTree();
        if (old) old->start_names_.erase(value->name());
        bindContentTree(value);
        const auto counter = start_use_counter_;
        value->setStartUseCounter(counter);
    }
    for (const auto& value : starts_) start_names_[value->name()] = value;
    // ネイティブvectorは、所有権を持つ取得時の状態として保持する。構築中のリンクは、Content::resolveTree
    // が共有所有権へ昇格できるまで保護された借用を使う。
}
EventTree::EventTree(lib::Array<ContentPtr> starts)
    : EventTree(lib::SharedSlice<ContentPtr>(std::move(starts))) {}
void EventTree::bindContentTree(const ContentPtr& value) noexcept {
    if (!value) return;
    if (auto self = weak_from_this().lock()) value->tree_.setShared(self);
    else value->tree_.setBorrowed(this, tree_lifetime_);
}
void EventTree::owner(EventTreeOwner* value) noexcept {
    owner(value, value ? value->eventTreeLifetimeToken()
                       : std::weak_ptr<path::OwnerLifetimeToken>{});
}
void EventTree::owner(
    EventTreeOwner* value,
    std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept {
    if (!value) {
        owner_.setRaw(nullptr);
    } else {
        // DのGCは所有元とツリーの循環を回収できるが、shared_ptrはでき
        // ない。この逆参照は弱参照とし、借用した所有元は寿命トークンで保護する。
        auto retained = std::dynamic_pointer_cast<EventTreeOwner>(
            value->retainForCallback());
        if (retained) owner_.setSharedBorrowed(retained, value, std::move(lifetime));
        else owner_.setBorrowed(value, std::move(lifetime));
    }
    // Dのownerプロパティは、所有するEventTreeOwnerへのリンクだけを変える。Content
    // のツリー所属は構築時やprepareStartで接続済みで、自身で解決する。
}
EventTreeOwner* EventTree::owner() const noexcept {
    const auto owner = owner_.lock();
    return owner.get();
}
bool EventTree::ownedBy(
    EventTreeOwner* owner,
    std::weak_ptr<path::OwnerLifetimeToken> lifetime) const noexcept {
    return owner_.matchesBorrowed(owner, lifetime);
}
void EventTree::validateStarts() const {
    // Dのstarts取得関数は、呼び出し側が巡回を始める前に、全要素
    // へ出力契約の検証を行う。ネイティブコードもこの検証順を維持する。
    for (const auto& start : starts_)
        if (checkedContent(start)->type() != CType::Start)
            throw EventException("EventTree requires Start content");
}
lib::SharedSlice<const ContentPtr> EventTree::starts() const {
    validateStarts();
    return starts_;
}
lib::SharedSlice<ContentPtr> EventTree::starts() {
    validateStarts();
    return starts_;
}
bool EventTree::opEquals(const EventTree& other) const noexcept {
    if (enter_ != other.enter_ || escape_ != other.escape_ || lose_ != other.lose_ ||
        every_round_ != other.every_round_ || round_end_ != other.round_end_ ||
        round0_ != other.round0_ || rounds_ != other.rounds_ ||
        key_codes_.size() != other.key_codes_.size() ||
        key_code_matching_type_ != other.key_code_matching_type_ || comment_ != other.comment_ ||
        starts_.size() != other.starts_.size()) return false;
    for (std::size_t i = 0; i < key_codes_.size(); ++i) {
        if (key_codes_[i].keyCode != other.key_codes_[i].keyCode ||
            key_codes_[i].kind != other.key_codes_[i].kind) return false;
    }
    for (std::size_t i = 0; i < starts_.size(); ++i) {
        if (static_cast<bool>(starts_[i]) != static_cast<bool>(other.starts_[i])) return false;
        if (starts_[i] && !starts_[i]->opEquals(*other.starts_[i])) return false;
    }
    return true;
}
void EventTree::changed() { if (change_) change_(); }
void EventTree::prepareStart(ContentPtr value) {
    if (const auto counter = use_counter_) {
        const auto owner = uc_owner_;
        value->setUseCounter(counter, owner);
    }
    const auto old = value->resolveTree();
    if (old) old->start_names_.erase(value->name());
    const auto counter = start_use_counter_;
    value->setStartUseCounter(counter);
    bindContentTree(value);
    value->changeHandler(changeHandler());
}

void EventTree::add(const ContentPtr& value) {
    // Dはクラス引数が可変のStart項目と同じ対象を指す場合も、その引数を保持する。
    const auto start = checkedContent(value);
    if (start->type() != CType::Start) throw EventException("EventTree requires Start content");
    const auto receiver_alive = weak_from_this().lock();
    prepareStart(start);
    starts_.push_back(start);
    start_names_[start->name()] = start;
    changed();
}
void EventTree::insert(std::size_t index, const ContentPtr& value) {
    const auto start = checkedContent(value);
    if (start->type() != CType::Start) throw EventException("EventTree requires Start content");
    const auto receiver_alive = weak_from_this().lock();
    prepareStart(start);
    // Dは接続後に挿入スライスを検査し、失敗時もその順序を維持する。
    // 準備中のコールバックがツリーを変えた場合は、現在の長さを使う。
    if (index > starts_.size()) throw EventException("EventTree index out of range");
    starts_.insert(starts_.begin() + static_cast<std::ptrdiff_t>(index), start);
    start_names_[start->name()] = start;
    changed();
}
void EventTree::removeStartBindings(ContentPtr value) {
    // Dのクラス引数は保持された参照値であり、可変の配列項目への参照
    // ではない。カウンターや変更のコールバックをまたいで生存させる。
    if (!value) throw EventException("Null Start content");
    if (use_counter_) value->removeUseCounter();
    value->removeStartUseCounter();
    value->tree_.setRaw(nullptr);
    value->changeHandler({});
    start_names_.erase(value->name());
    changed();
}
void EventTree::remove(std::size_t index) {
    if (starts_.size() <= 1) throw EventException("EventTree requires another Start");
    removeStartBindings(starts_.at(index));
    // DはremoveProcの通知後に新しいスライスを作る。コールバックが
    // 配列を置換した場合は、無効なメモリを削除せず、現在の配列を再検査する。
    if (index >= starts_.size()) throw std::out_of_range("EventTree removal index");
    starts_.erase(starts_.begin() + static_cast<std::ptrdiff_t>(index));
}
void EventTree::remove(const ContentPtr& value) {
    const auto start = value;
    if (!start || start->type() != CType::Start) throw EventException("EventTree requires Start content");
    const auto it = std::find(starts_.begin(), starts_.end(), start);
    if (it != starts_.end()) return remove(static_cast<std::size_t>(std::distance(starts_.begin(), it)));
    // DはStartが存在しなければアサートする。存在しないことを、何もしない成功として扱わない。
    throw EventException("Start content is not registered");
}
void EventTree::starts(lib::SharedSlice<ContentPtr> value) {
    changed();
    // Dのforeachは通知が現在の配列を置換しても入力スライ
    // スを保持する。ネイティブでも巡回中の要素の寿命を維持する。
    const lib::SharedSlice<ContentPtr> retained = starts_;
    for (const auto& start : retained) removeStartBindings(start);
    starts_.clear();
    for (const auto& start : value) add(start);
}
void EventTree::starts(lib::Array<ContentPtr> value) {
    starts(lib::SharedSlice<ContentPtr>(std::move(value)));
}
void EventTree::swapStart(std::size_t first, std::size_t second) {
    if (first >= starts_.size() || second >= starts_.size()) throw EventException("EventTree index out of range");
    if (first == second) return;
    changed();
    std::swap(starts_[first], starts_[second]);
}
lib::Array<String> EventTree::startNames() const {
    lib::Array<String> result;
    // DのAA.keysの順序は未規定。std::mapでは、同
    // じ一意な名前一覧の所属を維持しつつ、一定の順序を提供する。
    for (const auto& [name, value] : start_names_) result.push_back(name);
    return result;
}
bool EventTree::hasStart(StringView nameValue) const { return start_names_.contains(nameValue); }
ContentPtr EventTree::start(StringView nameValue) const {
    const auto found = start_names_.find(nameValue);
    return found == start_names_.end() ? ContentPtr{} : found->second;
}
ContentPtr EventTree::fromPath(const lib::Array<std::size_t>& pathValue) const {
    if (pathValue.empty()) return {};
    const auto start = starts_.at(pathValue.front());
    if (pathValue.size() == 1) return start;
    // Dは残りのパスをContentへ委ねる。不正なインデックスや
    // 途中のnullを、正常なパスなしの結果として握りつぶさない。
    if (!start) throw EventException("Null start content in path");
    return start->fromPath(lib::Array<std::size_t>(pathValue.begin() + 1, pathValue.end()));
}
lib::Array<std::size_t> EventTree::areaPath() const {
    auto owner = owner_.lock();
    if (!owner) return {};
    auto result = owner->areaPath();
    // DはareaPathの後に所有元を読み直し、検索結果が-1でもsize_t
    // へ変換して追加する。短いパスや空の結果へ置き換えない。
    auto* liveOwner = owner.get();
    if (!liveOwner) throw EventException("Event tree lost its owner during path lookup");
    const auto ownedTrees = liveOwner->trees();
    const auto index = utils::cCountUntil(ownedTrees, this,
        [](const auto& value, const auto* self) { return value.get() == self; });
    result.push_back(static_cast<std::size_t>(index));
    return result;
}
void EventTree::enter(bool value) noexcept { if (enter_ != value) changed(); enter_ = value; }
void EventTree::escape(bool value) noexcept { if (escape_ != value) changed(); escape_ = value; }
void EventTree::lose(bool value) noexcept { if (lose_ != value) changed(); lose_ = value; }
void EventTree::everyRound(bool value) noexcept { if (every_round_ != value) changed(); every_round_ = value; }
void EventTree::roundEnd(bool value) noexcept { if (round_end_ != value) changed(); round_end_ = value; }
void EventTree::round0(bool value) noexcept { if (round0_ != value) changed(); round0_ = value; }
void EventTree::rounds(lib::SharedSlice<std::uint32_t> value) {
    if (rounds_.size() == value.size() &&
        std::equal(rounds_.begin(), rounds_.end(), value.begin())) return;
    changed();
    rounds_ = lib::SharedArray<std::uint32_t>(value);
}
void EventTree::rounds(lib::Array<std::uint32_t> value) {
    rounds(lib::SharedSlice<std::uint32_t>(std::move(value)));
}
bool EventTree::addRound(std::uint32_t value) {
    if (fireRound(value)) return false;
    changed();
    rounds_.push_back(value);
    return true;
}
lib::Array<std::uint32_t> EventTree::addRounds(
    const lib::Array<std::uint32_t>& values) {
    std::set<std::uint32_t> rounds(rounds_.begin(), rounds_.end());
    lib::Array<std::uint32_t> added;
    for (const auto value : values) {
        if (rounds.insert(value).second) added.push_back(value);
    }
    if (added.empty()) return added;
    changed();
    rounds_ = lib::SharedArray<std::uint32_t>(
        lib::Array<std::uint32_t>(rounds.begin(), rounds.end()));
    return added;
}
lib::Array<std::uint32_t> EventTree::roundsFromNode(
    const EventTreeOwner& owner, const XNode& node,
    const system::XMLInfo* version) {
    if (!owner.canHasFireRound()) return {};
    try {
        if (!node.valid() || node.name() != ROUNDS_XML_NAME) return {};
        auto added = ::cwxeditor::cwx::event::roundsFromNode(node, version);
        added = addRounds(added);
        if (!added.empty()) {
            sortRounds();
            return added;
        }
    } catch (const std::exception& error) {
        utils::fdebugln(error.what());
    }
    return {};
}
void EventTree::sortRounds() {
    if (utils::isSorted(rounds_)) return;
    changed();
    std::sort(rounds_.begin(), rounds_.end());
}
bool EventTree::fireRound(std::uint32_t value) const { return std::find(rounds_.begin(), rounds_.end(), value) != rounds_.end(); }
void EventTree::removeRound(std::uint32_t value) { const auto it = std::find(rounds_.begin(), rounds_.end(), value); if (it == rounds_.end()) throw EventException("EventTree round is not present"); changed(); rounds_.erase(it); }
void EventTree::removeRoundsAll() noexcept { changed(); rounds_.clear(); }
void EventTree::keyCodes(lib::Array<FKeyCode> value) {
    if (key_codes_.size() == value.size() && std::equal(key_codes_.begin(), key_codes_.end(), value.begin(),
        [](const auto& a, const auto& b) { return a.keyCode == b.keyCode && a.kind == b.kind; })) return;
    changed();
    retireKeyCodeUsers();
    // Dは先にサイズを変えてからUserを一つずつ置き換える。各新
    // Userの書き込み前通知中も、後続の項目は旧値を公開する。
    key_codes_.resize(value.size());
    key_code_users_.resize(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto user = createKeyCodeUser();
        key_code_users_[i] = user;
        key_codes_[i] = FKeyCode{String{}, value[i].kind};
        user->keyCode(value[i].keyCode);
        if (use_counter_) user->setUseCounter(use_counter_, uc_owner_);
    }
}
bool EventTree::addKeyCode(FKeyCode value, std::ptrdiff_t index) {
    if (fireKeyCode(value)) return false;
    bindKeyCodeUsers();
    changed();
    auto user = createKeyCodeUser();
    if (use_counter_) user->setUseCounter(use_counter_, uc_owner_);
    user->keyCode(value.keyCode);
    const auto position = index < 0 || static_cast<std::size_t>(index) >= key_codes_.size()
        ? key_codes_.size() : static_cast<std::size_t>(index);
    key_codes_.insert(key_codes_.begin() + static_cast<std::ptrdiff_t>(position), std::move(value));
    key_code_users_.insert(key_code_users_.begin() + static_cast<std::ptrdiff_t>(position), std::move(user));
    return true;
}
lib::Array<FKeyCode> EventTree::addKeyCodes(const lib::Array<FKeyCode>& values, std::ptrdiff_t index) {
    lib::Array<FKeyCode> added;
    for (const auto& value : values)
        if (!value.keyCode.empty() && !fireKeyCode(value)) added.push_back(value);
    if (added.empty()) return added;
    bindKeyCodeUsers();
    changed();
    lib::Array<lib::Owner<usecounter::KeyCodeUser>> users;
    for (const auto& value : added) {
        auto user = createKeyCodeUser();
        if (use_counter_) user->setUseCounter(use_counter_, uc_owner_);
        user->keyCode(value.keyCode);
        users.push_back(std::move(user));
    }
    const auto position = index < 0 || static_cast<std::size_t>(index) >= key_codes_.size()
        ? key_codes_.size() : static_cast<std::size_t>(index);
    key_codes_.insert(key_codes_.begin() + static_cast<std::ptrdiff_t>(position), added.begin(), added.end());
    key_code_users_.insert(key_code_users_.begin() + static_cast<std::ptrdiff_t>(position), users.begin(), users.end());
    return added;
}
lib::Array<FKeyCode> EventTree::keyCodesFromNode(const EventTreeOwner& owner,
    XNode& node, const system::XMLInfo& version, const system::System& sys,
    std::ptrdiff_t index) {
    if (!owner.canHasFireKeyCode()) return {};
    try {
        if (node.name() != card::KEY_CODES_XML_NAME) return {};
        auto keys = card::keyCodesFromNode(node, version);
        lib::Array<FKeyCode> values;
        for (auto key : keys) {
            const auto kind = sys.fireKeyCodeKindRef(key);
            values.push_back(FKeyCode{std::move(key), kind});
        }
        return addKeyCodes(values, index);
    } catch (const std::exception& error) {
        utils::fdebugln(error.what());
        return {};
    }
}
void EventTree::setKeyCode(std::size_t index, FKeyCode value) {
    if (index >= key_codes_.size()) throw EventException("EventTree key-code index out of range");
    if (key_codes_[index].keyCode == value.keyCode && key_codes_[index].kind == value.kind) return;
    bindKeyCodeUsers();
    changed();
    key_code_users_[index]->keyCode(value.keyCode);
    key_codes_[index].kind = value.kind;
}
bool EventTree::fireKeyCode(const FKeyCode& value) const { return std::any_of(key_codes_.begin(), key_codes_.end(), [&](const auto& item) { return item.keyCode == value.keyCode && item.kind == value.kind; }); }
void EventTree::removeKeyCode(const FKeyCode& value) {
    const auto found = std::find_if(key_codes_.begin(), key_codes_.end(), [&](const auto& item) { return item.keyCode == value.keyCode && item.kind == value.kind; });
    if (found == key_codes_.end()) throw EventException("EventTree key-code is not present");
    const auto index = static_cast<std::size_t>(found - key_codes_.begin());
    const auto user = key_code_users_.at(index);
    changed();
    // Dのforeachはchangedの前に一致したレコードをコピーする。コ
    // ールバックが項目を移動しても、元の利用者のカウンターを外す必要がある。
    user->removeUseCounter();
    if (index >= key_codes_.size()) throw std::out_of_range("EventTree key-code removal index");
    key_code_users_.erase(key_code_users_.begin() + static_cast<std::ptrdiff_t>(index));
    key_codes_.erase(key_codes_.begin() + static_cast<std::ptrdiff_t>(index));
    std::erase_if(all_key_code_users_, [](const auto& observed) { return observed.expired(); });
}
void EventTree::removeKeyCodesAll() noexcept { changed(); retireKeyCodeUsers(); key_codes_.clear(); }
void EventTree::swapKeyCode(std::size_t first, std::size_t second) {
    if (first != second) changed();
    const auto temporary = key_codes_.at(first);
    key_codes_.at(first) = key_codes_.at(second);
    key_codes_.at(second) = temporary;
    // Dはレコードの既存の利用者をキーとともに交換する。ネイティブの
    // 並行した保存領域でも、カウンターを再登録せず同じ同一性を移す。
    std::swap(key_code_users_.at(first), key_code_users_.at(second));
}
bool EventTree::isEmpty() const noexcept { return std::all_of(starts_.begin(), starts_.end(), [](const auto& value) { return !value || value->next().empty(); }); }
const String& EventTree::name() const {
    // Dは最初のStartへ直接アクセスする。空またはnullのStartを名前として扱わない。
    const auto start = starts_.at(0);
    if (!start) throw EventException("Null first Start content");
    return start->name();
}
void EventTree::name(String value) {
    if (!starts_.empty() && starts_.front() && starts_.front()->name() == value) return;
    changed();
    if (!starts_.empty() && starts_.front()) start_names_.erase(starts_.front()->name());
    value = utils::createNewName(value, [this](const String& candidate) { return !hasStart(candidate); });
    if (starts_.empty()) add(std::make_shared<Content>(CType::Start, std::move(value)));
    else starts_.front()->setName(std::move(value));
    if (!starts_.empty()) start_names_[starts_.front()->name()] = starts_.front();
}
void EventTree::comment(String value) { if (comment_ == value) return; changed(); comment_ = std::move(value); }
void EventTree::changeHandler(ChangeHandler value) { for (auto& start : starts_) if (start) start->changeHandler(value); change_ = std::move(value); }
String EventTree::cwxPath(bool id) {
    const auto owner = owner_.lock();
    if (!owner) return {};
    const auto ownedTrees = owner->trees();
    const auto index = utils::cCountUntil(ownedTrees, this,
        [](const auto& value, const auto* self) { return value.get() == self; });
    const auto parentPath = owner->cwxPath(id);
    return path::cpjoin(parentPath, "event",
                        static_cast<std::size_t>(index));
}
lib::Owner<CWXPath> EventTree::findCWXPath(StringView value) {
    if (path::cpempty(value)) return std::static_pointer_cast<CWXPath>(shared_from_this());
    if (!path::cpcategory(value).empty()) return {};
    const auto index = path::cpindex(value);
    if (index >= starts_.size() || !starts_[index]) return {};
    return starts_[index]->findCWXPath(path::cpbottom(value));
}
lib::Array<lib::Observer<CWXPath>> EventTree::cwxChilds() { lib::Array<lib::Observer<CWXPath>> result; for (auto& value : starts_) if (value) result.emplace_back(std::static_pointer_cast<CWXPath>(value)); return result; }
lib::Observer<CWXPath> EventTree::cwxParent() {
    const auto owner = owner_.lock();
    if (!owner) return {};
    if (owner.shared()) return std::static_pointer_cast<CWXPath>(owner.shared());
    return owner->retainForCallback();
}
void EventTree::setUseCounter(const lib::Owner<UseCounter>& value, const lib::Owner<CWXPath>& owner) {
    const auto counter = value;
    const auto counterOwner = owner;
    const auto retainedStarts = starts();
    for (const auto& start : retainedStarts) checkedContent(start)->setUseCounter(counter, counterOwner);
    const auto retainedUsers = key_code_users_;
    for (const auto& user : retainedUsers) user->setUseCounter(counter, counterOwner);
    use_counter_ = counter;
    uc_owner_ = usecounter::renameInfo(counterOwner);
}
void EventTree::removeUseCounter() { detachUseCounter(false); }
void EventTree::detachUseCounter(bool finalizing) {
    // Dのforeachはスライスを保持する。ネイティブもコールバックをまた
    // いで子を生存させる。ネイティブのデストラクターは、一部が不正な配列も
    // 、公開取得関数の失敗契約を終了処理中に発動せずに片付ける必要がある。
    const auto retainedStarts = finalizing ? starts_ : starts();
    for (const auto& start : retainedStarts) {
        if (finalizing && !start) continue;
        checkedContent(start)->removeUseCounter();
    }
    const auto retainedUsers = key_code_users_;
    for (const auto& user : retainedUsers) user->removeUseCounter();
    use_counter_.reset();
    uc_owner_.reset();
}
EventTreePtr EventTree::dup() const { auto result = std::make_shared<EventTree>(); result->object_id_ = object_id_; result->enter_ = enter_; result->escape_ = escape_; result->lose_ = lose_; result->every_round_ = every_round_; result->round_end_ = round_end_; result->round0_ = round0_; result->rounds_ = rounds_; result->keyCodes(key_codes_); result->key_code_matching_type_ = key_code_matching_type_; result->comment_ = comment_; for (const auto& start : starts_) if (start) result->add(start->dup()); return result; }
void EventTree::copyIgnitions(const EventTree& other) {
    enter(other.enter_); escape(other.escape_); lose(other.lose_);
    everyRound(other.every_round_); roundEnd(other.round_end_); round0(other.round0_);
    rounds(lib::Array<std::uint32_t>(other.rounds_.begin(), other.rounds_.end()));
    keyCodes(other.key_codes_); keyCodeMatchingType(other.key_code_matching_type_);
}

String EventTree::toXML() const { return toNode(nullptr).text(); }
XNode EventTree::toNode(const card::XMLOption* option) const {
    auto node = XNode::create("Event");
    if (!comment_.empty()) node.newAttr("comment", comment_);
    if (enter_ || escape_ || lose_ || every_round_ || round_end_ || round0_ || !rounds_.empty() || !key_codes_.empty()) {
        auto ignitions = node.newElement("Ignitions");
        if (key_code_matching_type_ != MatchingType::Or) ignitions.newAttr("keyCodeMatchingType", enumText(key_code_matching_type_));
        lib::Array<String> numbers;
        if (enter_) numbers.push_back("1"); if (escape_) numbers.push_back("2"); if (lose_) numbers.push_back("3"); if (every_round_) numbers.push_back("4"); if (round0_) numbers.push_back("5"); if (round_end_) numbers.push_back("6");
        for (auto value : rounds_) numbers.push_back("-" + std::to_string(value));
        ignitions.newElement("Number", utils::encodeLf(numbers, false));
        lib::Array<String> keys;
        for (const auto& value : key_codes_) {
            keys.push_back(option && option->sys
                               ? option->sys->convFireKeyCode(value)
                               : value.keyCode);
        }
        ignitions.newElement("KeyCodes", utils::encodeLf(keys));
    }
    auto contents = node.newElement("Contents");
    for (const auto& start : starts_) if (start) start->toNode(contents, option);
    return node;
}
void EventTree::toNode(XNode& parent, const card::XMLOption* option) const {
    // DのEventTree.toNodeは、常にEventsコンテナーの下へEvent
    // を追加する。Eventsノード自体を再利用するとIgnitionsとContents
    // が平坦化され、次の読み込みでイベントツリー全体が失われる。
    auto event = parent.name() == "Event" ? parent : parent.newElement("Event");
    auto node = toNode(option);
    for (const auto& attr : node.attributes()) event.newAttr(attr.name, attr.value);
    for (const auto& child : node.element_children()) event.append(child.clone());
}
EventTreePtr EventTree::createFromXML(StringView text, const system::XMLInfo* version) {
    try {
        auto node = XNode::parse(text, true);
        return createFromNode(node, version);
    } catch (...) {
        return {};
    }
}
EventTreePtr EventTree::createFromNode(const XNode& source, const system::XMLInfo* version) {
    if (source.name() != "Event") return {};
    auto result = std::make_shared<EventTree>();
    result->comment_ = source.attr("comment", false, "");
    for (const auto& child : source.element_children()) {
        if (child.name() == "Ignitions") {
            if (child.hasAttr("keyCodeMatchingType")) result->key_code_matching_type_ = enumParse<MatchingType>(child.attr("keyCodeMatchingType", true), MatchingType::Or, types::toMatchingType);
            for (const auto& ignition : child.element_children()) {
                if (ignition.name() == "Number") for (const auto& number : utils::decodeLf(ignition.value())) {
                    if (number == "1") result->enter_ = true; else if (number == "2") result->escape_ = true; else if (number == "3") result->lose_ = true; else if (number == "4") result->every_round_ = true; else if (number == "5") result->round0_ = true; else if (number == "6") result->round_end_ = true; else if (number.size() > 1 && number.front() == '-') result->rounds_.push_back(static_cast<std::uint32_t>(integerText<std::int32_t>(StringView(number).substr(1)))); }
                else if (ignition.name() == "KeyCodes") {
                    for (auto key : utils::decodeLf(ignition.value())) {
                        if (key.empty()) continue;
                        auto kind = system::FKCKind::Use;
                        if (version) kind = version->sys.fireKeyCodeKindRef(key);
                        const FKeyCode code{std::move(key), kind};
                        result->addKeyCode(code);
                    }
                }
            }
        } else if (child.name() == "Contents") Content::createContentsFromNode(child, version, [&](ContentPtr value) { result->add(value); });
    }
    result->bindKeyCodeUsers();
    return result->starts_.empty() ? EventTreePtr{} : result;
}

AbstractEventTreeOwner::AbstractEventTreeOwner(CWXPath* connectedPath)
    : connected_path_(connectedPath) {}

AbstractEventTreeOwner::~AbstractEventTreeOwner() {
    const usecounter::detail::NativeCounterRetirement native_retirement;
    event_lifetime_->alive.store(false, std::memory_order_release);
    // DのGCと異なり、C++では別のshared_ptrがツリーを保持していても、こ
    // の所有元が破棄され得る。オブジェクトの寿命が終わる前にコールバックを切り離す。
    auto* linkedOwner = con();
    const auto linkedLifetime = linkedOwner->eventTreeLifetimeToken();
    const auto retained = trees_;
    for (const auto& tree : retained)
        detachEventTree(tree, true, linkedOwner, linkedLifetime);
}

lib::Owner<CWXPath> AbstractEventTreeOwner::retainForCallback() {
    if (connected_path_) {
        if (auto retained = connected_path_->retainForCallback()) return retained;
    }
    return std::static_pointer_cast<CWXPath>(weak_from_this().lock());
}

EventTreeOwner* AbstractEventTreeOwner::con() noexcept {
    // Dの委譲されたCETOは、外側のEffectCardをツリーの所有元として返す
    // 。ネイティブの接続パスは単なるCWXPathアダプターの場合もあるため、EventTreeOwner
    // を持たない追加の呼び出し側でも具体的な所有元を保持する。
    if (auto* owner = dynamic_cast<EventTreeOwner*>(connected_path_)) return owner;
    return this;
}

void EventTreeOwner::prepareEventTree(const EventTreePtr& value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto tree = checkedEventTree(value);
    if (!canHasFireEnter()) tree->enter(false);
    if (!canHasFireLose()) tree->lose(false);
    if (!canHasFireEscape()) tree->escape(false);
    if (!canHasFireEveryRound()) tree->everyRound(false);
    if (!canHasFireRoundEnd()) tree->roundEnd(false);
    if (!canHasFireRound0()) tree->round0(false);
    if (!canHasFireRound()) tree->removeRoundsAll();
    if (!canHasFireKeyCode()) tree->removeKeyCodesAll();
    const auto binding = eventCounterBinding();
    if (binding.counter) tree->setUseCounter(binding.counter, binding.owner);
    auto* linkedOwner = con();
    tree->changeHandler(eventTreeOwnerChangeHandler(linkedOwner));
    tree->owner(linkedOwner, linkedOwner->eventTreeLifetimeToken());
    changed();
}

void EventTreeOwner::detachEventTree(
    const EventTreePtr& tree, bool removeCounter,
    EventTreeOwner* expectedOwner,
    std::weak_ptr<path::OwnerLifetimeToken> expectedLifetime) {
    if (!tree) return;
    if (expectedOwner && !tree->ownedBy(expectedOwner, expectedLifetime)) return;
    if (removeCounter) tree->removeUseCounter();
    // カウンターのコールバックが、保持されたツリーの親を変更した可能性があ
    // る。この所有元が後処理を終える際、新しい所有元のリンクを解除しない。
    if (expectedOwner && !tree->ownedBy(expectedOwner, expectedLifetime)) return;
    tree->changeHandler({});
    tree->owner(nullptr);
}

void EventTreeOwner::add(const EventTreePtr& value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto tree = checkedEventTree(value);
    prepareEventTree(tree);
    auto updated = snapshotEventTrees(trees());
    updated.push_back(tree);
    eventTreesStorage() = lib::SharedSlice<EventTreePtr>(std::move(updated));
}
void EventTreeOwner::insert(std::size_t index, const EventTreePtr& value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto tree = checkedEventTree(value);
    utils::cwriteln("LOG: EventTree.insert(EventTree): index=" + std::to_string(index) +
                   " hasOwner=" + (tree->owner() ? "yes" : "no"));
    if (index == trees().size()) return add(tree);
    prepareEventTree(tree);
    // Dは共通の設定・通知の後に、連結するスライスを検査する。
    auto updated = snapshotEventTrees(trees());
    if (index > updated.size()) throw std::out_of_range("Event tree insertion index");
    updated.insert(updated.begin() + static_cast<std::ptrdiff_t>(index), tree);
    eventTreesStorage() = lib::SharedSlice<EventTreePtr>(std::move(updated));
}
void EventTreeOwner::removeEvent(std::size_t index) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    // Dと同様に、カウンターのコールバック後に添字位置のメンバーを読み直す。
    // 各一時的なネイティブ所有元は、その呼び出しが戻るまで対象を生存させる。
    if (eventCounterBinding().counter) checkedEventTree(trees().at(index))->removeUseCounter();
    const auto tree = checkedEventTree(trees().at(index));
    // Dはコールバックが逆参照を変更しても、現在の添字位置のツリーを削
    // 除する。期待する所有元との一致検査は、所有元の破棄時だけに使う。
    detachEventTree(tree, false);
    auto updated = snapshotEventTrees(trees());
    updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(index));
    eventTreesStorage() = lib::SharedSlice<EventTreePtr>(std::move(updated));
    changed();
}
void EventTreeOwner::remove(const EventTreePtr& value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto current = trees();
    const auto it = std::find(current.begin(), current.end(), value);
    if (it != current.end()) return removeEvent(static_cast<std::size_t>(std::distance(current.begin(), it)));
    // Dは未登録メンバーでassert(0)に到達する。黙って成功しない。
    throw EventException("Event tree is not registered");
}
void EventTreeOwner::swapEventTree(std::size_t first, std::size_t second) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (first != second) changed();
    auto& current = eventTreesStorage();
    std::swap(current.at(first), current.at(second));
}
void EventTreeOwner::clearEvents() {
    if (trees().empty()) return;
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    changed();
    // Dのforeachは、コールバックが_evtsを置換しても入力スライスを保持する。
    const auto retained = trees();
    for (const auto& tree : retained)
        detachEventTree(checkedEventTree(tree),
                        static_cast<bool>(eventCounterBinding().counter));
    eventTreesStorage() = {};
}

void EventTreeOwner::addAll(const lib::Array<EventTreePtr>& values) {
    // ネイティブのDTO入力は、コールバック実行前に一つの所有する記述子へ変換する。
    addAll(lib::SharedSlice<EventTreePtr>(values));
}

void EventTreeOwner::addAll(lib::SharedSlice<EventTreePtr> values) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    // Dは配列記述子を値で渡す。add()が所有元の記述
    // 子を置換しても、入力元は元の項目に接続したまま。
    for (const auto& value : values) add(value);
}

void AbstractEventTreeOwner::add(const EventTreePtr& value) { EventTreeOwner::add(value); }
void AbstractEventTreeOwner::insert(std::size_t index, const EventTreePtr& value) { EventTreeOwner::insert(index, value); }
void AbstractEventTreeOwner::removeEvent(std::size_t index) { EventTreeOwner::removeEvent(index); }
void AbstractEventTreeOwner::remove(const EventTreePtr& value) { EventTreeOwner::remove(value); }
void AbstractEventTreeOwner::swapEventTree(std::size_t first, std::size_t second) { EventTreeOwner::swapEventTree(first, second); }
void AbstractEventTreeOwner::clearEvents() { EventTreeOwner::clearEvents(); }
void AbstractEventTreeOwner::addAll(const lib::Array<EventTreePtr>& values) {
    EventTreeOwner::addAll(values);
}
void AbstractEventTreeOwner::setUseCounter(const lib::Owner<UseCounter>& counter,
                                          const lib::Owner<CWXPath>& owner) {
    const auto retainedCounter = counter;
    const auto retainedOwner = owner;
    const auto retained = trees_;
    for (const auto& tree : retained) checkedEventTree(tree)->setUseCounter(retainedCounter, retainedOwner);
    use_counter_ = retainedCounter;
    uc_owner_ = usecounter::renameInfo(retainedOwner);
}
void AbstractEventTreeOwner::removeUseCounter() {
    const auto retained = trees_;
    for (const auto& tree : retained) checkedEventTree(tree)->removeUseCounter();
    use_counter_.reset();
    uc_owner_.reset();
}
bool AbstractEventTreeOwner::isEmpty() const {
    return std::all_of(trees_.begin(), trees_.end(), [](const auto& value) {
        // Dは全ツリーのisEmptyを呼び、nullを空のツリーとは扱わない
        // 。nullのネイティブポインターを参照せず、ここで例外を送出する。
        if (!value) throw std::invalid_argument("Null event tree");
        return value->isEmpty();
    });
}
String AbstractEventTreeOwner::cwxPath(bool id) {
    return connected_path_ ? connected_path_->cwxPath(id) : String{};
}
lib::Owner<CWXPath> AbstractEventTreeOwner::findCWXPath(StringView value) {
    if (path::cpempty(value)) {
        // 共有された接続済み所有元はパスの同一性そのものであり、実際の制
        // 御ブロックから返せる。スタック所有の場合は保護された代理を公
        // 開する。何もしない解放処理のshared_ptrでは、外へ渡
        // したハンドルがこのオブジェクトの破棄後に無効参照になるため。
        if (connected_path_) {
            if (auto retained = connected_path_->retainForCallback()) return retained;
        }
        if (auto retained = retainForCallback()) return retained;
        return std::make_shared<EventTreeOwnerPathProxy>(this, event_lifetime_);
    }
    if (path::cpcategory(value) == "event") {
        const auto index = path::cpindex(value);
        if (index >= trees_.size()) return {};
        return checkedEventTree(trees_[index])->findCWXPath(path::cpbottom(value));
    }
    return {};
}
lib::Array<lib::Observer<CWXPath>> AbstractEventTreeOwner::cwxChilds() {
    lib::Array<lib::Observer<CWXPath>> result;
    for (const auto& value : trees_) result.emplace_back(std::static_pointer_cast<CWXPath>(value));
    return result;
}
EventTreePtr AbstractEventTreeOwner::eventTree(std::size_t index) const { return index < trees_.size() ? trees_[index] : EventTreePtr{}; }
lib::Array<EventTreePtr> AbstractEventTreeOwner::loadEventsFromNode(
    const XNode& source, String& commentForEvents,
    const system::XMLInfo* version) {
    lib::Array<EventTreePtr> result;
    for (const auto& child : source.element_children()) {
        if (child.name() == "Event") {
            if (auto tree = EventTree::createFromNode(child, version))
                result.push_back(std::move(tree));
        }
    }
    commentForEvents = source.attr("comment", false, "");
    return result;
}
void AbstractEventTreeOwner::appendEventsToNode(XNode& node, const card::XMLOption* option) const {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    [[maybe_unused]] const auto outer_owner = connected_path_ ? connected_path_->retainForCallback() : lib::Owner<CWXPath>{};
    auto events = node.newElement("Events");
    if (!comment_.empty()) events.newAttr("comment", comment_);
    // Dのforeachは、入れ子の出力コールバックをまたいで元のスライスを保持する。
    const auto retained = trees_;
    for (const auto& value : retained) checkedEventTree(value)->toNode(events, option);
}

void AbstractEventTreeOwner::deepCopyEventTreeOwner(const EventTreeOwner& source) {
    [[maybe_unused]] const auto self_owner = retainForCallback();
    [[maybe_unused]] const auto source_owner =
        const_cast<EventTreeOwner&>(source).retainForCallback();
    while (!trees_.empty()) removeEvent(0);
    lib::Array<EventTreePtr> copies;
    for (const auto& tree : source.trees()) if (tree) copies.push_back(tree->dup());
    for (const auto& tree : copies) add(tree);
    commentForEvents(source.commentForEvents());
}

ContentPtr createInitVariablesTree(const flag::FlagDir& directory) {
    return createInitVariablesTree(directory.allFlags(), directory.allSteps(), directory.allVariants());
}

ContentPtr createInitVariablesTree(
    const lib::Array<lib::Owner<flag::Flag>>& flags,
    const lib::Array<lib::Owner<flag::Step>>& steps,
    const lib::Array<lib::Owner<flag::Variant>>& variants) {
    ContentPtr first, last;
    const auto append = [&](const ContentPtr& value) {
        if (last) last->add(nullptr, value);
        else first = value;
        last = value;
    };
    for (const auto& variant : variants) if (variant) {
        auto value = std::make_shared<Content>(CType::SetVariant);
        value->variant(variant->path());
        value->expression(expression::variantValueToText(expression::VariantVal(*variant)));
        append(value);
    }
    for (const auto& step : steps) if (step) {
        auto value = std::make_shared<Content>(CType::SetStep);
        value->step(step->path());
        value->stepValue(step->select());
        append(value);
    }
    for (const auto& flag : flags) if (flag) {
        auto value = std::make_shared<Content>(CType::SetFlag);
        value->flag(flag->path());
        value->flagValue(flag->onOff());
        append(value);
    }
    return first;
}

XNode ContentInitializer::toNode() const {
    auto node = XNode::create(XML_NAME);
    toNodeImpl(node);
    return node;
}
void ContentInitializer::toNode(XNode& node) const {
    auto output = node.newElement(XML_NAME);
    toNodeImpl(output);
}
void ContentInitializer::toNodeImpl(XNode& node) const {
    node.newAttr("dataVersion", dataVersion);
    for (const auto& [type, value] : initializer) {
        static_cast<void>(type);
        checkedContent(value)->toNode(node, nullptr);
    }
}
void ContentInitializer::fromNode(XNode& node) {
    if (!node.valid() || node.name() != XML_NAME) throw EventException("Node is not content initializer");
    dataVersion = node.attr("dataVersion", true);
    initializer.clear();
    node.setLiveHandler("", [this](XNode& child) {
        const auto value = checkedContent(Content::createFromNode(child, nullptr));
        initializer[value->type()] = value;
    });
    node.parseLive();
}
void ContentInitializer::fromNode(const XNode& source) {
    // 追加のconstハンドルの呼び出し側も、別の
    // 子巡回を作らず、同じ登録・解析実装で扱う。
    auto node = source;
    fromNode(node);
}

lib::Array<std::uint32_t> roundsFromNode(const XNode& node, const system::XMLInfo*) {
    if (!node.valid() || node.name() != ROUNDS_XML_NAME) return {};
    lib::Array<std::uint32_t> result;
    for (const auto& value : utils::decodeLf(node.value(), true)) {
        const auto parsed = integerText<std::int32_t>(value);
        if (parsed < 0) {
            result.push_back(0U - static_cast<std::uint32_t>(parsed));
        }
    }
    return result;
}
void roundsToNode(XNode& node, const lib::Array<std::uint32_t>& rounds) {
    lib::Array<String> values;
    for (const auto value : rounds) {
        values.push_back(std::to_string(
            std::bit_cast<std::int32_t>(0U - value)));
    }
    node.newElement(ROUNDS_XML_NAME, utils::encodeLf(values));
}
XNode roundsToNode(const lib::Array<std::uint32_t>& rounds) {
    auto node = XNode::create(ROUNDS_XML_NAME);
    lib::Array<String> values;
    for (const auto value : rounds) {
        values.push_back(std::to_string(
            std::bit_cast<std::int32_t>(0U - value)));
    }
    node.value(utils::encodeLf(values));
    return node;
}
String roundsToXML(const lib::Array<std::uint32_t>& rounds) { return roundsToNode(rounds).text(); }

}  // namespace cwxeditor::cwx::event
