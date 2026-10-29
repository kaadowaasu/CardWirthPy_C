#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace cwxeditor::cwx::background {

namespace {

using ::cwxeditor::lib::String;
using ::cwxeditor::lib::StringView;

BgImageArray snapshotCells(
    const ::cwxeditor::lib::SharedSlice<BgImagePtr>& cells) {
    BgImageArray result;
    result.reserve(cells.size());
    for (const auto& cell : cells) result.push_back(cell);
    return result;
}

template <typename AppendEmpty, typename Emit>
void writeBackgroundCells(const BgImageArray& cells, bool can_inherit,
                          XNode& parent, AppendEmpty append_empty, Emit emit) {
    auto element = parent.newElement(BgImage::XML_NAME_M, String{});
    if (!cells.empty()) {
        if (can_inherit) {
            const auto image = std::dynamic_pointer_cast<ImageCell>(cells.front());
            if (!image || !image->flag().empty() || image->x() != 0 ||
                image->y() != 0 || image->width() != 632 ||
                image->height() != 420 || image->mask() ||
                !image->cellName().empty()) append_empty(element);
        }
        for (const auto& cell : cells) {
            // Dは全メンバーを呼ぶため、nullは省略するセルではなくエラー
            // 。nullのネイティブポインターを参照せず、例外を送出する。
            if (!cell) throw std::invalid_argument("Null background cell");
            emit(*cell, element);
        }
    } else if (can_inherit) {
        append_empty(element);
    }
}

// Dのテキスト利用者は、changed()が何もしないSimpleTextHolder
// に属する。名前変更を受理したコールバックがTextCellへ一度通知する
// 。通常の型付き利用者の処理を共有しつつ、この中間利用者の動作を維持する。
template <typename User>
class TextCellUser final : public User {
public:
    using User::User;
    void changed() override {}
};

class BackgroundPathProxy final : public CWXPath {
public:
    BackgroundPathProxy(CWXPath* target,
                        std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept
        : target_(target), lifetime_(std::move(lifetime)) {}

    [[nodiscard]] ::cwxeditor::lib::Owner<CWXPath> retainForCallback() override {
        const auto guard = liveLifetime();
        return guard ? target_->retainForCallback()
                     : ::cwxeditor::lib::Owner<CWXPath>{};
    }
    [[nodiscard]] String cwxPath(bool id) override {
        const auto guard = liveLifetime();
        return guard ? target_->cwxPath(id) : String{};
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
    [[nodiscard]] std::shared_ptr<path::OwnerLifetimeToken>
    liveLifetime() const noexcept {
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
    return std::make_shared<BackgroundPathProxy>(target, std::move(lifetime));
}

[[nodiscard]] String cellNameFormat(StringView cell_name, StringView value) {
    if (cell_name.empty()) return String(value);
    String result;
    result.reserve(cell_name.size() + value.size() + 2U);
    result.append(cell_name);
    result.append(": ");
    result.append(value);
    return result;
}

[[nodiscard]] String formatMessage(String format,
                                   const std::vector<String>& values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        const String token = "%" + std::to_string(index + 1U) + "$s";
        std::size_t position = 0;
        while ((position = format.find(token, position)) != String::npos) {
            format.replace(position, token.size(), values[index]);
            position += values[index].size();
        }
    }
    return format;
}

[[nodiscard]] String localizedCellName(const CProps& props,
                                       StringView cell_name, String value) {
    if (cell_name.empty()) return value;
    try {
        const auto& messages = props.requiredMsgs();
        return formatMessage(messages.nameWithCellName.value,
                             {String(cell_name), std::move(value)});
    } catch (const std::exception&) {
        // 初期化途中のEditorではメッセージ一覧がない場合
        // がある。その状態でもセル値を利用可能にし、通常のCProps
        // では上記のDと同じローカライズ形式に従う。
        return cellNameFormat(cell_name, value);
    }
}

[[nodiscard]] String imageBaseName(StringView value) {
    const auto slash = value.find_last_of("/\\");
    return slash == StringView::npos ? String(value)
                                     : String(value.substr(slash + 1U));
}

[[nodiscard]] String colorName(const CRGB& color) {
    char buffer[8]{};
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X",
                  static_cast<unsigned int>(color.r),
                  static_cast<unsigned int>(color.g),
                  static_cast<unsigned int>(color.b));
    return String(buffer);
}

template <typename T>
void newNumberAttr(XNode& node, StringView name, T value);

void requireNode(const XNode& node, StringView expected, StringView message) {
    if (node.name() != expected) {
        throw AreaException(String(message));
    }
}

void readColor(XNode& node, CRGB& color) {
    color.r = node.attr<std::uint32_t>("r", true);
    color.g = node.attr<std::uint32_t>("g", true);
    color.b = node.attr<std::uint32_t>("b", true);
    color.a = node.attr<std::uint32_t>("a", false, color.a);
}

void writeColor(XNode& parent, StringView name, const CRGB& color) {
    auto node = parent.newElement(name, String{});
    newNumberAttr(node, "r", color.r);
    newNumberAttr(node, "g", color.g);
    newNumberAttr(node, "b", color.b);
    newNumberAttr(node, "a", color.a);
}

void newTextAttr(XNode& node, StringView name, const String& value) {
    using NewAttr = void (XNode::*)(StringView, StringView);
    const auto method = static_cast<NewAttr>(&XNode::new_attr);
    (node.*method)(name, StringView(value));
}

template <typename T>
void newNumberAttr(XNode& node, StringView name, const T value) {
    newTextAttr(node, name, std::to_string(value));
}

void newBoolAttr(XNode& node, StringView name, const bool value) {
    newTextAttr(node, name, value ? "true" : "false");
}

}  // namespace

BgImage::BgImage(lib::StringView flag, const std::int32_t x, const std::int32_t y,
                 const std::int32_t width, const std::int32_t height,
                 const bool mask)
    : mask_(mask), x_(x), y_(y), width_(width), height_(height),
      flag_user_(std::make_shared<usecounter::FlagUser>(this)),
      cell_name_user_(std::make_shared<usecounter::CellNameUser>(this)) {
    // ネイティブのビューはnullと非nullの空文字列を区別する。FlagUser
    // は通知前や呼び出し側の寿命を超える前に、不変のバイト列を直ちに所有する。
    flag_user_->flag(flag);
}

BgImage::~BgImage() {
    const usecounter::detail::NativeCounterRetirement native_retirement;
    invalidateLifetime();
    // カウンターの取得済み状態により、Userオブジェクトがこのセルより長く生存
    // する場合がある。セルの保存領域を破棄する前に、所有しない逆参照を切り離す。
    if (flag_user_) {
        flag_user_->removeUseCounter();
        flag_user_->owner(static_cast<CWXPath*>(nullptr));
    }
    if (cell_name_user_) {
        cell_name_user_->removeUseCounter();
        cell_name_user_->owner(static_cast<CWXPath*>(nullptr));
    }
}

bool BgImage::equals(const BgImage& other) const noexcept {
    return typeid(*this) == typeid(other) && flag() == other.flag() &&
           x_ == other.x_ && y_ == other.y_ && width_ == other.width_ &&
           height_ == other.height_ && mask_ == other.mask_ &&
           cellName() == other.cellName() && layer_ == other.layer_ &&
           comment_ == other.comment_;
}

void BgImage::deepCopy(const BgImage& other) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    [[maybe_unused]] const auto donorLifetime = other.weak_from_this().lock();
    flag(other.flag());
    x(other.x_);
    y(other.y_);
    width(other.width_);
    height(other.height_);
    mask(other.mask_);
    cellName(other.cellName());
    layer(other.layer_);
    comment(other.comment_);
}

String BgImage::name(const CProps&) const { return "Background"; }

String BgImage::name() const {
    // DのAPIはローカライズされたラベルにCPropsを必要とする。このオ
    // ーバーロードはセル値だけを必要とする呼び出し側向けの、共通の代替処理。
    return "Background";
}

