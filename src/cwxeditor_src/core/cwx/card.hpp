#pragma once

// cwx/card.dのC++20移植。コレクショ
// ンはカードを共有所有し、親へのリンクは弱参照にす
// る。 パス・効果モーション・クーポン・参照数の
// 各モジュールと同じ方式で、所有権の循環を防ぐ。

#include "cwxeditor_src/core/cwx/coupon.hpp"
#include "cwxeditor_src/core/cwx/beast_owner.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/race.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/array_slice.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::props {
class CProps;
}
namespace cwxeditor::cwx::card {

using String = lib::String;
using StringView = lib::StringView;
using XNode = xml::XNode;
using XMLInfo = system::XMLInfo;
using System = system::System;
using CwxPath = path::CWXPath;
using Commentable = path::Commentable;
using UseCounter = usecounter::UseCounter;
using ChangeHandler = lib::Callback<void()>;
using CardImageType = types::CardImageType;
using CardImagePosition = types::CardImagePosition;
using Talker = types::Talker;
using ActionCardType = types::ActionCardType;
using Mentality = types::Mentality;
using EffectType = types::EffectType;
using Resist = types::Resist;
using CardVisual = types::CardVisual;
using Element = types::Element;
using DamageType = types::DamageType;
using Mental = types::Mental;
using Physical = types::Physical;
using Status = types::Status;
using Enhance = types::Enhance;
using Premium = types::Premium;
using CardTarget = types::CardTarget;
using ShowStyle = types::ShowStyle;

using SkillId = usecounter::SkillId;
using ItemId = usecounter::ItemId;
using BeastId = usecounter::BeastId;
using CastId = usecounter::CastId;
using InfoId = usecounter::InfoId;

class SkillCard;
class ItemCard;
class BeastCard;
class CastCard;
class InfoCard;
class KeyCodesUser;

/// カードの参照構造を保存形式へ変換するときの設定。
class XMLOption {
public:
    /// システムは借用する。D版と同様、独立したコピーではnullを許す。
    const System* sys{};
    String dataVersion = versioninfo::LATEST_VERSION;
    bool loadScaledImage{};
    bool includeCard{};
    bool noLinkId{};
    bool logicalSort{};
    lib::Callback<lib::Owner<const SkillCard>(std::uint64_t)> skill;
    lib::Callback<lib::Owner<const ItemCard>(std::uint64_t)> item;
    lib::Callback<lib::Owner<const BeastCard>(std::uint64_t)> beast;
    lib::Dictionary<std::uint64_t, std::uint32_t> nestCount;
    bool shallow{};
    bool saveSkinName{true};

    XMLOption(const System* system, String dataVersion);
    XMLOption(const System& system, String dataVersion)
        : XMLOption(&system, std::move(dataVersion)) {}
    [[nodiscard]] bool isTargetVersion(StringView ver) const;
};

struct OverData {
    std::uint64_t id{};
    std::uint64_t linkId{};
    bool overHold{};
    bool hold{};
};

/// カード画像のパス、PCの顔画像、または話者の仮指定。
class CardImage : public virtual CwxPath,
                  public std::enable_shared_from_this<CardImage> {
private:
    friend class event::Content;
    std::uint32_t pc_number_{};
    Talker talker_{Talker::Selected};
    lib::Owner<usecounter::PathUser> path_;
    CardImagePosition position_{CardImagePosition::Default};

    explicit CardImage(CwxPath* owner);
    [[nodiscard]] static XNode putToNode(
        XNode& parent, const lib::Owner<CardImage>& image, bool attr);

public:
    CardImageType type{CardImageType::File};
    CardImage(CwxPath* owner, const CardImage& base);
    CardImage(String path, CardImagePosition position =
                                      CardImagePosition::Default);
    explicit CardImage(std::uint32_t pcNumber);
    explicit CardImage(Talker talker);

    [[nodiscard]] lib::Owner<UseCounter> useCounter() const;
    void setUseCounter(const lib::Owner<UseCounter>& counter);
    void removeUseCounter();
    [[nodiscard]] String path() const;
    [[nodiscard]] CardImagePosition positionType() const noexcept {
        return position_;
    }
    [[nodiscard]] std::uint32_t pcNumber() const noexcept { return pc_number_; }
    [[nodiscard]] Talker talker() const noexcept { return talker_; }
    [[nodiscard]] bool opEquals(const CardImage& other) const;
    [[nodiscard]] bool operator==(const CardImage& other) const {
        return opEquals(other);
    }
    [[nodiscard]] std::size_t toHash() const;

    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] lib::Owner<CwxPath> findCWXPath(StringView value) override;
    [[nodiscard]] lib::Array<lib::Observer<CwxPath>> cwxChilds() override;
    [[nodiscard]] lib::Observer<CwxPath> cwxParent() override;
    void changed() override;

    static void setOnTag(XNode& node, lib::Array<lib::Owner<CardImage>>& paths,
                         bool attr);
    static void setOnTag(XNode& node, lib::Array<lib::Owner<CardImage>>& paths,
                         bool& isOverrideImage, bool attr);
    [[nodiscard]] static lib::Array<lib::Owner<CardImage>> createFromNode(
        XNode& node);
    [[nodiscard]] static XNode toNode(
        XNode& parent, const lib::Array<lib::Owner<CardImage>>& paths,
        bool attr = false, bool forceMulti = false);
    [[nodiscard]] static XNode toNode(
        const lib::Array<lib::Owner<CardImage>>& paths);
};

