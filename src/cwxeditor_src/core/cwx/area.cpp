#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <fstream>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>

namespace cwxeditor::cwx::area {

namespace {

using ::cwxeditor::cwx::path::cpbottom;
using ::cwxeditor::cwx::path::cpcategory;
using ::cwxeditor::cwx::path::cpempty;
using ::cwxeditor::cwx::path::cpindex;
using ::cwxeditor::cwx::path::cpjoin;
using ::cwxeditor::cwx::path::cpjoinid;
using ::cwxeditor::cwx::types::LAYER_MENU_CARD;
using ::cwxeditor::cwx::utils::decodeLf2;
using ::cwxeditor::cwx::utils::decodePath;
using ::cwxeditor::cwx::utils::encodeLf;
using ::cwxeditor::cwx::utils::encodePath;
using ::cwxeditor::cwx::utils::fromBool;
using ::cwxeditor::cwx::utils::parseBool;

template <typename T>
std::string areaObjectId(const T* object, std::string_view type_name) {
    static std::atomic<std::uint64_t> id_count{0};
    const auto identity = ::cwxeditor::cwx::utils::objectIDValue(object);
    // Dではクラスごとにコンストラクター内のulongカウンターを持つ。
    // Dのコンパイラー式の文字列ではなくC++の型ラベルを使う。アドレス
    // と時刻はいずれの言語でもプロセス固有。C++の呼び出し側が別スレッ
    // ドから構築してもカウンターを維持するため、アトミックに操作する。
    return std::string(type_name) + "-" + identity + "-" +
           std::to_string(id_count.fetch_add(1, std::memory_order_relaxed));
}

template <typename Pointer>
auto& requireAreaObject(const Pointer& object) {
    // Dはこれらのオブジェクトを参照し、nullを何もしない操作としては受け付け
    // ない。C++の未定義動作を起こさず、不正なネイティブポインターを報告する。
    if (!object) throw std::invalid_argument("Null area child");
    return *object;
}

class AreaPathProxy final : public CWXPath {
public:
    AreaPathProxy(CWXPath* target,
                  std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept
        : target_(target), lifetime_(std::move(lifetime)) {}

    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> retainForCallback() override {
        const auto guard = liveLifetime();
        return guard ? target_->retainForCallback()
                     : ::cwxeditor::lib::Owner<CWXPath>{};
    }
    [[nodiscard]] ::cwxeditor::lib::String cwxPath(bool id) override {
        const auto guard = liveLifetime();
        return guard ? target_->cwxPath(id) : std::string{};
    }
    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> findCWXPath(
        std::string_view value) override {
        const auto guard = liveLifetime();
        return guard ? target_->findCWXPath(value)
                     : ::cwxeditor::lib::Owner<CWXPath>{};
    }
    [[nodiscard]] ::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>>
    cwxChilds() override {
        const auto guard = liveLifetime();
        return guard ? target_->cwxChilds()
                     : ::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>>{};
    }
    [[nodiscard]] ::cwxeditor::lib::Observer<CWXPath> cwxParent() override {
        const auto guard = liveLifetime();
        return guard ? target_->cwxParent()
                     : ::cwxeditor::lib::Observer<CWXPath>{};
    }
    void changed() override {
        const auto guard = liveLifetime();
        if (guard) target_->changed();
    }

private:
    [[nodiscard]] std::shared_ptr<path::OwnerLifetimeToken> liveLifetime() const noexcept {
        auto lifetime = lifetime_.lock();
        return lifetime && lifetime->alive.load(std::memory_order_acquire)
            ? lifetime : std::shared_ptr<path::OwnerLifetimeToken>{};
    }

    CWXPath* target_{};
    std::weak_ptr<path::OwnerLifetimeToken> lifetime_;
};

[[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> safePathOwner(
    CWXPath* target, std::weak_ptr<path::OwnerLifetimeToken> lifetime) {
    if (!target) return {};
    if (auto owner = target->retainForCallback()) return owner;
    auto guard = lifetime.lock();
    if (!guard || !guard->alive.load(std::memory_order_acquire)) return {};
    return std::make_shared<AreaPathProxy>(target, std::move(lifetime));
}

[[nodiscard]] ChangeHandler guardedChangeHandler(
    CWXPath* target, std::weak_ptr<path::OwnerLifetimeToken> lifetime) {
    return [target, lifetime = std::move(lifetime)] {
        auto guard = lifetime.lock();
        if (!guard || !guard->alive.load(std::memory_order_acquire)) return;
        [[maybe_unused]] const auto keepAlive = target->retainForCallback();
        target->changed();
    };
}

template <typename Events, typename Cards>
EventTreePtr areaEventFromPath(const std::vector<std::size_t>& path,
                              const Events& events,
                              const std::shared_ptr<PlayerCardEvents>& player_events,
                              const Cards& cards) {
    // at()により、Dの範囲検査付き配列アクセスをネイティブの範囲エラーへ対応させる。
    const auto category = path.at(0);
    if (category == 0U) return events.at(path.at(1));
    if (category == 1U) return requireAreaObject(player_events).trees().at(path.at(1));
    return requireAreaObject(cards.at(category - 2U)).trees().at(path.at(1));
}

template <typename Cards, typename Notify>
void assignAreaCardIndices(Cards& cards,
                          const std::vector<std::pair<std::size_t, std::size_t>>& indices,
                          Notify notify) {
    std::vector<typename Cards::value_type> copy;
    for (const auto [source, destination] : indices) {
        if (source == destination) continue;
        if (copy.empty()) {
            notify();
            copy.assign(cards.begin(), cards.end());
        }
        cards.at(destination) = copy.at(source);
    }
}

template <typename Slice>
[[nodiscard]] std::vector<typename Slice::value_type> snapshotSlice(const Slice& values) {
    return {values.begin(), values.end()};
}

template <typename Read>
bool readAreaClipboard(Read read) {
    try {
        return read();
    } catch (const std::bad_alloc&) {
        // Dがここで捕捉するのはExceptionであり、OutOfMemoryError
        // ではない。ネイティブのメモリ確保失敗を通常のfalseへ変換しない。
        throw;
    } catch (const std::exception& error) {
        ::cwxeditor::cwx::utils::fdebugln(error.what());
        return false;
    }
}

template <typename T>
void newTextElement(XNode& parent, std::string_view name, const T& value) {
    const auto text = ::cwxeditor::cwx::utils::detail::stringFrom(value);
    parent.newElement(name, std::string_view(text));
}

void newTextAttr(XNode& node, std::string_view name, std::string_view value) {
    node.newAttr(name, value);
}

[[nodiscard]] std::string imagePath(const CardImagePtr& image) {
    return image ? image->path() : std::string{};
}

[[nodiscard]] bool sameCardImages(const CardImageArray& left,
                                  const CardImageArray& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index] && right[index]) {
            if (!left[index]->opEquals(*right[index])) return false;
        } else if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] CardImageArray copyCardImages(CWXPath* owner,
                                             const CardImageArray& images) {
    CardImageArray result;
    result.reserve(images.size());
    for (const auto& image : images) {
        if (image) result.push_back(std::make_shared<CardImage>(owner, *image));
    }
    return result;
}

[[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> nonOwning(CWXPath* value) {
    if (value == nullptr) return {};
    return ::cwxeditor::lib::Owner<CWXPath>(value, [](CWXPath*) {});
}

template <typename OwnerType>
[[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> ownerPath(
    const path::OwnerLink<OwnerType>& link) {
    const auto owner = link.lock();
    if (!owner) return {};
    if (owner.shared()) return std::static_pointer_cast<CWXPath>(owner.shared());
    return static_cast<CWXPath*>(owner.get())->findCWXPath({});
}

template <typename OwnerType>
[[nodiscard]] ::cwxeditor::lib::Observer<CWXPath> ownerObserver(
    const path::OwnerLink<OwnerType>& link) {
    if (auto owner = link.observer().lock())
        return std::static_pointer_cast<CWXPath>(std::move(owner));
    return {};
}

[[nodiscard]] ::cwxeditor::lib::Observer<CWXPath> observe(
    const ::cwxeditor::lib::Owner<CWXPath>& value) {
    return value;
}

[[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> optionOwner(CWXPath* value) {
    return nonOwning(value);
}

template <typename Range, typename T>
[[nodiscard]] std::size_t indexOf(const Range& values, const T* value) {
    const auto it = std::find_if(values.begin(), values.end(),
                                 [value](const auto& candidate) {
                                     return candidate && candidate.get() == value;
                                 });
    return it == values.end() ? values.size()
                              : static_cast<std::size_t>(it - values.begin());
}

template <typename T>
void installChangeHandler(const std::shared_ptr<T>& object,
                          const ChangeHandler& handler) {
    if (object) object->changeHandler(handler);
}

template <typename OwnerType>
void attachOwnerLink(path::OwnerLink<OwnerType>& link, OwnerType* owner,
                     std::weak_ptr<path::OwnerLifetimeToken> lifetime) {
    if (!owner) {
        link.setRaw(nullptr);
        return;
    }
    auto pathOwner = owner->retainForCallback();
    auto sharedOwner = std::dynamic_pointer_cast<OwnerType>(std::move(pathOwner));
    if (sharedOwner) link.setShared(sharedOwner);
    else link.setBorrowed(owner, std::move(lifetime));
}

template <typename OwnerType>
void installEventTreeCallback(const std::shared_ptr<event::EventTree>& tree,
                              OwnerType* owner) {
    if (tree) {
        tree->changeHandler(guardedChangeHandler(owner, owner->lifetimeToken()));
    }
}

void detachEventTreeIfOwned(const EventTreePtr& tree,
                            event::EventTreeOwner* owner,
                            const bool removeCounter) {
    event::EventTreeOwner::detachEventTree(
        tree, removeCounter, owner,
        owner ? owner->eventTreeLifetimeToken()
              : std::weak_ptr<path::OwnerLifetimeToken>{});
}

template <typename Events>
void appendEmptyEvents(XNode& parent, const Events& events,
                       const XMLOption& option) {
    auto container = parent.newElement("Events");
    for (const auto& event : events) if (event) event->toNode(container, &option);
}

template <typename Events>
void appendEvents(XNode& parent, const Events& events,
                  const std::string_view comment, const XMLOption& option) {
    auto container = parent.newElement("Events");
    if (!comment.empty()) container.newAttr("comment", comment);
    for (const auto& event : events) if (event) event->toNode(container, &option);
}

[[nodiscard]] EventTreeArray loadEvents(const XNode& node, std::string& comment,
                                        const XMLInfo& version) {
    EventTreeArray result;
    comment = node.attr("comment", false);
    for (const auto& child : node.element_children()) {
        if (child.name() == "Event") {
            if (auto event = EventTree::createFromNode(child, &version)) result.push_back(std::move(event));
        }
    }
    return result;
}

template <typename T>
[[nodiscard]] T parseNumber(std::string_view value) {
    if (const auto parsed = ::cwxeditor::cwx::utils::detail::parseDInteger<T>(value)) return *parsed;
    throw AreaException("Invalid number: " + std::string(value));
}

[[nodiscard]] std::string readTextFile(std::string_view path) {
    std::ifstream input(std::string(path), std::ios::binary);
    if (!input) throw std::runtime_error("Unable to open XML file: " + std::string(path));
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

}  // namespace

AbstractAreaArray createAreasFromNode(XNode& node, const std::string_view scenario_path,
                                      bool& same_summary, bool& from_table,
                                      const XMLInfo& version) {
    same_summary = false;
    from_table = false;
    try {
        const auto source_path = node.attr("scenarioPath", false);
        // 元のDの式をそのまま維持する。両方の被演算子が呼び出
        // し側のパスになっており、元の実装にもこの誤記がある。
        same_summary = !scenario_path.empty() && !source_path.empty() &&
                       ::cwxeditor::cwx::utils::cfnmatch(
                           ::cwxeditor::cwx::utils::nabs(scenario_path),
                           ::cwxeditor::cwx::utils::nabs(scenario_path));
        AbstractAreaArray result;
        auto load = [&](XNode& value) {
            if (value.name() == "Area") result.push_back(Area::createFromNode(value, version));
            else if (value.name() == "Battle") result.push_back(Battle::createFromNode(value, version));
            else if (value.name() == "Package") result.push_back(Package::createFromNode(value, version));
        };
        if (node.name() == "Table") {
            from_table = true;
            node.onTag[""] = load;
            node.onTag["*"] = load;
            node.parse();
        } else {
            load(node);
        }
        return result;
    } catch (const std::exception& error) {
        ::cwxeditor::cwx::utils::debugln(error.what());
        return {};
    }
}

AbstractSpCard::AbstractSpCard(const std::string_view flag, const std::int32_t x,
                               const std::int32_t y, const std::uint32_t scale,
                               const std::int32_t layer,
                               const std::string_view card_group,
                               const std::int32_t animation_speed)
    : obj_id_(areaObjectId(this, "AbstractSpCard")),
      x_(x), y_(y), scale_(scale), layer_(layer), user_(this),
      card_group_(this), animation_speed_(animation_speed) {
    user_.flag(flag);
    card_group_.cardGroup(card_group);
}

AbstractSpCard::~AbstractSpCard() {
    const usecounter::detail::NativeCounterRetirement native_retirement;
    invalidateLifetime();
    for (const auto& tree : events_) {
        detachEventTreeIfOwned(tree, this, true);
    }
}

::cwxeditor::lib::Owner<CWXPath> AbstractSpCard::retainForCallback() {
    return std::static_pointer_cast<CWXPath>(weak_from_this().lock());
}

::cwxeditor::lib::Owner<CWXPath> AbstractSpCard::selfOwner() const {
    return safePathOwner(const_cast<AbstractSpCard*>(this), lifetime_);
}

::cwxeditor::lib::Owner<CWXPath> AbstractSpCard::ownerHandle() const {
    return selfOwner();
}

void AbstractSpCard::changed() {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (change_) change_();
}

EventCounterBinding AbstractSpCard::eventCounterBinding() const {
    return {use_counter_, nonOwning(uc_owner_)};
}

void AbstractSpCard::add(const EventTreePtr& event) {
    ::cwxeditor::cwx::event::EventTreeOwner::add(event);
}

void AbstractSpCard::addAll(const EventTreeArray& events) {
    ::cwxeditor::cwx::event::EventTreeOwner::addAll(events);
}

void AbstractSpCard::insert(const std::size_t index, const EventTreePtr& event) {
    ::cwxeditor::cwx::event::EventTreeOwner::insert(index, event);
}

void AbstractSpCard::removeEvent(const std::size_t index) {
    ::cwxeditor::cwx::event::EventTreeOwner::removeEvent(index);
}

void AbstractSpCard::remove(const EventTreePtr& event) {
    ::cwxeditor::cwx::event::EventTreeOwner::remove(event);
}

void AbstractSpCard::swapEventTree(const std::size_t first, const std::size_t second) {
    ::cwxeditor::cwx::event::EventTreeOwner::swapEventTree(first, second);
}

void AbstractSpCard::clearEvents() {
    ::cwxeditor::cwx::event::EventTreeOwner::clearEvents();
}

bool AbstractSpCard::isEmpty() const {
    return std::all_of(events_.begin(), events_.end(), [](const auto& event) {
        return !event || event->isEmpty();
    });
}

void AbstractSpCard::commentForEvents(std::string value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (events_comment_ == value) return;
    changed();
    events_comment_ = std::move(value);
}

EventTreePtr AbstractSpCard::etFromPath(const std::vector<std::size_t>& path) const {
    return path.size() == 1U && path.front() < events_.size() ? events_[path.front()] : EventTreePtr{};
}

void AbstractSpCard::changeHandler(ChangeHandler handler) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto retainedEvents = events_;
    for (const auto& event : retainedEvents) installEventTreeCallback(event, this);
    change_ = std::move(handler);
}

void AbstractSpCard::flag(std::string value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (user_.flag() != value) changed();
    user_.flag(value);
}

void AbstractSpCard::x(const std::int32_t value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (x_ != value) changed();
    x_ = value;
}

void AbstractSpCard::y(const std::int32_t value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (y_ != value) changed();
    y_ = value;
}

void AbstractSpCard::scale(const std::uint32_t value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (scale_ != value) changed();
    scale_ = value;
}

void AbstractSpCard::layer(const std::int32_t value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (layer_ != value) changed();
    layer_ = value;
}

void AbstractSpCard::cardGroup(std::string value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (card_group_.cardGroup() != value) changed();
    card_group_.cardGroup(value);
}

void AbstractSpCard::animationSpeed(const std::int32_t value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (animation_speed_ != value) changed();
    animation_speed_ = value;
}

void AbstractSpCard::comment(std::string value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (comment_ == value) return;
    changed();
    comment_ = std::move(value);
}

CWXPath* AbstractSpCard::connectedResource(card::CastOwner*) noexcept { return nullptr; }

void AbstractSpCard::setUseCounter(const CounterOwner& counter,
                                   CWXPath* counter_owner) noexcept {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    user_.setUseCounter(counter);
    card_group_.setUseCounter(counter, counter_owner);
    const auto retainedEvents = events_;
    for (const auto& event : retainedEvents) if (event) {
        event->setUseCounter(counter, nonOwning(counter_owner));
    }
    use_counter_ = counter;
    uc_owner_ = usecounter::renameInfo(counter_owner).get();
}

void AbstractSpCard::removeUseCounter() noexcept {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    user_.removeUseCounter();
    card_group_.removeUseCounter();
    const auto retainedEvents = events_;
    for (const auto& event : retainedEvents) if (event) event->removeUseCounter();
    use_counter_.reset();
    uc_owner_ = nullptr;
}

::cwxeditor::lib::Owner<CWXPath> AbstractSpCard::findCWXPath(
    const std::string_view path) {
    if (cpempty(path)) return selfOwner();
    if (cpcategory(path) == "event") {
        const auto index = cpindex(path);
        return index < events_.size() && events_[index]
                   ? events_[index]->findCWXPath(cpbottom(path))
                   : ::cwxeditor::lib::Owner<CWXPath>{};
    }
    return {};
}

::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>>
AbstractSpCard::cwxChilds() {
    ::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> result;
    for (const auto& event : events_) if (event) result.emplace_back(event);
    return result;
}

::cwxeditor::lib::Observer<CWXPath> AbstractSpCard::cwxParent() { return {}; }

void AbstractSpCard::shallowCopyFrom(const AbstractSpCard& copy_base) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    [[maybe_unused]] const auto donorKeepAlive =
        const_cast<AbstractSpCard&>(copy_base).retainForCallback();
    flag(copy_base.flag());
    x(copy_base.x());
    y(copy_base.y());
    scale(copy_base.scale());
    layer(copy_base.layer());
    cardGroup(copy_base.cardGroup());
    animationSpeed(copy_base.animationSpeed());
    comment(copy_base.comment());
}

void AbstractSpCard::appendProp(XNode& node, XNode& property,
                                const XMLOption&) const {
    assert(property.name() == "Property");
    if (!comment_.empty()) newTextAttr(node, "comment", comment_);
    newTextElement(property, "Flag", user_.flag());
    auto location = property.newElement("Location");
    location.newAttr("left", x_);
    location.newAttr("top", y_);
    auto size = property.newElement("Size");
    newTextAttr(size, "scale", std::to_string(scale_) + "%");
    if (layer_ != LAYER_MENU_CARD) property.newElement("Layer", layer_);
    if (!card_group_.cardGroup().empty()) newTextElement(property, "CardGroup", card_group_.cardGroup());
    if (animation_speed_ != -1) property.newElement("DealingSpeed", animation_speed_);
}

void AbstractSpCard::loadProp(XNode& property, std::string& flag,
                              std::int32_t& x, std::int32_t& y,
                              std::uint32_t& scale, std::int32_t& layer,
                              std::string& card_group,
                              std::int32_t& animation_speed) {
    flag.clear();
    x = 0;
    y = 0;
    scale = 100;
    layer = LAYER_MENU_CARD;
    card_group.clear();
    animation_speed = -1;
    assert(property.name() == "Property");
    property.onTag["Flag"] = [&](XNode& node) { flag = node.value(); };
    property.onTag["Location"] = [&](XNode& node) {
        x = parseNumber<std::int32_t>(node.attr("left", true));
        y = parseNumber<std::int32_t>(node.attr("top", true));
    };
    property.onTag["Size"] = [&](XNode& node) {
        auto value = node.attr("scale", true);
        if (value.size() < 2U) throw AreaException("scale error: " + value);
        if (!value.empty() && value.back() == '%') value.pop_back();
        scale = parseNumber<std::uint32_t>(value);
    };
    property.onTag["Layer"] = [&](XNode& node) { layer = parseNumber<std::int32_t>(node.value()); };
    property.onTag["CardGroup"] = [&](XNode& node) { card_group = node.value(); };
    property.onTag["DealingSpeed"] = [&](XNode& node) {
        animation_speed = node.value() == "Default" ? -1 : parseNumber<std::int32_t>(node.value());
    };
    property.parse();
}

EnemyCard::EnemyCard(const std::uint64_t id, const ActionMap& actions,
                     const std::string_view flag, const std::int32_t x,
                     const std::int32_t y, const std::uint32_t scale,
                     const std::int32_t layer, const std::string_view card_group,
                     const std::int32_t animation_speed, const bool override_name,
                     const std::string_view name, const bool override_image,
                     const CardImageArray& images)
    : AbstractSpCard(flag, x, y, scale, layer, card_group, animation_speed),
      user_(this), override_name_(override_name),
      override_name_text_(std::make_unique<SimpleTextHolder>(
          selfOwner(), TextHolderType::CardName, "name")),
      override_image_(override_image) {
    user_.casts(id);
    this->actions(actions);
    override_name_text_->changeHandler(guardedChangeHandler(this, lifetime_));
    override_name_text_->text(name);
    overrideImages(images);
}

EnemyCard::~EnemyCard() {
    invalidateLifetime();
    if (override_name_text_) override_name_text_->changeHandler({});
    owner_.setRaw(nullptr);
    owner_raw_ = nullptr;
}

void EnemyCard::attachOwner(Battle* owner) {
    owner_raw_ = owner;
    attachOwnerLink(owner_, owner, owner ? owner->lifetimeToken()
                                         : std::weak_ptr<path::OwnerLifetimeToken>{});
}

std::string EnemyCard::cwxPath(const bool id_value) {
    const auto parent = owner_.lock();
    if (!parent) return {};
    const auto pathOwner = ownerPath(owner_);
    if (!pathOwner) return {};
    const auto index = indexOf(parent->cards(), this);
    return cpjoin(pathOwner->cwxPath(id_value), "enemycard", index);
}

::cwxeditor::lib::Observer<CWXPath> EnemyCard::cwxParent() {
    return ownerObserver(owner_);
}

AbstractSpCardPtr EnemyCard::shallowCopy() const {
    auto result = std::make_shared<EnemyCard>(id(), actions_, flag(), x(), y(), scale(),
                                              layer(), cardGroup(), animationSpeed(),
                                              override_name_, overrideName(),
                                              override_image_, override_images_);
    result->comment(comment());
    return result;
}

AbstractSpCardPtr EnemyCard::dup() const {
    auto result = shallowCopy();
    for (const auto& event : events_) if (event) result->add(event->dup());
    result->commentForEvents(events_comment_);
    return result;
}

void EnemyCard::shallowCopyFrom(const AbstractSpCard& base) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    [[maybe_unused]] const auto donorKeepAlive =
        const_cast<AbstractSpCard&>(base).retainForCallback();
    const auto* card = dynamic_cast<const EnemyCard*>(&base);
    if (!card) throw AreaException("copyBase is not EnemyCard");
    AbstractSpCard::shallowCopyFrom(base);
    actions(card->actions_);
    id(card->id());
    isOverrideName(card->isOverrideName());
    overrideName(card->overrideName());
    isOverrideImage(card->isOverrideImage());
    overrideImages(card->override_images_);
}

std::vector<std::size_t> EnemyCard::areaPath() const {
    const auto parent = owner_.lock();
    if (!parent) return {};
    const auto index = indexOf(parent->cards(), this);
    return {index + 2U};
}

AbstractArea* EnemyCard::abstractOwner() noexcept { return owner_.lock().get(); }
const AbstractArea* EnemyCard::abstractOwner() const noexcept { return owner_.lock().get(); }

void EnemyCard::actions(ActionMap value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    ActionMap normalized;
    for (const auto& [type, enabled] : value) {
        const auto default_value = type != ActionCardType::RunAway;
        if (enabled != default_value) normalized[type] = enabled;
    }
    if (actions_ == normalized) return;
    changed();
    actions_.clear();
    // Dの連想配列の反復順は未規定。ネイティブのmap
    // では列挙順を選び、各変更前の通知は維持する。
    for (const auto& [type, enabled] : normalized) action(type, enabled);
}

bool EnemyCard::action(const ActionCardType type) const {
    const auto it = actions_.find(type);
    return it == actions_.end() ? type != ActionCardType::RunAway : it->second;
}

void EnemyCard::action(const ActionCardType type, const bool value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (action(type) == value) return;
    changed();
    const auto default_value = type != ActionCardType::RunAway;
    if (value == default_value) actions_.erase(type);
    else actions_[type] = value;
}

void EnemyCard::id(const std::uint64_t value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (user_.casts() != value) changed();
    user_.casts(value);
}

void EnemyCard::isOverrideName(const bool value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (override_name_ != value) changed();
    override_name_ = value;
}

std::string EnemyCard::overrideName() const {
    return override_name_text_ ? override_name_text_->text() : std::string{};
}

void EnemyCard::overrideName(const std::string_view value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (overrideName() != value) changed();
    if (override_name_text_) override_name_text_->text(value);
}

std::vector<std::string> EnemyCard::flagsInText() const {
    return override_name_text_ ? override_name_text_->flagsInText() : std::vector<std::string>{};
}

std::vector<std::string> EnemyCard::stepsInText() const {
    return override_name_text_ ? override_name_text_->stepsInText() : std::vector<std::string>{};
}

std::vector<std::string> EnemyCard::variantsInText() const {
    return override_name_text_ ? override_name_text_->variantsInText() : std::vector<std::string>{};
}

void EnemyCard::isOverrideImage(const bool value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (override_image_ != value) changed();
    override_image_ = value;
}

CardImageArray EnemyCard::overrideImages() const { return copyCardImages(nullptr, override_images_); }

void EnemyCard::overrideImages(const CardImageArray& images) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (sameCardImages(override_images_, images)) return;
    changed();
    for (const auto& image : override_images_) if (image) image->removeUseCounter();
    override_images_ = copyCardImages(static_cast<CWXPath*>(this), images);
    for (const auto& image : override_images_) {
        if (image && use_counter_) image->setUseCounter(use_counter_);
    }
}

std::string EnemyCard::connectedFile() const {
    for (const auto& image : override_images_) {
        const auto path = imagePath(image);
        if (!path.empty() && !::cwxeditor::cwx::utils::isBinImg(path)) return path;
    }
    return {};
}

CWXPath* EnemyCard::connectedResource(card::CastOwner* summary) noexcept {
    // Dは有効なCastOwnerを必要とする。nullのネイティブポインタ
    // ーを参照せず、有効な所有元にはDと同じcwCast(id)検索を使う。
    if (!summary) return nullptr;
    const auto card = summary->cwCast(id());
    return card.get();
}

void EnemyCard::setUseCounter(const CounterOwner& counter, CWXPath* owner) noexcept {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    user_.setUseCounter(counter);
    if (override_name_text_) {
        override_name_text_->setUseCounter(counter ? counter->global() : CounterOwner{},
                                          optionOwner(owner));
    }
    for (const auto& image : override_images_) {
        if (image) image->setUseCounter(counter);
    }
    AbstractSpCard::setUseCounter(counter, owner);
}

void EnemyCard::removeUseCounter() noexcept {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    user_.removeUseCounter();
    if (override_name_text_) override_name_text_->removeUseCounter();
    for (const auto& image : override_images_) if (image) image->removeUseCounter();
    AbstractSpCard::removeUseCounter();
}

void EnemyCard::toNodeImpl(XNode& element, const XMLOption& option) const {
    auto property = element.newElement("Property");
    property.newElement("Id", id());
    if (isOverrideName() || !overrideName().empty()) {
        auto name = property.newElement("Name", std::string_view(overrideName()));
        newTextAttr(name, "override", fromBool(isOverrideName()));
    }
    if (isOverrideImage() || !override_images_.empty()) {
        auto images = CardImage::toNode(property, override_images_, false, true);
        newTextAttr(images, "override", fromBool(isOverrideImage()));
    }
    bool all_actions = true;
    for (const auto& [type, value] : actions_) {
        if (type != ActionCardType::RunAway && !value) all_actions = false;
    }
    if (all_actions || !option.isTargetVersion("4")) {
        newTextAttr(element, "escape", fromBool(action(ActionCardType::RunAway)));
    }
    if (!all_actions) {
        auto actions = property.newElement("Actions");
        for (const auto& [type, value] : actions_) {
            const auto default_value = type != ActionCardType::RunAway;
            if (value != default_value) {
                auto action_node = actions.newElement("Action", fromBool(value));
                action_node.newAttr("id", static_cast<std::int32_t>(type));
            }
        }
    }
    appendProp(element, property, option);
    appendEvents(element, events_, events_comment_, option);
}

XNode EnemyCard::toNode(const XMLOption& option) const {
    auto element = XNode::create(XML_NAME);
    toNodeImpl(element, option);
    return element;
}

XNode EnemyCard::toNode(XNode& parent, const XMLOption& option) const {
    if (parent.name() != XML_NAME_M) throw AreaException("Node is not EnemyCards");
    auto element = parent.newElement(XML_NAME);
    toNodeImpl(element, option);
    return element;
}

EnemyCardPtr EnemyCard::createFromNode(XNode& node, const XMLInfo& version) {
    if (node.name() != XML_NAME) throw AreaException("Node is not EnemyCard");
    bool have_id = false;
    std::int64_t id = 0;
    std::string flag;
    std::int32_t x = 0, y = 0, layer = LAYER_MENU_CARD, animation_speed = -1;
    std::uint32_t scale = 100;
    std::string card_group;
    bool override_name = false, override_image = false;
    std::string name;
    CardImageArray images;
    ActionMap actions;
    EventTreeArray events;
    std::string events_comment;
    const auto escape = node.attr("escape", false);
    if (!escape.empty()) actions[ActionCardType::RunAway] = parseBool(escape);
    node.onTag["Property"] = [&](XNode& property) {
        property.onTag["Id"] = [&](XNode& value) {
            // Dはulongをlongへ読み込み、演算せずにulongと
            // して渡し直す。この中間値の全64ビットを明示的に保持する。
            id = std::bit_cast<std::int64_t>(parseNumber<std::uint64_t>(value.value()));
            have_id = true;
        };
        property.onTag["Name"] = [&](XNode& value) {
            name = value.value();
            override_name = false;
            if (value.hasAttr("override")) {
                const auto parsed = ::cwxeditor::cwx::utils::detail::parseDBool(value.attr("override", true));
                if (!parsed) throw AreaException("Invalid bool: " + value.attr("override", true));
                override_name = *parsed;
            }
        };
        CardImage::setOnTag(property, images, override_image, false);
        property.onTag["Actions"] = [&](XNode& action_list) {
            action_list.onTag["Action"] = [&](XNode& action_node) {
                const auto type = static_cast<ActionCardType>(
                    parseNumber<std::int32_t>(action_node.attr("id", true)));
                actions[type] = parseBool(action_node.value());
            };
            action_list.parse();
        };
        loadProp(property, flag, x, y, scale, layer, card_group, animation_speed);
    };
    node.onTag["Events"] = [&](XNode& value) {
        events = loadEvents(value, events_comment, version);
    };
    node.parse();
    if (!have_id) throw AreaException("EnemyCard ID not found");
    auto result = std::make_shared<EnemyCard>(static_cast<std::uint64_t>(id), actions, flag, x, y, scale, layer,
                                              card_group, animation_speed, override_name,
                                              name, override_image, images);
    result->addAll(events);
    result->comment(node.attr("comment", false));
    result->commentForEvents(events_comment);
    return result;
}

EnemyCardArray EnemyCard::createCardsFromNode(XNode& node, const XMLInfo& version) {
    EnemyCardArray result;
    if (node.name() == XML_NAME_M) {
        node.onTag["EnemyCard"] = [&](XNode& card) {
            result.push_back(createFromNode(card, version));
        };
        node.parse();
        return result;
    }
    if (node.name() != card::CastCard::XML_NAME_M)
        throw AreaException("Node is neither EnemyCards nor CastCards");

    // DのEnemyCard.createCardsFromNodeは、クリップボードや
    // ドロップのCastCardsを、新しく配置する戦闘カードへ変換する。引き継ぐのはProperty/Id
    // だけで、配置・上書き・行動の各値はEnemyCardの既定値から始める。
    // CastCard専用のデータをBattle XMLへ混入させないため。
    node.onTag["CastCard"] = [&](XNode& cast) {
        const auto property = cast.child("Property", false);
        if (!property.valid()) return;
        const auto id_node = property.child("Id", false);
        if (!id_node.valid() || id_node.value().empty()) return;
        const auto id = parseNumber<std::uint64_t>(id_node.value());
        result.push_back(std::make_shared<EnemyCard>(
            id, ActionMap{}, "", 0, 0, 100U, LAYER_MENU_CARD, "", -1,
            false, "", false, CardImageArray{}));
    };
    node.parse();
    return result;
}

MenuCard::MenuCard(const std::string_view name, const bool expand_special_chars,
                   const CardImageArray& images, const std::string_view description,
                   const std::string_view flag, const std::int32_t x,
                   const std::int32_t y, const std::uint32_t scale,
                   const std::int32_t layer, const std::string_view card_group,
                   const std::int32_t animation_speed)
    : AbstractSpCard(flag, x, y, scale, layer, card_group, animation_speed),
      name_text_(std::make_unique<SimpleTextHolder>(selfOwner(), TextHolderType::CardName,
                                                    "name")),
      description_(description), expand_special_chars_(expand_special_chars) {
    name_text_->changeHandler(guardedChangeHandler(this, lifetime_));
    name_text_->text(name);
    paths(images);
}

MenuCard::~MenuCard() {
    invalidateLifetime();
    if (name_text_) name_text_->changeHandler({});
    owner_.setRaw(nullptr);
    owner_raw_ = nullptr;
}

void MenuCard::attachOwner(Area* owner) {
    owner_raw_ = owner;
    attachOwnerLink(owner_, owner, owner ? owner->lifetimeToken()
                                         : std::weak_ptr<path::OwnerLifetimeToken>{});
}

std::string MenuCard::cwxPath(const bool id_value) {
    const auto parent = owner_.lock();
    if (!parent) return {};
    const auto pathOwner = ownerPath(owner_);
    if (!pathOwner) return {};
    const auto index = indexOf(parent->cards(), this);
    return cpjoin(pathOwner->cwxPath(id_value), "menucard", index);
}

::cwxeditor::lib::Observer<CWXPath> MenuCard::cwxParent() {
    return ownerObserver(owner_);
}

AbstractSpCardPtr MenuCard::shallowCopy() const {
    auto result = std::make_shared<MenuCard>(name(), expand_special_chars_, paths_, desc(),
                                             flag(), x(), y(), scale(), layer(), cardGroup(),
                                             animationSpeed());
    result->comment(comment());
    return result;
}

AbstractSpCardPtr MenuCard::dup() const {
    auto result = shallowCopy();
    for (const auto& event : events_) if (event) result->add(event->dup());
    result->commentForEvents(events_comment_);
    return result;
}

void MenuCard::shallowCopyFrom(const AbstractSpCard& base) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    [[maybe_unused]] const auto donorKeepAlive =
        const_cast<AbstractSpCard&>(base).retainForCallback();
    const auto* card = dynamic_cast<const MenuCard*>(&base);
    if (!card) throw AreaException("copyBase is not MenuCard");
    AbstractSpCard::shallowCopyFrom(base);
    name(card->name());
    desc(card->desc());
    paths(card->paths_);
    expandSPChars(card->expand_special_chars_);
}

std::vector<std::size_t> MenuCard::areaPath() const {
    const auto parent = owner_.lock();
    if (!parent) return {};
    const auto index = indexOf(parent->cards(), this);
    return {index + 2U};
}

AbstractArea* MenuCard::abstractOwner() noexcept { return owner_.lock().get(); }
const AbstractArea* MenuCard::abstractOwner() const noexcept { return owner_.lock().get(); }

std::string MenuCard::name() const { return name_text_ ? name_text_->text() : std::string{}; }

void MenuCard::name(const std::string_view value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (name() != value) changed();
    if (name_text_) name_text_->text(value);
}

void MenuCard::expandSPChars(const bool value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (expand_special_chars_ == value) return;
    changed();
    expand_special_chars_ = value;
    if (!name_text_) return;
    if (value && use_counter_) name_text_->setUseCounter(use_counter_->global(), optionOwner(uc_owner_));
    else name_text_->removeUseCounter();
}

std::vector<std::string> MenuCard::flagsInText() const {
    return expand_special_chars_ && name_text_ ? name_text_->flagsInText()
                                               : std::vector<std::string>{};
}

std::vector<std::string> MenuCard::stepsInText() const {
    return expand_special_chars_ && name_text_ ? name_text_->stepsInText()
                                               : std::vector<std::string>{};
}

std::vector<std::string> MenuCard::variantsInText() const {
    return expand_special_chars_ && name_text_ ? name_text_->variantsInText()
                                               : std::vector<std::string>{};
}

void MenuCard::desc(std::string value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (description_ != value) changed();
    description_ = std::move(value);
}

CardImageArray MenuCard::paths() const { return copyCardImages(nullptr, paths_); }

void MenuCard::paths(const CardImageArray& value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (sameCardImages(paths_, value)) return;
    changed();
    for (const auto& image : paths_) if (image) image->removeUseCounter();
    paths_ = copyCardImages(static_cast<CWXPath*>(this), value);
    for (const auto& image : paths_) {
        if (image && use_counter_) image->setUseCounter(use_counter_);
    }
}

std::string MenuCard::connectedFile() const {
    for (const auto& image : paths_) {
        const auto path = imagePath(image);
        if (!path.empty() && !::cwxeditor::cwx::utils::isBinImg(path)) return path;
    }
    return {};
}

void MenuCard::setUseCounter(const CounterOwner& counter, CWXPath* owner) noexcept {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    for (const auto& image : paths_) if (image) image->setUseCounter(counter);
    if (name_text_ && expand_special_chars_) name_text_->setUseCounter(counter ? counter->global() : CounterOwner{}, optionOwner(owner));
    AbstractSpCard::setUseCounter(counter, owner);
}

void MenuCard::removeUseCounter() noexcept {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    for (const auto& image : paths_) if (image) image->removeUseCounter();
    if (name_text_) name_text_->removeUseCounter();
    AbstractSpCard::removeUseCounter();
}

void MenuCard::toNodeImpl(XNode& element, const XMLOption& option) const {
    auto property = element.newElement("Property");
    auto name = property.newElement("Name", std::string_view(this->name()));
    // D: newAttr("spchars", expandSPChars) は to!string(bool) = "true"(fromBool の "True" ではない)。
    if (expand_special_chars_) newTextAttr(name, "spchars", "true");
    (void)CardImage::toNode(property, paths_);
    newTextElement(property, "Description", encodeLf(description_));
    appendProp(element, property, option);
    appendEvents(element, events_, events_comment_, option);
}

XNode MenuCard::toNode(const XMLOption& option) const {
    auto element = XNode::create(XML_NAME);
    toNodeImpl(element, option);
    return element;
}

XNode MenuCard::toNode(XNode& parent, const XMLOption& option) const {
    if (parent.name() != XML_NAME_M) throw AreaException("Node is not MenuCards");
    auto element = parent.newElement(XML_NAME);
    toNodeImpl(element, option);
    return element;
}

MenuCardPtr MenuCard::createFromNode(XNode& node, const XMLInfo& version) {
    if (node.name() != XML_NAME) throw AreaException("Node is not MenuCard");
    std::string name;
    bool expand = false;
    std::string description;
    CardImageArray images;
    std::string flag, card_group;
    std::int32_t x = 0, y = 0, layer = LAYER_MENU_CARD, animation_speed = -1;
    std::uint32_t scale = 100;
    EventTreeArray events;
    std::string events_comment;
    node.onTag["Property"] = [&](XNode& property) {
        property.onTag["Name"] = [&](XNode& value) {
            name = value.value();
            expand = value.attr<bool>("spchars", false, false);
        };
        // DのCardImage.setOnTagは、Propertyノードから従来
        // の単一ImagePathと、複数画像のImagePathsの両形式を読む。
        CardImage::setOnTag(property, images, false);
        property.onTag["Description"] = [&](XNode& value) { description = decodeLf2(value.value()); };
        loadProp(property, flag, x, y, scale, layer, card_group, animation_speed);
    };
    node.onTag["Events"] = [&](XNode& value) {
        events = loadEvents(value, events_comment, version);
    };
    node.parse();
    auto result = std::make_shared<MenuCard>(name, expand, images, description, flag, x, y,
                                             scale, layer, card_group, animation_speed);
    result->comment(node.attr("comment", false));
    result->addAll(events);
    result->commentForEvents(events_comment);
    return result;
}

MenuCardArray MenuCard::createFromCardNode(XNode& node, const bool copy_description,
                                           const XMLInfo& version) {
    MenuCardArray result;
    auto parse = [&](XNode& value) {
        auto property = value.child("Property", false);
        if (!property.valid()) return;
        bool create = false;
        std::string name, description;
        CardImageArray images;
        property.onTag["Name"] = [&](XNode& n) {
            name = n.value();
            create = true;
        };
        CardImage::setOnTag(property, images, false);
        if (copy_description) property.onTag["Description"] = [&](XNode& n) { description = decodeLf2(n.value()); };
        property.parse();
        if (create) {
            result.push_back(std::make_shared<MenuCard>(name, false, images,
                                                        description, "", 0, 0, 100,
                                                        LAYER_MENU_CARD, "", -1));
        }
    };
    if (node.child("Property", false).valid()) parse(node);
    else {
        node.onTag[""] = parse;
        node.onTag["*"] = parse;
        node.parse();
    }
    return result;
}

AbstractArea::AbstractArea(const std::uint64_t id, const std::string_view name)
    : id_(id), obj_id_(areaObjectId(this, "AbstractArea")), name_(name) {}

AbstractArea::~AbstractArea() {
    const usecounter::detail::NativeCounterRetirement native_retirement;
    invalidateLifetime();
    for (const auto& tree : events_) {
        detachEventTreeIfOwned(tree, this, true);
    }
}

::cwxeditor::lib::Owner<CWXPath> AbstractArea::retainForCallback() {
    return std::static_pointer_cast<CWXPath>(weak_from_this().lock());
}

::cwxeditor::lib::Owner<CWXPath> AbstractArea::selfOwner() const {
    return safePathOwner(const_cast<AbstractArea*>(this), lifetime_);
}

::cwxeditor::lib::Owner<CWXPath> AbstractArea::ownerHandle() const {
    return selfOwner();
}

void AbstractArea::changed() {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    // 資源はSummaryから切り離されていても、自身の
    // 変更状態を保持する必要がある。saveChangedOnly
    // はこのフラグでArea/Battle XMLを
    // 書き込むか判断するため、子のコールバックはSummary
    // へ直接渡さず、この資源を経由させる。
    changed_ = true;
    if (change_) change_();
}

void AbstractArea::id(const std::uint64_t value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (id_ != value) changed();
    id_ = value;
}

void AbstractArea::name(std::string value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (name_ != value) changed();
    name_ = std::move(value);
}

std::string AbstractArea::toDirName(const std::string_view value) {
    const auto index = value.find_last_of('\\');
    return index == std::string_view::npos ? std::string{} : std::string(value.substr(0, index));
}

std::string AbstractArea::dirName() const { return toDirName(name_); }

void AbstractArea::dirName(std::string value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (!value.empty() && value.back() != '\\') value.push_back('\\');
    name(value + baseName());
}

std::string AbstractArea::toBaseName(const std::string_view value) {
    const auto index = value.find_last_of('\\');
    return index == std::string_view::npos ? std::string(value)
                                           : std::string(value.substr(index + 1U));
}

std::string AbstractArea::baseName() const { return toBaseName(name_); }

void AbstractArea::baseName(std::string value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    value.erase(std::remove(value.begin(), value.end(), '\\'), value.end());
    const auto separator = name_.find_last_of('\\');
    name(separator == std::string::npos ? std::move(value) : name_.substr(0, separator + 1) + value);
}

EventTreePtr AbstractArea::etFromPath(const std::vector<std::size_t>& path) {
    if (path.size() == 1U && path.front() < events_.size()) return events_[path.front()];
    return {};
}

EventCounterBinding AbstractArea::eventCounterBinding() const {
    return {use_counter_, nonOwning(uc_owner_)};
}

void AbstractArea::add(const EventTreePtr& event) {
    ::cwxeditor::cwx::event::EventTreeOwner::add(event);
}

void AbstractArea::addAll(const EventTreeArray& events) {
    ::cwxeditor::cwx::event::EventTreeOwner::addAll(events);
}

void AbstractArea::insert(const std::size_t index, const EventTreePtr& event) {
    ::cwxeditor::cwx::event::EventTreeOwner::insert(index, event);
}

void AbstractArea::removeEvent(const std::size_t index) {
    ::cwxeditor::cwx::event::EventTreeOwner::removeEvent(index);
}

void AbstractArea::remove(const EventTreePtr& event) {
    ::cwxeditor::cwx::event::EventTreeOwner::remove(event);
}

void AbstractArea::swapEventTree(const std::size_t first, const std::size_t second) {
    ::cwxeditor::cwx::event::EventTreeOwner::swapEventTree(first, second);
}

void AbstractArea::clearEvents() {
    ::cwxeditor::cwx::event::EventTreeOwner::clearEvents();
}

bool AbstractArea::isEmpty() const {
    return std::all_of(events_.begin(), events_.end(), [](const auto& event) {
        return !event || event->isEmpty();
    });
}

void AbstractArea::commentForEvents(std::string value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (events_comment_ == value) return;
    changed();
    events_comment_ = std::move(value);
}

int AbstractArea::opCmp(const AbstractArea& other) const noexcept {
    if (id_ < other.id_) return -1;
    if (id_ > other.id_) return 1;
    return 0;
}

std::size_t AbstractArea::toHash() const noexcept {
    std::size_t hash = 0;
    for (const unsigned char value : rootName()) hash = hash * 37U + value;
    return hash * 37U + static_cast<std::size_t>(id_);
}

bool AbstractArea::opEquals(const AbstractArea& other) const noexcept {
    return this == &other;
}

void AbstractArea::comment(std::string value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (comment_ == value) return;
    changed();
    comment_ = std::move(value);
}

std::string AbstractArea::toXML(const XMLOption& option) const {
    return toNode(option).text();
}

XNode AbstractArea::toNode(const XMLOption& option) const {
    auto element = XNode::create(rootName());
    newTextAttr(element, "dataVersion", option.dataVersion);
    toNodeImpl(element, option);
    return element;
}

XNode AbstractArea::toNode(XNode& parent, const XMLOption& option,
                           const std::string_view parent_path,
                           const std::string_view cut_path) const {
    auto element = parent.newElement(rootName());
    newTextAttr(element, "dataVersion", option.dataVersion);
    toNodeImpl(element, option, parent_path, cut_path);
    return element;
}

void AbstractArea::appendProp(XNode& node, XNode& property, const XMLOption&,
                              const std::string_view parent_path,
                              const std::string_view cut_path) const {
    if (!comment_.empty()) newTextAttr(node, "comment", comment_);
    assert(property.name() == "Property");
    property.newElement("Id", id_);
    std::string output = name_;
    if (!cut_path.empty() && ::cwxeditor::cwx::utils::istartsWith(
        output, std::string(cut_path) + '\\')) {
        output.erase(0U, cut_path.size() + 1U);
    }
    if (!parent_path.empty()) output = std::string(parent_path) + '\\' + output;
    newTextElement(property, "Name", output);
}

void AbstractArea::loadProp(XNode& node, std::uint64_t& id, std::string& name,
                            std::string& comment) {
    bool have_id = false;
    std::string id_string;
    name.clear();
    comment = node.attr("comment", false);
    node.onTag["Property"] = [&](XNode& property) {
        property.onTag["Id"] = [&](XNode& value) {
            id_string = value.value();
            have_id = true;
        };
        property.onTag["Name"] = [&](XNode& value) { name = value.value(); };
        property.parse();
    };
    node.parse();
    if (!have_id) throw AreaException("Id not found");
    id = parseNumber<std::uint64_t>(id_string);
}

::cwxeditor::lib::Owner<CWXPath> AbstractArea::findCWXPath(const std::string_view path) {
    if (cpempty(path)) return selfOwner();
    if (cpcategory(path) == "event") {
        const auto index = cpindex(path);
        return index < events_.size() && events_[index]
                   ? events_[index]->findCWXPath(cpbottom(path))
                   : ::cwxeditor::lib::Owner<CWXPath>{};
    }
    return {};
}

::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> AbstractArea::cwxChilds() {
    ::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> result;
    for (const auto& event : events_) if (event) result.emplace_back(event);
    return result;
}

::cwxeditor::lib::Observer<CWXPath> AbstractArea::cwxParent() { return {}; }

void AbstractArea::changeHandler(ChangeHandler handler) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto retainedEvents = events_;
    for (const auto& event : retainedEvents) installEventTreeCallback(event, this);
    change_ = std::move(handler);
}

void AbstractArea::setUseCounter(const CounterOwner& counter,
                                 CWXPath* counter_owner) noexcept {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto retainedEvents = events_;
    for (const auto& event : retainedEvents) if (event) {
        event->setUseCounter(counter, nonOwning(counter_owner));
    }
    use_counter_ = counter;
    uc_owner_ = usecounter::renameInfo(counter_owner).get();
}

void AbstractArea::removeUseCounter() noexcept {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto retainedEvents = events_;
    for (const auto& event : retainedEvents) if (event) event->removeUseCounter();
    use_counter_.reset();
    uc_owner_ = nullptr;
}

PlayerCardEvents::PlayerCardEvents(AbstractArea* owner)
    : obj_id_(areaObjectId(this, "PlayerCardEvents")), owner_raw_(owner) {
    attachOwnerLink(owner_, owner, owner ? owner->lifetimeToken()
                                         : std::weak_ptr<path::OwnerLifetimeToken>{});
}

PlayerCardEvents::~PlayerCardEvents() {
    const usecounter::detail::NativeCounterRetirement native_retirement;
    invalidateLifetime();
    for (const auto& tree : events_) {
        detachEventTreeIfOwned(tree, this, true);
    }
}

::cwxeditor::lib::Owner<CWXPath> PlayerCardEvents::retainForCallback() {
    return std::static_pointer_cast<CWXPath>(weak_from_this().lock());
}

::cwxeditor::lib::Owner<CWXPath> PlayerCardEvents::ownerHandle() const {
    return safePathOwner(const_cast<PlayerCardEvents*>(this), lifetime_);
}

EventTreePtr PlayerCardEvents::etFromPath(const std::vector<std::size_t>& path) {
    return path.size() == 1U && path.front() < events_.size() ? events_[path.front()] : EventTreePtr{};
}

EventCounterBinding PlayerCardEvents::eventCounterBinding() const {
    return {use_counter_, nonOwning(uc_owner_)};
}

void PlayerCardEvents::add(const EventTreePtr& event) {
    ::cwxeditor::cwx::event::EventTreeOwner::add(event);
}

void PlayerCardEvents::addAll(const EventTreeArray& values) {
    ::cwxeditor::cwx::event::EventTreeOwner::addAll(values);
}

void PlayerCardEvents::insert(const std::size_t index, const EventTreePtr& event) {
    ::cwxeditor::cwx::event::EventTreeOwner::insert(index, event);
}

void PlayerCardEvents::removeEvent(const std::size_t index) {
    ::cwxeditor::cwx::event::EventTreeOwner::removeEvent(index);
}

void PlayerCardEvents::remove(const EventTreePtr& event) {
    ::cwxeditor::cwx::event::EventTreeOwner::remove(event);
}

void PlayerCardEvents::swapEventTree(const std::size_t first, const std::size_t second) {
    ::cwxeditor::cwx::event::EventTreeOwner::swapEventTree(first, second);
}

void PlayerCardEvents::clearEvents() {
    ::cwxeditor::cwx::event::EventTreeOwner::clearEvents();
}

bool PlayerCardEvents::isEmpty() const {
    return std::all_of(events_.begin(), events_.end(), [](const auto& event) {
        return !event || event->isEmpty();
    });
}

void PlayerCardEvents::commentForEvents(std::string value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (events_comment_ == value) return;
    changed();
    events_comment_ = std::move(value);
}

void PlayerCardEvents::changeHandler(ChangeHandler handler) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto retainedEvents = events_;
    for (const auto& event : retainedEvents) installEventTreeCallback(event, this);
    change_ = std::move(handler);
}

void PlayerCardEvents::changed() {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (change_) change_();
}

void PlayerCardEvents::setUseCounter(const CounterOwner& counter, CWXPath* owner) noexcept {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto retainedEvents = events_;
    for (const auto& event : retainedEvents) if (event) {
        event->setUseCounter(counter, nonOwning(owner));
    }
    use_counter_ = counter;
    uc_owner_ = usecounter::renameInfo(owner).get();
}

void PlayerCardEvents::removeUseCounter() noexcept {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto retainedEvents = events_;
    for (const auto& event : retainedEvents) if (event) event->removeUseCounter();
    use_counter_.reset();
    uc_owner_ = nullptr;
}

void PlayerCardEvents::toNode(XNode& element, const XMLOption& option) const {
    auto player_events = element.newElement("PlayerCardEvents");
    appendEvents(player_events, events_, events_comment_, option);
}

std::string PlayerCardEvents::cwxPath(const bool id) {
    const auto owner = owner_.lock();
    return owner ? cpjoin(owner->cwxPath(id), "playercard") : std::string{};
}

::cwxeditor::lib::Owner<CWXPath> PlayerCardEvents::findCWXPath(const std::string_view path) {
    if (cpempty(path)) return ownerHandle();
    if (cpcategory(path) == "event") {
        const auto index = cpindex(path);
        return index < events_.size() && events_[index]
                   ? events_[index]->findCWXPath(cpbottom(path))
                   : ::cwxeditor::lib::Owner<CWXPath>{};
    }
    return {};
}

::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> PlayerCardEvents::cwxChilds() {
    ::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> result;
    for (const auto& event : events_) if (event) result.emplace_back(event);
    return result;
}

::cwxeditor::lib::Observer<CWXPath> PlayerCardEvents::cwxParent() {
    if (owner_raw_) {
        const auto current = owner_.lock();
        if (current && !current.shared()) {
            attachOwnerLink(owner_, owner_raw_, owner_raw_->lifetimeToken());
        }
    }
    return ownerObserver(owner_);
}

Area::Area(const std::uint64_t id, const std::string_view name)
    : AbstractArea(id, name), player_events_(std::make_shared<PlayerCardEvents>(this)) {}

Area::~Area() {
    const usecounter::detail::NativeCounterRetirement native_retirement;
    invalidateLifetime();
    const auto retainedCards = cards_;
    for (const auto& card : retainedCards) {
        if (card && card->owner_raw_ == this) {
            card->changeHandler({});
            card->removeUseCounter();
            if (card->owner_raw_ == this) card->attachOwner(nullptr);
        }
    }
    const auto retainedBackgrounds = backgrounds_;
    for (const auto& background : retainedBackgrounds) {
        if (background && background->ownedBy(this, lifetimeToken())) {
            background->changeHandler({});
            background->removeUseCounter();
            if (background->ownedBy(this, lifetimeToken())) background->owner(nullptr);
        }
    }
    if (player_events_ && player_events_->owner_raw_ == this) {
        player_events_->changeHandler({});
        player_events_->owner_.setRaw(nullptr);
        player_events_->owner_raw_ = nullptr;
    }
}

void Area::changed() { AbstractArea::changed(); }

void Area::changeHandler(ChangeHandler handler) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto child_changed = guardedChangeHandler(this, lifetimeToken());
    const auto retainedBackgrounds = backgrounds_;
    for (const auto& background : retainedBackgrounds)
        if (background) background->changeHandler(child_changed);
    const auto retainedCards = cards_;
    for (const auto& card : retainedCards)
        if (card) card->changeHandler(child_changed);
    if (player_events_) player_events_->changeHandler(child_changed);
    AbstractArea::changeHandler(std::move(handler));
}

AbstractAreaPtr Area::dup() const {
    auto result = std::make_shared<Area>(id(), name());
    result->spAuto(spAuto());
    result->comment(comment());
    for (const auto& card : cards_) {
        if (card) {
            auto copy = std::dynamic_pointer_cast<MenuCard>(card->dup());
            if (copy) result->append(std::move(copy));
        }
    }
    const auto retainedBackgrounds = backgrounds_;
    for (const auto& background : retainedBackgrounds)
        if (background) result->append(background->dup());
    for (const auto& event : events_) if (event) result->add(event->dup());
    result->commentForEvents(events_comment_);
    if (player_events_) {
        for (const auto& event : player_events_->events()) if (event) {
            result->player_events_->add(event->dup());
        }
        result->player_events_->commentForEvents(player_events_->commentForEvents());
    }
    return result;
}

EventTreePtr Area::etFromPath(const std::vector<std::size_t>& path) {
    return areaEventFromPath(path, events_, player_events_, cards_);
}

void Area::swapCards(const std::size_t first, const std::size_t second) {
    if (first == second) return;
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    AbstractArea::changed();
    std::swap(cards_.at(first), cards_.at(second));
}

void Area::setCardIndices(const std::vector<std::pair<std::size_t, std::size_t>>& indices) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    assignAreaCardIndices(cards_, indices, [this] { AbstractArea::changed(); });
}

