#include "cwxeditor_src/gui/cwx/editor/gui/dwt/cardpane.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaviewutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/cardpropertytransaction.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/smalldialogs.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/resourcehistory.hpp"
#include "cwxeditor_src/core/cwx/variables.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/ui_item_presentation.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/comment.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cwxeditor::cwx::editor::gui::dwt::cardpane {
namespace structs = ::cwxeditor::cwx::structs;
namespace {

[[nodiscard]] bool dRefListClearsSelection(
    const CViewMode source, const CViewMode target) noexcept {
    return (source == CViewMode::CARD && target == CViewMode::LIFE) ||
           (source == CViewMode::LIFE && target == CViewMode::CARD);
}

[[nodiscard]] SummaryPtr summaryFromPath(const CWXPathPtr& value) {
    if (!value) return {};
    if (const auto summary = std::dynamic_pointer_cast<Summary>(value)) return summary;
    if (const auto areaOwner = std::dynamic_pointer_cast<::cwxeditor::cwx::area::AreaOwner>(value))
        return std::dynamic_pointer_cast<Summary>(areaOwner);
    return {};
}

[[nodiscard]] CWXPathPtr summaryPath(const SummaryPtr& value) {
    if (!value) return {};
    const auto areaOwner = std::static_pointer_cast<::cwxeditor::cwx::area::AreaOwner>(value);
    return std::static_pointer_cast<CWXPath>(areaOwner);
}

[[nodiscard]] lib::ResourceId cardTypeImage(const Props& prop,
                                            const CardType type) noexcept {
    switch (type) {
    case CardType::Cast: return prop.castImage;
    case CardType::Skill: return prop.skillImage;
    case CardType::Item: return prop.itemImage;
    case CardType::Beast: return prop.beastImage;
    case CardType::Info: return prop.infoImage;
    }
    return 0U;
}

[[nodiscard]] bool hasModifier(const lib::Modifiers value,
                                const lib::Modifiers modifier) noexcept {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(modifier)) != 0U;
}

[[nodiscard]] String lower(StringView value) {
    String result(value);
    for (char& ch : result)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return result;
}

[[nodiscard]] std::vector<std::byte> clipboardBytes(const StringView text) {
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const unsigned char value : text)
        result.push_back(static_cast<std::byte>(value));
    return result;
}

[[nodiscard]] lib::DropOperation copyOperation(const lib::DropOperation allowed) noexcept {
    const auto bits = static_cast<std::uint32_t>(allowed);
    if ((bits & static_cast<std::uint32_t>(lib::DropOperation::Copy)) != 0U)
        return lib::DropOperation::Copy;
    if ((bits & static_cast<std::uint32_t>(lib::DropOperation::Link)) != 0U)
        return lib::DropOperation::Link;
    return lib::DropOperation::None;
}

[[nodiscard]] bool containsInsensitive(StringView text, StringView needle) {
    if (needle.empty()) return true;
    return lower(text).find(lower(needle)) != String::npos;
}

[[nodiscard]] String numberText(const std::uint64_t value) {
    return std::to_string(value);
}

[[nodiscard]] String typeName(const CardType type, const Props& prop) {
    switch (type) {
    case CardType::Cast: return prop.castName;
    case CardType::Skill: return prop.skillName;
    case CardType::Item: return prop.itemName;
    case CardType::Beast: return prop.beastName;
    case CardType::Info: return prop.infoName;
    }
    return {};
}

[[nodiscard]] lib::Color toColor(const structs::CRGB& value) noexcept {
    return {static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, value.r)),
            static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, value.g)),
            static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, value.b)),
            static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, value.a))};
}

void applyColorMask(lib::ImageData& image, const structs::CRGB& mask) noexcept {
    const auto alpha = std::min<std::uint32_t>(255U, mask.a);
    if (alpha == 0U) return;
    const auto inverse = 255U - alpha;
    const auto blend = [alpha, inverse](const std::uint8_t source,
                                        const std::uint8_t destination) {
        return static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(source) * alpha +
             static_cast<std::uint32_t>(destination) * inverse) / 255U);
    };
    const auto redMask = static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, mask.r));
    const auto greenMask = static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, mask.g));
    const auto blueMask = static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, mask.b));
    for (auto& frame : image.frames) {
        std::size_t red{}, green{}, blue{};
        std::optional<std::size_t> opacity;
        std::size_t channels{};
        switch (frame.pixel_format) {
        case lib::PixelFormat::RGBA32:
            red = 0U; green = 1U; blue = 2U; opacity = 3U; channels = 4U;
            break;
        case lib::PixelFormat::BGRA32:
            red = 2U; green = 1U; blue = 0U; opacity = 3U; channels = 4U;
            break;
        case lib::PixelFormat::RGB24:
            red = 0U; green = 1U; blue = 2U; channels = 3U;
            break;
        case lib::PixelFormat::BGR24:
            red = 2U; green = 1U; blue = 0U; channels = 3U;
            break;
        default:
            continue;
        }
        if (frame.pixel_height != 0U &&
            frame.pixel_width > std::numeric_limits<std::size_t>::max() /
                                    frame.pixel_height)
            continue;
        const auto pixels = static_cast<std::size_t>(frame.pixel_width) *
                            frame.pixel_height;
        const auto count = std::min(pixels, frame.pixels.size() / channels);
        for (std::size_t index = 0U; index < count; ++index) {
            const auto offset = index * channels;
            const auto byte = [&frame, offset](const std::size_t channel) {
                return std::to_integer<std::uint8_t>(frame.pixels[offset + channel]);
            };
            frame.pixels[offset + red] = static_cast<std::byte>(blend(redMask, byte(red)));
            frame.pixels[offset + green] = static_cast<std::byte>(blend(greenMask, byte(green)));
            frame.pixels[offset + blue] = static_cast<std::byte>(blend(blueMask, byte(blue)));
            if (opacity) {
                const auto destination = byte(*opacity);
                const auto composed = alpha +
                    static_cast<std::uint32_t>(destination) * inverse / 255U;
                frame.pixels[offset + *opacity] = static_cast<std::byte>(
                    static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, composed)));
            }
        }
    }
}

[[nodiscard]] double imageAverage(const lib::ImageData& image) noexcept {
    if (image.frames.empty()) return 255.0;
    const auto& frame = image.frames.front();
    const auto channels = frame.pixel_format == lib::PixelFormat::Gray8 ? 1U
        : frame.pixel_format == lib::PixelFormat::RGB24 ||
          frame.pixel_format == lib::PixelFormat::BGR24 ? 3U
        : frame.pixel_format == lib::PixelFormat::RGBA32 ||
          frame.pixel_format == lib::PixelFormat::BGRA32 ? 4U : 0U;
    if (channels == 0U || frame.pixels.size() < channels) return 255.0;
    std::uint64_t total{};
    std::uint64_t count{};
    for (std::size_t offset = 0; offset + channels <= frame.pixels.size();
         offset += channels) {
        const auto byte = [&](const std::size_t index) {
            return std::to_integer<std::uint8_t>(frame.pixels[offset + index]);
        };
        if (channels == 1U) total += byte(0U);
        else if (frame.pixel_format == lib::PixelFormat::BGR24 ||
                 frame.pixel_format == lib::PixelFormat::BGRA32)
            total += (static_cast<unsigned>(byte(2U)) + byte(1U) + byte(0U)) / 3U;
        else
            total += (static_cast<unsigned>(byte(0U)) + byte(1U) + byte(2U)) / 3U;
        ++count;
    }
    return count == 0U ? 255.0 : static_cast<double>(total) / count;
}

[[nodiscard]] String categoryName(const CardType type) {
    switch (type) {
    case CardType::Cast: return "castcard";
    case CardType::Skill: return "skillcard";
    case CardType::Item: return "itemcard";
    case CardType::Beast: return "beastcard";
    case CardType::Info: return "infocard";
    }
    return {};
}

[[nodiscard]] std::optional<CardType> cardTypeFromXmlName(
    const StringView name, const bool multiple) {
    if (name == (multiple ? CastCard::XML_NAME_M : CastCard::XML_NAME))
        return CardType::Cast;
    if (name == (multiple ? SkillCard::XML_NAME_M : SkillCard::XML_NAME))
        return CardType::Skill;
    if (name == (multiple ? ItemCard::XML_NAME_M : ItemCard::XML_NAME))
        return CardType::Item;
    if (name == (multiple ? BeastCard::XML_NAME_M : BeastCard::XML_NAME))
        return CardType::Beast;
    if (name == (multiple ? InfoCard::XML_NAME_M : InfoCard::XML_NAME))
        return CardType::Info;
    return std::nullopt;
}

[[nodiscard]] bool isCardXmlName(const StringView name) {
    return cardTypeFromXmlName(name, false).has_value() ||
           cardTypeFromXmlName(name, true).has_value();
}

[[nodiscard]] bool sameCardState(const CardPtr& left, const CardPtr& right) {
    if (!left || !right) return left == right;
    if (const auto value = std::dynamic_pointer_cast<CastCard>(left)) {
        const auto other = std::dynamic_pointer_cast<CastCard>(right);
        return other && value->opEquals(*other);
    }
    if (const auto value = std::dynamic_pointer_cast<SkillCard>(left)) {
        const auto other = std::dynamic_pointer_cast<SkillCard>(right);
        return other && value->opEquals(*other);
    }
    if (const auto value = std::dynamic_pointer_cast<ItemCard>(left)) {
        const auto other = std::dynamic_pointer_cast<ItemCard>(right);
        return other && value->opEquals(*other);
    }
    if (const auto value = std::dynamic_pointer_cast<BeastCard>(left)) {
        const auto other = std::dynamic_pointer_cast<BeastCard>(right);
        return other && value->opEquals(*other);
    }
    if (const auto value = std::dynamic_pointer_cast<InfoCard>(left)) {
        const auto other = std::dynamic_pointer_cast<InfoCard>(right);
        return other && value->opEquals(*other);
    }
    return false;
}

[[nodiscard]] CardPtr snapshotCardState(const CardPtr& value) {
    if (!value) return {};
    const lib::Array<lib::Owner<card::CardImage>> images;
    CardPtr snapshot;
    if (std::dynamic_pointer_cast<CastCard>(value))
        snapshot = std::make_shared<CastCard>(value->id(), String{}, images, String{});
    else if (std::dynamic_pointer_cast<SkillCard>(value))
        snapshot = std::make_shared<SkillCard>(nullptr, value->id(), String{}, images, String{});
    else if (std::dynamic_pointer_cast<ItemCard>(value))
        snapshot = std::make_shared<ItemCard>(nullptr, value->id(), String{}, images, String{});
    else if (std::dynamic_pointer_cast<BeastCard>(value))
        snapshot = std::make_shared<BeastCard>(nullptr, value->id(), String{}, images, String{});
    else if (std::dynamic_pointer_cast<InfoCard>(value))
        snapshot = std::make_shared<InfoCard>(value->id(), String{}, images, String{});
    if (!snapshot || !cardpropertytransaction::restoreProperties(snapshot, value)) return {};
    // 浅いプロパティの保存状態は、使用中カードの下位カウン
    // ターの参照名変更に追従する。イベント、ローカル宣言
    // 、所持品は含めず、架空の有効な使用登録を作らない。
    struct SnapshotOwner {
        CardPtr card;
        ~SnapshotOwner() { if (card) card->removeUseCounter(); }
    };
    auto owner = std::make_shared<SnapshotOwner>();
    owner->card = snapshot;
    if (const auto counter = value->useCounter())
        snapshot->setUseCounter(counter->sub(), value);
    return CardPtr(owner, snapshot.get());
}

bool restoreCardState(const CardPtr& target, const CardPtr& source) {
    if (!target || !source) return false;
    if (const auto value = std::dynamic_pointer_cast<CastCard>(target)) {
        const auto snapshot = std::dynamic_pointer_cast<CastCard>(source);
        if (!snapshot) return false;
        value->shallowCopy(*snapshot);
        return true;
    }
    if (const auto value = std::dynamic_pointer_cast<SkillCard>(target)) {
        const auto snapshot = std::dynamic_pointer_cast<SkillCard>(source);
        if (!snapshot) return false;
        value->shallowCopy(*snapshot);
        return true;
    }
    if (const auto value = std::dynamic_pointer_cast<ItemCard>(target)) {
        const auto snapshot = std::dynamic_pointer_cast<ItemCard>(source);
        if (!snapshot) return false;
        value->shallowCopy(*snapshot);
        return true;
    }
    if (const auto value = std::dynamic_pointer_cast<BeastCard>(target)) {
        const auto snapshot = std::dynamic_pointer_cast<BeastCard>(source);
        if (!snapshot) return false;
        value->shallowCopy(*snapshot);
        return true;
    }
    if (const auto value = std::dynamic_pointer_cast<InfoCard>(target)) {
        const auto snapshot = std::dynamic_pointer_cast<InfoCard>(source);
        if (!snapshot) return false;
        value->shallowCopy(*snapshot);
        return true;
    }
    return false;
}

[[nodiscard]] std::uint64_t valueOr(const std::optional<std::string>& value,
                                     const std::uint64_t fallback = 0U) {
    if (!value || value->empty()) return fallback;
    std::uint64_t result{};
    const auto* first = value->data();
    const auto* last = first + value->size();
    const auto parsed = std::from_chars(first, last, result);
    return parsed.ec == std::errc{} && parsed.ptr == last ? result : fallback;
}

[[nodiscard]] bool boolOr(const std::optional<std::string>& value,
                          const bool fallback = false) {
    if (!value) return fallback;
    const auto text = lower(*value);
    if (text == "1" || text == "true" || text == "yes") return true;
    if (text == "0" || text == "false" || text == "no") return false;
    return fallback;
}

template <typename Values>
[[nodiscard]] std::vector<CardPtr> eraseNulls(const Values& values) {
    // 表は独立した行一覧を使う。その中で各カードの所有元を保持する。
    std::vector<CardPtr> result;
    result.reserve(values.size());
    for (const auto& value : values)
        if (value) result.push_back(value);
    return result;
}

template <typename Values>
[[nodiscard]] std::ptrdiff_t ptrIndex(const Values& values,
                                      const CardPtr& target) {
    for (std::size_t index = 0U; index < values.size(); ++index)
        if (values[index].get() == target.get())
            return static_cast<std::ptrdiff_t>(index);
    return -1;
}

[[nodiscard]] String formatCount(String format, const std::size_t count) {
    const auto marker = format.find("%zu");
    if (marker != String::npos) format.replace(marker, 3U, std::to_string(count));
    return format;
}

[[nodiscard]] String formatSelected(String format, const std::size_t count,
                                    const std::size_t selected) {
    const auto first = format.find("%zu");
    if (first != String::npos) {
        format.replace(first, 3U, std::to_string(count));
        const auto second = format.find("%zu", first + 1U);
        if (second != String::npos)
            format.replace(second, 3U, std::to_string(selected));
    }
    return format;
}

[[nodiscard]] String formatSelectedId(String format, const std::size_t count,
                                      const std::uint64_t id) {
    const auto first = format.find("%zu");
    if (first != String::npos) {
        format.replace(first, 3U, std::to_string(count));
        const auto marker = format.find("%llu", first + 1U);
        if (marker != String::npos)
            format.replace(marker, 5U, std::to_string(id));
    }
    return format;
}

[[nodiscard]] String cardNumberText(const CardType type, const std::uint32_t number,
                                    const Props& prop) {
    if ((type == CardType::Item || type == CardType::Beast) && number == 0U)
        return prop.infinity;
    return std::to_string(number);
}

}  // namespace

bool CardDialog::openCWXPath(const StringView value, const bool shellActivate) {
    (void)shellActivate;
    if (value.empty()) return false;
    opened_path_ = String(value);
    open_ = true;
    active_ = true;
    return true;
}

bool CardDialog::open() {
    open_ = true;
    active_ = true;
    return true;
}

void CardDialog::active() {
    if (open_) active_ = true;
}

void CardDialog::forceCancel() {
    const bool wasOpen = open_;
    open_ = false;
    active_ = false;
    applying = {};
    if (wasOpen && closed) closed();
}

CardPane::CardPane(Commons comm, Props prop, SummaryPtr summary, Parent parent,
                   const OwnerType ownerType, const CardType cardType,
                   const customtable::Style style, SummaryPtr toc,
                   std::function<void(const CastCardPtr&)> openHand)
    : comm_(std::move(comm)), prop_(std::move(prop)), summary_(std::move(summary)),
      toc_(std::move(toc)), parent_(std::move(parent)), owner_type_(ownerType),
      card_type_(cardType), style_(style), read_only_(prop_.readOnly || !editMode()),
      open_hand_(std::move(openHand)) {
    // Dの両コンストラクターは、カード種別の選択後にconstruct1を実行す
    // る。製品のCardWindowラッパーもこの標準ペインを直接構築するため、
    // 実際の構築境界で同じ代替を共有する。C++の意図的な拡張として、Dの無条
    // 件の種別既定値ではなく、明示されたcardImageの上書きを維持する。
    if (!prop_.cardImage) prop_.cardImage = cardTypeImage(prop_, card_type_);
    // DはobjectIDValue(this)を使う。ペインの同一性で、Summary
    // 一覧と同じカード種別の各Castの手札を区別する。カテゴリだけのIDでは、ペイン
    // 間のAddHandやドラッグ・ドロップが内部移動と誤認され、リンクが抑止される。
    id_ = categoryName(card_type_) + ":" +
          std::to_string(reinterpret_cast<std::uintptr_t>(this));
}

CardPane::~CardPane() {
    renumber_lifetime_.reset();
    if (table_) table_->set_tooltip({});
    if (comm_.columnWidthEvent && shared_resize_token_)
        comm_.columnWidthEvent->remove(shared_resize_token_);
    if (comm_.appearanceEvent && appearance_token_)
        comm_.appearanceEvent->remove(appearance_token_);
    if (comm_.cardChangedEvent && card_changed_token_)
        comm_.cardChangedEvent->remove(card_changed_token_);
    if (comm_.refSortParamsEvent && sort_params_token_)
        comm_.refSortParamsEvent->remove(sort_params_token_);
    inc_search_.reset();
    closePreview();
    card_preview_.reset();
    if (renumber_dialog_) renumber_dialog_->forceCancel();
    renumber_dialog_.reset();
    if (comm_.services.drag_drop) {
        if (table_) comm_.services.drag_drop->unbind(table_);
        if (image_list_) comm_.services.drag_drop->unbind(image_list_);
        if (pane_)
            comm_.services.drag_drop->unbind(std::static_pointer_cast<lib::Widget>(pane_));
    }
    // ダイアログのコールバックはこのペインをキャプチャする。Dは一覧の破棄時に
    // 有効な全カードダイアログを強制的に取り消す。先にコールバックを切り離し
    // 、モードレスの終了が破棄途中のCardPaneへ再入しないようにする。
    auto dialogs = std::move(edit_dialogs_);
    edit_dialogs_.clear();
    for (const auto& [_, dialog] : dialogs) {
        if (!dialog) continue;
        dialog->applying = {};
        dialog->applied = {};
        dialog->closed = {};
        dialog->forceCancel();
    }
    auto comment_dialogs = std::move(comment_dialogs_);
    comment_dialogs_.clear();
    for (const auto& [_, dialog] : comment_dialogs) {
        if (!dialog) continue;
        dialog->applied = {};
        dialog->closed = {};
        dialog->forceCancel();
    }
    if (selection_token_ != 0U)
        comm_.refCardEvent.remove(selection_token_);
    if (edit_token_ != 0U)
        comm_.delCardEvent.remove(edit_token_);
    if (resize_token_ != 0U)
        comm_.refCardTableColumnWidth.remove(resize_token_);
}

void CardPane::construct1(Commons comm, Props prop, SummaryPtr summary,
                          const OwnerType ownerType, const CardType cardType,
                          const customtable::Style style) {
    comm_ = std::move(comm);
    prop_ = std::move(prop);
    summary_ = std::move(summary);
    owner_type_ = ownerType;
    card_type_ = cardType;
    // C++の意図的な拡張。Dの無条件の種別既定値ではなく、明示されたcardImageの上書きを維持する。
    if (!prop_.cardImage) prop_.cardImage = cardTypeImage(prop_, card_type_);
    style_ = style;
    read_only_ = prop_.readOnly || !editMode();
    id_ = categoryName(card_type_);
}

int CardPane::colIndex(const CardTableColumn column) const noexcept {
    switch (column) {
    case CardTableColumn::ID: return 0;
    case CardTableColumn::Name: return 1;
    case CardTableColumn::Desc: return useNum() ? 3 : 2;
    case CardTableColumn::UC:
        return owner_type_ == OwnerType::Summary ? colIndex(CardTableColumn::Desc) + 1 : -1;
    case CardTableColumn::Num: return useNum() ? 2 : -1;
    }
    return -1;
}

bool CardPane::ownerIsSummary() const noexcept {
    return owner_type_ == OwnerType::Summary;
}

std::vector<CardPtr> CardPane::cardsFrom(const CardType type,
                                         const CWXPathPtr& owner) {
    if (!owner) return {};
    switch (type) {
    case CardType::Cast:
        if (const auto source = std::dynamic_pointer_cast<card::CastOwner>(owner))
            return eraseNulls(source->casts());
        break;
    case CardType::Skill:
        if (const auto source = std::dynamic_pointer_cast<card::SkillOwner>(owner))
            return eraseNulls(source->skills());
        break;
    case CardType::Item:
        if (const auto source = std::dynamic_pointer_cast<card::ItemOwner>(owner))
            return eraseNulls(source->items());
        break;
    case CardType::Beast:
        if (const auto source = std::dynamic_pointer_cast<card::BeastOwner>(owner))
            return eraseNulls(source->beasts());
        break;
    case CardType::Info:
        if (const auto source = std::dynamic_pointer_cast<card::InfoOwner>(owner))
            return eraseNulls(source->infos());
        break;
    }
    return {};
}

CardPtr CardPane::cardFrom(const CWXPathPtr& owner, const CardType type,
                           const std::uint64_t id) {
    if (!owner) return {};
    switch (type) {
    case CardType::Cast:
        if (const auto source = std::dynamic_pointer_cast<card::CastOwner>(owner))
            return source->cwCast(id);
        break;
    case CardType::Skill:
        if (const auto source = std::dynamic_pointer_cast<card::SkillOwner>(owner))
            return source->skill(id);
        break;
    case CardType::Item:
        if (const auto source = std::dynamic_pointer_cast<card::ItemOwner>(owner))
            return source->item(id);
        break;
    case CardType::Beast:
        if (const auto source = std::dynamic_pointer_cast<card::BeastOwner>(owner))
            return source->beast(id);
        break;
    case CardType::Info:
        if (const auto source = std::dynamic_pointer_cast<card::InfoOwner>(owner))
            return source->info(id);
        break;
    }
    return {};
}

std::vector<CardPtr> CardPane::cards() const {
    return cardsFrom(card_type_, owner_);
}

std::vector<CardPtr> CardPane::cardsNarrow() const {
    const auto source = cards();
    if (search_text_.empty()) return source;
    std::vector<CardPtr> result;
    for (const auto& value : source)
        if (inc_search_ && inc_search_->isOpen()
                ? inc_search_->match(cardName(value))
                : containsInsensitive(cardName(value), search_text_))
            result.push_back(value);
    return result;
}

std::size_t CardPane::narrowCount() const noexcept {
    return search_text_.empty() ? cards_.size() : cardsNarrow().size();
}

CardPtr CardPane::card(const std::uint64_t id) const {
    return cardFrom(owner_, card_type_, id);
}

CardPtr CardPane::localCard(const std::uint64_t id) const {
    return owner_ ? card(id) : CardPtr{};
}

std::uint64_t CardPane::linkId(const CardPtr& value) noexcept {
    const auto effect = std::dynamic_pointer_cast<EffectCard>(value);
    return effect ? effect->linkId() : 0U;
}

CardPtr CardPane::pOwnerCard(const std::uint64_t id) const {
    if (!summary_) return {};
    switch (card_type_) {
    case CardType::Cast: return summary_->cwCast(id);
    case CardType::Skill: return summary_->skill(id);
    case CardType::Item: return summary_->item(id);
    case CardType::Beast: return summary_->beast(id);
    case CardType::Info: return summary_->info(id);
    }
    return {};
}

std::vector<CardPtr> CardPane::pOwnerCards() const {
    if (!summary_) return {};
    switch (card_type_) {
    case CardType::Cast: return eraseNulls(summary_->casts());
    case CardType::Skill: return eraseNulls(summary_->skills());
    case CardType::Item: return eraseNulls(summary_->items());
    case CardType::Beast: return eraseNulls(summary_->beasts());
    case CardType::Info: return eraseNulls(summary_->infos());
    }
    return {};
}

CardPtr CardPane::baseCard(const CardPtr& value) const {
    if (!value) return {};
    const auto id = linkId(value);
    if (id == 0U) return value;
    if (const auto base = pOwnerCard(id)) return base;
    return value;
}

String CardPane::cardName(const CardPtr& value) const {
    const auto base = baseCard(value);
    return base ? base->name() : String{};
}

String CardPane::cardDesc(const CardPtr& value) const {
    const auto base = baseCard(value);
    return base ? base->desc() : String{};
}

std::uint32_t CardPane::cardNum(const CardPtr& value) const {
    const auto base = baseCard(value);
    if (!base) return 0U;
    switch (card_type_) {
    case CardType::Cast: {
        const auto cast = std::dynamic_pointer_cast<CastCard>(base);
        return cast ? cast->level() : 0U;
    }
    case CardType::Skill: {
        const auto skill = std::dynamic_pointer_cast<SkillCard>(base);
        return skill ? skill->level() : 0U;
    }
    case CardType::Item: {
        const auto item = std::dynamic_pointer_cast<ItemCard>(base);
        return item ? item->useLimit() : 0U;
    }
    case CardType::Beast: {
        const auto beast = std::dynamic_pointer_cast<BeastCard>(base);
        return beast ? beast->useLimit() : 0U;
    }
    case CardType::Info: return 0U;
    }
    return 0U;
}

std::vector<CardType> CardPane::types() const {
    if (owner_type_ == OwnerType::Summary)
        return {CardType::Cast, CardType::Skill, CardType::Item,
                CardType::Beast, CardType::Info};
    return {CardType::Skill, CardType::Item, CardType::Beast};
}

String CardPane::xmlNameFrom(const CardType type) {
    switch (type) {
    case CardType::Cast: return String(CastCard::XML_NAME);
    case CardType::Skill: return String(SkillCard::XML_NAME);
    case CardType::Item: return String(ItemCard::XML_NAME);
    case CardType::Beast: return String(BeastCard::XML_NAME);
    case CardType::Info: return String(InfoCard::XML_NAME);
    }
    return {};
}

String CardPane::xmlName() const { return xmlNameFrom(card_type_); }

String CardPane::xmlNameM() const {
    switch (card_type_) {
    case CardType::Cast: return String(CastCard::XML_NAME_M);
    case CardType::Skill: return String(SkillCard::XML_NAME_M);
    case CardType::Item: return String(ItemCard::XML_NAME_M);
    case CardType::Beast: return String(BeastCard::XML_NAME_M);
    case CardType::Info: return String(InfoCard::XML_NAME_M);
    }
    return {};
}

CardType CardPane::cardTypeFrom(const CardPtr& value) {
    if (std::dynamic_pointer_cast<CastCard>(value)) return CardType::Cast;
    if (std::dynamic_pointer_cast<SkillCard>(value)) return CardType::Skill;
    if (std::dynamic_pointer_cast<ItemCard>(value)) return CardType::Item;
    if (std::dynamic_pointer_cast<BeastCard>(value)) return CardType::Beast;
    return CardType::Info;
}

CardPtr CardPane::makeCard(const CardType type, const std::uint64_t id,
                           String name, String desc) const {
    const lib::Array<CardImagePtr> images;
    // Dはローカル接頭辞も含め、EditorのSystemでEffectCardを作る。
    const auto* sys = prop_.coreProps ? &prop_.coreProps->sys()
        : (prop_.editorProps ? &prop_.editorProps->sys() : nullptr);
    switch (type) {
    case CardType::Cast: return std::make_shared<CastCard>(id, std::move(name), images, std::move(desc));
    case CardType::Skill: return std::make_shared<SkillCard>(sys, id, std::move(name), images, std::move(desc));
    case CardType::Item: return std::make_shared<ItemCard>(sys, id, std::move(name), images, std::move(desc));
    case CardType::Beast: return std::make_shared<BeastCard>(sys, id, std::move(name), images, std::move(desc));
    case CardType::Info: return std::make_shared<InfoCard>(id, std::move(name), images, std::move(desc));
    }
    return {};
}

CardPtr CardPane::createFromNode(XNode& node, const XMLInfo& version) const {
    const auto name = node.name();
    if (name == CastCard::XML_NAME) {
        if (prop_.coreProps) {
            try {
                return CastCard::createFromNode(*prop_.coreProps, node, version);
            } catch (...) {
                // 旧形式・最小限のクリップボードXMLは下記の共通の代替処理
                // で受け付けるが、製品のコピーでは完全な読み込み処理を使う。
            }
        }
        const auto id = valueOr(node.attribute("id"), card::Card::readId(node));
        String cardName = node.attribute("name").value_or(String{});
        String cardDesc = node.attribute("desc").value_or(String{});
        if (const auto property = node.child("Property", false); property.valid()) {
            if (cardName.empty()) cardName = property.childText("Name", false, String{});
            if (cardDesc.empty()) cardDesc = property.childText("Description", false, String{});
        }
        return makeCard(CardType::Cast, id, std::move(cardName), std::move(cardDesc));
    }
    CardType type = card_type_;
    if (name == SkillCard::XML_NAME) type = CardType::Skill;
    else if (name == ItemCard::XML_NAME) type = CardType::Item;
    else if (name == BeastCard::XML_NAME) type = CardType::Beast;
    else if (name == InfoCard::XML_NAME) type = CardType::Info;
    else return {};
    try {
        switch (type) {
        case CardType::Skill: return SkillCard::createFromNode(node, version);
        case CardType::Item: return ItemCard::createFromNode(node, version);
        case CardType::Beast: return BeastCard::createFromNode(node, version);
        case CardType::Info: return InfoCard::createFromNode(node, version);
        case CardType::Cast: break;
        }
    } catch (...) {
        // 旧クリップボードデータ向けに、下記の軽量な代替処理を使えるようにする。
    }
    const auto id = valueOr(node.attribute("id"), card::Card::readId(node));
    String cardName = node.attribute("name").value_or(String{});
    String cardDesc = node.attribute("desc").value_or(String{});
    if (const auto property = node.child("Property", false); property.valid()) {
        if (cardName.empty()) cardName = property.childText("Name", false, String{});
        if (cardDesc.empty()) cardDesc = property.childText("Description", false, String{});
    }
    auto value = makeCard(type, id, std::move(cardName), std::move(cardDesc));
    (void)version;
    return value;
}

void CardPane::change(const lib::Owner<::cwxeditor::cwx::usecounter::UseCounter>& counter,
                      const CardType type, const std::uint64_t from,
                      const std::uint64_t to) {
    if (!counter || from == to) return;
    switch (type) {
    case CardType::Cast: counter->change(::cwxeditor::cwx::usecounter::toCastId(from), ::cwxeditor::cwx::usecounter::toCastId(to)); break;
    case CardType::Skill: counter->change(::cwxeditor::cwx::usecounter::toSkillId(from), ::cwxeditor::cwx::usecounter::toSkillId(to)); break;
    case CardType::Item: counter->change(::cwxeditor::cwx::usecounter::toItemId(from), ::cwxeditor::cwx::usecounter::toItemId(to)); break;
    case CardType::Beast: counter->change(::cwxeditor::cwx::usecounter::toBeastId(from), ::cwxeditor::cwx::usecounter::toBeastId(to)); break;
    case CardType::Info: counter->change(::cwxeditor::cwx::usecounter::toInfoId(from), ::cwxeditor::cwx::usecounter::toInfoId(to)); break;
    }
}

int CardPane::ucGet(const lib::Owner<::cwxeditor::cwx::usecounter::UseCounter>& counter,
                    const CardType type, const std::uint64_t id) {
    if (!counter) return 0;
    switch (type) {
    case CardType::Cast: return static_cast<int>(counter->get(::cwxeditor::cwx::usecounter::toCastId(id)));
    case CardType::Skill: return static_cast<int>(counter->get(::cwxeditor::cwx::usecounter::toSkillId(id)));
    case CardType::Item: return static_cast<int>(counter->get(::cwxeditor::cwx::usecounter::toItemId(id)));
    case CardType::Beast: return static_cast<int>(counter->get(::cwxeditor::cwx::usecounter::toBeastId(id)));
    case CardType::Info: return static_cast<int>(counter->get(::cwxeditor::cwx::usecounter::toInfoId(id)));
    }
    return 0;
}

std::ptrdiff_t CardPane::indexOf(const CWXPathPtr& owner, const CardType type,
                                 const CardPtr& value) {
    if (!owner || !value) return -1;
    return [&] {
        const auto values = cardsFrom(type, owner);
        for (std::size_t index = 0U; index < values.size(); ++index)
            if (values[index].get() == value.get()) return static_cast<std::ptrdiff_t>(index);
        return std::ptrdiff_t{-1};
    }();
}