void BgImage::changed() {
    // 直接通知でも共有セルを生存させる。設定関数自身の保持は、コールバック後の書
    // き込みまで継続する必要がある。この保持が保護するのは今回の呼び出しだけ。
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    // Dはデリゲートを無条件に呼ぶ。カウンター登録自体は所有元に通知せず
    // 、TextCellの一時的な利用者のchanged()も何もしない
    // ため、接続時にネイティブの通知を抑止する必要はない。Dのデリゲート
    // は通知間で可変のクロージャー状態を保持するので、std::function
    // をコピーせず、登録済みの保持オブジェクトを生存させる。
    const auto change = change_;
    if (change && *change) (*change)();
}

void BgImage::setUseCounter(UseCounterPtr counter,
                            CWXPath* counter_owner) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (flag_user_) flag_user_->removeUseCounter();
    if (cell_name_user_) cell_name_user_->removeUseCounter();
    use_counter_ = std::move(counter);
    uc_owner_ = counter_owner;
    if (!flag_user_) flag_user_ = std::make_shared<usecounter::FlagUser>(this);
    if (!cell_name_user_) {
        cell_name_user_ = std::make_shared<usecounter::CellNameUser>(this);
    }
    flag_user_->setUseCounter(use_counter_ ? use_counter_->global() : UseCounterPtr{});
    cell_name_user_->setUseCounter(use_counter_, counter_owner);
}

void BgImage::removeUseCounter() noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (flag_user_) flag_user_->removeUseCounter();
    if (cell_name_user_) cell_name_user_->removeUseCounter();
    use_counter_.reset();
    uc_owner_ = nullptr;
}

void BgImage::owner(BackgroundOwner* value) noexcept {
    if (value) owner(value, value->backgroundLifetimeToken());
    else owner_.setRaw(nullptr);
}

void BgImage::owner(
    BackgroundOwner* value,
    std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept {
    if (!value) {
        owner_.setRaw(nullptr);
        return;
    }
    const auto retained = std::dynamic_pointer_cast<BackgroundOwner>(
        value->retainForCallback());
    if (retained) owner_.setSharedBorrowed(retained, value, std::move(lifetime));
    else owner_.setBorrowed(value, std::move(lifetime));
}

lib::NullableString BgImage::flag() const {
    return flag_user_ ? flag_user_->flag() : lib::NullableString{};
}

void BgImage::flag(String value) {
    assignFlag(lib::NullableString(std::move(value)));
}

void BgImage::assignFlag(lib::NullableString value) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (flag() != value) changed();
    if (!flag_user_) flag_user_ = std::make_shared<usecounter::FlagUser>(this);
    flag_user_->flag(value);
}

void BgImage::mask(const bool value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (mask_ != value) changed();
    mask_ = value;
}

void BgImage::x(const std::int32_t value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (x_ != value) changed();
    x_ = value;
}

void BgImage::y(const std::int32_t value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (y_ != value) changed();
    y_ = value;
}

void BgImage::width(std::int32_t value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (value < 0) value = 0;
    if (width_ != value) changed();
    width_ = value;
}

void BgImage::height(std::int32_t value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (value < 0) value = 0;
    if (height_ != value) changed();
    height_ = value;
}

String BgImage::cellName() const {
    return cell_name_user_ ? cell_name_user_->cellName() : String{};
}

void BgImage::cellName(String value) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (cellName() != value) changed();
    if (!cell_name_user_) {
        cell_name_user_ = std::make_shared<usecounter::CellNameUser>(this);
    }
    cell_name_user_->cellName(value);
}

void BgImage::layer(const std::int32_t value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (layer_ != value) changed();
    layer_ = value;
}

void BgImage::comment(String value) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (comment_ == value) return;
    changed();
    comment_ = std::move(value);
}

void BgImage::dupImpl(BgImage& cell) const {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    [[maybe_unused]] const auto targetLifetime = cell.retainLifetime();
    cell.flag(flag());
    cell.x_ = x_;
    cell.y_ = y_;
    cell.width_ = width_;
    cell.height_ = height_;
    cell.mask_ = mask_;
    cell.cellName(cellName());
    cell.layer_ = layer_;
    cell.comment_ = comment_;
}

void BgImage::toNodeCommon(XNode& element, const bool use_mask) const {
    if (use_mask) newTextAttr(element, "mask", utils::fromBool(mask_));
    const auto cell_name = cellName();
    if (!cell_name.empty()) newTextAttr(element, "cellname", cell_name);
    element.newElement("Flag", flag());
    auto location = element.newElement("Location", String{});
    newNumberAttr(location, "left", x_);
    newNumberAttr(location, "top", y_);
    auto size = element.newElement("Size", String{});
    newNumberAttr(size, "width", width_);
    newNumberAttr(size, "height", height_);
    if (layer_ != LAYER_BACK_CELL) element.newElement("Layer", std::to_string(layer_));
}

void BgImage::fromNodeCommon(XNode& node) {
    mask(utils::parseBool(node.attr("mask", false, utils::fromBool(false))));
    layer_ = LAYER_BACK_CELL;
    cellName(node.attr("cellname", false, String{}));
    node.onTag["Flag"] = [this](XNode& child) { flag(child.value()); };
    node.onTag["Location"] = [this](XNode& child) {
        x(child.attr<std::int32_t>("left", true));
        y(child.attr<std::int32_t>("top", true));
    };
    node.onTag["Size"] = [this](XNode& child) {
        width(child.attr<std::int32_t>("width", true));
        height(child.attr<std::int32_t>("height", true));
    };
    node.onTag["Layer"] = [this](XNode& child) {
        layer(child.valueTo<std::int32_t>());
    };
    node.parse();
}

::cwxeditor::lib::Owner<CWXPath> BgImage::selfOwner() {
    if (auto shared = retainForCallback()) return shared;
    if (!self_owner_proxy_) {
        self_owner_proxy_ = std::make_shared<BackgroundPathProxy>(
            this, background_lifetime_);
    }
    return self_owner_proxy_;
}

String BgImage::cwxPath(const bool id) {
    const auto owner = owner_.lock();
    if (!owner) return {};
    // コンストラクター内の接続は、enable_shared_from_this
    // へ制御ブロックが設定される前に起こり得る。借用した寿命トークンがこ
    // の同期アクセスを保護している間だけ、実際の共有所有元を再取得する。
    auto sharedOwner = owner.shared();
    if (!sharedOwner) {
        sharedOwner = std::dynamic_pointer_cast<BackgroundOwner>(
            owner->retainForCallback());
    }
    // スタック上の所有元は公開CWXPathハンドルを持たない。トークンは内部ア
    // クセスを保護するが、パスをそのスコープより長く生存させることはできない。
    if (!sharedOwner) return {};
    const auto cells = sharedOwner->backgroundImages();
    std::size_t index = 0;
    for (const auto& cell : cells) {
        if (cell && cell.get() == this) break;
        ++index;
    }
    // DのcCountUntilは逆参照先がメンバーでなければ-1を返す。cpjoin
    // のsize_t引数は、パスなしを返すのではなく、この番兵値を変換する。
    if (index == cells.size()) index = static_cast<std::size_t>(-1);
    return path::cpjoin(sharedOwner->cwxPath(id), "background", index);
}

::cwxeditor::lib::Owner<CWXPath> BgImage::findCWXPath(
    const std::string_view value) {
    return path::cpempty(value) ? selfOwner() : ::cwxeditor::lib::Owner<CWXPath>{};
}

::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>>
BgImage::cwxChilds() {
    return {};
}

::cwxeditor::lib::Observer<CWXPath> BgImage::cwxParent() {
    const auto owner = owner_.lock();
    if (!owner) return {};
    auto sharedOwner = owner.shared();
    if (!sharedOwner) {
        sharedOwner = std::dynamic_pointer_cast<BackgroundOwner>(
            owner->retainForCallback());
    }
    return sharedOwner ? std::static_pointer_cast<CWXPath>(sharedOwner)
                       : ::cwxeditor::lib::Observer<CWXPath>{};
}

