#include "cwxeditor_src/core/cwx/card.hpp"

#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

#include <bit>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace cwxeditor::cwx::card {
namespace {

template <typename T>
lib::Owner<T> nonOwning(T* value) {
    if (value == nullptr) return {};
    return lib::Owner<T>(value, [](T*) {});
}

lib::Owner<CwxPath> asPath(CwxPath* value) { return nonOwning(value); }

lib::Owner<CwxPath> asPath(const lib::Owner<CwxPath>& value) { return value; }

template <typename Pointer>
auto& requireCardObject(const Pointer& object) {
    // Dはこれらのメンバーを直接呼ぶ。項目を黙って落とさず、同じ時点
    // で失敗させつつ、nullのネイティブポインターの参照は避ける。
    if (!object) throw std::invalid_argument("Null card child");
    return *object;
}

template <typename OwnerType, typename Lookup>
lib::Owner<CwxPath> findLinkedCardPath(EffectCard& card, StringView pathValue, Lookup lookup) {
    const auto top = path::cwxTop(card.Card::findCWXPath({}));
    auto* owner = dynamic_cast<OwnerType*>(top.get());
    // Dはtopがこのカード種別を提供できなければnullを返す
    // 。提供できる場合は、リンク先がなくても参照が必須となる。
    if (!owner) return {};
    const auto target = (owner->*lookup)(card.linkId());
    return requireCardObject(target).findCWXPath(pathValue);
}

template <typename Collection, typename Visit>
void visitCardSlice(const Collection& collection, Visit visit) {
    // Dのforeachは、コールバックが所有元の配列を置換しても入力スライ
    // スを保持する。ネイティブのvectorでは削除・追加後の領域を維持でき
    // ないため、イテレーターを無効にせず、同じオブジェクト列を生存させる。
    const auto retained = collection;
    for (const auto& object : retained) visit(requireCardObject(object));
}

template <typename T>
lib::Owner<T> ownerFromShared(T* value) {
    if (value == nullptr) return {};
    return nonOwning(value);
}

template <typename T>
std::size_t enumIndex(T value) {
    return static_cast<std::size_t>(static_cast<std::underlying_type_t<T>>(value));
}

template <typename T, std::size_t Size>
T& cardArrayElement(T (&values)[Size], std::size_t index) {
    // Dの静的配列の添字アクセスは範囲検査を行う。ネイティブ配列の範囲
    // 外を読んだり、勝手に0や無操作にしたりせず、その失敗を報告する。
    if (index >= Size) throw std::out_of_range("Card array index");
    return values[index];
}

std::size_t effectEnhanceIndex(Enhance value) {
    const auto index = enumIndex(value);
    // Dの効果系カードの補正マップにはAvoid/Resist/Defenseだけがある。
    // 4要素のCastCardの補正配列と異なり、Actionも存在しないキーとして扱う。
    if (index == 0U || index >= 4U) throw std::out_of_range("Effect card enhance key");
    return index;
}

String removeCardMarker(StringView value, StringView marker) {
    String result;
    std::size_t offset = 0;
    for (auto found = value.find(marker, offset); found != StringView::npos;
         found = value.find(marker, offset)) {
        result.append(value.substr(offset, found - offset));
        offset = found + marker.size();
    }
    result.append(value.substr(offset));
    return result;
}

std::uint64_t parseUInt(StringView value, StringView what) {
    if (const auto parsed = utils::detail::parseDInteger<std::uint64_t>(value)) return *parsed;
    throw CardException("Invalid numeric " + String(what) + ": " + String(value));
}

std::int32_t parseInt(StringView value, StringView what) {
    // Dは不正なカード数値を引数・変換の失敗として扱う。こ
    // こでは対応するC++のカード領域の例外を報告する。
    if (const auto parsed = utils::detail::parseDInteger<std::int32_t>(value)) return *parsed;
    throw CardException("Invalid numeric " + String(what) + ": " + String(value));
}

double parseDouble(StringView value, StringView what) {
    try {
        std::size_t used = 0;
        const auto result = std::stod(String(value), &used);
        if (used != value.size()) throw std::invalid_argument("trailing");
        return result;
    } catch (...) {
        throw CardException("Invalid numeric " + String(what) + ": " + String(value));
    }
}

bool attrBool(const XNode& node, StringView name, bool fallback = false) {
    return utils::parseBool(node.attr(name, false, utils::fromBool(fallback)));
}

bool attrConvBool(const XNode& node, StringView name, bool fallback = false) {
    if (!node.hasAttr(name)) return fallback;
    if (const auto parsed = utils::detail::parseDBool(node.attr(name, true))) return *parsed;
    throw CardException("Invalid bool " + String(name) + ": " + node.attr(name, true));
}

std::uint32_t attrUInt(const XNode& node, StringView name,
                       std::uint32_t fallback = 0U) {
    if (!node.hasAttr(name)) return fallback;
    const auto value = node.attr(name, true);
    const auto parsed = parseUInt(value, name);
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw CardException("Invalid numeric " + String(name) + ": " + value);
    }
    return static_cast<std::uint32_t>(parsed);
}

std::int32_t attrInt(const XNode& node, StringView name,
                     std::int32_t fallback = 0) {
    const auto value = node.attr(name, false, String{});
    return value.empty() ? fallback : parseInt(value, name);
}

double attrDouble(const XNode& node, StringView name, double fallback = 0.0) {
    const auto value = node.attr(name, false, String{});
    return value.empty() ? fallback : parseDouble(value, name);
}

void setRootAttributes(XNode& node, const XMLOption& option) {
    node.newAttr("dataVersion", option.dataVersion);
    if (option.loadScaledImage) node.newAttr("scaledimage", true);
}

lib::Owner<XMLOption> optionOwner(const XMLOption& option) {
    return lib::Owner<XMLOption>(const_cast<XMLOption*>(&option), [](XMLOption*) {});
}

lib::Owner<CwxPath> cardPathOwner(CwxPath* value) { return nonOwning(value); }

template <typename T>
lib::Owner<CwxPath> cardPathOwner(const lib::Owner<T>& value) {
    if (!value) return {};
    return lib::Owner<CwxPath>(value,
                               static_cast<CwxPath*>(static_cast<Card*>(value.get())));
}

template <typename LeftRange, typename RightRange>
bool ptrEqual(const LeftRange& left, const RightRange& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto a = left[i];
        const auto b = right[i];
        // Dのクラスの等価判定は、nullも含め同一参照ならそれ以上比較しない。
        if (a == b) continue;
        if (!a || !b) return false;
        if constexpr (requires { a->opEquals(*b); }) {
            if (!a->opEquals(*b)) return false;
            if (typeid(*a) != typeid(*b) && !b->opEquals(*a)) return false;
        } else if (*a != *b) {
            return false;
        }
    }
    return true;
}

template <typename T>
void copyMotionArray(const lib::Array<lib::Owner<T>>& source,
                     lib::Array<lib::Owner<T>>& target) {
    target.clear();
    target.reserve(source.size());
    for (const auto& value : source) {
        if (value) target.push_back(value->dup());
    }
}

String joinObjectId(StringView type, const void* pointer, std::uint64_t serial) {
    std::ostringstream stream;
    stream << type << '-' << std::hex << reinterpret_cast<std::uintptr_t>(pointer)
           << '-' << std::dec << serial;
    return stream.str();
}

std::uint64_t nextObjectId() {
    static std::uint64_t serial = 0;
    return serial++;
}

template <typename OwnerType>
lib::Owner<CwxPath> ownerPath(const path::OwnerLink<OwnerType>& link) {
    const auto owner = link.lock();
    if (!owner) return {};
    if (owner.shared()) return std::static_pointer_cast<CwxPath>(owner.shared());
    auto* raw = owner.get();
    if (auto parent = static_cast<CwxPath*>(raw)->findCWXPath({})) return parent;
    return nonOwning(static_cast<CwxPath*>(raw));
}

template <typename OwnerType>
lib::Observer<CwxPath> ownerObserver(const path::OwnerLink<OwnerType>& link) {
    if (auto owner = link.observer().lock())
        return std::static_pointer_cast<CwxPath>(std::move(owner));
    return {};
}

}  // namespace

XMLOption::XMLOption(const System* system, String version)
    : sys(system), dataVersion(std::move(version)) {}

bool XMLOption::isTargetVersion(const StringView ver) const {
    return versioninfo::isTargetVersion(dataVersion, ver);
}

CardImage::CardImage(CwxPath* owner)
    : path_(std::make_shared<usecounter::PathUser>(owner)) {}

CardImage::CardImage(CwxPath* owner, const CardImage& base)
    : CardImage(owner) {
    type = base.type;
    switch (base.type) {
    case CardImageType::File:
        path_->path(base.path());
        position_ = base.positionType();
        break;
    case CardImageType::PCNumber:
        pc_number_ = base.pcNumber();
        break;
    case CardImageType::Talker:
        talker_ = base.talker();
        break;
    default:
        // Dはfinal switchを使う。未知の画像種別を空画像として扱わない。
        throw std::invalid_argument("Unknown card image type");
    }
}

CardImage::CardImage(String value, const CardImagePosition position)
    : CardImage(nullptr) {
    type = CardImageType::File;
    path_->path(value);
    position_ = position;
}

CardImage::CardImage(const std::uint32_t pcNumber) : CardImage(nullptr) {
    type = CardImageType::PCNumber;
    pc_number_ = pcNumber;
}

CardImage::CardImage(const Talker talker) : CardImage(nullptr) {
    type = CardImageType::Talker;
    talker_ = talker;
}

lib::Owner<UseCounter> CardImage::useCounter() const {
    return path_ ? path_->useCounter() : lib::Owner<UseCounter>{};
}

void CardImage::setUseCounter(const lib::Owner<UseCounter>& counter) {
    if (path_) path_->setUseCounter(counter);
}

void CardImage::removeUseCounter() {
    if (path_) path_->removeUseCounter();
}

String CardImage::path() const { return path_ ? path_->path() : String{}; }

bool CardImage::opEquals(const CardImage& other) const {
    return type == other.type && pcNumber() == other.pcNumber() &&
           path() == other.path() && talker() == other.talker() &&
           positionType() == other.positionType();
}

std::size_t CardImage::toHash() const {
    std::size_t hash = usecounter::toPathId(path()).toHash();
    hash = hash * 9U + enumIndex(type);
    hash = hash * 9U + pc_number_;
    hash = hash * 9U + enumIndex(talker_);
    hash = hash * 9U + enumIndex(position_);
    return hash;
}

String CardImage::cwxPath(const bool id) {
    return path_ ? path_->cwxPath(id) : String{};
}

lib::Owner<CwxPath> CardImage::findCWXPath(const StringView value) {
    return path_ ? path_->findCWXPath(value) : lib::Owner<CwxPath>{};
}

lib::Array<lib::Observer<CwxPath>> CardImage::cwxChilds() {
    return path_ ? path_->cwxChilds() : lib::Array<lib::Observer<CwxPath>>{};
}

lib::Observer<CwxPath> CardImage::cwxParent() {
    return path_ ? path_->cwxParent() : lib::Observer<CwxPath>{};
}

void CardImage::changed() {
    if (path_) path_->changed();
}