CardPtr CardPane::insert(const CWXPathPtr& owner, std::ptrdiff_t index,
                      const CardPtr& value, const bool preserveIdentity) {
    if (!owner || !value) return {};
    const auto safeIndex = std::max<std::ptrdiff_t>(0, index);
    if (const auto summary = summaryFromPath(owner)) {
        switch (cardTypeFrom(value)) {
        case CardType::Cast: static_cast<void>(summary->insert(safeIndex, std::dynamic_pointer_cast<CastCard>(value))); break;
        case CardType::Skill: static_cast<void>(summary->insert(safeIndex, std::dynamic_pointer_cast<SkillCard>(value))); break;
        case CardType::Item: static_cast<void>(summary->insert(safeIndex, std::dynamic_pointer_cast<ItemCard>(value))); break;
        case CardType::Beast: static_cast<void>(summary->insert(safeIndex, std::dynamic_pointer_cast<BeastCard>(value))); break;
        case CardType::Info: static_cast<void>(summary->insert(safeIndex, std::dynamic_pointer_cast<InfoCard>(value))); break;
        }
        return value;
    } else if (const auto cast = std::dynamic_pointer_cast<CastCard>(owner)) {
        switch (cardTypeFrom(value)) {
        case CardType::Skill: return cast->insert(static_cast<std::size_t>(safeIndex), std::dynamic_pointer_cast<SkillCard>(value), preserveIdentity);
        case CardType::Item: return cast->insert(static_cast<std::size_t>(safeIndex), std::dynamic_pointer_cast<ItemCard>(value), preserveIdentity);
        case CardType::Beast: return cast->insert(static_cast<std::size_t>(safeIndex), std::dynamic_pointer_cast<BeastCard>(value), preserveIdentity);
        case CardType::Cast:
        case CardType::Info: break;
        }
    }
    return {};
}

void CardPane::remove(const CWXPathPtr& owner, const CardPtr& value) {
    if (!owner || !value) return;
    if (const auto summary = summaryFromPath(owner)) {
        switch (cardTypeFrom(value)) {
        case CardType::Cast: summary->remove(std::dynamic_pointer_cast<CastCard>(value)); break;
        case CardType::Skill: summary->remove(std::dynamic_pointer_cast<SkillCard>(value)); break;
        case CardType::Item: summary->remove(std::dynamic_pointer_cast<ItemCard>(value)); break;
        case CardType::Beast: summary->remove(std::dynamic_pointer_cast<BeastCard>(value)); break;
        case CardType::Info: summary->remove(std::dynamic_pointer_cast<InfoCard>(value)); break;
        }
    } else if (const auto cast = std::dynamic_pointer_cast<CastCard>(owner)) {
        switch (cardTypeFrom(value)) {
        case CardType::Skill: cast->remove(std::dynamic_pointer_cast<SkillCard>(value)); break;
        case CardType::Item: cast->remove(std::dynamic_pointer_cast<ItemCard>(value)); break;
        case CardType::Beast: cast->remove(std::dynamic_pointer_cast<BeastCard>(value)); break;
        case CardType::Cast:
        case CardType::Info: break;
        }
    }
}

std::uint64_t CardPane::add(const SummaryPtr& owner, const CardPtr& value) {
    if (!owner || !value) return 0U;
    switch (cardTypeFrom(value)) {
    case CardType::Cast: return owner->add(std::dynamic_pointer_cast<CastCard>(value));
    case CardType::Skill: return owner->add(std::dynamic_pointer_cast<SkillCard>(value));
    case CardType::Item: return owner->add(std::dynamic_pointer_cast<ItemCard>(value));
    case CardType::Beast: return owner->add(std::dynamic_pointer_cast<BeastCard>(value));
    case CardType::Info: return owner->add(std::dynamic_pointer_cast<InfoCard>(value));
    }
    return 0U;
}

CardPtr CardPane::add(const CastCardPtr& owner, const CardPtr& value) {
    if (!owner || !value) return {};
    switch (cardTypeFrom(value)) {
    case CardType::Skill: return owner->add(std::dynamic_pointer_cast<SkillCard>(value), true);
    case CardType::Item: return owner->add(std::dynamic_pointer_cast<ItemCard>(value), true);
    case CardType::Beast: return owner->add(std::dynamic_pointer_cast<BeastCard>(value), true);
    case CardType::Cast:
    case CardType::Info: break;
    }
    return {};
}

CardPanePtr CardPane::openSameLevelPane(const CardType type, const bool shellActivate) {
    if (type == card_type_) {
        try {
            return shared_from_this();
        } catch (const std::bad_weak_ptr&) {
            return {};
        }
    }
    if (owner_type_ == OwnerType::Cast) {
        const auto cast = std::dynamic_pointer_cast<CastCard>(owner_);
        const auto open = comm_.openHandsPane;
        const auto sourceSummary = summary_;
        const auto sourceOwner = owner_;
        const auto generation = owner_binding_generation_;
        const std::weak_ptr<void> lifetime = renumber_lifetime_;
        if (!cast || !open) return {};
        const auto target = open(sourceSummary, cast, type, shellActivate);
        if (lifetime.expired()) return {};
        if (owner_binding_generation_ != generation ||
            summary_ != sourceSummary || owner_ != sourceOwner || !target ||
            target->summary_ != sourceSummary || target->owner_ != sourceOwner ||
            target->owner_type_ != OwnerType::Cast || target->card_type_ != type ||
            target->read_only_ != read_only_ || target->toc_ != toc_) return {};
        return target;
    }
    return comm_.openPane ? comm_.openPane(type, shellActivate) : CardPanePtr{};
}

PanelPtr CardPane::construct(const Parent& parent) {
    if (pane_) {
        if (parent && std::find(parent->children().begin(), parent->children().end(),
                                std::static_pointer_cast<lib::Widget>(pane_)) ==
                           parent->children().end())
            parent->add(pane_);
        return pane_;
    }
    parent_ = parent ? parent : parent_;
    constructCardList(parent_);
    return pane_;
}

void CardPane::reconstruct(const Parent& parent, const customtable::Style style) {
    parent_ = parent ? parent : parent_;
    style_ = style;
    if (!pane_) construct(parent_);
    else refreshImpl();
}

void CardPane::reconstruct(const Parent& parent) {
    if (parent) parent_ = parent;
    if (!pane_) construct(parent_);
    else refreshImpl();
}

void CardPane::syncCardListScale() {
    if (!image_list_) return;
    const auto scale = prop_.coreVars
        ? static_cast<float>(::cwxeditor::cwx::variables::previewScalePercent(
              prop_.coreVars->imageScalePercent.value,
              prop_.coreVars->imageScale.value)) / 100.0F
        : 1.0F;
    if (card_list_image_scale_ == scale) return;
    card_list_image_scale_ = scale;
    // CardListのセルは、MainWindowが返す画像に合わせて表示画
    // 素を使う。190×260の画像を95×130のセルに置くと切れてしまう。
    image_list_->setCardSize(
        static_cast<int>(std::lround(
            (card_type_ == CardType::Cast ? prop_.castCardWidth : prop_.cardWidth) * scale)),
        static_cast<int>(std::lround(
            (card_type_ == CardType::Cast ? prop_.castCardHeight : prop_.cardHeight) * scale)),
        prop_.showCardListTitle);
}

void CardPane::constructCardList(const Parent& parent) {
    parent_ = parent;
    if (!owner_ && owner_type_ == OwnerType::Summary) owner_ = summaryPath(summary_);
    pane_ = std::make_shared<lib::Panel>();
    // cardpane.dのupdateLayoutは、両画像一覧モードでCardList
    // の上にヘッダーだけのTableを置く。FillLayoutではC++の二つ
    // のコントロールが重なり、表かカードの一方しか見えないため、別々に配置する。
    pane_->set_layout(std::make_shared<lib::LinearLayout>(lib::Orientation::Vertical));
    if (parent_) parent_->add(pane_);

    const auto tableStyle = style_ | customtable::STYLE_MULTI_SELECT |
                            customtable::STYLE_HEADER |
                            (read_only_ ? customtable::STYLE_READ_ONLY
                                        : customtable::STYLE_NONE);
    table_ = customtable::rangeSelectableTable(pane_.get(), tableStyle,
                                               comm_.services);
    table_->deferUnselectedWhitespaceDownSelection(
        comm_.services.gui &&
        comm_.services.gui->platform() == lib::Platform::Windows);
    if (lib::editor_window_host_options().all_mode)
        table_->vertical_scrollbar_extra_width = 2.0F;
    // カード名と説明列は、カード画像内の文字ではなく通常のDWT
    // 一覧文字。Editor標準の読みやすいポイントサイズを使う
    // 。暗黙の小さい書体は、特に日本語の字形が細くなってしまう。
    auto cardListFont = lib::effective_editor_widget_font();
    cardListFont.weight = 600U;
    table_->set_font(std::move(cardListFont));
    // ALLの右上カード領域は上側余白を論理1画素減らす。
    // 単体版Editorの完全なヘッダー寸法は変更しない。
    const float header_height_adjustment =
        lib::editor_window_host_options().all_mode ? 1.0F : 0.0F;
    table_->setHeaderHeight(prop_.headerHeight - header_height_adjustment);
    table_->setRowHeight(prop_.rowHeight);
    const bool import_view = !editMode();
    const bool hand_view = owner_type_ != OwnerType::Summary;
    float id_width = import_view
        ? (hand_view ? prop_.importHandCardIdColumn : prop_.importCardIdColumn)
        : (hand_view ? prop_.handCardIdColumn : prop_.cardIdColumn);
    float name_width = import_view
        ? (hand_view ? prop_.importHandCardNameColumn : prop_.importCardNameColumn)
        : (hand_view ? prop_.handCardNameColumn : prop_.cardNameColumn);
    float number_width = import_view
        ? (hand_view ? prop_.importHandCardNumberColumn : prop_.importCardNumberColumn)
        : (hand_view ? prop_.handCardNumberColumn : prop_.cardNumberColumn);
    float description_width = import_view
        ? (hand_view ? prop_.importHandCardDescriptionColumn
                     : prop_.importCardDescriptionColumn)
        : (hand_view ? prop_.handCardDescriptionColumn : prop_.cardDescriptionColumn);
    float count_width = import_view ? prop_.importCardCountColumn : prop_.cardCountColumn;
    if (!lib::editor_window_host_options().all_mode && prop_.coreVars) {
        const auto& vars = *prop_.coreVars;
        id_width = static_cast<float>(import_view
            ? (hand_view ? vars.importHandCardIdColumn.value : vars.importCardIdColumn.value)
            : (hand_view ? vars.handCardIdColumn.value : vars.cardIdColumn.value));
        name_width = static_cast<float>(import_view
            ? (hand_view ? vars.importHandCardNameColumn.value : vars.importCardNameColumn.value)
            : (hand_view ? vars.handCardNameColumn.value : vars.cardNameColumn.value));
        number_width = static_cast<float>(import_view
            ? (hand_view ? vars.importHandCardNumberColumn.value : vars.importCardNumberColumn.value)
            : (hand_view ? vars.handCardNumberColumn.value : vars.cardNumberColumn.value));
        description_width = static_cast<float>(import_view
            ? (hand_view ? vars.importHandCardDescriptionColumn.value : vars.importCardDescriptionColumn.value)
            : (hand_view ? vars.handCardDescriptionColumn.value : vars.cardDescriptionColumn.value));
        count_width = static_cast<float>(import_view ? vars.importCardCountColumn.value : vars.cardCountColumn.value);
    }
    if (lib::editor_window_host_options().all_mode && !import_view && !hand_view) {
        // ALL仕様(右上のカード一覧): ID 70 / 名称 130 / レベル 80 /
        // 説明 290 / 利用 70。レベル列の無い一覧(情報カード)はその 80px を
        // 利用に加える。
        id_width = 70.0F;
        name_width = 130.0F;
        number_width = 80.0F;
        description_width = 290.0F;
        count_width = useNum() ? 70.0F : 150.0F;
    }
    if (lib::editor_window_host_options().all_mode) {
        // ALL仕様(右上・外部シナリオ・手札の全カード一覧): ID 列を 40px、
        // レベル列を 25px 狭くする。
        id_width = std::max(1.0F, id_width - 40.0F);
        number_width = std::max(1.0F, number_width - 25.0F);
    }

    columns_.clear();
    columns_[CardTableColumn::ID] = table_->addColumn(prop_.cardId, id_width);
    columns_[CardTableColumn::Name] = table_->addColumn(prop_.cardName, name_width);
    if (useNum()) {
        columns_[CardTableColumn::Num] = table_->addColumn(
            card_type_ == CardType::Cast || card_type_ == CardType::Skill
                ? prop_.level : prop_.useCount, number_width);
    }
    columns_[CardTableColumn::Desc] = table_->addColumn(prop_.cardDesc, description_width);
    if (owner_type_ == OwnerType::Summary)
        columns_[CardTableColumn::UC] = table_->addColumn(prop_.cardCount, count_width);

    for (const auto& [column, handle] : columns_)
        if (handle) handle->sortable(true);
    // DのCardWindow::construct2は、表示モードに関係なく
    // 列同期を接続する。ローカルな複数タブはウィンドウごとの通知経路を使う。
    const std::weak_ptr<int> life = renumber_lifetime_;
    table_->on_column_resized = [this, life](std::size_t index, float width) {
        if (life.expired()) return;
        for (const auto& [column, handle] : columns_) {
            if (handle && handle->index() == index) {
                columnResized(column, width, true);
                return;
            }
        }
    };
    if (comm_.columnWidthEvent) {
        shared_resize_token_ = comm_.columnWidthEvent->add(
            [this, life](const void* sender, CardTableColumn column, float width) {
                if (life.expired() || sender == this) return;
                columnResized(column, width, false);
            });
    }
    full_column_ = std::make_unique<customtable::FullTableColumn>(table_);
    if (full_column_->column() && !columns_.contains(CardTableColumn::Desc))
        columns_[CardTableColumn::Desc] = full_column_->column();

    // DはF2を、編集不可のID列ではなく名前の編集へ割り当てる。
    table_->inline_edit_column = static_cast<std::size_t>(colIndex(CardTableColumn::Name));
    table_->addSelectionListener(
        [this](const std::vector<std::size_t>& indices) { setSelectionFromTable(indices); });
    table_->addEditListener([this](const std::size_t row, const std::size_t column,
                                   String value) {
        if (column == static_cast<std::size_t>(colIndex(CardTableColumn::Name)))
            nameEditEnd(row, std::move(value));
        else if (column == static_cast<std::size_t>(colIndex(CardTableColumn::Desc))) {
            if (row < cards_.size() && !read_only_) {
                const auto valueCard = cards_[row];
                if (valueCard) {
                    const auto old = valueCard->desc();
                    pushUndo({[valueCard, old] { valueCard->desc(old); },
                              [valueCard, value] { valueCard->desc(value); },
                              "description"});
                    valueCard->desc(value);
                    refCard(valueCard);
                }
            }
        } else if (column == static_cast<std::size_t>(colIndex(CardTableColumn::Num))) {
            std::uint32_t number{};
            const auto* begin = value.data();
            const auto* end = begin + value.size();
            (void)std::from_chars(begin, end, number);
            numEditEnd(row, number);
        }
    });

    image_list_ = std::make_shared<cardlist::CardList<CardPtr>>(
        pane_, cardlist::Style::Multi | cardlist::Style::VerticalScroll,
        comm_.services);
    // cardpane.d:2710-2716は一覧と表の両方にXML転送
    // を設定する。CardListの汎用転送元はインデックス・タイトルのテ
    // キストなので、下記の型付き転送元を設定する前に既定の接続を解除する。
    image_list_->setDefaultDragSourceEnabled(false);
    syncCardListScale();
    // ALLの全カード一覧で、同じコンパクトなカード格子の
    // 寸法を使う。ブラウザALLは指定された余白・間隔を両
    // 軸で半分にしている。Castだけをコンパクトにすると
    // 、右上のカードや外部・手札ページの枠が大きすぎる。
    const bool compact_all_list = lib::editor_window_host_options().all_mode;
    image_list_->setLayoutValues(
                                 // ALLでは左右の余白をそれぞれ1px減らす。
                                 compact_all_list ? std::max(0, std::max(0, prop_.cardsMarginX) / 2 - 1)
                                                  : prop_.cardsMarginX,
                                 compact_all_list ? std::max(0, prop_.cardsSpaceX) / 2
                                                  : prop_.cardsSpaceX,
                                 compact_all_list ? std::max(0, prop_.cardsMarginY) / 2
                                                  : prop_.cardsMarginY,
                                 compact_all_list ? std::max(0, prop_.cardsSpaceY) / 2
                                                  : prop_.cardsSpaceY,
                                 compact_all_list ? 0 : prop_.cardsTitleSpace,
                                 prop_.cardsFocusLinePadding,
                                 prop_.cardsDefaultWrap);
    if (compact_all_list)
        image_list_->setTopMargin(1);
    image_list_->set_visible(false);
    image_list_->addSelectionListener([this](const cardlist::SelectionEvent& event) {
        std::vector<std::size_t> indices;
        indices.reserve(event.indices.size());
        for (const auto index : event.indices)
            if (index >= 0) indices.push_back(static_cast<std::size_t>(index));
        setSelectionFromTable(indices);
    });
    struct LifeMarker {
        lib::ResourceId image{};
        lib::Rect source{};
        lib::Rect destination{};
        lib::Rect hit{};
        bool populated{};
    };
    // Dのdutils.putHand/eventTreeMarkRect
    // に合わせ、描画とヒット判定で同じ型付き入力、指定原点、実資源寸法を
    // 共有する。登録表は既存のEditorのPropsが所有し、資源の寿
    // 命やCardPane/CardWindowの配置を追加変更しない。
    const auto lifeMarker = [this](const CardPtr& value, const lib::Rect bounds)
        -> std::optional<LifeMarker> {
        if (view_mode_ != CViewMode::LIFE || !value || !prop_.editorProps) return {};
        const auto& looks = prop_.editorProps->looks();
        const auto& settings = prop_.editorProps->var();
        const bool hand = static_cast<bool>(std::dynamic_pointer_cast<CastCard>(value));
        bool populated = false;
        structs::CPoint origin{};
        if (const auto cast = std::dynamic_pointer_cast<CastCard>(value)) {
            if (!settings.showHandMark.value) return {};
            populated = !cast->skills().empty() || !cast->items().empty() || !cast->beasts().empty();
            if (!populated && !settings.showHandMarkAlways.value) return {};
            origin = cast->life() > 0 ? looks.handXYWithLifeBar() : looks.handXY();
        } else {
            auto source = std::dynamic_pointer_cast<EffectCard>(value);
            if (!source || !settings.showEventTreeMark.value) return {};
            if (const auto id = source->linkId(); id != 0U) {
                if (!summary_) return {};
                if (std::dynamic_pointer_cast<SkillCard>(value)) source = summary_->skill(id);
                else if (std::dynamic_pointer_cast<ItemCard>(value)) source = summary_->item(id);
                else if (std::dynamic_pointer_cast<BeastCard>(value)) source = summary_->beast(id);
                else return {};
                if (!source) return {};
            }
            populated = source->flagDirRoot()->hasFlag() || source->flagDirRoot()->hasStep() ||
                source->flagDirRoot()->hasVariant() ||
                (settings.ignoreEmptyStart.value ? !source->isEmpty() : !source->trees().empty());
            if (!populated && !settings.showEventTreeMarkAlways.value) return {};
            bool useCount = false;
            const bool recycle = prop_.editorProps->sys().isRecycle(source->keyCodes());
            if (const auto item = std::dynamic_pointer_cast<ItemCard>(source))
                useCount = item->useLimit() > 0 || recycle;
            else if (const auto beast = std::dynamic_pointer_cast<BeastCard>(source))
                useCount = beast->useLimit() > 0 || recycle;
            origin = useCount ? looks.eventTreeXYWithCount() : looks.eventTreeXY();
        }
        const float scale = std::max(0.01F, card_list_image_scale_);
        const auto resourceScale = static_cast<std::uint32_t>(std::max(1.0F, std::ceil(scale)));
        auto& images = prop_.editorProps->images();
        const auto hitImage = hand ? images.handWith(resourceScale) : images.eventTreeWith(resourceScale);
        const auto paintImage = populated ? hitImage :
            (hand ? images.handWith(resourceScale, true) : images.eventTreeWith(resourceScale, true));
        const auto hitSize = images.imageSize(hitImage);
        const auto paintSize = images.imageSize(paintImage);
        if (!hitImage || !paintImage || hitSize.width <= 0 || hitSize.height <= 0 ||
            paintSize.width <= 0 || paintSize.height <= 0) return {};
        const float ratio = scale / static_cast<float>(resourceScale);
        const float x = bounds.x + static_cast<float>(origin.x) * scale;
        const float y = bounds.y + static_cast<float>(origin.y) * scale;
        // 小数の画像倍率はC++の拡張。次の整数倍率で復号
        // し、表示する印とヒット矩形を一緒に再標本化する。
        return LifeMarker{paintImage, {0, 0, paintSize.width, paintSize.height},
            {x, y, paintSize.width * ratio, paintSize.height * ratio},
            {x, y, hitSize.width * ratio, hitSize.height * ratio}, populated};
    };
    struct LifeHover { std::optional<lib::Point> point; int index{-1}; };
    const auto lifeHover = std::make_shared<LifeHover>();
    struct CommentHover { std::optional<lib::Point> point; };
    const auto commentHover = std::make_shared<CommentHover>();
    const auto oldImagePointer = image_list_->on_pointer;
    image_list_->on_pointer = [this, oldImagePointer, lifeMarker, lifeHover, commentHover,
        middleStart = std::optional<lib::Point>{}, middlePointer = std::uint64_t{},
        middleMoved = false](lib::PointerEvent& event) mutable {
        if (event.action == lib::PointerAction::Down && event.button == lib::PointerButton::Middle) {
            middleStart = event.position;
            middlePointer = event.pointer_id;
            middleMoved = false;
        }
        if (middleStart && event.pointer_id == middlePointer &&
            event.action == lib::PointerAction::Move) {
            middleMoved = middleMoved || std::abs(event.position.x - middleStart->x) > 3.0F ||
                std::abs(event.position.y - middleStart->y) > 3.0F;
        }
        const bool suppressMiddleAction = middleMoved && middleStart &&
            event.pointer_id == middlePointer && event.action == lib::PointerAction::Up;
        if (event.action == lib::PointerAction::Cancel || event.action == lib::PointerAction::Up)
            middleStart.reset();
        // CardListはDのクライアント座標でヒット判定するが、表示
        // 側はウィンドウ座標を渡す。自身の選択状態と、このペインのDの
        // LMouseのヒット判定の両方に対してイベントを正規化する。
        auto localEvent = event;
        const auto listBounds = image_list_->bounds();
        localEvent.position.x -= listBounds.x;
        localEvent.position.y -= listBounds.y;
        if (event.action == lib::PointerAction::Leave ||
            event.action == lib::PointerAction::Cancel)
            commentHover->point.reset();
        else
            commentHover->point = localEvent.position;
        // CardListの解放処理中に選択通知がプレビューを閉
        // じ、ホバー対象を解除する場合がある。同一性だけを保持し
        // 、古い対象を戻さず、選択後に下で再度ヒット判定する。
        const auto marked_before_selection = open_event_target_;
        const auto pointer_life = std::weak_ptr<int>(renumber_lifetime_);
        const auto pointer_binding = owner_binding_generation_;
        if (oldImagePointer) oldImagePointer(localEvent);
        if (pointer_life.expired()) return;
        if (pointer_binding != owner_binding_generation_) return;
        // CardListは通常、ホバー行が変わった場合だけヘルプを更
        // 新する。コメントの印への出入りは、同じ画像行内でも起こる。
        if (event.action == lib::PointerAction::Move ||
            event.action == lib::PointerAction::Enter ||
            event.action == lib::PointerAction::Leave ||
            event.action == lib::PointerAction::Cancel)
            image_list_->refreshToolTip();
        if (event.action == lib::PointerAction::Leave || event.action == lib::PointerAction::Cancel) {
            const auto oldHover = lifeHover->index;
            lifeHover->point.reset();
            lifeHover->index = -1;
            open_event_target_.reset();
            image_list_->set_cursor(lib::CursorKind::Arrow);
            if (oldHover >= 0 && static_cast<std::size_t>(oldHover) < cards_.size())
                image_list_->refresh(oldHover);
        } else if (event.action == lib::PointerAction::Move ||
                   event.action == lib::PointerAction::Down || event.action == lib::PointerAction::Up) {
            const auto oldHover = lifeHover->index;
            lifeHover->point = localEvent.position;
            lifeHover->index = image_list_->searchIndex(
                static_cast<int>(std::lround(localEvent.position.x)),
                static_cast<int>(std::lround(localEvent.position.y)));
            if (oldHover != lifeHover->index) {
                if (oldHover >= 0 && static_cast<std::size_t>(oldHover) < cards_.size()) image_list_->refresh(oldHover);
                if (lifeHover->index >= 0 && static_cast<std::size_t>(lifeHover->index) < cards_.size()) image_list_->refresh(lifeHover->index);
            }
        }
        event.handled = localEvent.handled;
        // 中央ボタンによるパンが完了した場合は、カードを開くクリックとして扱わない。
        if (suppressMiddleAction) return;
        // CardListは選択を適用済みだが、通常の主ボタンの押
        // 下・解放はMouseInputへ続けて渡す。この共通層
        // が連続クリックの判定とドラッグ元の開始準備を担当する。
        if (event.button == lib::PointerButton::Primary &&
            (event.action == lib::PointerAction::Down ||
             event.action == lib::PointerAction::Up)) event.handled = false;
        // このペインは下記の副ボタン解放時にコンテキストメ
        // ニューを開く。CardListはカード選択後に副
        // ボタン押下を消費するが、タッチの長押しは押下が未
        // 処理の場合だけ解放を送る。その消費を公開すると、
        // Androidの長押しでメニューが出なくなる。
        if (event.button == lib::PointerButton::Secondary &&
            event.action == lib::PointerAction::Down) event.handled = false;
        // cardpane.dのListMouseMoveは、LIFE
        // モードで小さい印の上にある場合だけHand/UseEvent
        // を実行対象にする。表や以前のLIFEホバーの対象を残すと、
        // 通常のCARDモードのダブルクリックで、プロパティより先
        // にイベントが開く。今回の画像一覧のヒットから再計算する。
        const bool release_mark = event.action == lib::PointerAction::Up &&
            event.button == lib::PointerButton::Primary;
        const bool press_mark = event.action == lib::PointerAction::Down &&
            event.button == lib::PointerButton::Primary;
        if (event.action == lib::PointerAction::Move || press_mark || release_mark) {
            open_event_target_.reset();
            // DのListMouseMoveは、読み取り専用の取り込み
            // ペインでも移動用の印を有効にする。実行すると入力元のCast
            // や使用時イベントを開き、ペインの内容は変えない。
            if (view_mode_ == CViewMode::LIFE) {
                const auto index = image_list_->searchIndex(
                    static_cast<int>(std::lround(localEvent.position.x)),
                    static_cast<int>(std::lround(localEvent.position.y)));
                if (index >= 0 && static_cast<std::size_t>(index) < cards_.size()) {
                    // Dの印は描画画像からの相対位置であり、タイトルやフォーカス
                    // 余白を含む外側のCardList項目からの位置ではない。
                    const auto bounds = image_list_->getImageBounds(index);
                    const auto marker = lifeMarker(cards_[static_cast<std::size_t>(index)], bounds);
                    const auto& hit_card = cards_[static_cast<std::size_t>(index)];
                    if (marker && lib::point_inside(marker->hit, localEvent.position) &&
                        (!release_mark || hit_card == marked_before_selection))
                        open_event_target_ = hit_card;
                }
            }
            // cardpane.dのListMouseMoveに合わせ、印の上はCURSOR_HAND、それ以外はnull。
            image_list_->set_cursor(open_event_target_ ? lib::CursorKind::Hand
                                                       : lib::CursorKind::Arrow);
        }
        // 小さいHand/UseEventの印で主ボタンを解放すると、そ
        // の印の操作を実行する。副ボタンの解放はカード選択とメニュー表
        // 示だけを行い、Hand/UseEventはメニューから選ぶ。
        if (event.action == lib::PointerAction::Up &&
            (event.button == lib::PointerButton::Primary ||
             event.button == lib::PointerButton::Secondary ||
             event.button == lib::PointerButton::Middle)) {
            CardPtr target;
            const auto index = image_list_->searchIndex(
                static_cast<int>(std::lround(localEvent.position.x)),
                static_cast<int>(std::lround(localEvent.position.y)));
            // cardpane.dのLMouseは副クリック下の行を使
            // うが、主ボタンの解放では、別途追跡する手札・イベントツリ
            // ーの印の上だけでHand/UseEventを開く。全カー
            // ドの主ボタンヒットを印とみなすと、後のダブルクリックを
            // 奪い、カードプロパティではなくイベントを開いてしまう。
            if ((event.button == lib::PointerButton::Secondary || event.button == lib::PointerButton::Middle) && index >= 0 &&
                static_cast<std::size_t>(index) < cards_.size()) {
                target = cards_[static_cast<std::size_t>(index)];
                // CardListはDの副ボタン押下時の複数選択を適用済み。
                if (event.button == lib::PointerButton::Middle)
                    select(static_cast<std::ptrdiff_t>(index));
            } else if (event.button == lib::PointerButton::Primary) {
                target = open_event_target_;
            }
            if (event.button == lib::PointerButton::Secondary) {
                showContextMenu(std::static_pointer_cast<lib::Widget>(image_list_), event.position);
                event.handled = true;
                return;
            }
            if (const auto cast = std::dynamic_pointer_cast<CastCard>(target)) {
                editHandWith(cast);
            }
            if (const auto effect = std::dynamic_pointer_cast<EffectCard>(target)) {
                editUseEvent(effect, false);
            }
            if (target) {
                event.handled = true;
                return;
            }
        }
        if (event.action != lib::PointerAction::DoubleClick ||
            event.button != lib::PointerButton::Primary)
            return;
        const auto index = image_list_->searchIndex(
            static_cast<int>(std::lround(localEvent.position.x)),
            static_cast<int>(std::lround(localEvent.position.y)));
        if (index < 0 || static_cast<std::size_t>(index) >= cards_.size()) return;
        // Dはヒットしたカードを直接開く。ネイティブのポインター選択がCtrl
        // の複数選択を管理するため、ダブルクリックで単一選択にしない。
        (void)edit(static_cast<std::ptrdiff_t>(index));
        event.handled = true;
    };
    if (editMode()) {
        // Dのcardpane.dのcreateCardListは、CardListEdit
        // を表の名前と同じSlow/Quickリスナー契約へ接続する。
        // 共通dutilsはヒット範囲を既存のタイトル帯に限定する。
        const std::weak_ptr<int> life = renumber_lifetime_;
        dutils::installClickSelectedEdit(
            image_list_, comm_.services, pane_,
            [this] {
                return prop_.editorProps &&
                    prop_.editorProps->var().editTriggerType.value ==
                        static_cast<std::int32_t>(structs::EditTrigger::Quick);
            },
            [this, life](const CardPtr& value) {
                if (life.expired() || !value || read_only_ || !editMode() ||
                    linkId(value)) return false;
                return std::find(cards_.begin(), cards_.end(), value) != cards_.end();
            },
            [this, life](const CardPtr& value) {
                if (!life.expired()) beginCardListNameEdit(value);
            });
    }
    const auto oldImageKey = image_list_->on_key_down;
    image_list_->on_key_down = [this, oldImageKey](lib::KeyEvent& event) {
        if (oldImageKey) oldImageKey(event);
        if (event.handled) return;
        if (event.key == lib::Key::F2 && image_list_->selection() >= 0) {
            event.handled = true;
            const auto index = static_cast<std::size_t>(image_list_->selection());
            if (!editMode() || read_only_ || index >= cards_.size() ||
                !cards_[index] || linkId(cards_[index]) || !comm_.services.gui) return;
            beginCardListNameEdit(cards_[index]);
            return;
        }
        if (event.key == lib::Key::Enter && image_list_->selection() >= 0) {
            (void)edit(static_cast<std::ptrdiff_t>(image_list_->selection()));
            event.handled = true;
        }
    };
    const auto warningImage = std::make_shared<comment::Image>();
    image_list_->additionalPaint.push_back(
        [this, lifeMarker, lifeHover, warningImage](lib::Canvas& canvas, lib::DrawingContext& context,
               const CardPtr& value, const lib::Rect bounds) {
            if (!value || bounds.width <= 0.0F || bounds.height <= 0.0F) return;
            const float scale = std::max(1.0F, card_list_image_scale_);
            if (prop_.coreVars && linkId(value) != 0U)
                context.fill_rect(bounds, toColor(
                    prop_.coreVars->linkCardMaskColor.value));
            const auto cast = std::dynamic_pointer_cast<CastCard>(value);
            // Dの合成済みCast画像にはタイトルが含まれる。ここ
            // で再描画すると、同じ名前が別の位置に二重表示される。
            if (!comm_.cardImageHasCaption || !comm_.cardImageHasCaption(value)) {
                lib::FontSpec nameFont;
                // DのLooks.cardNameFont/castCardNameFont
                // は、指定された12画素の太字。共通Canvas
                // も同じ論理画素単位なので、ここで9へ変換する
                // とDのカード画像より文字が小さく細くなる。
                nameFont.points = 12.0F * scale;
                nameFont.weight = 700U;
                // Dはメインペインの全カード名を指定された左の原点から描く。castCardNamePoint/cardNamePoint
                // はともに(5,5)で、キャスト、スキル、アイテム
                // 、召喚獣、情報カードのいずれも中央揃えにしない。
                const float cardNameX = 5.0F * scale;
                const float cardNameY = 5.0F * scale;
                const auto nameWidth =
                    std::max(1.0F, bounds.width - cardNameX * 2.0F);
                auto name = value->name();
                while (!name.empty() &&
                       context.measure_text(name, nameFont, 0.0F).width > nameWidth)
                    name.resize(lib::previous_utf8_boundary(name, name.size()));
                // DのGDIはフォント程度の高さの文字セルに名前を描き、
                // 字形の中心はcardNameY + points /
                // 2となる。SDLの行矩形はアセント・ディセントで高
                // く、字形はその中の下寄りにあるため、その上端をcardNameY
                // にすると数画素下がる。左上原点から意図的に補正
                // し、字形の描画部分の中心をDと同じ位置に置く。
                const float lineHeight = context.measure_text(
                    name.empty() ? String("M") : name, nameFont, 0.0F).height;
                const float glyphCenterY = cardNameY + nameFont.points * 0.5F;
                const float nameTop = glyphCenterY - lineHeight * 0.5F -
                    lib::measured_effective_ui_text_ink_offset(nameFont);
                const lib::Point namePoint{
                    bounds.x + cardNameX,
                    bounds.y + nameTop};
                lib::Color title{0U, 0U, 0U, 255U};
                lib::Color border{255U, 255U, 255U, 255U};
                if (prop_.coreVars) {
                    // 公開済みの一覧画像を再利用する。描画ごとにプロバイダ
                    // ーを呼ぶと、カード全体を再復号・再合成してしまう。
                    double average = 255.0;
                    if (const auto* list = dynamic_cast<const cardlist::CardList<CardPtr>*>(&canvas)) {
                        const auto index = list->indexOf(value);
                        if (index >= 0) average = imageAverage(list->items()[index]->imageData());
                    }
                    if (average < static_cast<double>(
                            prop_.coreVars->negativeCardNameBorder.value)) {
                        title = {255U, 255U, 255U, 255U};
                        border = toColor(
                            prop_.coreVars->cardNameBorderingColorForWhite.value);
                    } else {
                        border = toColor(
                            prop_.coreVars->cardNameBorderingColorForBlack.value);
                    }
                }
                // CardWirthはカード素材の上の名
                // 前に、設定された細い対比色の縁を付け、
                // 明暗どちらの画像でも読めるようにする。
                for (const auto [dx, dy] : {std::pair{-1.0F, 0.0F},
                                            std::pair{1.0F, 0.0F},
                                            std::pair{0.0F, -1.0F},
                                            std::pair{0.0F, 1.0F}})
                    context.draw_text(name, {namePoint.x + dx * scale, namePoint.y + dy * scale},
                                      nameFont, border);
                context.draw_text(name, namePoint, nameFont, title);
            }

            String number;
            if (cast)
                number = std::to_string(cast->level());
            else if (const auto skill = std::dynamic_pointer_cast<SkillCard>(value))
                number = std::to_string(skill->level());
            else if (const auto item = std::dynamic_pointer_cast<ItemCard>(value))
                number = std::to_string(item->useLimit());
            else if (const auto beast = std::dynamic_pointer_cast<BeastCard>(value))
                number = std::to_string(beast->useLimit());
            if (!number.empty()) {
                lib::FontSpec numberFont;
                numberFont.points = 16.0F * scale;
                numberFont.italic = true;
                const auto size = context.measure_text(number, numberFont, bounds.width);
                const lib::Point point{bounds.x + bounds.width - size.width - 4.0F * scale,
                                       bounds.y + 18.0F * scale};
                context.draw_text(number, point, numberFont,
                                  {112U, 112U, 112U, 210U});
            }
            if (const auto marker = lifeMarker(value, bounds);
                marker && (marker->populated ||
                    (lifeHover->point && lib::point_inside(bounds, *lifeHover->point)))) {
                context.draw_image(marker->image, marker->source, marker->destination,
                                   lib::Interpolation::Linear);
            }
            // Dは表だけでなく画像一覧にも、斜め
            // の警告表示とそのアイコンを描く。
            const auto warnings = comm_.warnings ? comm_.warnings(ownerScenario(), *value)
                                                 : getWarnings(value);
            if (!warnings.empty()) {
                if (!warningImage->valid()) {
                    comment::Props warningProps;
                    warningProps.services = comm_.services;
                    if (prop_.coreVars) {
                        warningProps.var.etc.warningImageColor = toColor(prop_.coreVars->warningImageColor.value);
                        warningProps.var.etc.warningImageWidthForCardList =
                            prop_.coreVars->warningImageWidthForCardList.value;
                    }
                    const int width = warningProps.var.etc.warningImageWidthForCardList;
                    *warningImage = comm_.services.graphics
                        ? comment::tiltWarningImage(warningProps, *comm_.services.graphics, width)
                        : comment::tiltWarningImage(warningProps, width);
                }
                const auto size = warningImage->logical_size;
                context.draw_image(warningImage->resource, warningImage->bounds(),
                    {bounds.x + bounds.width - size.width, bounds.y + bounds.height - size.height,
                     size.width, size.height}, lib::Interpolation::Nearest);
                if (comm_.warningImage) {
                    const float icon = 16.0F * scale;
                    const float commentOffset = value->comment().empty() ? 0.0F : icon;
                    context.draw_image(comm_.warningImage, {0, 0, 16, 16},
                        {bounds.x + bounds.width - icon - 5.0F * scale - commentOffset,
                         bounds.y + bounds.height - icon - 5.0F * scale, icon, icon},
                        lib::Interpolation::Linear);
                }
            }
            // Dのcomment.setupComment(CardList)に合わせ
            // 、コメントのあるカードは右下角の内側5pxにコメントアイコンを表示する。
            if (prop_.commentImage && !value->comment().empty()) {
                const float kIcon = 16.0F * scale;
                const float kInset = 5.0F * scale;
                const auto destination = lib::comment_marker_bounds(
                    bounds,{kIcon,kIcon},kInset,
                    lib::CommentMarkerPlacement::BottomRight);
                context.draw_image(prop_.commentImage, {0.0F, 0.0F, 16.0F, 16.0F},
                                   destination, lib::Interpolation::Linear);
            }
        });
    pane_->add(image_list_);
    resize_token_ = comm_.refCardTableColumnWidth.add(
        [this, life = std::weak_ptr<int>(renumber_lifetime_)](
            const CardTableColumn column, const float width) {
            if (life.expired()) return;
            refColumnWidth(column, width);
        });

    id_sorter_ = std::make_unique<customtable::TableSorter<CardPtr>>(
        columns_[CardTableColumn::ID],
        [this](const CardPtr& left, const CardPtr& right) { return compID(left, right); },
        [this](const CardPtr& left, const CardPtr& right) { return revCompID(left, right); });
    name_sorter_ = std::make_unique<customtable::TableSorter<CardPtr>>(
        columns_[CardTableColumn::Name],
        [this](const CardPtr& left, const CardPtr& right) { return compName(left, right); },
        [this](const CardPtr& left, const CardPtr& right) { return revCompName(left, right); });
    desc_sorter_ = std::make_unique<customtable::TableSorter<CardPtr>>(
        columns_[CardTableColumn::Desc],
        [this](const CardPtr& left, const CardPtr& right) { return compDesc(left, right); },
        [this](const CardPtr& left, const CardPtr& right) { return revCompDesc(left, right); });
    if (columns_.contains(CardTableColumn::UC)) {
        uc_sorter_ = std::make_unique<customtable::TableSorter<CardPtr>>(
            columns_[CardTableColumn::UC],
            [this](const CardPtr& left, const CardPtr& right) { return compUC(left, right); },
            [this](const CardPtr& left, const CardPtr& right) { return revCompUC(left, right); });
    }
    if (columns_.contains(CardTableColumn::Num)) {
        num_sorter_ = std::make_unique<customtable::TableSorter<CardPtr>>(
            columns_[CardTableColumn::Num],
            [this](const CardPtr& left, const CardPtr& right) { return compNum(left, right); },
            [this](const CardPtr& left, const CardPtr& right) { return revCompNum(left, right); });
    }
    const auto sortedCallback = [this] { sorted(); };
    id_sorter_->sortedEvent.push_back(sortedCallback);
    name_sorter_->sortedEvent.push_back(sortedCallback);
    desc_sorter_->sortedEvent.push_back(sortedCallback);
    if (uc_sorter_) uc_sorter_->sortedEvent.push_back(sortedCallback);
    if (num_sorter_) num_sorter_->sortedEvent.push_back(sortedCallback);

    const bool saved_import_view = !editMode();
    const bool saved_hand_view = owner_type_ != OwnerType::Summary;
    const int saved_column = saved_import_view
        ? (saved_hand_view ? prop_.importHandCardsSortColumn : prop_.importCardsSortColumn)
        : (saved_hand_view ? prop_.handCardsSortColumn : prop_.mainCardsSortColumn);
    const int saved_direction = saved_import_view
        ? (saved_hand_view ? prop_.importHandCardsSortDirection : prop_.importCardsSortDirection)
        : (saved_hand_view ? prop_.handCardsSortDirection : prop_.mainCardsSortDirection);
    if (const auto saved_handle = columnFromInt(saved_column)) {
        table_->sortColumn(saved_handle);
        table_->sortDirection(saved_direction == 2
            ? customtable::SortDirection::Down
            : customtable::SortDirection::Up);
    }

    // DのCardPaneは、表とCardListで共有する一つの検索ポップアップを所有する。
    const std::weak_ptr<int> search_life = renumber_lifetime_;
    incsearch::IncSearchSettings search_settings;
    if (prop_.coreVars) {
        search_settings.startIncrementalSearchWhenKeyDown = prop_.coreVars->startIncrementalSearchWhenKeyDown.value;
        const auto* core_vars = prop_.coreVars;
        search_settings.startIncrementalSearchWhenKeyDownProvider = [core_vars, search_life] {
            return !search_life.expired() && core_vars &&
                core_vars->startIncrementalSearchWhenKeyDown.value;
        };
        search_settings.incrementalSearchBoxWidth = prop_.coreVars->incrementalSearchBoxWidth.value;
        search_settings.incrementalSearchType = prop_.coreVars->incrementalSearchType.value;
    }
    inc_search_ = std::make_shared<incsearch::IncSearch>(comm_.services, pane_.get(),
        [this, search_life] { return !search_life.expired() && !cards().empty(); },
        std::vector<incsearch::AdditionMatcher>{}, search_settings);
    inc_search_->modEvent.push_back([this, search_life] {
        if (search_life.expired() || !inc_search_) return;
        search_text_ = inc_search_->searchText();
        refreshImpl();
    });
    inc_search_->searchTypeChanged = [this, search_life](const incsearch::SearchType type) {
        if (!search_life.expired() && prop_.editorProps)
            prop_.editorProps->var().incrementalSearchType.value = static_cast<std::int32_t>(type);
    };
    installInputHandlers();
    for (const auto& control : std::vector<WidgetPtr>{table_, image_list_}) {
        if (!control) continue;
        const auto key = control->on_key_down;
        const auto text = control->on_text_input;
        const auto blur = control->on_blur;
        const auto weak_control = std::weak_ptr<lib::Widget>(control);
        control->on_key_down = [this, search_life, weak_control, key](lib::KeyEvent& event) {
            if (search_life.expired()) return;
            const auto source = weak_control.lock();
            if (event.key == lib::Key::I && event.modifiers == lib::Modifiers::Control) {
                incSearch(); event.handled = true; return;
            }
            const auto search = inc_search_;
            if (search && search->handleHostKeyDown(event, source.get())) { event.handled = true; return; }
            if (!search_life.expired() && key) key(event);
        };
        control->on_text_input = [this, search_life, weak_control, text](lib::TextInputEvent& event) {
            if (search_life.expired()) return;
            const auto source = weak_control.lock();
            const auto search = inc_search_;
            if (search && search->handleHostTextInput(event, source.get())) { event.handled = true; return; }
            if (!search_life.expired() && text) text(event);
        };
        control->on_blur = [this, search_life, blur] {
            if (blur) blur();
            if (search_life.expired() || !inc_search_ || !comm_.services.gui) return;
            const auto search = inc_search_;
            const auto focus = comm_.services.gui->focused_widget();
            if (focus == table_ || focus == image_list_) return;
            search->handleFocusChanged(focus.get());
        };
    }

    bindDragDrop();
    if (comm_.appearanceEvent) {
        const std::weak_ptr<int> life = renumber_lifetime_;
        appearance_token_ = comm_.appearanceEvent->add([this, life] {
            if (life.expired()) return;
            const auto keep_alive = weak_from_this().lock();
            if (prop_.coreVars) {
                prop_.showHandMark = prop_.coreVars->showHandMark.value;
                prop_.showEventTreeMark = prop_.coreVars->showEventTreeMark.value;
                prop_.ignoreEmptyStart = prop_.coreVars->ignoreEmptyStart.value;
                const bool title = prop_.coreVars->showCardListTitle.value;
                if (prop_.showCardListTitle != title) {
                    prop_.showCardListTitle = title;
                    card_list_image_scale_ = -1.0F;
                }
            }
            refresh();
        });
    }
    if (comm_.refSortParamsEvent) {
        const std::weak_ptr<int> life = renumber_lifetime_;
        sort_params_token_ = comm_.refSortParamsEvent->add(
            [this, life](bool edit, bool summary, CardTableColumn column, int direction) {
                if (life.expired() || edit != editMode() ||
                    summary != (owner_type_ == OwnerType::Summary)) return;
                refSortParams(column, static_cast<customtable::SortDirection>(direction));
            });
    }
    if (editMode() && comm_.cardChangedEvent) {
        const std::weak_ptr<int> life = renumber_lifetime_;
        card_changed_token_ = comm_.cardChangedEvent->add(
            [this, life](const CardPtr& card, bool deleted) {
                if (life.expired()) return;
                if (deleted) comm_.delCardEvent.call(card);
                else comm_.refCardEvent.call(card);
            });
    }
    image_list_->setToolTip([this, commentHover](const CardPtr& value) {
        // DのsetupComment(CardList)は、ポインターがコメン
        // トアイコンの描画矩形内にある間だけ、リンクカードのヘルプを置き換える。
        if (value && prop_.commentImage && !value->comment().empty() &&
            commentHover->point) {
            const auto values = image_list_->cards();
            const auto found = std::find(values.begin(),values.end(),value);
            if (found != values.end()) {
                const auto index = static_cast<int>(
                    std::distance(values.begin(),found));
                const float scale = std::max(1.0F,card_list_image_scale_);
                const float icon = 16.0F * scale;
                const auto bounds = image_list_->getImageBounds(index);
                const auto marker = lib::comment_marker_bounds(
                    bounds,{icon,icon},5.0F*scale,
                    lib::CommentMarkerPlacement::BottomRight);
                const auto body = lib::comment_tooltip_at(
                    value->comment(),*commentHover->point,marker);
                if (!body.empty()) return String(body);
            }
        }
        return toolTip(value);
    });
    selection_token_ = comm_.refCardEvent.add([this](const CardPtr& value) {
        if (value) {
            if (const auto found = comment_dialogs_.find(value.get());
                found != comment_dialogs_.end() && found->second && comm_.commentTitle)
                found->second->title = comm_.commentTitle(value);
            refresh(value);
            sort();
        }
    });
    const std::weak_ptr<int> delete_life = renumber_lifetime_;
    edit_token_ = comm_.delCardEvent.add([this, delete_life](const CardPtr& value) {
        if (delete_life.expired() || !value) return;
        const auto deleted = value;
        // DはGCで保持された配列の状態を安定して巡回する。refresh()はsetRows()
        // を通じてcards_を置換し得るため、コールバックの前に行をコピーする。
        const auto referring_cards = cards_;
        const auto keep_alive = weak_from_this().lock();
        if (const auto found = comment_dialogs_.find(deleted.get());
            found != comment_dialogs_.end() && found->second) {
            const auto dialog = found->second;
            dialog->forceCancel();
            if (delete_life.expired()) return;
        }
        // cardpane.d:3162-3171は、削除された最上位カ
        // ード自身ではなく、それを参照していたこのペインのカードを更新
        // する。削除カードだけを更新すると、行に古いリンク診断が残る。
        for (const auto& card : referring_cards) {
            if (delete_life.expired()) return;
            if (card && linkId(card) == deleted->id()) {
                refresh(card);
                if (delete_life.expired()) return;
            }
        }
        if (!delete_life.expired()) sort();
    });
    view_mode_ = CViewMode::CARD;
    // 初期モードはCARDなので、構築直後のshowCardList()
    // は意図的に何もしない。モードを二度切り替え
    // るまで表だけの既定配置を残さず、ここでCARD
    // のヘッダー・一覧の座標を公開する。
    updateLayout();
    refreshImpl();
}