void BgImage::appendEmptyToNode(XNode& node, const XMLOption&) {
    appendEmptyToNode(node);
}

void BgImage::appendEmptyToNode(XNode& node) {
    // XMLOptionは背景の直列化では使わない。このオーバーロー
    // ドはカードのオプションを持たない呼び出し側向けの具体的な処理。
    requireNode(node, XML_NAME_M, "Node is not BgImages");
    auto element = node.newElement(ImageCell::XML_NAME, String{});
    newTextAttr(element, "mask", "False");
    element.newElement("ImagePath", String{});
    element.newElement("Flag", String{});
    auto location = element.newElement("Location", String{});
    newNumberAttr(location, "left", 0);
    newNumberAttr(location, "top", 0);
    auto size = element.newElement("Size", String{});
    newTextAttr(size, "width", "632");
    newTextAttr(size, "height", "420");
}

BgImagePtr BgImage::createFromNode(XNode& node, const XMLInfo& version) {
    const auto name = node.name();
    if (name == ImageCell::XML_NAME) return ImageCell::createFromNode(node, version);
    if (name == TextCell::XML_NAME) return TextCell::createFromNode(node, version);
    if (name == ColorCell::XML_NAME) return ColorCell::createFromNode(node, version);
    if (name == PCCell::XML_NAME) return PCCell::createFromNode(node, version);
    throw AreaException("Unknown background cell: " + name);
}

BgImagePtr BgImage::createFromNode(XNode& node) {
    const auto name = node.name();
    if (name == ImageCell::XML_NAME) return ImageCell::createFromNode(node);
    if (name == TextCell::XML_NAME) return TextCell::createFromNode(node);
    if (name == ColorCell::XML_NAME) return ColorCell::createFromNode(node);
    if (name == PCCell::XML_NAME) return PCCell::createFromNode(node);
    throw AreaException("Unknown background cell: " + name);
}

BgImageArray BgImage::bgImagesFromNode(XNode& node, const bool can_inherit,
                                       const XMLInfo& version) {
    requireNode(node, XML_NAME_M, "Node is not BgImages");
    BgImageArray cells;
    node.onTag[String(ImageCell::XML_NAME)] = [&cells, &version](XNode& child) {
        cells.emplace_back(ImageCell::createFromNode(child, version));
    };
    node.onTag[String(TextCell::XML_NAME)] = [&cells, &version](XNode& child) {
        cells.emplace_back(TextCell::createFromNode(child, version));
    };
    node.onTag[String(ColorCell::XML_NAME)] = [&cells, &version](XNode& child) {
        cells.emplace_back(ColorCell::createFromNode(child, version));
    };
    node.onTag[String(PCCell::XML_NAME)] = [&cells, &version](XNode& child) {
        cells.emplace_back(PCCell::createFromNode(child, version));
    };
    node.parse();
    if (cells.empty()) return cells;
    const auto image = std::dynamic_pointer_cast<ImageCell>(cells.front());
    if (can_inherit && image && image->path().empty() && image->flag().empty() &&
        image->x() == 0 && image->y() == 0 && image->width() == 632 &&
        image->height() == 420 && !image->mask() && image->cellName().empty()) {
        cells.erase(cells.begin());
    }
    return cells;
}

BgImageArray BgImage::bgImagesFromNode(XNode& node, const bool can_inherit) {
    requireNode(node, XML_NAME_M, "Node is not BgImages");
    BgImageArray cells;
    node.onTag[String(ImageCell::XML_NAME)] = [&cells](XNode& child) {
        cells.emplace_back(ImageCell::createFromNode(child));
    };
    node.onTag[String(TextCell::XML_NAME)] = [&cells](XNode& child) {
        cells.emplace_back(TextCell::createFromNode(child));
    };
    node.onTag[String(ColorCell::XML_NAME)] = [&cells](XNode& child) {
        cells.emplace_back(ColorCell::createFromNode(child));
    };
    node.onTag[String(PCCell::XML_NAME)] = [&cells](XNode& child) {
        cells.emplace_back(PCCell::createFromNode(child));
    };
    node.parse();
    if (cells.empty()) return cells;
    const auto image = std::dynamic_pointer_cast<ImageCell>(cells.front());
    if (can_inherit && image && image->path().empty() && image->flag().empty() &&
        image->x() == 0 && image->y() == 0 && image->width() == 632 &&
        image->height() == 420 && !image->mask() && image->cellName().empty()) {
        cells.erase(cells.begin());
    }
    return cells;
}

void BgImage::toNode(const BgImageArray& cells, const bool can_inherit,
                     XNode& parent, const XMLOption& option) {
    writeBackgroundCells(cells, can_inherit, parent,
        [&](XNode& element) { appendEmptyToNode(element, option); },
        [&](const BgImage& cell, XNode& element) { cell.toNode(element, option); });
}

void BgImage::toNode(const BgImageArray& cells, const bool can_inherit,
                     XNode& parent) {
    writeBackgroundCells(cells, can_inherit, parent,
        [](XNode& element) { appendEmptyToNode(element); },
        [](const BgImage& cell, XNode& element) { cell.toNode(element); });
}

void BgImage::toNode(XNode& node) const {
    // XMLOptionは状態を渡さない。D形式のオーバーロードとオプ
    // ションなしの簡易オーバーロードは、同じ具体的な直列化処理を使う。
    toNodeImpl(node);
}

ImageCell::ImageCell() : ImageCell({}, "", 0, 0, 0, 0, false) {}

ImageCell::ImageCell(String path, lib::StringView flag, const std::int32_t x,
                     const std::int32_t y, const std::int32_t width,
                     const std::int32_t height, const bool mask)
    : BgImage(std::move(flag), x, y, width, height, mask),
      path_user_(std::make_shared<usecounter::PathUser>(this)) {
    path_user_->path(path);
}

ImageCell::~ImageCell() {
    const usecounter::detail::NativeCounterRetirement native_retirement;
    invalidateLifetime();
    if (path_user_) {
        path_user_->removeUseCounter();
        path_user_->owner(static_cast<CWXPath*>(nullptr));
    }
}

bool ImageCell::equals(const BgImage& other) const noexcept {
    const auto* image = dynamic_cast<const ImageCell*>(&other);
    return image != nullptr && path() == image->path() &&
           smoothing_ == image->smoothing_ && BgImage::equals(other);
}

void ImageCell::deepCopy(const BgImage& other) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    [[maybe_unused]] const auto donorLifetime = other.weak_from_this().lock();
    const auto* image = dynamic_cast<const ImageCell*>(&other);
    if (image == nullptr) throw std::runtime_error("other is not ImageCell");
    BgImage::deepCopy(other);
    path(image->path());
    smoothing(image->smoothing_);
}

String ImageCell::name(const CProps& props) const {
    const auto image_path = path();
    String value;
    try {
        const auto& messages = props.requiredMsgs();
        if (image_path.empty()) {
            value = messages.noSelectImage.value;
        } else if (utils::isBinImg(image_path)) {
            value = messages.defaultSelection(messages.imageIncluding.value);
        } else {
            value = imageBaseName(image_path);
        }
    } catch (const std::exception&) {
        if (image_path.empty()) {
            value = "(no image)";
        } else if (utils::isBinImg(image_path)) {
            value = "(embedded image)";
        } else {
            value = imageBaseName(image_path);
        }
    }
    return localizedCellName(props, cellName(), std::move(value));
}

BgImagePtr ImageCell::dup() const {
    [[maybe_unused]] const auto donorLifetime = retainLifetime();
    auto result = std::make_shared<ImageCell>(path(), flag(), x_, y_, width_,
                                              height_, mask_);
    dupImpl(*result);
    result->smoothing_ = smoothing_;
    return result;
}

String ImageCell::path() const {
    return path_user_ ? path_user_->path() : String{};
}

