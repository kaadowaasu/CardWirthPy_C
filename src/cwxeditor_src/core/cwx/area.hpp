#pragma once

// cwx/area.dのC++20
// 移植。 イベントとカードの所有
// ハンドルは前方宣言で参照し、
// インクルードの循環を避ける。

#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/textholder.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/array_slice.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::card {
class CardImage;
class CastOwner;
class XMLOption;
}  // namespace cwxeditor::cwx::card

namespace cwxeditor::cwx::event {
class EventTree;
}  // namespace cwxeditor::cwx::event

namespace cwxeditor::cwx::area {

using ::cwxeditor::cwx::background::BgImage;
using ::cwxeditor::cwx::background::BgImageArray;
using ::cwxeditor::cwx::background::BgImageOwner;
using ::cwxeditor::cwx::background::BgImagePtr;
using ::cwxeditor::cwx::event::EventCounterBinding;
using ::cwxeditor::cwx::path::Commentable;
using ::cwxeditor::cwx::path::CWXPath;
using ::cwxeditor::cwx::system::XMLInfo;
using ::cwxeditor::cwx::textholder::SimpleTextHolder;
using ::cwxeditor::cwx::textholder::TextHolderType;
using ::cwxeditor::cwx::types::ActionCardType;
using ::cwxeditor::cwx::types::Transition;
using ::cwxeditor::cwx::usecounter::CastUser;
using ::cwxeditor::cwx::usecounter::CardGroupUser;
using ::cwxeditor::cwx::usecounter::CounterOwner;
using ::cwxeditor::cwx::usecounter::FlagUser;
using ::cwxeditor::cwx::usecounter::PathUser;
using ::cwxeditor::cwx::usecounter::UseCounter;
using XNode = ::cwxeditor::cwx::xml::XNode;
using XMLOption = ::cwxeditor::cwx::card::XMLOption;

using CardImage = ::cwxeditor::cwx::card::CardImage;
using CardImagePtr = ::cwxeditor::lib::Owner<CardImage>;
using CardImageArray = ::cwxeditor::lib::Array<CardImagePtr>;
using EventTree = ::cwxeditor::cwx::event::EventTree;
using EventTreePtr = ::cwxeditor::lib::Owner<EventTree>;
using EventTreeArray = ::cwxeditor::lib::Array<EventTreePtr>;

class Area;
class Battle;
class Package;
class AbstractArea;
class AbstractSpCard;
class MenuCard;
class EnemyCard;
class PlayerCardEvents;

using AreaPtr = ::cwxeditor::lib::Owner<Area>;
using BattlePtr = ::cwxeditor::lib::Owner<Battle>;
using PackagePtr = ::cwxeditor::lib::Owner<Package>;
using AbstractAreaPtr = ::cwxeditor::lib::Owner<AbstractArea>;
using AbstractSpCardPtr = ::cwxeditor::lib::Owner<AbstractSpCard>;
using MenuCardPtr = ::cwxeditor::lib::Owner<MenuCard>;
using EnemyCardPtr = ::cwxeditor::lib::Owner<EnemyCard>;
using AbstractAreaArray = ::cwxeditor::lib::Array<AbstractAreaPtr>;
using AreaArray = ::cwxeditor::lib::Array<AreaPtr>;
using BattleArray = ::cwxeditor::lib::Array<BattlePtr>;
using PackageArray = ::cwxeditor::lib::Array<PackagePtr>;
using MenuCardArray = ::cwxeditor::lib::Array<MenuCardPtr>;
using EnemyCardArray = ::cwxeditor::lib::Array<EnemyCardPtr>;
using ActionMap = std::map<ActionCardType, bool>;
using ChangeHandler = ::cwxeditor::lib::Callback<void()>;

class AreaException final : public std::runtime_error {
public:
    explicit AreaException(::cwxeditor::lib::String message)
        : std::runtime_error(std::move(message)) {}
};

class AreaOwner : public virtual CWXPath {
public:
    ~AreaOwner() override = default;
    [[nodiscard]] virtual ::cwxeditor::lib::SharedSlice<AreaPtr> areas() = 0;
    [[nodiscard]] virtual ::cwxeditor::lib::SharedSlice<const AreaPtr> areas() const = 0;
};

class BattleOwner : public virtual CWXPath {
public:
    ~BattleOwner() override = default;
    [[nodiscard]] virtual ::cwxeditor::lib::SharedSlice<BattlePtr> battles() = 0;
    [[nodiscard]] virtual ::cwxeditor::lib::SharedSlice<const BattlePtr> battles() const = 0;
};

class PackageOwner : public virtual CWXPath {
public:
    ~PackageOwner() override = default;
    [[nodiscard]] virtual ::cwxeditor::lib::SharedSlice<PackagePtr> packages() = 0;
    [[nodiscard]] virtual ::cwxeditor::lib::SharedSlice<const PackagePtr> packages() const = 0;
};

[[nodiscard]] AbstractAreaArray createAreasFromNode(
    XNode& node, std::string_view scenario_path, bool& same_summary,
    bool& from_table, const XMLInfo& version);

class AbstractSpCard : public ::cwxeditor::cwx::event::EventTreeOwner,
                       public Commentable,
                       public std::enable_shared_from_this<AbstractSpCard> {
public:
    AbstractSpCard(std::string_view flag, std::int32_t x, std::int32_t y,
                   std::uint32_t scale, std::int32_t layer,
                   std::string_view card_group, std::int32_t animation_speed);
    ~AbstractSpCard() override;
    AbstractSpCard(const AbstractSpCard&) = delete;
    AbstractSpCard& operator=(const AbstractSpCard&) = delete;
    AbstractSpCard(AbstractSpCard&&) = delete;
    AbstractSpCard& operator=(AbstractSpCard&&) = delete;
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> retainForCallback() override;
    [[nodiscard]] std::weak_ptr<path::OwnerLifetimeToken> lifetimeToken() const noexcept {
        return lifetime_;
    }

    [[nodiscard]] const std::string& objectId() const noexcept { return obj_id_; }
    [[nodiscard]] virtual AbstractArea* abstractOwner() noexcept = 0;
    [[nodiscard]] virtual const AbstractArea* abstractOwner() const noexcept = 0;
    [[nodiscard]] virtual AbstractSpCardPtr dup() const = 0;
    [[nodiscard]] virtual AbstractSpCardPtr shallowCopy() const = 0;
    virtual void shallowCopyFrom(const AbstractSpCard& copy_base);
    [[nodiscard]] virtual bool canHasFireLose() const noexcept { return false; }
    [[nodiscard]] virtual bool canHasFireEscape() const noexcept { return false; }
    [[nodiscard]] virtual bool canHasFireEveryRound() const noexcept { return false; }
    [[nodiscard]] virtual bool canHasFireRoundEnd() const noexcept { return false; }
    [[nodiscard]] virtual bool canHasFireRound0() const noexcept { return false; }
    [[nodiscard]] virtual bool canHasFireRound() const noexcept { return false; }
    [[nodiscard]] virtual bool canHasFireKeyCode() const noexcept { return true; }
    [[nodiscard]] virtual bool canHasFireEnter() const noexcept { return true; }
    [[nodiscard]] virtual std::vector<std::size_t> areaPath() const { return {}; }

    [[nodiscard]] ::cwxeditor::lib::SharedSlice<EventTreePtr>
    trees() override { return events_; }
    [[nodiscard]] ::cwxeditor::lib::ConstOwnerSlice<EventTree>
    trees() const override {
        return ::cwxeditor::lib::ConstOwnerSlice<EventTree>(events_);
    }
    void add(const EventTreePtr& event) override;
    using ::cwxeditor::cwx::event::EventTreeOwner::addAll;
    void addAll(const EventTreeArray& events);
    void insert(std::size_t index, const EventTreePtr& event) override;
    void removeEvent(std::size_t index) override;
    void remove(const EventTreePtr& event) override;
    void swapEventTree(std::size_t first, std::size_t second) override;
    void clearEvents() override;
    [[nodiscard]] bool isEmpty() const override;
    [[nodiscard]] std::string commentForEvents() const override { return events_comment_; }
    void commentForEvents(std::string value) override;
    [[nodiscard]] EventTreePtr etFromPath(const std::vector<std::size_t>& path) const;

    void flag(std::string value);
    [[nodiscard]] std::string flag() const { return user_.flag(); }
    [[nodiscard]] std::int32_t x() const noexcept { return x_; }
    void x(std::int32_t value);
    [[nodiscard]] std::int32_t y() const noexcept { return y_; }
    void y(std::int32_t value);
    [[nodiscard]] std::uint32_t scale() const noexcept { return scale_; }
    void scale(std::uint32_t value);
    [[nodiscard]] std::int32_t layer() const noexcept { return layer_; }
    void layer(std::int32_t value);
    void cardGroup(std::string value);
    [[nodiscard]] std::string cardGroup() const { return card_group_.cardGroup(); }
    [[nodiscard]] std::int32_t animationSpeed() const noexcept { return animation_speed_; }
    void animationSpeed(std::int32_t value);
    [[nodiscard]] std::string comment() const noexcept override { return comment_; }
    void comment(std::string value) override;
    [[nodiscard]] virtual CWXPath* connectedResource(card::CastOwner* summary) noexcept;
    [[nodiscard]] std::string connectedFile() const { return {}; }
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> findCWXPath(std::string_view path) override;
    [[nodiscard]] ::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> cwxChilds() override;
    [[nodiscard]] ::cwxeditor::lib::Observer<CWXPath> cwxParent() override;

    void setUseCounter(const CounterOwner& counter, CWXPath* counter_owner = nullptr) noexcept;
    void removeUseCounter() noexcept;
    [[nodiscard]] CounterOwner useCounter() const noexcept { return use_counter_; }
    [[nodiscard]] CWXPath* ucOwner() const noexcept { return uc_owner_; }
    virtual void changeHandler(ChangeHandler handler);
    [[nodiscard]] ChangeHandler changeHandler() const { return change_; }
    void changed() override;
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> ownerHandle() const;

protected:
    [[nodiscard]] ::cwxeditor::lib::SharedSlice<EventTreePtr>&
    eventTreesStorage() noexcept override { return events_; }
    [[nodiscard]] std::weak_ptr<path::OwnerLifetimeToken>
    eventTreeLifetimeToken() const noexcept override { return lifetime_; }
    void invalidateLifetime() noexcept {
        lifetime_->alive.store(false, std::memory_order_release);
    }
    [[nodiscard]] EventCounterBinding eventCounterBinding() const override;
    void appendProp(XNode& node, XNode& property, const XMLOption& option) const;
    static void loadProp(XNode& property, std::string& flag, std::int32_t& x,
                         std::int32_t& y, std::uint32_t& scale,
                         std::int32_t& layer, std::string& card_group,
                         std::int32_t& animation_speed);
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> selfOwner() const;

    std::string obj_id_;
    std::int32_t x_{};
    std::int32_t y_{};
    std::uint32_t scale_{100};
    std::int32_t layer_{::cwxeditor::cwx::types::LAYER_MENU_CARD};
    FlagUser user_;
    CardGroupUser card_group_;
    std::int32_t animation_speed_{-1};
    std::string comment_;
    ChangeHandler change_;
    CounterOwner use_counter_;
    CWXPath* uc_owner_{};
    std::shared_ptr<path::OwnerLifetimeToken> lifetime_{
        std::make_shared<path::OwnerLifetimeToken>()};
    ::cwxeditor::lib::SharedSlice<EventTreePtr> events_;
    std::string events_comment_;
};

class EnemyCard final : public AbstractSpCard {
public:
    inline static constexpr std::string_view XML_NAME = "EnemyCard";
    inline static constexpr std::string_view XML_NAME_M = "EnemyCards";
    EnemyCard(std::uint64_t id, const ActionMap& actions, std::string_view flag,
              std::int32_t x, std::int32_t y, std::uint32_t scale,
              std::int32_t layer, std::string_view card_group,
              std::int32_t animation_speed, bool override_name,
              std::string_view name, bool override_image,
              const CardImageArray& images);
    ~EnemyCard() override;
    [[nodiscard]] std::string cwxPath(bool id) override;
    [[nodiscard]] ::cwxeditor::lib::Observer<CWXPath> cwxParent() override;
    [[nodiscard]] AbstractSpCardPtr dup() const override;
    [[nodiscard]] AbstractSpCardPtr shallowCopy() const override;
    void shallowCopyFrom(const AbstractSpCard& copy_base) override;
    [[nodiscard]] std::vector<std::size_t> areaPath() const;
    [[nodiscard]] AbstractArea* abstractOwner() noexcept override;
    [[nodiscard]] const AbstractArea* abstractOwner() const noexcept override;
    [[nodiscard]] Battle* owner() noexcept { return owner_.lock().get(); }
    [[nodiscard]] const Battle* owner() const noexcept { return owner_.lock().get(); }
    [[nodiscard]] const ActionMap& actions() const noexcept { return actions_; }
    void actions(ActionMap value);
    [[nodiscard]] bool action(ActionCardType type) const;
    void action(ActionCardType type, bool value);
    [[nodiscard]] std::uint64_t id() const noexcept { return user_.casts(); }
    void id(std::uint64_t value);
    [[nodiscard]] bool isOverrideName() const noexcept { return override_name_; }
    void isOverrideName(bool value);
    [[nodiscard]] std::string overrideName() const;
    void overrideName(std::string_view value);
    [[nodiscard]] std::vector<std::string> flagsInText() const;
    [[nodiscard]] std::vector<std::string> stepsInText() const;
    [[nodiscard]] std::vector<std::string> variantsInText() const;
    [[nodiscard]] bool isOverrideImage() const noexcept { return override_image_; }
    void isOverrideImage(bool value);
    [[nodiscard]] CardImageArray overrideImages() const;
    void overrideImages(const CardImageArray& images);
    [[nodiscard]] std::string connectedFile() const;
    [[nodiscard]] CWXPath* connectedResource(card::CastOwner* summary) noexcept override;
    void setUseCounter(const CounterOwner& counter, CWXPath* counter_owner = nullptr) noexcept;
    void removeUseCounter() noexcept;
    [[nodiscard]] static EnemyCardArray createCardsFromNode(XNode& node,
                                                              const XMLInfo& version);
    [[nodiscard]] XNode toNode(const XMLOption& option) const;
    XNode toNode(XNode& parent, const XMLOption& option) const;
    [[nodiscard]] static EnemyCardPtr createFromNode(XNode& node,
                                                       const XMLInfo& version);

private:
    void attachOwner(Battle* owner);
    void toNodeImpl(XNode& element, const XMLOption& option) const;
    path::OwnerLink<Battle> owner_;
    Battle* owner_raw_{};
    ActionMap actions_;
    CastUser user_;
    bool override_name_{};
    std::unique_ptr<SimpleTextHolder> override_name_text_;
    bool override_image_{};
    CardImageArray override_images_;
    friend class Battle;
};

class MenuCard final : public AbstractSpCard {
public:
    inline static constexpr std::string_view XML_NAME = "MenuCard";
    inline static constexpr std::string_view XML_NAME_M = "MenuCards";
    MenuCard(std::string_view name, bool expand_special_chars,
             const CardImageArray& images, std::string_view description,
             std::string_view flag, std::int32_t x, std::int32_t y,
             std::uint32_t scale, std::int32_t layer,
             std::string_view card_group, std::int32_t animation_speed);
    ~MenuCard() override;
    [[nodiscard]] std::string cwxPath(bool id) override;
    [[nodiscard]] ::cwxeditor::lib::Observer<CWXPath> cwxParent() override;
    [[nodiscard]] AbstractSpCardPtr dup() const override;
    [[nodiscard]] AbstractSpCardPtr shallowCopy() const override;
    void shallowCopyFrom(const AbstractSpCard& copy_base) override;
    [[nodiscard]] std::vector<std::size_t> areaPath() const;
    [[nodiscard]] AbstractArea* abstractOwner() noexcept override;
    [[nodiscard]] const AbstractArea* abstractOwner() const noexcept override;
    [[nodiscard]] Area* owner() noexcept { return owner_.lock().get(); }
    [[nodiscard]] const Area* owner() const noexcept { return owner_.lock().get(); }
    [[nodiscard]] std::string name() const;
    void name(std::string_view value);
    [[nodiscard]] bool expandSPChars() const noexcept { return expand_special_chars_; }
    void expandSPChars(bool value);
    [[nodiscard]] std::vector<std::string> flagsInText() const;
    [[nodiscard]] std::vector<std::string> stepsInText() const;
    [[nodiscard]] std::vector<std::string> variantsInText() const;
    [[nodiscard]] const std::string& desc() const noexcept { return description_; }
    void desc(std::string value);
    [[nodiscard]] CardImageArray paths() const;
    void paths(const CardImageArray& value);
    [[nodiscard]] std::string connectedFile() const;
    void setUseCounter(const CounterOwner& counter, CWXPath* counter_owner = nullptr) noexcept;
    void removeUseCounter() noexcept;
    [[nodiscard]] static MenuCardArray createFromCardNode(XNode& node,
                                                            bool copy_description,
                                                            const XMLInfo& version);
    [[nodiscard]] XNode toNode(const XMLOption& option) const;
    XNode toNode(XNode& parent, const XMLOption& option) const;
    [[nodiscard]] static MenuCardPtr createFromNode(XNode& node,
                                                     const XMLInfo& version);

private:
    void attachOwner(Area* owner);
    void toNodeImpl(XNode& element, const XMLOption& option) const;
    path::OwnerLink<Area> owner_;
    Area* owner_raw_{};
    std::unique_ptr<SimpleTextHolder> name_text_;
    std::string description_;
    CardImageArray paths_;
    bool expand_special_chars_{};
    friend class Area;
};

class AbstractArea : public ::cwxeditor::cwx::event::EventTreeOwner,
                      public Commentable,
                     public std::enable_shared_from_this<AbstractArea> {
public:
    AbstractArea(std::uint64_t id, std::string_view name);
    ~AbstractArea() override;
    AbstractArea(const AbstractArea&) = delete;
    AbstractArea& operator=(const AbstractArea&) = delete;
    AbstractArea(AbstractArea&&) = delete;
    AbstractArea& operator=(AbstractArea&&) = delete;
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> retainForCallback() override;
    [[nodiscard]] std::weak_ptr<path::OwnerLifetimeToken> lifetimeToken() const noexcept {
        return lifetime_;
    }
    [[nodiscard]] const std::string& objectId() const noexcept { return obj_id_; }
    void changed() override;
    [[nodiscard]] bool isChanged() const noexcept { return changed_; }
    void resetChanged() noexcept { changed_ = false; }
    [[nodiscard]] virtual AbstractAreaPtr dup() const = 0;
    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    void id(std::uint64_t value);
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void name(std::string value);
    [[nodiscard]] std::string dirName() const;
    void dirName(std::string value);
    [[nodiscard]] static std::string toDirName(std::string_view value);
    [[nodiscard]] std::string baseName() const;
    void baseName(std::string value);
    [[nodiscard]] static std::string toBaseName(std::string_view value);
    [[nodiscard]] virtual EventTreePtr etFromPath(const std::vector<std::size_t>& path);
    [[nodiscard]] ::cwxeditor::lib::SharedSlice<EventTreePtr>
    trees() override { return events_; }
    [[nodiscard]] ::cwxeditor::lib::ConstOwnerSlice<EventTree>
    trees() const override {
        return ::cwxeditor::lib::ConstOwnerSlice<EventTree>(events_);
    }
    void add(const EventTreePtr& event) override;
    using ::cwxeditor::cwx::event::EventTreeOwner::addAll;
    void addAll(const EventTreeArray& events);
    void insert(std::size_t index, const EventTreePtr& event) override;
    void removeEvent(std::size_t index) override;
    void remove(const EventTreePtr& event) override;
    void swapEventTree(std::size_t first, std::size_t second) override;
    void clearEvents() override;
    [[nodiscard]] bool isEmpty() const override;
    [[nodiscard]] std::string commentForEvents() const override { return events_comment_; }
    void commentForEvents(std::string value) override;
    [[nodiscard]] int opCmp(const AbstractArea& other) const noexcept;
    [[nodiscard]] virtual std::string rootName() const = 0;
    [[nodiscard]] std::vector<std::size_t> areaPath() const override { return {0}; }
    [[nodiscard]] std::string comment() const noexcept override { return comment_; }
    void comment(std::string value) override;
    [[nodiscard]] std::size_t toHash() const noexcept;
    [[nodiscard]] bool opEquals(const AbstractArea& other) const noexcept;
    [[nodiscard]] std::string toXML(const XMLOption& option) const;
    [[nodiscard]] XNode toNode(const XMLOption& option) const;
    XNode toNode(XNode& parent, const XMLOption& option,
                 std::string_view parent_path = {},
                 std::string_view cut_path = {}) const;
    virtual void toNodeImpl(XNode& element, const XMLOption& option,
                            std::string_view parent_path = {},
                            std::string_view cut_path = {}) const = 0;
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> findCWXPath(std::string_view path) override;
    [[nodiscard]] ::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> cwxChilds() override;
    [[nodiscard]] ::cwxeditor::lib::Observer<CWXPath> cwxParent() override;
    virtual void changeHandler(ChangeHandler handler);
    [[nodiscard]] ChangeHandler changeHandler() const { return change_; }
    void setUseCounter(const CounterOwner& counter, CWXPath* counter_owner = nullptr) noexcept;
    void removeUseCounter() noexcept;
    [[nodiscard]] CounterOwner useCounter() const noexcept { return use_counter_; }
    [[nodiscard]] CWXPath* ucOwner() const noexcept { return uc_owner_; }
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> ownerHandle() const;
    [[nodiscard]] virtual bool canHasFireEnter() const noexcept { return true; }
    [[nodiscard]] virtual bool canHasFireLose() const noexcept = 0;
    [[nodiscard]] virtual bool canHasFireEscape() const noexcept = 0;
    [[nodiscard]] virtual bool canHasFireEveryRound() const noexcept = 0;
    [[nodiscard]] virtual bool canHasFireRoundEnd() const noexcept = 0;
    [[nodiscard]] virtual bool canHasFireRound0() const noexcept = 0;
    [[nodiscard]] virtual bool canHasFireRound() const noexcept = 0;
    [[nodiscard]] virtual bool canHasFireKeyCode() const noexcept = 0;

protected:
    [[nodiscard]] ::cwxeditor::lib::SharedSlice<EventTreePtr>&
    eventTreesStorage() noexcept override { return events_; }
    [[nodiscard]] std::weak_ptr<path::OwnerLifetimeToken>
    eventTreeLifetimeToken() const noexcept override { return lifetime_; }
    void invalidateLifetime() noexcept {
        lifetime_->alive.store(false, std::memory_order_release);
    }
    [[nodiscard]] EventCounterBinding eventCounterBinding() const override;
    void appendProp(XNode& node, XNode& property, const XMLOption& option,
                    std::string_view parent_path, std::string_view cut_path) const;
    static void loadProp(XNode& node, std::uint64_t& id, std::string& name,
                         std::string& comment);
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> selfOwner() const;
    ::cwxeditor::lib::SharedSlice<EventTreePtr> events_;
    std::string events_comment_;
    ChangeHandler change_;
    CounterOwner use_counter_;
    CWXPath* uc_owner_{};

private:
    std::uint64_t id_{};
    std::string obj_id_;
    std::string name_;
    bool changed_{};
    std::string comment_;
    std::shared_ptr<path::OwnerLifetimeToken> lifetime_{
        std::make_shared<path::OwnerLifetimeToken>()};
};

class PlayerCardEvents final : public ::cwxeditor::cwx::event::EventTreeOwner,
                               public std::enable_shared_from_this<PlayerCardEvents> {
public:
    explicit PlayerCardEvents(AbstractArea* owner);
    ~PlayerCardEvents() override;
    PlayerCardEvents(const PlayerCardEvents&) = delete;
    PlayerCardEvents& operator=(const PlayerCardEvents&) = delete;
    PlayerCardEvents(PlayerCardEvents&&) = delete;
    PlayerCardEvents& operator=(PlayerCardEvents&&) = delete;
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> retainForCallback() override;
    [[nodiscard]] std::weak_ptr<path::OwnerLifetimeToken> lifetimeToken() const noexcept {
        return lifetime_;
    }
    [[nodiscard]] const std::string& objectId() const noexcept { return obj_id_; }
    [[nodiscard]] AbstractArea* owner() noexcept { return owner_.lock().get(); }
    [[nodiscard]] const AbstractArea* owner() const noexcept { return owner_.lock().get(); }
    [[nodiscard]] bool canHasFireEnter() const noexcept { return true; }
    [[nodiscard]] bool canHasFireLose() const noexcept { return false; }
    [[nodiscard]] bool canHasFireEscape() const noexcept { return false; }
    [[nodiscard]] bool canHasFireEveryRound() const noexcept { return false; }
    [[nodiscard]] bool canHasFireRoundEnd() const noexcept { return false; }
    [[nodiscard]] bool canHasFireRound0() const noexcept { return false; }
    [[nodiscard]] bool canHasFireRound() const noexcept { return false; }
    [[nodiscard]] bool canHasFireKeyCode() const noexcept { return true; }
    [[nodiscard]] std::vector<std::size_t> areaPath() const { return {1}; }
    [[nodiscard]] EventTreePtr etFromPath(const std::vector<std::size_t>& path);
    [[nodiscard]] ::cwxeditor::lib::SharedSlice<EventTreePtr>
    trees() override { return events_; }
    [[nodiscard]] ::cwxeditor::lib::ConstOwnerSlice<EventTree>
    trees() const override {
        return ::cwxeditor::lib::ConstOwnerSlice<EventTree>(events_);
    }
    void add(const EventTreePtr& event) override;
    using ::cwxeditor::cwx::event::EventTreeOwner::addAll;
    void addAll(const EventTreeArray& events);
    void insert(std::size_t index, const EventTreePtr& event) override;
    void removeEvent(std::size_t index) override;
    void remove(const EventTreePtr& event) override;
    void swapEventTree(std::size_t first, std::size_t second) override;
    void clearEvents() override;
    [[nodiscard]] bool isEmpty() const override;
    [[nodiscard]] std::string commentForEvents() const override { return events_comment_; }
    void commentForEvents(std::string value) override;
    [[nodiscard]] ::cwxeditor::lib::ConstOwnerSlice<EventTree>
    events() const noexcept { return ::cwxeditor::lib::ConstOwnerSlice<EventTree>(events_); }
    [[nodiscard]] ::cwxeditor::lib::SharedSlice<EventTreePtr>
    events() noexcept { return events_; }
    void changeHandler(ChangeHandler handler);
    void changed() override;
    void setUseCounter(const CounterOwner& counter, CWXPath* owner = nullptr) noexcept;
    void removeUseCounter() noexcept;
    void toNode(XNode& element, const XMLOption& option) const;
    [[nodiscard]] std::string cwxPath(bool id) override;
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> findCWXPath(std::string_view path) override;
    [[nodiscard]] ::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> cwxChilds() override;
    [[nodiscard]] ::cwxeditor::lib::Observer<CWXPath> cwxParent() override;
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> ownerHandle() const;

protected:
    [[nodiscard]] ::cwxeditor::lib::SharedSlice<EventTreePtr>&
    eventTreesStorage() noexcept override { return events_; }
    [[nodiscard]] std::weak_ptr<path::OwnerLifetimeToken>
    eventTreeLifetimeToken() const noexcept override { return lifetime_; }
    void invalidateLifetime() noexcept {
        lifetime_->alive.store(false, std::memory_order_release);
    }
    [[nodiscard]] EventCounterBinding eventCounterBinding() const override;

private:
    friend class Area;
    friend class Battle;
    std::string obj_id_;
    path::OwnerLink<AbstractArea> owner_;
    AbstractArea* owner_raw_{};
    ::cwxeditor::lib::SharedSlice<EventTreePtr> events_;
    std::string events_comment_;
    ChangeHandler change_;
    CounterOwner use_counter_;
    CWXPath* uc_owner_{};
    std::shared_ptr<path::OwnerLifetimeToken> lifetime_{
        std::make_shared<path::OwnerLifetimeToken>()};
};

class Area final : public AbstractArea, public BgImageOwner {
public:
    inline static constexpr std::string_view XML_NAME = "Area";
    using toID = std::uint64_t;
    explicit Area(std::uint64_t id, std::string_view name);
    ~Area() override;
    void changed() override;
    using AbstractArea::changeHandler;
    void changeHandler(ChangeHandler handler) override;
    [[nodiscard]] AbstractAreaPtr dup() const override;
    [[nodiscard]] bool canHasFireLose() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireEscape() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireEveryRound() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireRoundEnd() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireRound0() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireRound() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireKeyCode() const noexcept override { return true; }
    [[nodiscard]] EventTreePtr etFromPath(const std::vector<std::size_t>& path) override;
    [[nodiscard]] PlayerCardEvents* playerEvents() noexcept { return player_events_.get(); }
    [[nodiscard]] const PlayerCardEvents* playerEvents() const noexcept { return player_events_.get(); }
    void swapCards(std::size_t first, std::size_t second);
    void setCardIndices(const std::vector<std::pair<std::size_t, std::size_t>>& indices);
    void swapBacks(std::size_t first, std::size_t second);
    [[nodiscard]] bool spAuto() const noexcept { return auto_spread_; }
    void spAuto(bool value);
    [[nodiscard]] ::cwxeditor::lib::SharedSlice<MenuCardPtr>
    cards() noexcept { return cards_; }
    [[nodiscard]] ::cwxeditor::lib::ConstOwnerSlice<MenuCard>
    cards() const noexcept { return ::cwxeditor::lib::ConstOwnerSlice<MenuCard>(cards_); }
    [[nodiscard]] ::cwxeditor::lib::SharedSlice<BgImagePtr> backs() noexcept override {
        return backgrounds_;
    }
    [[nodiscard]] ::cwxeditor::lib::ConstOwnerSlice<BgImage> backs() const noexcept override {
        return ::cwxeditor::lib::ConstOwnerSlice<BgImage>(backgrounds_);
    }
    [[nodiscard]] std::weak_ptr<path::OwnerLifetimeToken>
    backgroundLifetimeToken() const noexcept override { return lifetimeToken(); }
    void append(MenuCardPtr card);
    void insert(std::size_t index, MenuCardPtr card);
    void removeCard(std::size_t index);
    void append(BgImagePtr back);
    void insert(std::size_t index, BgImagePtr back);
    void set(std::size_t index, BgImagePtr back);
    void removeBgImage(std::size_t index);
    void setUseCounter(const CounterOwner& counter, CWXPath* owner = nullptr) noexcept;
    void removeUseCounter() noexcept;
    [[nodiscard]] std::string rootName() const override { return std::string(XML_NAME); }
    void toNodeImpl(XNode& element, const XMLOption& option,
                    std::string_view parent_path = {},
                    std::string_view cut_path = {}) const override;
    [[nodiscard]] static AreaPtr loadFromXML(std::string_view path, const XMLInfo& version);
    [[nodiscard]] static AreaPtr createFromNode(XNode& node, const XMLInfo& version);
    [[nodiscard]] static std::string CtoXML(const MenuCardArray& cards, const XMLOption& option);
    [[nodiscard]] static XNode CtoNode(const MenuCardArray& cards, const XMLOption& option);
    [[nodiscard]] static std::string BtoXML(const BgImageArray& backs, const XMLOption& option);
    [[nodiscard]] static XNode BtoNode(const BgImageArray& backs, const XMLOption& option);
    [[nodiscard]] static std::string CBtoXML(const MenuCardArray& cards,
                                              const BgImageArray& backs,
                                              const XMLOption& option);
    [[nodiscard]] static XNode CBtoNode(const MenuCardArray& cards,
                                         const BgImageArray& backs,
                                         const XMLOption& option);
    static bool CBfromXML(std::string_view xml, MenuCardArray& cards,
                          BgImageArray& backs, const XMLInfo& version);
    static bool CBfromXML(XNode& node, MenuCardArray& cards,
                          BgImageArray& backs, const XMLInfo& version);
    void owner(AreaOwner* owner) noexcept { owner_.setRaw(owner); }
    void owner(std::nullptr_t) noexcept { owner_.setRaw(nullptr); }
    void owner(AreaOwner* owner, std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept {
        owner_.setBorrowed(owner, std::move(lifetime));
    }
    void owner(const ::cwxeditor::lib::Owner<AreaOwner>& owner) { owner_.setShared(owner); }
    [[nodiscard]] std::string cwxPath(bool id) override;
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> findCWXPath(std::string_view path) override;
    [[nodiscard]] ::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> cwxChilds() override;
    [[nodiscard]] ::cwxeditor::lib::Observer<CWXPath> cwxParent() override;

private:
    ::cwxeditor::lib::SharedSlice<MenuCardPtr> cards_;
    ::cwxeditor::lib::SharedSlice<BgImagePtr> backgrounds_;
    bool auto_spread_{};
    std::shared_ptr<PlayerCardEvents> player_events_;
    path::OwnerLink<AreaOwner> owner_;
    friend class MenuCard;
};

class Package final : public AbstractArea {
public:
    inline static constexpr std::string_view XML_NAME = "Package";
    using toID = std::uint64_t;
    explicit Package(std::uint64_t id, std::string_view name);
    ~Package() override;
    [[nodiscard]] AbstractAreaPtr dup() const override;
    [[nodiscard]] bool canHasFireEnter() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireLose() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireEscape() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireRoundEnd() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireEveryRound() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireRound0() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireRound() const noexcept override { return false; }
    [[nodiscard]] bool canHasFireKeyCode() const noexcept override { return false; }
    [[nodiscard]] EventTreePtr etFromPath(const std::vector<std::size_t>& path) override;
    [[nodiscard]] std::string rootName() const override { return std::string(XML_NAME); }
    void toNodeImpl(XNode& element, const XMLOption& option,
                    std::string_view parent_path = {},
                    std::string_view cut_path = {}) const override;
    [[nodiscard]] static PackagePtr loadFromXML(std::string_view path, const XMLInfo& version);
    [[nodiscard]] static PackagePtr createFromNode(XNode& node, const XMLInfo& version);
    void owner(PackageOwner* owner) noexcept { owner_.setRaw(owner); }
    void owner(std::nullptr_t) noexcept { owner_.setRaw(nullptr); }
    void owner(PackageOwner* owner, std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept {
        owner_.setBorrowed(owner, std::move(lifetime));
    }
    void owner(const ::cwxeditor::lib::Owner<PackageOwner>& owner) { owner_.setShared(owner); }
    [[nodiscard]] std::string cwxPath(bool id) override;
    [[nodiscard]] ::cwxeditor::lib::Observer<CWXPath> cwxParent() override;

private:
    path::OwnerLink<PackageOwner> owner_;
};

class Battle final : public AbstractArea {
public:
    inline static constexpr std::string_view XML_NAME = "Battle";
    using toID = std::uint64_t;
    Battle(std::uint64_t id, std::string_view name, std::string_view music);
    ~Battle() override;
    using AbstractArea::changeHandler;
    void changeHandler(ChangeHandler handler) override;
    [[nodiscard]] AbstractAreaPtr dup() const override;
    [[nodiscard]] bool canHasFireLose() const noexcept override { return true; }
    [[nodiscard]] bool canHasFireEscape() const noexcept override { return true; }
    [[nodiscard]] bool canHasFireEveryRound() const noexcept override { return true; }
    [[nodiscard]] bool canHasFireRoundEnd() const noexcept override { return true; }
    [[nodiscard]] bool canHasFireRound0() const noexcept override { return true; }
    [[nodiscard]] bool canHasFireRound() const noexcept override { return true; }
    [[nodiscard]] bool canHasFireKeyCode() const noexcept override { return true; }
    [[nodiscard]] EventTreePtr etFromPath(const std::vector<std::size_t>& path) override;
    [[nodiscard]] PlayerCardEvents* playerEvents() noexcept { return player_events_.get(); }
    [[nodiscard]] const PlayerCardEvents* playerEvents() const noexcept { return player_events_.get(); }
    void music(std::string value);
    [[nodiscard]] std::string music() const { return music_.path(); }
    void volume(std::uint32_t value);
    [[nodiscard]] std::uint32_t volume() const noexcept { return volume_; }
    void loopCount(std::uint32_t value);
    [[nodiscard]] std::uint32_t loopCount() const noexcept { return loop_count_; }
    void fadeIn(std::uint32_t value);
    [[nodiscard]] std::uint32_t fadeIn() const noexcept { return fade_in_; }
    void continueBGM(bool value);
    [[nodiscard]] bool continueBGM() const noexcept { return continue_bgm_; }
    void append(EnemyCardPtr card);
    void insert(std::size_t index, EnemyCardPtr card);
    void removeCard(std::size_t index);
    void swapCards(std::size_t first, std::size_t second);
    void setCardIndices(const std::vector<std::pair<std::size_t, std::size_t>>& indices);
    [[nodiscard]] ::cwxeditor::lib::SharedSlice<EnemyCardPtr> cards() noexcept {
        return cards_;
    }
    [[nodiscard]] ::cwxeditor::lib::ConstOwnerSlice<EnemyCard> cards() const noexcept {
        return ::cwxeditor::lib::ConstOwnerSlice<EnemyCard>(cards_);
    }
    [[nodiscard]] bool spAuto() const noexcept { return auto_spread_; }
    void spAuto(bool value);
    [[nodiscard]] bool possibleToRunAway() const noexcept { return possible_to_run_away_; }
    void possibleToRunAway(bool value);
    [[nodiscard]] Transition battleEndLoseBgTransition() const noexcept { return transition_; }
    void battleEndLoseBgTransition(Transition value);
    [[nodiscard]] std::int32_t battleEndLoseBgTransitionSpeed() const noexcept { return transition_speed_; }
    void battleEndLoseBgTransitionSpeed(std::int32_t value);
    [[nodiscard]] bool battleEndLoseBgDoAnime() const noexcept { return do_anime_; }
    void battleEndLoseBgDoAnime(bool value);
    [[nodiscard]] bool battleEndLoseBgIgnoreEffectBooster() const noexcept { return ignore_effect_booster_; }
    void battleEndLoseBgIgnoreEffectBooster(bool value);
    void setUseCounter(const CounterOwner& counter, CWXPath* owner = nullptr) noexcept;
    void removeUseCounter() noexcept;
    [[nodiscard]] std::string rootName() const override { return std::string(XML_NAME); }
    void toNodeImpl(XNode& element, const XMLOption& option,
                    std::string_view parent_path = {},
                    std::string_view cut_path = {}) const override;
    [[nodiscard]] static BattlePtr loadFromXML(std::string_view path, const XMLInfo& version);
    [[nodiscard]] static BattlePtr createFromNode(XNode& node, const XMLInfo& version);
    [[nodiscard]] static std::string CtoXML(const EnemyCardArray& cards, const XMLOption& option);
    [[nodiscard]] static XNode CtoNode(const EnemyCardArray& cards, const XMLOption& option);
    [[nodiscard]] static std::string CtoXML(::cwxeditor::lib::SharedSlice<const EnemyCardPtr> cards, const XMLOption& option);
    [[nodiscard]] static XNode CtoNode(::cwxeditor::lib::SharedSlice<const EnemyCardPtr> cards, const XMLOption& option);
    [[nodiscard]] static std::string CtoXML(std::initializer_list<EnemyCardPtr> cards, const XMLOption& option) {
        return CtoXML(::cwxeditor::lib::SharedSlice<const EnemyCardPtr>(cards), option);
    }
    [[nodiscard]] static XNode CtoNode(std::initializer_list<EnemyCardPtr> cards, const XMLOption& option) {
        return CtoNode(::cwxeditor::lib::SharedSlice<const EnemyCardPtr>(cards), option);
    }
    static bool CfromXML(std::string_view xml, EnemyCardArray& cards,
                         const XMLInfo& version);
    static bool CfromXML(XNode& node, EnemyCardArray& cards,
                         const XMLInfo& version);
    void owner(BattleOwner* owner) noexcept { owner_.setRaw(owner); }
    void owner(std::nullptr_t) noexcept { owner_.setRaw(nullptr); }
    void owner(BattleOwner* owner, std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept {
        owner_.setBorrowed(owner, std::move(lifetime));
    }
    void owner(const ::cwxeditor::lib::Owner<BattleOwner>& owner) { owner_.setShared(owner); }
    [[nodiscard]] std::string cwxPath(bool id) override;
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> findCWXPath(std::string_view path) override;
    [[nodiscard]] ::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> cwxChilds() override;
    [[nodiscard]] ::cwxeditor::lib::Observer<CWXPath> cwxParent() override;

private:
    ::cwxeditor::lib::SharedSlice<EnemyCardPtr> cards_;
    bool auto_spread_{};
    PathUser music_;
    bool continue_bgm_{};
    std::uint32_t volume_{100};
    std::uint32_t loop_count_{};
    std::uint32_t fade_in_{};
    bool possible_to_run_away_{true};
    Transition transition_{Transition::None};
    std::int32_t transition_speed_{5};
    bool do_anime_{};
    bool ignore_effect_booster_{true};
    std::shared_ptr<PlayerCardEvents> player_events_;
    path::OwnerLink<BattleOwner> owner_;
    friend class EnemyCard;
};

}  // namespace cwxeditor::cwx::area