WidgetPtr CardPane::widget() const noexcept {
    // CardWindowはこの値をタブページとして組み込む。CARD/LIFE
    // では、任意のヘッダーだけの表の下にCardListを表示する。表だけを
    // 返すと、見えているCardListとDのTMouseポップアップ処理
    // が有効なウィジェットツリーから外れる。複合ペインが両表示を所有する。
    return pane_ ? std::static_pointer_cast<lib::Widget>(pane_)
                 : std::static_pointer_cast<lib::Widget>(table_);
}

std::map<CardTableColumn, TableColumnPtr> CardPane::columns() const {
    return columns_;
}

void CardPane::installInputHandlers() {
    if (!table_) return;
    const auto dispatchCardKey = [this](lib::KeyEvent& event) {
        SelectionEvent selection{event.modifiers, false};
        const bool control = hasModifier(event.modifiers, lib::Modifiers::Control);
        const bool shift = hasModifier(event.modifiers, lib::Modifiers::Shift);
        if (control && event.key == lib::Key::C) copy(selection);
        else if (control && event.key == lib::Key::X) cut(selection);
        else if (control && event.key == lib::Key::V) paste(selection);
        else if (control && event.key == lib::Key::D) clone(selection);
        else if (control && event.key == lib::Key::Z && shift && canRedo()) redo();
        else if (control && event.key == lib::Key::Z && canUndo()) undo();
        else if (control && event.key == lib::Key::Y && canRedo()) redo();
        else if (event.key == lib::Key::Delete) del(selection);
        // TABLEのF2は、共通のインライン編集ディスパッチャーが扱う。
        else if (event.key == lib::Key::Enter) {
            // Dと同様に、キーによる実行は最初の選択カードだけを開く。
            const auto values = selectedCards();
            if (!values.empty()) (void)edit(values.front());
        }
        else if (event.key == lib::Key::Up && canUp()) up();
        else if (event.key == lib::Key::Down && canDown()) down();
        else return;
        event.handled = true;
    };
    const auto oldKey = table_->on_key_down;
    table_->on_key_down = [oldKey, dispatchCardKey](lib::KeyEvent& event) {
        if (oldKey) oldKey(event);
        if (!event.handled) dispatchCardKey(event);
    };
    // Dは表とカード画像表示の両方から同じ編集ショートカット
    // を配送する。メインウィンドウ右上では画像一覧が表示・
    // フォーカスされるため、隠れた表だけにハンドラーを付け
    // ると、実際のCtrl+C/V/Z/Yが動かなくなる。
    if (image_list_) {
        const auto oldImageKey = image_list_->on_key_down;
        image_list_->on_key_down =
            [oldImageKey, dispatchCardKey](lib::KeyEvent& event) {
                if (oldImageKey) oldImageKey(event);
                if (!event.handled) dispatchCardKey(event);
            };
    }
    // カード表のコメントは、共通の項目表示側が準備する行データ。
    const auto oldWheel = table_->on_wheel;
    table_->on_wheel = [this, oldWheel, life = std::weak_ptr<int>(renumber_lifetime_)](lib::WheelEvent& event) {
        if (life.expired()) return;
        table_->set_tooltip({});
        closePreview();
        if (!life.expired() && oldWheel) oldWheel(event);
    };
    const auto oldPointer = table_->on_pointer;
    table_->on_pointer = [this, oldPointer, life = std::weak_ptr<int>(renumber_lifetime_)](lib::PointerEvent& event) {
        if (life.expired()) return;
        if (oldPointer) oldPointer(event);
        if (life.expired()) return;
        if (event.action == lib::PointerAction::Leave) {
            table_->set_tooltip({});
            if (!life.expired()) closePreview();
            return;
        }
        // TableModelは主ボタンの範囲選択を消費し
        // 、rowAtはローカル座標を受け取る。表の選択更
        // 新後にDのTMouse操作を行うが、副ボタン・ホ
        // バー対象のrowAtへウィンドウ座標を渡さない。
        const bool tableHandled = event.handled;
        // 選択状態はTableModel、ダブルクリック判定とドラ
        // ッグ開始はMouseInputが担当する。ここで通常の主
        // ボタン押下を消費すると、両方の操作を開始できなくなる。
        if (event.button == lib::PointerButton::Primary &&
            (event.action == lib::PointerAction::Down ||
             event.action == lib::PointerAction::Up)) event.handled = false;
        // コンテキストメニューは下記の副ボタン解放時に開く。副ボタン
        // 押下を消費すると、タッチの長押しが解放通知より前に止まる。
        if (event.button == lib::PointerButton::Secondary &&
            event.action == lib::PointerAction::Down) event.handled = false;
        const auto tableBounds = table_->bounds();
        const lib::Point local{event.position.x - tableBounds.x,
                               event.position.y - tableBounds.y};
        if (event.action == lib::PointerAction::Move || event.action == lib::PointerAction::Enter) {
            String tooltip;
            const auto row = table_->rowAt(local);
            if (row && *row < cards_.size() && cards_[*row])
                // 共通の目印のヒット対象がコメントヘルプを管理する。リンクカ
                // ードの参照文字列は、表の代替ツールチップとして保持する。
                tooltip = toolTip(cards_[*row]);
            table_->set_tooltip(std::move(tooltip));
            if (life.expired()) return;
        }
        // cardpane.dのTMouseに合わせ、右クリック時のCastの手札・Effect
        // の使用時イベント対象は、古い選択ではなくクリックした行で決める。
        if (event.action == lib::PointerAction::Up &&
            (event.button == lib::PointerButton::Primary ||
              event.button == lib::PointerButton::Secondary ||
             event.button == lib::PointerButton::Middle)) {
            CardPtr target;
            const auto row = table_->rowAt(local);
            if ((event.button == lib::PointerButton::Secondary || event.button == lib::PointerButton::Middle) && row &&
                *row < cards_.size()) {
                target = cards_[*row];
                select(static_cast<std::ptrdiff_t>(*row));
            } else if (event.button == lib::PointerButton::Primary) {
                target = open_event_target_;
            }
            if (event.button == lib::PointerButton::Secondary) {
                showContextMenu(std::static_pointer_cast<lib::Widget>(table_), event.position);
                event.handled = true;
                return;
            }
            if (const auto cast = std::dynamic_pointer_cast<CastCard>(target)) {
                editHandWith(cast);
            }
            if (const auto effect = std::dynamic_pointer_cast<EffectCard>(target)) {
                editUseEvent(effect, false);
            }
            if (target) {
                event.handled = true;
                return;
            }
        }
        // 取り込みカードは読み取り専用で
        // もプロパティを表示できる。主ボ
        // タンのダブルクリックでは対象行
        // を解決してedit(card)
        // を呼ぶ。TableModel
        // の選択更新による処理済み判定
        // より先にこの操作を配送する。
        if (event.action == lib::PointerAction::DoubleClick &&
            event.button == lib::PointerButton::Primary) {
            // 行の実行は、TableModelの選択コールバックより先に起
            // き得る。古い選択を再利用せず、このイベントの行を解決する。
            const auto row = table_->rowAt(local);
            const auto column = lib::hit_test_table_column(*table_, event.position.x);
            if (row && column && useNum() && editMode() &&
                *column == static_cast<std::size_t>(colIndex(CardTableColumn::Num)) &&
                canEditT(*row, *column)) {
                // DのTableTCEditが数値セルを担当する。プロパテ
                // ィを開かず、共通のダブルクリック操作でその編集部を作る。
                event.handled = false;
                return;
            }
            if (row && *row < cards_.size()) {
                (void)edit(static_cast<std::ptrdiff_t>(*row));
                event.handled = true;
            }
        }
        if (event.action == lib::PointerAction::Move) {
            previewTrigger(local);
            if (life.expired()) return;
        }
        if ((tableHandled && event.action != lib::PointerAction::Down &&
             event.action != lib::PointerAction::Up) || event.handled) return;
        // Dは説明セルの画像上だけで、主ボタンのUseEvent/Hand
        // を実行対象にする。共通表示の正確な座標情報を再利用し
        // 、文字倍率が変わっても描画とヒット判定を一致させる。
        if (event.action == lib::PointerAction::Move) {
            open_event_target_.reset();
            if (editMode()) {
                const auto row = table_->rowAt(local);
                if (row && *row < cards_.size() &&
                    (std::dynamic_pointer_cast<CastCard>(cards_[*row]) ||
                     std::dynamic_pointer_cast<EffectCard>(cards_[*row]))) {
                    const auto view = lib::prepare_item_view(table_, prop_.rowHeight);
                    const auto presented = std::find_if(
                        view.rows.begin(), view.rows.end(),
                        [&](const auto& value) { return value.model_index == *row; });
                    const auto desc = colIndex(CardTableColumn::Desc);
                    if (presented != view.rows.end() && desc >= 0 &&
                        static_cast<std::size_t>(desc) < presented->cells.size()) {
                        const auto& icon = presented->cells[static_cast<std::size_t>(desc)];
                        if (icon.icon && lib::point_inside(icon.icon_bounds, event.position))
                            open_event_target_ = cards_[*row];
                    }
                }
            }
            // cardpane.dのPreviewTriggerに合わせ、Desc画像の上でCURSOR_HANDを表示する。
            table_->set_cursor(open_event_target_ ? lib::CursorKind::Hand
                                                  : lib::CursorKind::Default);
        }
    };
    table_->on_cell_activate = [this](const std::size_t row, const std::size_t column) {
        if (useNum() && editMode() &&
            column == static_cast<std::size_t>(colIndex(CardTableColumn::Num)) &&
            canEditT(row, column)) return;
        if (row < cards_.size()) {
            (void)edit(static_cast<std::ptrdiff_t>(row));
        }
    };
}

void CardPane::showContextMenu(const lib::WidgetPtr& owner, const lib::Point position) {
    if (!owner || !comm_.services.gui) return;
    rebuildContextMenu();
    if (!context_menu_) return;
    if (context_menu_->on_open) context_menu_->on_open();
    comm_.services.gui->show_context_menu(context_menu_, owner, position);
}

void CardPane::rebuildContextMenu() {
    if (!context_menu_) {
        context_menu_ = std::make_shared<lib::Menu>(true);
        context_menu_->on_command = [this](const std::string_view command) {
            SelectionEvent selection;
            if (command == "cardpane.new") create();
            else if (command == "cardpane.import") doImport();
            else if (command == "cardpane.inc-search") incSearch();
            else if (command == "cardpane.property") editM();
            else if (command == "cardpane.open-hand") {
                // Dの読み取り専用の取り込みメニューは、通常版・ALLの両方
                // で選択した各Castに_openHandを呼ぶ。ホストモー
                // ド間で同じデリゲート経路を使う。editHand()ではenterEdit()
                // が読み取り専用ペインを拒否してしまう。
                if (!editMode() && open_hand_) {
                    const auto open = open_hand_;
                    const auto values = selectedCards();
                    const auto summary = summary_;
                    const auto owner = owner_;
                    const auto type = card_type_;
                    const auto generation = open_cohort_generation_;
                    const auto stop = comm_.stopOpen;
                    const std::weak_ptr<int> life = renumber_lifetime_;
                    const auto current = [this, life, summary, owner, generation] {
                        return !life.expired() && open_cohort_generation_ == generation &&
                            summary_ == summary && owner_ == owner;
                    };
                    for (const auto& value : values) {
                        if (!current()) break;
                        const bool stopped = stop && stop();
                        if (!current() || stopped || indexOf(owner, type, value) < 0) break;
                        if (const auto cast = std::dynamic_pointer_cast<CastCard>(value)) open(cast);
                    }
                } else editHand();
            } else if (command == "cardpane.use-event") editUseEvent(false);
            else if (command == "cardpane.remove-ref") removeRef();
            else if (command == "cardpane.comment") writeComment();
            else if (command == "cardpane.undo") undo();
            else if (command == "cardpane.redo") redo();
            else if (command == "cardpane.cut") cut(selection);
            else if (command == "cardpane.copy") copy(selection);
            else if (command == "cardpane.paste") paste(selection);
            else if (command == "cardpane.delete") del(selection);
            else if (command == "cardpane.clone") clone(selection);
            else if (command == "cardpane.select-all") selectAll();
            else if (command == "cardpane.select-connected") selectConnectedResource();
            else if (command == "cardpane.find-id") replaceID();
            else if (command == "cardpane.renumber") reNumbering();
            else if (command == "cardpane.copy-xml") copySelectionXML();
        };
        context_menu_->on_open = [this] { rebuildContextMenu(); };
    }

    const auto item = [](std::string text, std::string command, const bool enabled) {
        lib::MenuItem value;
        value.text = std::move(text);
        value.command = std::move(command);
        value.enabled = enabled;
        return value;
    };
    const auto menu_text = [this](const MenuID id, String fallback) {
        if (comm_.menuText) {
            if (auto value = comm_.menuText(id); !value.empty()) return value;
        }
        return fallback;
    };
    const auto separator = [] {
        lib::MenuItem value;
        value.separator = true;
        return value;
    };

    std::vector<lib::MenuItem> items;
    // cardpane.d:3075-3101の読み取り専用の契約
    // に対応する。編集ペインにも同じ共通コマンドを残し、既存のcanDo*
    // 判定で変更を許可する。別のコマンド経路は作らない。
    items.push_back(item(menu_text(MenuID::IncSearch, "Incremental search"), "cardpane.inc-search",
                         !cards_.empty()));
    items.push_back(separator());
    if (read_only_)
        items.push_back(item(menu_text(MenuID::Import, "Import"), "cardpane.import", canDoImport()));
    if (read_only_) items.push_back(separator());
    items.push_back(separator());
    // DのcanEditは、読み取り専用ペインでもリンクカードのプロパ
    // ティ画面を許可する。ダイアログ自身には読み取り専用フラグを渡す。
    const auto selected = selection();
    const bool canShowProperty = selected &&
        (linkId(selected) == 0U || static_cast<bool>(pOwnerCard(linkId(selected))));
    items.push_back(item(menu_text(read_only_ ? MenuID::ShowProp : MenuID::EditProp,
                                  read_only_ ? "Show properties" : "Edit properties"),
                          "cardpane.property", canShowProperty));
    if (card_type_ == CardType::Skill || card_type_ == CardType::Item ||
        card_type_ == CardType::Beast) {
        items.push_back(item(menu_text(MenuID::EditEventAtTimeOfUsing,
                                       "Edit event at time of using"), "cardpane.use-event",
                              canEditUseEvent()));
    }
    if (card_type_ == CardType::Cast) {
        items.push_back(item(menu_text(MenuID::OpenHand, "Open hand"), "cardpane.open-hand",
                              canEditHand()));
    }
    if (editMode() && !read_only_) {
        const auto new_id = [this] {
            switch (card_type_) {
            case CardType::Cast: return MenuID::NewCast;
            case CardType::Skill: return MenuID::NewSkill;
            case CardType::Item: return MenuID::NewItem;
            case CardType::Beast: return MenuID::NewBeast;
            case CardType::Info: return MenuID::NewInfo;
            }
            return MenuID::NewCast;
        }();
        items.push_back(separator());
        items.push_back(item(menu_text(new_id, "New"), "cardpane.new",
                             static_cast<bool>(summary_)));
        if (card_type_ == CardType::Skill || card_type_ == CardType::Item) {
            bool held{};
            if (const auto value = selection()) {
                if (const auto skill = std::dynamic_pointer_cast<SkillCard>(value)) held = skill->hold();
                else if (const auto item_card = std::dynamic_pointer_cast<ItemCard>(value)) held = item_card->hold();
            }
            auto hold_item = item(menu_text(MenuID::Hold, "Hold"), "cardpane.hold", canHold());
            hold_item.checked = held;
            hold_item.action = [this, held] { hold(!held); };
            items.push_back(std::move(hold_item));
        }
        if (owner_type_ == OwnerType::Cast) {
            const auto source_cards = pOwnerCards();
            auto add_hand = item(menu_text(MenuID::AddHand, "Add owned card"),
                                 "cardpane.add-hand",
                                 static_cast<bool>(owner_) && static_cast<bool>(summary_) &&
                                     !source_cards.empty());
            add_hand.icon = prop_.cardImage;
            add_hand.children.reserve(source_cards.size());
            for (const auto& source : source_cards) {
                if (!source) continue;
                lib::MenuItem child = item(
                    std::to_string(source->id()) + "." + source->name(),
                    "cardpane.add-hand." + std::to_string(source->id()), true);
                child.icon = prop_.cardImage;
                child.action = [this, source, life = std::weak_ptr<int>(renumber_lifetime_)] {
                    if (life.expired()) return;
                    const auto keep_alive = weak_from_this().lock();
                    if (!owner_ || !summary_ || read_only_) return;
                    const auto binding = owner_binding_generation_;
                    const auto services = comm_.services;
                    // DのrefAddHandMenuは、所有カードの直列化と手札への挿
                    // 入確定の前に、アクティブなカードコントロールへフォーカスを戻す。
                    dutils::forceFocus(inputWidget(), false, services);
                    // フォーカスのコールバックが、挿入先の手札を終了・再接続し得る。
                    if (life.expired() || owner_binding_generation_ != binding || read_only_) return;
                    // この階層メニューはペイン内の貼り付けではない。入力元はSummary
                    // のカード一覧、出力先はこのCastの手札。同一シナリオ・
                    // 最上位のDのフラグを明示的に保持し、refreshLink
                    // が深いコピーではなくリンクカードを作るようにする。
                    static_cast<void>(addCardsImpl(
                        std::vector<CardPtr>{source}, false, false, true, true));
                    if (!life.expired() && owner_binding_generation_ == binding) notifyToolbar();
                };
                add_hand.children.push_back(std::move(child));
            }
            items.push_back(std::move(add_hand));
        }
        items.push_back(item(menu_text(MenuID::RemoveRef, "Remove reference"), "cardpane.remove-ref",
                             canRemoveRef()));
        items.push_back(separator());
        items.push_back(item(menu_text(MenuID::Comment, "Comment"), "cardpane.comment",
                             canWriteComment()));
        items.push_back(separator());
        items.push_back(item(menu_text(MenuID::Undo, "Undo"), "cardpane.undo", canUndo()));
        items.push_back(item(menu_text(MenuID::Redo, "Redo"), "cardpane.redo", canRedo()));
    }
    items.push_back(separator());
    items.push_back(item(menu_text(MenuID::Cut, "Cut"), "cardpane.cut", canDoT()));
    items.push_back(item(menu_text(MenuID::Copy, "Copy"), "cardpane.copy", canDoC()));
    items.push_back(item(menu_text(MenuID::Paste, "Paste"), "cardpane.paste", canDoP()));
    items.push_back(item(menu_text(MenuID::Delete, "Delete"), "cardpane.delete", canDoD()));
    items.push_back(item(menu_text(MenuID::Clone, "Clone"), "cardpane.clone", canDoClone()));
    items.push_back(separator());
    items.push_back(item(menu_text(MenuID::SelectAll, "Select all"), "cardpane.select-all", canSelectAll()));
    if (!read_only_) {
        items.push_back(separator());
        items.push_back(item(menu_text(MenuID::SelectConnectedResource, "Select connected resource"),
                             "cardpane.select-connected", canSelectConnectedResource()));
        items.push_back(separator());
        items.push_back(item(menu_text(MenuID::FindID, "Find ID"), "cardpane.find-id", canReplaceID()));
        items.push_back(separator());
        items.push_back(item(menu_text(MenuID::ReNumbering, "Renumber"), "cardpane.renumber",
                             canReNumbering()));
    }
    items.push_back(separator());
    // Dは編集用と読み取り専用の両ポップアップにこの階層メニューを追加する。末端項
    // 目では、既存の共通コピーコマンドとクリップボードの利用可否判定を維持する。
    const bool has_selection = static_cast<bool>(selection());
    const bool can_copy_xml = has_selection &&
                              static_cast<bool>(comm_.services.clipboard);
    auto other_menu = item("その他", "cardpane.other", has_selection);
    other_menu.children.push_back(item("XMLのコピー", "cardpane.copy-xml",
                                       can_copy_xml));
    items.push_back(std::move(other_menu));
    context_menu_->items = std::move(items);
    ++context_menu_->item_update_revision;
    ++context_menu_->item_order_revision;
}