void ImageCell::path(String value) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (path() != value) changed();
    if (!path_user_) path_user_ = std::make_shared<usecounter::PathUser>(this);
    path_user_->path(value);
}

void ImageCell::smoothing(const Smoothing value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (smoothing_ != value) changed();
    smoothing_ = value;
}

String ImageCell::connectedFile() const {
    const auto value = path();
    return utils::isBinImg(value) ? String{} : value;
}

void ImageCell::setUseCounter(UseCounterPtr counter,
                              CWXPath* counter_owner) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    BgImage::setUseCounter(std::move(counter), counter_owner);
    if (path_user_) path_user_->removeUseCounter();
    if (!path_user_) path_user_ = std::make_shared<usecounter::PathUser>(this);
    path_user_->setUseCounter(use_counter_);
}

void ImageCell::removeUseCounter() noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (path_user_) path_user_->removeUseCounter();
    BgImage::removeUseCounter();
}

void ImageCell::toNode(XNode& node, const XMLOption&) const { toNodeImpl(node); }

void ImageCell::toNodeImpl(XNode& node) const {
    requireNode(node, XML_NAME_M, "Node is not BgImages");
    auto element = node.newElement(XML_NAME, String{});
    element.newElement("ImagePath", utils::encodePath(path()));
    newTextAttr(element, "smoothing", types::fromSmoothing(smoothing_));
    toNodeCommon(element, true);
}

::cwxeditor::lib::Owner<ImageCell> ImageCell::createFromNode(
    XNode& node, const XMLInfo&) {
    requireNode(node, XML_NAME, "Node is not BgImage");
    String path;
    node.onTag["ImagePath"] = [&path](XNode& child) {
        path = utils::decodePath(child.value());
    };
    node.parse();
    auto result = std::make_shared<ImageCell>(std::move(path), String{}, 0, 0,
                                              0, 0, false);
    result->fromNodeCommon(node);
    result->smoothing(types::toSmoothing(node.attr("smoothing", false,
                                                   String("Default"))));
    return result;
}

::cwxeditor::lib::Owner<ImageCell> ImageCell::createFromNode(XNode& node) {
    requireNode(node, XML_NAME, "Node is not BgImage");
    String path;
    node.onTag["ImagePath"] = [&path](XNode& child) {
        path = utils::decodePath(child.value());
    };
    node.parse();
    auto result = std::make_shared<ImageCell>(std::move(path), String{}, 0, 0,
                                              0, 0, false);
    result->fromNodeCommon(node);
    result->smoothing(types::toSmoothing(node.attr("smoothing", false,
                                                   String("Default"))));
    return result;
}

TextCell::TextCell() : BgImage("", 0, 0, 0, 0, false) {
    text_path_ = std::make_shared<TextPath>(this);
}

TextCell::TextCell(String text, String font_name, const std::uint32_t size,
                   const CRGB color, const bool bold, const bool italic,
                   const bool underline, const bool strike,
                   const bool vertical, const bool antialias,
                   const BorderingType bordering_type,
                   const CRGB bordering_color,
                   const std::uint32_t bordering_width,
                   const UpdateType update_type, lib::StringView flag,
                   const std::int32_t x, const std::int32_t y,
                   const std::int32_t width, const std::int32_t height,
                   const bool mask)
    : BgImage(std::move(flag), x, y, width, height, mask),
      text_(std::move(text)), font_name_(std::move(font_name)), size_(size),
      color_(color), bold_(bold), italic_(italic), underline_(underline),
      strike_(strike), vertical_(vertical), antialias_(antialias),
      bordering_type_(bordering_type), bordering_color_(bordering_color),
      bordering_width_(bordering_width), update_type_(update_type) {
    // DはここでSimpleTextHolderを構築し、初期テキストを検証する。
    std::vector<std::string> flags, steps, variants, fonts;
    std::vector<char> colors;
    msgutils::textUseItems(text_, flags, steps, variants, fonts, colors);
    text_path_ = std::make_shared<TextPath>(this);
}

TextCell::~TextCell() {
    const usecounter::detail::NativeCounterRetirement native_retirement;
    invalidateLifetime();
    if (text_path_) text_path_->owner_ = nullptr;
    removeTextUsers();
}

bool TextCell::equals(const BgImage& other) const noexcept {
    const auto* text = dynamic_cast<const TextCell*>(&other);
    return text != nullptr && text_ == text->text_ &&
           font_name_ == text->font_name_ && size_ == text->size_ &&
           color_ == text->color_ && bold_ == text->bold_ &&
           italic_ == text->italic_ && underline_ == text->underline_ &&
           strike_ == text->strike_ && vertical_ == text->vertical_ &&
           antialias_ == text->antialias_ &&
           bordering_type_ == text->bordering_type_ &&
           bordering_color_ == text->bordering_color_ &&
           bordering_width_ == text->bordering_width_ &&
           update_type_ == text->update_type_ && BgImage::equals(other);
}

void TextCell::deepCopy(const BgImage& other) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    [[maybe_unused]] const auto donorLifetime = other.weak_from_this().lock();
    const auto* text = dynamic_cast<const TextCell*>(&other);
    if (text == nullptr) throw std::runtime_error("other is not TextCell");
    BgImage::deepCopy(other);
    this->text(text->text_);
    fontName(text->font_name_);
    size(text->size_);
    color(text->color_);
    bold(text->bold_);
    italic(text->italic_);
    underline(text->underline_);
    strike(text->strike_);
    vertical(text->vertical_);
    antialias(text->antialias_);
    borderingType(text->bordering_type_);
    borderingColor(text->bordering_color_);
    borderingWidth(text->bordering_width_);
    updateType(text->update_type_);
}

String TextCell::name(const CProps& props) const {
    return localizedCellName(props, cellName(), utils::singleLine(text_));
}

BgImagePtr TextCell::dup() const {
    [[maybe_unused]] const auto donorLifetime = retainLifetime();
    auto result = std::make_shared<TextCell>(
        text_, font_name_, size_, color_, bold_, italic_, underline_, strike_,
        vertical_, antialias_, bordering_type_, bordering_color_,
        bordering_width_, update_type_, flag(), x_, y_, width_, height_, mask_);
    dupImpl(*result);
    return result;
}

void TextCell::text(const std::string_view value) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (text_ == value) return;
    // Dは先に通知し、SimpleTextHolderで解析してから利用者を
    // 解除する。そのため不正な置換内容では、古いテキストと参照を維持する。
    changed();
    std::vector<std::string> flags, steps, variants, fonts;
    std::vector<char> colors;
    msgutils::textUseItems(value, flags, steps, variants, fonts, colors);
    String next(value);
    text_ = std::move(next);
    rebuildTextUsers(flags, steps, variants, fonts);
}

void TextCell::fontName(String value) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (font_name_ == value) return;
    changed();
    font_name_ = std::move(value);
}

void TextCell::size(const std::uint32_t value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (size_ != value) changed();
    size_ = value;
}

void TextCell::color(const CRGB value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (color_ != value) changed();
    color_ = value;
}

void TextCell::bold(const bool value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (bold_ != value) changed();
    bold_ = value;
}

void TextCell::italic(const bool value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (italic_ != value) changed();
    italic_ = value;
}

void TextCell::underline(const bool value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (underline_ != value) changed();
    underline_ = value;
}

void TextCell::strike(const bool value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (strike_ != value) changed();
    strike_ = value;
}

void TextCell::vertical(const bool value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (vertical_ != value) changed();
    vertical_ = value;
}

void TextCell::antialias(const bool value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (antialias_ != value) changed();
    antialias_ = value;
}

void TextCell::borderingType(const BorderingType value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (bordering_type_ != value) changed();
    bordering_type_ = value;
}

void TextCell::borderingColor(const CRGB value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (bordering_color_ != value) changed();
    bordering_color_ = value;
}

void TextCell::borderingWidth(const std::uint32_t value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (bordering_width_ != value) changed();
    bordering_width_ = value;
}

void TextCell::updateType(const UpdateType value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (update_type_ != value) changed();
    update_type_ = value;
}