void Area::swapBacks(const std::size_t first, const std::size_t second) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (first != second) AbstractArea::changed();
    std::swap(backgrounds_.at(first), backgrounds_.at(second));
}

void Area::spAuto(const bool value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (auto_spread_ != value) AbstractArea::changed();
    auto_spread_ = value;
}

void Area::append(MenuCardPtr card) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    requireAreaObject(card);
    card->changeHandler(guardedChangeHandler(this, lifetimeToken()));
    if (use_counter_) card->setUseCounter(use_counter_, ucOwner());
    card->attachOwner(this);
    auto cards = snapshotSlice(cards_);
    cards.push_back(std::move(card));
    cards_ = ::cwxeditor::lib::SharedSlice<MenuCardPtr>(std::move(cards));
    AbstractArea::changed();
}

void Area::insert(const std::size_t index, MenuCardPtr card) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (index == cards_.size()) return append(std::move(card));
    requireAreaObject(card);
    card->changeHandler(guardedChangeHandler(this, lifetimeToken()));
    if (use_counter_) card->setUseCounter(use_counter_, ucOwner());
    card->attachOwner(this);
    if (index > cards_.size()) throw std::out_of_range("Area card insertion index");
    auto cards = snapshotSlice(cards_);
    cards.insert(cards.begin() + static_cast<std::ptrdiff_t>(index), std::move(card));
    cards_ = ::cwxeditor::lib::SharedSlice<MenuCardPtr>(std::move(cards));
    AbstractArea::changed();
}