namespace {
void registerCardImageTags(XNode& node, lib::Array<lib::Owner<CardImage>>& paths,
                           bool* overrideImage, const bool attr) {
    if (overrideImage) *overrideImage = false;
    const auto convert = [&paths](String value, String position) {
        lib::Owner<CardImage> image;
        const auto endsWith = [](const String& text, const String& suffix) {
            return text.size() >= suffix.size() &&
                   text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        if (endsWith(value, "??" + types::fromTalker(Talker::Selected))) {
            image = std::make_shared<CardImage>(Talker::Selected);
        } else if (endsWith(value, "??" + types::fromTalker(Talker::Unselected))) {
            image = std::make_shared<CardImage>(Talker::Unselected);
        } else if (endsWith(value, "??" + types::fromTalker(Talker::Random))) {
            image = std::make_shared<CardImage>(Talker::Random);
        } else if (endsWith(value, "??" + types::fromTalker(Talker::Card))) {
            image = std::make_shared<CardImage>(Talker::Card);
        } else if (endsWith(value, "??" + types::fromTalker(Talker::Valued))) {
            image = std::make_shared<CardImage>(Talker::Valued);
        } else if (!value.empty()) {
            CardImagePosition pos = CardImagePosition::Default;
            if (position == "Center") pos = CardImagePosition::Center;
            else if (position == "TopLeft") pos = CardImagePosition::TopLeft;
            image = std::make_shared<CardImage>(utils::decodePath(std::move(value)), pos);
        }
        if (image) paths.push_back(std::move(image));
    };
    const auto addPc = [&paths](const XNode& n) {
        const auto parsed = parseUInt(n.value(), "PCNumber");
        if (parsed > std::numeric_limits<std::uint32_t>::max()) {
            throw CardException("Invalid numeric PCNumber: " + n.value());
        }
        paths.push_back(std::make_shared<CardImage>(
            static_cast<std::uint32_t>(parsed)));
    };
    if (attr) {
        convert(node.attr("path", false, ""), node.attr("positiontype", false, "Default"));
        const auto pc = attrUInt(node, "pcNumber", 0U);
        if (pc != 0U) paths.push_back(std::make_shared<CardImage>(pc));
    }
    const auto parsePathNode = [convert, overrideImage](XNode& n) {
        if (n.hasAttr("override")) {
            const auto value = attrConvBool(n, "override");
            if (overrideImage) *overrideImage = value;
        }
        convert(n.value(), n.attr("positiontype", false, "Default"));
    };
    // Dと同様に登録と配送は分ける。他のタグハンドラーや不正なPCNumber
    // の処理は、呼び出し側がparse()を実行したときだけ行う。
    if (!attr) {
        node.onTag["ImagePath"] = parsePathNode;
        node.onTag["PCNumber"] = addPc;
    }
    if (node.name() != "ImagePaths") {
        node.onTag["ImagePaths"] = [convert, addPc, overrideImage](XNode& n) {
            if (n.hasAttr("override")) {
                const auto value = attrConvBool(n, "override");
                if (overrideImage) *overrideImage = value;
            }
            n.onTag["ImagePath"] = [convert](XNode& image) {
                convert(image.value(), image.attr("positiontype", false, "Default"));
            };
            n.onTag["PCNumber"] = addPc;
            n.parse();
        };
    }
}
} // namespace

void CardImage::setOnTag(XNode& node, lib::Array<lib::Owner<CardImage>>& paths,
                         const bool attr) {
    // C++のコールバックは、破棄されたローカルboolへの参照を
    // 保持できない。値を捨てる場合も共通ハンドラーは各上書き属性
    // を検証し、Dのローカル出力引数のオーバーロードに合わせる。
    registerCardImageTags(node, paths, nullptr, attr);
}

void CardImage::setOnTag(XNode& node, lib::Array<lib::Owner<CardImage>>& paths,
                         bool& isOverrideImage, const bool attr) {
    registerCardImageTags(node, paths, &isOverrideImage, attr);
}

lib::Array<lib::Owner<CardImage>> CardImage::createFromNode(XNode& node) {
    lib::Array<lib::Owner<CardImage>> result;
    // DのコンテナーAPIに加え、C++の単一ImagePath用の簡易
    // APIを維持する。同じ登録済みImagePathハンドラーを使う。
    if (node.name() == "ImagePath") {
        auto container = XNode::create("ImagePaths");
        container.append(node);
        setOnTag(container, result, false);
        container.parse();
        return result;
    }
    const auto priorTags = node.onTag;
    const auto priorSnakeTags = node.on_tag;
    setOnTag(node, result, false);
    try { node.parse(); }
    catch (...) {
        // DのクロージャーはGCでローカル変数を保持する。C++では解析失敗を伝
        // 播する前に、このローカル結果を参照するハンドラーを解除する必要がある。
        node.onTag = priorTags;
        node.on_tag = priorSnakeTags;
        throw;
    }
    return result;
}

XNode CardImage::putToNode(XNode& parent, const lib::Owner<CardImage>& image,
                           const bool attr) {
    if (!image) {
        // Dは出力ノードを変更する前にpathを参照する。nullのネイテ
        // ィブ参照や勝手な空画像への置換を行わず、その時点で失敗させる。
        throw std::invalid_argument("Null card image");
    }
    switch (image->type) {
    case CardImageType::File: {
        XNode out = parent;
        if (!attr) out = parent.newElement("ImagePath", utils::encodePath(image->path()));
        else parent.newAttr("path", utils::encodePath(image->path()));
        if (image->positionType() == CardImagePosition::Center) out.newAttr("positiontype", "Center");
        else if (image->positionType() == CardImagePosition::TopLeft) out.newAttr("positiontype", "TopLeft");
        else if (image->positionType() != CardImagePosition::Default)
            throw CardException("Invalid card image position type");
        return out;
    }
    case CardImageType::PCNumber:
        if (attr) {
            parent.newAttr("path", "");
            parent.newAttr("pcNumber", image->pcNumber());
            return parent;
        } else {
            if (parent.name() != "ImagePaths") parent.newElement("ImagePath", "");
            return parent.newElement("PCNumber", image->pcNumber());
        }
    case CardImageType::Talker: {
        const auto value = String("Material/??") + types::fromTalker(image->talker());
        if (attr) {
            parent.newAttr("path", value);
            return parent;
        }
        return parent.newElement("ImagePath", value);
    }
    }
    // Dはfinal switchを使うため、不正なネイティブ
    // 列挙値で直列化に成功したノードを黙って返してはならない。
    throw CardException("Invalid card image type");
}

XNode CardImage::toNode(XNode& parent,
                        const lib::Array<lib::Owner<CardImage>>& paths,
                        const bool attr, const bool forceMulti) {
    if (paths.empty() && !forceMulti) {
        if (attr) {
            parent.newAttr("path", "");
            return parent;
        }
        return parent.newElement("ImagePath", "");
    }
    if (paths.size() <= 1U && !forceMulti) {
        return putToNode(parent, paths.front(), attr);
    }
    auto multi = parent.newElement("ImagePaths");
    for (const auto& image : paths) static_cast<void>(putToNode(multi, image, false));
    return multi;
}

XNode CardImage::toNode(const lib::Array<lib::Owner<CardImage>>& paths) {
    auto node = XNode::create("ImagePaths");
    for (const auto& image : paths) static_cast<void>(putToNode(node, image, false));
    return node;
}

Card::Card(const std::uint64_t id, String name,
           const lib::Array<lib::Owner<CardImage>>& paths, String desc)
    : id_(id),
      object_id_(joinObjectId("Card", this, nextObjectId())),
      name_(std::move(name)),
      desc_(std::move(desc)) {
    this->paths(paths);
}

void Card::shallowCopyCard(const Card& other) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    // Dはクラス値のコピー元引数を呼び出し全体で生存させる。ネイテ
    // ィブのconst参照はそうならないため、受信側のコールバック
    // がコピー元を解放すると、後続フィールドを読み取れなくなる。
    [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
    id(other.id());
    name(other.name());
    desc(other.desc());
    paths(other.paths());
    comment(other.comment());
}

bool Card::equalsExcludeIdCard(const Card& other) const {
    return name_ == other.name_ && desc_ == other.desc_ &&
           ptrEqual(paths_, other.paths_);
}

void Card::clearData() {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    name({});
    desc({});
    paths({});
}

ChangeHandler Card::changeHandler() const {
    const auto lifetime = cardLifetime();
    const auto observer = weak_from_this();
    return [this, lifetime, observer] {
        const auto token = lifetime.lock();
        if (!token || !token->alive.load(std::memory_order_acquire)) return;
        // Dのデリゲートは実行中の受信先を生存させる。共有カードでは実際のネ
        // イティブ所有元を保持し、スタック上のカードではトークンで保護する。
        const auto retained = observer.lock();
        const_cast<Card*>(retained ? retained.get() : this)->changed();
    };
}

bool Card::opEquals(const Card& other) const {
    if (typeid(*this) != typeid(other)) return false;
    if (const auto value = dynamic_cast<const CastCard*>(this)) return value->opEquals(static_cast<const CastCard&>(other));
    if (const auto value = dynamic_cast<const SkillCard*>(this)) return value->opEquals(static_cast<const SkillCard&>(other));
    if (const auto value = dynamic_cast<const ItemCard*>(this)) return value->opEquals(static_cast<const ItemCard&>(other));
    if (const auto value = dynamic_cast<const BeastCard*>(this)) return value->opEquals(static_cast<const BeastCard&>(other));
    if (const auto value = dynamic_cast<const ActionCard*>(this)) return value->opEquals(static_cast<const ActionCard&>(other));
    return baseEquals(other);
}

bool Card::baseEquals(const Card& other) const {
    return id_ == other.id_ && comment_ == other.comment_ &&
           equalsExcludeIdCard(other);
}

void Card::changeHandler(ChangeHandler change) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    if (!change) {
        change_ = {};
        return;
    }
    // 参照を返すネイティブのコールバックアクセサーは、安定したメンバーに結び付けたま
    // まにする。アダプターのコピー間では、Dのデリゲートのコンテキストを共有する。
    auto context = std::make_shared<ChangeHandler>(std::move(change));
    change_ = [context = std::move(context)] {
        // ネイティブのアクセサーを通した直接呼び出しで、このアダプターが置き換わる場合がある。
        const auto retained = context;
        (*retained)();
    };
}

void Card::changed() {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    // 変更を行う各呼び出し側も受信先を保持する。この局所的な保持は、呼
    // び出し側がDの「通知してから書き込む」処理を行う前に終わるため。
    // コピーするのはアダプターだけとし、共有コンテキストで可変状態を維
    // 持し、コールバック自身の置換中も実際のデリゲートを生存させる。
    const auto change = change_;
    if (change) {
        change();
        changed_ = true;
    }
}

void Card::setUseCounter(const lib::Owner<UseCounter>& counter,
                         const lib::Owner<CwxPath>& owner) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    use_counter_ = counter;
    use_counter_owner_ = usecounter::renameInfo(owner);
    for (const auto& image : paths_) {
        if (image) {
            if (counter) image->setUseCounter(counter);
            else image->removeUseCounter();
        }
    }
}

void Card::removeUseCounter() {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    use_counter_.reset();
    use_counter_owner_.reset();
    for (const auto& image : paths_) if (image) image->removeUseCounter();
}

void Card::id(const std::uint64_t value) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    if (id_ != value) changed();
    id_ = value;
}

void Card::name(String value) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    if (name_ != value) changed();
    name_ = std::move(value);
}

void Card::paths(const lib::Array<lib::Owner<CardImage>>& value) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    if (ptrEqual(paths_, value)) return;
    changed();
    for (const auto& image : paths_) if (image) image->removeUseCounter();
    paths_.clear();
    paths_.reserve(value.size());
    for (const auto& image : value) {
        if (!image) continue;
        auto copy = std::make_shared<CardImage>(static_cast<CwxPath*>(this), *image);
        if (use_counter_) copy->setUseCounter(use_counter_);
        paths_.push_back(std::move(copy));
    }
}

lib::Array<lib::Owner<CardImage>> Card::paths() const {
    lib::Array<lib::Owner<CardImage>> result;
    result.reserve(paths_.size());
    for (const auto& image : paths_) {
        if (image) result.push_back(std::make_shared<CardImage>(nullptr, *image));
    }
    return result;
}

String Card::connectedFile() const {
    for (const auto& image : paths_) {
        if (image && !image->path().empty() && !utils::isBinImg(image->path()))
            return image->path();
    }
    return {};
}

void Card::desc(String value) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    if (desc_ != value) changed();
    desc_ = std::move(value);
}

void Card::comment(String value) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    if (comment_ == value) return;
    changed();
    comment_ = std::move(value);
}

std::uint64_t Card::readId(XNode& node) {
    const auto property = node.child("Property", false);
    if (!property.valid()) return 0U;
    const auto idNode = property.child("Id", false);
    if (!idNode.valid() || idNode.value().empty()) return 0U;
    return parseUInt(idNode.value(), "Id");
}

std::uint64_t Card::readLinkInfo(XNode& node, std::uint64_t& linkId,
                                 bool& hold, String& comment) {
    linkId = 0U;
    hold = false;
    comment = node.attr("comment", false, "");
    auto property = node.child("Property", false);
    if (!property.valid()) return 0U;
    std::uint64_t id = 0U;
    for (const auto& child : property.children()) {
        if (child.name() == "Id") id = parseUInt(child.value(), "Id");
        else if (child.name() == "LinkId") linkId = parseUInt(child.value(), "LinkId");
        else if (child.name() == "Hold") hold = utils::parseBool(child.value());
    }
    return id;
}

XNode Card::setProp(XNode& node, const XMLOption*, const OverData* over) const {
    if (!comment_.empty()) node.newAttr("comment", comment_);
    auto property = node.newElement("Property");
    property.newElement("Id", over && over->id != 0U ? over->id : id_);
    property.newElement("Name", name_);
    static_cast<void>(CardImage::toNode(property, paths_, false, false));
    property.newElement("Description", utils::encodeLf(desc_));
    return property;
}

void Card::loadProp(XNode& node, XNode& property, const XMLInfo&, const bool loadId) {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    comment(node.attr("comment", false, ""));
    String idText;
    auto priorTags = property.onTag;
    auto priorSnakeTags = property.on_tag;
    lib::Array<lib::Owner<CardImage>> loadedPaths;
    try {
        if (loadId) property.onTag["Id"] = [&](XNode& child) { idText = child.value(); };
        CardImage::setOnTag(property, loadedPaths, false);
        name_.clear();
        property.onTag["Name"] = [&](XNode& child) { name_ = child.value(); };
        property.onTag["Description"] = [&](XNode& child) { desc_ = utils::decodeLf2(child.value()); };
        property.parse();
    } catch (...) {
        // DのクロージャーはGCでローカル状態を保持する。解析失敗を伝播する
        // 前に、この呼び出しの領域を参照するネイティブハンドラーを解除する。
        property.onTag = std::move(priorTags);
        property.on_tag = std::move(priorSnakeTags);
        throw;
    }
    paths(loadedPaths);
    if (name_.empty()) name_ = {};
    if (loadId) {
        if (idText.empty()) throw CardException("Id not found");
        id_ = parseUInt(idText, "Id");
    }
}

lib::Owner<CwxPath> Card::findCWXPath(const StringView value) {
    if (path::cpempty(value)) {
        try {
            const auto self = shared_from_this();
            return lib::Owner<CwxPath>(self, static_cast<CwxPath*>(this));
        } catch (const std::bad_weak_ptr&) {
            return nonOwning(static_cast<CwxPath*>(this));
        }
    }
    return {};
}

int Card::opCmp(const Card& other) const noexcept {
    return id_ < other.id_ ? -1 : (id_ > other.id_ ? 1 : 0);
}

std::size_t Card::toHash() const noexcept {
    // Dは動的なカード型と安定したカードIDの順にハッシュ
    // する。objectIdは確保時のアドレスを含み、イン
    // スタンスごとに一意なので、値のハッシュには含めない。
    std::size_t hash = typeid(*this).hash_code();
    return hash * 37U + std::hash<std::uint64_t>{}(id_);
}

KeyCodesUser::KeyCodesUser(CwxPath* owner) : cwx_path_(cardPathOwner(owner)) {}

KeyCodesUser::KeyCodesUser(const lib::Owner<CwxPath>& owner) : cwx_path_(owner) {}

String KeyCodesUser::cwxPath(const bool id) const {
    // 委譲先の呼び出し中も実際の共有モデルを生存させる。追加の生ポインター
    // ・スタック用コンストラクターでは、寿命の管理を呼び出し側に任せる。
    const auto owner = path::retainCallbackOwner(cwx_path_);
    return requireCardObject(owner).cwxPath(id);
}

void KeyCodesUser::changed() {
    if (change_) change_();
}

void KeyCodesUser::setUseCounter(const lib::Owner<UseCounter>& counter,
                                 const lib::Owner<CwxPath>& owner) {
    for (const auto& key : key_codes_) {
        if (key) {
              key->setUseCounter(counter, owner);
        }
    }
    use_counter_ = counter;
    use_counter_owner_ = usecounter::renameInfo(owner);
}

void KeyCodesUser::removeUseCounter() {
    for (const auto& key : key_codes_) if (key) key->removeUseCounter();
    use_counter_.reset();
    use_counter_owner_.reset();
}

void KeyCodesUser::keyCodes(const lib::Array<String>& values) {
    if (keyCodes() == values) return;
    changed();
    const auto counter = use_counter_;
    const auto counterOwner = use_counter_owner_.lock();
    for (const auto& key : key_codes_) if (key) key->removeUseCounter();
    key_codes_.clear();
    key_codes_.reserve(values.size());
    for (const auto& value : values) {
        auto key = std::make_shared<usecounter::KeyCodeUser>(cwx_path_);
        key->keyCode(value);
        if (counter) key->setUseCounter(counter, counterOwner);
        key_codes_.push_back(std::move(key));
    }
}

lib::Array<String> KeyCodesUser::keyCodes() const {
    lib::Array<String> result;
    result.reserve(key_codes_.size());
    for (const auto& key : key_codes_) if (key) result.push_back(key->keyCode());
    return result;
}

lib::Array<String> keyCodesFromNode(XNode& node, const XMLInfo&) {
    if (!node.valid() || node.name() != KEY_CODES_XML_NAME) return {};
    return utils::decodeLf(node.value(), true);
}

void keyCodesToNode(XNode& node, const lib::Array<String>& keyCodes) {
    node.newElement(KEY_CODES_XML_NAME, utils::encodeLf(keyCodes, true));
}

XNode keyCodesToNode(const lib::Array<String>& keyCodes) {
    return XNode::create(KEY_CODES_XML_NAME, utils::encodeLf(keyCodes, true));
}

String keyCodesToXML(const lib::Array<String>& keyCodes) {
    return keyCodesToNode(keyCodes).text();
}

EffectCard::EffectCard(const System* system, const std::uint64_t id,
                       String name,
                       const lib::Array<lib::Owner<CardImage>>& imagePaths,
                       String desc)
    : Card(id, std::move(name), imagePaths, std::move(desc)),
      sound1_(std::make_shared<usecounter::PathUser>(static_cast<CwxPath*>(static_cast<Card*>(this)))),
      sound2_(std::make_shared<usecounter::PathUser>(static_cast<CwxPath*>(static_cast<Card*>(this)))),
      key_codes_(std::make_shared<KeyCodesUser>(static_cast<CwxPath*>(static_cast<Card*>(this)))),
      external_variant_(std::make_shared<usecounter::VariantUser>(static_cast<CwxPath*>(static_cast<Card*>(this)))),
      motion_user_(static_cast<motion::MotionOwner*>(this)),
      event_owner_(static_cast<CwxPath*>(static_cast<Card*>(this))),
      flag_dir_root_(std::make_shared<flag::FlagDir>(
          lib::Owner<CwxPath>{},
          system ? system->localVariablePrefix() : String{})) {
    event_owner_.changeHandler([this] { changed(); });
}