void TextCell::setUseCounter(UseCounterPtr counter,
                             CWXPath* counter_owner) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    removeTextUsers();
    BgImage::setUseCounter(std::move(counter), counter_owner);
    rebuildTextUsers();
}

void TextCell::removeUseCounter() noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    removeTextUsers();
    BgImage::removeUseCounter();
}

void TextCell::removeTextUsers() noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    const auto retire = [](const auto& users) {
        for (const auto& user : users) {
            if (!user) continue;
            user->removeUseCounter();
            user->owner(static_cast<CWXPath*>(nullptr));
        }
    };
    retire(text_flag_users_);
    retire(text_step_users_);
    retire(text_variant_users_);
    retire(text_path_users_);
    text_flag_users_.clear();
    text_step_users_.clear();
    text_variant_users_.clear();
    text_path_users_.clear();
}

void TextCell::rebuildTextUsers() {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    std::vector<std::string> flags, steps, variants, fonts;
    std::vector<char> colors;
    msgutils::textUseItems(text_, flags, steps, variants, fonts, colors);
    rebuildTextUsers(flags, steps, variants, fonts);
}

void TextCell::rebuildTextUsers(const std::vector<std::string>& flags,
                               const std::vector<std::string>& steps,
                               const std::vector<std::string>& variants,
                               const std::vector<std::string>& fonts) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    removeTextUsers();
    if (!use_counter_) return;
    const auto self = selfOwner();
    const auto global = use_counter_->global();
    for (const auto& id : flags) {
        auto user = std::make_shared<TextCellUser<usecounter::FlagUser>>(self);
        user->flag(id); user->setUseCounter(global);
        text_flag_users_.push_back(std::move(user));
    }
    for (const auto& id : steps) {
        auto user = std::make_shared<TextCellUser<usecounter::StepUser>>(self);
        user->step(id); user->setUseCounter(global);
        text_step_users_.push_back(std::move(user));
    }
    for (const auto& id : variants) {
        auto user = std::make_shared<TextCellUser<usecounter::VariantUser>>(self);
        user->variant(id); user->setUseCounter(global);
        text_variant_users_.push_back(std::move(user));
    }
    // DのTextCellはSimpleTextHolderを所有するため、メッセージのフォント指
    // 定記号はここでは文字どおり扱う。変数の利用者と、上記の移行用パス利用者の後処理は維持する。
    (void)fonts;
}

// カウンターはこのコールバックの後で既存の利用者を移す。ここで登
// 録を作り直すと、古い利用者の移動中に別の一式を作ってしまう。
bool TextCell::renameFlagInText(std::string_view oldId, std::string_view newId) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    text_ = msgutils::replTextUseFlag(text_, oldId, newId);
    changed();
    return true;
}
bool TextCell::renameStepInText(std::string_view oldId, std::string_view newId) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    text_ = msgutils::replTextUseStep(text_, oldId, newId);
    changed();
    return true;
}
bool TextCell::renameVariantInText(std::string_view oldId, std::string_view newId) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    text_ = msgutils::replTextUseVariant(text_, oldId, newId);
    changed();
    return true;
}
bool TextCell::renameFontInText(std::string_view oldId, std::string_view newId) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    text_ = msgutils::replTextUseFont(text_, oldId, newId);
    changed();
    return true;
}

std::vector<std::string> TextCell::flagsInText() const {
    std::vector<std::string> flags;
    std::vector<std::string> steps;
    std::vector<std::string> variants;
    std::vector<std::string> fonts;
    std::vector<char> colors;
    msgutils::textUseItems(text_, flags, steps, variants, fonts, colors);
    return flags;
}

std::vector<std::string> TextCell::stepsInText() const {
    std::vector<std::string> flags;
    std::vector<std::string> steps;
    std::vector<std::string> variants;
    std::vector<std::string> fonts;
    std::vector<char> colors;
    msgutils::textUseItems(text_, flags, steps, variants, fonts, colors);
    return steps;
}

std::vector<std::string> TextCell::variantsInText() const {
    std::vector<std::string> flags;
    std::vector<std::string> steps;
    std::vector<std::string> variants;
    std::vector<std::string> fonts;
    std::vector<char> colors;
    msgutils::textUseItems(text_, flags, steps, variants, fonts, colors);
    return variants;
}

std::vector<std::string> TextCell::fontsInText() const {
    std::vector<std::string> flags;
    std::vector<std::string> steps;
    std::vector<std::string> variants;
    std::vector<std::string> fonts;
    std::vector<char> colors;
    msgutils::textUseItems(text_, flags, steps, variants, fonts, colors);
    return fonts;
}

std::vector<char> TextCell::colorsInText() const {
    std::vector<std::string> flags;
    std::vector<std::string> steps;
    std::vector<std::string> variants;
    std::vector<std::string> fonts;
    std::vector<char> colors;
    msgutils::textUseItems(text_, flags, steps, variants, fonts, colors);
    return colors;
}

std::vector<char> TextCell::namesInText() const {
    return msgutils::namesInText(text_, false);
}

void TextCell::changeInText(const std::size_t index, const std::string_view id) {
    auto flags = flagsInText();
    if (index < flags.size()) changeFlagInText(index, id);
}

void TextCell::changeFlagInText(const std::size_t index,
                                const std::string_view id) {
    const auto values = flagsInText();
    if (index >= values.size()) throw std::out_of_range("flag text index");
    text(msgutils::replTextUseFlag(text_, values[index], id));
}

void TextCell::changeStepInText(const std::size_t index,
                                const std::string_view id) {
    const auto values = stepsInText();
    if (index >= values.size()) throw std::out_of_range("step text index");
    text(msgutils::replTextUseStep(text_, values[index], id));
}

void TextCell::changeVariantInText(const std::size_t index,
                                   const std::string_view id) {
    const auto values = variantsInText();
    if (index >= values.size()) throw std::out_of_range("variant text index");
    text(msgutils::replTextUseVariant(text_, values[index], id));
}

void TextCell::changePathInText(const std::size_t index,
                                const std::string_view id) {
    const auto values = fontsInText();
    if (index >= values.size()) throw std::out_of_range("font path text index");
    text(msgutils::replTextUseFont(text_, values[index], id));
}

void TextCell::toNode(XNode& node, const XMLOption&) const { toNodeImpl(node); }

void TextCell::toNodeImpl(XNode& node) const {
    requireNode(node, XML_NAME_M, "Node is not BgImages");
    auto element = node.newElement(XML_NAME, String{});
    element.newElement("Text", utils::encodeLf(text_));
    auto font = element.newElement("Font", font_name_);
    newNumberAttr(font, "size", size_);
    newBoolAttr(font, "bold", bold_);
    newBoolAttr(font, "italic", italic_);
    newBoolAttr(font, "underline", underline_);
    newBoolAttr(font, "strike", strike_);
    element.newElement("Vertical", vertical_);
    if (antialias_) element.newElement("Antialias", antialias_);
    writeColor(element, "Color", color_);
    if (bordering_type_ != BorderingType::None) {
        auto bordering = element.newElement("Bordering", String{});
        newTextAttr(bordering, "type",
                    types::fromBorderingType(bordering_type_));
        newNumberAttr(bordering, "width", bordering_width_);
        writeColor(bordering, "Color", bordering_color_);
    }
    element.newElement("UpdateType", types::fromUpdateType(update_type_));
    toNodeCommon(element, false);
}