void Area::removeCard(const std::size_t index) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    requireAreaObject(cards_.at(index)).changeHandler({});
    requireAreaObject(cards_.at(index)).removeUseCounter();
    requireAreaObject(cards_.at(index)).attachOwner(nullptr);
    auto cards = snapshotSlice(cards_);
    cards.erase(cards.begin() + static_cast<std::ptrdiff_t>(index));
    cards_ = ::cwxeditor::lib::SharedSlice<MenuCardPtr>(std::move(cards));
    AbstractArea::changed();
}

void Area::append(BgImagePtr back) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    requireAreaObject(back);
    back->changeHandler(guardedChangeHandler(this, lifetimeToken()));
    if (use_counter_) back->setUseCounter(use_counter_, ucOwner());
    back->owner(this, lifetimeToken());
    auto backgrounds = snapshotSlice(backgrounds_);
    backgrounds.push_back(std::move(back));
    backgrounds_ = ::cwxeditor::lib::SharedSlice<BgImagePtr>(std::move(backgrounds));
    AbstractArea::changed();
}

void Area::insert(const std::size_t index, BgImagePtr back) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (index == backgrounds_.size()) return append(std::move(back));
    requireAreaObject(back);
    back->changeHandler(guardedChangeHandler(this, lifetimeToken()));
    if (use_counter_) back->setUseCounter(use_counter_, ucOwner());
    back->owner(this, lifetimeToken());
    if (index > backgrounds_.size()) throw std::out_of_range("Area background insertion index");
    auto backgrounds = snapshotSlice(backgrounds_);
    backgrounds.insert(backgrounds.begin() + static_cast<std::ptrdiff_t>(index),
                       std::move(back));
    backgrounds_ = ::cwxeditor::lib::SharedSlice<BgImagePtr>(std::move(backgrounds));
    AbstractArea::changed();
}