EffectCard::~EffectCard() {
    // Motionのコールバックは組み込みのBeastCardへコピーさ
    // れる。EffectCardのメンバーの破棄が始まる前に無効化する。
    invalidateMotionLifetime();
    invalidateCardLifetime();
}

lib::Owner<flag::FlagDir> EffectCard::flagDirRoot() const {
    // 構築処理はmake_sharedが実際の所有元を設定する前に走る
    // 。公開時に利用可能になった実際の制御ブロックへ接続し、局所的に取
    // 得した所有元がカードを生存させる一方、ルートとカウンターは弱参照
    // に保つ。スタック所有のカードでは架空の共有所有元を公開しない。
    if (flag_dir_root_) {
        const auto self = const_cast<EffectCard*>(this)->Card::weak_from_this().lock();
        flag_dir_root_->localOwner(std::static_pointer_cast<CwxPath>(self));
    }
    return flag_dir_root_;
}

bool EffectCard::effectEquals(const EffectCard& other) const {
    const auto equalEvents = [&]() {
        return ptrEqual(event_owner_.trees(), other.event_owner_.trees());
    };
    return linkId() == other.linkId() && scenario_ == other.scenario_ &&
           author_ == other.author_ && physical_ == other.physical_ &&
           mental_ == other.mental_ && target_ == other.target_ &&
           all_range_ == other.all_range_ && spell_ == other.spell_ &&
           effect_type_ == other.effect_type_ && resist_ == other.resist_ &&
           success_rate_ == other.success_rate_ && visual_ == other.visual_ &&
           enhance(types::Enhance::Avoid) == other.enhance(types::Enhance::Avoid) &&
           enhance(types::Enhance::Resist) == other.enhance(types::Enhance::Resist) &&
           enhance(types::Enhance::Defense) == other.enhance(types::Enhance::Defense) &&
           soundPath1() == other.soundPath1() && volume1_ == other.volume1_ &&
           loop1_ == other.loop1_ && soundPath2() == other.soundPath2() &&
           volume2_ == other.volume2_ && loop2_ == other.loop2_ &&
           keyCodes() == other.keyCodes() && settings_comment_ == other.settings_comment_ &&
           external_variant_scope_ == other.external_variant_scope_ &&
           externalVariantWriteScenarioVar() == other.externalVariantWriteScenarioVar() &&
           external_variant_yado_ == other.external_variant_yado_ &&
           premium_ == other.premium_ &&
           ptrEqual(motions(), other.motions()) && equalEvents() &&
           ((!flag_dir_root_ && !other.flag_dir_root_) ||
            (flag_dir_root_ && other.flag_dir_root_ && *flag_dir_root_ == *other.flag_dir_root_));
}

bool EffectCard::opEqualsEffect(const EffectCard& other) const {
    // ここはDのsuper.opEqualsに対応する入口。派生カードを再度呼ん
    // でしまう、ネイティブCard::opEqualsの動的配送の入口ではない。
    if (linkId() != 0U) return linkId() == other.linkId();
    if (!baseEquals(other)) return false;
    return effectEquals(other);
}

bool EffectCard::equalsExcludeIdEffect(const EffectCard& other) const {
    if (linkId() != 0U) return linkId() == other.linkId();
    return equalsExcludeIdCard(other) && effectEquals(other);
}

void EffectCard::shallowCopyEffectCard(const EffectCard& other) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
    shallowCopyCard(other);
    linkId(other.linkId());
    scenario(other.scenario());
    author(other.author());
    physical(other.physical());
    mental(other.mental());
    target(other.target());
    allRange(other.allRange());
    spell(other.spell());
    effectType(other.effectType());
    resist(other.resist());
    successRate(other.successRate());
    visual(other.visual());
    enhance(Enhance::Avoid, other.enhance(Enhance::Avoid));
    enhance(Enhance::Resist, other.enhance(Enhance::Resist));
    enhance(Enhance::Defense, other.enhance(Enhance::Defense));
    soundPath1(other.soundPath1());
    volume1(other.volume1());
    loopCount1(other.loopCount1());
    soundPath2(other.soundPath2());
    volume2(other.volume2());
    loopCount2(other.loopCount2());
    keyCodes(other.keyCodes());
    settingsComment(other.settingsComment());
    externalVariantWriteScope(other.externalVariantWriteScope());
    externalVariantWriteScenarioVar(other.externalVariantWriteScenarioVar());
    externalVariantWriteYado(other.externalVariantWriteYado());
    premium(other.premium());
    lib::Array<lib::Owner<motion::Motion>> copied;
    for (const auto& value : other.motions()) copied.push_back(requireCardObject(value).dup());
    motions(copied);
}

void EffectCard::deepCopyEffectCard(const EffectCard& other) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
    shallowCopyEffectCard(other);
    visitCardSlice(other.event_owner_.trees(), [&](const auto& tree) { add(tree.dup()); });
    const auto sourceRoot = other.flagDirRoot();
    flag_dir_root_ = std::make_shared<flag::FlagDir>(
        std::static_pointer_cast<CwxPath>(Card::weak_from_this().lock()),
        requireCardObject(sourceRoot));
}

void EffectCard::clearData() {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    Card::clearData();
    event_owner_.clearEvents();
    linkId(0U);
    scenario({});
    author({});
    physical(Physical::Dex);
    mental(Mental::Aggressive);
    target(CardTarget::None);
    allRange(false);
    spell(false);
    effectType(EffectType::Physic);
    resist(Resist::Avoid);
    successRate(0);
    visual(CardVisual::None);
    enhance(Enhance::Avoid, 0);
    enhance(Enhance::Resist, 0);
    enhance(Enhance::Defense, 0);
    soundPath1({});
    volume1(100U);
    loopCount1(1U);
    soundPath2({});
    volume2(100U);
    loopCount2(1U);
    keyCodes({});
    settingsComment({});
    externalVariantWriteScope("False");
    externalVariantWriteScenarioVar({});
    externalVariantWriteYado(false);
    premium(Premium::Normal);
    motions({});
    requireCardObject(flag_dir_root_).removeAll();
}

void EffectCard::scenario(String value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (scenario_ != value) changed();
    scenario_ = std::move(value);
}
void EffectCard::author(String value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (author_ != value) changed();
    author_ = std::move(value);
}
void EffectCard::physical(const Physical value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (physical_ != value) changed();
    physical_ = value;
}
void EffectCard::mental(const Mental value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (mental_ != value) changed();
    mental_ = value;
}
void EffectCard::target(const CardTarget value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (target_ != value) changed();
    target_ = value;
}
void EffectCard::allRange(const bool value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (all_range_ != value) changed();
    all_range_ = value;
}
void EffectCard::spell(const bool value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (spell_ != value) changed();
    spell_ = value;
}
void EffectCard::effectType(const EffectType value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (effect_type_ != value) changed();
    effect_type_ = value;
}
void EffectCard::resist(const Resist value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (resist_ != value) changed();
    resist_ = value;
}
void EffectCard::successRate(const std::int32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (success_rate_ != value) changed();
    success_rate_ = value;
}
void EffectCard::visual(const CardVisual value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (visual_ != value) changed();
    visual_ = value;
}

std::int32_t EffectCard::enhance(const Enhance value) const {
    return enhance_[effectEnhanceIndex(value)];
}

void EffectCard::enhance(const Enhance value, const std::int32_t amount) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    const auto index = effectEnhanceIndex(value);
    if (enhance_[index] != amount) changed();
    enhance_[index] = amount;
}

String EffectCard::soundPath1() const { return sound1_ ? sound1_->path() : String{}; }
String EffectCard::soundPath2() const { return sound2_ ? sound2_->path() : String{}; }
void EffectCard::soundPath1(String value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (soundPath1() != value) changed();
    if (sound1_) sound1_->path(value);
}
void EffectCard::soundPath2(String value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (soundPath2() != value) changed();
    if (sound2_) sound2_->path(value);
}
void EffectCard::volume1(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (volume1_ != value) changed();
    volume1_ = value;
}
void EffectCard::volume2(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (volume2_ != value) changed();
    volume2_ = value;
}
void EffectCard::loopCount1(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (loop1_ != value) changed();
    loop1_ = value;
}
void EffectCard::loopCount2(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (loop2_ != value) changed();
    loop2_ = value;
}
lib::Array<String> EffectCard::keyCodes() const {
    return key_codes_ ? key_codes_->keyCodes() : lib::Array<String>{};
}
void EffectCard::keyCodes(const lib::Array<String>& value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (key_codes_) key_codes_->keyCodes(value);
}
void EffectCard::settingsComment(String value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (settings_comment_ != value) changed();
    settings_comment_ = std::move(value);
}
void EffectCard::externalVariantWriteScope(String value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (external_variant_scope_ != value) changed();
    external_variant_scope_ = std::move(value);
}
String EffectCard::externalVariantWriteScenarioVar() const {
    return external_variant_ ? external_variant_->variant() : String{};
}
void EffectCard::externalVariantWriteScenarioVar(String value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (external_variant_) external_variant_->variant(value);
}
void EffectCard::externalVariantWriteYado(const bool value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (external_variant_yado_ != value) changed();
    external_variant_yado_ = value;
}
String EffectCard::valueWriteScenarioAttr() const {
    if (external_variant_scope_ == "False") return {};
    if (external_variant_scope_ == "Scenario_in")
        return "Scenario_in_" + externalVariantWriteScenarioVar();
    return external_variant_scope_;
}
void EffectCard::valueWriteScenarioAttr(String value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    const auto text = enumutils::stripWhiteSpace(value);
    constexpr StringView prefix = "Scenario_in_";
    if (text.rfind(prefix, 0U) == 0U) {
        externalVariantWriteScope("Scenario_in");
        externalVariantWriteScenarioVar(text.substr(prefix.size()));
    } else {
        externalVariantWriteScope(text.empty() ? "False" : text);
        externalVariantWriteScenarioVar({});
    }
}
void EffectCard::loadExternalVariantWriteAttr(String value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    externalVariantWriteYado(value.find("[Yado]") != String::npos);
    // Dは重ならない二段階の置換を行う。各置換で新
    // たにつながった記号を、繰り返し除去しない。
    value = enumutils::stripWhiteSpace(removeCardMarker(removeCardMarker(value, ", [Yado]"), "[Yado]"));
    if (value == "True" || value == "False") {
        externalVariantWriteScope(value);
        externalVariantWriteScenarioVar({});
    } else if (value.size() >= 2U && value.front() == '[' && value.back() == ']') {
        value = value.substr(1U, value.size() - 2U);
        constexpr StringView prefix = "Scenario_in_";
        if (value.rfind(prefix, 0U) == 0U) {
            externalVariantWriteScope("Scenario_in");
            externalVariantWriteScenarioVar(value.substr(prefix.size()));
        } else {
            externalVariantWriteScope(value);
            externalVariantWriteScenarioVar({});
        }
    } else {
        externalVariantWriteScope(value.empty() ? "False" : value);
        externalVariantWriteScenarioVar({});
    }
}
void EffectCard::premium(const Premium value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (premium_ != value) changed();
    premium_ = value;
}
void EffectCard::motions(lib::SharedSlice<lib::Owner<motion::Motion>> value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    motion_user_.motions(std::move(value));
    // 異なる値への置換はMotionUserがこのハンドラーを通じ
    // て通知済み。同じ値や同じ重複値の場合は、観測だけにとどめる。
}
void EffectCard::motions(lib::Array<lib::Owner<motion::Motion>> value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    motions(lib::SharedSlice<lib::Owner<motion::Motion>>(std::move(value)));
}
void EffectCard::motions(std::initializer_list<lib::Owner<motion::Motion>> value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    motions(lib::SharedSlice<lib::Owner<motion::Motion>>(value));
}
void EffectCard::changeHandler(ChangeHandler change) {
    Card::changeHandler(change);
    // 呼び出し側が公開された所有権を保持すると、子モデルがこのカード
    // より長く生存する場合がある。Cardのトークンで保護したコール
    // バックを再利用し、通知中は実際の共有カード所有元も保持する。
    const auto cardChange = Card::changeHandler();
    event_owner_.changeHandler(cardChange);
    const auto lifetime = motionLifetimeToken();
    motion_user_.changeHandler([lifetime, cardChange] {
        const auto token = lifetime.lock();
        if (token && token->alive.load(std::memory_order_acquire)) cardChange();
    });
    for (const auto& tree : event_owner_.trees()) {
        if (tree) tree->changeHandler(cardChange);
    }
    if (flag_dir_root_) flag_dir_root_->changeHandler(cardChange);
}
void EffectCard::changed() { Card::changed(); }

void EffectCard::unbindEventTree(const lib::Owner<event::EventTree>& tree) {
    event::EventTreeOwner::detachEventTree(
        tree, static_cast<bool>(useCounter()), this, eventTreeLifetimeToken());
}

void EffectCard::setUseCounter(const lib::Owner<UseCounter>& counter,
                               const lib::Owner<CwxPath>& owner) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (!counter) {
        removeUseCounter();
        return;
    }
    // 効果系カードはローカルカウンターを所有し、そのグローバル側に
    // は所属パッケージやシナリオのカウンターを使う。Dはこの層を作
    // り、カード内のIDが兄弟カードのIDと衝突しないようにする。
    const auto self = std::static_pointer_cast<CwxPath>(Card::weak_from_this().lock());
    const auto localCounter = std::make_shared<UseCounter>(self, counter);
    if (flag_dir_root_) {
        flag_dir_root_->localOwner(self);
        for (const auto& value : flag_dir_root_->allFlags()) {
            if (value) localCounter->createID(usecounter::toFlagId(value->path()));
        }
        for (const auto& value : flag_dir_root_->allSteps()) {
            if (value) localCounter->createID(usecounter::toStepId(value->path()));
        }
        for (const auto& value : flag_dir_root_->allVariants()) {
            if (value) localCounter->createID(usecounter::toVariantId(value->path()));
        }
    }
    setUseCounterImpl(localCounter);
    const auto pathOwner = owner;
    event_use_counter_owner_ = pathOwner;
    event_owner_.setUseCounter(localCounter, pathOwner);
    motion_user_.setUseCounter(localCounter, pathOwner);
    if (sound1_) {
        sound1_->setUseCounter(localCounter);
    }
    if (sound2_) {
        sound2_->setUseCounter(localCounter);
    }
    if (key_codes_) key_codes_->setUseCounter(localCounter, pathOwner);
    if (external_variant_) {
        external_variant_->setUseCounter(localCounter);
    }
    if (flag_dir_root_) flag_dir_root_->useCounter(localCounter);
    Card::setUseCounter(localCounter, pathOwner);
}

void EffectCard::removeUseCounter() {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    removeUseCounterImpl();
    event_owner_.removeUseCounter();
    event_use_counter_owner_.reset();
    motion_user_.removeUseCounter();
    if (sound1_) sound1_->removeUseCounter();
    if (sound2_) sound2_->removeUseCounter();
    if (key_codes_) key_codes_->removeUseCounter();
    if (external_variant_) external_variant_->removeUseCounter();
    if (flag_dir_root_) flag_dir_root_->useCounter({});
    Card::removeUseCounter();
}

