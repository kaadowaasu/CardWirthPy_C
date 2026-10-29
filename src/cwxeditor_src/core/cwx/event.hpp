#pragma once

// cwx/event.dの移植可能なC++20実装
// 。 D版と同様、CTypeごとに別クラスを作らず
// 、一つの値を多く持つContentにイベントの内
// 容をまとめる。 CDetail・Content・
// EventTreeのモデルを共有所有と弱い親リン
// クで表す。 XMLにはcwx/xmlの小さなDOM
// を使い、dxml・GUI・OSには依存しない。

#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/coupon.hpp"
#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/textholder.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/array_slice.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <algorithm>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::card {
class CardImage;
class EffectCard;
class XMLOption;
}

namespace cwxeditor::cwx::props {
class CProps;
}

namespace cwxeditor::cwx::summary {
class Summary;
}

namespace cwxeditor::cwx::skin {
class Skin;
}

namespace cwxeditor::cwx::flag {
class Flag;
class Step;
class Variant;
class FlagDir;
}

namespace cwxeditor::cwx::structs {
enum class DialogStatus : std::int32_t;
}

namespace cwxeditor::cwx::expression {
class Expression;
struct ExprError;
struct VariableInfo;
struct VariantVal;
}

namespace cwxeditor::cwx::event {

using String = lib::String;
using StringView = lib::StringView;
using XNode = xml::XNode;
using CType = types::CType;
using CTypeGroup = types::CTypeGroup;
using CArg = types::CArg;
using CNextType = types::CNextType;
using FKeyCode = system::FKeyCode;
using MatchingType = types::MatchingType;
using UseCounter = usecounter::UseCounter;
using CWXPath = path::CWXPath;
using StartId = usecounter::StartId;

class Content;
class EventTree;
class EventTreeOwner;
class AbstractEventTreeOwner;

using ContentPtr = lib::Owner<Content>;
using ContentObserver = lib::Observer<Content>;
using EventTreePtr = lib::Owner<EventTree>;
using EventTreeObserver = lib::Observer<EventTree>;
using StartUseCounter = lib::Owner<UseCounter>;
using ChangeHandler = lib::Callback<void()>;

class EventException : public std::runtime_error {
public:
    explicit EventException(String message) : std::runtime_error(std::move(message)) {}
};

/// ツールバーとXMLの読み書きで使う、人が読めるメタデータ。
struct CDetail {
    lib::Array<String> names;
    String type;
    CNextType nextType{CNextType::None};
    bool owner{};
    lib::Dictionary<CArg, String> args;

    [[nodiscard]] bool use(CArg arg) const;
    /// 空なら、引数を子ノードで表す。
    [[nodiscard]] String attr(CArg arg) const;

    [[nodiscard]] static CDetail opCall(String name, String type,
                                         CNextType nextType, bool owner);
    [[nodiscard]] static CDetail opCall(
        String name, String type, CNextType nextType, bool owner,
        lib::Dictionary<CArg, String> args);
    [[nodiscard]] static CDetail opCall(
        lib::Array<String> names, String type, CNextType nextType, bool owner,
        lib::Dictionary<CArg, String> args);
    [[nodiscard]] static CDetail fromType(CType type);
};

/// ツールバーの順序と、XMLの名前から種別への逆引き。
[[nodiscard]] const lib::Array<CTypeGroup>& CTYPE_GROUPS_LIST();
[[nodiscard]] const lib::OrderedDictionary<CTypeGroup, lib::Array<CType>>&
CTYPE_GROUP();
[[nodiscard]] const lib::OrderedDictionary<CType, CDetail>& CONTENT_DETAILS();
[[nodiscard]] CDetail contentDetail(CType type);
[[nodiscard]] const lib::OrderedDictionary<
    String, lib::Dictionary<String, CType>>& CTYPE_MAP();
[[nodiscard]] CType cTypeFrom(StringView name, StringView type);
[[nodiscard]] bool isBranchContent(CType type);
[[nodiscard]] StartId toStartId(StringView start);

/// 口調ごとのセリフ（必要クーポンとメッセージ本文）。
class SDialog : public CWXPath,
                public textholder::ITextHolder,
                public std::enable_shared_from_this<SDialog> {
public:
    inline static constexpr StringView XML_NAME = "Dialog";

    explicit SDialog(String text = {}, lib::Array<String> requiredCoupons = {});
    SDialog(const SDialog& other);
    ~SDialog() override;

    [[nodiscard]] lib::Owner<CWXPath> retainForCallback() override {
        return std::static_pointer_cast<CWXPath>(weak_from_this().lock());
    }
    [[nodiscard]] ContentPtr parent() const;
    void parent(const ContentPtr& value);
    [[nodiscard]] lib::Owner<UseCounter> useCounter() const { return use_counter_; }
    [[nodiscard]] lib::Owner<CWXPath> ucOwner() const { return uc_owner_; }
    void setUseCounter(const lib::Owner<UseCounter>& counter,
                       const lib::Owner<CWXPath>& owner = {});
    void removeUseCounter() noexcept;

    [[nodiscard]] lib::Owner<SDialog> dup() const;
    [[nodiscard]] bool opEquals(const SDialog& other) const noexcept;
    [[nodiscard]] String text() const noexcept { return text_; }
    void text(StringView value) override;
    [[nodiscard]] const lib::Array<String>& rCoupons() const noexcept {
        return required_coupons_;
    }
    void rCoupons(lib::Array<String> value);

    void changed() override;
    [[nodiscard]] lib::Array<String> fontsInText() const override;
    [[nodiscard]] lib::Array<String> flagsInText() const override;
    [[nodiscard]] lib::Array<String> stepsInText() const override;
    [[nodiscard]] lib::Array<String> variantsInText() const override;
    [[nodiscard]] std::vector<char> colorsInText() const;
    void changeInText(std::size_t index, StringView id) override;
    void changeInText(
        std::size_t index,
        const usecounter::PathId& id) override;
    void changeFlagInText(std::size_t index, StringView id) override;
    void changeStepInText(std::size_t index, StringView id) override;
    void changeVariantInText(std::size_t index, StringView id) override;
    void changePathInText(std::size_t index, StringView id) override;

    [[nodiscard]] XNode toNode() const;
    void toNode(XNode& dialogsNode) const;
    [[nodiscard]] static lib::Owner<SDialog> createFromNode(
        XNode& node, const system::XMLInfo* version = nullptr);
    [[nodiscard]] static lib::Owner<SDialog> createFromNode(
        const XNode& node, const system::XMLInfo* version = nullptr);

    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] lib::Owner<CWXPath> findCWXPath(StringView value) override;
    [[nodiscard]] lib::Array<lib::Observer<CWXPath>> cwxChilds() override;
    [[nodiscard]] lib::Observer<CWXPath> cwxParent() override;

private:
    void toNodeImpl(XNode& node) const;
    void reindexText();
    void rebuildCouponUsers(const lib::Array<String>& values);
    lib::Array<lib::Owner<usecounter::CouponUser>> coupon_users_;
    String text_;
    // DのSDialogが持つTextHolderと同じく、メッセー
    // ジを実際のCWXPathの子として保持する。 text_は、既
    // 存のC++ APIとXML処理が値として使うキャッシュにする。
    lib::Owner<textholder::TextHolder> text_holder_;
    lib::Array<String> required_coupons_;
    lib::Array<String> text_flags_;
    lib::Array<String> text_steps_;
    lib::Array<String> text_variants_;
    lib::Array<String> text_fonts_;
    std::vector<char> text_colors_;
    ContentObserver parent_;
    lib::Owner<UseCounter> use_counter_;
    lib::Owner<CWXPath> uc_owner_;
    ChangeHandler change_;
};