void Area::set(const std::size_t index, BgImagePtr back) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    requireAreaObject(backgrounds_.at(index)).changeHandler({});
    requireAreaObject(backgrounds_.at(index)).removeUseCounter();
    requireAreaObject(backgrounds_.at(index)).owner(nullptr);
    requireAreaObject(back);
    back->changeHandler(guardedChangeHandler(this, lifetimeToken()));
    if (use_counter_) back->setUseCounter(use_counter_, ucOwner());
    back->owner(this, lifetimeToken());
    backgrounds_.at(index) = std::move(back);
    AbstractArea::changed();
}

void Area::removeBgImage(const std::size_t index) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    requireAreaObject(backgrounds_.at(index)).changeHandler({});
    requireAreaObject(backgrounds_.at(index)).removeUseCounter();
    requireAreaObject(backgrounds_.at(index)).owner(nullptr);
    auto backgrounds = snapshotSlice(backgrounds_);
    backgrounds.erase(backgrounds.begin() + static_cast<std::ptrdiff_t>(index));
    backgrounds_ = ::cwxeditor::lib::SharedSlice<BgImagePtr>(std::move(backgrounds));
    AbstractArea::changed();
}

void Area::setUseCounter(const CounterOwner& counter, CWXPath* owner) noexcept {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto retainedCards = cards_;
    for (const auto& card : retainedCards)
        if (card) card->setUseCounter(counter, owner);
    const auto retainedBackgrounds = backgrounds_;
    for (const auto& back : retainedBackgrounds)
        if (back) back->setUseCounter(counter, owner);
    if (player_events_) player_events_->setUseCounter(counter, owner);
    AbstractArea::setUseCounter(counter, owner);
}