void EffectCard::add(const lib::Owner<event::EventTree>& tree) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    event_owner_.add(tree);
}
void EffectCard::insert(const std::size_t index,
                        const lib::Owner<event::EventTree>& tree) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    event_owner_.insert(index, tree);
}
void EffectCard::removeEvent(const std::size_t index) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    event_owner_.removeEvent(index);
}
void EffectCard::remove(const lib::Owner<event::EventTree>& tree) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    event_owner_.remove(tree);
}
void EffectCard::swapEventTree(const std::size_t a, const std::size_t b) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    event_owner_.swapEventTree(a, b);
}
bool EffectCard::isEmpty() const {
    return event_owner_.isEmpty();
}
lib::Owner<event::EventTree> EffectCard::etFromPath(const std::vector<std::size_t>& value) const {
    if (value.at(0) == 0U) {
        const auto index = value.at(1);
        if (index >= event_owner_.trees().size())
            throw std::out_of_range("array slice index");
        return event_owner_.eventTree(index);
    }
    throw std::out_of_range("Effect card event category");
}
void EffectCard::commentForEvents(String value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    event_owner_.commentForEvents(std::move(value));
}

lib::Owner<CwxPath> EffectCard::findCWXPath(const StringView value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    // Dは委譲先の呼び出しをまたいで、不変の文字列スライスを生存させる。
    const String input(value);
    if (path::cpempty(input)) return Card::findCWXPath(input);
    const auto category = path::cpcategory(input);
    if (category == "event") return event_owner_.findCWXPath(input);
    if (category == "motion") {
        const auto index = path::cpindex(input);
        if (index >= motions().size()) return {};
        const auto motion = motions().at(index);
        return requireCardObject(motion).findCWXPath(path::cpbottom(input));
    }
    if (category == "variable") {
        const auto root = flagDirRoot();
        return requireCardObject(root).findCWXPath(path::cpbottom(input));
    }
    return {};
}

lib::Array<lib::Observer<CwxPath>> EffectCard::cwxChilds() {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (linkId() != 0U) return {};
    lib::Array<lib::Observer<CwxPath>> result;
    // ネイティブのツリーAPIは弱参照の監視先を使う。Dのnull
    // 項目を結果から除外せず、空の監視先として順序も維持する。
    for (const auto& value : motions()) result.emplace_back(std::static_pointer_cast<CwxPath>(value));
    const auto events = event_owner_.cwxChilds();
    result.insert(result.end(), events.begin(), events.end());
    const auto root = flagDirRoot();
    const auto variables = requireCardObject(root).cwxChilds();
    result.insert(result.end(), variables.begin(), variables.end());
    return result;
}

XNode EffectCard::setEffProp(XNode& node, const XMLOption* option,
                             const OverData* over) const {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (linkId() != 0U && (!option || !option->includeCard)) {
        if (!comment().empty()) node.newAttr("comment", comment());
        auto property = node.newElement("Property");
        property.newElement("Id", over && over->id ? over->id : id());
        property.newElement("LinkId", over && over->linkId ? over->linkId : linkId());
        return property;
    }
    auto property = setProp(node, option, over);
    const auto value_write_scenario = valueWriteScenarioAttr();
    if (!settings_comment_.empty() || !value_write_scenario.empty() || external_variant_yado_) {
        auto setting = property.newElement("Setting");
        if (!settings_comment_.empty()) setting.newAttr("comment", settings_comment_);
        if (!value_write_scenario.empty()) setting.newAttr("valueWriteScenario", value_write_scenario);
        if (external_variant_yado_) setting.newAttr("valueWriteYado", utils::fromBool(external_variant_yado_));
    }
    property.newElement("LinkId", over && over->linkId ? over->linkId : linkId());
    property.newElement("Scenario", scenario_);
    property.newElement("Author", author_);
    auto ability = property.newElement("Ability");
    ability.newAttr("physical", types::fromPhysical(physical_));
    ability.newAttr("mental", types::fromMental(mental_));
    property.newElement("Target", types::fromCardTarget(target_)).newAttr("allrange", utils::fromBool(all_range_));
    property.newElement("EffectType", types::fromEffectType(effect_type_)).newAttr("spell", utils::fromBool(spell_));
    property.newElement("ResistType", types::fromResist(resist_));
    property.newElement("SuccessRate", success_rate_);
    property.newElement("VisualEffect", types::fromCardVisual(visual_));
    auto enhanceNode = property.newElement("Enhance");
    enhanceNode.newAttr("avoid", enhance(Enhance::Avoid));
    enhanceNode.newAttr("resist", enhance(Enhance::Resist));
    enhanceNode.newAttr("defense", enhance(Enhance::Defense));
    auto sound = property.newElement("SoundPath", utils::encodePath(soundPath1()));
    if (volume1_ != 100U) sound.newAttr("volume", volume1_);
    if (loop1_ != 1U) sound.newAttr("loopcount", loop1_);
    sound = property.newElement("SoundPath2", utils::encodePath(soundPath2()));
    if (volume2_ != 100U) sound.newAttr("volume", volume2_);
    if (loop2_ != 1U) sound.newAttr("loopcount", loop2_);
    // カードXMLでは末尾改行なしの形式を使う。単独の補助
    // 関数は、D向けの既定動作として末尾改行を維持する。
    property.newElement(KEY_CODES_XML_NAME, utils::encodeLf(keyCodes(), false));
    property.newElement("Premium", types::fromPremium(premium_));
    auto motionNode = node.newElement("Motions");
    for (const auto& value : motions()) {
        // Dはnullのオプションも渡す。非nullが必要なのは後続のlogicalSort
        // アクセスだけなので、それ以前のXML出力を抑止しない。
        static_cast<void>(requireCardObject(value).toNode(
            motionNode, option ? optionOwner(*option) : lib::Owner<XMLOption>{}));
    }
    event_owner_.appendEventsToNode(node, option);
    const auto root = flagDirRoot();
    if (!option) throw std::invalid_argument("Null effect-card XML option");
    requireCardObject(root).toNodeAll(node, option->logicalSort, false);
    return property;
}

void EffectCard::loadEffProp(XNode& node, XNode& property,
                             const XMLInfo& version, const bool loadId) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    // Dはここで同じタグのハンドラーを置き換え、その後、共通の基底読み
    // 込み処理でPropertyの全子要素を文書順に一度ずつ配送する。
    const auto retain_self = [this, self_owner]() -> EffectCard* {
        return self_owner ? static_cast<EffectCard*>(self_owner.get()) : this;
    };
    property.set_handler("LinkId", [retain_self](XNode& child) {
        retain_self()->linkId(parseUInt(child.value(), "LinkId"));
    });
    property.set_handler("Scenario", [retain_self](XNode& child) {
        retain_self()->scenario_ = child.value();
    });
    property.set_handler("Author", [retain_self](XNode& child) {
        retain_self()->author_ = child.value();
    });
    property.set_handler("Ability", [retain_self](XNode& child) {
        auto* self = retain_self();
        self->physical_ = types::toPhysical(child.attr("physical", true));
        self->mental_ = types::toMental(child.attr("mental", true));
    });
    property.set_handler("Target", [retain_self](XNode& child) {
        auto* self = retain_self();
        self->target_ = types::toCardTarget(child.value());
        self->all_range_ = utils::parseBool(child.attr("allrange", true));
    });
    property.set_handler("EffectType", [retain_self](XNode& child) {
        auto* self = retain_self();
        self->effect_type_ = types::toEffectType(child.value());
        self->spell_ = utils::parseBool(child.attr("spell", true));
    });
    property.set_handler("ResistType", [retain_self](XNode& child) {
        retain_self()->resist_ = types::toResist(child.value());
    });
    property.set_handler("SuccessRate", [retain_self](XNode& child) {
        retain_self()->success_rate_ = parseInt(child.value(), "SuccessRate");
    });
    property.set_handler("VisualEffect", [retain_self](XNode& child) {
        retain_self()->visual_ = types::toCardVisual(child.value());
    });
    property.set_handler("Enhance", [retain_self](XNode& child) {
        auto* self = retain_self();
        self->enhance_[1] = child.attr<std::int32_t>("avoid", true);
        self->enhance_[2] = child.attr<std::int32_t>("resist", true);
        self->enhance_[3] = child.attr<std::int32_t>("defense", true);
    });
    property.set_handler("SoundPath", [retain_self](XNode& child) {
        auto* self = retain_self();
        self->soundPath1(utils::decodePath(child.value()));
        self->volume1_ = attrUInt(child, "volume", 100U);
        self->loop1_ = attrUInt(child, "loopcount", 1U);
    });
    property.set_handler("SoundPath2", [retain_self](XNode& child) {
        auto* self = retain_self();
        self->soundPath2(utils::decodePath(child.value()));
        self->volume2_ = attrUInt(child, "volume", 100U);
        self->loop2_ = attrUInt(child, "loopcount", 1U);
    });
    property.set_handler("KeyCodes", [retain_self](XNode& child) {
        retain_self()->keyCodes(utils::decodeLf(child.value(), true));
    });
    property.set_handler("Premium", [retain_self](XNode& child) {
        retain_self()->premium(types::toPremium(child.value()));
    });
    property.set_handler("Setting", [retain_self](XNode& child) {
        auto* self = retain_self();
        self->settings_comment_ = child.attr("comment", false, "");
        self->valueWriteScenarioAttr(child.attr("valueWriteScenario", false, ""));
        self->external_variant_yado_ = attrBool(child, "valueWriteYado");
    });
    settings_comment_ = node.attr("comment", false, "");
    loadExternalVariantWriteAttr(node.attr("externalVariantWrite", false, "False"));
    if (flag_dir_root_) flag_dir_root_->fromXmlNode(node, version, false);
    loadProp(node, property, version, loadId);
}

void EffectCard::loadEffV(XNode& node, const XMLInfo& version) {
    const auto self_owner = Card::weak_from_this().lock();
    const auto retain_self = [this, self_owner]() -> EffectCard* {
        return self_owner ? static_cast<EffectCard*>(self_owner.get()) : this;
    };
    const auto version_snapshot = version;
    node.set_handler("Motions", [retain_self, version_snapshot](XNode& motionContainer) {
        // 子の解析が失敗すると、このハンドラーはXMLノードに残る。保持されたハン
        // ドラーが再試行されるまで、途中まで蓄積したD形式のデータを生存させる。
        auto values = std::make_shared<lib::Array<lib::Owner<motion::Motion>>>();
        motionContainer.set_handler("Motion", [values, version_snapshot](XNode& motionNode) {
            values->push_back(motion::Motion::createFromNode(motionNode, version_snapshot));
        });
        motionContainer.parse();
        retain_self()->motions(std::move(*values));
    });
    node.set_handler("Events", [retain_self, version_snapshot](XNode& eventContainer) {
        String comment;
        const auto events = event::AbstractEventTreeOwner::loadEventsFromNode(
            eventContainer, comment, &version_snapshot);
        retain_self()->event_owner_.addAll(events);
        retain_self()->event_owner_.commentForEvents(std::move(comment));
    });
    node.parseDCompatible();
}

CastCard::~CastCard() {
    invalidateBeastOwnerLifetime();
    invalidateCardLifetime();
}

lib::Owner<CwxPath> CastCard::ownerPath() const {
    return card::ownerPath(owner_);
}

lib::Owner<coupon::CouponsOwner> CastCard::couponOwner() {
    try {
        auto self = shared_from_this();
        return std::dynamic_pointer_cast<coupon::CouponsOwner>(self);
    } catch (const std::bad_weak_ptr&) {
        return nonOwning(static_cast<coupon::CouponsOwner*>(this));
    }
}

template <typename T>
void CastCard::attachCard(const lib::Owner<T>& card, const bool forceCounter) {
    requireCardObject(card);
    // Dの途中挿入はnullも含めて常にカウンターを伝える。
    // 末尾追加では、親が現在カウンターを持つ場合だけ伝える。
    if (forceCounter || useCounter()) card->setUseCounter(useCounter(), useCounterOwner());
    card->changeHandler(Card::changeHandler());
    const auto self = Card::weak_from_this().lock();
    if constexpr (std::is_same_v<T, SkillCard>) {
        if (self) card->owner(std::dynamic_pointer_cast<SkillOwner>(self));
        else card->owner(static_cast<SkillOwner*>(this), cardLifetime());
    } else if constexpr (std::is_same_v<T, ItemCard>) {
        if (self) card->owner(std::dynamic_pointer_cast<ItemOwner>(self));
        else card->owner(static_cast<ItemOwner*>(this), cardLifetime());
    } else if constexpr (std::is_same_v<T, BeastCard>) {
        if (self) card->owner(std::dynamic_pointer_cast<BeastOwner>(self));
        else card->owner(static_cast<BeastOwner*>(this), cardLifetime());
    }
}

template <typename T>
lib::Owner<T> CastCard::findImpl(const lib::SharedSlice<lib::Owner<T>>& array,
                                const std::uint64_t id) const {
    for (const auto& card : array) if (requireCardObject(card).id() == id) return card;
    return {};
}

template <typename T>
void CastCard::detachCard(const lib::Owner<T>& card) {
    if (!card) return;
    card->removeUseCounter();
    card->changeHandler({});
    if constexpr (std::is_same_v<T, SkillCard>) card->owner(static_cast<SkillOwner*>(nullptr));
    else if constexpr (std::is_same_v<T, ItemCard>) card->owner(static_cast<ItemOwner*>(nullptr));
    else if constexpr (std::is_same_v<T, BeastCard>) card->owner(static_cast<BeastOwner*>(nullptr));
}

template <typename T>
lib::Owner<T> CastCard::addImpl(lib::SharedSlice<lib::Owner<T>>& array,
                                 lib::Owner<T> card, const bool newCard) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    requireCardObject(card);
    bool copy = !newCard;
    for (const auto& current : array) {
        if (current.get() == card.get()) {
            remove(card);
            copy = false;
            break;
        }
    }
    if (copy) {
        if constexpr (std::is_same_v<T, SkillCard>) card = card->dupSkill();
        else if constexpr (std::is_same_v<T, ItemCard>) card = card->dupItem();
        else if constexpr (std::is_same_v<T, BeastCard>) card = card->dupBeast();
    }
    if (!array.empty() && requireCardObject(array.back()).id() >= card->id())
        card->id(array.back()->id() + 1U);
    attachCard(card);
    lib::appendToSlice(array, card);
    changed();
    return card;
}

template <typename T>
void CastCard::removeAtImpl(lib::SharedSlice<lib::Owner<T>>& array,
                            const std::size_t index) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    // コールバックが現在の一覧を置換し得るため、Dは各コールバックの後
    // にarr[i]を読み直す。実行する操作の受信先だけを保持する。
    auto current = array.at(index);
    requireCardObject(current).removeUseCounter();
    current = array.at(index);
    requireCardObject(current).changeHandler({});
    current = array.at(index);
    requireCardObject(current);
    if constexpr (std::is_same_v<T, SkillCard>) current->owner(static_cast<SkillOwner*>(nullptr));
    else if constexpr (std::is_same_v<T, ItemCard>) current->owner(static_cast<ItemOwner*>(nullptr));
    else if constexpr (std::is_same_v<T, BeastCard>) current->owner(static_cast<BeastOwner*>(nullptr));
    // Dはforeach中の古い領域を変更せず、新しい前半・後半のスラ
    // イスを連結する。イテレーターを作る前に現在のスライスを検査する。
    if (index >= array.size()) throw std::out_of_range("CastCard removal slice");
    lib::eraseFromSlice(array, index);
    changed();
}

template <typename T>
void CastCard::removeImpl(lib::SharedSlice<lib::Owner<T>>& array,
                          const std::uint64_t id) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    const auto retained = array;
    for (std::size_t i = 0; i < retained.size(); ++i) {
        const auto card = retained[i];
        if (requireCardObject(card).id() == id) removeAtImpl(array, i);
    }
}