/// 一つのイベントコンテント。スカラー項目はDの生成プロ
/// パティに対応する。 未使用の項目は既定値を保ち、CDetail::args
/// に従ってXMLへの出力を省く。
class Content : public virtual CWXPath,
                public background::BackgroundOwner,
                public motion::MotionOwner,
                public coupon::CouponsOwner,
                public path::Commentable,
                public textholder::ITextHolder,
                public usecounter::detail::OrdinaryFlagStepChangeOwner,
                public usecounter::ChgPathCallback,
                public usecounter::ChgAreaCallback,
                public usecounter::ChgBattleCallback,
                public usecounter::ChgCouponCallback,
                public usecounter::ChgVariantCallback,
                public std::enable_shared_from_this<Content> {
public:
    explicit Content(CType type = CType::Start, String name = {});
    ~Content() override;

    [[nodiscard]] lib::Owner<CWXPath> retainForCallback() override {
        return std::static_pointer_cast<CWXPath>(weak_from_this().lock());
    }
    [[nodiscard]] std::weak_ptr<path::OwnerLifetimeToken>
    backgroundLifetimeToken() const noexcept override {
        return background_lifetime_;
    }

    [[nodiscard]] ContentPtr dup() const;
    void shallowCopy(const Content& other);
    [[nodiscard]] bool opEquals(const Content& other) const noexcept;

    [[nodiscard]] const String& eventId() const noexcept { return id_; }
    [[nodiscard]] CType type() const noexcept { return type_; }
    [[nodiscard]] CDetail detail() const { return contentDetail(type_); }
    [[nodiscard]] std::uint64_t updateCounter() const noexcept {
        return update_counter_;
    }
    [[nodiscard]] bool canConvert(CType value) const;
    void convertType(CType value, const ContentPtr& base = {},
                     const props::CProps* prop = nullptr,
                     const structs::DialogStatus* dialogStatus = nullptr,
                     const skin::Skin* skin = nullptr,
                     StringView scenarioPath = {}, StringView wsnVersion = {});
    void normalizeChildName(const props::CProps& prop, const ContentPtr& child);

    [[nodiscard]] const String& name() const noexcept { return name_; }
    [[nodiscard]] std::vector<char> namesInName() const;
    [[nodiscard]] lib::Array<String> flagsInName() const;
    [[nodiscard]] lib::Array<String> stepsInName() const;
    [[nodiscard]] lib::Array<String> variantsInName() const;
    [[nodiscard]] lib::Array<String> flagsInCoupons() const;
    [[nodiscard]] lib::Array<String> stepsInCoupons() const;
    [[nodiscard]] lib::Array<String> variantsInCoupons() const;
    [[nodiscard]] lib::Array<expression::ExprError> getExpressionErrors(
        const props::CProps* prop, const expression::VariableInfo& info) const;
    [[nodiscard]] expression::VariantVal getExpressionReturnValue(
        const props::CProps* prop) const;
    [[nodiscard]] lib::Array<String> flagsInExpression() const;
    [[nodiscard]] lib::Array<String> stepsInExpression() const;
    [[nodiscard]] lib::Array<String> variantsInExpression() const;
    [[nodiscard]] lib::Array<String> flagsInGossip() const;
    [[nodiscard]] lib::Array<String> stepsInGossip() const;
    [[nodiscard]] lib::Array<String> variantsInGossip() const;
    void setName(String value);
    void setName(const props::CProps* prop, String value);
    [[nodiscard]] String comment() const noexcept override { return comment_; }
    void comment(String value) override;

    [[nodiscard]] ContentPtr parent() const { return parent_.lock(); }
    void parent(const ContentPtr& value);
    [[nodiscard]] EventTreePtr tree() const;
    [[nodiscard]] ContentPtr parentStart() const;
    [[nodiscard]] lib::Array<std::size_t> ctPath() const;
    [[nodiscard]] ContentPtr fromPath(const lib::Array<std::size_t>& path);
    [[nodiscard]] bool isDescendant(const ContentPtr& value) const;
    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] lib::Owner<CWXPath> findCWXPath(StringView value) override;
    [[nodiscard]] lib::Array<lib::Observer<CWXPath>> cwxChilds() override;
    [[nodiscard]] lib::Observer<CWXPath> cwxParent() override;

    [[nodiscard]] lib::Array<ContentPtr>& next() noexcept { return next_; }
    [[nodiscard]] const lib::Array<ContentPtr>& next() const noexcept { return next_; }
    [[nodiscard]] std::size_t countChildren() const;
    [[nodiscard]] lib::Array<ContentPtr> allChildren() const;
    void swapContent(std::size_t first, std::size_t second);
    void add(const ContentPtr& value);
    void add(const props::CProps* prop, const ContentPtr& value);
    void insert(std::size_t index, const ContentPtr& value);
    void insert(const props::CProps* prop, std::size_t index, const ContentPtr& value);
    void remove(std::size_t index);
    void remove(const ContentPtr& value);

    [[nodiscard]] lib::Owner<UseCounter> useCounter() const { return use_counter_; }
    [[nodiscard]] lib::Owner<CWXPath> ucOwner() const { return uc_owner_; }
    [[nodiscard]] lib::Owner<CWXPath> ucOwner() override { return uc_owner_; }
    [[nodiscard]] std::uint64_t useCounterRevision() const noexcept override { return use_counter_revision_; }
    [[nodiscard]] std::uint64_t useCounterBindingRevision() const noexcept override { return use_counter_binding_revision_; }
    bool changeCallback(const usecounter::PathId& oldValue,
                         const usecounter::PathId& newValue) override;
    bool changeCallback(const usecounter::AreaId& oldValue,
                        const usecounter::AreaId& newValue) override;
    bool changeCallback(const usecounter::BattleId& oldValue,
                        const usecounter::BattleId& newValue) override;
    bool changeCallback(const usecounter::CouponId& oldValue,
                        const usecounter::CouponId& newValue) override;
    bool changeCallback(const usecounter::VariantId& oldValue,
                        const usecounter::VariantId& newValue) override;
    void setUseCounter(const lib::Owner<UseCounter>& counter,
                       const lib::Owner<CWXPath>& owner = {}) override;
    void removeUseCounter() noexcept override;
    void setStartUseCounter(const StartUseCounter& counter);
    void removeStartUseCounter() noexcept;
    [[nodiscard]] StartUseCounter startUseCounter() const noexcept { return start_use_counter_; }
    void changeHandler(ChangeHandler value);
    [[nodiscard]] ChangeHandler changeHandler() const {
        // DのデリゲートはGCオブジェクトを保持するが、ネイティブのコールバ
        // ックは寿命トークンを参照する。 親子の所有循環を避け、共有所有・ス
        // タック上のどちらのContentも破棄後に呼び出さないようにする。
        const std::weak_ptr<int> lifetime = callback_lifetime_;
        return [this, lifetime] { if (!lifetime.expired()) const_cast<Content*>(this)->changed(); };
    }
    void changed() override;

    [[nodiscard]] String text() const noexcept { return text_; }
    void text(StringView value) override;
    [[nodiscard]] lib::Array<String> fontsInText() const override;
    [[nodiscard]] lib::Array<String> flagsInText() const override;
    [[nodiscard]] lib::Array<String> stepsInText() const override;
    [[nodiscard]] lib::Array<String> variantsInText() const override;
    [[nodiscard]] std::vector<char> colorsInText() const;
    void changeInText(std::size_t index, StringView id) override;
    void changeInText(
        std::size_t index,
        const usecounter::PathId& id) override;
    void changeFlagInText(std::size_t index, StringView id) override;
    void changeStepInText(std::size_t index, StringView id) override;
    void changeVariantInText(std::size_t index, StringView id) override;
    void changePathInText(std::size_t index, StringView id) override;