void Area::removeUseCounter() noexcept {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto retainedCards = cards_;
    for (const auto& card : retainedCards)
        if (card) card->removeUseCounter();
    const auto retainedBackgrounds = backgrounds_;
    for (const auto& back : retainedBackgrounds)
        if (back) back->removeUseCounter();
    if (player_events_) player_events_->removeUseCounter();
    AbstractArea::removeUseCounter();
}

void Area::toNodeImpl(XNode& element, const XMLOption& option,
                      const std::string_view parent_path,
                      const std::string_view cut_path) const {
    auto property = element.newElement("Property");
    appendProp(element, property, option, parent_path, cut_path);
    requireAreaObject(player_events_).toNode(element, option);
    const auto backgroundSnapshot = snapshotSlice(backgrounds_);
    BgImage::toNode(backgroundSnapshot, true, element, option);
    auto cards = element.newElement("MenuCards");
    newTextAttr(cards, "spreadtype", auto_spread_ ? "Auto" : "Custom");
    for (const auto& card : cards_) requireAreaObject(card).toNode(cards, option);
    appendEvents(element, events_, events_comment_, option);
}

AreaPtr Area::loadFromXML(const std::string_view path, const XMLInfo& version) {
    auto document = XNode::parse(readTextFile(path));
    if (document.name() != XML_NAME) throw AreaException("File is not area");
    return createFromNode(document, version);
}