template <typename T>
void CastCard::removeImpl(lib::SharedSlice<lib::Owner<T>>& array,
                          const lib::Owner<T>& input) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    const auto card = input;
    const auto retained = array;
    // Dの元のforeachインデックスを維持し、一致後も継続する
    // 。重複項目があると、後の現在一覧のインデックスでDは失敗し得
    // る。「最初の一つを削除」「すべて削除」へ黙って置き換えない。
    for (std::size_t i = 0; i < retained.size(); ++i) {
        const auto current = retained[i];
        if (current.get() == card.get()) removeAtImpl(array, i);
    }
}

template <typename T>
lib::Owner<T> CastCard::insertImpl(lib::SharedSlice<lib::Owner<T>>& array,
                                   std::size_t index,
                                   lib::Owner<T> card, const bool newCard) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (index == array.size()) return addImpl(array, std::move(card), newCard);
    requireCardObject(card);
    auto old = indexOf(card);
    bool copy = !newCard;
    if (old >= 0) {
        remove(card);
        copy = false;
        if (static_cast<std::size_t>(old) < index) --index;
    }
    if (copy) {
        if constexpr (std::is_same_v<T, SkillCard>) card = card->dupSkill();
        else if constexpr (std::is_same_v<T, ItemCard>) card = card->dupItem();
        else if constexpr (std::is_same_v<T, BeastCard>) card = card->dupBeast();
    }
    card->id(index == 0U ? 1U : requireCardObject(array.at(index - 1U)).id() + 1U);
    // DはID設定後にスライスを検査する。C++で安全に使えな
    // いイテレーターを作る前に、ここでも現在の長さを検査する。
    if (index > array.size()) throw std::out_of_range("CastCard::insert index");
    lib::insertIntoSlice(array, index, card);
    for (std::size_t i = index + 1U; i < array.size(); ++i) {
        const auto previous = array.at(i - 1U);
        const auto current = array.at(i);
        if (requireCardObject(previous).id() == requireCardObject(current).id())
            current->id(current->id() + 1U);
    }
    attachCard(card, true);
    changed();
    return card;
}

CastCard::CastCard(const std::uint64_t id, String name,
                   const lib::Array<lib::Owner<CardImage>>& imagePaths,
                   String desc, const std::uint32_t level,
                   const std::uint32_t lifeMax)
    : Card(id, std::move(name), imagePaths, std::move(desc)),
      level_(level), life_(lifeMax), life_max_(lifeMax) {
    constructRace();
}

void CastCard::changeHandler(ChangeHandler change) {
    Card::changeHandler(change);
    const auto childChange = Card::changeHandler();
    visitCardSlice(items_, [&](auto& card) { card.changeHandler(childChange); });
    visitCardSlice(skills_, [&](auto& card) { card.changeHandler(childChange); });
    visitCardSlice(beasts_, [&](auto& card) { card.changeHandler(childChange); });
}

void CastCard::changed() { Card::changed(); }

void CastCard::shallowCopy(const CastCard& other) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
    shallowCopyCard(other);
    copyRaceParam(other);
    level(other.level());
    life(other.life());
    lifeMax(other.lifeMax());
    mentality(other.mentality());
    mentalityRound(other.mentalityRound());
    paralyze(other.paralyze());
    poison(other.poison());
    bindRound(other.bindRound());
    silenceRound(other.silenceRound());
    faceUpRound(other.faceUpRound());
    antiMagicRound(other.antiMagicRound());
    for (const auto value : {Enhance::Action, Enhance::Avoid, Enhance::Resist, Enhance::Defense}) {
        enhance(value, other.enhance(value));
        enhanceRound(value, other.enhanceRound(value));
    }
    levelCoefficient(other.levelCoefficient());
    epPerLevel(other.epPerLevel());
    lib::Array<lib::Owner<coupon::Coupon>> copied;
    visitCardSlice(other.coupons_, [&](const auto& coupon) { copied.push_back(coupon.dup()); });
    coupons(copied);
}

void CastCard::deepCopy(const CastCard& other) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
    shallowCopy(other);
    visitCardSlice(other.skills_, [&](const auto& card) { add(card.dupSkill()); });
    visitCardSlice(other.items_, [&](const auto& card) { add(card.dupItem()); });
    visitCardSlice(other.beasts_, [&](const auto& card) { add(card.dupBeast()); });
}

bool CastCard::opEquals(const CastCard& other) const {
    return baseEquals(other) && equalsRace(other) &&
           level_ == other.level_ && life_ == other.life_ && life_max_ == other.life_max_ &&
           mentality_ == other.mentality_ && mentality_round_ == other.mentality_round_ &&
           paralyze_ == other.paralyze_ && poison_ == other.poison_ &&
           bind_round_ == other.bind_round_ && silence_round_ == other.silence_round_ &&
           face_up_round_ == other.face_up_round_ && anti_magic_round_ == other.anti_magic_round_ &&
           std::equal(std::begin(enhance_), std::end(enhance_), std::begin(other.enhance_)) &&
           std::equal(std::begin(enhance_round_), std::end(enhance_round_), std::begin(other.enhance_round_)) &&
           level_coefficient_ == other.level_coefficient_ && ep_per_level_ == other.ep_per_level_ &&
           ptrEqual(coupons_, other.coupons_) && ptrEqual(items_, other.items_) &&
           ptrEqual(skills_, other.skills_) && ptrEqual(beasts_, other.beasts_);
}

bool CastCard::equalsExcludeId(const CastCard& other) const {
    return equalsExcludeIdCard(other) && equalsRace(other) &&
           level_ == other.level_ && life_ == other.life_ && life_max_ == other.life_max_ &&
           mentality_ == other.mentality_ && mentality_round_ == other.mentality_round_ &&
           paralyze_ == other.paralyze_ && poison_ == other.poison_ &&
           bind_round_ == other.bind_round_ && silence_round_ == other.silence_round_ &&
           face_up_round_ == other.face_up_round_ && anti_magic_round_ == other.anti_magic_round_ &&
           std::equal(std::begin(enhance_), std::end(enhance_), std::begin(other.enhance_)) &&
           std::equal(std::begin(enhance_round_), std::end(enhance_round_), std::begin(other.enhance_round_)) &&
           level_coefficient_ == other.level_coefficient_ && ep_per_level_ == other.ep_per_level_ &&
           ptrEqual(coupons_, other.coupons_) && ptrEqual(items_, other.items_) &&
           ptrEqual(skills_, other.skills_) && ptrEqual(beasts_, other.beasts_);
}

lib::Owner<CastCard> CastCard::dupCast() const {
    auto result = std::make_shared<CastCard>(0U, "", lib::Array<lib::Owner<CardImage>>{}, "");
    result->deepCopy(*this);
    return result;
}

lib::Owner<Card> CastCard::dup() const { return dupCast(); }

void CastCard::setUseCounter(const lib::Owner<UseCounter>& counter,
                             const lib::Owner<CwxPath>& owner) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    visitCardSlice(items_, [&](auto& card) { card.setUseCounter(counter, owner); });
    visitCardSlice(skills_, [&](auto& card) { card.setUseCounter(counter, owner); });
    visitCardSlice(beasts_, [&](auto& card) { card.setUseCounter(counter, owner); });
    visitCardSlice(coupons_, [&](auto& coupon) { coupon.setUseCounter(counter, owner); });
    Card::setUseCounter(counter, owner);
}

void CastCard::removeUseCounter() {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    visitCardSlice(items_, [](auto& card) { card.removeUseCounter(); });
    visitCardSlice(skills_, [](auto& card) { card.removeUseCounter(); });
    visitCardSlice(beasts_, [](auto& card) { card.removeUseCounter(); });
    visitCardSlice(coupons_, [](auto& coupon) { coupon.removeUseCounter(); });
    Card::removeUseCounter();
}

void CastCard::level(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (level_ != value) changed();
    level_ = value;
}
void CastCard::life(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (life_ != value) changed();
    life_ = value;
}
void CastCard::lifeMax(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (life_max_ != value) changed();
    life_max_ = value;
}

int CastCard::aptitude(const Physical physical, const Mental mental) const {
    auto value = this->mental(mental);
    if (static_cast<int>(value) != value) value = value < 0.0 ? value + 0.5 : value - 0.5;
    return static_cast<int>(this->physical(physical)) + static_cast<int>(value);
}

void CastCard::levelCoefficient(const double value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (level_coefficient_ != value) changed();
    level_coefficient_ = value;
}
void CastCard::epPerLevel(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (ep_per_level_ != value) changed();
    ep_per_level_ = value;
}

void CastCard::coupons(lib::SharedSlice<lib::Owner<coupon::Coupon>> value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    // Dは通知が呼び出し側の配列記述子を置換する前に、スラ
    // イス値を受け取る。ここでも入力の共有領域を保持する。
    const auto incoming = value;
    // Dは同じ値なら通知だけを抑止し、クーポンの同一性の置換までは省
    // 略しない。公開前に古いレコードを切り離し、入力側を接続し直す。
    if (!ptrEqual(coupons_, incoming)) changed();
    visitCardSlice(coupons_, [](auto& coupon) {
        coupon.owner(lib::Owner<coupon::CouponsOwner>{});
        coupon.removeUseCounter();
    });
    const auto owner = couponOwner();
    for (const auto& coupon : incoming) {
        requireCardObject(coupon).owner(owner);
        if (useCounter()) coupon->setUseCounter(useCounter(), useCounterOwner());
    }
    coupons_ = incoming;
}

void CastCard::coupons(const lib::Array<lib::Owner<coupon::Coupon>>& value) {
    auto snapshot = value;
    coupons(lib::SharedSlice<lib::Owner<coupon::Coupon>>(std::move(snapshot)));
}

void CastCard::coupons(
    const std::initializer_list<lib::Owner<coupon::Coupon>> value) {
    coupons(lib::SharedSlice<lib::Owner<coupon::Coupon>>(value));
}

lib::Owner<ItemCard> CastCard::item(const std::uint64_t id) { return findImpl(items_, id); }
lib::Owner<const ItemCard> CastCard::item(const std::uint64_t id) const { return findImpl(items_, id); }
lib::Owner<SkillCard> CastCard::skill(const std::uint64_t id) { return findImpl(skills_, id); }
lib::Owner<const SkillCard> CastCard::skill(const std::uint64_t id) const { return findImpl(skills_, id); }
lib::Owner<BeastCard> CastCard::beast(const std::uint64_t id) { return findImpl(beasts_, id); }
lib::Owner<const BeastCard> CastCard::beast(const std::uint64_t id) const { return findImpl(beasts_, id); }
lib::Owner<ItemCard> CastCard::add(const lib::Owner<ItemCard>& card,
                                   const bool newCard) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    return addImpl(items_, card, newCard);
}
lib::Owner<SkillCard> CastCard::add(const lib::Owner<SkillCard>& card,
                                    const bool newCard) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    return addImpl(skills_, card, newCard);
}
lib::Owner<BeastCard> CastCard::add(const lib::Owner<BeastCard>& card,
                                    const bool newCard) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    return addImpl(beasts_, card, newCard);
}
void CastCard::removeItem(const std::uint64_t id) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    removeImpl(items_, id);
}
void CastCard::removeSkill(const std::uint64_t id) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    removeImpl(skills_, id);
}
void CastCard::removeBeast(const std::uint64_t id) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    removeImpl(beasts_, id);
}
void CastCard::remove(const lib::Owner<ItemCard>& card) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    removeImpl(items_, card);
}
void CastCard::remove(const lib::Owner<SkillCard>& card) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    removeImpl(skills_, card);
}
void CastCard::remove(const lib::Owner<BeastCard>& card) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    removeImpl(beasts_, card);
}
lib::Owner<ItemCard> CastCard::insert(const std::size_t index,
                                      const lib::Owner<ItemCard>& card,
                                      const bool newCard) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    return insertImpl(items_, index, card, newCard);
}
lib::Owner<SkillCard> CastCard::insert(const std::size_t index,
                                       const lib::Owner<SkillCard>& card,
                                       const bool newCard) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    return insertImpl(skills_, index, card, newCard);
}
lib::Owner<BeastCard> CastCard::insert(const std::size_t index,
                                       const lib::Owner<BeastCard>& card,
                                       const bool newCard) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    return insertImpl(beasts_, index, card, newCard);
}

#define CWX_CAST_SETTER(NAME, FIELD, TYPE) \
    void CastCard::NAME(const TYPE value) { \
        [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock(); \
        if (FIELD != value) changed(); \
        FIELD = value; \
    }
CWX_CAST_SETTER(mentality, mentality_, Mentality)
CWX_CAST_SETTER(mentalityRound, mentality_round_, std::uint32_t)
CWX_CAST_SETTER(paralyze, paralyze_, std::uint32_t)
CWX_CAST_SETTER(poison, poison_, std::uint32_t)
CWX_CAST_SETTER(bindRound, bind_round_, std::uint32_t)
CWX_CAST_SETTER(silenceRound, silence_round_, std::uint32_t)
CWX_CAST_SETTER(faceUpRound, face_up_round_, std::uint32_t)
CWX_CAST_SETTER(antiMagicRound, anti_magic_round_, std::uint32_t)
#undef CWX_CAST_SETTER

std::int32_t CastCard::enhance(const Enhance value) const {
    return cardArrayElement(enhance_, enumIndex(value));
}
void CastCard::enhance(const Enhance value, const std::int32_t amount) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    auto& stored = cardArrayElement(enhance_, enumIndex(value));
    if (stored != amount) changed();
    stored = amount;
}
std::uint32_t CastCard::enhanceRound(const Enhance value) const {
    return cardArrayElement(enhance_round_, enumIndex(value));
}
void CastCard::enhanceRound(const Enhance value, const std::uint32_t amount) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    auto& stored = cardArrayElement(enhance_round_, enumIndex(value));
    if (stored != amount) changed();
    stored = amount;
}

String CastCard::toXML(const XMLOption& option) const { return toNode(option).text(); }

XNode CastCard::toNode(const XMLOption& option) const {
    auto node = XNode::create(XML_NAME);
    setRootAttributes(node, option);
    auto property = setProp(node, &option, nullptr);
    property.newElement("Level", level_);
    property.newElement("Life", life_).newAttr("max", life_max_);
    setFeature(property);
    setAbility(property);
    auto status = property.newElement("Status");
    status.newElement("Mentality", types::fromMentality(mentality_)).newAttr("duration", mentality_round_);
    status.newElement("Paralyze", paralyze_);
    status.newElement("Poison", poison_);
    status.newElement("Bind").newAttr("duration", bind_round_);
    status.newElement("Silence").newAttr("duration", silence_round_);
    status.newElement("FaceUp").newAttr("duration", face_up_round_);
    status.newElement("AntiMagic").newAttr("duration", anti_magic_round_);
    auto enhanceNode = property.newElement("Enhance");
    for (const auto value : {Enhance::Action, Enhance::Avoid, Enhance::Resist, Enhance::Defense}) {
        const auto name = value == Enhance::Action ? "Action" : value == Enhance::Avoid ? "Avoid" : value == Enhance::Resist ? "Resist" : "Defense";
        enhanceNode.newElement(name, enhance(value)).newAttr("duration", enhanceRound(value));
    }
    if (level_coefficient_ != 1.0 || ep_per_level_ != race::DEFAULT_EP_PER_LEVEL) {
        auto coefficient = property.newElement("Coefficient");
        if (level_coefficient_ != 1.0) coefficient.newAttr("level", level_coefficient_);
        if (ep_per_level_ != race::DEFAULT_EP_PER_LEVEL) coefficient.newAttr("ep", ep_per_level_);
    }
    auto couponNode = property.newElement(coupon::Coupon::XML_NAME_M);
    for (const auto& value : coupons_) if (value) static_cast<void>(value->toNode(couponNode));
    auto itemNode = node.newElement("ItemCards");
    for (const auto& value : items_) if (value) static_cast<void>(value->toNode(itemNode, option));
    auto skillNode = node.newElement("SkillCards");
    for (const auto& value : skills_) if (value) static_cast<void>(value->toNode(skillNode, option));
    auto beastNode = node.newElement("BeastCards");
    for (const auto& value : beasts_) if (value) static_cast<void>(value->toNode(beastNode, option));
    return node;
}