class CastOwner : public virtual CwxPath {
public:
    ~CastOwner() override = default;
    [[nodiscard]] virtual lib::SharedSlice<lib::Owner<CastCard>> casts() = 0;
    [[nodiscard]] virtual lib::ConstOwnerSlice<CastCard> casts() const = 0;
    [[nodiscard]] virtual lib::Owner<CastCard> cwCast(std::uint64_t id) = 0;
    [[nodiscard]] virtual lib::Owner<const CastCard> cwCast(std::uint64_t id) const = 0;
};
class SkillOwner : public virtual CwxPath {
public:
    ~SkillOwner() override = default;
    [[nodiscard]] virtual lib::SharedSlice<lib::Owner<SkillCard>> skills() = 0;
    [[nodiscard]] virtual lib::ConstOwnerSlice<SkillCard> skills() const = 0;
    [[nodiscard]] virtual lib::Owner<SkillCard> skill(std::uint64_t id) = 0;
    [[nodiscard]] virtual lib::Owner<const SkillCard> skill(std::uint64_t id) const = 0;
};
class ItemOwner : public virtual CwxPath {
public:
    ~ItemOwner() override = default;
    [[nodiscard]] virtual lib::SharedSlice<lib::Owner<ItemCard>> items() = 0;
    [[nodiscard]] virtual lib::ConstOwnerSlice<ItemCard> items() const = 0;
    [[nodiscard]] virtual lib::Owner<ItemCard> item(std::uint64_t id) = 0;
    [[nodiscard]] virtual lib::Owner<const ItemCard> item(std::uint64_t id) const = 0;
};
class InfoOwner : public virtual CwxPath {
public:
    ~InfoOwner() override = default;
    [[nodiscard]] virtual lib::SharedSlice<lib::Owner<InfoCard>> infos() = 0;
    [[nodiscard]] virtual lib::ConstOwnerSlice<InfoCard> infos() const = 0;
    [[nodiscard]] virtual lib::Owner<InfoCard> info(std::uint64_t id) = 0;
    [[nodiscard]] virtual lib::Owner<const InfoCard> info(std::uint64_t id) const = 0;
};

class CardException : public std::runtime_error {
public:
    explicit CardException(String message) : std::runtime_error(std::move(message)) {}
};

/// 独立した全カードに共通する状態と、XMLプロパティの処理。
class Card : public virtual CwxPath,
             public Commentable,
             public std::enable_shared_from_this<Card> {
private:
    std::uint64_t id_{};
    String object_id_;
    String name_;
    String desc_;
    // 呼び出し中は関数オブジェクトを保持する。Dのデリゲート
    // と同様に通知間で可変の クロージャー状態を保ち、コール
    // バックが自分を置き換えても実行中の対象を破棄しない。
    ChangeHandler change_;
    bool changed_{};
    lib::Array<lib::Owner<CardImage>> paths_;
    lib::Owner<UseCounter> use_counter_;
    lib::Observer<CwxPath> use_counter_owner_;
    String comment_;
    std::shared_ptr<path::OwnerLifetimeToken> card_lifetime_ =
        std::make_shared<path::OwnerLifetimeToken>();

protected:
    void invalidateCardLifetime() noexcept {
        card_lifetime_->alive.store(false, std::memory_order_release);
    }
    [[nodiscard]] std::weak_ptr<path::OwnerLifetimeToken> cardLifetime() const noexcept {
        return card_lifetime_;
    }
    void shallowCopyCard(const Card& other);
    [[nodiscard]] bool equalsExcludeIdCard(const Card& other) const;
    [[nodiscard]] bool baseEquals(const Card& other) const;
    virtual void clearData();
    [[nodiscard]] ChangeHandler changeHandler() const;
    [[nodiscard]] const lib::Array<lib::Owner<CardImage>>& pathStorage() const {
        return paths_;
    }
    [[nodiscard]] lib::Owner<CwxPath> useCounterOwner() const {
        return use_counter_owner_.lock();
    }
    [[nodiscard]] XNode setProp(XNode& node, const XMLOption* option,
                                const OverData* over = nullptr) const;
    void loadProp(XNode& node, XNode& property, const XMLInfo& version,
                  bool loadId = true);
    static std::uint64_t readLinkInfo(XNode& node, std::uint64_t& linkId,
                                      bool& hold, String& comment);

public:
    Card(std::uint64_t id, String name,
         const lib::Array<lib::Owner<CardImage>>& paths, String desc);
    ~Card() override { invalidateCardLifetime(); }
    // Dのカードは参照としての同一性を持つ。ネイティブではdup/shallowCopy
    // でコピーし、 寿命トークンを複製して別の有効なカードを無効化しないようにする。
    Card(const Card&) = delete;
    Card& operator=(const Card&) = delete;
    Card(Card&&) = delete;
    Card& operator=(Card&&) = delete;
    [[nodiscard]] lib::Owner<CwxPath> retainForCallback() override {
        return std::static_pointer_cast<CwxPath>(weak_from_this().lock());
    }
    [[nodiscard]] String objectId() const { return object_id_; }
    [[nodiscard]] virtual lib::Owner<Card> dup() const = 0;
    [[nodiscard]] bool opEquals(const Card& other) const;
    [[nodiscard]] bool operator==(const Card& other) const { return opEquals(other); }
    virtual void changeHandler(ChangeHandler change);
    [[nodiscard]] const ChangeHandler& changeHandlerCallback() const noexcept {
        return change_;
    }
    void changed() override;
    [[nodiscard]] bool isChanged() const noexcept { return changed_; }
    void resetChanged() noexcept { changed_ = false; }
    [[nodiscard]] lib::Owner<UseCounter> useCounter() const { return use_counter_; }
    virtual void setUseCounter(const lib::Owner<UseCounter>& counter,
                               const lib::Owner<CwxPath>& owner = {});
    virtual void removeUseCounter();
    void id(std::uint64_t value);
    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    void name(String value);
    [[nodiscard]] String name() const { return name_; }
    void paths(const lib::Array<lib::Owner<CardImage>>& value);
    [[nodiscard]] lib::Array<lib::Owner<CardImage>> paths() const;
    [[nodiscard]] String connectedFile() const;
    void desc(String value);
    [[nodiscard]] String desc() const { return desc_; }
    [[nodiscard]] String comment() const override { return comment_; }
    void comment(String value) override;
    [[nodiscard]] static std::uint64_t readId(XNode& node);
    [[nodiscard]] virtual XNode toNode(XNode& parent,
                                       const XMLOption& option) const = 0;
    [[nodiscard]] String cwxPath(bool) override { return {}; }
    [[nodiscard]] lib::Owner<CwxPath> findCWXPath(StringView value) override;
    [[nodiscard]] lib::Observer<CwxPath> cwxParent() override { return {}; }
    [[nodiscard]] int opCmp(const Card& other) const noexcept;
    [[nodiscard]] std::size_t toHash() const noexcept;
};