AreaPtr Area::createFromNode(XNode& node, const XMLInfo& version) {
    if (node.name() != XML_NAME) throw AreaException("Node is not area: " + node.name());
    std::uint64_t id = 0;
    std::string name, comment;
    bool spread = false;
    BgImageArray backgrounds;
    MenuCardArray cards;
    EventTreeArray events;
    EventTreeArray player_events;
    std::string events_comment;
    std::string player_events_comment;
    loadProp(node, id, name, comment);
    node.onTag["MenuCards"] = [&](XNode& value) {
        spread = value.attr("spreadtype", false) == "Auto";
        value.onTag["MenuCard"] = [&](XNode& card) { cards.push_back(MenuCard::createFromNode(card, version)); };
        value.parse();
    };
    node.onTag["BgImages"] = [&](XNode& value) {
        backgrounds = BgImage::bgImagesFromNode(value, true, version);
    };
    node.onTag["PlayerCardEvents"] = [&](XNode& value) {
        value.onTag["Events"] = [&](XNode& event_node) {
            player_events = loadEvents(event_node, player_events_comment, version);
        };
        value.parse();
    };
    node.onTag["Events"] = [&](XNode& value) {
        events = loadEvents(value, events_comment, version);
    };
    node.parse();
    auto result = std::make_shared<Area>(id, name);
    result->comment(comment);
    result->spAuto(spread);
    for (auto& card : cards) result->append(std::move(card));
    for (auto& background : backgrounds) result->append(std::move(background));
    result->addAll(events);
    result->commentForEvents(events_comment);
    result->player_events_->addAll(player_events);
    result->player_events_->commentForEvents(player_events_comment);
    return result;
}