XNode CastCard::toNode(XNode& parent, const XMLOption& option) const {
    auto node = parent.newElement(XML_NAME);
    setRootAttributes(node, option);
    auto property = setProp(node, &option, nullptr);
    property.newElement("Level", level_);
    property.newElement("Life", life_).newAttr("max", life_max_);
    setFeature(property);
    setAbility(property);
    auto status = property.newElement("Status");
    status.newElement("Mentality", types::fromMentality(mentality_)).newAttr("duration", mentality_round_);
    status.newElement("Paralyze", paralyze_);
    status.newElement("Poison", poison_);
    status.newElement("Bind").newAttr("duration", bind_round_);
    status.newElement("Silence").newAttr("duration", silence_round_);
    status.newElement("FaceUp").newAttr("duration", face_up_round_);
    status.newElement("AntiMagic").newAttr("duration", anti_magic_round_);
    auto enhanceNode = property.newElement("Enhance");
    for (const auto value : {Enhance::Action, Enhance::Avoid, Enhance::Resist, Enhance::Defense}) {
        const auto name = value == Enhance::Action ? "Action" : value == Enhance::Avoid ? "Avoid" : value == Enhance::Resist ? "Resist" : "Defense";
        enhanceNode.newElement(name, enhance(value)).newAttr("duration", enhanceRound(value));
    }
    if (level_coefficient_ != 1.0 || ep_per_level_ != race::DEFAULT_EP_PER_LEVEL) {
        auto coefficient = property.newElement("Coefficient");
        if (level_coefficient_ != 1.0) coefficient.newAttr("level", level_coefficient_);
        if (ep_per_level_ != race::DEFAULT_EP_PER_LEVEL) coefficient.newAttr("ep", ep_per_level_);
    }
    auto couponNode = property.newElement(coupon::Coupon::XML_NAME_M);
    for (const auto& value : coupons_) if (value) static_cast<void>(value->toNode(couponNode));
    auto itemNode = node.newElement("ItemCards");
    for (const auto& value : items_) if (value) static_cast<void>(value->toNode(itemNode, option));
    auto skillNode = node.newElement("SkillCards");
    for (const auto& value : skills_) if (value) static_cast<void>(value->toNode(skillNode, option));
    auto beastNode = node.newElement("BeastCards");
    for (const auto& value : beasts_) if (value) static_cast<void>(value->toNode(beastNode, option));
    return node;
}

lib::Owner<CastCard> CastCard::createFromNode(const props::CProps& prop,
                                              XNode& node,
                                              const XMLInfo& version) {
    if (node.name() != XML_NAME) throw CardException("Node is not cast card: " + node.name());
    auto result = std::make_shared<CastCard>(0U, "", lib::Array<lib::Owner<CardImage>>{}, "", 1U, 1U);
    // DはXNodeを値で受け取る。ハンドラーの連想配列が初めに空なら、ローカル
    // 値に領域を確保するため、解析が例外で終わっても、結果をキャプチャするハン
    // ドラーを呼び出し元のノードに残さない。初めから要素がある連想配列は呼び出
    // し元と共有されるため、その場合は元のノードで解析して同じ動作を維持する。
    XNode local_node;
    XNode* parse_node_pointer = &node;
    if (node.onTag.empty()) {
        local_node = node;
        parse_node_pointer = &local_node;
    }
    XNode& parse_node = *parse_node_pointer;
    // Dの一回のルート解析に合わせる。Propertyのコールバックはクーポンの
    // 状態を置換し、繰り返し現れるカード群は共有の子一覧へ追加して並べ替える。
    const auto root_snapshot = parse_node;
    const auto version_snapshot = version;
    const auto* properties = &prop;
    parse_node.set_handler("Property", [result, root_snapshot, version_snapshot, properties](XNode& property) {
        auto coupons = std::make_shared<lib::Array<lib::Owner<coupon::Coupon>>>();
        auto seen_coupon_names = std::make_shared<std::unordered_set<String>>();
        property.set_handler("Level", [result](XNode& child) {
            result->level_ = static_cast<std::uint32_t>(parseInt(child.value(), "Level"));
        });
        property.set_handler("Life", [result](XNode& child) {
            result->life_ = static_cast<std::uint32_t>(parseInt(child.value(), "Life"));
            result->life_max_ = static_cast<std::uint32_t>(
                child.attr<std::int32_t>("max", true));
        });
        property.set_handler("Feature", [result, version_snapshot](XNode& child) {
            result->loadFeature(child, version_snapshot);
        });
        // CPropsは呼び出し側が提供するコピー不可のオブジェクト。Dのクロージャ
        // ーが入力引数を保持するのと同様に、ハンドラーではAPIの借用を維持する。
        property.set_handler("Ability", [result, properties, version_snapshot](XNode& child) {
            result->loadAbility(*properties, child, version_snapshot);
        });
        property.set_handler("Status", [result](XNode& status) {
            status.set_handler("Mentality", [result](XNode& child) {
                result->mentality_ = types::toMentality(child.value());
                result->mentality_round_ = static_cast<std::uint32_t>(
                    child.attr<std::int32_t>("duration", true));
            });
            status.set_handler("Paralyze", [result](XNode& child) {
                result->paralyze_ = static_cast<std::uint32_t>(parseInt(child.value(), "Paralyze"));
            });
            status.set_handler("Poison", [result](XNode& child) {
                result->poison_ = static_cast<std::uint32_t>(parseInt(child.value(), "Poison"));
            });
            status.set_handler("Bind", [result](XNode& child) {
                result->bind_round_ = static_cast<std::uint32_t>(
                    child.attr<std::int32_t>("duration", true));
            });
            status.set_handler("Silence", [result](XNode& child) {
                result->silence_round_ = static_cast<std::uint32_t>(
                    child.attr<std::int32_t>("duration", true));
            });
            status.set_handler("FaceUp", [result](XNode& child) {
                result->face_up_round_ = static_cast<std::uint32_t>(
                    child.attr<std::int32_t>("duration", true));
            });
            status.set_handler("AntiMagic", [result](XNode& child) {
                result->anti_magic_round_ = static_cast<std::uint32_t>(
                    child.attr<std::int32_t>("duration", true));
            });
            status.parse();
        });
        property.set_handler("Enhance", [result](XNode& enhance) {
            enhance.set_handler("Action", [result](XNode& child) {
                result->enhance_[enumIndex(Enhance::Action)] = parseInt(child.value(), "Action");
                result->enhance_round_[enumIndex(Enhance::Action)] =
                    static_cast<std::uint32_t>(child.attr<std::int32_t>("duration", true));
            });
            enhance.set_handler("Avoid", [result](XNode& child) {
                result->enhance_[enumIndex(Enhance::Avoid)] = parseInt(child.value(), "Avoid");
                result->enhance_round_[enumIndex(Enhance::Avoid)] =
                    static_cast<std::uint32_t>(child.attr<std::int32_t>("duration", true));
            });
            enhance.set_handler("Resist", [result](XNode& child) {
                result->enhance_[enumIndex(Enhance::Resist)] = parseInt(child.value(), "Resist");
                result->enhance_round_[enumIndex(Enhance::Resist)] =
                    static_cast<std::uint32_t>(child.attr<std::int32_t>("duration", true));
            });
            enhance.set_handler("Defense", [result](XNode& child) {
                result->enhance_[enumIndex(Enhance::Defense)] = parseInt(child.value(), "Defense");
                result->enhance_round_[enumIndex(Enhance::Defense)] =
                    static_cast<std::uint32_t>(child.attr<std::int32_t>("duration", true));
            });
            enhance.parse();
        });
        property.set_handler("Coefficient", [result](XNode& child) {
            result->level_coefficient_ = attrDouble(child, "level", 1.0);
            result->ep_per_level_ = attrUInt(child, "ep", race::DEFAULT_EP_PER_LEVEL);
        });
        property.set_handler(String(coupon::Coupon::XML_NAME_M),
                             [coupons, seen_coupon_names, version_snapshot](XNode& group) {
            group.set_handler(String(coupon::Coupon::XML_NAME),
                              [coupons, seen_coupon_names, version_snapshot](XNode& couponNode) {
                auto value = coupon::Coupon::fromNode(couponNode, version_snapshot);
                if (value && seen_coupon_names->insert(value->name()).second)
                    coupons->push_back(std::move(value));
            });
            group.parse();
        });

        auto root = root_snapshot;
        result->loadProp(root, property, version_snapshot);
        result->coupons(lib::SharedSlice<lib::Owner<coupon::Coupon>>(std::move(*coupons)));
    });
    parse_node.set_handler("ItemCards", [result, version_snapshot](XNode& group) {
        group.set_handler(String(ItemCard::XML_NAME), [result, version_snapshot](XNode& cardNode) {
            auto value = ItemCard::createFromNode(cardNode, version_snapshot);
            value->owner(std::static_pointer_cast<ItemOwner>(result));
            lib::appendToSlice(result->items_, std::move(value));
        });
        group.parse();
        if (result->items_.size() > 1U)
            std::sort(result->items_.begin(), result->items_.end(),
                      [](const auto& a, const auto& b) { return a->id() < b->id(); });
    });
    parse_node.set_handler("SkillCards", [result, version_snapshot](XNode& group) {
        group.set_handler(String(SkillCard::XML_NAME), [result, version_snapshot](XNode& cardNode) {
            auto value = SkillCard::createFromNode(cardNode, version_snapshot);
            value->owner(std::static_pointer_cast<SkillOwner>(result));
            lib::appendToSlice(result->skills_, std::move(value));
        });
        group.parse();
        if (result->skills_.size() > 1U)
            std::sort(result->skills_.begin(), result->skills_.end(),
                      [](const auto& a, const auto& b) { return a->id() < b->id(); });
    });
    parse_node.set_handler("BeastCards", [result, version_snapshot](XNode& group) {
        group.set_handler(String(BeastCard::XML_NAME), [result, version_snapshot](XNode& cardNode) {
            auto value = BeastCard::createFromNode(cardNode, version_snapshot);
            value->owner(std::static_pointer_cast<BeastOwner>(result));
            lib::appendToSlice(result->beasts_, std::move(value));
        });
        group.parse();
        if (result->beasts_.size() > 1U)
            std::sort(result->beasts_.begin(), result->beasts_.end(),
                      [](const auto& a, const auto& b) { return a->id() < b->id(); });
    });
    const bool caller_handlers_alias = parse_node_pointer == &node;
    const auto caller_handlers = caller_handlers_alias
        ? node.onTag : decltype(node.onTag){};
    const auto caller_handler_aliases = caller_handlers_alias
        ? node.on_tag : decltype(node.on_tag){};
    parse_node.parseDCompatible();
    if (caller_handlers_alias) {
        // Dは解析後、値渡しのXNodeの連想配列記述子だけをnullにする。初
        // めから要素があった呼び出し側の連想配列は、新旧の項目を両方保持する。
        node.onTag = caller_handlers;
        node.on_tag = caller_handler_aliases;
    }
    return result;
}

void CastCard::owner(const lib::Owner<CastOwner>& owner) { owner_.setShared(owner); }

String CastCard::cwxPath(const bool id) {
    auto owner = ownerPath();
    if (!owner) return {};
    if (id) return path::cpjoinid(owner, "castcard", this->id());
    std::size_t index = 0U;
    if (const auto parent = owner_.lock())
        for (const auto& value : parent->casts()) { if (value.get() == this) break; ++index; }
    return path::cpjoin(owner, "castcard", index, false);
}

lib::Owner<CwxPath> CastCard::findCWXPath(const StringView value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    const String input(value);
    if (path::cpempty(input)) return Card::findCWXPath(input);
    const auto category = path::cpcategory(input);
    if (category == "skillcard") {
        const auto index = path::cpindex(input);
        if (index >= skills_.size()) return {};
        const auto card = skills_.at(index);
        return requireCardObject(card).findCWXPath(path::cpbottom(input));
    }
    if (category == "itemcard") {
        const auto index = path::cpindex(input);
        if (index >= items_.size()) return {};
        const auto card = items_.at(index);
        return requireCardObject(card).findCWXPath(path::cpbottom(input));
    }
    if (category == "beastcard") {
        const auto index = path::cpindex(input);
        if (index >= beasts_.size()) return {};
        const auto card = beasts_.at(index);
        return requireCardObject(card).findCWXPath(path::cpbottom(input));
    }
    if (category == "skillcard:id") {
        const auto card = skill(path::cpindex(input));
        return card ? card->findCWXPath(path::cpbottom(input)) : lib::Owner<CwxPath>{};
    }
    if (category == "itemcard:id") {
        const auto card = item(path::cpindex(input));
        return card ? card->findCWXPath(path::cpbottom(input)) : lib::Owner<CwxPath>{};
    }
    if (category == "beastcard:id") {
        const auto card = beast(path::cpindex(input));
        return card ? card->findCWXPath(path::cpbottom(input)) : lib::Owner<CwxPath>{};
    }
    return {};
}

lib::Array<lib::Observer<CwxPath>> CastCard::cwxChilds() {
    lib::Array<lib::Observer<CwxPath>> result;
    for (const auto& value : skills_) result.emplace_back(cardPathOwner(value));
    for (const auto& value : items_) result.emplace_back(cardPathOwner(value));
    for (const auto& value : beasts_) result.emplace_back(cardPathOwner(value));
    return result;
}

lib::Observer<CwxPath> CastCard::cwxParent() { return ownerObserver(owner_); }

SkillCard::SkillCard(const System* system, const std::uint64_t id, String name,
                     const lib::Array<lib::Owner<CardImage>>& imagePaths,
                     String desc)
    : EffectCard(system, id, std::move(name), imagePaths, std::move(desc)),
      link_user_(std::make_shared<usecounter::SkillUser>(static_cast<CwxPath*>(static_cast<Card*>(this)))) {}
SkillCard::~SkillCard() { invalidateMotionLifetime(); }

lib::Owner<CwxPath> SkillCard::ownerPath() const { return card::ownerPath(owner_); }

std::uint64_t SkillCard::linkId() const { return link_user_ ? link_user_->skill() : 0U; }

void SkillCard::linkId(const std::uint64_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (linkId() != value) changed();
    if (link_user_) link_user_->skill(value);
}

void SkillCard::setUseCounterImpl(const lib::Owner<UseCounter>& counter) {
    if (!link_user_) return;
    if (counter) link_user_->setUseCounter(counter);
    else link_user_->removeUseCounter();
}
void SkillCard::removeUseCounterImpl() { if (link_user_) link_user_->removeUseCounter(); }

void SkillCard::shallowCopy(const SkillCard& other) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
    shallowCopyEffectCard(other);
    level(other.level());
    hold(other.hold());
    price(other.price());
    useLimit(other.useLimit());
}
void SkillCard::deepCopy(const SkillCard& other) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
    deepCopyEffectCard(other);
    level(other.level());
    hold(other.hold());
    price(other.price());
    useLimit(other.useLimit());
}
void SkillCard::clearData() {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    EffectCard::clearData();
    level(0U); hold(false); price(0U); useLimit(0U);
}