::cwxeditor::lib::Owner<TextCell> TextCell::createFromNode(
    XNode& node, const XMLInfo&) {
    requireNode(node, XML_NAME, "Node is not TextCell");
    String text;
    String font_name;
    std::uint32_t size = 1;
    CRGB color{0, 0, 0, 255};
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool vertical = false;
    bool antialias = false;
    BorderingType bordering_type = BorderingType::None;
    CRGB bordering_color{255, 255, 255, 255};
    std::uint32_t bordering_width = 1;
    UpdateType update_type = UpdateType::Variables;

    node.onTag["Text"] = [&text](XNode& child) {
        text = utils::decodeLf2(child.value());
    };
    node.onTag["Font"] = [&font_name, &size, &bold, &italic, &underline,
                            &strike](XNode& child) {
        font_name = child.value();
        size = child.attr<std::uint32_t>("size", true);
        bold = child.attr<bool>("bold", false, bold);
        italic = child.attr<bool>("italic", false, italic);
        underline = child.attr<bool>("underline", false, underline);
        strike = child.attr<bool>("strike", false, strike);
    };
    node.onTag["Vertical"] = [&vertical](XNode& child) {
        vertical = child.valueTo<bool>();
    };
    node.onTag["Antialias"] = [&antialias](XNode& child) {
        antialias = child.valueTo<bool>();
    };
    node.onTag["Color"] = [&color](XNode& child) { readColor(child, color); };
    node.onTag["Bordering"] = [&bordering_type, &bordering_width,
                                &bordering_color](XNode& child) {
        bordering_type = types::toBorderingType(child.attr("type", true));
        bordering_width = child.attr<std::uint32_t>("width", false,
                                                    bordering_width);
        child.onTag["Color"] = [&bordering_color](XNode& color_node) {
            readColor(color_node, bordering_color);
        };
        child.parse();
    };
    node.onTag["UpdateType"] = [&update_type](XNode& child) {
        update_type = types::toUpdateType(child.value());
    };
    node.parse();

    auto result = std::make_shared<TextCell>(
        std::move(text), std::move(font_name), size, color, bold, italic,
        underline, strike, vertical, antialias, bordering_type, bordering_color,
        bordering_width, update_type, String{}, 0, 0, 0, 0, false);
    result->fromNodeCommon(node);
    return result;
}

::cwxeditor::lib::Owner<TextCell> TextCell::createFromNode(XNode& node) {
    requireNode(node, XML_NAME, "Node is not TextCell");
    String text;
    String font_name;
    std::uint32_t size = 1;
    CRGB color{0, 0, 0, 255};
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool vertical = false;
    bool antialias = false;
    BorderingType bordering_type = BorderingType::None;
    CRGB bordering_color{255, 255, 255, 255};
    std::uint32_t bordering_width = 1;
    UpdateType update_type = UpdateType::Variables;
    node.onTag["Text"] = [&text](XNode& child) { text = utils::decodeLf2(child.value()); };
    node.onTag["Font"] = [&font_name, &size, &bold, &italic, &underline,
                            &strike](XNode& child) {
        font_name = child.value();
        size = child.attr<std::uint32_t>("size", true);
        bold = child.attr<bool>("bold", false, bold);
        italic = child.attr<bool>("italic", false, italic);
        underline = child.attr<bool>("underline", false, underline);
        strike = child.attr<bool>("strike", false, strike);
    };
    node.onTag["Vertical"] = [&vertical](XNode& child) { vertical = child.valueTo<bool>(); };
    node.onTag["Antialias"] = [&antialias](XNode& child) { antialias = child.valueTo<bool>(); };
    node.onTag["Color"] = [&color](XNode& child) { readColor(child, color); };
    node.onTag["Bordering"] = [&bordering_type, &bordering_width,
                                &bordering_color](XNode& child) {
        bordering_type = types::toBorderingType(child.attr("type", true));
        bordering_width = child.attr<std::uint32_t>("width", false,
                                                    bordering_width);
        child.onTag["Color"] = [&bordering_color](XNode& color_node) {
            readColor(color_node, bordering_color);
        };
        child.parse();
    };
    node.onTag["UpdateType"] = [&update_type](XNode& child) {
        update_type = types::toUpdateType(child.value());
    };
    node.parse();
    auto result = std::make_shared<TextCell>(
        std::move(text), std::move(font_name), size, color, bold, italic,
        underline, strike, vertical, antialias, bordering_type, bordering_color,
        bordering_width, update_type, String{}, 0, 0, 0, 0, false);
    result->fromNodeCommon(node);
    return result;
}

::cwxeditor::lib::Owner<CWXPath> TextCell::findCWXPath(
    const std::string_view value) {
    if (path::cpempty(value)) return selfOwner();
    if (path::cpcategory(value) != "text" || !text_path_) return {};
    return text_path_->findCWXPath(path::cpbottom(value));
}

TextPath::TextPath(TextCell* owner)
    : owner_(owner),
      owner_lifetime_(owner ? owner->background_lifetime_
                            : std::weak_ptr<path::OwnerLifetimeToken>{}) {}

TextPath::~TextPath() {
    if (lifetime_) lifetime_->alive.store(false, std::memory_order_release);
}

TextCell* TextPath::liveOwner() const noexcept {
    if (!owner_) return nullptr;
    const auto lifetime = owner_lifetime_.lock();
    if (!lifetime || !lifetime->alive.load(std::memory_order_acquire)) return nullptr;
    return owner_;
}

String TextPath::cwxPath(const bool id) {
    const auto owner = liveOwner();
    if (owner == nullptr) return {};
    const auto parent = owner->cwxPath(id);
    return parent.empty() ? String("text") : parent + "/text";
}

::cwxeditor::lib::Owner<CWXPath> TextPath::findCWXPath(
    const std::string_view value) {
    if (!path::cpempty(value)) return {};
    try {
        return std::static_pointer_cast<CWXPath>(shared_from_this());
    } catch (const std::bad_weak_ptr&) {
        // DのTextPathはクラス参照。ネイティブのスタック上のインスタンスには
        // 、オブジェクトより長く生存するハンドルではなく、無効化可能な代理を使う。
        if (!lifetime_ || !lifetime_->alive.load(std::memory_order_acquire)) return {};
        return std::make_shared<BackgroundPathProxy>(this, lifetime_);
    }
}

::cwxeditor::lib::Observer<CWXPath> TextPath::cwxParent() {
    const auto owner = liveOwner();
    if (owner == nullptr) return {};
    return owner->selfOwner();
}

void TextPath::changed() {
    if (const auto owner = liveOwner()) owner->changed();
}

ColorCell::ColorCell()
    : ColorCell(BlendMode::Normal, GradientDir::None,
                CRGB{255, 255, 255, 255}, CRGB{0, 0, 0, 255}, "", 0, 0, 0, 0,
                false) {}

ColorCell::ColorCell(const BlendMode blend_mode, const GradientDir gradient_dir,
                     const CRGB color1, const CRGB color2, lib::StringView flag,
                     const std::int32_t x, const std::int32_t y,
                     const std::int32_t width, const std::int32_t height,
                     const bool mask)
    : BgImage(std::move(flag), x, y, width, height, mask),
      blend_mode_(blend_mode), gradient_dir_(gradient_dir), color1_(color1),
      color2_(color2) {}

bool ColorCell::equals(const BgImage& other) const noexcept {
    const auto* color = dynamic_cast<const ColorCell*>(&other);
    return color != nullptr && blend_mode_ == color->blend_mode_ &&
           gradient_dir_ == color->gradient_dir_ && color1_ == color->color1_ &&
           color2_ == color->color2_ && BgImage::equals(other);
}

void ColorCell::deepCopy(const BgImage& other) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    [[maybe_unused]] const auto donorLifetime = other.weak_from_this().lock();
    const auto* color = dynamic_cast<const ColorCell*>(&other);
    if (color == nullptr) throw std::runtime_error("other is not ColorCell");
    BgImage::deepCopy(other);
    blendMode(color->blend_mode_);
    gradientDir(color->gradient_dir_);
    color1(color->color1_);
    color2(color->color2_);
}

String ColorCell::name(const CProps& props) const {
    String value = colorName(color1_);
    if (gradient_dir_ != GradientDir::None) {
        value += "-";
        value += colorName(color2_);
    }
    return localizedCellName(props, cellName(), std::move(value));
}