std::string Area::CtoXML(const MenuCardArray& cards, const XMLOption& option) {
    return CtoNode(cards, option).text();
}

XNode Area::CtoNode(const MenuCardArray& cards, const XMLOption& option) {
    return CBtoNode(cards, {}, option);
}

std::string Area::BtoXML(const BgImageArray& backs, const XMLOption& option) {
    return BtoNode(backs, option).text();
}

XNode Area::BtoNode(const BgImageArray& backs, const XMLOption& option) {
    return CBtoNode({}, backs, option);
}

std::string Area::CBtoXML(const MenuCardArray& cards, const BgImageArray& backs,
                          const XMLOption& option) {
    return CBtoNode(cards, backs, option).text();
}

XNode Area::CBtoNode(const MenuCardArray& cards, const BgImageArray& backs,
                     const XMLOption& option) {
    auto element = XNode::create("MenuCardsAndBgImages");
    if (!cards.empty()) {
        auto card_nodes = element.newElement("MenuCards");
        for (const auto& card : cards) requireAreaObject(card).toNode(card_nodes, option);
    }
    if (!backs.empty()) BgImage::toNode(backs, false, element, option);
    return element;
}

bool Area::CBfromXML(const std::string_view xml, MenuCardArray& cards,
                      BgImageArray& backs, const XMLInfo& version) {
    cards.clear();
    backs.clear();
    return readAreaClipboard([&] {
        auto document = XNode::parse(xml);
        return CBfromXML(document, cards, backs, version);
    });
}

bool Area::CBfromXML(XNode& node, MenuCardArray& cards, BgImageArray& backs,
                      const XMLInfo& version) {
    cards.clear();
    backs.clear();
    return readAreaClipboard([&] {
        if (node.name() != "MenuCardsAndBgImages") return false;
        node.onTag["MenuCards"] = [&](XNode& value) {
            value.onTag["MenuCard"] = [&](XNode& card) { cards.push_back(MenuCard::createFromNode(card, version)); };
            value.parse();
        };
        node.onTag["BgImages"] = [&](XNode& value) {
            const auto load_cell = [&](XNode& cell) {
                backs.push_back(BgImage::createFromNode(cell, version));
            };
            value.onTag[""] = load_cell;
            value.onTag["*"] = load_cell;
            value.parse();
        };
        node.parse();
        return true;
    });
}

std::string Area::cwxPath(const bool id_value) {
    const auto parent = owner_.lock();
    if (!parent) return {};
    const auto parentPath = ownerPath(owner_);
    if (!parentPath) return {};
    if (id_value) return cpjoinid(parentPath->cwxPath(true), "area", id());
    const auto index = indexOf(parent->areas(), this);
    return cpjoin(parentPath->cwxPath(false), "area", index);
}

::cwxeditor::lib::Owner<CWXPath> Area::findCWXPath(const std::string_view path) {
    if (cpempty(path)) return ownerHandle();
    const auto category = cpcategory(path);
    if (category == "playercard") return requireAreaObject(player_events_).findCWXPath(cpbottom(path));
    if (category == "menucard") {
        const auto index = cpindex(path);
        if (index >= cards_.size()) return nullptr;
        return requireAreaObject(cards_[index]).findCWXPath(cpbottom(path));
    }
    if (category == "background") {
        const auto index = cpindex(path);
        if (index >= backgrounds_.size()) return nullptr;
        return requireAreaObject(backgrounds_[index]).findCWXPath(cpbottom(path));
    }
    return AbstractArea::findCWXPath(path);
}

::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> Area::cwxChilds() {
    ::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> result;
    result.emplace_back(player_events_ ? player_events_->ownerHandle() : nullptr);
    for (const auto& card : cards_) result.emplace_back(card ? card->ownerHandle() : nullptr);
    const auto retainedBackgrounds = backgrounds_;
    for (const auto& background : retainedBackgrounds) {
        result.emplace_back(background ? background->findCWXPath({}) : nullptr);
    }
    const auto events = AbstractArea::cwxChilds();
    result.insert(result.end(), events.begin(), events.end());
    return result;
}

::cwxeditor::lib::Observer<CWXPath> Area::cwxParent() {
    return ownerObserver(owner_);
}

Package::Package(const std::uint64_t id, const std::string_view name)
    : AbstractArea(id, name) {}

Package::~Package() { invalidateLifetime(); }

AbstractAreaPtr Package::dup() const {
    auto result = std::make_shared<Package>(id(), name());
    result->comment(comment());
    for (const auto& event : events_) if (event) result->add(event->dup());
    result->commentForEvents(events_comment_);
    return result;
}

EventTreePtr Package::etFromPath(const std::vector<std::size_t>& path) {
    if (path.size() > 1U && path.front() == 0U)
        return AbstractArea::etFromPath({path[1]});
    // Dは上記のカテゴリ・インデックスのパスを受け付ける。この公開APIではDのア
    // サートや範囲外エラーを導入せず、C++の既存の単一インデックスによるCWL書
    // き込み経路と、不正な形式・インデックスに対する安全なnull結果を維持する。
    return AbstractArea::etFromPath(path);
}

void Package::toNodeImpl(XNode& element, const XMLOption& option,
                         const std::string_view parent_path,
                         const std::string_view cut_path) const {
    auto property = element.newElement("Property");
    appendProp(element, property, option, parent_path, cut_path);
    appendEvents(element, events_, events_comment_, option);
}

PackagePtr Package::loadFromXML(const std::string_view path, const XMLInfo& version) {
    auto document = XNode::parse(readTextFile(path));
    if (document.name() != XML_NAME) throw AreaException("File is not package");
    return createFromNode(document, version);
}

PackagePtr Package::createFromNode(XNode& node, const XMLInfo& version) {
    if (node.name() != XML_NAME) throw AreaException("Node is not package: " + node.name());
    std::uint64_t id = 0;
    std::string name, comment;
    EventTreeArray events;
    std::string events_comment;
    node.onTag["Events"] = [&](XNode& value) {
        events = loadEvents(value, events_comment, version);
    };
    loadProp(node, id, name, comment);
    auto result = std::make_shared<Package>(id, name);
    result->comment(comment);
    result->addAll(events);
    result->commentForEvents(events_comment);
    return result;
}

std::string Package::cwxPath(const bool id_value) {
    const auto parent = owner_.lock();
    if (!parent) return {};
    const auto parentPath = ownerPath(owner_);
    if (id_value) return cpjoinid(parentPath->cwxPath(true), "package", id());
    const auto index = indexOf(parent->packages(), this);
    return cpjoin(parentPath->cwxPath(false), "package", index);
}

::cwxeditor::lib::Observer<CWXPath> Package::cwxParent() {
    return ownerObserver(owner_);
}

Battle::Battle(const std::uint64_t id, const std::string_view name,
               const std::string_view music)
    : AbstractArea(id, name), music_(this),
      player_events_(std::make_shared<PlayerCardEvents>(this)) {
    music_.path(music);
}

Battle::~Battle() {
    const usecounter::detail::NativeCounterRetirement native_retirement;
    invalidateLifetime();
    const auto retainedCards = cards_;
    for (const auto& card : retainedCards) {
        if (card && card->owner_raw_ == this) {
            card->changeHandler({});
            card->removeUseCounter();
            if (card->owner_raw_ == this) card->attachOwner(nullptr);
        }
    }
    if (player_events_ && player_events_->owner_raw_ == this) {
        player_events_->changeHandler({});
        player_events_->owner_.setRaw(nullptr);
        player_events_->owner_raw_ = nullptr;
    }
}

void Battle::changeHandler(ChangeHandler handler) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto child_changed = guardedChangeHandler(this, lifetimeToken());
    const auto retainedCards = cards_;
    for (const auto& card : retainedCards) {
        if (card) card->changeHandler(child_changed);
    }
    if (player_events_) player_events_->changeHandler(child_changed);
    AbstractArea::changeHandler(std::move(handler));
}

AbstractAreaPtr Battle::dup() const {
    auto result = std::make_shared<Battle>(id(), name(), music());
    result->spAuto(spAuto());
    result->possibleToRunAway(possibleToRunAway());
    result->continueBGM(continueBGM());
    result->volume(volume());
    result->loopCount(loopCount());
    result->fadeIn(fadeIn());
    result->battleEndLoseBgTransition(battleEndLoseBgTransition());
    result->battleEndLoseBgTransitionSpeed(battleEndLoseBgTransitionSpeed());
    result->battleEndLoseBgDoAnime(battleEndLoseBgDoAnime());
    result->battleEndLoseBgIgnoreEffectBooster(battleEndLoseBgIgnoreEffectBooster());
    result->comment(comment());
    const auto retainedCards = cards_;
    for (const auto& card : retainedCards) {
        if (card) {
            auto copy = std::dynamic_pointer_cast<EnemyCard>(card->dup());
            if (copy) result->append(std::move(copy));
        }
    }
    for (const auto& event : events_) if (event) result->add(event->dup());
    result->commentForEvents(events_comment_);
    if (player_events_) {
        for (const auto& event : player_events_->events()) if (event) {
            result->player_events_->add(event->dup());
        }
        result->player_events_->commentForEvents(player_events_->commentForEvents());
    }
    return result;
}

EventTreePtr Battle::etFromPath(const std::vector<std::size_t>& path) {
    return areaEventFromPath(path, events_, player_events_, cards_);
}

void Battle::music(std::string value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (music_.path() != value) changed();
    music_.path(value);
}

void Battle::volume(const std::uint32_t value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (volume_ != value) changed();
    volume_ = value;
}

void Battle::loopCount(const std::uint32_t value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (loop_count_ != value) changed();
    loop_count_ = value;
}

void Battle::fadeIn(const std::uint32_t value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (fade_in_ != value) changed();
    fade_in_ = value;
}

void Battle::continueBGM(const bool value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (continue_bgm_ != value) changed();
    continue_bgm_ = value;
}

void Battle::append(EnemyCardPtr card) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    requireAreaObject(card);
    card->changeHandler(guardedChangeHandler(this, lifetimeToken()));
    if (use_counter_) card->setUseCounter(use_counter_, ucOwner());
    card->attachOwner(this);
    auto cards = snapshotSlice(cards_);
    cards.push_back(std::move(card));
    cards_ = ::cwxeditor::lib::SharedSlice<EnemyCardPtr>(std::move(cards));
    changed();
}