bool SkillCard::opEquals(const SkillCard& other) const {
    if (linkId() != 0U) return linkId() == other.linkId() && hold_ == other.hold_ && comment() == other.comment();
    return level_ == other.level_ && hold_ == other.hold_ &&
           price_ == other.price_ && use_limit_ == other.use_limit_ && opEqualsEffect(other);
}
bool SkillCard::equalsExcludeId(const SkillCard& other) const {
    if (linkId() != 0U) return linkId() == other.linkId() && hold_ == other.hold_ && comment() == other.comment();
    return level_ == other.level_ && hold_ == other.hold_ &&
           price_ == other.price_ && use_limit_ == other.use_limit_ && equalsExcludeIdEffect(other);
}
void SkillCard::level(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (level_ != value) changed();
    level_ = value;
}
void SkillCard::useLimit(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    // Dはこのプロパティをintに格納してuintと比較し、代入時
    // は入力の32ビットを保持する。同じ符号付きの内部値を使う。
    if (static_cast<std::uint32_t>(use_limit_) != value) changed();
    use_limit_ = std::bit_cast<std::int32_t>(value);
}
void SkillCard::price(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (price_ != value) changed();
    price_ = value;
}
void SkillCard::hold(const bool value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (hold_ != value) changed();
    hold_ = value;
}

lib::Owner<SkillCard> SkillCard::dupSkill() const {
    auto result = std::make_shared<SkillCard>(nullptr, 0U, "", lib::Array<lib::Owner<CardImage>>{}, "");
    result->deepCopy(*this);
    return result;
}
lib::Owner<Card> SkillCard::dup() const { return dupSkill(); }

void SkillCard::toNodeImpl(XNode& node, const XMLOption& option,
                           const OverData* over) const {
    if (linkId() != 0U && option.includeCard) {
        OverData overrideData;
        overrideData.id = id();
        if (!option.noLinkId) overrideData.linkId = linkId();
        overrideData.overHold = true;
        overrideData.hold = hold_;
        lib::Owner<const SkillCard> source;
        if (option.skill) source = option.skill(linkId());
        if (!source) source = std::make_shared<SkillCard>(option.sys, id(), "", lib::Array<lib::Owner<CardImage>>{}, "");
        source->toNodeImpl(node, option, &overrideData);
        return;
    }
    auto property = setEffProp(node, &option, over);
    if (linkId() == 0U || option.includeCard) {
        property.newElement("Level", level_);
        property.newElement("UseLimit", useLimit());
        property.newElement("Price", price_);
    }
    property.newElement("Hold", utils::fromBool(over && over->overHold ? over->hold : hold_));
}

String SkillCard::toXML(const XMLOption& option) const { return toNode(option).text(); }
XNode SkillCard::toNode(const XMLOption& option) const {
    auto node = XNode::create(XML_NAME); setRootAttributes(node, option); toNodeImpl(node, option, nullptr); return node;
}
XNode SkillCard::toNode(XNode& parent, const XMLOption& option) const {
    return toNode(parent, option, OverData{});
}
XNode SkillCard::toNode(XNode& parent, const XMLOption& option, const OverData& over) const {
    auto node = parent.newElement(XML_NAME); setRootAttributes(node, option);
    const OverData* p = (over.id || over.linkId || over.overHold) ? &over : nullptr;
    toNodeImpl(node, option, p);
    return node;
}

lib::Owner<SkillCard> SkillCard::createFromNode(XNode& node, const XMLInfo& version) {
    if (node.name() != XML_NAME) throw CardException("Node is not skill card: " + node.name());
    auto result = std::make_shared<SkillCard>(&version.sys, 0U, "", lib::Array<lib::Owner<CardImage>>{}, "");
    std::uint64_t link = 0U; bool hold = false; String comment;
    const auto id = readLinkInfo(node, link, hold, comment);
    if (link != 0U) {
        result->id(id); result->linkId(link); result->hold(hold); result->comment(comment); return result;
    }
    const auto version_snapshot = version;
    // DはPropertyのコールバックをまたいで、このクロージャーのpriceLoaded状
    // 態を保持する。C++のD互換ディスパッチャーはstd::functionハンドラーをコ
    // ピーするため、状態を明示的に共有し、Property間と再試行時のDの動作を維持する。
    auto price_loaded = std::make_shared<bool>(false);
    node.set_handler("Property", [result, &node, version_snapshot, price_loaded](XNode& property) {
        for (const auto& child : property.children()) {
            if (child.name() == "Level") result->level_ = static_cast<std::uint32_t>(parseInt(child.value(), "Level"));
            else if (child.name() == "UseLimit") result->use_limit_ = parseInt(child.value(), "UseLimit");
            else if (child.name() == "Price") {
                result->price_ = static_cast<std::uint32_t>(parseInt(child.value(), "Price"));
                *price_loaded = true;
            }
            else if (child.name() == "Hold") result->hold_ = utils::parseBool(child.value());
        }
        result->loadEffProp(node, property, version_snapshot);
        if (!*price_loaded) result->price_ = 400U + result->level_ * 200U;
    });
    result->loadEffV(node, version_snapshot);
    return result;
}

void SkillCard::owner(const lib::Owner<SkillOwner>& owner) { owner_.setShared(owner); }
String SkillCard::cwxPath(const bool id) {
    auto owner = ownerPath(); if (!owner) return {};
    if (id) return path::cpjoinid(owner, "skillcard", this->id());
    std::size_t index = 0U;
    if (const auto parent = owner_.lock()) for (const auto& value : parent->skills()) { if (value.get() == this) break; ++index; }
    return path::cpjoin(owner, "skillcard", index, false);
}
lib::Observer<CwxPath> SkillCard::cwxParent() { return ownerObserver(owner_); }
lib::Owner<CwxPath> SkillCard::findCWXPath(const StringView value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    const String input(value);
    if (path::cpempty(input)) return Card::findCWXPath(input);
    if (linkId() != 0U) return findLinkedCardPath<SkillOwner>(
        *this, input, static_cast<lib::Owner<SkillCard> (SkillOwner::*)(std::uint64_t)>(
                        &SkillOwner::skill));
    return EffectCard::findCWXPath(input);
}

ItemCard::ItemCard(const System* system, const std::uint64_t id, String name,
                   const lib::Array<lib::Owner<CardImage>>& imagePaths,
                   String desc)
    : EffectCard(system, id, std::move(name), imagePaths, std::move(desc)),
      link_user_(std::make_shared<usecounter::ItemUser>(static_cast<CwxPath*>(static_cast<Card*>(this)))) {}
ItemCard::~ItemCard() { invalidateMotionLifetime(); }

lib::Owner<CwxPath> ItemCard::ownerPath() const { return card::ownerPath(owner_); }
std::uint64_t ItemCard::linkId() const { return link_user_ ? link_user_->item() : 0U; }
void ItemCard::linkId(const std::uint64_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (linkId() != value) changed();
    if (link_user_) link_user_->item(value);
}
void ItemCard::setUseCounterImpl(const lib::Owner<UseCounter>& counter) { if (link_user_) { if (counter) link_user_->setUseCounter(counter); else link_user_->removeUseCounter(); } }
void ItemCard::removeUseCounterImpl() { if (link_user_) link_user_->removeUseCounter(); }

void ItemCard::shallowCopy(const ItemCard& other) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
    shallowCopyEffectCard(other);
    enhanceOwner(Enhance::Avoid, other.enhanceOwner(Enhance::Avoid));
    enhanceOwner(Enhance::Resist, other.enhanceOwner(Enhance::Resist));
    enhanceOwner(Enhance::Defense, other.enhanceOwner(Enhance::Defense));
    price(other.price()); useLimit(other.useLimit()); useLimitMax(other.useLimitMax()); hold(other.hold());
}
void ItemCard::deepCopy(const ItemCard& other) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
    deepCopyEffectCard(other);
    enhanceOwner(Enhance::Avoid, other.enhanceOwner(Enhance::Avoid));
    enhanceOwner(Enhance::Resist, other.enhanceOwner(Enhance::Resist));
    enhanceOwner(Enhance::Defense, other.enhanceOwner(Enhance::Defense));
    price(other.price());
    useLimit(other.useLimit());
    useLimitMax(other.useLimitMax());
    hold(other.hold());
}
void ItemCard::clearData() {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    EffectCard::clearData();
    enhanceOwner(Enhance::Avoid, 0); enhanceOwner(Enhance::Resist, 0); enhanceOwner(Enhance::Defense, 0);
    price(0U); useLimit(0U); useLimitMax(0U); hold(false);
}
bool ItemCard::opEquals(const ItemCard& other) const {
    if (linkId() != 0U) return linkId() == other.linkId() && hold_ == other.hold_ && comment() == other.comment();
    return owner_enhance_[0] == other.owner_enhance_[0] && owner_enhance_[1] == other.owner_enhance_[1] &&
           owner_enhance_[2] == other.owner_enhance_[2] && price_ == other.price_ &&
           use_limit_ == other.use_limit_ && use_limit_max_ == other.use_limit_max_ && hold_ == other.hold_ &&
           opEqualsEffect(other);
}
bool ItemCard::equalsExcludeId(const ItemCard& other) const {
    if (linkId() != 0U) return linkId() == other.linkId() && hold_ == other.hold_ && comment() == other.comment();
    return owner_enhance_[0] == other.owner_enhance_[0] &&
           owner_enhance_[1] == other.owner_enhance_[1] && owner_enhance_[2] == other.owner_enhance_[2] &&
           price_ == other.price_ && use_limit_ == other.use_limit_ && use_limit_max_ == other.use_limit_max_ && hold_ == other.hold_ &&
           equalsExcludeIdEffect(other);
}
void ItemCard::useLimit(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (use_limit_ != value) changed();
    use_limit_ = value;
}
void ItemCard::useLimitMax(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (use_limit_max_ != value) changed();
    use_limit_max_ = value;
}
void ItemCard::price(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (price_ != value) changed();
    price_ = value;
}
std::int32_t ItemCard::enhanceOwner(const Enhance value) const {
    // DはEffectCardと同じ三つの必須キーだけを初期化する。
    return owner_enhance_[effectEnhanceIndex(value) - 1U];
}
void ItemCard::enhanceOwner(const Enhance value, const std::int32_t amount) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    const auto index = effectEnhanceIndex(value) - 1U;
    if (owner_enhance_[index] != amount) changed();
    owner_enhance_[index] = amount;
}
void ItemCard::hold(const bool value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (hold_ != value) changed();
    hold_ = value;
}

lib::Owner<ItemCard> ItemCard::dupItem() const {
    auto result = std::make_shared<ItemCard>(nullptr, 0U, "", lib::Array<lib::Owner<CardImage>>{}, ""); result->deepCopy(*this); return result;
}
lib::Owner<Card> ItemCard::dup() const { return dupItem(); }

void ItemCard::toNodeImpl(XNode& node, const XMLOption& option, const OverData* over) const {
    if (linkId() != 0U && option.includeCard) {
        OverData overrideData{ id(), option.noLinkId ? 0U : linkId(), true, hold_ };
        lib::Owner<const ItemCard> source;
        if (option.item) source = option.item(linkId());
        if (!source) source = std::make_shared<ItemCard>(option.sys, id(), "", lib::Array<lib::Owner<CardImage>>{}, "");
        source->toNodeImpl(node, option, &overrideData); return;
    }
    auto property = setEffProp(node, &option, over);
    if (linkId() == 0U || option.includeCard) {
        property.newElement("UseLimit", use_limit_).newAttr("max", use_limit_max_);
        property.newElement("Price", price_);
        auto ownerEnhance = property.newElement("EnhanceOwner");
        ownerEnhance.newAttr("avoid", enhanceOwner(Enhance::Avoid)); ownerEnhance.newAttr("resist", enhanceOwner(Enhance::Resist)); ownerEnhance.newAttr("defense", enhanceOwner(Enhance::Defense));
    }
    property.newElement("Hold", utils::fromBool(over && over->overHold ? over->hold : hold_));
}
String ItemCard::toXML(const XMLOption& option) const { return toNode(option).text(); }
XNode ItemCard::toNode(const XMLOption& option) const { auto node = XNode::create(XML_NAME); setRootAttributes(node, option); toNodeImpl(node, option, nullptr); return node; }
XNode ItemCard::toNode(XNode& parent, const XMLOption& option) const { return toNode(parent, option, OverData{}); }
XNode ItemCard::toNode(XNode& parent, const XMLOption& option, const OverData& over) const { auto node = parent.newElement(XML_NAME); setRootAttributes(node, option); const OverData* p = (over.id || over.linkId || over.overHold) ? &over : nullptr; toNodeImpl(node, option, p); return node; }

lib::Owner<ItemCard> ItemCard::createFromNode(XNode& node, const XMLInfo& version) {
    if (node.name() != XML_NAME) throw CardException("Node is not item card: " + node.name());
    auto result = std::make_shared<ItemCard>(&version.sys, 0U, "", lib::Array<lib::Owner<CardImage>>{}, "");
    std::uint64_t link = 0U; bool hold = false; String comment; const auto id = readLinkInfo(node, link, hold, comment);
    if (link != 0U) { result->id(id); result->linkId(link); result->hold(hold); result->comment(comment); return result; }
    const auto version_snapshot = version;
    node.set_handler("Property", [result, &node, version_snapshot](XNode& property) {
        for (const auto& child : property.children()) {
            if (child.name() == "UseLimit") {
                result->use_limit_ = static_cast<std::uint32_t>(parseInt(child.value(), "UseLimit"));
                result->use_limit_max_ = static_cast<std::uint32_t>(
                    parseInt(child.attr("max", true), "UseLimit.max"));
            }
            else if (child.name() == "Price") result->price_ = static_cast<std::uint32_t>(parseInt(child.value(), "Price"));
            else if (child.name() == "EnhanceOwner") {
                result->owner_enhance_[0] = parseInt(child.attr("avoid", true), "EnhanceOwner.avoid");
                result->owner_enhance_[1] = parseInt(child.attr("resist", true), "EnhanceOwner.resist");
                result->owner_enhance_[2] = parseInt(child.attr("defense", true), "EnhanceOwner.defense");
            }
            else if (child.name() == "Hold") result->hold_ = utils::parseBool(child.value());
        }
        result->loadEffProp(node, property, version_snapshot);
    });
    result->loadEffV(node, version_snapshot); return result;
}
void ItemCard::owner(const lib::Owner<ItemOwner>& owner) { owner_.setShared(owner); }
String ItemCard::cwxPath(const bool id) {
    auto owner = ownerPath(); if (!owner) return {};
    if (id) return path::cpjoinid(owner, "itemcard", this->id());
    std::size_t index = 0U; if (const auto parent = owner_.lock()) for (const auto& value : parent->items()) { if (value.get() == this) break; ++index; }
    return path::cpjoin(owner, "itemcard", index, false);
}
lib::Observer<CwxPath> ItemCard::cwxParent() { return ownerObserver(owner_); }
lib::Owner<CwxPath> ItemCard::findCWXPath(const StringView value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    const String input(value);
    if (path::cpempty(input)) return Card::findCWXPath(input);
    if (linkId() != 0U) return findLinkedCardPath<ItemOwner>(
        *this, input, static_cast<lib::Owner<ItemCard> (ItemOwner::*)(std::uint64_t)>(
                        &ItemOwner::item));
    return EffectCard::findCWXPath(input);
}