BgImagePtr ColorCell::dup() const {
    [[maybe_unused]] const auto donorLifetime = retainLifetime();
    auto result = std::make_shared<ColorCell>(blend_mode_, gradient_dir_, color1_,
                                              color2_, flag(), x_, y_, width_,
                                              height_, mask_);
    dupImpl(*result);
    return result;
}

void ColorCell::blendMode(const BlendMode value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (blend_mode_ != value) changed();
    blend_mode_ = value;
}

void ColorCell::gradientDir(const GradientDir value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (gradient_dir_ != value) changed();
    gradient_dir_ = value;
}

void ColorCell::color1(const CRGB value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (color1_ != value) changed();
    color1_ = value;
}

void ColorCell::color2(const CRGB value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (color2_ != value) changed();
    color2_ = value;
}

void ColorCell::toNode(XNode& node, const XMLOption&) const { toNodeImpl(node); }

void ColorCell::toNodeImpl(XNode& node) const {
    requireNode(node, XML_NAME_M, "Node is not BgImages");
    auto element = node.newElement(XML_NAME, String{});
    element.newElement("BlendMode", types::fromBlendMode(blend_mode_));
    writeColor(element, "Color", color1_);
    if (gradient_dir_ != GradientDir::None) {
        auto gradient = element.newElement("Gradient", String{});
        newTextAttr(gradient, "direction",
                    types::fromGradientDir(gradient_dir_));
        writeColor(gradient, "EndColor", color2_);
    }
    toNodeCommon(element, false);
}

::cwxeditor::lib::Owner<ColorCell> ColorCell::createFromNode(
    XNode& node, const XMLInfo&) {
    // 従来のTextCellという表記も含め、Dの診断文を維持する。
    requireNode(node, XML_NAME, "Node is not TextCell");
    BlendMode blend_mode = BlendMode::Normal;
    GradientDir gradient_dir = GradientDir::None;
    CRGB color1{255, 255, 255, 255};
    CRGB color2{255, 255, 255, 255};
    bool has_end_color = false;
    node.onTag["BlendMode"] = [&blend_mode](XNode& child) {
        blend_mode = types::toBlendMode(child.value());
    };
    node.onTag["Color"] = [&color1](XNode& child) { readColor(child, color1); };
    node.parse();
    node.onTag["Gradient"] = [&gradient_dir, &color2, &has_end_color](XNode& child) {
        gradient_dir = types::toGradientDir(child.attr("direction", true));
        child.onTag["EndColor"] = [&color2, &has_end_color](XNode& end) {
            readColor(end, color2);
            has_end_color = true;
        };
        child.parse();
    };
    node.parse();
    if (!has_end_color) color2 = color1;
    auto result = std::make_shared<ColorCell>(blend_mode, gradient_dir, color1,
                                              color2, String{}, 0, 0, 0, 0,
                                              false);
    result->fromNodeCommon(node);
    return result;
}

::cwxeditor::lib::Owner<ColorCell> ColorCell::createFromNode(XNode& node) {
    requireNode(node, XML_NAME, "Node is not TextCell");
    BlendMode blend_mode = BlendMode::Normal;
    GradientDir gradient_dir = GradientDir::None;
    CRGB color1{255, 255, 255, 255};
    CRGB color2{255, 255, 255, 255};
    bool has_end_color = false;
    node.onTag["BlendMode"] = [&blend_mode](XNode& child) {
        blend_mode = types::toBlendMode(child.value());
    };
    node.onTag["Color"] = [&color1](XNode& child) { readColor(child, color1); };
    node.parse();
    node.onTag["Gradient"] = [&gradient_dir, &color2, &has_end_color](XNode& child) {
        gradient_dir = types::toGradientDir(child.attr("direction", true));
        child.onTag["EndColor"] = [&color2, &has_end_color](XNode& end) {
            readColor(end, color2);
            has_end_color = true;
        };
        child.parse();
    };
    node.parse();
    if (!has_end_color) color2 = color1;
    auto result = std::make_shared<ColorCell>(blend_mode, gradient_dir, color1,
                                              color2, String{}, 0, 0, 0, 0,
                                              false);
    result->fromNodeCommon(node);
    return result;
}

PCCell::PCCell() : PCCell(0, false, "", 0, 0, 0, 0, false) {}

PCCell::PCCell(const std::uint32_t pc_number, const bool expand, lib::StringView flag,
               const std::int32_t x, const std::int32_t y,
               const std::int32_t width, const std::int32_t height,
               const bool mask)
    : BgImage(std::move(flag), x, y, width, height, mask),
      pc_number_(pc_number), expand_(expand) {}

bool PCCell::equals(const BgImage& other) const noexcept {
    const auto* pc = dynamic_cast<const PCCell*>(&other);
    return pc != nullptr && pc_number_ == pc->pc_number_ &&
           expand_ == pc->expand_ && smoothing_ == pc->smoothing_ &&
           BgImage::equals(other);
}

void PCCell::deepCopy(const BgImage& other) {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    [[maybe_unused]] const auto donorLifetime = other.weak_from_this().lock();
    const auto* pc = dynamic_cast<const PCCell*>(&other);
    if (pc == nullptr) throw std::runtime_error("other is not PCCell");
    BgImage::deepCopy(other);
    pcNumber(pc->pc_number_);
    expand(pc->expand_);
    smoothing(pc->smoothing_);
}

String PCCell::name(const CProps& props) const {
    String value;
    try {
        const auto& messages = props.requiredMsgs();
        value = pc_number_ == 0
                    ? messages.pcCellNoSet.value
                    : formatMessage(messages.pc.value,
                                    {std::to_string(pc_number_)});
    } catch (const std::exception&) {
        value = pc_number_ == 0 ? "(PC not selected)"
                                : "PC " + std::to_string(pc_number_);
    }
    return localizedCellName(props, cellName(), std::move(value));
}

BgImagePtr PCCell::dup() const {
    [[maybe_unused]] const auto donorLifetime = retainLifetime();
    auto result = std::make_shared<PCCell>(pc_number_, expand_, flag(), x_, y_,
                                           width_, height_, mask_);
    dupImpl(*result);
    result->smoothing_ = smoothing_;
    return result;
}

void PCCell::pcNumber(const std::uint32_t value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (pc_number_ != value) changed();
    pc_number_ = value;
}

void PCCell::expand(const bool value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (expand_ != value) changed();
    expand_ = value;
}

void PCCell::smoothing(const Smoothing value) noexcept {
    [[maybe_unused]] const auto receiverLifetime = retainLifetime();
    if (smoothing_ != value) changed();
    smoothing_ = value;
}

void PCCell::toNode(XNode& node, const XMLOption&) const { toNodeImpl(node); }

void PCCell::toNodeImpl(XNode& node) const {
    requireNode(node, XML_NAME_M, "Node is not BgImages");
    auto element = node.newElement(XML_NAME, String{});
    if (expand_) newBoolAttr(element, "expand", expand_);
    element.newElement("PCNumber", std::to_string(pc_number_));
    newTextAttr(element, "smoothing", types::fromSmoothing(smoothing_));
    toNodeCommon(element, false);
}

::cwxeditor::lib::Owner<PCCell> PCCell::createFromNode(XNode& node,
                                                       const XMLInfo&) {
    requireNode(node, XML_NAME, "Node is not PCCell");
    const auto expand = node.attr<bool>("expand", false, false);
    std::uint32_t pc_number = 0;
    node.onTag["PCNumber"] = [&pc_number](XNode& child) {
        pc_number = child.valueTo<std::uint32_t>();
    };
    node.parse();
    auto result = std::make_shared<PCCell>(pc_number, expand, String{}, 0, 0,
                                           0, 0, false);
    result->fromNodeCommon(node);
    result->smoothing(types::toSmoothing(node.attr("smoothing", false,
                                                 String("Default"))));
    return result;
}