void Battle::insert(const std::size_t index, EnemyCardPtr card) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (index == cards_.size()) return append(std::move(card));
    requireAreaObject(card);
    card->changeHandler(guardedChangeHandler(this, lifetimeToken()));
    if (use_counter_) card->setUseCounter(use_counter_, ucOwner());
    card->attachOwner(this);
    if (index > cards_.size()) throw std::out_of_range("Battle card insertion index");
    auto cards = snapshotSlice(cards_);
    cards.insert(cards.begin() + static_cast<std::ptrdiff_t>(index), std::move(card));
    cards_ = ::cwxeditor::lib::SharedSlice<EnemyCardPtr>(std::move(cards));
    changed();
}

void Battle::removeCard(const std::size_t index) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    requireAreaObject(cards_.at(index)).changeHandler({});
    requireAreaObject(cards_.at(index)).removeUseCounter();
    requireAreaObject(cards_.at(index)).attachOwner(nullptr);
    auto cards = snapshotSlice(cards_);
    cards.erase(cards.begin() + static_cast<std::ptrdiff_t>(index));
    cards_ = ::cwxeditor::lib::SharedSlice<EnemyCardPtr>(std::move(cards));
    changed();
}

void Battle::swapCards(const std::size_t first, const std::size_t second) {
    if (first == second) return;
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    changed();
    std::swap(cards_.at(first), cards_.at(second));
}

void Battle::setCardIndices(const std::vector<std::pair<std::size_t, std::size_t>>& indices) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    assignAreaCardIndices(cards_, indices, [this] { changed(); });
}

void Battle::spAuto(const bool value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (auto_spread_ != value) changed();
    auto_spread_ = value;
}

void Battle::possibleToRunAway(const bool value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (possible_to_run_away_ != value) changed();
    possible_to_run_away_ = value;
}

void Battle::battleEndLoseBgTransition(const Transition value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (transition_ != value) changed();
    transition_ = value;
}

void Battle::battleEndLoseBgTransitionSpeed(const std::int32_t value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (transition_speed_ != value) changed();
    transition_speed_ = value;
}

void Battle::battleEndLoseBgDoAnime(const bool value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (do_anime_ != value) changed();
    do_anime_ = value;
}

void Battle::battleEndLoseBgIgnoreEffectBooster(const bool value) {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    if (ignore_effect_booster_ != value) changed();
    ignore_effect_booster_ = value;
}

void Battle::setUseCounter(const CounterOwner& counter, CWXPath* owner) noexcept {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto retainedCards = cards_;
    for (const auto& card : retainedCards)
        if (card) card->setUseCounter(counter, owner);
    music_.setUseCounter(counter);
    if (player_events_) player_events_->setUseCounter(counter, owner);
    AbstractArea::setUseCounter(counter, owner);
}

void Battle::removeUseCounter() noexcept {
    [[maybe_unused]] const auto keepAlive = retainForCallback();
    const auto retainedCards = cards_;
    for (const auto& card : retainedCards)
        if (card) card->removeUseCounter();
    music_.removeUseCounter();
    if (player_events_) player_events_->removeUseCounter();
    AbstractArea::removeUseCounter();
}

void Battle::toNodeImpl(XNode& element, const XMLOption& option,
                        const std::string_view parent_path,
                        const std::string_view cut_path) const {
    auto property = element.newElement("Property");
    appendProp(element, property, option, parent_path, cut_path);
    if (!possible_to_run_away_) newTextElement(property, "RunAway", fromBool(false));
    if (transition_ != Transition::None || transition_speed_ != 5 || do_anime_ || !ignore_effect_booster_) {
        auto setting = property.newElement("Setting");
        if (transition_ != Transition::None) newTextAttr(setting, "battleEndLoseBgTransition", ::cwxeditor::cwx::types::fromTransition(transition_));
        if (transition_speed_ != 5) setting.newAttr("battleEndLoseBgTransitionSpeed", transition_speed_);
        if (do_anime_) newTextAttr(setting, "battleEndLoseBgDoAnime", fromBool(true));
        if (!ignore_effect_booster_) newTextAttr(setting, "battleEndLoseBgIgnoreEffectBooster", fromBool(false));
    }
    if (player_events_) player_events_->toNode(element, option);
    auto music = property.newElement("MusicPath", encodePath(this->music()));
    if (volume_ != 100) music.newAttr("volume", volume_);
    if (loop_count_ != 0) music.newAttr("loopcount", loop_count_);
    if (fade_in_ != 0) music.newAttr("fadein", fade_in_);
    if (continue_bgm_) newTextAttr(music, "continue", fromBool(true));
    auto cards = element.newElement("EnemyCards");
    newTextAttr(cards, "spreadtype", auto_spread_ ? "Auto" : "Custom");
    for (const auto& card : cards_) if (card) card->toNode(cards, option);
    appendEvents(element, events_, events_comment_, option);
}

BattlePtr Battle::loadFromXML(const std::string_view path, const XMLInfo& version) {
    auto document = XNode::parse(readTextFile(path));
    if (document.name() != XML_NAME) throw AreaException("File is not battle");
    return createFromNode(document, version);
}

BattlePtr Battle::createFromNode(XNode& node, const XMLInfo& version) {
    if (node.name() != XML_NAME) throw AreaException("Node is not battle: " + node.name());
    std::uint64_t id = 0;
    std::string name, music, comment;
    bool spread = false, possible_run = true, continue_bgm = false;
    std::uint32_t volume = 100, loop_count = 0, fade_in = 0;
    Transition transition = Transition::None;
    std::int32_t transition_speed = 5;
    bool do_anime = false, ignore_booster = true;
    EnemyCardArray cards;
    EventTreeArray events;
    EventTreeArray player_events;
    std::string events_comment;
    std::string player_events_comment;
    loadProp(node, id, name, comment);
    node.onTag["EnemyCards"] = [&](XNode& value) {
        spread = value.attr("spreadtype", false) == "Auto";
        value.onTag["EnemyCard"] = [&](XNode& card) { cards.push_back(EnemyCard::createFromNode(card, version)); };
        value.parse();
    };
    node.onTag["PlayerCardEvents"] = [&](XNode& value) {
        value.onTag["Events"] = [&](XNode& event_node) {
            player_events = loadEvents(event_node, player_events_comment, version);
        };
        value.parse();
    };
    node.onTag["Events"] = [&](XNode& value) {
        events = loadEvents(value, events_comment, version);
    };
    node.onTag["Property"] = [&](XNode& property) {
        property.onTag["MusicPath"] = [&](XNode& value) {
            music = decodePath(value.value());
            volume = value.attr<std::uint32_t>("volume", false, 100);
            loop_count = value.attr<std::uint32_t>("loopcount", false, 0);
            fade_in = value.attr<std::uint32_t>("fadein", false, 0);
            continue_bgm = value.attr<bool>("continue", false, false);
        };
        property.onTag["RunAway"] = [&](XNode& value) { possible_run = value.valueTo<bool>(); };
        property.onTag["Setting"] = [&](XNode& value) {
            const auto transition_name = value.attr("battleEndLoseBgTransition", false);
            if (!transition_name.empty()) transition = ::cwxeditor::cwx::types::toTransition(transition_name);
            transition_speed = value.attr<std::int32_t>("battleEndLoseBgTransitionSpeed", false, 5);
            do_anime = parseBool(value.attr("battleEndLoseBgDoAnime", false, "False"));
            ignore_booster = parseBool(value.attr("battleEndLoseBgIgnoreEffectBooster", false, "True"));
        };
        property.parse();
    };
    node.parse();
    auto result = std::make_shared<Battle>(id, name, music);
    result->comment(comment);
    result->spAuto(spread);
    result->possibleToRunAway(possible_run);
    result->volume(volume);
    result->loopCount(loop_count);
    result->fadeIn(fade_in);
    result->continueBGM(continue_bgm);
    result->battleEndLoseBgTransition(transition);
    result->battleEndLoseBgTransitionSpeed(transition_speed);
    result->battleEndLoseBgDoAnime(do_anime);
    result->battleEndLoseBgIgnoreEffectBooster(ignore_booster);
    for (auto& card : cards) result->append(std::move(card));
    result->addAll(events);
    result->commentForEvents(events_comment);
    result->player_events_->addAll(player_events);
    result->player_events_->commentForEvents(player_events_comment);
    return result;
}

std::string Battle::CtoXML(const EnemyCardArray& cards, const XMLOption& option) {
    return CtoNode(cards, option).text();
}

XNode Battle::CtoNode(const EnemyCardArray& cards, const XMLOption& option) {
    // ネイティブのvectorを使う呼び出し側は、D形式の記述子を準備する。直列化は一つの経路で行う。
    return CtoNode(::cwxeditor::lib::SharedSlice<const EnemyCardPtr>(cards), option);
}

std::string Battle::CtoXML(::cwxeditor::lib::SharedSlice<const EnemyCardPtr> cards,
                           const XMLOption& option) {
    return CtoNode(cards, option).text();
}

XNode Battle::CtoNode(::cwxeditor::lib::SharedSlice<const EnemyCardPtr> cards,
                       const XMLOption& option) {
    auto element = XNode::create("EnemyCards");
    for (const auto& card : cards) requireAreaObject(card).toNode(element, option);
    return element;
}

bool Battle::CfromXML(const std::string_view xml, EnemyCardArray& cards,
                       const XMLInfo& version) {
    cards.clear();
    return readAreaClipboard([&] {
        auto document = XNode::parse(xml);
        return CfromXML(document, cards, version);
    });
}

bool Battle::CfromXML(XNode& node, EnemyCardArray& cards,
                       const XMLInfo& version) {
    cards.clear();
    return readAreaClipboard([&] {
        if (node.name() != "EnemyCards") return false;
        node.onTag["EnemyCard"] = [&](XNode& card) { cards.push_back(EnemyCard::createFromNode(card, version)); };
        node.parse();
        return true;
    });
}

std::string Battle::cwxPath(const bool id_value) {
    const auto parent = owner_.lock();
    if (!parent) return {};
    const auto parentPath = ownerPath(owner_);
    if (!parentPath) return {};
    if (id_value) return cpjoinid(parentPath->cwxPath(true), "battle", id());
    const auto index = indexOf(parent->battles(), this);
    return cpjoin(parentPath->cwxPath(false), "battle", index);
}

::cwxeditor::lib::Owner<CWXPath> Battle::findCWXPath(const std::string_view path) {
    if (cpempty(path)) return ownerHandle();
    const auto category = cpcategory(path);
    if (category == "playercard") return player_events_->findCWXPath(cpbottom(path));
    if (category == "enemycard") {
        const auto index = cpindex(path);
        return index < cards_.size() && cards_[index] ? cards_[index]->findCWXPath(cpbottom(path)) : nullptr;
    }
    return AbstractArea::findCWXPath(path);
}

::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> Battle::cwxChilds() {
    ::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> result;
    if (player_events_) result.push_back(player_events_->ownerHandle());
    for (const auto& card : cards_) if (card) result.push_back(card->ownerHandle());
    const auto events = AbstractArea::cwxChilds();
    result.insert(result.end(), events.begin(), events.end());
    return result;
}

::cwxeditor::lib::Observer<CWXPath> Battle::cwxParent() {
    return ownerObserver(owner_);
}

}  // namespace cwxeditor::cwx::area