bool CardPane::idSorted() const {
    return !table_ || !table_->sortColumn() ||
        table_->sortColumn() == columnFromInt(columnToInt(CardTableColumn::ID));
}

bool CardPane::acceptsCardDrop(const bool samePane) const {
    return editMode() && !read_only_ && cards_.size() == cards().size() &&
        (!samePane || idSorted());
}

void CardPane::bindDragDrop() {
    if (!table_ || !comm_.services.drag_drop) return;
    const auto bindSource = [this](const lib::WidgetPtr& widget) {
        if (!widget) return;
        lib::DragSourceBinding source;
        source.source = widget;
        // Dでは読み取り専用の取り込み元もコピー・ドラッグ元に
        // なる。移動は公開しないが、選択カードは転送できる。
        source.allowed = lib::DropOperation::Copy | lib::DropOperation::Link;
        if (editMode()) source.allowed = source.allowed | lib::DropOperation::Move;
        source.provide_origin = [this, widget] {
            if (image_list_ && widget == image_list_) image_list_->beginDragSelection();
            lib::DragOrigin origin;
            origin.pane_id = id_;
            for (const auto& card : selectedCards())
                if (card) origin.selected_keys.push_back(std::to_string(card->id()));
            return origin;
        };
        source.provide_data = [this, widget] {
            if (image_list_ && widget == image_list_) image_list_->beginDragSelection();
            lib::ClipboardData data;
            // DのCDSListener::dragStartは、空でないカード選択
            // に加え、その転送元コントロール自身がフォーカスを持つことを要求する。
            if (!comm_.services.gui || comm_.services.gui->focused_widget() != widget)
                return data;
            const auto values = selectedCards();
            if (!values.empty()) {
                data.text = toXML(values, false);
                const auto bytes = clipboardBytes(data.text);
                // DはXMLBytesTransferでカードを公開する
                // 。従来のC++のMIME表記も維持しつつ、正規の登録
                // 名とプラットフォームのMIME別名も公開し、すべての
                // 場面の転送先が同じデータ形式で合意できるようにする。
                data.formats[String(xmlbytestransfer::XML_BYTES_FORMAT)] = bytes;
                data.formats[String(xmlbytestransfer::XML_BYTES_MIME)] = bytes;
                data.formats["application/x-cwxeditor-card-xml"] = bytes;
            }
            return data;
        };
        source.provide_visual = [this, widget]() -> std::optional<lib::DragVisual> {
            if (selection_.empty()) return std::nullopt;
            const auto index = selection_.front();
            if (widget.get() == table_.get()) {
                if (!table_ || index >= table_->rows.size()) return std::nullopt;
                const auto bounds = table_->bounds();
                const float row_height = table_->row_height > 0.0F
                    ? table_->row_height : 20.0F;
                const float header = table_->header_height > 0.0F
                    ? table_->header_height : row_height;
                return lib::DragVisual{{
                    bounds.x,
                    bounds.y + header + row_height * static_cast<float>(index),
                    bounds.width, row_height}};
            }
            if (!image_list_ || widget.get() != image_list_.get() ||
                index >= static_cast<std::size_t>(image_list_->count()))
                return std::nullopt;
            const auto item = image_list_->getBounds(static_cast<int>(index));
            const auto bounds = image_list_->bounds();
            return lib::DragVisual{{bounds.x + item.x, bounds.y + item.y,
                                    item.width, item.height}};
        };
        const auto weak = weak_from_this();
        source.completed = [weak](const lib::DropOperation) {
            if (const auto self = weak.lock()) self->notifyToolbar();
        };
        comm_.services.drag_drop->bind_source(std::move(source));
    };
    bindSource(std::static_pointer_cast<lib::Widget>(table_));
    if (image_list_) bindSource(std::static_pointer_cast<lib::Widget>(image_list_));

    if (!editMode()) return;

    const auto bindTarget = [this](const lib::WidgetPtr& widget) {
        if (!widget) return;
        const bool image_target = image_list_ && widget.get() == image_list_.get();
        lib::DropTargetBinding binding;
        binding.target = widget;
        binding.pane_id = id_;
        binding.accepted_formats = {"text/plain", "text/uri-list",
                                    "application/x-cwxeditor-card-xml",
                                    String(xmlbytestransfer::XML_BYTES_FORMAT),
                                    String(xmlbytestransfer::XML_BYTES_MIME)};
        binding.on_enter = [this, image_target](lib::DragEvent& event) {
            const bool target_matches_view = image_target || view_mode_ == CViewMode::TABLE;
            // DのCTDTListenerのdragEnter/dragOverは、TABLE
            // モードと未絞り込みの行集合だけを判定する。ID以外の並び順での同一
            // ペイン内移動は、カーソル表示時ではなく後のdrop()で拒否する。
            event.selected = !target_matches_view || !acceptsCardDrop(false)
                ? lib::DropOperation::None
                : event.same_pane() ? lib::DropOperation::Move : copyOperation(event.allowed);
            event.handled = true;
        };
        binding.on_over = binding.on_enter;
        binding.accepts_target_index = [this, image_target](
                                               const std::optional<std::size_t> index) {
            // TableViewはホバー行を提供する。CardList
            // はCanvasなので表示側の行インデックスがなく、下記
            // で配置後のポインター位置からモデルのヒット判定を行う。
            return acceptsCardDrop(false) && (image_target || !index || *index <= cards_.size());
        };
        binding.on_drop = [this, image_target](lib::DragEvent& event) {
            if (!acceptsCardDrop(event.same_pane()) || event.selected == lib::DropOperation::None) {
                event.selected = lib::DropOperation::None;
                event.handled = true;
                return;
            }
            auto drop_index = event.target_index;
            if (image_target && image_list_) {
                const auto bounds = image_list_->bounds();
                const auto local_x = static_cast<int>(std::lround(
                    event.position.x - bounds.x));
                const auto local_y = static_cast<int>(std::lround(
                    event.position.y - bounds.y));
                const int hit = image_list_->searchIndexLoose(local_x, local_y);
                drop_index = static_cast<std::size_t>(std::clamp(
                    hit, 0, static_cast<int>(cards_.size())));
            }

            // Webの外部シナリオ経路では、qCardMaterialCopy()
            // がモーダル確認を開く場合がある。ネイティブの解放コールバック内
            // から開くと、WebKit/SDLが入力終了処理へ再入し、ドラッ
            // グセッションが止まる可能性がある。転送データを所有して保持し、
            // on_dropが戻った後にメインディスパッチャーで完了する。
            const auto data = event.data;
            const auto selected = event.selected;
            utils::debugln("[CARD_DROP_ROUTE] samePane=", event.same_pane(),
                           " defer=", static_cast<bool>(event.defer),
                           " sourcePane=", event.origin.pane_id,
                           " targetPane=", event.target_pane_id);
            if (!event.same_pane() && event.defer) {
                const auto weak = weak_from_this();
                if (const auto self = weak.lock()) {
                    if (event.defer([weak, data, selected, drop_index]() {
                            if (const auto self = weak.lock())
                                return self->performDrop(data, selected, drop_index);
                            return lib::DropOperation::None;
                        })) {
                        event.handled = true;
                        return;
                    }
                }
            }

            event.selected = performDrop(data, selected, drop_index);
            event.handled = true;
        };
        comm_.services.drag_drop->bind_target(std::move(binding));
    };
    bindTarget(std::static_pointer_cast<lib::Widget>(table_));
    if (image_list_) bindTarget(std::static_pointer_cast<lib::Widget>(image_list_));
}

lib::DropOperation CardPane::performDrop(
    const lib::ClipboardData& data, const lib::DropOperation selected,
    const std::optional<std::size_t> dropIndex) {
    lib::DropOperation result = lib::DropOperation::None;
    if (!acceptsCardDrop(selected == lib::DropOperation::Move)) return result;
    const auto xml = xmlbytestransfer::CBtoXML(data);
    utils::debugln("[CARD_DROP_PAYLOAD] xmlBytes=", xml.size(),
                   " textBytes=", data.text.size(),
                   " formats=", data.formats.size(),
                   " files=", data.files.size(),
                   " selected=", static_cast<std::uint32_t>(selected));
    if (!xml.empty()) {
        try {
            auto node = XNode::parse(xml);
            static system::System defaultSystem;
            const XMLInfo version(defaultSystem,
                summary_ ? summary_->dataVersion() : String(versioninfo::LATEST_VERSION));
            const bool deferred_material = selected != lib::DropOperation::Move &&
                beginExternalMaterialDrop(node, version, dropIndex);
            if (deferred_material || addFromNode(node, version, dropIndex,
                                                 selected == lib::DropOperation::Move)) {
                if (deferred_material)
                    utils::debugln("[CARD_DROP_ASYNC] material transaction scheduled");
                result = selected == lib::DropOperation::Move
                    ? lib::DropOperation::Move : selected;
            }
        } catch (...) {
            utils::debugln("[CARD_DROP_RESULT] XML drop failed");
            result = lib::DropOperation::None;
        }
    }
    if (!data.files.empty() && comm_.doImport) {
        std::vector<String> files;
        for (const auto& file : data.files)
            files.push_back(structs::pathToUtf8(file));
        comm_.doImport(toc_, files);
        result = lib::DropOperation::Link;
    }
    utils::debugln("[CARD_DROP_RESULT] operation=", static_cast<std::uint32_t>(result));
    return result;
}

lib::TableRow CardPane::prepareRow(const CardPtr& value) const {
    lib::TableRow row;
    if (!value) return row;
    row.comment_text = value->comment();
    row.comment_icon = prop_.commentImage;
    row.cells.push_back({numberText(value->id()), {}, prop_.cardImage, true});
    const auto name = cardName(value);
    lib::ResourceId name_warning{};
    // Dのcardpane.dは、等幅文字での幅が設定上限を超えた場合
    // にNameセルだけへ印を付ける。Infoは上限なしを維持する。
    if (prop_.editorProps &&
        card_type_ != CardType::Info) {
        const auto limit = card_type_ == CardType::Cast
            ? prop_.editorProps->looks().castNameLimit()
            : prop_.editorProps->looks().nameLimit();
        const std::weak_ptr<int> life = renumber_lifetime_;
        const auto props = prop_.editorProps;
        const auto table = table_;
        const auto services = comm_.services;
        const auto warning_icon = props->images().warning();
        if (life.expired()) return {};
        const auto width = dutils::textWidth(props, table, name, services);
        if (life.expired()) return {};
        if (static_cast<std::uint64_t>(std::max(0, width)) > static_cast<std::uint64_t>(limit))
            name_warning = warning_icon;
    }
    row.cells.push_back({name, {}, name_warning, true});
    if (useNum()) {
        const auto number = cardNum(value);
        row.cells.push_back({cardNumberText(card_type_, number, prop_), {}, {}, true});
        auto& cell = row.cells.back();
        // DのnumCreateEditorは、Item/Beastの
        // 表示が無限でも、数値モデルからSpinnerを初期化する。
        cell.editor = lib::CellEditorKind::Number;
        cell.editor_value = std::to_string(number);
        cell.editor_minimum = card_type_ == CardType::Cast ? 1.0 : 0.0;
        switch (card_type_) {
        case CardType::Cast: cell.editor_maximum = prop_.castLevelMax; break;
        case CardType::Skill: cell.editor_maximum = prop_.skillLevelMax; break;
        case CardType::Item:
        case CardType::Beast: cell.editor_maximum = prop_.useCountMax; break;
        case CardType::Info: break;
        }
    }
    lib::ResourceId mark{};
    if (const auto cast = std::dynamic_pointer_cast<CastCard>(value);
        cast && prop_.showHandMark) {
        const bool populated = !cast->skills().empty() || !cast->items().empty() ||
                               !cast->beasts().empty();
        if (populated || prop_.showHandMarkAlways)
            mark = populated ? prop_.handImage : prop_.handEmptyImage;
    } else if (const auto effect = std::dynamic_pointer_cast<EffectCard>(value);
               effect && prop_.showEventTreeMark) {
        auto source = effect;
        if (const auto id = linkId(value); id != 0U)
            source = std::dynamic_pointer_cast<EffectCard>(pOwnerCard(id));
        const bool populated = source &&
            (source->flagDirRoot()->hasFlag() || source->flagDirRoot()->hasStep() ||
             source->flagDirRoot()->hasVariant() ||
             (prop_.ignoreEmptyStart ? !source->isEmpty() : !source->trees().empty()));
        if (populated || prop_.showEventTreeMarkAlways)
            mark = populated ? prop_.eventTreeImage : prop_.eventTreeEmptyImage;
    }
    row.cells.push_back({cardDesc(value), {}, mark, true});
    if (owner_type_ == OwnerType::Summary)
        row.cells.push_back({std::to_string(ucGet(summary_ ? summary_->useCounter() : nullptr,
                                                  card_type_, value->id())), {}, {}, true});
    // 共通のF2処理が実際の編集部を開く場合も、Dの編集・リンクの制限を維持する。
    for (std::size_t column = 0; column < row.cells.size(); ++column)
        row.cells[column].inline_editable = editMode() && !read_only_ &&
            linkId(value) == 0U &&
            (column == static_cast<std::size_t>(colIndex(CardTableColumn::Name)) ||
             column == static_cast<std::size_t>(colIndex(CardTableColumn::Num)));
    if (comm_.warnings) row.warnings = comm_.warnings(ownerScenario(), *value);
    if (!row.warnings.empty()) row.warning_icon = comm_.warningImage;
    return row;
}

void CardPane::setRows(std::vector<CardPtr> values, std::vector<std::size_t> selected,
                       const CardPtr& changed) {
    if (!table_) return;
    table_->set_tooltip({});
    const auto keep_alive = weak_from_this().lock();
    const auto changed_card = changed;
    const auto request = ++render_revision_;
    const auto target = table_;
    const auto old_selection = selection_;
    const auto old_table_selection = target->selection;
    std::vector<lib::TableRow> rows;
    std::vector<customtable::TableRowState> states;
    rows.reserve(values.size());
    states.reserve(values.size());
    for (const auto& value : values) {
        rows.push_back(prepareRow(value));
        customtable::TableRowState state;
        state.data = value;
        states.push_back(std::move(state));
    }
    selected.erase(std::remove_if(selected.begin(), selected.end(),
        [&](std::size_t index) { return index >= values.size(); }), selected.end());
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    const auto list = image_list_;
    const auto table_revision = target->row_order_revision;
    std::optional<cardlist::CardList<CardPtr>::PreparedRefresh> images;
    std::vector<int> image_selection;
    for (const auto index : selected) image_selection.push_back(static_cast<int>(index));
    const bool needs_images = bool(list);
    if (needs_images) {
        std::optional<std::vector<bool>> replace_images;
        if (changed_card) {
            replace_images.emplace(values.size(), true);
            for (std::size_t index = 0; index < values.size(); ++index)
                (*replace_images)[index] = index >= cards_.size() ||
                    values[index] != cards_[index] || sameCard(values[index], changed_card) ||
                    (values[index] && linkId(values[index]) == changed_card->id());
        }
        const auto image_provider = view_mode_ == CViewMode::LIFE &&
                                            comm_.cardLifeImageData
                                        ? comm_.cardLifeImageData
                                        : comm_.cardImageData;
        images = list->prepareRefresh(
            values,
            [provider = std::move(image_provider)](const CardPtr& value) {
                if (provider) return provider(value);
                lib::ImageData image;
                image.format = lib::ImageFormat::Bitmap;
                lib::ImageFrame frame;
                frame.logical_size = {64.0F, 96.0F};
                frame.pixel_width = 64U;
                frame.pixel_height = 96U;
                frame.pixel_format = lib::PixelFormat::BGRA32;
                frame.pixels.resize(64U * 96U * 4U, std::byte{0});
                image.frames.push_back(std::move(frame));
                return image;
            },
            prop_.showCardListTitle
                ? cardlist::CardList<CardPtr>::TitleCreator([this](const CardPtr& value) {
                    if (!value) return String{};
                    return numberText(value->id()) + "." + cardName(value);
                })
                : cardlist::CardList<CardPtr>::TitleCreator{},
            std::move(image_selection), std::move(replace_images),
            view_mode_ != CViewMode::TABLE);
    }
    if (request != render_revision_ || target != table_ || list != image_list_ ||
        selection_ != old_selection || target->selection != old_table_selection ||
        table_revision != target->row_order_revision ||
        (needs_images && (!images || !list->canPublish(*images)))) return;
    // 表の計測もプロバイダー呼び出し。複合更新の一部でも公
    // 開する前に、準備した画像一覧と対象状態を再検査する。
    const auto current=[&] {
        return request==render_revision_ && target==table_ && list==image_list_ &&
            selection_==old_selection && target->selection==old_table_selection &&
            table_revision==target->row_order_revision &&
            (!needs_images || (images && list->canPublish(*images)));
    };
    if(!target->replaceRows(std::move(rows), selected, std::move(states), {}, current)) return;
    cards_.swap(values);
    selection_.swap(selected);
    if (!selection_.empty()) (void)lib::reveal_table_row(*target, selection_.front());
    if (images) (void)list->publishRefresh(*images);
    target->redraw();
    if (request == render_revision_ && list == image_list_ && images) {
        list->finishRefresh(false);
        // TABLEモードの非表示の画像一覧は遅延処理のままにする。スクロ
        // ールさせると、表の新しい行を公開した後に画像準備が強制される。
        if (view_mode_ != CViewMode::TABLE && !selection_.empty())
            list->scroll(static_cast<int>(selection_.front()));
    }
}

void CardPane::refreshImpl() {
    syncCardListScale();
    if (!owner_) {
        setRows({}, {});
        refreshStatusLine();
        return;
    }
    // Dの単一選択の保持に対する意図的な拡張。更新
    // 後も表示される選択カードをすべて維持する。
    const auto selectedIds = selectionIDs();
    auto values = cardsNarrow();
    sort(values);
    std::vector<std::size_t> selected;
    for (const auto id : selectedIds) {
        const auto it = std::find_if(values.begin(), values.end(),
                                     [id](const CardPtr& value) {
                                         return value && value->id() == id;
                                     });
        if (it != values.end()) selected.push_back(
            static_cast<std::size_t>(std::distance(values.begin(), it)));
    }
    setRows(std::move(values), std::move(selected));
    refreshStatusLine();
}

// Dのcardpane.dのwriteCommentImplは、モードレスの各コメントをreplText
// へ接続する。現在の絞り込みで隠れた行も含め、所有関係からカードを解決する。
void CardPane::refreshCommentTitles() {
    const auto title = comm_.commentTitle;
    if (!title || comment_dialogs_.empty()) return;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto generation = owner_binding_generation_;
    const auto cards = cardsFrom(card_type_, owner_);
    const auto dialogs = comment_dialogs_;
    for (const auto& card : cards) {
        if (!card) continue;
        const auto found = dialogs.find(card.get());
        if (found == dialogs.end() || !found->second) continue;
        const auto dialog = found->second;
        const auto next = title(card);
        if (life.expired() || owner_binding_generation_ != generation) return;
        const auto current = comment_dialogs_.find(card.get());
        if (current != comment_dialogs_.end() && current->second == dialog)
            dialog->title = next;
    }
}

void CardPane::refresh() {
    if (read_only_ && editMode()) read_only_ = prop_.readOnly;
    refreshImpl();
}

void CardPane::refresh(const CardPtr& value, const bool updateStatus) {
    if (!table_ || !value) return;
    if (std::any_of(cards_.begin(), cards_.end(), [&](const CardPtr& item) {
            return sameCard(item, value) || linkId(item) == value->id();
        })) setRows(cards_, selection_, value);
    if (updateStatus) refreshStatusLine();
}

void CardPane::refreshAll(SummaryPtr summary, CWXPathPtr owner) {
    // 通常のMainWindowの更新はこのドキュメントを再利用する。
    // Dは更新前にインライン編集を確定するが、ドキュメントの置換時は古
    // い編集中の値を別のSummaryへ公開せず、意図的に取り消す。
    if (summary_.get() == summary.get() && owner_.get() == owner.get() &&
        editMode() && !enterEdit()) return;
    const auto preview_life = std::weak_ptr<int>(renumber_lifetime_);
    closePreview();
    if (preview_life.expired()) return;
    ++open_cohort_generation_;
    const bool document_replaced = summary_.get() != summary.get() ||
                                   owner_.get() != owner.get();
    if (document_replaced) {
        ++owner_binding_generation_;
        // MainWindowの固定カードペインは文書の置換後も残る
        // が、操作履歴とモードレス編集部は引き継がない。一部の取り
        // 消し記録はペインを保持して後からowner_を解決するた
        // め、残すと旧文書のカードを新しい所有元へ適用してしまう。
        auto dialogs = std::move(edit_dialogs_);
        edit_dialogs_.clear();
        for (const auto& [_, dialog] : dialogs) {
            if (!dialog) continue;
            dialog->applying = {};
            dialog->applied = {};
            dialog->closed = {};
            dialog->forceCancel();
        }
        auto comments = std::move(comment_dialogs_);
        comment_dialogs_.clear();
        for (const auto& [_, dialog] : comments) {
            if (!dialog) continue;
            dialog->applied = {};
            dialog->closed = {};
            dialog->forceCancel();
        }
        undo_.clear();
        undo_position_ = 0U;
        ++undo_history_revision_;
        selection_.clear();
        open_event_target_.reset();
        cancelEdit();
    }
    summary_ = std::move(summary);
    owner_ = std::move(owner);
    refreshImpl();
    if (document_replaced) notifyToolbar();
}

void CardPane::refreshTableItem(const std::size_t row, const CardPtr& value) {
    if (!table_ || row >= table_->rows.size() || row >= cards_.size() || !value) return;
    refreshListItem(row, value);
}

void CardPane::createTableItem(const CardPtr& value, const std::ptrdiff_t index) {
    if (!table_ || !value) return;
    const auto position = index < 0 ? cards_.size()
        : std::min(static_cast<std::size_t>(index), cards_.size());
    auto values = cards_;
    auto selected = selection_;
    values.insert(values.begin() + static_cast<std::ptrdiff_t>(position), value);
    for (auto& selected_index : selected) if (selected_index >= position) ++selected_index;
    setRows(std::move(values), std::move(selected));
}

void CardPane::refreshListItem(const std::size_t row, const CardPtr& value) {
    if (row >= cards_.size() || !value) return;
    auto values = cards_;
    values[row] = value;
    setRows(std::move(values), selection_, value);
}

void CardPane::refreshIDs() {
    setRows(cards_, selection_);
}

void CardPane::refreshUseCount() {
    if (owner_type_ != OwnerType::Summary) return;
    setRows(cards_, selection_);
    refreshStatusLine();
}