::cwxeditor::lib::Owner<PCCell> PCCell::createFromNode(XNode& node) {
    requireNode(node, XML_NAME, "Node is not PCCell");
    const auto expand = node.attr<bool>("expand", false, false);
    std::uint32_t pc_number = 0;
    node.onTag["PCNumber"] = [&pc_number](XNode& child) {
        pc_number = child.valueTo<std::uint32_t>();
    };
    node.parse();
    auto result = std::make_shared<PCCell>(pc_number, expand, String{}, 0, 0,
                                           0, 0, false);
    result->fromNodeCommon(node);
    result->smoothing(types::toSmoothing(node.attr("smoothing", false,
                                                 String("Default"))));
    return result;
}

BgImageContainer::BgImageContainer(BgImageArray cells, UseCounterPtr counter)
    : use_counter_(std::move(counter)) {
    backs(std::move(cells));
}

BgImageContainer::~BgImageContainer() {
    const usecounter::detail::NativeCounterRetirement native_retirement;
    for (auto& cell : cells_) {
        if (cell && cell->owner() == this) unbind(cell);
    }
    if (background_lifetime_)
        background_lifetime_->alive.store(false, std::memory_order_release);
}

String BgImageContainer::cwxPath(const bool) { return {}; }

::cwxeditor::lib::Owner<CWXPath> BgImageContainer::findCWXPath(
    const std::string_view value) {
    if (path::cpempty(value)) {
        return safePathOwner(this, backgroundLifetimeToken());
    }
    if (path::cpcategory(value) != "background") return {};
    const auto index = path::cpindex(value);
    if (index >= cells_.size() || !cells_[index]) return {};
    return cells_[index]->findCWXPath(path::cpbottom(value));
}

::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>>
BgImageContainer::cwxChilds() {
    ::cwxeditor::lib::Array<::cwxeditor::lib::Observer<CWXPath>> result;
    result.reserve(cells_.size());
    for (const auto& cell : cells_) {
        if (cell) result.emplace_back(cell);
    }
    return result;
}

::cwxeditor::lib::Observer<CWXPath> BgImageContainer::cwxParent() {
    return {};
}

void BgImageContainer::dispose() { backs({}); }

void BgImageContainer::bind(BgImagePtr& cell) {
    // Dは渡された全セルを参照する。コンテナーに未接続のnull
    // 項目を黙って残さず、ネイティブ側の失敗を報告する。
    if (!cell) throw std::invalid_argument("Null background cell");
    cell->owner(this, backgroundLifetimeToken());
    if (use_counter_) cell->setUseCounter(use_counter_, this);
}

void BgImageContainer::unbind(BgImagePtr& cell) noexcept {
    if (!cell) return;
    cell->owner(nullptr);
    cell->removeUseCounter();
}

void BgImageContainer::backs(BgImageArray cells) {
    for (auto& cell : cells_) unbind(cell);
    for (auto& cell : cells) bind(cell);
    cells_ = ::cwxeditor::lib::SharedSlice<BgImagePtr>(std::move(cells));
}

void BgImageContainer::append(BgImagePtr cell) {
    bind(cell);
    auto cells = snapshotCells(cells_);
    cells.push_back(std::move(cell));
    cells_ = ::cwxeditor::lib::SharedSlice<BgImagePtr>(std::move(cells));
}

void BgImageContainer::insert(const std::size_t index, BgImagePtr cell) {
    if (index == cells_.size()) {
        append(std::move(cell));
        return;
    }
    bind(cell);
    // Dは二つの配列スライスを評価する前にセルを接続する。挿入イ
    // ンデックスが範囲外の場合も、この副作用の順序を維持する。
    if (index > cells_.size()) throw std::out_of_range("background index");
    auto cells = snapshotCells(cells_);
    cells.insert(cells.begin() + static_cast<std::ptrdiff_t>(index),
                 std::move(cell));
    cells_ = ::cwxeditor::lib::SharedSlice<BgImagePtr>(std::move(cells));
}

void BgImageContainer::set(const std::size_t index, BgImagePtr cell) {
    const auto oldCell = cells_.at(index);
    if (!oldCell) throw std::invalid_argument("Null background cell");
    // Dはここで古いセルの所有元だけを解除する。カウンターからの削除はremoveBgImage/backs
    // が担当するため、この置換経路には追加しない。
    oldCell->owner(nullptr);
    bind(cell);
    cells_[index] = std::move(cell);
}

void BgImageContainer::removeBgImage(const std::size_t index) {
    if (index >= cells_.size()) throw std::out_of_range("background index");
    unbind(cells_[index]);
    auto cells = snapshotCells(cells_);
    cells.erase(cells.begin() + static_cast<std::ptrdiff_t>(index));
    cells_ = ::cwxeditor::lib::SharedSlice<BgImagePtr>(std::move(cells));
}

void BgImageContainer::swapBacks(const std::size_t index1,
                                  const std::size_t index2) {
    if (index1 >= cells_.size() || index2 >= cells_.size()) {
        throw std::out_of_range("background index");
    }
    // Dの配列範囲外エラーをネイティブの範囲エラーで置き換える。代入はDの
    // 第1・第2・第1の順序を維持し、同じインデックスの交換も同様に扱う。
    const auto temporary = cells_.at(index1);
    cells_.at(index1) = cells_.at(index2);
    cells_.at(index2) = temporary;
}

String BgImageContainer::BtoXML(const BgImageArray& cells,
                                const XMLOption& option) {
    auto document = XNode::create("MenuCardsAndBgImages");
    if (!cells.empty()) {
        auto element = document.newElement(BgImage::XML_NAME_M, String{});
        for (const auto& cell : cells) {
            if (cell) cell->toNode(element, option);
        }
    }
    return document.text();
}

String BgImageContainer::BtoXML(const BgImageArray& cells) {
    auto document = XNode::create("MenuCardsAndBgImages");
    if (!cells.empty()) {
        auto element = document.newElement(BgImage::XML_NAME_M, String{});
        for (const auto& cell : cells) {
            if (cell) cell->toNode(element);
        }
    }
    return document.text();
}

bool BgImageContainer::BfromXML(StringView xml, BgImageArray& cells,
                                const XMLInfo& version) noexcept {
    // Dの文字列引数は、セルが呼び出し側の入力文字列を所有していても、出
    // 力配列の消去後も有効。ネイティブの借用ビューは先にコピーしておく。
    try {
        const String input(xml);
        cells.clear();
        auto document = XNode::parse(input);
        if (document.name() != "MenuCardsAndBgImages") return false;
        document.onTag[String(BgImage::XML_NAME_M)] = [&cells, &version](XNode& node) {
            node.onTag[{}] = [&cells, &version](XNode& child) {
                cells.emplace_back(BgImage::createFromNode(child, version));
            };
            node.onTag["*"] = node.onTag[{}];
            node.parse();
        };
        document.parse();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool BgImageContainer::BfromXML(StringView xml, BgImageArray& cells) noexcept {
    try {
        const String input(xml);
        cells.clear();
        auto document = XNode::parse(input);
        if (document.name() != "MenuCardsAndBgImages") return false;
        document.onTag[String(BgImage::XML_NAME_M)] = [&cells](XNode& node) {
            node.onTag[{}] = [&cells](XNode& child) {
                cells.emplace_back(BgImage::createFromNode(child));
            };
            node.onTag["*"] = node.onTag[{}];
            node.parse();
        };
        document.parse();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

namespace {

template <typename Cells>
bool isInheritBackgroundImpl(const Cells& cells) noexcept {
    if (cells.empty()) return true;
    const auto image = std::dynamic_pointer_cast<ImageCell>(cells.front());
    return !(image && !image->path().empty() && image->flag().empty() &&
             image->x() == 0 && image->y() == 0 && image->width() == 632 &&
             image->height() == 420 && !image->mask() &&
             image->cellName().empty());
}

}  // namespace

bool isInheritBackground(const BgImageArray& cells) noexcept {
    return isInheritBackgroundImpl(cells);
}

bool isInheritBackground(
    const ::cwxeditor::lib::SharedSlice<BgImagePtr>& cells) noexcept {
    return isInheritBackgroundImpl(cells);
}

}  // namespace cwxeditor::cwx::background