BeastCard::BeastCard(const System* system, const std::uint64_t id, String name,
                     const lib::Array<lib::Owner<CardImage>>& imagePaths,
                     String desc)
    : EffectCard(system, id, std::move(name), imagePaths, std::move(desc)),
      link_user_(std::make_shared<usecounter::BeastUser>(static_cast<CwxPath*>(static_cast<Card*>(this)))) {}
BeastCard::~BeastCard() { invalidateMotionLifetime(); }

lib::Owner<CwxPath> BeastCard::ownerPath() const {
    return card::ownerPath(owner_);
}
std::uint64_t BeastCard::linkId() const { return link_user_ ? link_user_->beast() : 0U; }
void BeastCard::linkId(const std::uint64_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (linkId() != value) changed();
    if (link_user_) link_user_->beast(value);
}
void BeastCard::setUseCounterImpl(const lib::Owner<UseCounter>& counter) { if (link_user_) { if (counter) link_user_->setUseCounter(counter); else link_user_->removeUseCounter(); } }
void BeastCard::removeUseCounterImpl() { if (link_user_) link_user_->removeUseCounter(); }

void BeastCard::shallowCopy(const BeastCard& other) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
    shallowCopyEffectCard(other);
    useLimit(other.useLimit());
    price(other.price());
    invocationCondition(other.invocationCondition());
    removeWithUnconscious(other.removeWithUnconscious());
    showStyle(other.showStyle());
}
void BeastCard::deepCopy(const BeastCard& other) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
    deepCopyEffectCard(other);
    useLimit(other.useLimit());
    price(other.price());
    invocationCondition(other.invocationCondition());
    removeWithUnconscious(other.removeWithUnconscious());
    showStyle(other.showStyle());
}
void BeastCard::clearData() {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    EffectCard::clearData();
    useLimit(0U);
    price(0U);
    invocationCondition({Status::Alive});
    removeWithUnconscious(true);
    showStyle(ShowStyle::Center);
}
bool BeastCard::opEquals(const BeastCard& other) const {
    if (linkId() != 0U) return linkId() == other.linkId() && comment() == other.comment();
    return use_limit_ == other.use_limit_ && price_ == other.price_ && invocation_condition_ == other.invocation_condition_ && remove_with_unconscious_ == other.remove_with_unconscious_ && show_style_ == other.show_style_ && opEqualsEffect(other);
}
bool BeastCard::equalsExcludeId(const BeastCard& other) const {
    if (linkId() != 0U) return linkId() == other.linkId() && comment() == other.comment();
    return use_limit_ == other.use_limit_ && price_ == other.price_ && invocation_condition_ == other.invocation_condition_ && remove_with_unconscious_ == other.remove_with_unconscious_ && show_style_ == other.show_style_ && equalsExcludeIdEffect(other);
}
void BeastCard::useLimit(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (use_limit_ != value) changed();
    use_limit_ = value;
}
void BeastCard::price(const std::uint32_t value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (price_ != value) changed();
    price_ = value;
}
void BeastCard::invocationCondition(lib::SharedSlice<Status> value) {
    lib::Array<Status> snapshot;
    snapshot.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index)
        snapshot.push_back(value[index]);
    setInvocationCondition(std::move(snapshot));
}

void BeastCard::invocationCondition(lib::SharedSlice<const Status> value) {
    lib::Array<Status> snapshot;
    snapshot.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index)
        snapshot.push_back(value[index]);
    setInvocationCondition(std::move(snapshot));
}

void BeastCard::invocationCondition(const lib::Array<Status>& value) {
    setInvocationCondition(value);
}

void BeastCard::invocationCondition(const std::initializer_list<Status> value) {
    setInvocationCondition(lib::Array<Status>(value));
}

void BeastCard::setInvocationCondition(lib::Array<Status> value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    // Dは通知前に入力を複製してそのコピーを並べ替
    // え、値が等しく通知を抑止する場合も公開する。
    std::sort(value.begin(), value.end(), [](Status a, Status b) {
        return enumIndex(a) < enumIndex(b);
    });
    if (invocation_condition_ != value) changed();
    invocation_condition_ = lib::SharedSlice<Status>(std::move(value));
}
void BeastCard::removeWithUnconscious(const bool value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (remove_with_unconscious_ != value) changed();
    remove_with_unconscious_ = value;
}
void BeastCard::showStyle(const ShowStyle value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    if (show_style_ != value) changed();
    show_style_ = value;
}
bool BeastCard::isOption() const {
    const auto owner = owner_.lock();
    return dynamic_cast<const CastCard*>(owner.get()) != nullptr && use_limit_ == 0U;
}

lib::Owner<BeastCard> BeastCard::dupBeast() const { auto result = std::make_shared<BeastCard>(nullptr, 0U, "", lib::Array<lib::Owner<CardImage>>{}, ""); result->deepCopy(*this); return result; }
lib::Owner<Card> BeastCard::dup() const { return dupBeast(); }

void BeastCard::toNodeImpl(XNode& node, const XMLOption& option, const OverData* over) const {
    if (linkId() != 0U && option.includeCard) {
        OverData overrideData{ id(), option.noLinkId ? 0U : linkId(), false, false };
        lib::Owner<const BeastCard> source; if (option.beast) source = option.beast(linkId());
        if (!source) source = std::make_shared<BeastCard>(option.sys, id(), "", lib::Array<lib::Owner<CardImage>>{}, "");
        source->toNodeImpl(node, option, &overrideData); return;
    }
    auto property = setEffProp(node, &option, over);
    if (linkId() == 0U || option.includeCard) {
        property.newElement("UseLimit", use_limit_);
        property.newElement("Price", price_);
        if (invocation_condition_ != lib::Array<Status>{Status::Alive}) {
            auto condition = property.newElement("InvocationCondition");
            for (const auto status : invocation_condition_) condition.newElement("Status", types::fromStatus(status));
        }
        if (!remove_with_unconscious_) property.newElement("RemovalCondition");
        if (show_style_ != ShowStyle::Center) property.newElement("ShowStyle", types::fromShowStyle(show_style_));
    }
}
String BeastCard::toXML(const XMLOption& option) const { return toNode(option).text(); }
String BeastCard::toXML(const XMLOption& option, const OverData& over) const { auto node = XNode::create(XML_NAME); setRootAttributes(node, option); toNodeImpl(node, option, &over); return node.text(); }
XNode BeastCard::toNode(const XMLOption& option) const { auto node = XNode::create(XML_NAME); setRootAttributes(node, option); toNodeImpl(node, option, nullptr); return node; }
XNode BeastCard::toNode(XNode& parent, const XMLOption& option) const { return toNode(parent, option, OverData{}); }
XNode BeastCard::toNode(XNode& parent, const XMLOption& option, const OverData& over) const { auto node = parent.newElement(XML_NAME); setRootAttributes(node, option); const OverData* p = (over.id || over.linkId || over.overHold) ? &over : nullptr; toNodeImpl(node, option, p); return node; }

lib::Owner<BeastCard> BeastCard::createFromNode(XNode& node, const XMLInfo& version) {
    if (node.name() != XML_NAME) throw CardException("Node is not beast card: " + node.name());
    auto result = std::make_shared<BeastCard>(&version.sys, 0U, "", lib::Array<lib::Owner<CardImage>>{}, "");
    std::uint64_t link = 0U; bool hold = false; String comment; const auto id = readLinkInfo(node, link, hold, comment);
    if (link != 0U) { result->id(id); result->linkId(link); result->comment(comment); return result; }
    const auto version_snapshot = version;
    // DはPropertyのコールバックをまたいで、このクロージャーのpriceLoaded状
    // 態を保持する。C++のD互換ディスパッチャーはstd::functionハンドラーをコ
    // ピーするため、状態を明示的に共有し、Property間と再試行時のDの動作を維持する。
    auto price_loaded = std::make_shared<bool>(false);
    node.set_handler("Property", [result, &node, version_snapshot, price_loaded](XNode& property) {
        for (const auto& child : property.children()) {
            if (child.name() == "UseLimit") result->use_limit_ = static_cast<std::uint32_t>(parseInt(child.value(), "UseLimit"));
            else if (child.name() == "Price") {
                result->price_ = static_cast<std::uint32_t>(parseInt(child.value(), "Price"));
                *price_loaded = true;
            }
            else if (child.name() == "InvocationCondition") { lib::Array<Status> values; for (const auto& status : child.children()) if (status.name() == "Status") values.push_back(types::toStatus(status.value())); result->invocationCondition(values); }
            else if (child.name() == "RemovalCondition") { result->remove_with_unconscious_ = false; for (const auto& status : child.children()) if (status.name() == "Status" && types::toStatus(status.value()) == Status::Unconscious) result->remove_with_unconscious_ = true; }
            else if (child.name() == "ShowStyle") result->show_style_ = types::toShowStyle(child.value());
        }
        result->loadEffProp(node, property, version_snapshot);
        if (!*price_loaded) result->price_ = 1000U;
    });
    result->loadEffV(node, version_snapshot); return result;
}

void BeastCard::owner(const lib::Owner<BeastOwner>& owner) { owner_.setShared(owner); }
void BeastCard::owner(BeastOwner* owner) noexcept {
    if (!owner) {
        owner_.setShared({});
        return;
    }
    // Dのクラス所有元はGCで保持される。ネイティブの親の監視参照は実際の共
    // 有制御ブロックを使い、スタック所有元では保護された借用リンクを使う。
    if (auto retained = std::dynamic_pointer_cast<BeastOwner>(owner->retainForCallback()))
        owner_.setShared(retained);
    else
        owner_.setBorrowed(owner, owner->beastOwnerLifetimeToken());
}
void BeastCard::owner(motion::Motion* owner) noexcept {
    this->owner(static_cast<BeastOwner*>(owner));
}
void BeastCard::owner(CwxPath* owner) noexcept {
    this->owner(dynamic_cast<BeastOwner*>(owner));
}
String BeastCard::cwxPath(const bool id) {
    auto owner = ownerPath(); if (!owner) return {};
    if (const auto parent = owner_.lock()) {
        if (id) return path::cpjoinid(owner, "beastcard", this->id());
        std::size_t index = 0U;
        for (const auto& value : parent->beasts()) {
            if (value.get() == this) break;
            ++index;
        }
        return path::cpjoin(owner, "beastcard", index, false);
    }
    return {};
}
lib::Observer<CwxPath> BeastCard::cwxParent() {
    return ownerObserver(owner_);
}
lib::Owner<CwxPath> BeastCard::findCWXPath(const StringView value) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    const String input(value);
    if (path::cpempty(input)) return Card::findCWXPath(input);
    if (linkId() != 0U) return findLinkedCardPath<BeastOwner>(
        *this, input, static_cast<lib::Owner<BeastCard> (BeastOwner::*)(std::uint64_t)>(
                        &BeastOwner::beast));
    return EffectCard::findCWXPath(input);
}

lib::Owner<InfoCard> InfoCard::dupInfo() const {
    auto result = std::make_shared<InfoCard>(0U, "", lib::Array<lib::Owner<CardImage>>{}, "");
    result->deepCopy(*this);
    return result;
}
lib::Owner<Card> InfoCard::dup() const { return dupInfo(); }
String InfoCard::toXML(const XMLOption& option) const { return toNode(option).text(); }
XNode InfoCard::toNode(const XMLOption& option) const { auto node = XNode::create(XML_NAME); setRootAttributes(node, option); static_cast<void>(setProp(node, &option, nullptr)); return node; }
XNode InfoCard::toNode(XNode& parent, const XMLOption& option) const { auto node = parent.newElement(XML_NAME); setRootAttributes(node, option); static_cast<void>(setProp(node, &option, nullptr)); return node; }
lib::Owner<InfoCard> InfoCard::createFromNode(XNode& node, const XMLInfo& version) {
    if (node.name() != XML_NAME) throw CardException("Node is not info card: " + node.name());
    auto result = std::make_shared<InfoCard>(0U, "", lib::Array<lib::Owner<CardImage>>{}, "");
    if (auto property = node.child("Property", false); property.valid()) result->loadProp(node, property, version);
    return result;
}
void InfoCard::owner(const lib::Owner<InfoOwner>& owner) { owner_.setShared(owner); }
String InfoCard::cwxPath(const bool id) {
    const auto owner = owner_.lock();
    const auto base = card::ownerPath(owner_);
    if (!base) return {};
    if (id) return path::cpjoinid(base, "infocard", this->id());
    std::size_t index = 0U; if (owner) for (const auto& value : owner->infos()) { if (value.get() == this) break; ++index; }
    return path::cpjoin(base, "infocard", index, false);
}
lib::Owner<CwxPath> InfoCard::findCWXPath(const StringView value) { return Card::findCWXPath(value); }
lib::Observer<CwxPath> InfoCard::cwxParent() { return ownerObserver(owner_); }

ActionCard::ActionCard(String name, const lib::Array<lib::Owner<CardImage>>& imagePaths, String desc)
    : EffectCard(nullptr, 0U, std::move(name), imagePaths, std::move(desc)) {}
ActionCard::~ActionCard() { invalidateMotionLifetime(); }
void ActionCard::setUseCounterImpl(const lib::Owner<UseCounter>&) { throw std::logic_error("ActionCard has no use counter"); }
void ActionCard::removeUseCounterImpl() { throw std::logic_error("ActionCard has no use counter"); }
void ActionCard::shallowCopy(const ActionCard& other) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
    shallowCopyEffectCard(other);
    action_type_ = other.action_type_;
}
void ActionCard::deepCopy(const ActionCard& other) {
    [[maybe_unused]] const auto self_owner = Card::weak_from_this().lock();
    [[maybe_unused]] const auto donor_owner = other.weak_from_this().lock();
    deepCopyEffectCard(other);
    action_type_ = other.action_type_;
}
bool ActionCard::opEquals(const ActionCard& other) const { return action_type_ == other.action_type_ && opEqualsEffect(other); }
bool ActionCard::equalsExcludeId(const ActionCard& other) const { return action_type_ == other.action_type_ && equalsExcludeIdEffect(other); }
std::uint64_t ActionCard::linkId() const { throw std::logic_error("ActionCard has no link ID"); }
void ActionCard::linkId(std::uint64_t) { throw std::logic_error("ActionCard has no link ID"); }
lib::Owner<ActionCard> ActionCard::dupAction() const { auto result = std::make_shared<ActionCard>("", lib::Array<lib::Owner<CardImage>>{}, ""); result->deepCopy(*this); return result; }
lib::Owner<Card> ActionCard::dup() const { return dupAction(); }
XNode ActionCard::toNode(XNode&, const XMLOption&) const { throw std::logic_error("ActionCard serialization is unsupported"); }
lib::Owner<ActionCard> ActionCard::createFromNode(XNode& node, const XMLInfo& version) {
    if (node.name() != XML_NAME) throw CardException("Node is not action card: " + node.name());
    auto result = std::make_shared<ActionCard>("", lib::Array<lib::Owner<CardImage>>{}, "");
    const auto version_snapshot = version;
    node.set_handler("Property", [result, &node, version_snapshot](XNode& property) {
        for (const auto& child : property.children()) if (child.name() == "Id") result->action_type_ = static_cast<ActionCardType>(parseInt(child.value(), "Id"));
        result->loadEffProp(node, property, version_snapshot, false);
    });
    result->loadEffV(node, version_snapshot); return result;
}
String ActionCard::cwxPath(bool) { throw std::logic_error("ActionCard has no CWX path"); }
lib::Owner<CwxPath> ActionCard::findCWXPath(StringView) { throw std::logic_error("ActionCard has no CWX path"); }
lib::Observer<CwxPath> ActionCard::cwxParent() { throw std::logic_error("ActionCard has no CWX path"); }

}  // namespace cwxeditor::cwx::card