void CardPane::refreshStatusLine() {
    // Dは所有元のカード集合全体を報告する。
    // 逐次検索では表示行だけを絞り込める。
    const auto count = owner_ ? cards().size() : 0U;
    const auto selected = selection_.size();
    if (!owner_) {
        status_line_.clear();
    } else if (owner_type_ == OwnerType::Cast) {
        std::size_t maxCount = 0U;
        if (const auto cast = std::dynamic_pointer_cast<CastCard>(owner_)) {
            const ::cwxeditor::cwx::props::Looks fallback;
            const auto& looks = prop_.coreProps ? prop_.coreProps->looks() : fallback;
            switch (card_type_) {
            case CardType::Skill: maxCount = looks.skillCardMaxNum(cast->level()); break;
            case CardType::Item: maxCount = looks.itemCardMaxNum(cast->level()); break;
            case CardType::Beast: maxCount = looks.beastCardMaxNum(cast->level()); break;
            case CardType::Cast:
            case CardType::Info: break;
            }
        }
        if (selected == 1U && !selection_.empty() && selection_.front() < cards_.size() && cards_[selection_.front()])
            status_line_ = utils::tryFormat(prop_.handCardStatusSelOne, count, maxCount,
                static_cast<unsigned long long>(cards_[selection_.front()]->id()));
        else if (selected > 1U)
            status_line_ = utils::tryFormat(prop_.handCardStatusSelMulti, count, maxCount, selected);
        else
            status_line_ = utils::tryFormat(prop_.handCardStatus, count, maxCount);
    } else if (selected == 1U && !selection_.empty() && selection_.front() < cards_.size() && cards_[selection_.front()]) {
        status_line_ = formatSelectedId(prop_.cardStatusSelOne, count,
                                         cards_[selection_.front()]->id());
    } else if (selected > 1U) {
        status_line_ = formatSelected(prop_.cardStatusSelMulti, count, selected);
    } else {
        status_line_ = formatCount(prop_.cardStatus, count);
    }
    const auto publishStatus = comm_.setStatusLine;
    const auto statusOwner = std::static_pointer_cast<lib::Widget>(table_);
    const auto status = status_line_;
    if (publishStatus && statusOwner) publishStatus(statusOwner, status);
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void CardPane::refreshR(const StringView, const StringView) { refreshImpl(); }

void CardPane::refList() {
    const auto source_mode = view_mode_;
    open_event_target_.reset();
    if (dRefListClearsSelection(source_mode, CViewMode::LIFE)) deselectAll();
    view_mode_ = CViewMode::LIFE;
    updateLayout();
    refreshImpl();
}

void CardPane::refTbl() {
    open_event_target_.reset();
    view_mode_ = CViewMode::TABLE;
    updateLayout();
    refreshImpl();
}

void CardPane::showCardLife() {
    setViewMode(CViewMode::LIFE);
}

void CardPane::showCardList() {
    setViewMode(CViewMode::CARD);
}

void CardPane::showCardTable() {
    setViewMode(CViewMode::TABLE);
}

void CardPane::setViewMode(const CViewMode mode) {
    if (view_mode_ == mode) return;
    // DのshowCardListImplは行を変える前に編集中の名前・数
    // 値を確定する。両ホストで同じ編集の境界を使い、配置寸法は変えない。
    if (editMode() && !enterEdit()) return;
    const auto previous_selection_ids = selectionIDs();
    const auto previous_target = open_event_target_;
    const auto preview_life = std::weak_ptr<int>(renumber_lifetime_);
    closePreview();
    if (preview_life.expired()) return;
    open_event_target_.reset();
    const lib::ScopedRedraw redraw(comm_.services.gui, pane_);
    const auto previous = view_mode_;
    view_mode_ = mode;
    try {
        // DのrefListはCARD/LIFEの表示切替時に画像の選択を
        // 解除する。TABLEから画像への切替では、表の選択行を引き継ぐ。
        if (dRefListClearsSelection(previous, mode)) deselectAll();
        updateLayout();
        refreshImpl();
    } catch (const std::exception& error) {
        view_mode_ = previous;
        open_event_target_ = previous_target;
        // 表示形式の切替失敗時は、カードの同一性に基づく選択も元へ戻す。
        selectID(previous_selection_ids);
        // 以前の表示形式と配置の両方を再構築する。巻き戻しが成
        // 功した場合だけ、イベントの境界で編集を続行できる。
        updateLayout();
        refreshImpl();
        throw lib::RecoverableUiError(error.what());
    } catch (...) {
        view_mode_ = previous;
        open_event_target_ = previous_target;
        // 表示形式の切替失敗時は、カードの同一性に基づく選択も元へ戻す。
        selectID(previous_selection_ids);
        updateLayout();
        refreshImpl();
        throw;
    }
}

void CardPane::updateLayout() {
    if (!table_) return;
    if (view_mode_ == CViewMode::TABLE) {
        table_->setHeaderVisible(true);
        table_->horizontal_scroll_enabled = true;
        table_->set_visible(true);
        table_->set_layout_item({.horizontal = lib::Align::Stretch,
                                 .vertical = lib::Align::Stretch, .grow = 1.0F});
        if (image_list_) image_list_->set_visible(false);
        return;
    }

    // DはCViewMode.CARDとCViewMode.LIFEで同じCardList
    // を表示する。showCardListHeaderを有効にした場合、表は
    // ヘッダーの高さに縮める。カードの下に別の完全な表を置くものではない。
    const auto show_header = prop_.showCardListHeader;
    table_->setHeaderVisible(show_header);
    // cardpane.dのupdateLayoutは、CardListに添えるヘッダ
    // ー専用表示ではTable.getHorizontalBar()を隠す。汎用バー
    // を有効のままにすると、外部シナリオの表以外のタブでヘッダー下に灰色の帯が出る。
    table_->horizontal_scroll_enabled = false;
    table_->horizontal_offset = 0.0F;
    table_->set_visible(show_header);
    // ここはヘッダーの表示領域で、データ行を置く表ではない。headerHeight()
    // はフォント・UI倍率を適用済みだが、LayoutItemは論理
    // 単位を使う。明示的に制限しないと、共通フォームの最小サイズが一
    // 行分を確保し、画像一覧の上に選択カードの名前・説明が現れる。
    table_->set_layout_item({.horizontal = lib::Align::Stretch,
                             .vertical = lib::Align::Start,
                             .preferred = {0.0F, table_->headerHeight() / lib::editor_ui_text_scale()},
                             .grow = 0.0F,
                             .constrain_minimum_to_preferred = true});
    if (image_list_) {
        image_list_->set_visible(true);
        image_list_->set_layout_item({.horizontal = lib::Align::Stretch,
                                      .vertical = lib::Align::Stretch, .grow = 1.0F});
    }
}

CardTableColumn CardPane::columnVal(const TableColumnPtr& column) const {
    for (const auto& [value, handle] : columns_)
        if (handle == column) return value;
    return CardTableColumn::ID;
}

int CardPane::columnToInt(const CardTableColumn column) noexcept {
    switch (column) {
    case CardTableColumn::ID: return 0;
    case CardTableColumn::Name: return 1;
    case CardTableColumn::Desc: return 2;
    case CardTableColumn::UC: return 3;
    case CardTableColumn::Num: return 4;
    }
    return -1;
}

TableColumnPtr CardPane::columnFromInt(const int column) const {
    const auto find_column = [this](const CardTableColumn value) {
        const auto found = columns_.find(value);
        return found == columns_.end() ? TableColumnPtr{} : found->second;
    };
    const auto id_column = find_column(CardTableColumn::ID);
    switch (column) {
    case 0: return id_column;
    case 1: return find_column(CardTableColumn::Name);
    case 2: return find_column(CardTableColumn::Desc);
    case 3:
        if (const auto handle = find_column(CardTableColumn::UC)) return handle;
        return id_column;
    case 4:
        if (const auto handle = find_column(CardTableColumn::Num)) return handle;
        return id_column;
    default: return id_column;
    }
}

bool CardPane::compID(const CardPtr& left, const CardPtr& right) const {
    return left && right ? left->id() < right->id() : static_cast<bool>(right);
}

bool CardPane::compName(const CardPtr& left, const CardPtr& right) const {
    if (!left || !right) return !left && right;
    const auto l = cardName(left);
    const auto r = cardName(right);
    // Dのincmpは数値列を比較し、同値なら元の数字表記で順序を決める。
    const auto comparison = prop_.logicalSort
        ? ::cwxeditor::cwx::utils::incmp(l, r)
        : (lower(l) == lower(r) ? 0 : (lower(l) < lower(r) ? -1 : 1));
    return comparison == 0 ? compID(left, right) : comparison < 0;
}

bool CardPane::compDesc(const CardPtr& left, const CardPtr& right) const {
    if (!left || !right) return !left && right;
    const auto l = cardDesc(left);
    const auto r = cardDesc(right);
    const auto comparison = prop_.logicalSort
        ? ::cwxeditor::cwx::utils::incmp(l, r)
        : (lower(l) == lower(r) ? 0 : (lower(l) < lower(r) ? -1 : 1));
    return comparison == 0 ? compID(left, right) : comparison < 0;
}

bool CardPane::compUC(const CardPtr& left, const CardPtr& right) const {
    if (!left || !right) return !left && right;
    const auto l = ucGet(summary_ ? summary_->useCounter() : nullptr, card_type_, left->id());
    const auto r = ucGet(summary_ ? summary_->useCounter() : nullptr, card_type_, right->id());
    return l == r ? compID(left, right) : l < r;
}

bool CardPane::compNum(const CardPtr& left, const CardPtr& right) const {
    if (!left || !right) return !left && right;
    const auto l = cardNum(left);
    const auto r = cardNum(right);
    return l == r ? compID(left, right) : l < r;
}

bool CardPane::revCompID(const CardPtr& left, const CardPtr& right) const {
    return compID(right, left);
}

bool CardPane::revCompName(const CardPtr& left, const CardPtr& right) const {
    return compName(right, left);
}

bool CardPane::revCompDesc(const CardPtr& left, const CardPtr& right) const {
    return compDesc(right, left);
}

bool CardPane::revCompUC(const CardPtr& left, const CardPtr& right) const {
    if (!left || !right) return !left && right;
    const auto l = ucGet(summary_ ? summary_->useCounter() : nullptr, card_type_, left->id());
    const auto r = ucGet(summary_ ? summary_->useCounter() : nullptr, card_type_, right->id());
    // DのrevCompUCは値の順を逆にするが、同値ではIDの昇順を維持する。
    return l == r ? compID(left, right) : l > r;
}

bool CardPane::revCompNum(const CardPtr& left, const CardPtr& right) const {
    if (!left || !right) return !left && right;
    const auto l = cardNum(left), r = cardNum(right);
    return l == r ? compID(left, right) : l > r;
}

void CardPane::sort(std::vector<CardPtr>& values) const {
    if (values.size() < 2U) return;
    CardTableColumn column = CardTableColumn::ID;
    auto direction = customtable::SortDirection::None;
    if (table_) {
        if (table_->sortColumn()) column = columnVal(table_->sortColumn());
        direction = table_->sortDirection();
    }
    if (direction == customtable::SortDirection::None) return;

    const auto compare = [this, column](const CardPtr& left, const CardPtr& right) {
        switch (column) {
        case CardTableColumn::ID: return compID(left, right);
        case CardTableColumn::Name: return compName(left, right);
        case CardTableColumn::Desc: return compDesc(left, right);
        case CardTableColumn::UC: return compUC(left, right);
        case CardTableColumn::Num: return compNum(left, right);
        }
        return false;
    };
    const bool reverse = direction == customtable::SortDirection::Down;
    std::stable_sort(values.begin(), values.end(), [this, column, compare, reverse](const CardPtr& left,
                                                                       const CardPtr& right) {
        if (reverse && column == CardTableColumn::UC) return revCompUC(left, right);
        if (reverse && column == CardTableColumn::Num) return revCompNum(left, right);
        return reverse ? compare(right, left) : compare(left, right);
    });
}

void CardPane::sort() {
    if (sorting_) return;
    refreshImpl();
}

void CardPane::refSortParams(const CardTableColumn column,
                             const customtable::SortDirection direction) {
    if (!table_) return;
    const auto handle = columnFromInt(columnToInt(column));
    if (!handle) return;
    table_->sortColumn(handle);
    table_->sortDirection(direction);
    refreshImpl();
}

void CardPane::sorted() {
    if (!table_ || sorting_) return;
    const auto keep_alive = weak_from_this().lock();
    sorting_ = true;
    struct Reset { bool& value; ~Reset() { value = false; } } reset{sorting_};
    std::vector<CardPtr> ordered;
    ordered.reserve(table_->rows.size());
    for (std::size_t index = 0U; index < table_->rows.size(); ++index) {
        const auto& state = table_->rowState(index);
        if (const auto* value = std::any_cast<CardPtr>(&state.data); value && *value)
            ordered.push_back(*value);
    }
    if (ordered.size() != cards_.size()) return;
    auto selected = table_->selection;
    const auto list = image_list_;
    std::optional<cardlist::CardList<CardPtr>::PreparedRefresh> images;
    if (list) {
        // TableSorterは全セルと付随データを並べ替え済み。並べ替
        // え後のコールバック内で全カードを再復号せず、画像を再利用する。
        std::multimap<const Card*, std::size_t> positions;
        const auto image_cards = list->cards();
        for (std::size_t i = 0; i < image_cards.size(); ++i) positions.emplace(image_cards[i].get(), i);
        std::vector<std::size_t> order;
        for (const auto& value : ordered) {
            const auto found = positions.find(value.get());
            if (found == positions.end()) return;
            order.push_back(found->second);
            positions.erase(found);
        }
        std::vector<int> image_selection;
        for (const auto index : selected) image_selection.push_back(static_cast<int>(index));
        images = list->prepareReorder(order, image_selection);
        if (!images || !list->canPublish(*images)) return;
    }
    ++render_revision_;
    cards_.swap(ordered);
    selection_.swap(selected);
    if (list) {
        (void)list->publishRefresh(*images);
        list->finishRefresh(false);
    }
    if (comm_.storeSort && table_->sortColumn()) {
        const auto direction = table_->sortDirection() == customtable::SortDirection::Down
            ? 2 : 1;
        comm_.storeSort(editMode(), owner_type_ == OwnerType::Summary,
                        columnToInt(columnVal(table_->sortColumn())), direction);
    }
    refreshStatusLine();
}

void CardPane::nameEditEnd(const std::size_t row, String value) {
    // DのcanEditTはリンク行で編集を始めるのを
    // 防ぐ。通常行から複数選択の編集を始めた後は、リ
    // ンク行も含め全選択行へ新しい文字列を適用する。
    if (!canEditT(row, static_cast<std::size_t>(std::max(0, colIndex(CardTableColumn::Name))))) return;
    // DのTableTextEditの確定コールバックは空文字列を受け付
    // けるが、C++では意図的な仕様差として空の名前の拒否を維持する。
    if (value.empty()) return;
    // Dは確定した名前をそのまま保持し、表示幅の上限は警告にとどめる。ここでバ
    // イト単位に切り詰めると、通常の複数バイトUTF-8文字も分断してしまう。
    std::vector<CardPtr> values;
    if (std::find(selection_.begin(), selection_.end(), row) != selection_.end()) {
        for (const auto index : selection_)
            if (index < cards_.size() && cards_[index]) values.push_back(cards_[index]);
    } else if (row < cards_.size() && cards_[row]) {
        values.push_back(cards_[row]);
    }
    if (values.empty()) return;
    std::vector<std::pair<CardPtr, String>> old;
    for (const auto& cardValue : values)
        if (cardValue && cardValue->name() != value) old.push_back({cardValue, cardValue->name()});
    if (old.empty()) return;
    // DのUndoEditは変更した各カードに通知する。CPUndoは並べ
    // 替え後も、取り消し・やり直しごとに現在の選択と保存状態を交換する。
    auto history_selection = std::make_shared<std::vector<std::uint64_t>>(selectionIDs());
    const std::weak_ptr<int> history_life = renumber_lifetime_;
    const auto history_generation = owner_binding_generation_;
    const auto apply_name = [this, old, value, history_selection,
                             history_life, history_generation](bool undo) {
        if (history_life.expired() || owner_binding_generation_ != history_generation) return;
        const auto keep_alive = weak_from_this().lock();
        auto restore_selection = *history_selection;
        *history_selection = selectionIDs();
        for (const auto& [cardValue, oldName] : old)
            cardValue->name(undo ? oldName : value);
        for (const auto& [cardValue, oldName] : old) {
            (void)oldName;
            refCard(cardValue, false);
            if (history_life.expired() || owner_binding_generation_ != history_generation) return;
        }
        refreshImpl();
        if (history_life.expired() || owner_binding_generation_ != history_generation) return;
        selectID(restore_selection);
    };
    pushUndo({[apply_name] { apply_name(true); },
              [apply_name] { apply_name(false); }, "name"});
    for (const auto& [cardValue, oldName] : old) { (void)oldName; cardValue->name(value); refCard(cardValue, false); }
    refreshStatusLine();
    notifyToolbar();
}

void CardPane::numEditEnd(const std::size_t row, const std::uint32_t value) {
    const auto column = static_cast<std::size_t>(std::max(0, colIndex(CardTableColumn::Num)));
    if (!canEditT(row, column)) return;
    const auto [minimum, maximum] = [&] {
        switch (card_type_) {
        case CardType::Cast: return std::pair<std::uint32_t, std::uint32_t>{1U, prop_.castLevelMax};
        case CardType::Skill: return std::pair<std::uint32_t, std::uint32_t>{0U, prop_.skillLevelMax};
        case CardType::Item:
        case CardType::Beast: return std::pair<std::uint32_t, std::uint32_t>{0U, prop_.useCountMax};
        case CardType::Info: return std::pair<std::uint32_t, std::uint32_t>{0U, 0U};
        }
        return std::pair<std::uint32_t, std::uint32_t>{0U, 0U};
    }();
    const auto bounded = std::clamp(value, minimum, maximum);
    struct NumericEdit {
        CardPtr card;
        std::uint32_t before{}, before_max{}, after{}, after_max{};
    };
    std::vector<NumericEdit> edits;
    // Dの数値確定は、現在の選択全体に一つの取り消し単位で適用す
    // る。リンク・読み取り専用の行は既存の編集判定で保護する。
    for (const auto index : selection_) {
        if (!canEditT(index, column)) continue;
        const auto& card = cards_[index];
        NumericEdit edit{card, cardNum(card), cardNum(card), bounded, bounded};
        if (const auto item = std::dynamic_pointer_cast<ItemCard>(card)) {
            edit.before_max = item->useLimitMax();
            edit.after = edit.before == edit.before_max
                ? bounded : std::min(edit.before, bounded);
        }
        if (edit.before != edit.after || edit.before_max != edit.after_max)
            edits.push_back(std::move(edit));
    }
    if (edits.empty()) return;
    const auto assign = [type = card_type_](const NumericEdit& edit, bool undo) {
        const auto number = undo ? edit.before : edit.after;
        switch (type) {
        case CardType::Cast: std::static_pointer_cast<CastCard>(edit.card)->level(number); break;
        case CardType::Skill: std::static_pointer_cast<SkillCard>(edit.card)->level(number); break;
        case CardType::Item: {
            const auto item = std::static_pointer_cast<ItemCard>(edit.card);
            item->useLimitMax(undo ? edit.before_max : edit.after_max);
            item->useLimit(number);
            break;
        }
        case CardType::Beast: std::static_pointer_cast<BeastCard>(edit.card)->useLimit(number); break;
        case CardType::Info: break;
        }
    };
    pushUndo({[edits, assign] { for (const auto& edit : edits) assign(edit, true); },
              [edits, assign] { for (const auto& edit : edits) assign(edit, false); }, "number"});
    for (const auto& edit : edits) {
        assign(edit, false);
        refCard(edit.card, false);
    }
    refreshImpl();
    refreshStatusLine();
    notifyToolbar();
}

bool CardPane::canEditT(const std::size_t row, const std::size_t column) const {
    if (read_only_ || row >= cards_.size() || !cards_[row]) return false;
    if (linkId(cards_[row]) != 0U) return false;
    return column == static_cast<std::size_t>(std::max(0, colIndex(CardTableColumn::Name))) ||
           column == static_cast<std::size_t>(std::max(0, colIndex(CardTableColumn::Desc))) ||
           (useNum() && column == static_cast<std::size_t>(std::max(0, colIndex(CardTableColumn::Num))));
}

String CardPane::toolTip(const CardPtr& value) const {
    if (!value) return {};
    const auto id = linkId(value);
    if (id == 0U) return {};
    const auto target = pOwnerCard(id);
    if (!target) return utils::tryFormat(prop_.referencedCardIsNotFound, id);
    return utils::tryFormat(prop_.cardIsReference, id, target->name());
}

void CardPane::setSearchText(String value) {
    const auto search = inc_search_;
    if (search && search->isOpen() && search->searchText() != value) {
        search->setSearchText(std::move(value));
        return;
    }
    search_text_ = std::move(value);
    refreshImpl();
}

std::vector<String> CardPane::getWarnings(const CardPtr& value) const {
    std::vector<String> result;
    if (!value) return result;
    if (value->name().empty()) result.push_back(prop_.cardName + " is empty");
    if (linkId(value) != 0U && !pOwnerCard(linkId(value)))
        result.push_back(formatSelectedId(prop_.referencedCardIsNotFound, 0U, linkId(value)));
    return result;
}

void CardPane::setSelectionFromTable(const std::vector<std::size_t>& indices) {
    const auto preview_life = std::weak_ptr<int>(renumber_lifetime_);
    closePreview();
    if (preview_life.expired()) return;
    selection_.clear();
    for (const auto index : indices)
        if (index < cards_.size() && std::find(selection_.begin(), selection_.end(), index) == selection_.end())
            selection_.push_back(index);
    std::sort(selection_.begin(), selection_.end());
    syncSelectionControls();
    refreshStatusLine();
}

void CardPane::syncSelectionControls(const bool reveal) {
    if (table_) {
        // SWTのプログラムからのselectはSelectionイベ
        // ントを送らない。ユーザー入力として返すと、スクロールなしの
        // 指定でも入力リスナーが配列選択を見える位置へ移してしまう。
        (void)lib::select_table_rows(*table_, selection_, false, false);
        if (reveal && !selection_.empty()) (void)lib::reveal_table_row(*table_, selection_.front());
    }
    if (!image_list_) return;
    std::vector<int> image_indices;
    image_indices.reserve(selection_.size());
    for (const auto index : selection_) {
        if (index <= static_cast<std::size_t>(std::numeric_limits<int>::max()))
            image_indices.push_back(static_cast<int>(index));
    }
    image_list_->selectionIndices(image_indices);
    if (reveal && !image_indices.empty()) image_list_->scroll(image_indices.front());
}

void CardPane::updateCardSelection() {
    if (!table_) return;
    std::vector<std::size_t> mapped;
    mapped.reserve(selection_.size());
    for (const auto oldIndex : selection_) {
        if (oldIndex >= cards_.size()) continue;
        const auto target = cards_[oldIndex];
        for (std::size_t index = 0U; index < table_->rows.size(); ++index) {
            const auto& state = table_->rowState(index);
            const auto* data = std::any_cast<CardPtr>(&state.data);
            if (data && sameCard(*data, target)) {
                mapped.push_back(index);
                break;
            }
        }
    }
    selection_ = std::move(mapped);
    syncSelectionControls();
}

void CardPane::select(const std::vector<std::size_t>& indices) {
    selectIndices(indices, false);
}

void CardPane::selectIndices(const std::vector<std::size_t>& indices, const bool reveal) {
    std::vector<std::size_t> values;
    values.reserve(indices.size());
    for (const auto index : indices)
        if (index < cards_.size() && std::find(values.begin(), values.end(), index) == values.end())
            values.push_back(index);
    std::sort(values.begin(), values.end());
    selection_ = values;
    syncSelectionControls(reveal);
    refreshStatusLine();
}

void CardPane::select(const std::ptrdiff_t index) {
    if (index < 0) {
        deselectAll();
        return;
    }
    // Dの単一インデックスのオーバーロードは、選択カードを明示的に見
    // える位置へ移す。配列のオーバーロードは選択の公開だけを行う。
    selectIndices(std::vector<std::size_t>{static_cast<std::size_t>(index)}, true);
}

bool CardPane::debugTouch(const std::size_t index) {
    if (!image_list_ || index >= cards_.size()) return false;
    // 操作サーバーは、ネイティブのポインター座標がない意図的な非表
    // 示ウィンドウでも動く必要がある。デスクトップのマウスイベン
    // トを合成したり、ユーザーのアクティブウィンドウを変えたりせ
    // ず、CardListの選択通知と再描画の正確な経路を使う。
    image_list_->deselectAll();
    image_list_->select(static_cast<int>(index));
    image_list_->callSelectChanged();
    image_list_->redraw(image_list_->getBounds(static_cast<int>(index)));
    return image_list_->selectionCount() != 0;
}

void CardPane::selectID(const std::uint64_t id) {
    for (std::size_t index = 0U; index < cards_.size(); ++index) {
        if (cards_[index] && cards_[index]->id() == id) {
            select(static_cast<std::ptrdiff_t>(index));
            return;
        }
    }
}

void CardPane::selectID(const std::vector<std::uint64_t>& ids) {
    std::map<std::uint64_t, std::size_t> index_by_id;
    for (std::size_t index = 0U; index < cards_.size(); ++index)
        if (cards_[index]) index_by_id[cards_[index]->id()] = index;
    std::vector<std::size_t> values;
    values.reserve(ids.size());
    // Dはselect()前に全IDを連想配列で解決する。存在しな
    // いIDは既存の選択を変えずRangeErrorを起こす。C++
    // ではstd::out_of_rangeが対応する検索失敗。
    for (const auto id : ids) {
        const auto found = index_by_id.find(id);
        if (found == index_by_id.end())
            throw std::out_of_range("CardPane::selectID: absent card ID " + std::to_string(id));
        values.push_back(found->second);
    }
    select(values);
}

std::vector<CardPtr> CardPane::selectedCards() const {
    std::vector<CardPtr> result;
    result.reserve(selection_.size());
    for (const auto index : selection_)
        if (index < cards_.size() && cards_[index]) result.push_back(cards_[index]);
    return result;
}

std::ptrdiff_t CardPane::selectionIndex() const noexcept {
    // Dは最小の選択行ではなく、画像一覧の選択カーソルを使う。同一ペ
    // イン内のドロップが無操作かの判定も、ドラッグしたカードに従う。
    if (view_mode_ != CViewMode::TABLE && image_list_)
        return image_list_->selection();
    return selection_.empty() ? -1 : static_cast<std::ptrdiff_t>(selection_.front());
}

std::vector<std::size_t> CardPane::selectionIndices() const {
    return selection_;
}

std::vector<std::uint64_t> CardPane::selectionIDs() const {
    std::vector<std::uint64_t> result;
    result.reserve(selection_.size());
    for (const auto index : selection_)
        if (index < cards_.size() && cards_[index]) result.push_back(cards_[index]->id());
    return result;
}

CardPtr CardPane::selection() const {
    if (!summary_) return {};
    // DのCardList.selectionは複数選択内のアクティブカーソルを優先する
    // 。selectionIndicesは一括操作と状態報告用に整列済みのままにする。
    const auto index = selectionIndex();
    return index >= 0 && static_cast<std::size_t>(index) < cards_.size()
               ? cards_[static_cast<std::size_t>(index)]
               : CardPtr{};
}

std::size_t CardPane::selectionCount() const noexcept {
    return selection_.size();
}

void CardPane::deselectAll() {
    selection_.clear();
    syncSelectionControls();
    refreshStatusLine();
}

void CardPane::selectAll() {
    if (!canSelectAll()) return;
    selection_.resize(cards_.size());
    std::iota(selection_.begin(), selection_.end(), std::size_t{0});
    syncSelectionControls();
    refreshStatusLine();
}

bool CardPane::canSelectAll() const noexcept {
    return table_ && !cards_.empty() && selection_.size() != cards_.size();
}

void CardPane::pushUndo(UndoRecord record) {
    if (!record.undo || !record.redo || read_only_) return;
    ++undo_history_revision_;
    if (undo_position_ < undo_.size()) undo_.erase(undo_.begin() + static_cast<std::ptrdiff_t>(undo_position_), undo_.end());
    // Dのmax=0での早期終了に対する意図的な安全性の改善。
    // 記録無効でも、新しい編集で古いやり直し分岐を無効にする。
    if (prop_.undoMax == 0U) return;
    undo_.push_back(std::move(record));
    if (undo_.size() > prop_.undoMax) {
        const auto excess = undo_.size() - prop_.undoMax;
        undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(excess));
    }
    undo_position_ = undo_.size();
    notifyToolbar();
}

void CardPane::notifyCollectionChanged() {
    if (comm_.refUseCount) comm_.refUseCount();
    if (comm_.refOwner) comm_.refOwner();
}

void CardPane::restoreIDState(const std::vector<std::uint64_t>& ids, const std::size_t first) {
    const auto values = cardsFrom(card_type_, owner_);
    if (values.size() != ids.size() || first > values.size()) return;
    const auto counter = summary_ ? summary_->useCounter() : nullptr;
    const auto temporaryBase = std::numeric_limits<std::uint64_t>::max() - values.size();
    std::vector<std::uint64_t> oldIds;
    oldIds.resize(values.size());
    std::vector<CardPtr> changed;
    changed.reserve(values.size() - first);
    for (std::size_t i = first; i < values.size(); ++i) {
        if (!values[i]) { oldIds[i] = 0U; continue; }
        const auto oldId = values[i]->id();
        const auto temporaryId = temporaryBase + i;
        oldIds[i] = oldId;
        values[i]->id(temporaryId);
        if (owner_type_ == OwnerType::Summary)
            change(counter, card_type_, oldId, temporaryId);
    }
    for (std::size_t i = first; i < values.size(); ++i) {
        if (!values[i]) continue;
        const auto temporaryId = values[i]->id();
        values[i]->id(ids[i]);
        if (owner_type_ == OwnerType::Summary)
            change(counter, card_type_, temporaryId, ids[i]);
        if (oldIds[i] != ids[i]) changed.push_back(values[i]);
    }
    // Dは全IDが最終値になってから通知する。同期の監視
    // 通知がペインを再構築したりSummary全体を調べ
    // たりし得るため、一時値と最終値の混在を公開しない。
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto binding_generation = owner_binding_generation_;
    for (const auto& value : changed) {
        // コールバックがペインを終了させたり、別の所有元へ再接続したりす
        // る場合がある。いずれも、古い復元処理の通知一式を使い続けない。
        if (life.expired() || owner_binding_generation_ != binding_generation)
            return;
        refCard(value, false);
    }
}

std::uint64_t CardPane::nextId() const {
    std::uint64_t id = 1U;
    for (const auto& value : cards())
        if (value) id = std::max(id, value->id() + 1U);
    for (const auto& value : pOwnerCards())
        if (value) id = std::max(id, value->id() + 1U);
    return id;
}

CardPtr CardPane::duplicateCard(const CardPtr& value) const {
    if (!value) return {};
    auto result = value->dup();
    if (!result) return {};
    result->id(nextId());
    return result;
}

CardPtr CardPane::addCard(const CardPtr& value, const std::ptrdiff_t index) {
    if (!owner_ || !value || read_only_) return {};
    if (cardTypeFrom(value) != card_type_) return {};
    auto inserted = value;
    if (index >= 0) inserted = insert(owner_, index, value);
    else if (ownerIsSummary()) {
        const auto summary = summaryFromPath(owner_);
        if (!summary) return {};
        // Summary::addは成功フラグではなく
        // カードの以前のIDを返す。そのため仮IDが0
        // の新規カードでは0も有効な結果で、DのList.add
        // も拒否しない。ID値を挿入失敗とみなさ
        // ず、結果のモデルへの所属を確認する。
        static_cast<void>(add(summary, value));
        if (indexOf(owner_, card_type_, value) < 0) return {};
    } else {
        const auto cast = std::dynamic_pointer_cast<CastCard>(owner_);
        if (!cast) return {};
        inserted = add(cast, value);
    }
    if (!inserted) return {};
    refCard(inserted);
    return inserted;
}

void CardPane::removeCard(const CardPtr& value) {
    if (!owner_ || !value) return;
    remove(owner_, value);
    delCard(value);
}

String CardPane::cardPath(const CardPtr& value) const {
    return value ? value->cwxPath(true) : String{};
}

SummaryPtr CardPane::ownerScenario() const {
    if (owner_type_ == OwnerType::Summary)
        return summaryFromPath(owner_);
    return summary_;
}

String CardPane::ownerScenarioId() const {
    const auto value = ownerScenario();
    return value ? value->id() : String{};
}

String CardPane::ownerScenarioPath() const {
    const auto value = ownerScenario();
    return value ? value->scenarioPath() : String{};
}

String CardPane::ownerScenarioName() const {
    const auto value = ownerScenario();
    return value ? value->scenarioName() : String{};
}

String CardPane::ownerScenarioAuthor() const {
    const auto value = ownerScenario();
    return value ? value->author() : String{};
}

bool CardPane::sameCard(const CardPtr& left, const CardPtr& right) const noexcept {
    return left && right && left.get() == right.get();
}

void CardPane::swapCards(const std::size_t first, const std::size_t second) {
    if (!owner_ || first >= cards().size() || second >= cards().size() || first == second) return;
    if (const auto summary = summaryFromPath(owner_)) {
        switch (card_type_) {
        case CardType::Cast: summary->swapCast(static_cast<std::ptrdiff_t>(first), static_cast<std::ptrdiff_t>(second)); break;
        case CardType::Skill: summary->swapSkill(static_cast<std::ptrdiff_t>(first), static_cast<std::ptrdiff_t>(second)); break;
        case CardType::Item: summary->swapItem(static_cast<std::ptrdiff_t>(first), static_cast<std::ptrdiff_t>(second)); break;
        case CardType::Beast: summary->swapBeast(static_cast<std::ptrdiff_t>(first), static_cast<std::ptrdiff_t>(second)); break;
        case CardType::Info: summary->swapInfo(static_cast<std::ptrdiff_t>(first), static_cast<std::ptrdiff_t>(second)); break;
        }
    } else if (const auto cast = std::dynamic_pointer_cast<CastCard>(owner_)) {
        switch (card_type_) {
        case CardType::Skill: cast->swapSkill(first, second); break;
        case CardType::Item: cast->swapItem(first, second); break;
        case CardType::Beast: cast->swapBeast(first, second); break;
        case CardType::Cast:
        case CardType::Info: return;
        }
    }
}

void CardPane::notifyToolbar() {
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

bool CardPane::enterEdit() {
    if (finishing_inline_edit_) return false;
    // Dは変更前にTableEdit.enter()を呼ぶ。保留
    // テキストは共通表示側に属するため、全ホストで同じ境界で確定
    // してから行やIDを変える。ホストの配置・寸法は変えない。
    {
        const std::weak_ptr<int> life = renumber_lifetime_;
        const auto generation = owner_binding_generation_;
        const auto gui = comm_.services.gui;
        const auto table = table_;
        const auto image_list = image_list_;
        const auto binding_current = [&] {
            return !life.expired() && owner_binding_generation_ == generation &&
                   table_ == table && image_list_ == image_list;
        };
        if (gui) {
            // DのCardPane.enterEdit()はCardListEdit
            // とTableEditの両方を確定する。table_だけの検査
            // では構造操作で行を変える際にCardListのF2編集が未確
            // 定になる。共通CardListの編集部はimage_list_
            // 、ネイティブ表の編集部はtable_が所有するため、この境
            // 界ですでに有効な方を確定してから、どちらの表示も変更する。
            const bool table_active = table &&
                gui->programmatic_inline_edit_active(table);
            if (!binding_current()) return false;
            const bool list_active = image_list &&
                gui->programmatic_inline_edit_active(image_list);
            if (!binding_current()) return false;
            // 通常バックエンドのインライン編集は一つ。独自バ
            // ックエンドが同時に二つを報告した場合、どちら
            // の選択モデル文字列を確定したいか推測しない。
            if (table_active && list_active) return false;
            const auto active_owner = table_active ? table :
                (list_active ? std::static_pointer_cast<lib::Widget>(image_list)
                             : lib::WidgetPtr{});
            if (active_owner) {
                finishing_inline_edit_ = true;
                bool replacementEdit{};
                try {
                    // 確定はコールバック前に旧表示セッションを終了する
                    // 。同じ値や不正な文字列でも編集セッションは閉じる
                    // ため、値変更の結果は無視する。確定対象はコールバ
                    // ック前に有効だった所有元だけ。置換後のセッション
                    // は開いたまま残し、要求された構造編集を拒否する。
                    (void)gui->finish_programmatic_inline_edit(active_owner, true);
                    if (!binding_current()) {
                        if (!life.expired()) finishing_inline_edit_ = false;
                        return false;
                    }
                    replacementEdit = (table &&
                        gui->programmatic_inline_edit_active(table));
                    if (!binding_current()) {
                        if (!life.expired()) finishing_inline_edit_ = false;
                        return false;
                    }
                    replacementEdit = replacementEdit || (image_list &&
                        gui->programmatic_inline_edit_active(image_list));
                    if (!binding_current()) {
                        if (!life.expired()) finishing_inline_edit_ = false;
                        return false;
                    }
                } catch (...) {
                    // 表示側のコールバック内で再接続した後に例外が出る場合が
                    // ある。ペインが生存していれば保護状態をリセットする。
                    if (!life.expired()) finishing_inline_edit_ = false;
                    throw;
                }
                if (!binding_current()) {
                    if (!life.expired()) finishing_inline_edit_ = false;
                    return false;
                }
                finishing_inline_edit_ = false;
                if (replacementEdit) return false;
            }
        }
        // 確定コールバックは、ペインを再接続せずに編集可否を取り消す場合がある。
        if (!editMode() || read_only_ || !owner_) return false;
    }
    editing_ = true;
    return true;
}

void CardPane::cancelEdit() {
    editing_ = false;
}

bool CardPane::canUndo() const noexcept {
    return editMode() && undo_position_ != 0U;
}

bool CardPane::canRedo() const noexcept {
    return editMode() && undo_position_ < undo_.size();
}

void CardPane::undo() {
    if (!canUndo()) return;
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto generation = owner_binding_generation_;
    cancelEdit();
    if (life.expired() || owner_binding_generation_ != generation || !canUndo()) return;
    const auto position = undo_position_;
    const auto history_revision = undo_history_revision_;
    // フォーカス要求前に呼び出し対象を取得する。フォーカスハンドラーの再入
    // で履歴ベクターが置換され得るため、下記のリビジョン確認で拒否する。
    const auto callback = undo_[position - 1U].undo;
    const auto beforeReplay = undo_[position - 1U].beforeReplay;
    // DのCPUndo::udbは、フォーカスによって編集が公開される前に、履歴再適用用の選択を取得する。
    if (beforeReplay) beforeReplay();
    const auto gui = comm_.services.gui;
    const auto target = inputWidget();
    if (gui && target) gui->focus(target);
    if (life.expired() || owner_binding_generation_ != generation ||
        undo_history_revision_ != history_revision || undo_position_ != position ||
        !canUndo()) return;
    if (callback) callback();
    if (life.expired() || owner_binding_generation_ != generation) return;
    if (undo_history_revision_ != history_revision || undo_position_ != position) return;
    --undo_position_;
    ++undo_history_revision_;
    refreshImpl();
    if (comm_.refUseCount) comm_.refUseCount();
    notifyToolbar();
}

void CardPane::redo() {
    if (!canRedo()) return;
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto generation = owner_binding_generation_;
    cancelEdit();
    if (life.expired() || owner_binding_generation_ != generation || !canRedo()) return;
    const auto position = undo_position_;
    const auto history_revision = undo_history_revision_;
    // フォーカス要求前に呼び出し対象を取得する。フォーカスハンドラーの再入
    // で履歴ベクターが置換され得るため、下記のリビジョン確認で拒否する。
    const auto callback = undo_[position].redo;
    const auto beforeReplay = undo_[position].beforeReplay;
    // DのCPUndo::udbは、フォーカスによって編集が公開される前に、履歴再適用用の選択を取得する。
    if (beforeReplay) beforeReplay();
    const auto gui = comm_.services.gui;
    const auto target = inputWidget();
    if (gui && target) gui->focus(target);
    if (life.expired() || owner_binding_generation_ != generation ||
        undo_history_revision_ != history_revision || undo_position_ != position ||
        !canRedo()) return;
    if (callback) callback();
    if (life.expired() || owner_binding_generation_ != generation) return;
    if (undo_history_revision_ != history_revision || undo_position_ != position) return;
    ++undo_position_;
    ++undo_history_revision_;
    refreshImpl();
    if (comm_.refUseCount) comm_.refUseCount();
    notifyToolbar();
}

void CardPane::refEventTree() {
    if (comm_.refEventTree) comm_.refEventTree();
    refreshImpl();
}

void CardPane::refScenario(SummaryPtr summary) {
    if (summary) summary_ = std::move(summary);
    if (comm_.refScenario) comm_.refScenario();
    refreshImpl();
}

void CardPane::refUndoMax() {
    setUndoLimit(prop_.undoMax);
    if (comm_.refUndoMax) comm_.refUndoMax();
}

void CardPane::setUndoLimit(const std::size_t value) {
    prop_.undoMax = value;
    // Dが上限を適用するのは適用済みの先頭部分だけ。上限を0も含め
    // て減らしても、未適用のやり直し記録は有効で、参照も維持する。
    if (undo_position_ > prop_.undoMax) {
        const auto excess = undo_position_ - prop_.undoMax;
        undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(excess));
        undo_position_ -= excess;
        ++undo_history_revision_;
    }
    notifyToolbar();
}