#define CWX_EVENT_SCALAR_IMPL(T, N, REBIND, NOTIFICATION_COUNT)                  \
    [[nodiscard]] T N() const noexcept { return N##_; }                          \
    void N(T value) { PropertyValidation validate{*this}; if (N##_ != value) { const auto keep_alive = weak_from_this().lock(); \
        for (std::size_t notification = 0; notification < NOTIFICATION_COUNT; ++notification) notifyBeforeAssignment(); \
        N##_ = value; if constexpr (REBIND) { if (use_counter_) rebuildUseCounterUsers(); } } }
// DのsetValUCsは単純な値に対して何もしない。ここで再構成すると、無関係なUsers
// の 削除・追加が発生し、公開されているvalueSetの別名参照まで無効になる。
#define CWX_EVENT_SCALAR(T, N) CWX_EVENT_SCALAR_IMPL(T, N, false, 1)
// DのProp(New)はContent.changed()で1回、入れ子のカウンタ
// ーUser設定でさらに1回通知する。 どちらもUserのIDを変更する前に行う。
#define CWX_EVENT_COUNTER_ID(T, N, REENTRY) \
    [[nodiscard]] T N() const noexcept { return N##_; } \
    void N(T value) { assignCounterProperty(N##_, std::move(value), CounterSetterBehavior::REENTRY, CounterProperty::N); }
#define CWX_EVENT_COUNTER_STRING(N, REENTRY) \
    [[nodiscard]] const String& N() const noexcept { return N##_; } \
    void N(String value) { assignCounterProperty(N##_, std::move(value), CounterSetterBehavior::REENTRY, CounterProperty::N); }
// 元実装(D)の MaxMin mixin 相当: D の Prop setter は代入前に値を [Min, Max] へ
// クランプしてから現在値と比較する。クランプ対象のスカラーだけこのマクロを使う。
#define CWX_EVENT_SCALAR_CLAMPED(T, N, MAXV, MINV)  \
    [[nodiscard]] T N() const noexcept { return N##_; }  \
    void N(T value) { PropertyValidation validate{*this};  \
        if (value > static_cast<T>(MAXV)) value = static_cast<T>(MAXV);  \
        if (value < static_cast<T>(MINV)) value = static_cast<T>(MINV);  \
        if (N##_ != value) { const auto keep_alive = weak_from_this().lock();  \
        notifyBeforeAssignment(); N##_ = value; } }
#define CWX_EVENT_STRING_STORAGE_IMPL(T, N, REBIND, NOTIFICATION_COUNT)          \
    [[nodiscard]] std::conditional_t<std::is_same_v<T, lib::NullableString>, T, const T&> N() const noexcept { return N##_; } \
    void N(String value) { assignStringProperty(N##_, T(std::move(value)), REBIND, NOTIFICATION_COUNT); } \
    template <typename Input> requires (std::is_same_v<T, lib::NullableString> && \
        (std::is_same_v<std::remove_cvref_t<Input>, lib::NullableString> || std::is_same_v<std::remove_cvref_t<Input>, std::nullptr_t>)) \
    void N(Input&& value) { assignStringProperty(N##_, T(std::forward<Input>(value)), REBIND, NOTIFICATION_COUNT); }
#define CWX_EVENT_STRING_STORAGE(T, N) CWX_EVENT_STRING_STORAGE_IMPL(T, N, true, 1)
#define CWX_EVENT_STRING(N) CWX_EVENT_STRING_STORAGE_IMPL(String, N, true, 1)
#define CWX_EVENT_PLAIN_STRING(N) CWX_EVENT_STRING_STORAGE_IMPL(String, N, false, 1)
#define CWX_EVENT_VECTOR(T, N)                                                    \
    [[nodiscard]] const lib::Array<T>& N() const noexcept { return N##_; }         \
    void N(lib::Array<T> value) { PropertyValidation validate{*this}; if (N##_ != value) { const auto keep_alive = weak_from_this().lock(); \
        notifyBeforeAssignment(); N##_ = std::move(value); if (use_counter_) rebuildUseCounterUsers(); } }

    CWX_EVENT_COUNTER_ID(std::uint64_t, area, entryGuard)
    CWX_EVENT_COUNTER_ID(std::uint64_t, battle, entryGuard)
    CWX_EVENT_COUNTER_ID(std::uint64_t, packages, alwaysRebind)
    CWX_EVENT_STRING_STORAGE(lib::NullableString, flag)
    CWX_EVENT_STRING_STORAGE(lib::NullableString, step)
    CWX_EVENT_STRING_STORAGE(lib::NullableString, variant)
    [[nodiscard]] const String& expression() const noexcept { return expression_; }
    void expression(String value);
    CWX_EVENT_COUNTER_STRING(bgmPath, alwaysRebind)
    CWX_EVENT_SCALAR_CLAMPED(std::uint32_t, bgmChannel, 1, 0)
    CWX_EVENT_SCALAR_CLAMPED(std::uint32_t, bgmVolume, 100, 0)
    CWX_EVENT_SCALAR(std::uint32_t, bgmLoopCount)
    CWX_EVENT_SCALAR(std::uint32_t, bgmFadeIn)
    CWX_EVENT_COUNTER_STRING(soundPath, alwaysRebind)
    CWX_EVENT_SCALAR_CLAMPED(std::uint32_t, soundChannel, 1, 0)
    CWX_EVENT_SCALAR_CLAMPED(std::uint32_t, soundVolume, 100, 0)
    CWX_EVENT_SCALAR_CLAMPED(std::uint32_t, soundLoopCount, std::numeric_limits<int>::max(), 1)
    CWX_EVENT_SCALAR(std::uint32_t, soundFadeIn)
    CWX_EVENT_SCALAR(bool, initialEffect)
    CWX_EVENT_COUNTER_STRING(initialSoundPath, alwaysRebind)
    CWX_EVENT_SCALAR_CLAMPED(std::uint32_t, initialSoundChannel, 1, 0)
    CWX_EVENT_SCALAR_CLAMPED(std::uint32_t, initialSoundVolume, 100, 0)
    CWX_EVENT_SCALAR_CLAMPED(std::uint32_t, initialSoundLoopCount, std::numeric_limits<int>::max(), 1)
    CWX_EVENT_SCALAR(std::uint32_t, initialSoundFadeIn)
    CWX_EVENT_COUNTER_ID(std::uint64_t, casts, alwaysRebind)
    CWX_EVENT_COUNTER_ID(std::uint64_t, item, alwaysRebind)
    CWX_EVENT_COUNTER_ID(std::uint64_t, skill, alwaysRebind)
    CWX_EVENT_COUNTER_ID(std::uint64_t, beast, alwaysRebind)
    CWX_EVENT_COUNTER_ID(std::uint64_t, info, alwaysRebind)
    [[nodiscard]] const String& start() const noexcept { return start_; }
    void start(String value);
    [[nodiscard]] const String& coupon() const noexcept { return coupon_; }
    void coupon(String value);
    [[nodiscard]] const String& gossip() const noexcept { return gossip_; }
    void gossip(String value);
    CWX_EVENT_COUNTER_STRING(completeStamp, alwaysRebind)
    CWX_EVENT_SCALAR(types::Mental, mental)
    CWX_EVENT_SCALAR(types::Physical, physical)
    CWX_EVENT_SCALAR(types::Status, status)
    CWX_EVENT_SCALAR(types::Range, range)
    CWX_EVENT_SCALAR(types::CardVisual, cardVisual)
    CWX_EVENT_SCALAR(types::EffectType, effectType)
    CWX_EVENT_SCALAR(types::Resist, resist)
    CWX_EVENT_SCALAR(types::Transition, transition)
    CWX_EVENT_SCALAR(bool, targetAll)
    CWX_EVENT_SCALAR(types::SelectionMethod, selectionMethod)
    CWX_EVENT_SCALAR(bool, average)
    CWX_EVENT_SCALAR(bool, complete)
    CWX_EVENT_SCALAR_CLAMPED(int, unsignedLevel, std::numeric_limits<int>::max(), 1)
    CWX_EVENT_SCALAR(int, signedLevel)
    CWX_EVENT_SCALAR_CLAMPED(int, successRate, 5, -5)
    CWX_EVENT_SCALAR_CLAMPED(int, transitionSpeed, 10, 0)
    CWX_EVENT_SCALAR_CLAMPED(int, cardSpeed, 10, -1)
    CWX_EVENT_SCALAR(bool, overrideCardSpeed)
    [[nodiscard]] int percent() const noexcept { return percent_; }
    void percent(int value);
    CWX_EVENT_SCALAR(bool, flagValue)
    [[nodiscard]] int stepValue() const noexcept { return stepValue_; }
    void stepValue(int value) {
        if (value < 0) value = 0;
        if (stepValue_ != value) { changed(); stepValue_ = value; }
    }
    CWX_EVENT_SCALAR(int, couponValue)
    CWX_EVENT_SCALAR_CLAMPED(int, partyNumber, std::numeric_limits<int>::max(), 1)
    CWX_EVENT_SCALAR_CLAMPED(int, cardNumber, std::numeric_limits<int>::max(), 0)
    CWX_EVENT_SCALAR_CLAMPED(int, money, std::numeric_limits<int>::max(), 0)
    [[nodiscard]] int wait() const noexcept { return wait_; }
    void wait(int value) {
        // Dは等価判定の前に範囲を補正するため、0の状態に別の負数を設定しても何もしない。
        if (value < 0) value = 0;
        if (wait_ != value) { changed(); wait_ = value; }
    }
    CWX_EVENT_STRING_STORAGE(lib::NullableString, flag2)
    CWX_EVENT_STRING_STORAGE(lib::NullableString, step2)
    CWX_EVENT_SCALAR_CLAMPED(int, levelMin, std::numeric_limits<int>::max(), 0)
    CWX_EVENT_SCALAR_CLAMPED(int, levelMax, std::numeric_limits<int>::max(), 0)
    [[nodiscard]] types::Range keyCodeRange() const noexcept { return keyCodeRange_; }
    void keyCodeRange(types::Range value) {
        struct ValidateOnExit { Content& content; ~ValidateOnExit() { content.validate(); } } validate_on_exit{*this};
        switch (value) {
        case types::Range::Selected: case types::Range::Random:
        case types::Range::Backpack: case types::Range::PartyAndBackpack:
        case types::Range::SelectedCard: break;
        default: throw EventException("Invalid keyCodeRange");
        }
        if (keyCodeRange_ != value) { changed(); keyCodeRange_ = value; }
    }
    CWX_EVENT_SCALAR(bool, targetIsSkill)
    CWX_EVENT_SCALAR(bool, targetIsItem)
    CWX_EVENT_SCALAR(bool, targetIsBeast)
    CWX_EVENT_SCALAR(bool, targetIsHand)
    CWX_EVENT_COUNTER_STRING(keyCode, alwaysRebind)
    CWX_EVENT_SCALAR(int, initValue)
    CWX_EVENT_SCALAR(types::Comparison4, comparison4)
    CWX_EVENT_SCALAR(types::Comparison3, comparison3)
    CWX_EVENT_SCALAR(std::uint32_t, round)
    CWX_EVENT_COUNTER_STRING(cellName, alwaysRebind)
    CWX_EVENT_COUNTER_STRING(cardGroup, alwaysRebind)
    CWX_EVENT_SCALAR(types::CoordinateType, positionType)
    CWX_EVENT_SCALAR(int, x)
    CWX_EVENT_SCALAR(int, y)
    CWX_EVENT_SCALAR(types::CoordinateType, sizeType)
    CWX_EVENT_SCALAR(int, width)
    CWX_EVENT_SCALAR(int, height)
    CWX_EVENT_SCALAR(int, scale)
    CWX_EVENT_SCALAR(int, layer)
    CWX_EVENT_SCALAR(bool, doAnime)
    CWX_EVENT_SCALAR(bool, ignoreEffectBooster)
    CWX_EVENT_SCALAR(bool, singleLine)
    [[nodiscard]] std::uint32_t selectionColumns() const noexcept { return selectionColumns_; }
    void selectionColumns(std::uint32_t value) {
        if (value == 0U) value = 1U;
        if (selectionColumns_ != value) { selectionColumns_ = value; changed(); }
    }
    CWX_EVENT_SCALAR(bool, centeringX)
    CWX_EVENT_SCALAR(bool, centeringY)
    CWX_EVENT_SCALAR(bool, boundaryCheck)
    CWX_EVENT_SCALAR(types::StartAction, startAction)
    CWX_EVENT_SCALAR(bool, ignite)
    CWX_EVENT_SCALAR(types::MatchingType, matchingType)
    [[nodiscard]] bool expandSPChars() const noexcept { return expandSPChars_; }
    void expandSPChars(bool value);
    CWX_EVENT_COUNTER_STRING(holdingCoupon, entryGuard)
    CWX_EVENT_SCALAR(bool, refAbility)
    CWX_EVENT_SCALAR(types::AbsorbTo, absorbTo)
    CWX_EVENT_SCALAR(bool, selectCard)
    CWX_EVENT_SCALAR(bool, selectTalker)
    CWX_EVENT_SCALAR(bool, invertResult)
    CWX_EVENT_SCALAR(types::MatchingCondition, matchingCondition)
    CWX_EVENT_SCALAR(bool, consumeCard)
    [[nodiscard]] types::Talker talkerNC() const noexcept { return talkerNC_; }
    void talkerNC(types::Talker value);
    CWX_EVENT_SCALAR(types::EnvironmentStatus, backpackEnabled)
    CWX_EVENT_SCALAR(types::EnvironmentStatus, gameOverEnabled)
    CWX_EVENT_SCALAR(types::EnvironmentStatus, runAwayEnabled)
    CWX_EVENT_PLAIN_STRING(xmlType)
    CWX_EVENT_STRING(xmlStr)
    CWX_EVENT_SCALAR(int, xmlAssignment)
    CWX_EVENT_PLAIN_STRING(xmlVariable)

    [[nodiscard]] const lib::Array<String>& keyCodes() const noexcept { return key_codes_; }
    // DのProp(New)と入れ子のKeyCodesUserは、それぞれ代入
    // 前に通知する。 ネイティブの平坦な表現でも、両方のコールバックを保つ。
    void keyCodes(lib::Array<String> value);
    // Dは保持中のコレクションを読み、子Userの初期化通知中に確
    // 定した部分状態も参照する。 古い配列キャッシュは使わない。
    [[nodiscard]] lib::Array<String> couponNames() const {
        return coupon_names_producer_ ? coupon_names_producer_->couponNames() : coupon_names_;
    }
    void couponNames(lib::SharedSlice<String> value);
    [[nodiscard]] const lib::Array<types::CastRange>& castRange() const noexcept { return cast_range_; }
    // DのPropと同じく、同値なら何もせず、通知は代入前に行う。
    void castRange(lib::Array<types::CastRange> value) {
        if (cast_range_ == value) return;
        changed();
        cast_range_ = std::move(value);
    }
    /// ファイルパスだけを扱う呼び出し元向けの互換表現。
    [[nodiscard]] const lib::Array<String>& cardPaths() const noexcept { return card_paths_; }
    void cardPaths(lib::Array<String> value);
    /// Dの話者画像を情報を失わずに表す。File/PC/Talkerの種別
    /// とファイル画像の位置を保持し、 cardPaths()とも同期する。
    [[nodiscard]] lib::Array<lib::Owner<card::CardImage>> cardImages() const;
    void cardImages(lib::Array<lib::Owner<card::CardImage>> value);
    [[nodiscard]] const lib::Array<String>& backs() const noexcept { return backs_; }
    void backs(lib::Array<String> value);
    [[nodiscard]] lib::SharedSlice<background::BgImagePtr>
    backgroundImages() noexcept { return backgrounds_; }
    [[nodiscard]] lib::ConstOwnerSlice<background::BgImage>
    backgroundImages() const noexcept override {
        return lib::ConstOwnerSlice<background::BgImage>(backgrounds_);
    }
    void backgroundImages(background::BgImageArray value);
    [[nodiscard]] lib::SharedSlice<lib::Owner<motion::Motion>> motions() override { return motions_; }
    [[nodiscard]] lib::ConstOwnerSlice<motion::Motion> motions() const override {
        return lib::ConstOwnerSlice<motion::Motion>(motions_);
    }
    [[nodiscard]] lib::ConstOwnerSlice<motion::Motion> motionsRef() const noexcept {
        return lib::ConstOwnerSlice<motion::Motion>(motions_);
    }
    void motions(lib::SharedSlice<lib::Owner<motion::Motion>> value);
    void motions(std::initializer_list<lib::Owner<motion::Motion>> value);
    void motions(lib::Array<lib::Owner<motion::Motion>> value);
    /// クーポン格納先への直接書き込みは、割り当て済みの配列を共有するが、管理対象の結び付けを
    /// 更新しない。 所有権と参照数の関連付けを更新するには、設定関数のオーバーロードを使う。
    [[nodiscard]] lib::SharedSlice<lib::Owner<coupon::Coupon>> coupons() override {
        return coupon_records_;
    }
    [[nodiscard]] lib::ConstOwnerSlice<coupon::Coupon> coupons() const override {
        return lib::ConstOwnerSlice<coupon::Coupon>(coupon_records_);
    }
    void coupons(lib::SharedSlice<lib::Owner<coupon::Coupon>> value);
    void coupons(std::initializer_list<lib::Owner<coupon::Coupon>> value);
    void coupons(lib::Array<lib::Owner<coupon::Coupon>> value) {
        coupons(lib::SharedSlice<lib::Owner<coupon::Coupon>>(std::move(value)));
    }
    [[nodiscard]] lib::Array<String> valuedCouponNames() const;
    void valuedCouponNames(lib::Array<String> value);
    [[nodiscard]] lib::ConstOwnerSlice<coupon::Coupon> couponRecords() const noexcept {
        return lib::ConstOwnerSlice<coupon::Coupon>(coupon_records_);
    }
    void couponRecords(lib::SharedSlice<lib::Owner<coupon::Coupon>> value) { coupons(std::move(value)); }
    void couponRecords(std::initializer_list<lib::Owner<coupon::Coupon>> value) { coupons(value); }
    void couponRecords(lib::Array<lib::Owner<coupon::Coupon>> value) { coupons(std::move(value)); }

    [[nodiscard]] const lib::Array<lib::Owner<SDialog>>& dialogs() const noexcept { return dialogs_; }
    void dialogs(lib::Array<lib::Owner<SDialog>> value);
    [[nodiscard]] const lib::Array<lib::Owner<SDialog>>& dialogsRef() const noexcept { return dialogs_; }

    [[nodiscard]] types::Target targetS() const noexcept { return target_s_; }
    void targetS(types::Target value) {
        if (!(target_s_ == value)) { changed(); target_s_ = value; }
    }

#undef CWX_EVENT_SCALAR
#undef CWX_EVENT_SCALAR_IMPL
#undef CWX_EVENT_COUNTER_ID
#undef CWX_EVENT_COUNTER_STRING
#undef CWX_EVENT_STRING
#undef CWX_EVENT_COUNTER_STRING
#undef CWX_EVENT_PLAIN_STRING
#undef CWX_EVENT_STRING_STORAGE
#undef CWX_EVENT_STRING_STORAGE_IMPL
#undef CWX_EVENT_VECTOR

    [[nodiscard]] std::uint64_t branchAreaCondition() const noexcept { return branch_area_condition_; }
    void branchAreaCondition(std::uint64_t value);
    [[nodiscard]] std::uint64_t branchBattleCondition() const noexcept { return branch_battle_condition_; }
    void branchBattleCondition(std::uint64_t value);
    [[nodiscard]] const String& branchCouponCondition() const noexcept { return branch_coupon_condition_; }
    void branchCouponCondition(String value);

    // DのinoutなSummary・クラス参照に対応する。const入力からは
    // 読み取り専用の結果を返し、 両方のオーバーロードは同じ解決処理を使う。 D
    // のEditor側では具体的なSummary型でテンプレートを実体化する。
    [[nodiscard]] lib::Owner<CWXPath> connectedResource(summary::Summary* summ);
    [[nodiscard]] lib::Owner<const CWXPath> connectedResource(const summary::Summary* summ) const;
    [[nodiscard]] lib::Array<usecounter::CouponId> connectedCoupons() const;
    [[nodiscard]] lib::Array<usecounter::GossipId> connectedGossips() const;
    [[nodiscard]] lib::Array<usecounter::CompleteStampId> connectedCompleteStamps() const;
    [[nodiscard]] lib::Array<usecounter::KeyCodeId> connectedKeyCodes() const;
    [[nodiscard]] lib::Array<usecounter::CellNameId> connectedCellNames() const;
    [[nodiscard]] lib::Array<usecounter::CardGroupId> connectedCardGroups() const;
    [[nodiscard]] String connectedFile() const;

    [[nodiscard]] String toXML(const card::XMLOption* option = nullptr) const;
    [[nodiscard]] XNode toNode(const card::XMLOption* option = nullptr) const;
    XNode toNode(XNode& parent, const card::XMLOption* option = nullptr) const;
    [[nodiscard]] static ContentPtr createFromNode(
        const XNode& node, const system::XMLInfo* version = nullptr,
        bool nextContent = true);
    [[nodiscard]] static ContentPtr createFromXML(
        StringView text, const system::XMLInfo* version, String& id);
    static void createContentsFromNode(const XNode& node,
                                       const system::XMLInfo* version,
                                       const std::function<void(ContentPtr)>& appender);

private:
    friend class EventTree;
    [[nodiscard]] path::OwnerLink<EventTree>::Access resolveTree() const;
    // DのPropの設定は、値が同じ場合でもscope(exit)の検証を実行する。
    struct PropertyValidation {
        Content& content;
        // 通知で呼び出し元の最後の所有参照が解放される場合がある。 DのGC
        // 参照と同様に、終了時の検証が済むまでこのオブジェクトを保持する。
        ContentPtr retained;
        explicit PropertyValidation(Content& value)
            : content(value), retained(value.weak_from_this().lock()) {}
        ~PropertyValidation() noexcept(false) { content.validate(); }
    };
    void prepareChild(const props::CProps* prop, const ContentPtr& value);
    void bindBackgroundImages();
    void bindBackgroundImages(const lib::Owner<UseCounter>& bindingCounter,
                              const lib::Owner<CWXPath>& bindingOwner);
    void bindBackgroundImage(const background::BgImagePtr& image,
                             const lib::Owner<UseCounter>& bindingCounter,
                             const lib::Owner<CWXPath>& bindingOwner);
    void unbindBackgroundImagesForReplacement();
    void detachBackgroundImages() noexcept;
    void validate();
    void parseAttributes(const XNode& node, const system::XMLInfo* version = nullptr);
    void putNodeData(XNode& node, const card::XMLOption* option) const;
    XNode appendToNode(XNode& parent, const card::XMLOption* option, bool standalone) const;
    void setNameInternal(String value);
    void assignName(String value);
    bool changeNumericBranchName(std::uint64_t oldValue, std::uint64_t newValue,
                                 CNextType kind);
    void bindTextProducerOwners() const;
    void bindNameCounter();
    void bindNameCounter(const lib::Owner<UseCounter>& bindingCounter,
                         const lib::Owner<CWXPath>& bindingOwner);
    void ensureGossipProducer();
    void ensureCouponProducer();
    bool checkExpandSPChars(bool value);
    void synchronizeValueProducers();
    void synchronizeCouponNames();
    void ensureExpressionProducer();
    [[nodiscard]] const expression::Expression& checkedExpression() const;
    void ensureMessageProducer();
    [[nodiscard]] bool readsNameTokens() const;

    enum class CounterSetterBehavior { entryGuard, alwaysRebind };
    enum class CounterProperty {
        area, battle, packages, bgmPath, soundPath, initialSoundPath,
        casts, item, skill, beast, info, completeStamp, keyCode, cellName,
        cardGroup, holdingCoupon
    };

    void rebuildUseCounterUsers();
    void rebuildUseCounterUsers(const lib::Owner<UseCounter>& bindingCounter,
                                const lib::Owner<CWXPath>& bindingOwner);
    void beginOrdinaryFlagStepChange(
        usecounter::detail::OrdinaryFlagStepChangeMarker& marker) noexcept override;
    void endOrdinaryFlagStepChange(
        usecounter::detail::OrdinaryFlagStepChangeMarker& marker) noexcept override;
    [[nodiscard]] bool consumeOrdinaryFlagStepChange() noexcept;
    void rebindCounterPropertyAlias(CounterProperty property);
    template <typename ID, typename User>
    void rebindCounterUserAlias(const ID& id, const lib::Owner<User>& user) {
        const auto counter = use_counter_;
        if (!counter || !user || counter->get(id) == 0U) return;
        counter->remove(id, user);
        counter->add(id, user);
    }
    void setOwnUseCounter(const lib::Owner<UseCounter>& counter,
                         const lib::Owner<CWXPath>& owner);
    void retireUseCounterUsers() noexcept;

    String id_;
    CType type_{};
    String name_;
    lib::Owner<textholder::SimpleTextHolder> name_holder_;
    lib::Owner<textholder::TextHolder> message_holder_;
    String comment_;
    std::uint64_t update_counter_{};
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};
    std::shared_ptr<path::OwnerLifetimeToken> background_lifetime_{
        std::make_shared<path::OwnerLifetimeToken>()};
    ContentObserver parent_;
    // Dは構築中にルートへ接続する。C++では共有所有を参照できるまで、トークンで保護した借用にする。
    mutable path::OwnerLink<EventTree> tree_;
    lib::Array<ContentPtr> next_;
    lib::Owner<UseCounter> use_counter_;
    lib::Owner<CWXPath> uc_owner_;
    template <typename T>
    void assignCounterProperty(T& field, T value, CounterSetterBehavior behavior,
                               CounterProperty property) {
        PropertyValidation validate{*this};
        if (field == value) return;
        const auto keep_alive = weak_from_this().lock();
        const auto finishNestedSetter = [&](std::size_t notification) {
            if (field != value) return false;
            // DのContentのPropは入れ子の書き込み後も具体的なUser
            // 設定へ進む。 Area/Battle/Couponは入口で同値を
            // 判定するため、2回目の通知だけが既にその判定を通っている。 Path
            // などの対応する設定処理は、同じ値で入っても削除・追加を行う。
            if (behavior == CounterSetterBehavior::alwaysRebind || notification == 2U)
                rebindCounterPropertyAlias(property);
            return true;
        };
        notifyBeforeAssignment();
        if (finishNestedSetter(1U)) return;
        notifyBeforeAssignment();
        if (finishNestedSetter(2U)) return;
        field = std::move(value);
        if (use_counter_) rebuildUseCounterUsers();
    }
    template <typename T>
    void assignStringProperty(T& field, T value, bool rebuildUsers,
                              std::size_t notificationCount) {
        PropertyValidation validate{*this};
        if (field == value) return;
        const auto keep_alive = weak_from_this().lock();
        for (std::size_t notification = 0; notification < notificationCount; ++notification)
            notifyBeforeAssignment();
        field = std::move(value);
        if (rebuildUsers && use_counter_) rebuildUseCounterUsers();
    }
    void notifyBeforeAssignment();
    void setNumericBranchCondition(std::uint64_t& field, std::uint64_t value, CNextType kind);
    StartUseCounter start_use_counter_;
    class StartReferenceUser;
    lib::Owner<StartReferenceUser> start_user_;
    lib::Owner<usecounter::AreaUser> area_user_;
    lib::Owner<usecounter::BattleUser> battle_user_;
    lib::Owner<usecounter::PackageUser> package_user_;
    lib::Owner<usecounter::CastUser> cast_user_;
    lib::Owner<usecounter::SkillUser> skill_user_;
    lib::Owner<usecounter::ItemUser> item_user_;
    lib::Owner<usecounter::BeastUser> beast_user_;
    lib::Owner<usecounter::InfoUser> info_user_;
    lib::Owner<usecounter::AreaUser> branch_area_user_;
    lib::Owner<usecounter::BattleUser> branch_battle_user_;

    lib::Owner<usecounter::FlagUser> flag_user_;
    lib::Owner<usecounter::FlagUser> flag2_user_;
    lib::Owner<usecounter::StepUser> step_user_;
    lib::Owner<usecounter::StepUser> step2_user_;
    lib::Owner<usecounter::VariantUser> variant_user_;
    lib::Owner<usecounter::CouponUser> holding_coupon_user_;
    lib::Owner<coupon::CouponNamesUser> coupon_names_producer_;
    std::map<String, lib::Owner<usecounter::PathUser>> path_users_;
    std::map<String, lib::Observer<card::CardImage>> path_image_owners_;
    lib::Owner<expression::Expression> expression_tracker_;
    lib::Owner<textholder::TextHolder> xml_text_holder_;
    lib::Owner<usecounter::CouponUser> coupon_user_;
    lib::Owner<usecounter::CouponUser> branch_coupon_user_;
    lib::Owner<usecounter::GossipUser> gossip_user_;
    lib::Owner<usecounter::KeyCodeUser> key_code_user_;
    lib::Owner<usecounter::CompleteStampUser> complete_stamp_user_;
    lib::Owner<usecounter::CellNameUser> cell_name_user_;
    lib::Owner<usecounter::CardGroupUser> card_group_user_;
    lib::Array<lib::Owner<usecounter::KeyCodeUser>> key_code_users_;
    bool rebuilding_use_counter_{};
    usecounter::detail::OrdinaryFlagStepChangeMarker* ordinary_flag_step_change_{};
    std::uint64_t use_counter_revision_{};
    std::uint64_t use_counter_binding_revision_{};
    ChangeHandler change_;

    String text_;
    lib::Array<lib::Owner<SDialog>> dialogs_;
    lib::Array<String> card_paths_;
    lib::Array<lib::Owner<card::CardImage>> card_images_;
    lib::Array<String> backs_;
    lib::SharedSlice<background::BgImagePtr> backgrounds_;
    lib::SharedSlice<lib::Owner<motion::Motion>> motions_;
    void bindMotionPayloads();
    void bindMotionPayloads(const lib::Owner<UseCounter>& bindingCounter,
                            const lib::Owner<CWXPath>& bindingOwner);
    void detachMotionPayloads() noexcept;
    lib::SharedSlice<lib::Owner<coupon::Coupon>> coupon_records_;
    lib::Array<String> key_codes_;
    lib::Array<String> coupon_names_;
    lib::Array<types::CastRange> cast_range_{types::CastRange::Party};

    std::uint64_t area_{};
    std::uint64_t battle_{};
    std::uint64_t packages_{};
    // DでUserに結び付く値の初期値はnull。ネイティブのString
    // の既定値を使うと、 未使用の全項目がnullではない空のカウンタ
    // ーキーとして登録される。 DのProp(New)のUser不在時
    // の既定取得値と、同値で戻る前の確保は別途整合が必要。 この保持方
    // 法の変更は、既存の接続経路でnull許容の値を保つものに限る。
    lib::NullableString flag_, step_, variant_;
    String expression_;
    String bgmPath_, soundPath_, initialSoundPath_;
    std::uint32_t bgmChannel_{}, bgmVolume_{100}, bgmLoopCount_{}, bgmFadeIn_{};
    std::uint32_t soundChannel_{}, soundVolume_{100}, soundLoopCount_{1}, soundFadeIn_{};
    bool initialEffect_{};
    std::uint32_t initialSoundChannel_{}, initialSoundVolume_{100}, initialSoundLoopCount_{1}, initialSoundFadeIn_{};
    std::uint64_t casts_{}, item_{}, skill_{}, beast_{}, info_{};
    String start_, coupon_, gossip_, completeStamp_;
    types::Mental mental_{};
    types::Physical physical_{};
    types::Status status_{types::Status::None};
    types::Range range_{types::Range::Field};
    types::CardVisual cardVisual_{};
    types::EffectType effectType_{types::EffectType::None};
    types::Resist resist_{types::Resist::Unfail};
    types::Transition transition_{};
    types::Target target_s_{};
    bool targetAll_{}, average_{}, complete_{};
    types::SelectionMethod selectionMethod_{};
    int unsignedLevel_{1}, signedLevel_{}, successRate_{5}, transitionSpeed_{5}, cardSpeed_{-1};
    bool overrideCardSpeed_{};
    int percent_{50}, stepValue_{}, couponValue_{}, partyNumber_{2}, cardNumber_{1}, money_{}, wait_{10};
    bool flagValue_{true};
    lib::NullableString flag2_, step2_;
    int levelMin_{}, levelMax_{};
    types::Range keyCodeRange_{types::Range::PartyAndBackpack};
    bool targetIsSkill_{true}, targetIsItem_{true}, targetIsBeast_{true}, targetIsHand_{};
    String keyCode_;
    int initValue_{};
    types::Comparison4 comparison4_{};
    types::Comparison3 comparison3_{};
    std::uint32_t round_{};
    String cellName_, cardGroup_;
    types::CoordinateType positionType_{}, sizeType_{};
    int x_{}, y_{}, width_{}, height_{}, scale_{-1}, layer_{-1};
    bool doAnime_{}, ignoreEffectBooster_{};
    bool singleLine_{};
    std::uint32_t selectionColumns_{1};
    bool centeringX_{}, centeringY_{}, boundaryCheck_{}, ignite_{};
    types::StartAction startAction_{types::StartAction::NextRound};
    types::MatchingType matchingType_{};
    bool expandSPChars_{}, refAbility_{}, selectCard_{}, selectTalker_{}, invertResult_{};
    String holdingCoupon_;
    types::AbsorbTo absorbTo_{};
    types::MatchingCondition matchingCondition_{};
    bool consumeCard_{true};
    types::Talker talkerNC_{types::Talker::Selected};
    types::EnvironmentStatus backpackEnabled_{}, gameOverEnabled_{}, runAwayEnabled_{};
    String xmlType_{"ChangeBgImage"};
    String xmlStr_;
    int xmlAssignment_{1};
    String xmlVariable_;
    std::uint64_t branch_area_condition_{}, branch_battle_condition_{};
    String branch_coupon_condition_;
};

/// イベントツリーは、一つ以上のStartコンテントとその子孫を所有する。
class EventTree : public CWXPath,
                  public path::Commentable,
                  public std::enable_shared_from_this<EventTree> {
public:
    EventTree();
    explicit EventTree(String name);
    explicit EventTree(const ContentPtr& start);
    explicit EventTree(lib::SharedSlice<ContentPtr> starts);
    explicit EventTree(lib::Array<ContentPtr> starts);
    ~EventTree() override;
    EventTree(const EventTree&) = delete;
    EventTree& operator=(const EventTree&) = delete;
    EventTree(EventTree&&) = delete;
    EventTree& operator=(EventTree&&) = delete;

    [[nodiscard]] const String& objectId() const noexcept { return object_id_; }
    [[nodiscard]] EventTreePtr dup() const;
    [[nodiscard]] bool opEquals(const EventTree& other) const noexcept;
    void copyIgnitions(const EventTree& other);

    [[nodiscard]] EventTreeOwner* owner() const noexcept;
    [[nodiscard]] bool ownedBy(
        EventTreeOwner* owner,
        std::weak_ptr<path::OwnerLifetimeToken> lifetime) const noexcept;
    void owner(EventTreeOwner* value) noexcept;
    void owner(EventTreeOwner* value,
               std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept;
    // Dは動的配列の記述子を値で返す。要素の書き換えは共有されるが、
    // 返された記述子への構造変更では、その記述子独自の長さを持つ。
    [[nodiscard]] lib::SharedSlice<const ContentPtr> starts() const;
    [[nodiscard]] lib::SharedSlice<ContentPtr> starts();
    void starts(lib::SharedSlice<ContentPtr> value);
    void starts(lib::Array<ContentPtr> value);
    void add(const ContentPtr& value);
    void insert(std::size_t index, const ContentPtr& value);
    void remove(std::size_t index);
    void remove(const ContentPtr& value);
    void swapStart(std::size_t first, std::size_t second);
    [[nodiscard]] lib::Array<String> startNames() const;
    [[nodiscard]] bool hasStart(StringView name) const;
    [[nodiscard]] ContentPtr start(StringView name) const;
    [[nodiscard]] ContentPtr fromPath(const lib::Array<std::size_t>& path) const;
    [[nodiscard]] lib::Array<std::size_t> areaPath() const;

    void enter(bool value) noexcept;
    [[nodiscard]] bool fireEnter() const noexcept { return enter_; }
    void escape(bool value) noexcept;
    [[nodiscard]] bool fireEscape() const noexcept { return escape_; }
    void lose(bool value) noexcept;
    [[nodiscard]] bool fireLose() const noexcept { return lose_; }
    void everyRound(bool value) noexcept;
    [[nodiscard]] bool fireEveryRound() const noexcept { return every_round_; }
    void roundEnd(bool value) noexcept;
    [[nodiscard]] bool fireRoundEnd() const noexcept { return round_end_; }
    void round0(bool value) noexcept;
    [[nodiscard]] bool fireRound0() const noexcept { return round0_; }
    [[nodiscard]] lib::SharedSlice<const std::uint32_t> rounds() const noexcept {
        return rounds_;
    }
    [[nodiscard]] lib::SharedSlice<std::uint32_t> rounds() noexcept {
        return rounds_;
    }
    void rounds(lib::SharedSlice<std::uint32_t> value);
    void rounds(lib::Array<std::uint32_t> value);
    void rounds(std::initializer_list<std::uint32_t> value) {
        rounds(lib::Array<std::uint32_t>(value));
    }
    bool addRound(std::uint32_t value);
    lib::Array<std::uint32_t> addRounds(const lib::Array<std::uint32_t>& value);
    [[nodiscard]] lib::Array<std::uint32_t> roundsFromNode(
        const EventTreeOwner& owner, const XNode& node,
        const system::XMLInfo* version = nullptr);
    void sortRounds();
    bool fireRound(std::uint32_t value) const;
    void removeRound(std::uint32_t value);
    void removeRoundsAll() noexcept;
    [[nodiscard]] const lib::Array<FKeyCode>& keyCodes() const noexcept { return key_codes_; }
    void keyCodes(lib::Array<FKeyCode> value);
    bool addKeyCode(FKeyCode value, std::ptrdiff_t index = -1);
    [[nodiscard]] lib::Array<FKeyCode> addKeyCodes(const lib::Array<FKeyCode>& values,
                                                 std::ptrdiff_t index = -1);
    [[nodiscard]] lib::Array<FKeyCode> keyCodesFromNode(const EventTreeOwner& owner,
        XNode& node, const system::XMLInfo& version, const system::System& sys,
        std::ptrdiff_t index = -1);
    void setKeyCode(std::size_t index, FKeyCode value);
    bool fireKeyCode(const FKeyCode& value) const;
    void removeKeyCode(const FKeyCode& value);
    void removeKeyCodesAll() noexcept;
    void swapKeyCode(std::size_t first, std::size_t second);
    [[nodiscard]] MatchingType keyCodeMatchingType() const noexcept { return key_code_matching_type_; }
    void keyCodeMatchingType(MatchingType value) noexcept { if (key_code_matching_type_ != value) { changed(); key_code_matching_type_ = value; } }

    [[nodiscard]] bool isEmpty() const noexcept;
    [[nodiscard]] const String& name() const;
    void name(String value);
    [[nodiscard]] String comment() const noexcept override { return comment_; }
    void comment(String value) override;
    void changeHandler(ChangeHandler value);
    [[nodiscard]] ChangeHandler changeHandler() const { return change_; }
    void changed() override;
    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] lib::Owner<CWXPath> findCWXPath(StringView value) override;
    [[nodiscard]] lib::Array<lib::Observer<CWXPath>> cwxChilds() override;
    [[nodiscard]] lib::Observer<CWXPath> cwxParent() override;

    [[nodiscard]] lib::Owner<UseCounter> useCounter() const { return use_counter_; }
    void setUseCounter(const lib::Owner<UseCounter>& value,
                       const lib::Owner<CWXPath>& owner = {});
    void removeUseCounter();
    [[nodiscard]] StartUseCounter startUseCounter() const noexcept { return start_use_counter_; }
    [[nodiscard]] String toXML() const;
    [[nodiscard]] XNode toNode(const card::XMLOption* option = nullptr) const;
    void toNode(XNode& parent, const card::XMLOption* option = nullptr) const;
    [[nodiscard]] static EventTreePtr createFromXML(
        StringView text, const system::XMLInfo* version = nullptr);
    [[nodiscard]] static EventTreePtr createFromNode(const XNode& node,
                                                       const system::XMLInfo* version = nullptr);

private:
    friend class Content;
    String object_id_;
    path::OwnerLink<EventTreeOwner> owner_;
    lib::SharedArray<ContentPtr> starts_;
    // 構築時のContentリンクは、このトークンが有効な間だけツリーを借用する。
    std::shared_ptr<path::OwnerLifetimeToken> tree_lifetime_{
        std::make_shared<path::OwnerLifetimeToken>()};
    void bindContentTree(const ContentPtr& value) noexcept;
    void validateStarts() const;
    void prepareStart(ContentPtr value);
    void detachUseCounter(bool finalizing);
    // Dはこの検索表を_startsと分けて管理する。同名なら検索表を上
    // 書きし、 重複した項目のいずれかを削除・改名すると古いキーを消す。
    std::map<String, ContentPtr, std::less<>> start_names_;
    void removeStartBindings(ContentPtr value);
    bool enter_{}, escape_{}, lose_{}, every_round_{}, round_end_{}, round0_{};
    lib::SharedArray<std::uint32_t> rounds_;
    lib::Array<FKeyCode> key_codes_;
    lib::Array<lib::Owner<usecounter::KeyCodeUser>> key_code_users_;
    // DのUserの所有者はキー配列から除いた後も残る。弱参照のUserを追跡
    // し、 ネイティブのデストラクターが最後に借用の所有者リンクを無効化する。
    lib::Array<lib::Observer<usecounter::KeyCodeUser>> all_key_code_users_;
    [[nodiscard]] lib::Owner<usecounter::KeyCodeUser> createKeyCodeUser();
    void bindKeyCodeUsers();
    void retireKeyCodeUsers() noexcept;
    MatchingType key_code_matching_type_{MatchingType::Or};
    lib::Owner<UseCounter> use_counter_;
    lib::Owner<CWXPath> uc_owner_;
    StartUseCounter start_use_counter_;
    String comment_;
    ChangeHandler change_;
};

struct EventCounterBinding {
    lib::Owner<UseCounter> counter;
    lib::Owner<CWXPath> owner;
};

/// カード・エリア・Summaryが使う所有者の契約。操作は一つの実装を共有する。
/// Dの所有者インターフェースはCard・Motion・変数の所有者とパスの同一
/// 性を共有するため、 C++の多重継承でも一つの仮想パス基底で同じ識別を保つ。
class EventTreeOwner : public virtual CWXPath {
public:
    ~EventTreeOwner() override = default;
    [[nodiscard]] virtual lib::SharedSlice<EventTreePtr> trees() = 0;
    [[nodiscard]] virtual lib::ConstOwnerSlice<EventTree> trees() const = 0;
    [[nodiscard]] virtual std::weak_ptr<path::OwnerLifetimeToken>
    eventTreeLifetimeToken() const noexcept = 0;
    [[nodiscard]] virtual bool canHasFireEnter() const noexcept = 0;
    [[nodiscard]] virtual bool canHasFireLose() const noexcept = 0;
    [[nodiscard]] virtual bool canHasFireEscape() const noexcept = 0;
    [[nodiscard]] virtual bool canHasFireEveryRound() const noexcept = 0;
    [[nodiscard]] virtual bool canHasFireRoundEnd() const noexcept = 0;
    [[nodiscard]] virtual bool canHasFireRound0() const noexcept = 0;
    [[nodiscard]] virtual bool canHasFireRound() const noexcept = 0;
    [[nodiscard]] virtual bool canHasFireKeyCode() const noexcept = 0;
    virtual void add(const EventTreePtr& value);
    virtual void insert(std::size_t index, const EventTreePtr& value);
    virtual void removeEvent(std::size_t index);
    virtual void remove(const EventTreePtr& value);
    virtual void swapEventTree(std::size_t first, std::size_t second);
    virtual void clearEvents();
    void addAll(const lib::Array<EventTreePtr>& values);
    void addAll(lib::SharedSlice<EventTreePtr> values);
    void addAll(std::initializer_list<EventTreePtr> values) {
        addAll(lib::SharedSlice<EventTreePtr>(values));
    }
    [[nodiscard]] virtual lib::Array<std::size_t> areaPath() const = 0;
    [[nodiscard]] virtual bool isEmpty() const = 0;
    [[nodiscard]] virtual String commentForEvents() const = 0;
    virtual void commentForEvents(String value) = 0;

    void prepareEventTree(const EventTreePtr& tree);
    static void detachEventTree(
        const EventTreePtr& tree, bool removeCounter,
        EventTreeOwner* expectedOwner = nullptr,
        std::weak_ptr<path::OwnerLifetimeToken> expectedLifetime = {});

protected:
    [[nodiscard]] virtual EventTreeOwner* con() noexcept { return this; }
    [[nodiscard]] virtual lib::SharedSlice<EventTreePtr>& eventTreesStorage() = 0;
    [[nodiscard]] static lib::SharedSlice<EventTreePtr>&
    eventTreesStorageOf(EventTreeOwner& owner) {
        return owner.eventTreesStorage();
    }
    // Dは発火条件の設定とコールバックが終わってから、所有者の関連付けを読む。
    [[nodiscard]] virtual EventCounterBinding eventCounterBinding() const { return {}; }
};

class AbstractEventTreeOwner : public EventTreeOwner,
                               public std::enable_shared_from_this<AbstractEventTreeOwner> {
public:
    explicit AbstractEventTreeOwner(CWXPath* connectedPath = nullptr);
    ~AbstractEventTreeOwner() override;
    AbstractEventTreeOwner(const AbstractEventTreeOwner&) = delete;
    AbstractEventTreeOwner& operator=(const AbstractEventTreeOwner&) = delete;
    AbstractEventTreeOwner(AbstractEventTreeOwner&&) = delete;
    AbstractEventTreeOwner& operator=(AbstractEventTreeOwner&&) = delete;
    [[nodiscard]] lib::Owner<CWXPath> retainForCallback() override;
    [[nodiscard]] lib::SharedSlice<EventTreePtr> trees() override { return trees_; }
    [[nodiscard]] lib::ConstOwnerSlice<EventTree> trees() const override {
        return lib::ConstOwnerSlice<EventTree>(trees_);
    }
    [[nodiscard]] std::weak_ptr<path::OwnerLifetimeToken>
    eventTreeLifetimeToken() const noexcept override { return event_lifetime_; }
    [[nodiscard]] bool canHasFireEnter() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireLose() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireEscape() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireEveryRound() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireRoundEnd() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireRound0() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireRound() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireKeyCode() const noexcept override { return true; }
    void add(const EventTreePtr& value) override;
    void insert(std::size_t index, const EventTreePtr& value) override;
    void removeEvent(std::size_t index) override;
    void remove(const EventTreePtr& value) override;
    void swapEventTree(std::size_t first, std::size_t second) override;
    void clearEvents() override;
    using EventTreeOwner::addAll;
    void addAll(const lib::Array<EventTreePtr>& values);
    void setUseCounter(const lib::Owner<UseCounter>& counter,
                       const lib::Owner<CWXPath>& owner);
    void removeUseCounter();
    [[nodiscard]] lib::Owner<UseCounter> useCounter() const { return use_counter_; }
    [[nodiscard]] lib::Owner<CWXPath> ucOwner() const { return uc_owner_; }
    [[nodiscard]] lib::Array<std::size_t> areaPath() const override { return {0U}; }
    [[nodiscard]] bool isEmpty() const override;
    [[nodiscard]] String commentForEvents() const override { return comment_; }
    void commentForEvents(String value) override {
        if (comment_ == value) return;
        [[maybe_unused]] const auto self_owner = weak_from_this().lock();
        [[maybe_unused]] const auto outer_owner = connected_path_ ? connected_path_->retainForCallback() : lib::Owner<CWXPath>{};
        changed();
        comment_ = std::move(value);
    }
    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] lib::Owner<CWXPath> findCWXPath(StringView value) override;
    [[nodiscard]] lib::Array<lib::Observer<CWXPath>> cwxChilds() override;
    [[nodiscard]] lib::Observer<CWXPath> cwxParent() override {
        return connected_path_ ? connected_path_->cwxParent()
                               : lib::Observer<CWXPath>{};
    }
    void changed() override {
        [[maybe_unused]] const auto keepAlive = retainForCallback();
        if (change_) change_();
    }
    void changeHandler(ChangeHandler value) { change_ = std::move(value); }
    [[nodiscard]] EventTreePtr eventTree(std::size_t index) const;
    [[nodiscard]] static lib::Array<EventTreePtr> loadEventsFromNode(
        const XNode& node, String& commentForEvents,
        const system::XMLInfo* version = nullptr);
    void appendEventsToNode(XNode& node, const card::XMLOption* option = nullptr) const;

protected:
    [[nodiscard]] EventTreeOwner* con() noexcept override;
    [[nodiscard]] lib::SharedSlice<EventTreePtr>& eventTreesStorage() override {
        return trees_;
    }
    [[nodiscard]] EventCounterBinding eventCounterBinding() const override {
        return {use_counter_, uc_owner_};
    }
    void deepCopyEventTreeOwner(const EventTreeOwner& source);

private:
    friend class ::cwxeditor::cwx::card::EffectCard;
    lib::SharedSlice<EventTreePtr> trees_;
    std::shared_ptr<path::OwnerLifetimeToken> event_lifetime_{
        std::make_shared<path::OwnerLifetimeToken>()};
    String comment_;
    ChangeHandler change_;
    CWXPath* connected_path_{};
    lib::Owner<UseCounter> use_counter_;
    lib::Owner<CWXPath> uc_owner_;
};

[[nodiscard]] ContentPtr createInitVariablesTree(const flag::FlagDir& directory);
[[nodiscard]] ContentPtr createInitVariablesTree(
    const lib::Array<lib::Owner<flag::Flag>>& flags,
    const lib::Array<lib::Owner<flag::Step>>& steps,
    const lib::Array<lib::Owner<flag::Variant>>& variants);

/// 変数初期値の編集補助用のXML保存処理。
struct ContentInitializer {
    inline static constexpr StringView XML_NAME = "contentInitializer";
    String dataVersion;
    lib::OrderedDictionary<CType, ContentPtr> initializer;
    [[nodiscard]] XNode toNode() const;
    void toNode(XNode& node) const;
    void fromNode(XNode& node);
    void fromNode(const XNode& node);

private:
    void toNodeImpl(XNode& node) const;
};

inline constexpr StringView ROUNDS_XML_NAME = "Number";
[[nodiscard]] lib::Array<std::uint32_t> roundsFromNode(
    const XNode& node, const system::XMLInfo* version = nullptr);
void roundsToNode(XNode& node, const lib::Array<std::uint32_t>& rounds);
[[nodiscard]] XNode roundsToNode(const lib::Array<std::uint32_t>& rounds);
[[nodiscard]] String roundsToXML(const lib::Array<std::uint32_t>& rounds);

}  // namespace cwxeditor::cwx::event