class CastCard : public Card,
                       public SkillOwner,
                       public ItemOwner,
                       public BeastOwner,
                       public coupon::CouponsOwner,
                       public race::RaceParam<true> {
private:
    std::uint32_t level_{};
    std::uint32_t life_{};
    std::uint32_t life_max_{};
    Mentality mentality_{Mentality::Normal};
    std::uint32_t mentality_round_{};
    std::uint32_t paralyze_{};
    std::uint32_t poison_{};
    std::uint32_t bind_round_{};
    std::uint32_t silence_round_{};
    std::uint32_t face_up_round_{};
    std::uint32_t anti_magic_round_{};
    std::int32_t enhance_[4]{};
    std::uint32_t enhance_round_[4]{};
    double level_coefficient_{1.0};
    std::uint32_t ep_per_level_{race::DEFAULT_EP_PER_LEVEL};
    lib::SharedSlice<lib::Owner<coupon::Coupon>> coupons_;
    lib::SharedSlice<lib::Owner<ItemCard>> items_;
    lib::SharedSlice<lib::Owner<SkillCard>> skills_;
    lib::SharedSlice<lib::Owner<BeastCard>> beasts_;
    path::OwnerLink<CastOwner> owner_;

    // RaceParamの設定関数は新しい値の保存前に通知する
    // 。 再利用可能なRaceParamミックスインをCard
    // へ依存させず、通知中は共有のCastCardを保持する。
    [[nodiscard]] std::shared_ptr<void> retainMutationReceiver() override {
        return Card::weak_from_this().lock();
    }
    [[nodiscard]] lib::Owner<CwxPath> ownerPath() const;
    [[nodiscard]] lib::Owner<coupon::CouponsOwner> couponOwner();
    template <typename T>
    void attachCard(const lib::Owner<T>& card, bool forceCounter = false);
    template <typename T>
    void detachCard(const lib::Owner<T>& card);
    template <typename T>
    [[nodiscard]] lib::Owner<T> findImpl(const lib::SharedSlice<lib::Owner<T>>& array,
                                       std::uint64_t id) const;
    template <typename T>
    [[nodiscard]] lib::Owner<T> addImpl(lib::SharedSlice<lib::Owner<T>>& array,
                                        lib::Owner<T> card, bool newCard);
    template <typename T>
    void removeAtImpl(lib::SharedSlice<lib::Owner<T>>& array, std::size_t index);
    template <typename T>
    void removeImpl(lib::SharedSlice<lib::Owner<T>>& array, std::uint64_t id);
    template <typename T>
    void removeImpl(lib::SharedSlice<lib::Owner<T>>& array, const lib::Owner<T>& card);
    template <typename T>
    [[nodiscard]] lib::Owner<T> insertImpl(lib::SharedSlice<lib::Owner<T>>& array,
                                           std::size_t index,
                                           lib::Owner<T> card, bool newCard);

public:
    inline static constexpr std::string_view XML_NAME = "CastCard";
    inline static constexpr std::string_view XML_NAME_M = "CastCards";
    using toID = CastId;
    CastCard(std::uint64_t id, String name,
             const lib::Array<lib::Owner<CardImage>>& imagePaths, String desc,
             std::uint32_t level, std::uint32_t lifeMax);
    CastCard(std::uint64_t id, String name,
             const lib::Array<lib::Owner<CardImage>>& imagePaths, String desc)
        : CastCard(id, std::move(name), imagePaths, std::move(desc), 1U, 1U) {}
    ~CastCard() override;
    using Card::changeHandler;
    void changeHandler(ChangeHandler change) override;
    void changed() override;
    void shallowCopy(const CastCard& other);
    void deepCopy(const CastCard& other);
    [[nodiscard]] bool opEquals(const CastCard& other) const;
    [[nodiscard]] bool operator==(const CastCard& other) const { return opEquals(other); }
    [[nodiscard]] bool equalsExcludeId(const CastCard& other) const;
    [[nodiscard]] lib::Owner<Card> dup() const override;
    [[nodiscard]] lib::Owner<CastCard> dupCast() const;
    void setUseCounter(const lib::Owner<UseCounter>& counter,
                       const lib::Owner<CwxPath>& owner = {}) override;
    void removeUseCounter() override;
    [[nodiscard]] std::uint32_t level() const noexcept { return level_; }
    void level(std::uint32_t value);
    [[nodiscard]] std::uint32_t life() const noexcept { return life_; }
    void life(std::uint32_t value);
    [[nodiscard]] std::uint32_t lifeMax() const noexcept { return life_max_; }
    void lifeMax(std::uint32_t value);
    [[nodiscard]] int aptitude(Physical physical, Mental mental) const;
    [[nodiscard]] double levelCoefficient() const noexcept { return level_coefficient_; }
    void levelCoefficient(double value);
    [[nodiscard]] std::uint32_t epPerLevel() const noexcept { return ep_per_level_; }
    void epPerLevel(std::uint32_t value);
    [[nodiscard]] lib::SharedSlice<lib::Owner<coupon::Coupon>> coupons() override {
        return coupons_;
    }
    [[nodiscard]] lib::ConstOwnerSlice<coupon::Coupon> coupons() const override {
        return lib::ConstOwnerSlice<coupon::Coupon>(coupons_);
    }
    void coupons(lib::SharedSlice<lib::Owner<coupon::Coupon>> value);
    void coupons(const lib::Array<lib::Owner<coupon::Coupon>>& value);
    void coupons(std::initializer_list<lib::Owner<coupon::Coupon>> value);
    [[nodiscard]] lib::SharedSlice<lib::Owner<ItemCard>> items() override { return items_; }
    [[nodiscard]] lib::ConstOwnerSlice<ItemCard> items() const override {
        return lib::ConstOwnerSlice<ItemCard>(items_);
    }
    [[nodiscard]] lib::Owner<ItemCard> item(std::uint64_t id) override;
    [[nodiscard]] lib::Owner<const ItemCard> item(std::uint64_t id) const override;
    lib::Owner<ItemCard> add(const lib::Owner<ItemCard>& card, bool newCard = false);
    void removeItem(std::uint64_t id);
    void remove(const lib::Owner<ItemCard>& card);
    lib::Owner<ItemCard> insert(std::size_t index, const lib::Owner<ItemCard>& card,
                                bool newCard = false);
    [[nodiscard]] lib::SharedSlice<lib::Owner<SkillCard>> skills() override { return skills_; }
    [[nodiscard]] lib::ConstOwnerSlice<SkillCard> skills() const override {
        return lib::ConstOwnerSlice<SkillCard>(skills_);
    }
    [[nodiscard]] lib::Owner<SkillCard> skill(std::uint64_t id) override;
    [[nodiscard]] lib::Owner<const SkillCard> skill(std::uint64_t id) const override;
    lib::Owner<SkillCard> add(const lib::Owner<SkillCard>& card, bool newCard = false);
    void removeSkill(std::uint64_t id);
    void remove(const lib::Owner<SkillCard>& card);
    lib::Owner<SkillCard> insert(std::size_t index, const lib::Owner<SkillCard>& card,
                                 bool newCard = false);
    [[nodiscard]] lib::SharedSlice<lib::Owner<BeastCard>> beasts() override { return beasts_; }
    [[nodiscard]] lib::ConstOwnerSlice<BeastCard> beasts() const override {
        return lib::ConstOwnerSlice<BeastCard>(beasts_);
    }
    [[nodiscard]] lib::Owner<BeastCard> beast(std::uint64_t id) override;
    [[nodiscard]] lib::Owner<const BeastCard> beast(std::uint64_t id) const override;
    lib::Owner<BeastCard> add(const lib::Owner<BeastCard>& card, bool newCard = false);
    void removeBeast(std::uint64_t id);
    void remove(const lib::Owner<BeastCard>& card);
    lib::Owner<BeastCard> insert(std::size_t index, const lib::Owner<BeastCard>& card,
                                 bool newCard = false);
    template <typename T>
    [[nodiscard]] std::ptrdiff_t indexOf(const lib::Owner<T>& card) const;
    template <typename T>
    void swap(std::size_t index1, std::size_t index2);
    void swapSkill(std::size_t a, std::size_t b) {
        [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
        swap<SkillCard>(a, b);
    }
    void swapItem(std::size_t a, std::size_t b) {
        [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
        swap<ItemCard>(a, b);
    }
    void swapBeast(std::size_t a, std::size_t b) {
        [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
        swap<BeastCard>(a, b);
    }
    [[nodiscard]] Mentality mentality() const noexcept { return mentality_; }
    void mentality(Mentality value);
    [[nodiscard]] std::uint32_t mentalityRound() const noexcept { return mentality_round_; }
    void mentalityRound(std::uint32_t value);
    [[nodiscard]] std::uint32_t paralyze() const noexcept { return paralyze_; }
    void paralyze(std::uint32_t value);
    [[nodiscard]] std::uint32_t poison() const noexcept { return poison_; }
    void poison(std::uint32_t value);
    [[nodiscard]] std::uint32_t bindRound() const noexcept { return bind_round_; }
    void bindRound(std::uint32_t value);
    [[nodiscard]] std::uint32_t silenceRound() const noexcept { return silence_round_; }
    void silenceRound(std::uint32_t value);
    [[nodiscard]] std::uint32_t faceUpRound() const noexcept { return face_up_round_; }
    void faceUpRound(std::uint32_t value);
    [[nodiscard]] std::uint32_t antiMagicRound() const noexcept { return anti_magic_round_; }
    void antiMagicRound(std::uint32_t value);
    [[nodiscard]] std::int32_t enhance(Enhance value) const;
    void enhance(Enhance value, std::int32_t amount);
    [[nodiscard]] std::uint32_t enhanceRound(Enhance value) const;
    void enhanceRound(Enhance value, std::uint32_t amount);
    [[nodiscard]] String toXML(const XMLOption& option) const;
    [[nodiscard]] XNode toNode(const XMLOption& option) const;
    [[nodiscard]] XNode toNode(XNode& parent, const XMLOption& option) const override;
    [[nodiscard]] static lib::Owner<CastCard> createFromNode(
        const props::CProps& prop, XNode& node, const XMLInfo& version);
    void owner(const lib::Owner<CastOwner>& owner);
    void owner(CastOwner* owner) noexcept { owner_.setRaw(owner); }
    void owner(CastOwner* owner, std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept {
        owner_.setBorrowed(owner, std::move(lifetime));
    }
    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] lib::Owner<CwxPath> findCWXPath(StringView value) override;
    [[nodiscard]] lib::Array<lib::Observer<CwxPath>> cwxChilds() override;
    [[nodiscard]] lib::Observer<CwxPath> cwxParent() override;
};

class EffectCard : public Card,
                   public event::EventTreeOwner,
                   public motion::MotionOwner,
                   public flag::LocalVariableOwner {
protected:
    [[nodiscard]] event::EventCounterBinding eventCounterBinding() const override {
        return {event_owner_.useCounter(), event_owner_.ucOwner()};
    }
    [[nodiscard]] lib::SharedSlice<event::EventTreePtr>&
    eventTreesStorage() override {
        return eventTreesStorageOf(event_owner_);
    }
    String scenario_;
    String author_;
    Physical physical_{Physical::Dex};
    Mental mental_{Mental::Aggressive};
    CardTarget target_{CardTarget::None};
    bool all_range_{};
    bool spell_{};
    EffectType effect_type_{EffectType::Physic};
    Resist resist_{Resist::Avoid};
    std::int32_t success_rate_{};
    CardVisual visual_{CardVisual::None};
    std::int32_t enhance_[4]{};
    lib::Owner<usecounter::PathUser> sound1_;
    lib::Owner<usecounter::PathUser> sound2_;
    std::uint32_t volume1_{100};
    std::uint32_t volume2_{100};
    std::uint32_t loop1_{1};
    std::uint32_t loop2_{1};
    lib::Owner<KeyCodesUser> key_codes_;
    String settings_comment_;
    String external_variant_scope_{"False"};
    lib::Owner<usecounter::VariantUser> external_variant_;
    bool external_variant_yado_{};
    Premium premium_{Premium::Normal};
    motion::MotionUser motion_user_;
    event::AbstractEventTreeOwner event_owner_;
    lib::Owner<CwxPath> event_use_counter_owner_;
    lib::Owner<flag::FlagDir> flag_dir_root_;

    void unbindEventTree(const lib::Owner<event::EventTree>& tree);

    [[nodiscard]] bool opEqualsEffect(const EffectCard& other) const;
    [[nodiscard]] bool equalsExcludeIdEffect(const EffectCard& other) const;
    void shallowCopyEffectCard(const EffectCard& other);
    void deepCopyEffectCard(const EffectCard& other);
    void clearData() override;
    [[nodiscard]] XNode setEffProp(XNode& node, const XMLOption* option,
                                   const OverData* over = nullptr) const;
    void loadEffProp(XNode& node, XNode& property, const XMLInfo& version,
                     bool loadId = true);
    void loadEffV(XNode& node, const XMLInfo& version);
    [[nodiscard]] bool effectEquals(const EffectCard& other) const;
    virtual void setUseCounterImpl(const lib::Owner<UseCounter>& counter) = 0;
    virtual void removeUseCounterImpl() = 0;

public:
    EffectCard(const System* system, std::uint64_t id, String name,
               const lib::Array<lib::Owner<CardImage>>& imagePaths, String desc);
    EffectCard(const System& system, std::uint64_t id, String name,
               const lib::Array<lib::Owner<CardImage>>& imagePaths, String desc)
        : EffectCard(&system, id, std::move(name), imagePaths, std::move(desc)) {}
    ~EffectCard() override;
    [[nodiscard]] virtual std::uint64_t linkId() const = 0;
    virtual void linkId(std::uint64_t value) = 0;
    [[nodiscard]] std::uint64_t id() const noexcept override { return Card::id(); }
    using Card::id;
    using Card::name;
    [[nodiscard]] String name() const override { return Card::name(); }
    [[nodiscard]] lib::Owner<UseCounter> useCounter() const override {
        return Card::useCounter();
    }
    [[nodiscard]] lib::Owner<flag::FlagDir> flagDirRoot() const override;
    [[nodiscard]] String scenario() const { return scenario_; }
    void scenario(String value);
    [[nodiscard]] String author() const { return author_; }
    void author(String value);
    [[nodiscard]] Physical physical() const noexcept { return physical_; }
    void physical(Physical value);
    [[nodiscard]] Mental mental() const noexcept { return mental_; }
    void mental(Mental value);
    [[nodiscard]] CardTarget target() const noexcept { return target_; }
    void target(CardTarget value);
    [[nodiscard]] bool allRange() const noexcept { return all_range_; }
    void allRange(bool value);
    [[nodiscard]] bool spell() const noexcept { return spell_; }
    void spell(bool value);
    [[nodiscard]] EffectType effectType() const noexcept { return effect_type_; }
    void effectType(EffectType value);
    [[nodiscard]] Resist resist() const noexcept { return resist_; }
    void resist(Resist value);
    [[nodiscard]] std::int32_t successRate() const noexcept { return success_rate_; }
    void successRate(std::int32_t value);
    [[nodiscard]] CardVisual visual() const noexcept { return visual_; }
    void visual(CardVisual value);
    [[nodiscard]] std::int32_t enhance(Enhance value) const;
    void enhance(Enhance value, std::int32_t amount);
    [[nodiscard]] String soundPath1() const;
    void soundPath1(String value);
    [[nodiscard]] String soundPath2() const;
    void soundPath2(String value);
    [[nodiscard]] std::uint32_t volume1() const noexcept { return volume1_; }
    void volume1(std::uint32_t value);
    [[nodiscard]] std::uint32_t volume2() const noexcept { return volume2_; }
    void volume2(std::uint32_t value);
    [[nodiscard]] std::uint32_t loopCount1() const noexcept { return loop1_; }
    void loopCount1(std::uint32_t value);
    [[nodiscard]] std::uint32_t loopCount2() const noexcept { return loop2_; }
    void loopCount2(std::uint32_t value);
    [[nodiscard]] lib::Array<String> keyCodes() const;
    void keyCodes(const lib::Array<String>& value);
    [[nodiscard]] String settingsComment() const { return settings_comment_; }
    void settingsComment(String value);
    [[nodiscard]] String externalVariantWriteScope() const { return external_variant_scope_; }
    void externalVariantWriteScope(String value);
    [[nodiscard]] String externalVariantWriteScenarioVar() const;
    void externalVariantWriteScenarioVar(String value);
    [[nodiscard]] bool externalVariantWriteYado() const noexcept { return external_variant_yado_; }
    void externalVariantWriteYado(bool value);
    [[nodiscard]] String valueWriteScenarioAttr() const;
    void valueWriteScenarioAttr(String value);
    void loadExternalVariantWriteAttr(String value);
    [[nodiscard]] Premium premium() const noexcept { return premium_; }
    void premium(Premium value);
    [[nodiscard]] lib::SharedSlice<lib::Owner<motion::Motion>> motions() override {
        return motion_user_.motions();
    }
    [[nodiscard]] lib::ConstOwnerSlice<motion::Motion> motions() const override {
        return motion_user_.motions();
    }
    void motions(lib::SharedSlice<lib::Owner<motion::Motion>> value);
    void motions(lib::Array<lib::Owner<motion::Motion>> value);
    void motions(std::initializer_list<lib::Owner<motion::Motion>> value);
    using Card::changeHandler;
    void changeHandler(ChangeHandler change) override;
    void changed() override;
    void setUseCounter(const lib::Owner<UseCounter>& counter,
                       const lib::Owner<CwxPath>& owner = {}) override;
    void removeUseCounter() override;
    [[nodiscard]] lib::SharedSlice<lib::Owner<event::EventTree>>
    trees() override { return event_owner_.trees(); }
    [[nodiscard]] lib::ConstOwnerSlice<event::EventTree>
    trees() const override { return event_owner_.trees(); }
    [[nodiscard]] std::weak_ptr<path::OwnerLifetimeToken>
    eventTreeLifetimeToken() const noexcept override { return cardLifetime(); }
    [[nodiscard]] bool canHasFireEnter() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireLose() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireEscape() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireEveryRound() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireRoundEnd() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireRound0() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireRound() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireKeyCode() const noexcept override { return true; }
    void add(const lib::Owner<event::EventTree>& tree) override;
    void insert(std::size_t index, const lib::Owner<event::EventTree>& tree) override;
    void removeEvent(std::size_t index) override;
    void remove(const lib::Owner<event::EventTree>& tree) override;
    void swapEventTree(std::size_t index1, std::size_t index2) override;
    void clearEvents() override { event_owner_.clearEvents(); }
    [[nodiscard]] std::vector<std::size_t> areaPath() const override { return {0U}; }
    [[nodiscard]] bool isEmpty() const override;
    [[nodiscard]] String commentForEvents() const override { return event_owner_.commentForEvents(); }
    void commentForEvents(String value) override;
    [[nodiscard]] lib::Owner<event::EventTree> etFromPath(const std::vector<std::size_t>& value) const;
    [[nodiscard]] String cwxPath(bool id) override { return Card::cwxPath(id); }
    [[nodiscard]] lib::Owner<CwxPath> findCWXPath(StringView value) override;
    [[nodiscard]] lib::Array<lib::Observer<CwxPath>> cwxChilds() override;
    [[nodiscard]] lib::Observer<CwxPath> cwxParent() override { return Card::cwxParent(); }
};

class SkillCard : public EffectCard {
private:
    lib::Owner<usecounter::SkillUser> link_user_;
    std::uint32_t level_{};
    bool hold_{};
    std::uint32_t price_{};
    std::int32_t use_limit_{};
    path::OwnerLink<SkillOwner> owner_;
    [[nodiscard]] lib::Owner<CwxPath> ownerPath() const;
    void toNodeImpl(XNode& node, const XMLOption& option, const OverData* over) const;

public:
    inline static constexpr std::string_view XML_NAME = "SkillCard";
    inline static constexpr std::string_view XML_NAME_M = "SkillCards";
    using toID = SkillId;
    SkillCard(const System* system, std::uint64_t id, String name,
              const lib::Array<lib::Owner<CardImage>>& imagePaths, String desc);
    SkillCard(const System& system, std::uint64_t id, String name,
              const lib::Array<lib::Owner<CardImage>>& imagePaths, String desc)
        : SkillCard(&system, id, std::move(name), imagePaths, std::move(desc)) {}
    ~SkillCard() override;
    void shallowCopy(const SkillCard& other);
    void deepCopy(const SkillCard& other);
    void clearData() override;
    [[nodiscard]] bool opEquals(const SkillCard& other) const;
    [[nodiscard]] bool operator==(const SkillCard& other) const { return opEquals(other); }
    [[nodiscard]] bool equalsExcludeId(const SkillCard& other) const;
    [[nodiscard]] std::uint64_t linkId() const override;
    void linkId(std::uint64_t value) override;
    void setUseCounterImpl(const lib::Owner<UseCounter>& counter) override;
    void removeUseCounterImpl() override;
    [[nodiscard]] std::uint32_t level() const noexcept { return level_; }
    void level(std::uint32_t value);
    [[nodiscard]] std::uint32_t useLimit() const noexcept { return static_cast<std::uint32_t>(use_limit_); }
    void useLimit(std::uint32_t value);
    [[nodiscard]] std::uint32_t price() const noexcept { return price_; }
    void price(std::uint32_t value);
    [[nodiscard]] bool hold() const noexcept { return hold_; }
    void hold(bool value);
    [[nodiscard]] lib::Owner<Card> dup() const override;
    [[nodiscard]] lib::Owner<SkillCard> dupSkill() const;
    [[nodiscard]] String toXML(const XMLOption& option) const;
    [[nodiscard]] XNode toNode(const XMLOption& option) const;
    [[nodiscard]] XNode toNode(XNode& parent, const XMLOption& option) const override;
    [[nodiscard]] XNode toNode(XNode& parent, const XMLOption& option, const OverData& over) const;
    [[nodiscard]] static lib::Owner<SkillCard> createFromNode(XNode& node, const XMLInfo& version);
    void owner(const lib::Owner<SkillOwner>& owner);
    void owner(SkillOwner* owner) noexcept { owner_.setRaw(owner); }
    void owner(SkillOwner* owner, std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept {
        owner_.setBorrowed(owner, std::move(lifetime));
    }
    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] lib::Observer<CwxPath> cwxParent() override;
    [[nodiscard]] lib::Owner<CwxPath> findCWXPath(StringView value) override;
};

class ItemCard : public EffectCard {
private:
    lib::Owner<usecounter::ItemUser> link_user_;
    std::int32_t owner_enhance_[3]{};
    std::uint32_t price_{};
    std::uint32_t use_limit_{};
    std::uint32_t use_limit_max_{};
    bool hold_{};
    path::OwnerLink<ItemOwner> owner_;
    [[nodiscard]] lib::Owner<CwxPath> ownerPath() const;
    void toNodeImpl(XNode& node, const XMLOption& option, const OverData* over) const;

public:
    inline static constexpr std::string_view XML_NAME = "ItemCard";
    inline static constexpr std::string_view XML_NAME_M = "ItemCards";
    using toID = ItemId;
    ItemCard(const System* system, std::uint64_t id, String name,
             const lib::Array<lib::Owner<CardImage>>& imagePaths, String desc);
    ItemCard(const System& system, std::uint64_t id, String name,
             const lib::Array<lib::Owner<CardImage>>& imagePaths, String desc)
        : ItemCard(&system, id, std::move(name), imagePaths, std::move(desc)) {}
    ~ItemCard() override;
    void shallowCopy(const ItemCard& other);
    void deepCopy(const ItemCard& other);
    void clearData() override;
    [[nodiscard]] bool opEquals(const ItemCard& other) const;
    [[nodiscard]] bool operator==(const ItemCard& other) const { return opEquals(other); }
    [[nodiscard]] bool equalsExcludeId(const ItemCard& other) const;
    [[nodiscard]] std::uint64_t linkId() const override;
    void linkId(std::uint64_t value) override;
    void setUseCounterImpl(const lib::Owner<UseCounter>& counter) override;
    void removeUseCounterImpl() override;
    [[nodiscard]] std::uint32_t useLimit() const noexcept { return use_limit_; }
    void useLimit(std::uint32_t value);
    [[nodiscard]] std::uint32_t useLimitMax() const noexcept { return use_limit_max_; }
    void useLimitMax(std::uint32_t value);
    [[nodiscard]] std::uint32_t price() const noexcept { return price_; }
    void price(std::uint32_t value);
    [[nodiscard]] std::int32_t enhanceOwner(Enhance value) const;
    void enhanceOwner(Enhance value, std::int32_t amount);
    [[nodiscard]] bool hold() const noexcept { return hold_; }
    void hold(bool value);
    [[nodiscard]] lib::Owner<Card> dup() const override;
    [[nodiscard]] lib::Owner<ItemCard> dupItem() const;
    [[nodiscard]] String toXML(const XMLOption& option) const;
    [[nodiscard]] XNode toNode(const XMLOption& option) const;
    [[nodiscard]] XNode toNode(XNode& parent, const XMLOption& option) const override;
    [[nodiscard]] XNode toNode(XNode& parent, const XMLOption& option, const OverData& over) const;
    [[nodiscard]] static lib::Owner<ItemCard> createFromNode(XNode& node, const XMLInfo& version);
    void owner(const lib::Owner<ItemOwner>& owner);
    void owner(ItemOwner* owner) noexcept { owner_.setRaw(owner); }
    void owner(ItemOwner* owner, std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept {
        owner_.setBorrowed(owner, std::move(lifetime));
    }
    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] lib::Observer<CwxPath> cwxParent() override;
    [[nodiscard]] lib::Owner<CwxPath> findCWXPath(StringView value) override;
};

class BeastCard : public EffectCard {
private:
    lib::Owner<usecounter::BeastUser> link_user_;
    std::uint32_t use_limit_{};
    std::uint32_t price_{};
    lib::SharedSlice<Status> invocation_condition_{Status::Alive};
    bool remove_with_unconscious_{true};
    ShowStyle show_style_{ShowStyle::Center};
    path::OwnerLink<BeastOwner> owner_;
    [[nodiscard]] lib::Owner<CwxPath> ownerPath() const;
    void toNodeImpl(XNode& node, const XMLOption& option, const OverData* over) const;
    void setInvocationCondition(lib::Array<Status> value);

public:
    inline static constexpr std::string_view XML_NAME = "BeastCard";
    inline static constexpr std::string_view XML_NAME_M = "BeastCards";
    using toID = BeastId;
    BeastCard(const System* system, std::uint64_t id, String name,
              const lib::Array<lib::Owner<CardImage>>& imagePaths, String desc);
    BeastCard(const System& system, std::uint64_t id, String name,
              const lib::Array<lib::Owner<CardImage>>& imagePaths, String desc)
        : BeastCard(&system, id, std::move(name), imagePaths, std::move(desc)) {}
    ~BeastCard() override;
    void shallowCopy(const BeastCard& other);
    void deepCopy(const BeastCard& other);
    void clearData() override;
    [[nodiscard]] bool opEquals(const BeastCard& other) const;
    [[nodiscard]] bool operator==(const BeastCard& other) const { return opEquals(other); }
    [[nodiscard]] bool equalsExcludeId(const BeastCard& other) const;
    [[nodiscard]] std::uint64_t linkId() const override;
    void linkId(std::uint64_t value) override;
    void setUseCounterImpl(const lib::Owner<UseCounter>& counter) override;
    void removeUseCounterImpl() override;
    [[nodiscard]] std::uint32_t useLimit() const noexcept { return use_limit_; }
    void useLimit(std::uint32_t value);
    [[nodiscard]] std::uint32_t price() const noexcept { return price_; }
    void price(std::uint32_t value);
    [[nodiscard]] bool isOption() const;
    [[nodiscard]] lib::SharedSlice<Status> invocationCondition() noexcept {
        return invocation_condition_;
    }
    [[nodiscard]] lib::SharedSlice<const Status> invocationCondition() const noexcept {
        return lib::SharedSlice<const Status>(invocation_condition_);
    }
    void invocationCondition(lib::SharedSlice<Status> value);
    void invocationCondition(lib::SharedSlice<const Status> value);
    void invocationCondition(const lib::Array<Status>& value);
    void invocationCondition(std::initializer_list<Status> value);
    [[nodiscard]] bool removeWithUnconscious() const noexcept { return remove_with_unconscious_; }
    void removeWithUnconscious(bool value);
    [[nodiscard]] ShowStyle showStyle() const noexcept { return show_style_; }
    void showStyle(ShowStyle value);
    [[nodiscard]] lib::Owner<Card> dup() const override;
    [[nodiscard]] lib::Owner<BeastCard> dupBeast() const;
    [[nodiscard]] String toXML(const XMLOption& option) const;
    [[nodiscard]] String toXML(const XMLOption& option, const OverData& over) const;
    [[nodiscard]] XNode toNode(const XMLOption& option) const;
    [[nodiscard]] XNode toNode(XNode& parent, const XMLOption& option) const override;
    [[nodiscard]] XNode toNode(XNode& parent, const XMLOption& option, const OverData& over) const;
    [[nodiscard]] static lib::Owner<BeastCard> createFromNode(XNode& node, const XMLInfo& version);
    void owner(const lib::Owner<BeastOwner>& owner);
    void owner(BeastOwner* owner) noexcept;
    void owner(BeastOwner* owner, std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept {
        owner_.setBorrowed(owner, std::move(lifetime));
    }
    void owner(motion::Motion* owner) noexcept;
    void owner(CwxPath* owner) noexcept;
    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] lib::Observer<CwxPath> cwxParent() override;
    [[nodiscard]] lib::Owner<CwxPath> findCWXPath(StringView value) override;
};

class InfoCard : public Card {
private:
    path::OwnerLink<InfoOwner> owner_;
public:
    inline static constexpr std::string_view XML_NAME = "InfoCard";
    inline static constexpr std::string_view XML_NAME_M = "InfoCards";
    using toID = InfoId;
    InfoCard(std::uint64_t id, String name,
             const lib::Array<lib::Owner<CardImage>>& imagePaths, String desc)
        : Card(id, std::move(name), imagePaths, std::move(desc)) {}
    void shallowCopy(const InfoCard& other) {
        [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
        [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
        shallowCopyCard(other);
    }
    void deepCopy(const InfoCard& other) {
        [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
        [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
        shallowCopyCard(other);
    }
    [[nodiscard]] bool opEquals(const InfoCard& other) const { return Card::opEquals(other); }
    [[nodiscard]] bool equalsExcludeId(const InfoCard& other) const { return equalsExcludeIdCard(other); }
    [[nodiscard]] lib::Owner<Card> dup() const override;
    [[nodiscard]] lib::Owner<InfoCard> dupInfo() const;
    [[nodiscard]] String toXML(const XMLOption& option) const;
    [[nodiscard]] XNode toNode(const XMLOption& option) const;
    [[nodiscard]] XNode toNode(XNode& parent, const XMLOption& option) const override;
    [[nodiscard]] static lib::Owner<InfoCard> createFromNode(XNode& node, const XMLInfo& version);
    void owner(const lib::Owner<InfoOwner>& owner);
    void owner(InfoOwner* owner) noexcept { owner_.setRaw(owner); }
    void owner(InfoOwner* owner, std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept {
        owner_.setBorrowed(owner, std::move(lifetime));
    }
    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] lib::Owner<CwxPath> findCWXPath(StringView value) override;
    [[nodiscard]] lib::Array<lib::Observer<CwxPath>> cwxChilds() override { return {}; }
    [[nodiscard]] lib::Observer<CwxPath> cwxParent() override;
};

class ActionCard : public EffectCard {
private:
    ActionCardType action_type_{ActionCardType::Exchange};
    void setUseCounterImpl(const lib::Owner<UseCounter>&) override;
    void removeUseCounterImpl() override;
public:
    inline static constexpr std::string_view XML_NAME = "ActionCard";
    ActionCard(String name, const lib::Array<lib::Owner<CardImage>>& imagePaths, String desc);
    ~ActionCard() override;
    [[nodiscard]] ActionCardType actionCardType() const noexcept { return action_type_; }
    void actionCardType(ActionCardType value) {
        [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
        action_type_ = value;
    }
    void shallowCopy(const ActionCard& other);
    void deepCopy(const ActionCard& other);
    [[nodiscard]] bool opEquals(const ActionCard& other) const;
    [[nodiscard]] bool equalsExcludeId(const ActionCard& other) const;
    [[nodiscard]] std::uint64_t linkId() const override;
    void linkId(std::uint64_t) override;
    [[nodiscard]] lib::Owner<Card> dup() const override;
    [[nodiscard]] lib::Owner<ActionCard> dupAction() const;
    [[nodiscard]] XNode toNode(XNode& parent, const XMLOption& option) const override;
    [[nodiscard]] static lib::Owner<ActionCard> createFromNode(XNode& node, const XMLInfo& version);
    [[nodiscard]] String cwxPath(bool) override;
    [[nodiscard]] lib::Owner<CwxPath> findCWXPath(StringView) override;
    [[nodiscard]] lib::Observer<CwxPath> cwxParent() override;
};

inline constexpr std::string_view KEY_CODES_XML_NAME = "KeyCodes";
[[nodiscard]] lib::Array<String> keyCodesFromNode(XNode& node, const XMLInfo& version);
void keyCodesToNode(XNode& node, const lib::Array<String>& keyCodes);
[[nodiscard]] XNode keyCodesToNode(const lib::Array<String>& keyCodes);
[[nodiscard]] String keyCodesToXML(const lib::Array<String>& keyCodes);

class KeyCodesUser {
private:
    lib::Array<lib::Owner<usecounter::KeyCodeUser>> key_codes_;
    ChangeHandler change_;
    lib::Owner<UseCounter> use_counter_;
    lib::Observer<CwxPath> use_counter_owner_;
    lib::Owner<CwxPath> cwx_path_;
    void changed();
public:
    explicit KeyCodesUser(CwxPath* owner);
    explicit KeyCodesUser(const lib::Owner<CwxPath>& owner);
    [[nodiscard]] String cwxPath(bool id) const;
    void changeHandler(ChangeHandler change) { change_ = std::move(change); }
    [[nodiscard]] lib::Owner<UseCounter> useCounter() const { return use_counter_; }
    void setUseCounter(const lib::Owner<UseCounter>& counter,
                       const lib::Owner<CwxPath>& owner = {});
    void removeUseCounter();
    void keyCodes(const lib::Array<String>& values);
    [[nodiscard]] lib::Array<String> keyCodes() const;
};

template <typename T>
std::ptrdiff_t CastCard::indexOf(const lib::Owner<T>& card) const {
    if constexpr (std::is_same_v<T, SkillCard>) {
        for (std::size_t i = 0; i < skills_.size(); ++i)
            if (skills_[i].get() == card.get()) return static_cast<std::ptrdiff_t>(i);
    } else if constexpr (std::is_same_v<T, ItemCard>) {
        for (std::size_t i = 0; i < items_.size(); ++i)
            if (items_[i].get() == card.get()) return static_cast<std::ptrdiff_t>(i);
    } else if constexpr (std::is_same_v<T, BeastCard>) {
        for (std::size_t i = 0; i < beasts_.size(); ++i)
            if (beasts_[i].get() == card.get()) return static_cast<std::ptrdiff_t>(i);
    }
    return -1;
}

template <typename T>
void CastCard::swap(const std::size_t index1, const std::size_t index2) {
    lib::SharedSlice<lib::Owner<T>>* array = nullptr;
    if constexpr (std::is_same_v<T, SkillCard>) array = &skills_;
    else if constexpr (std::is_same_v<T, ItemCard>) array = &items_;
    else if constexpr (std::is_same_v<T, BeastCard>) array = &beasts_;
    if (!array) return;
    if (index1 == index2) return;
    if (index1 >= array->size() || index2 >= array->size())
        throw std::out_of_range("CastCard::swap index");
    [[maybe_unused]] const auto keepAlive = Card::weak_from_this().lock();
    changed();
    const auto id1 = (*array)[index1]->id();
    const auto id2 = (*array)[index2]->id();
    std::swap((*array)[index1], (*array)[index2]);
    (*array)[index1]->id(id1);
    (*array)[index2]->id(id2);
}

}  // namespace cwxeditor::cwx::card