void CardPane::closePreview() {
    ++preview_revision_;
    preview_card_.reset();
    open_event_target_.reset();
    const auto preview = card_preview_;
    if (preview) preview->close();
}

void CardPane::previewTrigger(const lib::Point local) {
    if (!table_ || view_mode_ != CViewMode::TABLE || !prop_.editorProps) return;
    const auto row = table_->rowAt(local);
    if (!row || *row >= cards_.size()) { closePreview(); return; }
    const auto card = cards_[*row];
    if (!card || card == preview_card_) return;
    const auto life = std::weak_ptr<int>(renumber_lifetime_);
    const auto binding = owner_binding_generation_;
    closePreview();
    if (life.expired() || owner_binding_generation_ != binding) return;
    const auto revision = preview_revision_;
    const auto table = table_;
    auto* const properties = prop_.editorProps;
    const auto services = comm_.services;
    const auto provider = comm_.cardImageData;
    if (!provider) return;
    auto image = provider(card);
    // DのcardImageWithScaleは、完成した画像全体にリンクカ
    // ードの色を適用する。CardListは行の重ね描きで色を付けるため、TABLE
    // のプレビューには独立して合成した画像へ色を付ける必要がある。
    if (linkId(card) != 0U && prop_.coreVars)
        applyColorMask(image, prop_.coreVars->linkCardMaskColor.value);
    if (life.expired() || preview_revision_ != revision ||
        owner_binding_generation_ != binding || table_ != table ||
        view_mode_ != CViewMode::TABLE || *row >= cards_.size() || cards_[*row] != card) return;
    if (image.frames.empty()) return;
    const auto size = image.frames.front().logical_size;
    auto pile = std::make_shared<areaviewutils::PileImage>(
        areaviewutils::ImageDataWithScale(std::move(image)), properties->drawingScale(),
        0, 0, std::max(1, static_cast<int>(size.width)),
        std::max(1, static_cast<int>(size.height)), false);
    pile->setGraphics(services.graphics);
    pile->createImage();
    if (life.expired() || preview_revision_ != revision || owner_binding_generation_ != binding) return;
    auto preview = card_preview_;
    if (!preview) {
        preview = std::make_shared<areaviewutils::Preview>(*properties, table, services);
        if (life.expired() || preview_revision_ != revision || owner_binding_generation_ != binding) return;
        card_preview_ = preview;
    }
    preview_card_ = card;
    const auto bounds = table->rowBounds(*row);
    const auto table_bounds = table->bounds();
    preview->image(pile, static_cast<int>(table_bounds.x + bounds.x),
        static_cast<int>(table_bounds.y + bounds.y + bounds.height), static_cast<int>(bounds.height));
    if (life.expired() || preview_revision_ != revision) return;
    preview->show();
}

void CardPane::incSearch() {
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto search = inc_search_;
    const auto text = search_text_;
    const auto pane = widget();
    const auto input = inputWidget();
    const auto summary = summary_;
    const auto owner = owner_;
    const auto binding = owner_binding_generation_;
    if (!search || !pane || !input || cards().empty()) return;
    const auto services = comm_.services;
    // DはComposite.setFocus()を呼び、有効な子へ解決する。共通
    // Panelは配置用のコンテナーであり、キーボードフォーカスの対象ではない。
    if (services.gui) dutils::forceFocus(input, true, services);
    if (life.expired() || widget() != pane || inputWidget() != input ||
        summary_ != summary || owner_ != owner ||
        owner_binding_generation_ != binding || inc_search_ != search) return;
    search->startIncSearch(text);
}

void CardPane::selectChanged() {
    const auto preview_life = std::weak_ptr<int>(renumber_lifetime_);
    closePreview();
    if (preview_life.expired()) return;
    if (table_) setSelectionFromTable(table_->selection);
    else refreshStatusLine();
}

void CardPane::selectOnly(const std::ptrdiff_t index) {
    select(index);
}

void CardPane::showCardListImpl(const CViewMode mode) {
    setViewMode(mode);
}

void CardPane::updateToolTip() {
    if (!table_) return;
    const auto cursor = table_->cursorRow();
    if (cursor && *cursor < cards_.size()) table_->set_tooltip(toolTip(cards_[*cursor]));
    else table_->set_tooltip({});
}

void CardPane::refColumnWidth(const CardTableColumn column, const float width) {
    // この互換用の項目も、使用中の兄弟との同期と同じサイズ変更経路に置く。
    columnResized(column, width, false);
}

void CardPane::columnResized(const CardTableColumn column, const float width,
                            const bool broadcast) {
    if (!std::isfinite(width) || width < 0.0F ||
        static_cast<double>(width) > std::numeric_limits<int>::max() || *resizing_column_) return;
    const auto handle = columnFromInt(columnToInt(column));
    if (!handle) return;
    // 監視先がペインを閉じても、保護状態の保存領域を生存させる。
    const auto guard = resizing_column_;
    *guard = true;
    struct Reset { std::shared_ptr<bool> value; ~Reset() { *value = false; } } reset{guard};
    const std::weak_ptr<int> life = renumber_lifetime_;
    const bool editable = editMode();
    const bool summary_owner = owner_type_ == OwnerType::Summary;
    const auto store = comm_.storeColumnWidth;
    const auto event = comm_.columnWidthEvent;
    handle->width(width);
    const auto column_id = columnToInt(column);
    // DのローカルSummaryのサイズ変更は一斉通知する。受信側は自身の
    // 種別の設定を保存し、共有済みの通知経路へ同じ幅を再送する必要はない。
    if (broadcast && event &&
        (comm_.localColumnWidthSync || (editable && summary_owner)))
        event->call(this, column, width);
    if (life.expired()) return;
    if (store) store(editable, summary_owner, column_id, width);
}

String CardPane::ownerId() const {
    const auto value = ownerScenario();
    return value ? value->id() : String{};
}

String CardPane::ownerWsnVersion() const {
    const auto value = ownerScenario();
    return value ? value->dataVersion() : String{};
}

String CardPane::cardTitle(const CardPtr& value) const {
    if (!value) return {};
    return numberText(value->id()) + " " + cardName(value);
}

void CardPane::open(const bool shellActivate) {
    if (owner_type_ == OwnerType::Summary) {
        if (comm_.openCardPane) comm_.openCardPane(card_type_, shellActivate);
    } else if (owner_type_ == OwnerType::Cast) {
        const auto cast = std::dynamic_pointer_cast<CastCard>(owner_);
        if (cast && comm_.openHandsPane)
            (void)comm_.openHandsPane(summary_, cast, card_type_, shellActivate);
    }
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void CardPane::create() {
    if (!editMode() || read_only_ || !owner_) return;
    const auto value = makeCard(card_type_, nextId(), typeName(card_type_, prop_), {});
    if (!value) return;

    // Dはカード固有のダイアログで適用するまで、新規カードを
    // 未接続に保つ。先に挿入すると取消時に空・初期化途中の
    // カードが残り、右側のCardメニューが編集画面を迂回
    // したように見える。従来の即時挿入は、意図的にダイアロ
    // グファクトリーを持たないヘッドレスホストだけに残す。
    if (!comm_.makeDialog) {
        if (!addCard(value)) return;
        const auto inserted_id = value->id();
        const auto inserted_index = indexOf(owner_, card_type_, value);
        const auto live = std::make_shared<CardPtr>(value);
        pushUndo({[this, live] {
                      removeCard(*live);
                      notifyCollectionChanged();
                      refreshImpl();
                  },
                  [this, live, inserted_id, inserted_index] {
                      (*live)->id(inserted_id);
                      // これは取り込んだ値ではなく、すでに所有している作成
                      // 済みカード。同一性を維持し、既存のプロパティセッ
                      // ションが切り離された複製を編集しないようにする。
                      *live = insert(owner_, inserted_index, *live, true);
                      refCard(*live);
                      notifyCollectionChanged();
                      refreshImpl();
                      selectID(inserted_id);
                  }, "create"});
        notifyCollectionChanged();
        refreshImpl();
        selectID(value->id());
        return;
    }

    auto dialog = comm_.makeDialog(card_type_, summary_, owner_, value, false);
    if (!dialog) return;
    dialog->creationMode = true;
    edit_dialogs_[value.get()] = dialog;
    const auto inserted = std::make_shared<bool>(false);
    const auto pending_before = std::make_shared<CardPtr>();
    // プロパティ画面は無関係な編集をまたいで残り得る
    // 。カード挿入後の二回目以降の適用も含め、取消可
    // 能な検証後に、適用ごとに最新の状態を取得する。
    dialog->applying = [value, pending_before] {
        *pending_before = snapshotCardState(value);
    };
    const std::weak_ptr<int> pane_lifetime = renumber_lifetime_;
    const auto binding_generation = owner_binding_generation_;
    dialog->applied = [this, value, inserted, pending_before,
                       pane_lifetime, binding_generation] {
        if (pane_lifetime.expired()) return;
        if (owner_binding_generation_ != binding_generation) return;
        // DのappliedEventは、更新前に所有するペインを開き直す。
        open(false);
        if (pane_lifetime.expired() ||
            owner_binding_generation_ != binding_generation) return;
        const auto before = std::exchange(*pending_before, CardPtr{});
        if (!*inserted) {
            if (!addCard(value)) return;
            *inserted = true;
            const auto inserted_id = value->id();
            const auto inserted_index = indexOf(owner_, card_type_, value);
            const auto live = std::make_shared<CardPtr>(value);
            pushUndo({[this, live] {
                          removeCard(*live);
                          notifyCollectionChanged();
                          refreshImpl();
                      },
                      [this, live, inserted_id, inserted_index] {
                          (*live)->id(inserted_id);
                          *live = insert(owner_, inserted_index, *live, true);
                          refCard(*live);
                          notifyCollectionChanged();
                          refreshImpl();
                          selectID(inserted_id);
                      }, "create"});
            notifyCollectionChanged();
            refreshImpl();
            selectID(value->id());
        } else {
            const auto after = snapshotCardState(value);
            if (before && after && !sameCardState(before, after)) {
                const auto state = std::make_shared<CardPtr>(before);
                const auto exchangeProperties = [value, state] {
                    auto current = snapshotCardState(value);
                    if (cardpropertytransaction::restoreProperties(value, *state))
                        *state = std::move(current);
                };
                // 取り消し・やり直しは、プロパティの書き込み対象だけを交換する
                // 。対象の名前変更に追従する一つの管理済み履歴状態を保持する。
                pushUndo({exchangeProperties, exchangeProperties, "edit"});
            }
            refresh(value);
            refCard(value);
        }
        notifyToolbar();
    };
    dialog->closed = [this, key = value.get()] { edit_dialogs_.erase(key); };
    dialog->open();
}

std::vector<CardPtr> CardPane::createCardsFromNode(
    XNode& node, const XMLInfo& version) const {
    std::vector<CardPtr> values;
    const auto rootName = node.name();
    if (cardTypeFromXmlName(rootName, false)) {
        if (auto value = createFromNode(node, version)) values.push_back(std::move(value));
    } else if (cardTypeFromXmlName(rootName, true)) {
        for (auto child : node.element_children())
            if (auto value = createFromNode(child, version)) values.push_back(std::move(value));
    } else if (auto value = createFromNode(node, version)) {
        values.push_back(std::move(value));
    }
    return values;
}

bool CardPane::addFromNode(XNode& node, const XMLInfo& version,
                           const std::optional<std::size_t> dropIndex,
                           const bool moveExisting) {
    if (!editMode() || read_only_) return false;
    const auto rootName = node.name();
    const auto sourceSummaryAttribute = node.attribute("summId");
    const auto sourceSummary = sourceSummaryAttribute.value_or(String{});
    // DはownerIdを直接比較する。未保存のテストデー
    // タはシナリオIDが空でも有効。非空を要求すると自身の
    // ペインが外部コピーと判定され、カードが複製される。
    const bool sameSummary = sourceSummaryAttribute.has_value() && sourceSummary == ownerId();
    const auto sourcePane = node.attribute("paneId").value_or(String{});
    const bool samePane = !sourcePane.empty() && sourcePane == id_;
    const bool topLevel = boolOr(node.attribute("topLevel"), false);
    std::ptrdiff_t moveIndex = static_cast<std::ptrdiff_t>(dropIndex.value_or(cards_.size()));
    if (moveExisting && sameSummary && samePane) {
        if (!acceptsCardDrop(true) || rootName != xmlNameM() || cards_.empty()) return false;
        const auto row = std::min(dropIndex.value_or(cards_.size()), cards_.size());
        const auto selected = selectionIndex();
        if (selected >= 0 && ((row < cards_.size() ? row : cards_.size() - 1U) ==
            static_cast<std::size_t>(selected) || row == static_cast<std::size_t>(selected) + 1U))
            return false;
        // Dは選択メンバーをn-row-1で移動する。下記のペイン間の挿入
        // 間隙はn-rowを使う。両者は意図的に異なる位置の意味を持つ。
        moveIndex = table_ && table_->sortDirection() == customtable::SortDirection::Down
            ? static_cast<std::ptrdiff_t>(cards_.size()) - static_cast<std::ptrdiff_t>(row) - 1
            : static_cast<std::ptrdiff_t>(row);
        // Dは降順の末尾の間隙を-1へ変換し、カード削除後にSummary::insertImpl
        // でその不正位置を参照する。削除後の範囲外
        // 失敗を再現せず、安全な先頭挿入を維持し、
        // 取り消し用に正規化した位置を記録する。
        moveIndex = std::max<std::ptrdiff_t>(0, moveIndex);
    }
    if (dropIndex && !acceptsCardDrop(false)) return false;
    if (rootName == xmlName() || rootName == xmlNameM()) {
        // cardpane.d:1662-1688は、XMLの復元前
        // に既存のモデルIDから同一ペインのドロップを解決する。リン
        // クカードの保存形式は省略されているため、この順序が必要。
        if (moveExisting && sameSummary && samePane) {
            std::vector<CardPtr> moving;
            const auto collect = [this, &moving](XNode value) {
                const auto id = valueOr(value.attribute("id"), card::Card::readId(value));
                if (id == 0U) return;
                if (const auto card = localCard(id);
                    card && std::find_if(moving.begin(), moving.end(), [&card](const CardPtr& current) {
                        return current.get() == card.get();
                    }) == moving.end()) moving.push_back(card);
            };
            if (rootName == xmlNameM()) {
                for (const auto child : node.element_children()) collect(child);
            } else {
                collect(node);
            }
            if (!moving.empty()) {
                moveCards(moving, moveIndex, true);
                return true;
            }
        }
    }
    auto values = createCardsFromNode(node, version);
    if (values.empty()) return false;
    // 旧Card XMLの一部は、完全な読み込みでProperty
    // を生成して初めてIdを取得できる。上の直接
    // 経路を維持し、その後は解析済みIDを二度目
    // の同一性検索に使う。コピー経路へ移らない。
    if (moveExisting && sameSummary && samePane) {
        std::vector<CardPtr> moving;
        for (const auto& value : values) {
            if (!value) continue;
            if (const auto local = localCard(value->id());
                local && std::find_if(moving.begin(), moving.end(), [&local](const CardPtr& current) {
                    return current.get() == local.get();
                }) == moving.end()) moving.push_back(local);
        }
        if (moving.empty()) return false;
        moveCards(moving, moveIndex, true);
        return true;
    }
    if (!sameSummary) {
        const auto material_result = qCardMaterialCopy(node, values);
        utils::debugln("[CARD_ADD_MATERIAL] result=",
                       static_cast<int>(material_result),
                       " cards=", values.size());
        if (material_result == MaterialCopyResult::Cancelled) return false;
    }
    // Dは別ペインからこのペインの種別のカードが来た場
    // 合、ドロップ行を維持する。他の種別は下記で対応ペ
    // インへ振り分け、そのペインの末尾追加規則を使う。
    return addCardsImpl(values, samePane, samePane, sameSummary, topLevel,
                        dropIndex);
}

void CardPane::addCards(const std::vector<CardPtr>& values) {
    static_cast<void>(addCardsImpl(values, false, false, false, true));
}

bool CardPane::addCardsImpl(const std::vector<CardPtr>& values, const bool inPane,
                            const bool samePane, const bool sameSummary,
                            const bool topLevel,
                            const std::optional<std::size_t> insertIndex) {
    if (!editMode() || read_only_ || !owner_) return false;
    if (!enterEdit()) return false;
    bool changed{};
    std::map<CardType, std::vector<CardPtr>> groups;
    std::optional<CardType> lastType;
    for (const auto& value : values) {
        if (!value) continue;
        lastType = cardTypeFrom(value);
        groups[*lastType].push_back(value);
    }
    for (const auto& [sourceType, group] : groups) {
        if (sourceType != card_type_) {
            const auto target = openSameLevelPane(
                sourceType, lastType && sourceType == *lastType);
            if (target && target.get() != this)
                changed = target->addCardsImpl(
                    group, inPane, samePane, sameSummary, topLevel, {}) || changed;
            continue;
        }
        const auto oldSelection = selectionIDs();
        const auto oldCards = cardsFrom(card_type_, owner_);
        const auto oldUndo = undo_;
        const auto oldUndoPosition = undo_position_;
        const bool summaryWasChanged = summary_ && summary_->isChanged();
        const auto castOwner = std::dynamic_pointer_cast<CastCard>(owner_);
        const bool ownerWasChanged = castOwner && castOwner->isChanged();
        std::vector<std::uint64_t> oldIdState;
        for (const auto& value : oldCards)
            oldIdState.push_back(value ? value->id() : 0U);
        std::vector<CardPtr> added;
        // Dのペイン間挿入はID順の間隙を使う。ID以外の並べ替えでは常に末尾
        // 追加し、降順の間隙はn-rowへ変換する。n-row-1ではない。
        std::ptrdiff_t next_insert = -1;
        if (insertIndex) {
            const auto row = std::min(*insertIndex, oldCards.size());
            next_insert = !idSorted() ? static_cast<std::ptrdiff_t>(oldCards.size())
                : table_ && table_->sortDirection() == customtable::SortDirection::Down
                    ? static_cast<std::ptrdiff_t>(oldCards.size() - row)
                    : static_cast<std::ptrdiff_t>(row);
        }
        // 今回の呼び出しの挿入前のTABLE順を保持する。addCard()中に
        // refCardEventが同期的にペインを並べ替え得るため、メンバーバ
        // ッファや後のcards_からは以前の貼り付けの追加行を復元できない。
        std::optional<std::vector<CardPtr>> oldTableDisplay;
        if (view_mode_ == CViewMode::TABLE) oldTableDisplay = cards_;
        try {
            for (const auto& source : group) {
                auto value = source;
                refreshLink(value, samePane, sameSummary, topLevel);
                if (indexOf(owner_, card_type_, value) >= 0)
                    value = duplicateCard(source);
                if (auto inserted = addCard(value, next_insert)) {
                    added.push_back(std::move(inserted));
                    if (next_insert >= 0) ++next_insert;
                }
            }
            if (added.empty()) continue;
            std::vector<std::ptrdiff_t> addedIndices;
            std::vector<std::uint64_t> addedIds;
            addedIndices.reserve(added.size());
            addedIds.reserve(added.size());
            for (const auto& value : added) {
                addedIndices.push_back(indexOf(owner_, card_type_, value));
                addedIds.push_back(value ? value->id() : 0U);
            }
            std::vector<std::uint64_t> newIdState;
            for (const auto& value : cardsFrom(card_type_, owner_))
                newIdState.push_back(value ? value->id() : 0U);
            // Castの位置指定挿入は非メンバーを複製する。実際の有効
            // なメンバーを両クロージャーで共有し、やり直しごとに再接続
            // する。モデルの所有分離と同一ペインの同一性は維持する。
            auto insertedCards = std::make_shared<std::vector<CardPtr>>(added);
            auto references = std::make_shared<resourcehistory::References>(
                owner_type_ == OwnerType::Summary && summary_ ? summary_->useCounter() : nullptr);
            // DのCPUndo::udb/udaは、履歴再適
            // 用ごとに保存選択と現在選択を交換する。取り消し
            // で常に元の選択、やり直しで挿入群全体を戻すと、
            // 履歴操作の間にユーザーが変更した選択を失う。
            auto replaySelection = std::make_shared<std::vector<std::uint64_t>>(oldSelection);
            auto replayInput = std::make_shared<std::optional<std::vector<std::uint64_t>>>();
            pushUndo({[this, insertedCards, references, oldSelection, oldIdState,
                       replaySelection, replayInput] {
                          auto currentSelection = replayInput->has_value()
                              ? std::move(**replayInput) : selectionIDs();
                          replayInput->reset();
                          for (auto it = insertedCards->rbegin(); it != insertedCards->rend(); ++it) {
                              removeCard(*it);
                              references->hold(*it);
                          }
                          restoreIDState(oldIdState);
                          refreshImpl();
                          selectID(*replaySelection);
                          *replaySelection = std::move(currentSelection);
                          notifyCollectionChanged();
                      },
                      [this, insertedCards, references, addedIndices, addedIds, newIdState,
                       replaySelection, replayInput] {
                          auto currentSelection = replayInput->has_value()
                              ? std::move(**replayInput) : selectionIDs();
                          replayInput->reset();
                          for (std::size_t i = 0U; i < insertedCards->size(); ++i) {
                              auto& value = (*insertedCards)[i];
                              references->release(value.get());
                              if (value) value->id(addedIds[i]);
                              value = insert(owner_, addedIndices[i], value);
                              refCard(value, false);
                          }
                          restoreIDState(newIdState);
                          refreshImpl();
                          selectID(*replaySelection);
                          *replaySelection = std::move(currentSelection);
                          notifyCollectionChanged();
                      }, "insert", [this, replayInput] {
                          *replayInput = selectionIDs();
                      }});
            pasteRefresh(added, std::move(oldTableDisplay));
            notifyCollectionChanged();
            changed = true;
        } catch (...) {
            // addCardはモデル変更後に通知する。その
            // ため通知、確保、履歴作成の失敗時には、部分挿
            // 入を残して貼り付け未処理と報告するだけでなく
            // 、一括処理前のモデル全体へ戻す必要がある。
            const auto current = cardsFrom(card_type_, owner_);
            for (auto it = current.rbegin(); it != current.rend(); ++it)
                remove(owner_, *it);
            for (std::size_t i = 0U; i < oldCards.size(); ++i)
                insert(owner_, static_cast<std::ptrdiff_t>(i), oldCards[i]);
            restoreIDState(oldIdState);
            undo_ = oldUndo;
            undo_position_ = oldUndoPosition;
            ++undo_history_revision_;
            if (castOwner && !ownerWasChanged) castOwner->resetChanged();
            if (summary_ && !summaryWasChanged) summary_->resetChanged();
            refreshImpl();
            selectID(oldSelection);
            notifyToolbar();
            throw;
        }
    }
    return changed;
}

void CardPane::pasteRefresh(
    const std::vector<CardPtr>& values,
    std::optional<std::vector<CardPtr>> previous_table_display) {
    const bool d_list_paste = view_mode_ != CViewMode::TABLE && image_list_;
    const bool d_table_paste = view_mode_ == CViewMode::TABLE && table_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto generation = owner_binding_generation_;
    const auto mode = view_mode_;
    const auto list = image_list_;
    const auto current = [this, life, generation, mode, list] {
        return !life.expired() && owner_binding_generation_ == generation &&
            view_mode_ == mode && image_list_ == list;
    };
    // DのpasteRefreshは、どちらの表示も旧選択を消して
    // から最終行を追加・選択する。ALLと単体版で同じ規則を使う。
    deselectAll();
    if (!current()) return;
    if (d_table_paste) {
        syncCardListScale();
        if (!current()) return;
        // addCard()はrefCardEventを同期通知し、こ
        // のペインのリスナーがpasteRefresh前にTABLE行
        // を再構築・整列する。呼び出しごとの保存状態を優先し、後の貼り
        // 付けでもその整列前のDの表示順を保つ。保存状態がなければ、更
        // 新行から追加した対象を除いて重複を避ける。一致する値を入力順
        // で追加し、後の更新では未変更の所有元モデルから並べ替える。
        auto displayed = previous_table_display
            ? std::move(*previous_table_display) : cards_;
        const auto matching = cardsNarrow();
        for (const auto& value : values) {
            if (!value) continue;
            displayed.erase(std::remove_if(displayed.begin(), displayed.end(),
                [&](const CardPtr& candidate) { return sameCard(candidate, value); }),
                displayed.end());
            const auto found = std::find_if(matching.begin(), matching.end(),
                [&](const CardPtr& candidate) { return sameCard(candidate, value); });
            if (found != matching.end()) displayed.push_back(value);
        }
        // Dは末尾のvalues.size()行を選択する。表示行の総数
        // がそれ未満の場合だけ制限し、全選択インデックスを有効に保つ。
        const auto selected_count = std::min(values.size(), displayed.size());
        std::vector<std::size_t> selected;
        selected.reserve(selected_count);
        for (std::size_t row = displayed.size() - selected_count;
             row < displayed.size(); ++row)
            selected.push_back(row);
        setRows(std::move(displayed), std::move(selected));
        if (!current()) return;
        refreshStatusLine();
    } else {
        refreshImpl();
        if (d_list_paste && !current()) return;
    }
    std::vector<std::uint64_t> ids;
    for (const auto& value : values)
        if (value) ids.push_back(value->id());
    if (d_table_paste) {
        // setRowsはTABLEの末尾N行を選択済み。絞り込みで一
        // 部の貼り付け名が隠れた場合、末尾の既存行も含む点はDと同じ。
    } else if (d_list_paste) {
        const int count = list->count();
        if (count > 0) {
            const auto selected_count = std::min(values.size(), static_cast<std::size_t>(count));
            const int first = count - static_cast<int>(selected_count);
            std::vector<std::size_t> rows;
            std::vector<int> list_indices;
            rows.reserve(selected_count);
            list_indices.reserve(selected_count);
            for (int index = first; index < count; ++index) {
                rows.push_back(static_cast<std::size_t>(index));
                list_indices.push_back(index);
            }
            selection_ = std::move(rows);
            list->selectionIndices(list_indices);
            if (!current()) return;
            // Dのrefreshはカーソルを行0へ戻す。MULTI一覧のselectionIndices
            // は、そのカーソルを変えない。
            list->setCursor(0, false, false);
            if (!current()) return;
            list->scroll(count - 1);
            if (!current()) return;
            refreshStatusLine();
        } else {
            selection_.clear();
            list->selectionIndices(std::vector<int>{});
            if (!current()) return;
            list->setCursor(-1, false, false);
            if (!current()) return;
            refreshStatusLine();
        }
    } else {
        selectID(ids);
    }
    if (!current()) return;
    notifyToolbar();
}

void CardPane::doImport() {
    if (!canDoImport() || !comm_.doImport) return;
    std::vector<String> paths;
    for (const auto& card : selectedCards()) {
        if (card) paths.push_back(card->cwxPath(true));
    }
    const auto import = comm_.doImport;
    const auto toc = toc_;
    import(toc, paths);
}

bool CardPane::canDoImport() const noexcept {
    return !editMode() && selection() && static_cast<bool>(comm_.doImport);
}

bool CardPane::canReNumbering() const noexcept {
    return editMode() && !read_only_ && owner_ && static_cast<bool>(selection());
}

void CardPane::reNumbering() {
    if (!canReNumbering() || !prop_.editorProps || renumber_dialog_) return;
    if (!enterEdit()) return;
    const auto value = selection();
    const auto values = cards();
    const auto found = std::find(values.begin(), values.end(), value);
    if (found == values.end()) return;
    const auto index = static_cast<std::size_t>(found - values.begin());
    const auto minimum = index ? values[index - 1U]->id() + 1U : 1U;
    if (minimum > prop_.editorProps->looks().idMax()) return;
    auto owner = prop_.dialogOwner;
    if (!owner) owner = std::dynamic_pointer_cast<lib::Window>(parent_);
    auto dialog = std::make_shared<smalldialogs::ReNumDialog<Card>>(
        *prop_.editorProps, owner, summary_.get(), *value, minimum, comm_.services);
    renumber_dialog_ = dialog;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto source = owner_;
    const bool presented = smalldialogs::presentReNumbering<Card>(dialog,
        [this, life, source, values, index](bool accepted, std::uint64_t next) {
            if (life.expired()) return;
            auto keep = std::exchange(renumber_dialog_, {});
            if (accepted && owner_ == source && cards() == values)
                reNumbering(static_cast<std::ptrdiff_t>(index), next);
        });
    if (!presented) renumber_dialog_.reset();
}

void CardPane::reNumbering(const std::ptrdiff_t selectedIndex, const std::uint64_t newId) {
    if (!editMode() || read_only_ || selectedIndex < 0 || !newId) return;
    auto values = cards();
    const auto index = static_cast<std::size_t>(selectedIndex);
    if (index >= values.size() || !values[index]) return;
    const auto indexedCard = values[index];
    if (index && values[index - 1U]->id() >= newId) return;
    if (values.size() - index - 1U > std::numeric_limits<std::uint64_t>::max() - newId) return;
    if (!enterEdit()) return;
    // 保留中の名前編集が表を更新・並べ替える場合がある。ダイアログのイ
    // ンデックスは、確定後も同じ所有カードを指している場合だけ安全。
    values = cards();
    if (index >= values.size() || !values[index] || values[index] != indexedCard) return;
    if (index && values[index - 1U]->id() >= newId) return;
    if (values.size() - index - 1U > std::numeric_limits<std::uint64_t>::max() - newId) return;
    std::vector<std::uint64_t> oldIds;
    for (const auto& value : values) oldIds.push_back(value ? value->id() : 0U);
    auto newIds = oldIds;
    auto next = newId;
    for (auto i = index; i < values.size(); ++i) newIds[i] = next++;
    if (oldIds == newIds) return;
    const auto selectedValues = selectedCards();
    // DのCPUndo::udbはフォーカス前に選択オブジェクトを保存する
    // 。udaはその状態を復元し、再適用前の選択を次の再適用へ引き継ぐ。
    auto replaySelection = std::make_shared<std::vector<CardPtr>>(selectedValues);
    auto replayInput = std::make_shared<std::optional<std::vector<CardPtr>>>();
    const auto exchangeReplaySelection = [this, replaySelection, replayInput] {
        auto currentSelection = replayInput->has_value()
            ? std::move(**replayInput) : selectedCards();
        replayInput->reset();
        std::vector<std::uint64_t> ids;
        ids.reserve(replaySelection->size());
        for (const auto& value : *replaySelection)
            if (value) ids.push_back(value->id());
        selectID(ids);
        *replaySelection = std::move(currentSelection);
    };
    // Dは参照の衝突を避けるため、後続部分のIDを一時的に退避する。
    restoreIDState(newIds, index);
    std::vector<std::uint64_t> newSelection;
    for (const auto& value : selectedValues) if (value) newSelection.push_back(value->id());
    pushUndo({[this, oldIds, index, exchangeReplaySelection] {
                  restoreIDState(oldIds, index);
                  refreshImpl();
                  exchangeReplaySelection();
              },
              [this, newIds, index, exchangeReplaySelection] {
                  restoreIDState(newIds, index);
                  refreshImpl();
                  exchangeReplaySelection();
              }, "renumber", [this, replayInput] {
                  *replayInput = selectedCards();
              }});
    refreshImpl();
    selectID(newSelection);
}

CardDialogPtr CardPane::edit(const CardPtr& value) {
    if (!value) return {};
    if (const auto id = linkId(value); id != 0U) {
        const auto source = pOwnerCard(id);
        if (!source || source == value || linkId(source) != 0U) return {};
        // Dは手札の空のリンクラッパーではなく、Summaryの入力元を編
        // 集する。通常ホストはopenPaneを渡し、入力元一覧の所有関係
        // と履歴を維持する（cardpane.d:3708-3731）。
        if (editMode() && !read_only_ && comm_.openPane) {
            const auto open_path = comm_.openCWXPath;
            const auto open_pane = comm_.openPane;
            const auto type = card_type_;
            const auto summary = summary_;
            const auto owner = owner_;
            const auto generation = owner_binding_generation_;
            const std::weak_ptr<int> life = renumber_lifetime_;
            if (open_path) (void)open_path(source->cwxPath(true), false);
            if (life.expired() || owner_binding_generation_ != generation ||
                summary_ != summary || owner_ != owner || pOwnerCard(id) != source) return {};
            const auto pane = open_pane(type, false);
            if (life.expired() || owner_binding_generation_ != generation ||
                summary_ != summary || owner_ != owner || pOwnerCard(id) != source) return {};
            if (pane && pane.get() != this) {
                if (pane->summary_ != summary || pane->owner_type_ != OwnerType::Summary ||
                    indexOf(pane->owner_, type, source) < 0) return {};
                return pane->edit(source);
            }
        }
        // 読み取り専用の通常シナリオペインは、書き込み可能なSummary
        // ペインを経由しない。解決した入力元を、このモードのまま表示する。
        return edit(source);
    }
    if (const auto it = edit_dialogs_.find(value.get()); it != edit_dialogs_.end()) {
        const auto dialog = it->second;
        if (dialog) dialog->active();
        return dialog;
    }
    if (!canEdit() && !comm_.makeDialog) return {};
    CardDialogPtr dialog;
    // 取り込み元のペインは読み取り専用だが、Dはプロパティ画面を
    // 開く。適用・OKで元シナリオを変更できないよう、ダイアロ
    // グ自身へそのモードを渡す。editMode()はToCペ
    // インの区別だけで、読み取り専用状態の代わりにはならない。
    const auto dialog_owner = owner_type_ == OwnerType::Cast && summary_ &&
        indexOf(std::static_pointer_cast<CWXPath>(summary_), card_type_, value) >= 0
            ? std::static_pointer_cast<CWXPath>(summary_) : owner_;
    const auto make = comm_.makeDialog;
    const auto summary = summary_;
    const auto owner = owner_;
    const auto type = card_type_;
    const auto generation = owner_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto current = [this, life, summary, owner, generation] {
        return !life.expired() && owner_binding_generation_ == generation &&
            summary_ == summary && owner_ == owner;
    };
    if (make) dialog = make(type, summary, dialog_owner, value, read_only_ || !editMode());
    if (!current() || !dialog || indexOf(dialog_owner, type, value) < 0) return {};
    edit_dialogs_[value.get()] = dialog;
    if (editMode()) {
        const auto pending_before = std::make_shared<CardPtr>();
        dialog->applying = [current, value, pending_before] {
            *pending_before = current() ? snapshotCardState(value) : CardPtr{};
        };
        dialog->applied = [this, current, value, pending_before] {
            if (!current()) return;
            const auto before = std::exchange(*pending_before, CardPtr{});
            const auto after = snapshotCardState(value);
            if (before && after && !sameCardState(before, after)) {
                const auto state = std::make_shared<CardPtr>(before);
                const auto exchangeProperties = [value, state] {
                    auto current = snapshotCardState(value);
                    if (cardpropertytransaction::restoreProperties(value, *state))
                        *state = std::move(current);
                };
                // 取り消し・やり直しは、プロパティの書き込み対象だけを交換する
                // 。対象の名前変更に追従する一つの管理済み履歴状態を保持する。
                pushUndo({exchangeProperties, exchangeProperties, "edit"});
            }
            if (!current()) return;
            refresh(value);
            if (!current()) return;
            refCard(value);
            if (current()) notifyToolbar();
        };
    }
    dialog->closed = [this, current, key = value.get()] {
        if (current()) edit_dialogs_.erase(key);
    };
    dialog->open();
    return dialog;
}

void CardPane::edit() {
    (void)edit(std::ptrdiff_t{-1});
}

CardDialogPtr CardPane::edit(const std::ptrdiff_t index) {
    if (index >= 0) {
        const auto position = static_cast<std::size_t>(index);
        return position < cards_.size() ? edit(cards_[position]) : CardDialogPtr{};
    }
    CardDialogPtr result;
    const auto values = selectedCards();
    const auto summary = summary_;
    const auto owner = owner_;
    const auto generation = open_cohort_generation_;
    const auto stop = comm_.stopOpen;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto current = [this, life, summary, owner, generation] {
        return !life.expired() && open_cohort_generation_ == generation &&
            summary_ == summary && owner_ == owner;
    };
    for (const auto& value : values) {
        if (!current()) break;
        const bool stopped = stop && stop();
        if (!current() || stopped || indexOf(owner, card_type_, value) < 0) break;
        result = edit(value);
    }
    return result;
}

void CardPane::editUseEvent(const bool canDuplicate) {
    if (const auto value = selection()) editUseEvent(value, canDuplicate);
}

void CardPane::editUseEvent(const CardPtr& value, const bool canDuplicate) {
    if (!value || !canEditUseEvent()) return;
    const auto effect = std::dynamic_pointer_cast<EffectCard>(value);
    if (!effect || !comm_.openUseEvents) return;
    auto base = effect;
    if (const auto id = linkId(value); id != 0U) {
        if (const auto source = pOwnerCard(id)) base = std::dynamic_pointer_cast<EffectCard>(source);
    }
    const auto open = comm_.openUseEvents;
    const auto summary = summary_;
    if (base && open) open(summary, base, canDuplicate);
}

void CardPane::editHand() {
    // DのeditHand()は編集可能なシナリオの経路。読み取り専用のImportSource
    // メニューはopen_hand_を直接呼ぶため、直接呼び出しでその経路を代行しない。
    if (read_only_ || !canEditHand() || !enterEdit()) return;
    const auto values = selectedCards();
    const auto summary = summary_;
    const auto owner = owner_;
    const auto generation = open_cohort_generation_;
    const auto stop = comm_.stopOpen;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto current = [this, life, summary, owner, generation] {
        return !life.expired() && open_cohort_generation_ == generation &&
            summary_ == summary && owner_ == owner;
    };
    for (const auto& value : values) {
        if (!current()) break;
        const bool stopped = stop && stop();
        if (!current() || stopped || indexOf(owner, card_type_, value) < 0) break;
        const auto cast = std::dynamic_pointer_cast<CastCard>(value);
        if (!cast) continue;
        editHandWith(cast);
    }
}

void CardPane::editHandWith(const CastCardPtr& value) {
    if (!value) return;
    const auto open = open_hand_;
    if (open) {
        open(value);
        return;
    }
    const auto summary = summary_;
    const auto owner = owner_;
    if (!comm_.openHands || !enterEdit()) return;
    if (summary_ != summary || owner_ != owner || read_only_ || !editMode() ||
        open_hand_ || indexOf(owner_, card_type_, value) < 0) return;
    const auto openHands = comm_.openHands;
    if (openHands) openHands(summary, value, true);
}

bool CardPane::canEdit() const noexcept {
    // Dは読み取り専用の取り込み元からもプロパティ画面を許可する
    // 。変更コマンドは各書き込み境界でread_only_によ
    // り保護し、上記makeDialogにも同じフラグを渡す。
    const auto value = selection();
    return value && (linkId(value) == 0U || static_cast<bool>(pOwnerCard(linkId(value))));
}

bool CardPane::canEditUseEvent() const noexcept {
    return canEdit() && (card_type_ == CardType::Skill || card_type_ == CardType::Item || card_type_ == CardType::Beast) &&
           static_cast<bool>(comm_.openUseEvents);
}

bool CardPane::canEditHand() const noexcept {
    return canEdit() && card_type_ == CardType::Cast &&
           (static_cast<bool>(open_hand_) || static_cast<bool>(comm_.openHands));
}

bool CardPane::canWriteComment() const noexcept {
    // Dのコメントはリンクカードも含む全選択カードに適用する。selection()
    // はDに従い、表示中の最初の選択カードを返す。
    return editMode() && !read_only_ && !selection_.empty();
}

void CardPane::writeComment() {
    // DのwriteComment()はモードレスのコメント画面を開く前に編集
    // モードへ入り、取り付けられた名前・数値の編集部をコマンド境界で確定する。
    if (!canWriteComment() || !enterEdit()) return;
    const auto values = selectedCards();
    const auto owner = owner_;
    const auto generation = owner_binding_generation_;
    const auto stop = comm_.stopOpen;
    const std::weak_ptr<int> life = renumber_lifetime_;
    for (const auto& value : values) {
        if (life.expired() || owner_binding_generation_ != generation) break;
        const bool stopped = stop && stop();
        if (life.expired() || owner_binding_generation_ != generation || stopped ||
            indexOf(owner, card_type_, value) < 0) break;
        writeCommentImpl(value);
    }
}

void CardPane::writeCommentImpl(const CardPtr& value) {
    if (!value || read_only_ || !comm_.makeCommentDialog) return;
    const auto key = value.get();
    if (const auto found = comment_dialogs_.find(key); found != comment_dialogs_.end()) {
        const auto dialog = found->second;
        if (dialog) dialog->active();
        return;
    }
    const auto make = comm_.makeCommentDialog;
    const auto summary = summary_;
    const auto owner = owner_;
    const auto type = card_type_;
    const auto generation = owner_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto current = [this, life, generation, owner, type, value] {
        return !life.expired() && owner_binding_generation_ == generation &&
            indexOf(owner, type, value) >= 0;
    };
    auto dialog = make(summary, value, value->comment());
    if (!current() || !dialog) return;
    comment_dialogs_[key] = dialog;
    dialog->applied = [this, current, value](const StringView next) {
        if (!current() || read_only_ || value->comment() == next) return;
        const auto old = value->comment();
        const String committed(next);
        pushUndo({[value, old] { value->comment(old); },
                  [value, committed] { value->comment(committed); }, "comment"});
        if (!current()) return;
        value->comment(committed);
        refCard(value);
    };
    // Dはダイアログを閉じる前にカードが削除されていても登録項目を
    // 外す。世代とダイアログの同一性を保持し、古い終了通知が同じカ
    // ードポインターに登録された新しいものを消さないようにする。
    const std::weak_ptr<comment::CommentDialog> weak_dialog = dialog;
    dialog->closed = [this, life, generation, key, weak_dialog] {
        if (life.expired() || owner_binding_generation_ != generation) return;
        const auto found = comment_dialogs_.find(key);
        const auto closed_dialog = weak_dialog.lock();
        if (closed_dialog && found != comment_dialogs_.end() &&
            found->second == closed_dialog)
            comment_dialogs_.erase(found);
    };
    (void)dialog->open();
}

void CardPane::hold(const bool checked) {
    if (!editMode() || !canHold() || selectedCards().empty()) return;
    const auto values = selectedCards();
    const auto generation = owner_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto current = [this, life, generation] {
        return !life.expired() && owner_binding_generation_ == generation;
    };
    std::vector<std::pair<CardPtr, bool>> changes;
    for (const auto& value : values) {
        bool old = false;
        if (const auto skill = std::dynamic_pointer_cast<SkillCard>(value)) old = skill->hold();
        else if (const auto item = std::dynamic_pointer_cast<ItemCard>(value)) old = item->hold();
        else continue;
        if (old != checked) changes.emplace_back(value, old);
    }
    if (changes.empty()) return;
    pushUndo({[changes] {
                  for (const auto& [value, old] : changes) {
                      if (const auto skill = std::dynamic_pointer_cast<SkillCard>(value)) skill->hold(old);
                      else if (const auto item = std::dynamic_pointer_cast<ItemCard>(value)) item->hold(old);
                  }
              },
              [changes, checked] {
                  for (const auto& [value, old] : changes) {
                      (void)old;
                      if (const auto skill = std::dynamic_pointer_cast<SkillCard>(value)) skill->hold(checked);
                      else if (const auto item = std::dynamic_pointer_cast<ItemCard>(value)) item->hold(checked);
                  }
              }, "hold"});
    if (!current()) return;
    for (const auto& [value, old] : changes) {
        (void)old;
        if (const auto skill = std::dynamic_pointer_cast<SkillCard>(value)) skill->hold(checked);
        else if (const auto item = std::dynamic_pointer_cast<ItemCard>(value)) item->hold(checked);
    }
    for (const auto& [value, old] : changes) {
        (void)old;
        if (!current()) return;
        refCard(value, false);
    }
    if (current()) refreshStatusLine();
}

bool CardPane::canRemoveRef() const noexcept {
    if (!editMode() || read_only_ || owner_type_ != OwnerType::Cast) return false;
    for (const auto& value : selectedCards())
        if (linkId(value) != 0U && pOwnerCard(linkId(value))) return true;
    return false;
}

void CardPane::removeRef() {
    if (!canRemoveRef()) return;
    const auto generation = owner_binding_generation_;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto current = [this, life, generation] {
        return !life.expired() && owner_binding_generation_ == generation;
    };
    // Dは、リンク表示を独立した深いコピーへ置き換える
    // 前に、一つの編集可能なカード変更として記録する。
    if (!enterEdit()) return;
    const auto deepCopy = [](const CardPtr& destination, const CardPtr& source) {
        if (const auto destination_skill = std::dynamic_pointer_cast<SkillCard>(destination)) {
            if (const auto source_skill = std::dynamic_pointer_cast<SkillCard>(source)) {
                destination_skill->deepCopy(*source_skill);
                return true;
            }
        } else if (const auto destination_item = std::dynamic_pointer_cast<ItemCard>(destination)) {
            if (const auto source_item = std::dynamic_pointer_cast<ItemCard>(source)) {
                destination_item->deepCopy(*source_item);
                return true;
            }
        } else if (const auto destination_beast = std::dynamic_pointer_cast<BeastCard>(destination)) {
            if (const auto source_beast = std::dynamic_pointer_cast<BeastCard>(source)) {
                destination_beast->deepCopy(*source_beast);
                return true;
            }
        }
        return false;
    };
    const auto shallowCopy = [](const CardPtr& destination, const CardPtr& source) {
        if (const auto destination_skill = std::dynamic_pointer_cast<SkillCard>(destination)) {
            if (const auto source_skill = std::dynamic_pointer_cast<SkillCard>(source)) {
                destination_skill->shallowCopy(*source_skill);
                return true;
            }
        } else if (const auto destination_item = std::dynamic_pointer_cast<ItemCard>(destination)) {
            if (const auto source_item = std::dynamic_pointer_cast<ItemCard>(source)) {
                destination_item->shallowCopy(*source_item);
                return true;
            }
        } else if (const auto destination_beast = std::dynamic_pointer_cast<BeastCard>(destination)) {
            if (const auto source_beast = std::dynamic_pointer_cast<BeastCard>(source)) {
                destination_beast->shallowCopy(*source_beast);
                return true;
            }
        }
        return false;
    };
    struct Change { CardPtr value; CardPtr source; CardPtr before; CardPtr after; std::uint64_t id{}; bool hold{}; };
    std::vector<Change> changes;
    for (const auto& value : selectedCards()) {
        const auto link = linkId(value);
        const auto base = link ? pOwnerCard(link) : CardPtr{};
        if (!base) continue;
        if (const auto before = value->dup()) {
            bool hold{};
            if (const auto skill = std::dynamic_pointer_cast<SkillCard>(value)) hold = skill->hold();
            else if (const auto item = std::dynamic_pointer_cast<ItemCard>(value)) hold = item->hold();
            changes.push_back({value, base, before, {}, value->id(), hold});
        }
    }
    if (changes.empty()) return;
    const auto unlink = [deepCopy](const std::vector<Change>& values) {
        for (const auto& change : values) {
            if (!change.source || !deepCopy(change.value, change.source)) continue;
            change.value->id(change.id);
            if (const auto effect = std::dynamic_pointer_cast<EffectCard>(change.value)) effect->linkId(0U);
            if (const auto skill = std::dynamic_pointer_cast<SkillCard>(change.value)) skill->hold(change.hold);
            else if (const auto item = std::dynamic_pointer_cast<ItemCard>(change.value)) item->hold(change.hold);
        }
    };
    unlink(changes);
    // やり直しは最初のリンク解除の結果を復元する。取り消し中に
    // 別のイベント画面が編集した入力元を再取得しない。切り離し
    // た深いコピーで、イベントツリーも両カードから独立させる。
    for (auto& change : changes) {
        change.after = change.value->dup();
        change.source.reset();
    }
    // DのCardPane::UndoEditはshallowCopy
    // でカード項目を戻す。やり直しごとに深いコピーを追加せず、最
    // 初のリンク解除で生成したイベントツリーをそのまま維持する。
    const auto replay = [shallowCopy](const std::vector<Change>& values) {
        for (const auto& change : values) {
            if (change.after) (void)shallowCopy(change.value, change.after);
        }
    };
    const auto restore = [shallowCopy](const std::vector<Change>& values) {
        for (const auto& change : values) {
            if (change.before) (void)shallowCopy(change.value, change.before);
        }
    };
    pushUndo({[this, restore, changes] { restore(changes); refreshImpl(); },
              [this, replay, changes] { replay(changes); refreshImpl(); }, "unlink"});
    for (const auto& change : changes) {
        if (!current()) return;
        refCard(change.value);
    }
    if (!current()) return;
    refreshImpl();
    if (current()) notifyToolbar();
}

bool CardPane::canSelectConnectedResource() const noexcept {
    if (!canEdit()) return false;
    const auto value = selection();
    if (!value || !summary_) return false;
    const auto base = linkId(value) ? pOwnerCard(linkId(value)) : value;
    return base && !base->connectedFile().empty() && summary_->hasMaterial(
        base->connectedFile(), prop_.coreVars ? prop_.coreVars->ignorePaths.value
                                             : std::vector<String>{});
}

void CardPane::selectConnectedResource() {
    if (!canSelectConnectedResource() || !comm_.openFilePath) return;
    const auto value = selection();
    const auto base = linkId(value) ? pOwnerCard(linkId(value)) : value;
    if (base) comm_.openFilePath(base->connectedFile(), false, true);
}

void CardPane::moveCards(const std::vector<CardPtr>& values, const std::ptrdiff_t index,
                         const bool store) {
    if (!owner_ || values.empty() || read_only_) return;
    std::vector<std::uint64_t> oldIds;
    for (const auto& value : cardsFrom(card_type_, owner_))
        oldIds.push_back(value ? value->id() : 0U);
    const auto oldSelection = selectionIDs();
    auto movedValues = values;
    std::vector<std::ptrdiff_t> from;
    from.reserve(movedValues.size());
    for (const auto& value : movedValues) {
        const auto current = indexOf(owner_, card_type_, value);
        if (current >= 0) from.push_back(current);
    }
    if (from.empty()) return;
    std::sort(from.begin(), from.end());
    std::sort(movedValues.begin(), movedValues.end(), [](const CardPtr& left, const CardPtr& right) {
        return left && right ? left->id() < right->id() : static_cast<bool>(left);
    });
    moveCards(owner_, card_type_, movedValues, index);
    std::vector<std::uint64_t> newIds;
    for (const auto& value : cardsFrom(card_type_, owner_))
        newIds.push_back(value ? value->id() : 0U);
    if (store) {
        // DのCPUndo::udaは再適用ごとに保存済み選択を交換す
        // る。移動時の固定選択を、両方向で常に復元するものではない。
        auto replaySelection = std::make_shared<std::vector<std::uint64_t>>(oldSelection);
        auto replayInput = std::make_shared<std::optional<std::vector<std::uint64_t>>>();
        const auto exchangeReplaySelection = [this, replaySelection, replayInput] {
            auto currentSelection = replayInput->has_value()
                ? std::move(**replayInput) : selectionIDs();
            replayInput->reset();
            selectID(*replaySelection);
            *replaySelection = std::move(currentSelection);
        };
        pushUndo({[this, from, index, oldIds, exchangeReplaySelection] {
                      const auto current = cards();
                      auto toIndex = index;
                      for (const auto sourceIndex : from) {
                          if (sourceIndex < index) --toIndex;
                          else break;
                      }
                      std::vector<CardPtr> moved;
                      for (std::size_t offset = 0U; offset < from.size(); ++offset) {
                          const auto currentIndex = toIndex + static_cast<std::ptrdiff_t>(offset);
                          if (currentIndex >= 0 && static_cast<std::size_t>(currentIndex) < current.size())
                              moved.push_back(current[static_cast<std::size_t>(currentIndex)]);
                      }
                      for (std::size_t offset = 0U; offset < moved.size(); ++offset) {
                          auto restoreIndex = from[offset];
                          if (index < restoreIndex) {
                              restoreIndex += static_cast<std::ptrdiff_t>(from.size());
                              restoreIndex -= static_cast<std::ptrdiff_t>(offset);
                          }
                          insert(owner_, restoreIndex, moved[offset]);
                      }
                      restoreIDState(oldIds);
                      refreshImpl();
                      exchangeReplaySelection();
                      if (comm_.refOwner) comm_.refOwner();
                  },
                  [this, from, index, newIds, exchangeReplaySelection] {
                      const auto current = cards();
                      std::vector<CardPtr> moved;
                      moved.reserve(from.size());
                      for (const auto sourceIndex : from)
                          if (sourceIndex >= 0 && static_cast<std::size_t>(sourceIndex) < current.size())
                              moved.push_back(current[static_cast<std::size_t>(sourceIndex)]);
                      moveCards(owner_, card_type_, moved, index);
                      restoreIDState(newIds);
                      refreshImpl();
                      exchangeReplaySelection();
                      if (comm_.refOwner) comm_.refOwner();
                  }, "move", [this, replayInput] {
                      *replayInput = selectionIDs();
                  }});
    }
    refreshImpl();
    selectMovedCards(movedValues);
    if (comm_.refOwner) comm_.refOwner();
}

void CardPane::selectMovedCards(const std::vector<CardPtr>& values) {
    selectMovedCardsImpl(values, true);
}

void CardPane::selectMovedCardsImpl(const std::vector<CardPtr>& values,
                                    const bool revealLast) {
    std::vector<std::uint64_t> ids;
    CardPtr last_moved;
    for (const auto& value : values) {
        if (!value) continue;
        ids.push_back(value->id());
        last_moved = value;
    }
    selectID(ids);
    if (!revealLast || !last_moved) return;
    const auto found = std::find(cards_.begin(), cards_.end(), last_moved);
    if (found == cards_.end()) return;
    const auto index = static_cast<std::size_t>(std::distance(cards_.begin(), found));
    // DのselectMovedCardsは引数を並べ替えずadds[$
    // - 1]を使う。通常のmoveCardsは呼び出し前に並べ替えるが、
    // ここでは渡された順を維持し、他の呼び出し側のアクティブな終点を保つ。
    if (view_mode_ == CViewMode::TABLE) {
        if (table_) (void)lib::reveal_table_row(*table_, index);
    } else if (image_list_ &&
               index <= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        image_list_->setCursor(static_cast<int>(index), true, false);
    }
}

void CardPane::moveCards(const CWXPathPtr& owner, const CardType type,
                         const std::vector<CardPtr>& values, std::ptrdiff_t index) {
    if (!owner || values.empty()) return;
    for (const auto& value : values) {
        if (!value || indexOf(owner, type, value) < 0) continue;
        const auto count = static_cast<std::ptrdiff_t>(cardsFrom(type, owner).size());
        insert(owner, std::clamp(index, std::ptrdiff_t{0}, count), value);
        index = indexOf(owner, type, value) + 1;
    }
}

void CardPane::refreshLink(CardPtr& value, const bool samePane, const bool sameSummary,
                           const bool topLevel) {
    if (!value || !editMode()) return;
    const auto effect = std::dynamic_pointer_cast<EffectCard>(value);
    if (!effect) return;
    const auto linked = effect->linkId();
    if (sameSummary && owner_type_ == OwnerType::Summary && linked != 0U) {
        if (const auto base = pOwnerCard(linked)) {
            if (const auto copy = base->dup()) value = copy;
        } else {
            value = makeCard(card_type_, 1U);
            if (const auto replacement = std::dynamic_pointer_cast<EffectCard>(value))
                replacement->linkId(0U);
        }
        return;
    }
    // Dの最上位カードから手札へのコピーは、入力カードが別のlinkId
    // を持っていても入力カード自身のIDを参照する。Dと同様
    // にリンク先が変わったり、未解決参照が残ったりし得る。
    if (sameSummary && owner_type_ == OwnerType::Cast && topLevel &&
        ((prop_.coreVars ? prop_.coreVars->linkCard.value : prop_.linkCard) ||
         !summary_ || !summary_->legacy())) {
        const auto id = value->id();
        auto linkedCard = makeCard(card_type_, 1U);
        if (const auto replacement = std::dynamic_pointer_cast<EffectCard>(linkedCard)) {
            replacement->linkId(id);
            value = std::move(linkedCard);
        }
        return;
    }
    // 同一シナリオ内の未解決の手札リンクは、有効な警告状態。Dはそ
    // のまま残し、Summaryへの実体化の分岐だけが置き換える。
}

void CardPane::insert(const CardPtr& value, const bool move) {
    if (!value || !editMode() || read_only_) return;
    if (move && indexOf(owner_, card_type_, value) >= 0) {
        selectID(value->id());
        return;
    }
    auto candidate = value;
    if (!move) candidate = duplicateCard(value);
    if (candidate && addCard(candidate)) {
        refreshImpl();
        selectID(candidate->id());
    }
}

void CardPane::refCard(const CardPtr& value, const bool updateStatus) {
    if (value) {
        if (const auto found = comment_dialogs_.find(value.get());
            found != comment_dialogs_.end() && found->second && comm_.commentTitle)
            found->second->title = comm_.commentTitle(value);
    }
    refCard(this, comm_, value, updateStatus);
}

void CardPane::refCard(CardPane* view, Commons& comm, const CardPtr& value,
                       const bool updateStatus) {
    const auto notify = comm.refCard;
    const auto event = comm.refCardEvent;
    const auto generation = view ? view->owner_binding_generation_ : 0U;
    const std::weak_ptr<int> life = view ? view->renumber_lifetime_ : std::weak_ptr<int>{};
    const auto current = [view, life, generation] {
        return !view || (!life.expired() && view->owner_binding_generation_ == generation);
    };
    if (notify) notify(value);
    if (!current()) return;
    event.call(value);
    if (!current()) return;
    if (view) {
        view->refresh(value, updateStatus);
        if (current() && updateStatus) view->notifyToolbar();
    }
}

void CardPane::delCard(const CardPtr& value) {
    if (value) {
        if (const auto found = comment_dialogs_.find(value.get());
            found != comment_dialogs_.end() && found->second)
            found->second->forceCancel();
    }
    delCard(this, comm_, owner_, value);
}

void CardPane::delCard(CardPane* view, Commons& comm, const CWXPathPtr& owner,
                       const CardPtr& value) {
    delImpl(view, comm, owner, value);
}

void CardPane::delImpl(CardPane* view, Commons& comm, const CWXPathPtr& owner,
                       const CardPtr& value) {
    // Dはメンバーを切り離した後も、包含する所有元を渡す。
    if (comm.delCard) comm.delCard(owner, value);
    comm.delCardEvent.call(value);
    if (view) {
        view->refreshImpl();
        view->notifyToolbar();
    }
}

void CardPane::toNode(XNode& parent, const std::vector<CardPtr>& values,
                      const bool del) const {
    if (values.empty()) return;
    if (!del) {
        parent.newAttr("summId", ownerId());
        parent.newAttr("paneId", id_);
        const auto source_parent = values.front() ? values.front()->cwxParent().lock()
                                                  : CWXPathPtr{};
        parent.newAttr("topLevel", summary_ && source_parent.get() == summary_.get());
    }
    parent.newAttr("scenarioPath", utils::nabs(StringView{ownerScenarioPath()}));
    parent.newAttr("scenarioName", ownerScenarioName());
    parent.newAttr("scenarioAuthor", ownerScenarioAuthor());
    if (del) parent.newAttr("delete", true);
    // クリップボードの表現を手作業で組み立てない。画像パス、効果内
    // 容・イベント、フラグ、カード種別ごとのバージョン依存項目はモ
    // デルの直列化処理が管理する。DはここでSystemを渡すが、
    // クリップボードのコピーではincludeCard=false
    // なのでカード解決は不要。通常コピーは入力元Summaryの保
    // 存バージョンに関係なく、各カードを最新形式として出力する。
    const card::XMLOption option(nullptr, String(versioninfo::LATEST_VERSION));
    for (const auto& value : values) {
        if (!value) continue;
        static_cast<void>(value->toNode(parent, option));
    }
}

String CardPane::toXML(const std::vector<CardPtr>& values, const bool del) const {
    auto document = XNode::create(xmlNameM());
    toNode(document, values, del);
    return document.text();
}

CardPane::MaterialCopyPlan::~MaterialCopyPlan() { detach(); }

void CardPane::MaterialCopyPlan::detach() noexcept {
    if (!counter) return;
    const auto current = std::exchange(counter, {});
    for (const auto& value : values) {
        try {
            if (value && value->useCounter() == current)
                value->removeUseCounter();
        } catch (...) {
            // 後処理の失敗で、完了済みのカードドロップをエラーにしない。
        }
    }
}

std::shared_ptr<CardPane::MaterialCopyPlan> CardPane::prepareMaterialCopyPlan(
    const XNode& node, std::vector<CardPtr> values) const {
    if (values.empty() || !summary_) return {};
    const String source_path = node.attribute("scenarioPath").value_or(String{});
    const String source_name = node.attribute("scenarioName").value_or(String{});
    const String source_author = node.attribute("scenarioAuthor").value_or(String{});
    const String target_path = summary_->scenarioPath();
    utils::debugln("[CARD_COPY_META] sourcePath=", source_path,
                   " sourceName=", source_name,
                   " sourceAuthor=", source_author,
                   " targetPath=", target_path,
                   " cardCount=", values.size());

    // 未保存の出力先はまだファイルを公開できないが、
    // カードの受理は有効。次の通常保存で参照を扱う。
    if (source_path.empty() || target_path.empty()) return {};

    const auto scenario_root = [](const String& value) {
        auto result = utils::nabs(structs::pathFromUtf8(value)).lexically_normal();
        std::error_code error;
        if (std::filesystem::is_regular_file(result, error))
            result = result.parent_path();
        return result;
    };
    const auto source_root = scenario_root(source_path);
    const auto target_root = scenario_root(target_path);
    utils::debugln("[CARD_COPY_ROOT] source=", structs::pathToUtf8(source_root),
                   " target=", structs::pathToUtf8(target_root));
    if (comm_.services.filesystem
            ? comm_.services.filesystem->filenames_equal(source_root, target_root)
            : source_root == target_root)
        return {};

    // Dは貼り付けた全カードを一時UseCounterへ接続する。PathId
    // により画像、音声、イベントの子孫、今後のPathUser項目も対象となる
    // 。Card::paths()だけをたどると、入れ子の素材参照を見落とす。
    auto plan = std::make_shared<MaterialCopyPlan>();
    plan->values = std::move(values);
    plan->counter = std::make_shared<MaterialCopyPlan::Counter>(
        static_cast<CWXPath*>(nullptr));
    plan->source_name = source_name;
    plan->source_author = source_author;
    plan->target_root = target_root;
    try {
        for (const auto& value : plan->values)
            if (value) value->setUseCounter(plan->counter, nullptr);
    } catch (const std::exception& error) {
        utils::debugln("[CARD_COPY_ERROR] counter attach: ", error.what());
        return {};
    } catch (...) {
        utils::debugln("[CARD_COPY_ERROR] counter attach: unknown exception");
        return {};
    }

    for (const auto& id : plan->counter->keys<MaterialCopyPlan::PathId>()) {
        const String old_path = static_cast<String>(id);
        if (old_path.empty()) continue;
        if (id.isBinData()) {
            if (utils::isBinImg(old_path)) {
                plan->candidates.push_back({id, old_path, {}, true});
                utils::debugln("[CARD_COPY_PATH] binary-image candidate bytes=",
                               old_path.size());
            } else {
                // 埋め込み音声はファイル素材のコピー候補では
                // ない。旧形式のカードには付けたままにし、他
                // の出力先では通常の音声直列化処理で扱う。
                utils::debugln("[CARD_COPY_PATH] binary non-image skipped");
            }
            continue;
        }

        auto relative = structs::pathFromUtf8(old_path).lexically_normal();
        bool invalid_relative = relative.empty() || relative.is_absolute();
        for (const auto& part : relative) {
            if (part == "..") {
                invalid_relative = true;
                break;
            }
        }
        if (invalid_relative) {
            utils::debugln("[CARD_COPY_PATH] invalid relative path=", old_path);
            continue;
        }
        const auto source = (source_root / relative).lexically_normal();
        std::error_code error;
        if (!std::filesystem::is_regular_file(source, error)) {
            utils::debugln("[CARD_COPY_PATH] missing or non-file path=",
                           structs::pathToUtf8(source));
            continue;
        }
        plan->candidates.push_back({id, old_path, source, false});
        plan->normal_source_names.push_back(structs::pathToUtf8(source));
    }
    for (const auto& candidate : plan->candidates)
        if (candidate.binary_image) ++plan->binary_count;
    utils::debugln("[CARD_COPY_PATH_COUNT] candidates=", plan->candidates.size(),
                   " normal=", plan->normal_source_names.size(),
                   " binaryImage=", plan->binary_count);
    if (plan->candidates.empty()) return {};
    return plan;
}

MaterialCopyResult CardPane::applyMaterialCopy(
    const std::shared_ptr<MaterialCopyPlan>& plan,
    const bool externalizeBinary, const bool copyReferences,
    const bool showWarning) const {
    if (!plan || !summary_) return MaterialCopyResult::ContinuedWithoutCopy;
    const bool target_legacy = summary_->legacy();
    String material_path = target_legacy ? String{} : String("Material");
    if (comm_.materialPath) {
        try {
            material_path = comm_.materialPath(summary_);
        } catch (const std::exception& error) {
            utils::debugln("[CARD_COPY_ERROR] material path callback: ", error.what());
        } catch (...) {
            utils::debugln("[CARD_COPY_ERROR] material path callback: unknown exception");
        }
    }
    const auto material = structs::pathFromUtf8(material_path);
    utils::debugln("[CARD_COPY_DEST] root=", structs::pathToUtf8(plan->target_root),
                   " material=", material_path,
                   " externalizeBinary=", externalizeBinary,
                   " copyReferences=", copyReferences);

    bool failed = false;
    bool copied_any = false;
    for (const auto& candidate : plan->candidates) {
        if (candidate.binary_image && !externalizeBinary) continue;
        if (!candidate.binary_image && !copyReferences) continue;

        try {
            String exported_name;
            if (candidate.binary_image && externalizeBinary && prop_.coreProps) {
                auto users = plan->counter->values(candidate.id);
                std::sort(users.begin(), users.end(), [](const auto& left, const auto& right) {
                    if (!left) return false;
                    if (!right) return true;
                    // Dは共有画像の命名用の所有元を、数値のCWXパス順で選ぶ。
                    return ::cwxeditor::cwx::path::cpcmp(
                        ::cwxeditor::cwx::path::cpsplit(left->cwxPath(true)),
                        ::cwxeditor::cwx::path::cpsplit(right->cwxPath(true))) < 0;
                });
                if (!users.empty() && users.front()) {
                    exported_name = summary_model::pathUserToExportedImageName(
                        *prop_.coreProps, plan->source_name, plan->source_author,
                        *users.front());
                }
            }
            const auto source = candidate.binary_image
                ? utils::Path(candidate.value) : candidate.source;
            const auto copied = utils::copyTo(
                plan->target_root, source, material, candidate.binary_image, exported_name);
            if (copied.empty()) {
                failed = true;
                utils::debugln("[CARD_COPY_ERROR] copy failed path=", candidate.value);
                continue;
            }
            const auto new_path = structs::pathToUtf8(copied);
            if (candidate.value != new_path)
                plan->counter->change(candidate.id,
                                      ::cwxeditor::cwx::usecounter::toPathId(new_path));
            copied_any = true;
            utils::debugln("[CARD_COPY_REWRITE] ", candidate.value, " -> ", new_path);
        } catch (const std::exception& error) {
            failed = true;
            utils::debugln("[CARD_COPY_ERROR] path=", candidate.value,
                           " message=", error.what());
        } catch (...) {
            failed = true;
            utils::debugln("[CARD_COPY_ERROR] path=", candidate.value,
                           " unknown exception");
        }
    }

    if (!target_legacy && !externalizeBinary) {
        // WSNの出力先は画像のバイナリ内容を保持できない。
        // ユーザーが外部ファイル化しなかった画像は、Dの規則
        // に合わせ、カード受理後に意図的にモデルから除く。
        for (const auto& id : plan->counter->keys<MaterialCopyPlan::PathId>()) {
            if (!id.isBinData() || !utils::isBinImg(static_cast<String>(id))) continue;
            try {
                plan->counter->change(id,
                                      ::cwxeditor::cwx::usecounter::toPathId(""), true);
            } catch (const std::exception& error) {
                failed = true;
                utils::debugln("[CARD_COPY_ERROR] binary cleanup: ", error.what());
            } catch (...) {
                failed = true;
                utils::debugln("[CARD_COPY_ERROR] binary cleanup: unknown exception");
            }
        }
    }

    plan->detach();
    if (failed && showWarning && prop_.coreProps && comm_.services.dialogs) {
        lib::MessageDialogRequest warning;
        warning.title = prop_.coreProps->requiredMsgs().dlgTitWarning.value;
        warning.message = prop_.coreProps->requiredMsgs().dlgMsgCopyError.value;
        warning.severity = lib::MessageSeverity::Warning;
        warning.buttons = lib::MessageButtons::OK;
        try {
            (void)comm_.services.dialogs->message(warning);
        } catch (...) {
            // 警告ダイアログの失敗で、受理済みのカードを拒否しない。
        }
    }
    if (copied_any && comm_.refPaths) {
        try {
            comm_.refPaths(material_path);
        } catch (const std::exception& error) {
            utils::debugln("[CARD_COPY_ERROR] ref paths callback: ", error.what());
        } catch (...) {
            utils::debugln("[CARD_COPY_ERROR] ref paths callback: unknown exception");
        }
    }
    const auto result = failed ? MaterialCopyResult::CopiedWithErrors
                               : (copied_any ? MaterialCopyResult::Copied
                                              : MaterialCopyResult::ContinuedWithoutCopy);
    utils::debugln("[CARD_COPY_OUTCOME] result=", static_cast<int>(result),
                   " copied=", copied_any, " failed=", failed);
    return result;
}

void CardPane::showMaterialCopyWarningAsync() const {
    if (!prop_.coreProps || !comm_.services.dialogs) return;
    lib::MessageDialogRequest warning;
    warning.title = prop_.coreProps->requiredMsgs().dlgTitWarning.value;
    warning.message = prop_.coreProps->requiredMsgs().dlgMsgCopyError.value;
    warning.severity = lib::MessageSeverity::Warning;
    warning.buttons = lib::MessageButtons::OK;
    try {
        if (!comm_.services.dialogs->message_async(warning, [](const lib::MessageResult) {}))
            utils::debugln("[CARD_COPY_DIALOG] async warning unavailable");
    } catch (const std::exception& error) {
        utils::debugln("[CARD_COPY_DIALOG] async warning exception: ", error.what());
    } catch (...) {
        utils::debugln("[CARD_COPY_DIALOG] async warning unknown exception");
    }
}

bool CardPane::beginExternalMaterialDrop(
    XNode& node, const XMLInfo& version,
    const std::optional<std::size_t> dropIndex) {
    if (!summary_ || !comm_.services.dialogs) return false;
    if (!comm_.services.dialogs->supports_message_async()) return false;
    const auto source_summary = node.attribute("summId");
    if (source_summary && source_summary.value() == ownerId()) return false;

    // 複製上で作業する。テストや旧バックエンドが非同期経路に未対応で
    // も、addFromNode()には元のデータを渡す必要がある。
    auto candidate_node = node.clone();
    auto plan = prepareMaterialCopyPlan(
        candidate_node, createCardsFromNode(candidate_node, version));
    if (!plan) return false;

    struct Pending final {
        std::weak_ptr<CardPane> pane;
        std::shared_ptr<MaterialCopyPlan> plan;
        SummaryPtr target_summary;
        std::optional<std::size_t> drop_index;
        bool top_level{};
        bool externalize_binary{};
        bool copy_references{};
    };
    const auto pending = std::make_shared<Pending>(Pending{
        weak_from_this(), plan, summary_, dropIndex,
        boolOr(node.attribute("topLevel"), false), false, false});
    const auto dialogs = comm_.services.dialogs;
    const auto weak = pending->pane;

    const auto complete = std::make_shared<std::function<void()>>();
    *complete = [pending, weak] {
        const auto self = weak.lock();
        if (!self || self->summary_.get() != pending->target_summary.get() ||
            self->read_only_) return;
        const auto result = self->applyMaterialCopy(
            pending->plan, pending->externalize_binary,
            pending->copy_references, false);
        utils::debugln("[CARD_ADD_MATERIAL] result=", static_cast<int>(result),
                       " cards=", pending->plan->values.size(), " async=1");
        if (!self->addCardsImpl(pending->plan->values, false, false, false,
                                pending->top_level, pending->drop_index))
            return;
        if (result == MaterialCopyResult::CopiedWithErrors)
            self->showMaterialCopyWarningAsync();
    };

    const auto make_request = [this](const String& message) {
        lib::MessageDialogRequest request;
        request.title = prop_.coreProps
            ? prop_.coreProps->requiredMsgs().dlgTitQuestion.value : String("確認");
        request.message = message;
        request.severity = lib::MessageSeverity::Question;
        request.buttons = lib::MessageButtons::YesNoCancel;
        return request;
    };
    const auto normal_message = plan->normal_source_names.size() == 1U
        ? utils::tryFormat(prop_.coreProps
                               ? prop_.coreProps->requiredMsgs().dlgMsgCopyMaterial2.value
                               : String("Copy referenced material?\n%1$s"),
                           plan->normal_source_names.front())
        : utils::tryFormat(prop_.coreProps
                               ? prop_.coreProps->requiredMsgs().dlgMsgCopyMaterial3.value
                               : String("Copy %1$s referenced materials?"),
                           plan->normal_source_names.size());
    const auto normal_request = make_request(normal_message);
    const auto binary_request = make_request(
        summary_->legacy() && prop_.coreProps
            ? prop_.coreProps->requiredMsgs().dlgMsgCopyMaterial1.value
            : (prop_.coreProps ? prop_.coreProps->requiredMsgs().dlgMsgExcludeImage.value
                               : String("Copy embedded image material?")));

    const auto ask_normal = std::make_shared<std::function<void()>>();
    *ask_normal = [pending, dialogs, normal_request, complete] {
        if (pending->plan->normal_source_names.empty()) {
            (*complete)();
            return;
        }
        const auto started = [&] {
            try {
                return dialogs->message_async(normal_request,
                    [pending, complete](const lib::MessageResult answer) {
                        if (answer == lib::MessageResult::Cancel ||
                            answer == lib::MessageResult::None) {
                            utils::debugln("[CARD_COPY_DIALOG] answer=cancel");
                            return;
                        }
                        pending->copy_references = answer == lib::MessageResult::Yes;
                        utils::debugln("[CARD_COPY_DIALOG] answer=",
                                       pending->copy_references ? "yes" : "no");
                        (*complete)();
                    });
            } catch (const std::exception& error) {
                utils::debugln("[CARD_COPY_DIALOG] async exception: ", error.what());
                return false;
            } catch (...) {
                utils::debugln("[CARD_COPY_DIALOG] async unknown exception");
                return false;
            }
        }();
        if (started) {
            utils::debugln("[MATERIAL_DIALOG_ENTER] card-copy-async");
            return;
        }
        utils::debugln("[CARD_COPY_DIALOG] async unavailable; continuing without copy");
        (*complete)();
    };

    const bool ask_binary = plan->binary_count > 0U && prop_.coreProps &&
        (!summary_->legacy() || (prop_.coreVars &&
                                 prop_.coreVars->saveInnerImagePath.value));
    if (ask_binary) {
        const auto started = [&] {
            try {
                return dialogs->message_async(binary_request,
                    [pending, ask_normal, complete](const lib::MessageResult answer) {
                        if (answer == lib::MessageResult::Cancel ||
                            answer == lib::MessageResult::None) {
                            utils::debugln("[CARD_COPY_DIALOG] answer=cancel");
                            return;
                        }
                        pending->externalize_binary = answer == lib::MessageResult::Yes;
                        utils::debugln("[CARD_COPY_DIALOG] answer=",
                                       pending->externalize_binary ? "yes" : "no");
                        (*ask_normal)();
                    });
            } catch (const std::exception& error) {
                utils::debugln("[CARD_COPY_DIALOG] async exception: ", error.what());
                return false;
            } catch (...) {
                utils::debugln("[CARD_COPY_DIALOG] async unknown exception");
                return false;
            }
        }();
        if (started) {
            utils::debugln("[MATERIAL_DIALOG_ENTER] card-copy-async");
            return true;
        }
        utils::debugln("[CARD_COPY_DIALOG] binary prompt unavailable; asking referenced materials");
        (*ask_normal)();
        return true;
    }
    if (!plan->normal_source_names.empty()) {
        (*ask_normal)();
        return true;
    }
    (*complete)();
    return true;
}

MaterialCopyResult CardPane::qCardMaterialCopy(
    const XNode& node, const std::vector<CardPtr>& values) const {
    const auto plan = prepareMaterialCopyPlan(node, values);
    if (!plan) return MaterialCopyResult::ContinuedWithoutCopy;

    bool cancelled = false;
    const auto question = [&](const String& message) {
        if (!prop_.coreProps || !comm_.services.dialogs) {
            utils::debugln("[CARD_COPY_DIALOG] unavailable; continuing without copy");
            return false;
        }
        lib::MessageDialogRequest request;
        request.title = prop_.coreProps->requiredMsgs().dlgTitQuestion.value;
        request.message = message;
        request.severity = lib::MessageSeverity::Question;
        request.buttons = lib::MessageButtons::YesNoCancel;
        lib::MessageResult answer = lib::MessageResult::None;
        try {
            utils::debugln("[MATERIAL_DIALOG_ENTER] card-copy");
            answer = comm_.services.dialogs->message(request);
        } catch (const std::exception& error) {
            utils::debugln("[CARD_COPY_DIALOG] exception: ", error.what());
            return false;
        } catch (...) {
            utils::debugln("[CARD_COPY_DIALOG] unknown exception");
            return false;
        }
        if (answer == lib::MessageResult::Cancel || answer == lib::MessageResult::None) {
            cancelled = true;
            utils::debugln("[CARD_COPY_DIALOG] answer=cancel");
            return false;
        }
        utils::debugln("[CARD_COPY_DIALOG] answer=",
                       answer == lib::MessageResult::Yes ? "yes" : "no");
        return answer == lib::MessageResult::Yes;
    };

    bool externalize_binary = false;
    if (plan->binary_count > 0U && prop_.coreProps &&
        (!summary_->legacy() || (prop_.coreVars &&
                                 prop_.coreVars->saveInnerImagePath.value))) {
        externalize_binary = question(
            summary_->legacy() ? prop_.coreProps->requiredMsgs().dlgMsgCopyMaterial1.value
                               : prop_.coreProps->requiredMsgs().dlgMsgExcludeImage.value);
    }
    bool copy_references = false;
    if (!cancelled && !plan->normal_source_names.empty()) {
        const auto message = plan->normal_source_names.size() == 1U
            ? utils::tryFormat(prop_.coreProps
                                   ? prop_.coreProps->requiredMsgs().dlgMsgCopyMaterial2.value
                                   : String("Copy referenced material?\n%1$s"),
                               plan->normal_source_names.front())
            : utils::tryFormat(prop_.coreProps
                                   ? prop_.coreProps->requiredMsgs().dlgMsgCopyMaterial3.value
                                   : String("Copy %1$s referenced materials?"),
                               plan->normal_source_names.size());
        copy_references = question(message);
    }
    if (cancelled) {
        utils::debugln("[CARD_COPY_OUTCOME] cancelled");
        return MaterialCopyResult::Cancelled;
    }
    return applyMaterialCopy(plan, externalize_binary, copy_references, true);
}

void CardPane::copy(SelectionEvent& event) {
    if (!canDoC()) return;
    copyImpl(false);
    event.handled = true;
}

void CardPane::cut(SelectionEvent& event) {
    if (!canDoT()) return;
    copyImpl(true);
    event.handled = true;
    del(event);
}

void CardPane::paste(SelectionEvent& event) {
    if (!canDoP()) return;
    const auto data = comm_.services.clipboard->read();
    try {
        const auto xml = xmlbytestransfer::CBtoXML(data);
        if (xml.empty()) return;
        auto node = XNode::parse(xml, true);
        static system::System defaultSystem;
        const XMLInfo version(defaultSystem,
            summary_ ? summary_->dataVersion() : String(versioninfo::LATEST_VERSION));
        event.handled = addFromNode(node, version);
    } catch (...) {
        event.handled = false;
    }
    refreshStatusLine();
    notifyToolbar();
}

void CardPane::clone(SelectionEvent& event) {
    if (!canDoClone()) return;
    std::vector<CardPtr> values;
    for (const auto& source : selectedCards())
        if (source) values.push_back(source->dup());
    // 切り離した候補と同一ペインの入力元情報は、Dの非公開のXML往
    // 復変換に合わせる。失敗時も実際のクリップボードには触れない。
    event.handled = addCardsImpl(values, true, true, true,
                                owner_type_ == OwnerType::Summary);
}

void CardPane::del(SelectionEvent& event) {
    if (!editMode() || read_only_ || selectedCards().empty() || !enterEdit()) return;
    const auto values = selectedCards();
    if (values.empty()) return;
    auto references = std::make_shared<resourcehistory::References>(
        owner_type_ == OwnerType::Summary && summary_ ? summary_->useCounter() : nullptr);
    const auto oldSelection = selectionIDs();
    // 表示順は名前や説明の並べ替えの場合がある。Dは削除した各カ
    // ードを、所有元集合のインデックスへモデルの昇順で復元する。
    auto restored = std::make_shared<std::vector<CardPtr>>(values);
    std::sort(restored->begin(), restored->end(), [this](const CardPtr& left,
                                                      const CardPtr& right) {
        return indexOf(owner_, card_type_, left) < indexOf(owner_, card_type_, right);
    });
    std::vector<std::ptrdiff_t> oldIndices;
    for (const auto& value : *restored)
        oldIndices.push_back(indexOf(owner_, card_type_, value));
    std::vector<std::uint64_t> oldIdState;
    for (const auto& value : cardsFrom(card_type_, owner_))
        oldIdState.push_back(value ? value->id() : 0U);
    std::vector<std::uint64_t> oldIds;
    oldIds.reserve(values.size());
    for (const auto& value : *restored) oldIds.push_back(value ? value->id() : 0U);
    auto replaySelection = std::make_shared<std::vector<std::uint64_t>>(oldSelection);
    auto replayInput = std::make_shared<std::optional<std::vector<std::uint64_t>>>();
    const auto exchangeReplaySelection = [this, replaySelection, replayInput] {
        auto currentSelection = replayInput->has_value()
            ? std::move(**replayInput) : selectionIDs();
        replayInput->reset();
        selectID(*replaySelection);
        *replaySelection = std::move(currentSelection);
    };
    for (auto it = values.rbegin(); it != values.rend(); ++it) { removeCard(*it); references->hold(*it); }
    std::vector<std::uint64_t> newIdState;
    for (const auto& value : cardsFrom(card_type_, owner_))
        newIdState.push_back(value ? value->id() : 0U);
    pushUndo({[this, restored, references, oldIndices, oldIds, oldIdState,
               exchangeReplaySelection] {
                  for (std::size_t i = 0U; i < restored->size(); ++i) {
                      auto& value = (*restored)[i];
                      references->release(value.get());
                      if (value && i < oldIds.size()) value->id(oldIds[i]);
                      // Cast::insert(false)は新しい有効なインス
                      // タンスを返す。切り離された保存用コピーをポインター同一性で
                      // 削除せず、次のやり直し用に返されたインスタンスを保持する。
                      value = insert(owner_, oldIndices[i], value);
                      refCard(value, false);
                  }
                  restoreIDState(oldIdState);
                  refreshImpl();
                  exchangeReplaySelection();
                  notifyCollectionChanged();
              },
              [this, restored, references, newIdState, exchangeReplaySelection] {
                  for (auto it = restored->rbegin(); it != restored->rend(); ++it) { removeCard(*it); references->hold(*it); }
                  restoreIDState(newIdState);
                  refreshImpl();
                  exchangeReplaySelection();
                  notifyCollectionChanged();
              }, "delete", [this, replayInput] {
                  *replayInput = selectionIDs();
              }});
    notifyCollectionChanged();
    deselectAll();
    refreshImpl();
    event.handled = true;
}

bool CardPane::canDoTCPD() const noexcept {
    // Dは一覧が空でも、表示中のCardList/Table
    // でアクティブなTCPDを選ぶ。新しく空になったカードペ
    // インへの貼り付けも、そのペインへ配送する必要がある。
    return owner_ && ((image_list_ && image_list_->visible()) ||
                      (table_ && table_->visible()));
}

bool CardPane::canDoT() const noexcept {
    return editMode() && !read_only_ && !selectedCards().empty();
}

bool CardPane::canDoC() const noexcept {
    return comm_.services.clipboard && !selectedCards().empty();
}

bool CardPane::canDoP() const noexcept {
    // Dは最終的なカードの形ではなく、クリップボードのXML対応で貼り付け可
    // 否を決める。解析と対応カードの検証は貼り付け操作内で行う。このホスト
    // では、接続されたSummaryが読み取り専用なら書き込みも拒否する。
    return editMode() && !read_only_ && summary_ && comm_.services.clipboard &&
        xmlbytestransfer::CBisXML(*comm_.services.clipboard);
}

bool CardPane::canDoD() const noexcept {
    return canDoT();
}

bool CardPane::canDoClone() const noexcept {
    return canDoT();
}

bool CardPane::canReplaceID() const noexcept {
    return editMode() && !read_only_ && static_cast<bool>(comm_.replaceID) && selection();
}

void CardPane::replaceID() {
    if (!canReplaceID() || !enterEdit() || !canReplaceID()) return;
    comm_.replaceID(card_type_, selection()->id(), true);
}

bool CardPane::canUp() const noexcept {
    if (!summary_ || !editMode() || read_only_ || selection_.empty() || cards_.empty())
        return false;
    // Dは絞り込み後も入力元の全カードが見えていれば並べ替えを許可する。共
    // 通ホストでは、接続されたSummaryの読み取り専用状態にも従う。
    if (cards_.size() != cards().size()) return false;
    if (table_ && table_->sortColumn() && table_->sortColumn() != columnFromInt(columnToInt(CardTableColumn::ID))) return false;
    return *std::min_element(selection_.begin(), selection_.end()) > 0U;
}

bool CardPane::canDown() const noexcept {
    if (!summary_ || !editMode() || read_only_ || selection_.empty() || cards_.empty())
        return false;
    // Dは絞り込み後も入力元の全カードが見えていれば並べ替えを許可する。共
    // 通ホストでは、接続されたSummaryの読み取り専用状態にも従う。
    if (cards_.size() != cards().size()) return false;
    if (table_ && table_->sortColumn() && table_->sortColumn() != columnFromInt(columnToInt(CardTableColumn::ID))) return false;
    return *std::max_element(selection_.begin(), selection_.end()) + 1U < cards_.size();
}

void CardPane::up() {
    if (!canUp()) return;
    // DのudImplは、行を並べ替える前に表示側の編集中の値を確定する。
    if (!enterEdit() || !canUp()) return;
    auto selected = selection_;
    const auto selectedValues = selectedCards();
    if (table_ && table_->sortDirection() == customtable::SortDirection::Down)
        for (auto& index : selected) index = cards_.size() - index - 1U;
    std::sort(selected.begin(), selected.end());
    if (selected.front() == 0U) return;
    const auto oldSelection = selectionIDs();
    auto replaySelection = std::make_shared<std::vector<std::uint64_t>>(oldSelection);
    auto replayInput = std::make_shared<std::optional<std::vector<std::uint64_t>>>();
    const auto exchangeReplaySelection = [this, replaySelection, replayInput] {
        auto currentSelection = replayInput->has_value()
            ? std::move(**replayInput) : selectionIDs();
        replayInput->reset();
        selectID(*replaySelection);
        *replaySelection = std::move(currentSelection);
    };
    for (const auto index : selected) swapCards(index, index - 1U);
    refreshImpl();
    selectMovedCardsImpl(selectedValues, false);
    pushUndo({[this, selected, exchangeReplaySelection] {
                  for (auto it = selected.rbegin(); it != selected.rend(); ++it) swapCards(*it - 1U, *it);
                  refreshImpl();
                  exchangeReplaySelection();
              },
              [this, selected, exchangeReplaySelection] {
                  for (const auto index : selected) swapCards(index, index - 1U);
                  refreshImpl();
                  exchangeReplaySelection();
              }, "up", [this, replayInput] {
                  *replayInput = selectionIDs();
              }});
}

void CardPane::down() {
    if (!canDown()) return;
    // DのudImplは、行を並べ替える前に表示側の編集中の値を確定する。
    if (!enterEdit() || !canDown()) return;
    auto selected = selection_;
    const auto selectedValues = selectedCards();
    if (table_ && table_->sortDirection() == customtable::SortDirection::Down)
        for (auto& index : selected) index = cards_.size() - index - 1U;
    std::sort(selected.begin(), selected.end());
    if (selected.back() + 1U >= cards_.size()) return;
    const auto oldSelection = selectionIDs();
    auto replaySelection = std::make_shared<std::vector<std::uint64_t>>(oldSelection);
    auto replayInput = std::make_shared<std::optional<std::vector<std::uint64_t>>>();
    const auto exchangeReplaySelection = [this, replaySelection, replayInput] {
        auto currentSelection = replayInput->has_value()
            ? std::move(**replayInput) : selectionIDs();
        replayInput->reset();
        selectID(*replaySelection);
        *replaySelection = std::move(currentSelection);
    };
    for (auto it = selected.rbegin(); it != selected.rend(); ++it) swapCards(*it, *it + 1U);
    refreshImpl();
    selectMovedCardsImpl(selectedValues, false);
    pushUndo({[this, selected, exchangeReplaySelection] {
                  for (const auto index : selected) swapCards(index, index + 1U);
                  refreshImpl();
                  exchangeReplaySelection();
              },
              [this, selected, exchangeReplaySelection] {
                  for (auto it = selected.rbegin(); it != selected.rend(); ++it) swapCards(*it, *it + 1U);
                  refreshImpl();
                  exchangeReplaySelection();
              }, "down", [this, replayInput] {
                  *replayInput = selectionIDs();
              }});
}

std::vector<String> CardPane::openedCWXPath() const {
    std::vector<String> result;
    for (const auto& value : selectedCards())
        if (value) result.push_back(value->cwxPath(true));
    if (!result.empty()) return result;
    if (owner_) result.push_back(path::cpjoin(owner_, categoryName(card_type_) + "view", true));
    return result;
}

bool CardPane::openCWXPath(const StringView value, const bool shellActivate) {
    const auto category = path::cpcategory(value);
    const auto name = categoryName(card_type_);
    const auto focus = [&] {
        if (path::cphasattr(value, "nofocus") || !comm_.services.gui) return;
        const auto input = inputWidget();
        if (!input) return;
        dutils::forceFocus(input, shellActivate, comm_.services);
    };
    if (category == name + "view") {
        focus();
        return true;
    }
    if (category != name && category != name + ":id") return false;
    const auto index = path::cpindex(value);
    // IDと位置の名前空間は意図的に分離する。特にIDが見つからなくても
    // 、インデックスやMainWindowのコールバックへ切り替えない。
    const auto source = cards();
    const auto target = category == name + ":id" ? card(index)
        : index < source.size() ? source[index] : CardPtr{};
    if (!target) return false;
    const auto bottom = path::cpbottom(value);
    const auto tail = path::cpcategory(bottom);
    const bool motion = (card_type_ == CardType::Skill ||
                         card_type_ == CardType::Item ||
                         card_type_ == CardType::Beast) && tail == "motion";
    if (!path::cpempty(bottom) && (!motion || !target->findCWXPath(bottom))) return false;
    if (std::find(cards_.begin(), cards_.end(), target) == cards_.end() &&
        !search_text_.empty()) setSearchText({});
    const auto found = std::find(cards_.begin(), cards_.end(), target);
    if (found == cards_.end()) return false;
    auto selected = path::cphasattr(value, "only")
        ? std::vector<std::size_t>{} : selection_;
    const auto position = static_cast<std::size_t>(found - cards_.begin());
    selected.push_back(position);
    select(selected);
    focus();
    notifyToolbar();
    if (editMode() && (path::cphasattr(value, "opendialog") || motion)) {
        const auto dialog = edit(target);
        if (!dialog) return false;
        if (motion) return dialog->openCWXPath(bottom, shellActivate);
    }
    return true;
}

void CardPane::copyImpl(const bool del) {
    const auto values = selectedCards();
    if (values.empty() || !comm_.services.clipboard) return;
    const auto xml = toXML(values, del);
    if (xml.empty()) return;
    const bool asText = prop_.coreVars ? prop_.coreVars->xmlCopy.value : prop_.xmlCopy;
    comm_.services.clipboard->write(asText ? xmlbytestransfer::makeTextPayload(xml)
                                          : xmlbytestransfer::makeXMLPayload(xml));
    notifyToolbar();
}

void CardPane::copySelectionXML() {
    const auto value = selection();
    if (!value || !comm_.services.clipboard) return;
    lib::ClipboardData data;
    // Raw Copyは単一カードの直列化であり、通常コピーの入力元情報は付けない。
    const card::XMLOption option(nullptr, String(versioninfo::LATEST_VERSION));
    if (const auto typed = std::dynamic_pointer_cast<CastCard>(value)) data.text = typed->toXML(option);
    else if (const auto typed = std::dynamic_pointer_cast<SkillCard>(value)) data.text = typed->toXML(option);
    else if (const auto typed = std::dynamic_pointer_cast<ItemCard>(value)) data.text = typed->toXML(option);
    else if (const auto typed = std::dynamic_pointer_cast<BeastCard>(value)) data.text = typed->toXML(option);
    else if (const auto typed = std::dynamic_pointer_cast<InfoCard>(value)) data.text = typed->toXML(option);
    comm_.services.clipboard->write(data);
}

void CardPane::editM() {
    (void)edit(-1);
}

void CardPane::numCreateEditor(const std::size_t row, const std::size_t column) {
    if (canEditT(row, column) && table_) table_->setCursor(row, column);
}

void CardPane::listEditEnd(const std::size_t row, String value) {
    nameEditEnd(row, std::move(value));
}

void CardPane::listCreateEditor(const CardPtr& value) {
    if (value) selectID(value->id());
}

void CardPane::beginCardListNameEdit(const CardPtr& value) {
    // DのF2とTextEditMFListenerは、どちらもCardListEdit.createEditor
    // へ到達する。C++でも同じ表示側への要求・確定経路を使う。value
    // はcards_の要素を参照し得るため、enterEditの確定
    // コールバックが一覧を更新・再接続する前に、同一性をコピーする。
    const CardPtr requested = value;
    if (!editMode() || read_only_ || !requested || linkId(requested) ||
        !image_list_ || !comm_.services.gui) return;
    const std::weak_ptr<int> life = renumber_lifetime_;
    const auto generation = owner_binding_generation_;
    const auto list = image_list_;
    const auto gui = comm_.services.gui;
    // DのCardListEdit::startEditは、新しい編集
    // 部を作る前に既存の編集を確定する。共通表示側は編集中の二つ目の要
    // 求を拒否するため、先にCardPaneの境界で確定を済ませる。
    if (!enterEdit()) return;
    // 確定により行が更新・再接続されたり、コールバックがペインを終了させたり
    // する場合がある。その境界の後でだけ、要求カードを同一性から再解決する。
    if (life.expired() || owner_binding_generation_ != generation ||
        image_list_ != list || comm_.services.gui != gui || !editMode() ||
        read_only_ || !requested || linkId(requested)) return;
    const auto found = std::find(cards_.begin(), cards_.end(), requested);
    if (found == cards_.end()) return;
    const auto index = static_cast<int>(std::distance(cards_.begin(), found));
    list->scroll(index);
    lib::ProgrammaticInlineEditRequest request;
    request.generic_owner = list;
    request.focus_scope = pane_;
    request.generic_text = requested->name();
    request.generic_bounds = [this, life, generation, list, requested]() -> std::optional<lib::Rect> {
        if (life.expired() || owner_binding_generation_ != generation ||
            image_list_ != list) return std::nullopt;
        const auto row = std::find(cards_.begin(), cards_.end(), requested);
        if (row == cards_.end()) return std::nullopt;
        const auto current_index = static_cast<int>(std::distance(cards_.begin(), row));
        const auto image = list->getImageBounds(current_index);
        const auto title = list->getTitleBounds(current_index);
        const auto host = list->bounds();
        return lib::Rect{host.x + image.x, host.y + title.y,
                         image.width, std::max(20.0F, title.height)};
    };
    request.generic_commit = [this, life, generation, list, requested](String text) {
        if (life.expired() || owner_binding_generation_ != generation ||
            image_list_ != list || read_only_ || linkId(requested)) return false;
        const auto row = std::find(cards_.begin(), cards_.end(), requested);
        if (row == cards_.end()) return false;
        nameEditEnd(static_cast<std::size_t>(std::distance(cards_.begin(), row)),
                    std::move(text));
        return true;
    };
    (void)gui->request_programmatic_inline_edit(request);
}

CardPane::CPUndo::CPUndo(CardPane* view, std::function<void()> undo,
                         std::function<void()> redo)
    : view_(view), undo_(std::move(undo)), redo_(std::move(redo)) {}

void CardPane::CPUndo::undo() {
    if (undo_) undo_();
}

void CardPane::CPUndo::redo() {
    if (redo_) redo_();
}

void CardPane::CPUndo::dispose() {
    undo_ = {};
    redo_ = {};
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::cardpane
