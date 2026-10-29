#include "cwxeditor_src/gui/cwx/editor/gui/dwt/cardwindow.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::cardwindow {
namespace usecounter = ::cwxeditor::cwx::usecounter;
namespace structs = ::cwxeditor::cwx::structs;
namespace {

[[nodiscard]] bool hasModifier(const lib::Modifiers value,
                               const lib::Modifiers modifier) noexcept {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(modifier)) != 0U;
}

[[nodiscard]] String numberText(const std::uint64_t value) {
    return std::to_string(value);
}

[[nodiscard]] String cardCategory(const CardType type) {
    switch (type) {
    case CardType::Cast: return "castcard";
    case CardType::Skill: return "skillcard";
    case CardType::Item: return "itemcard";
    case CardType::Beast: return "beastcard";
    case CardType::Info: return "infocard";
    }
    return {};
}

[[nodiscard]] lib::ResourceId cardImage(const CardType type, const Props& prop) {
    switch (type) {
    case CardType::Cast: return prop.castImage;
    case CardType::Skill: return prop.skillImage;
    case CardType::Item: return prop.itemImage;
    case CardType::Beast: return prop.beastImage;
    case CardType::Info: return prop.infoImage;
    }
    return 0U;
}

[[nodiscard]] CardPtr typedCard(const CardType type, const std::uint64_t id,
                                const String& name) {
    const lib::Array<lib::Owner<card::CardImage>> images;
    switch (type) {
    case CardType::Cast:
        return std::make_shared<CastCard>(id, name, images, String{});
    case CardType::Skill:
        return std::make_shared<SkillCard>(nullptr, id, name, images, String{});
    case CardType::Item:
        return std::make_shared<ItemCard>(nullptr, id, name, images, String{});
    case CardType::Beast:
        return std::make_shared<BeastCard>(nullptr, id, name, images, String{});
    case CardType::Info:
        return std::make_shared<InfoCard>(id, name, images, String{});
    }
    return {};
}

[[nodiscard]] String cardName(const CardType type, const Props& prop) {
    switch (type) {
    case CardType::Cast: return prop.castName;
    case CardType::Skill: return prop.skillName;
    case CardType::Item: return prop.itemName;
    case CardType::Beast: return prop.beastName;
    case CardType::Info: return prop.infoName;
    }
    return {};
}

[[nodiscard]] CWXPathPtr asPath(const SummaryPtr& value) {
    return value ? std::dynamic_pointer_cast<CWXPath>(value) : CWXPathPtr{};
}

[[nodiscard]] CWXPathPtr asPath(const CastCardPtr& value) {
    return value ? std::dynamic_pointer_cast<CWXPath>(value) : CWXPathPtr{};
}

template <typename Values>
void appendCards(std::vector<CardPtr>& output,
                 const Values& values) {
    // CardPaneは独立した表示行順を持つ。モデル範囲をUI
    // 用の状態に変換するときに、所有元の項目をコピーする。
    for (const auto& value : values)
        if (value) output.push_back(value);
}

template <typename Values>
[[nodiscard]] std::ptrdiff_t indexOf(const Values& values,
                                     const CardPtr& card) {
    for (std::size_t index = 0U; index < values.size(); ++index)
        if (values[index].get() == card.get())
            return static_cast<std::ptrdiff_t>(index);
    return -1;
}

[[nodiscard]] String joinPaths(const std::vector<CardPtr>& cards) {
    String result;
    for (const auto& value : cards) {
        if (!value) continue;
        const auto pathValue = value->cwxPath(true);
        if (pathValue.empty()) continue;
        if (!result.empty()) result += '&';
        result += pathValue;
    }
    return result;
}

[[nodiscard]] canonical_cardpane::CardType canonicalType(const CardType value) noexcept {
    return static_cast<canonical_cardpane::CardType>(value);
}

[[nodiscard]] canonical_cardpane::OwnerType canonicalOwnerType(
    const OwnerType value) noexcept {
    return static_cast<canonical_cardpane::OwnerType>(value);
}

[[nodiscard]] canonical_cardpane::Props canonicalProps(const Props& value,
                                                        const bool readOnly) {
    canonical_cardpane::Props result;
    result.editorProps = value.editorProps;
    if (value.areaTableContext)
        result.dialogOwner = value.areaTableContext->dialog_owner;
    result.coreProps = value.editorProps ? &value.editorProps->parent() : nullptr;
    result.coreVars = value.editorProps ? &value.editorProps->var() : nullptr;
    if (result.coreVars) {
        result.linkCard = result.coreVars->linkCard.value;
        result.xmlCopy = result.coreVars->xmlCopy.value;
        result.showCardListTitle = result.coreVars->showCardListTitle.value;
    }
    result.cardId = value.idLabel;
    result.cardName = value.nameLabel;
    result.cardDesc = value.descriptionLabel;
    result.level = value.levelLabel;
    result.useCount = value.useNumberLabel;
    result.cardCount = value.useCountLabel;
    result.cardIdColumn = value.cardIdColumn;
    result.cardNameColumn = value.cardNameColumn;
    result.cardNumberColumn = value.cardNumberColumn;
    result.cardDescriptionColumn = value.cardDescriptionColumn;
    result.cardCountColumn = value.cardCountColumn;
    result.handCardIdColumn = value.handCardIdColumn;
    result.handCardNameColumn = value.handCardNameColumn;
    result.handCardNumberColumn = value.handCardNumberColumn;
    result.handCardDescriptionColumn = value.handCardDescriptionColumn;
    result.importCardIdColumn = value.importCardIdColumn;
    result.importCardNameColumn = value.importCardNameColumn;
    result.importCardNumberColumn = value.importCardNumberColumn;
    result.importCardDescriptionColumn = value.importCardDescriptionColumn;
    result.importCardCountColumn = value.importCardCountColumn;
    result.importHandCardIdColumn = value.importHandCardIdColumn;
    result.importHandCardNameColumn = value.importHandCardNameColumn;
    result.importHandCardNumberColumn = value.importHandCardNumberColumn;
    result.importHandCardDescriptionColumn = value.importHandCardDescriptionColumn;
    result.mainCardsSortColumn = value.mainCardsSortColumn;
    result.mainCardsSortDirection = value.mainCardsSortDirection;
    result.handCardsSortColumn = value.handCardsSortColumn;
    result.handCardsSortDirection = value.handCardsSortDirection;
    result.importCardsSortColumn = value.importCardsSortColumn;
    result.importCardsSortDirection = value.importCardsSortDirection;
    result.importHandCardsSortColumn = value.importHandCardsSortColumn;
    result.importHandCardsSortDirection = value.importHandCardsSortDirection;
    result.cardWidth = value.cardWidth;
    result.cardHeight = value.cardHeight;
    result.castCardWidth = value.castCardWidth;
    result.castCardHeight = value.castCardHeight;
    result.cardsMarginX = value.cardsMarginX;
    result.cardsSpaceX = value.cardsSpaceX;
    result.cardsMarginY = value.cardsMarginY;
    result.cardsSpaceY = value.cardsSpaceY;
    result.cardsTitleSpace = value.cardsTitleSpace;
    result.cardsFocusLinePadding = value.cardsFocusLinePadding;
    result.cardsDefaultWrap = value.cardsDefaultWrap;
    result.cardStatus = value.cardLifeStatus;
    result.cardStatusSelOne = value.cardSelectedStatus;
    result.cardStatusSelMulti = value.cardListStatus;
    if (value.editorProps) {
        const auto& messages = value.editorProps->requiredMsgs();
        result.handCardStatus = messages.handCardStatus.value;
        result.handCardStatusSelOne = messages.handCardStatusSelOne.value;
        result.handCardStatusSelMulti = messages.handCardStatusSelMulti.value;
        result.cardIsReference = messages.cardIsReference.value;
        result.referencedCardIsNotFound = messages.referencedCardIsNotFound.value;
    }
    result.cardTabName = value.cardTabName;
    result.castName = value.castName;
    result.skillName = value.skillName;
    result.itemName = value.itemName;
    result.beastName = value.beastName;
    result.infoName = value.infoName;
    result.castImage = value.castImage;
    result.skillImage = value.skillImage;
    result.itemImage = value.itemImage;
    result.beastImage = value.beastImage;
    result.infoImage = value.infoImage;
    result.handImage = value.handImage;
    result.handEmptyImage = value.handEmptyImage;
    result.eventTreeImage = value.eventTreeImage;
    result.eventTreeEmptyImage = value.eventTreeEmptyImage;
    result.commentImage = value.commentImage;
    result.showHandMark = value.showHandMark;
    result.showHandMarkAlways = value.showHandMarkAlways;
    result.showEventTreeMark = value.showEventTreeMark;
    result.showEventTreeMarkAlways = value.showEventTreeMarkAlways;
    result.ignoreEmptyStart = value.ignoreEmptyStart;
    result.readOnly = readOnly || value.readOnly;
    result.undoMax = value.undoMax;
    return result;
}

class CallbackCardDialog final
    : public canonical_cardpane::CardDialog,
      public std::enable_shared_from_this<CallbackCardDialog> {
public:
    CallbackCardDialog(CardPtr value, std::function<void(lib::Action, lib::Action, lib::Action, bool)> callback,
                       std::function<bool(StringView, bool)> openPath)
        : callback_(std::move(callback)), open_path_(std::move(openPath)) {
        card(std::move(value));
    }

    bool openCWXPath(const StringView path, const bool activate) override {
        const auto open = open_path_;
        return open && open(path, activate);
    }

    bool open() override {
        notifyHost();
        return canonical_cardpane::CardDialog::open();
    }

    void active() override {
        // CardPaneはDと同様にカードごとのダイアログハン
        // ドルを一つ保持する。この橋渡しのハンドルはネイティブダ
        // イアログ自体ではないため、後のアクティブ化では抽象CardDialog
        // の状態変更だけでなく、再度転送する。
        notifyHost();
        canonical_cardpane::CardDialog::active();
    }

private:
    void notifyHost() {
        if (!callback_) return;
        const auto weak = weak_from_this();
        callback_(
            [weak] {
                if (const auto self = weak.lock()) {
                    const auto applying = self->applying;
                    if (applying) applying();
                }
            },
            [weak] {
                if (const auto self = weak.lock(); self && self->applied)
                    self->applied();
            },
            [weak] {
                if (const auto self = weak.lock()) {
                    self->applying = {};
                    if (self->closed) self->closed();
                    self->callback_ = {};
                }
            }, creationMode);
    }

    std::function<void(lib::Action, lib::Action, lib::Action, bool)> callback_;
    std::function<bool(StringView, bool)> open_path_;
};

}  // namespace

CardPane::CardPane(Commons comm, Props prop, SummaryPtr summary, Parent parent,
                   const OwnerType ownerType, const CardType cardType,
                   const bool readOnly, SummaryPtr toc,
                   std::function<void(const CastCardPtr&)> openHand)
    : comm_(std::move(comm)), prop_(std::move(prop)), parent_(std::move(parent)),
      summary_(std::move(summary)), toc_(std::move(toc)), owner_type_(ownerType),
      card_type_(cardType), read_only_(readOnly || prop_.readOnly),
      open_hand_(std::move(openHand)) {
    canonical_cardpane::Commons bridge;
    bridge.services = comm_.services;
    bridge.materialPath = comm_.materialPath;
    bridge.refPaths = comm_.refPaths;
    bridge.refreshToolBar = comm_.refreshToolBar;
    bridge.setStatusLine = comm_.setStatusLine;
    bridge.stopOpen = [life = std::weak_ptr<int>(callback_lifetime_), stop = comm_.stopOpen] {
        return life.expired() || (stop && stop());
    };
    bridge.refUseCount = comm_.refUseCount;
    bridge.refOwner = [this, life = std::weak_ptr<int>(callback_lifetime_)] {
        if (life.expired()) return;
        const auto owner = std::dynamic_pointer_cast<CastCard>(owner_);
        const auto notify = comm_.refCast;
        if (owner) notify.call(owner);
    };
    bridge.refScenario = comm_.addScenario;
    bridge.openCardPane = [this](const canonical_cardpane::CardType type,
                                 const bool activate) {
        if (comm_.openCardPane)
            (void)comm_.openCardPane(static_cast<CardType>(type), activate);
    };
    bridge.openPane = [this](const canonical_cardpane::CardType type,
                             const bool activate) {
        const auto pane = comm_.openCardPane
            ? comm_.openCardPane(static_cast<CardType>(type), activate)
            : CardPanePtr{};
        return pane ? pane->canonical() : canonical_cardpane::CardPanePtr{};
    };
    // 呼び出せるように見える無操作の関数を公開しない。標準ペインはコ
    // ールバックの有無でDのOpenHandコマンドの可否を判断す
    // る。取り込み元のペインは別のopen_hand_経路を使う。
    if (comm_.openHands) {
        bridge.openHands = [open = comm_.openHands, props = prop_, toc = toc_](
                                  const SummaryPtr& summ, const CastCardPtr& card,
                                  const bool activate) {
            open(props, summ, card, toc, activate);
        };
    }
    if (comm_.openHandsPane) {
        bridge.openHandsPane = [open = comm_.openHandsPane, props = prop_, toc = toc_](
                                      const SummaryPtr& summ, const CastCardPtr& card,
                                      const canonical_cardpane::CardType type,
                                      const bool activate) {
            const auto pane = open(props, summ, card, toc,
                static_cast<CardType>(type), activate);
            return pane ? pane->canonical() : canonical_cardpane::CardPanePtr{};
        };
    }
    bridge.openUseEvents = [life = std::weak_ptr<int>(callback_lifetime_),
                            open = comm_.openUseEvents, props = prop_](
                               const SummaryPtr& summ, const EffectCardPtr& card,
                               const bool duplicate) {
        if (!life.expired() && open) open(props, summ, card, true, duplicate);
    };
    bridge.makeDialog = [edit = comm_.editCard, openPath = comm_.openCardDialogPath](const canonical_cardpane::CardType type,
                               const SummaryPtr& summ, const CWXPathPtr& owner,
                               const CardPtr& card, const bool readOnlyValue)
        -> canonical_cardpane::CardDialogPtr {
        if (!edit) return canonical_cardpane::CardDialogPtr{};
        return std::make_shared<CallbackCardDialog>(card, [edit, type, summ, owner,
                                                           card, readOnlyValue](
                                                              lib::Action applying,
                                                              lib::Action applied,
                                                              lib::Action closed, bool creationMode) {
            edit(static_cast<CardType>(type), summ, owner, card,
                           readOnlyValue, std::move(applying), std::move(applied), std::move(closed), creationMode);
        }, [openPath, card](const StringView path, const bool activate) {
            return openPath && openPath(card, path, activate);
        });
    };
    bridge.doImport = [this, life = std::weak_ptr<int>(callback_lifetime_),
                       import = comm_.doImport](const SummaryPtr& target,
                                               const std::vector<String>& paths) {
        if (life.expired() || !import) return;
        const auto source = summary_;
        import(target, source, paths);
    };
    bridge.openCWXPath = comm_.openCWXPath;
    // Dの素材コマンドは、shellActivateとonlySelectを維持して実
    // ファイルを選択する。素材パスはEditorのCWXPathの移動経路ではない。
    bridge.openFilePath = [life = std::weak_ptr<int>(callback_lifetime_),
                           openFile = comm_.openFilePath](const StringView pathValue,
                                                         bool activate, bool onlySelect) {
        if (!life.expired() && openFile) (void)openFile(pathValue, activate, onlySelect);
    };
    bridge.refCard = [this, life = std::weak_ptr<int>(callback_lifetime_)](const CardPtr& value) {
        if (life.expired()) return;
        const auto notify = comm_.refCard;
        const auto refCast = comm_.refCast;
        const auto toolbar = comm_.refreshToolBar;
        // ホストへの公開前に、窓口を標準の有効な行へ合わせる。ラッパーの終了で
        // 呼び出し先の保存領域が破棄されないよう、コールバックをコピーする。
        refreshSnapshot();
        if (notify) notify(value);
        else if (const auto cast = std::dynamic_pointer_cast<CastCard>(value)) refCast.call(cast);
        if (!life.expired() && toolbar) toolbar();
    };
    bridge.delCard = [this](const CWXPathPtr& owner, const CardPtr& value) {
        // 削除はモデルのイベントであり、同じ所有元の更新ではない。ネイティブのプロパ
        // ティ・使用時イベントの利用側は、後の取り消しより前に終了する必要がある。
        if (comm_.delCard) comm_.delCard(owner, value);
        refreshSnapshot();
        if (comm_.refreshToolBar) comm_.refreshToolBar();
    };
    if (comm_.makeCommentDialog) {
        bridge.makeCommentDialog = [life = std::weak_ptr<int>(callback_lifetime_),
                                    make = comm_.makeCommentDialog](
                                       const SummaryPtr& summary, const CardPtr& card,
                                       const StringView text) -> std::shared_ptr<comment::CommentDialog> {
            if (life.expired()) return {};
            const auto dialog = make(summary, card, text);
            return life.expired() ? std::shared_ptr<comment::CommentDialog>{} : dialog;
        };
    }
    bridge.commentTitle = comm_.commentTitle;
    bridge.storeSort = comm_.storeSort;
    bridge.storeColumnWidth = comm_.storeColumnWidth;
    bridge.columnWidthEvent = comm_.columnWidthEvent;
    bridge.localColumnWidthSync = comm_.localColumnWidthSync;
    bridge.refSortParamsEvent = comm_.refSortParamsEvent;
    bridge.cardChangedEvent = comm_.cardChangedEvent;
    bridge.appearanceEvent = comm_.appearanceEvent;
    bridge.warnings = comm_.warnings;
    bridge.warningImage = comm_.warningImage;
    bridge.menuText = comm_.menuText;
    bridge.replaceID = [this](const canonical_cardpane::CardType type,
                              const std::uint64_t id, const bool start) {
        if (comm_.replaceID)
            comm_.replaceID(static_cast<CardType>(type), id, start);
    };
    bridge.cardImageData = comm_.cardImageData;
    bridge.cardLifeImageData = comm_.cardLifeImageData;
    bridge.cardImageHasCaption = comm_.cardImageHasCaption;
    canonical_ = std::make_shared<canonical_cardpane::CardPane>(
        std::move(bridge), canonicalProps(prop_, read_only_), summary_, parent_,
        canonicalOwnerType(owner_type_), canonicalType(card_type_),
        customtable::STYLE_MULTI_SELECT | customtable::STYLE_HEADER, toc_,
        open_hand_);
}

PanelPtr CardPane::construct(const Parent& parent) {
    if (canonical_) {
        component_ = canonical_->construct(parent);
        table_ = canonical_->table();
        refreshSnapshot();
        return component_;
    }
    if (component_) {
        if (parent && std::find(parent->children().begin(), parent->children().end(),
                                std::static_pointer_cast<lib::Widget>(component_)) ==
                           parent->children().end())
            parent->add(component_);
        return component_;
    }

    parent_ = parent;
    component_ = std::make_shared<lib::Panel>();
    component_->set_layout(std::make_shared<lib::FillLayout>());
    const auto style = customtable::STYLE_MULTI_SELECT |
                       customtable::STYLE_HEADER |
                       (read_only_ ? customtable::STYLE_READ_ONLY : customtable::STYLE_NONE);
    table_ = customtable::rangeSelectableTable(component_.get(), style, comm_.services);
    table_->setHeaderHeight(24.0F);
    table_->setRowHeight(22.0F);
    id_column_ = table_->addColumn(prop_.idLabel, 52.0F, customtable::STYLE_NONE);
    name_column_ = table_->addColumn(prop_.nameLabel, 140.0F, customtable::STYLE_NONE);
    level_column_ = table_->addColumn(prop_.levelLabel, 58.0F, customtable::STYLE_NONE);
    desc_column_ = table_->addColumn(prop_.descriptionLabel, 260.0F, customtable::STYLE_NONE);
    use_count_column_ = table_->addColumn(prop_.useCountLabel, 62.0F, customtable::STYLE_NONE);
    if (id_column_) id_column_->sortable(true);
    if (name_column_) name_column_->sortable(true);

    table_->addSelectionListener(
        [this](const std::vector<std::size_t>& values) { handleSelection(values); });
    table_->addEditListener(
        [this](const std::size_t row, const std::size_t column, String value) {
            handleEdit(row, column, std::move(value));
        });

    image_list_ = std::make_shared<cardlist::CardList<CardPtr>>(
        component_, cardlist::Style::Multi | cardlist::Style::VerticalScroll,
        comm_.services);
    image_list_->set_visible(false);
    image_list_->addSelectionListener([this](const cardlist::SelectionEvent& event) {
        std::vector<std::size_t> selection;
        selection.reserve(event.indices.size());
        for (const auto index : event.indices)
            if (index >= 0) selection.push_back(static_cast<std::size_t>(index));
        handleSelection(selection);
    });
    component_->add(image_list_);

    id_sorter_ = std::make_unique<customtable::TableSorter<CardPtr>>(
        id_column_,
        [](const CardPtr& left, const CardPtr& right) {
            return left && right && left->id() < right->id();
        });
    name_sorter_ = std::make_unique<customtable::TableSorter<CardPtr>>(
        name_column_,
        [](const CardPtr& left, const CardPtr& right) {
            return left && right && left->name() < right->name();
        });
    const auto sorted = [this] {
        cards_.clear();
        if (!table_) return;
        for (std::size_t index = 0U; index < table_->rows.size(); ++index) {
            const auto* value = std::any_cast<CardPtr>(&table_->rowState(index).data);
            if (value && *value) cards_.push_back(*value);
        }
        rebuildOwnerOrder();
        refreshStatusLine();
    };
    id_sorter_->sortedEvent.push_back(sorted);
    name_sorter_->sortedEvent.push_back(sorted);

    installInputHandlers();
    bindDragDrop();
    refresh();
    return component_;
}

void CardPane::reconstruct(const Parent& parent) {
    if (canonical_) {
        canonical_->reconstruct(parent);
        component_ = canonical_->pane();
        table_ = canonical_->table();
        refreshSnapshot();
        return;
    }
    if (!component_) {
        construct(parent);
        return;
    }
    if (parent) {
        parent_ = parent;
        if (std::find(parent->children().begin(), parent->children().end(),
                      std::static_pointer_cast<lib::Widget>(component_)) ==
            parent->children().end())
            parent->add(component_);
    }
    refresh();
}

std::vector<customtable::TableColumnPtr> CardPane::columns() const {
    if (canonical_) {
        std::vector<customtable::TableColumnPtr> result;
        for (const auto& [kind, column] : canonical_->columns()) {
            (void)kind;
            if (column) result.push_back(column);
        }
        return result;
    }
    std::vector<customtable::TableColumnPtr> result;
    if (id_column_) result.push_back(id_column_);
    if (name_column_) result.push_back(name_column_);
    if (level_column_) result.push_back(level_column_);
    if (desc_column_) result.push_back(desc_column_);
    if (use_count_column_) result.push_back(use_count_column_);
    return result;
}

CardPtr CardPane::card(const std::size_t index) const noexcept {
    if (canonical_) {
        const auto values = canonical_->cards();
        return index < values.size() ? values[index] : CardPtr{};
    }
    return index < cards_.size() ? cards_[index] : CardPtr{};
}

CardPtr CardPane::cardById(const std::uint64_t id) const noexcept {
    if (canonical_) return canonical_->card(id);
    const auto it = std::find_if(cards_.begin(), cards_.end(),
                                 [id](const CardPtr& value) {
                                     return value && value->id() == id;
                                 });
    return it == cards_.end() ? CardPtr{} : *it;
}

void CardPane::refreshCards() {
    if (canonical_) {
        cards_ = canonical_->cards();
        selected_ = canonical_->selectionIndices();
        return;
    }
    cards_.clear();
    if (!summary_) return;
    if (owner_type_ == OwnerType::Summary) {
        switch (card_type_) {
        case CardType::Cast: appendCards(cards_, summary_->casts()); break;
        case CardType::Skill: appendCards(cards_, summary_->skills()); break;
        case CardType::Item: appendCards(cards_, summary_->items()); break;
        case CardType::Beast: appendCards(cards_, summary_->beasts()); break;
        case CardType::Info: appendCards(cards_, summary_->infos()); break;
        }
    } else if (cast_owner_) {
        switch (card_type_) {
        case CardType::Cast: break;
        case CardType::Skill: appendCards(cards_, cast_owner_->skills()); break;
        case CardType::Item: appendCards(cards_, cast_owner_->items()); break;
        case CardType::Beast: appendCards(cards_, cast_owner_->beasts()); break;
        case CardType::Info: break;
        }
    }
}

void CardPane::refreshSnapshot() {
    if (!canonical_) return;
    cards_ = canonical_->cards();
    selected_ = canonical_->selectionIndices();
    status_line_ = canonical_->statusLine();
    component_ = canonical_->pane();
    table_ = canonical_->table();
}

String CardPane::formatStatus() const {
    if (view_mode_ == CViewMode::LIFE)
        return "Cards: " + numberText(cards_.size());
    return "Cards: " + numberText(cards_.size()) +
           ", selected: " + numberText(selected_.size());
}

void CardPane::refreshStatusLine() {
    if (canonical_) {
        status_line_ = canonical_->statusLine();
        return;
    }
    status_line_ = formatStatus();
}

void CardPane::refreshTable() {
    if (canonical_) {
        canonical_->refresh();
        table_ = canonical_->table();
        refreshSnapshot();
        return;
    }
    if (!table_) {
        refreshStatusLine();
        return;
    }
    std::vector<lib::TableRow> rows;
    std::vector<customtable::TableRowState> states;
    rows.reserve(cards_.size());
    states.reserve(cards_.size());
    for (const auto& value : cards_) {
        if (!value) continue;
        lib::TableRow row;
        row.cells.push_back({numberText(value->id()), {}, {}, true});
        row.cells.push_back({value->name(), {}, {}, true});
        String level;
        if (const auto cast = std::dynamic_pointer_cast<CastCard>(value))
            level = numberText(cast->level());
        else if (const auto skill = std::dynamic_pointer_cast<SkillCard>(value))
            level = numberText(skill->level());
        row.cells.push_back({std::move(level), {}, {}, true});
        row.cells.push_back({value->desc(), {}, {}, true});
        String use_count;
        if (const auto counter = value->useCounter()) {
            if (const auto cast = std::dynamic_pointer_cast<CastCard>(value))
                use_count = numberText(counter->get(usecounter::CastId{cast->id()}));
            else if (const auto skill = std::dynamic_pointer_cast<SkillCard>(value))
                use_count = numberText(counter->get(usecounter::SkillId{skill->id()}));
            else if (const auto item = std::dynamic_pointer_cast<ItemCard>(value))
                use_count = numberText(counter->get(usecounter::ItemId{item->id()}));
            else if (const auto beast = std::dynamic_pointer_cast<BeastCard>(value))
                use_count = numberText(counter->get(usecounter::BeastId{beast->id()}));
            else if (const auto info = std::dynamic_pointer_cast<InfoCard>(value))
                use_count = numberText(counter->get(usecounter::InfoId{info->id()}));
        }
        row.cells.push_back({std::move(use_count), {}, {}, true});
        customtable::TableRowState state;
        state.data = value;
        state.text.reserve(row.cells.size());
        for (const auto& cell : row.cells) state.text.push_back(cell.text);
        rows.push_back(std::move(row));
        states.push_back(std::move(state));
    }
    if (!table_->replaceRows(std::move(rows), selected_, std::move(states))) return;
    table_->redraw();
    if (image_list_) {
        const auto image_provider = view_mode_ == CViewMode::LIFE &&
                                            comm_.cardLifeImageData
                                        ? comm_.cardLifeImageData
                                        : comm_.cardImageData;
        image_list_->refresh(
            cards_,
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
            [](const CardPtr& value) { return value ? value->name() : String{}; });
    }
    refreshStatusLine();
}

void CardPane::refresh() {
    if (canonical_) {
        canonical_->refreshAll(summary_, owner_);
        component_ = canonical_->pane();
        table_ = canonical_->table();
        refreshSnapshot();
        return;
    }
    refreshCards();
    selected_.clear();
    refreshTable();
}

void CardPane::refreshAll(const SummaryPtr& summary, const CWXPathPtr& owner) {
    if (canonical_) {
        summary_ = summary;
        owner_ = owner;
        cast_owner_ = std::dynamic_pointer_cast<CastCard>(owner_);
        canonical_->refreshAll(summary_, owner_);
        component_ = canonical_->pane();
        table_ = canonical_->table();
        refreshSnapshot();
        return;
    }
    summary_ = summary;
    owner_ = owner;
    cast_owner_ = std::dynamic_pointer_cast<CastCard>(owner_);
    refresh();
}

void CardPane::constructView() {
    if (canonical_) {
        canonical_->updateLayout();
        return;
    }
    if (!component_) construct(parent_);
    else refreshTable();
}

void CardPane::showCardLife() {
    if (canonical_) {
        canonical_->showCardLife();
        view_mode_ = CViewMode::LIFE;
        refreshSnapshot();
        return;
    }
    view_mode_ = CViewMode::LIFE;
    if (table_) {
        table_->set_visible(false);
        table_->setHeaderVisible(false);
    }
    if (image_list_) image_list_->set_visible(true);
    refreshStatusLine();
}

void CardPane::showCardList() {
    if (canonical_) {
        canonical_->showCardList();
        view_mode_ = CViewMode::CARD;
        refreshSnapshot();
        return;
    }
    view_mode_ = CViewMode::CARD;
    if (table_) {
        table_->set_visible(true);
        table_->setHeaderVisible(false);
    }
    if (image_list_) image_list_->set_visible(false);
    refreshStatusLine();
}

void CardPane::showCardTable() {
    if (canonical_) {
        canonical_->showCardTable();
        view_mode_ = CViewMode::TABLE;
        refreshSnapshot();
        return;
    }
    view_mode_ = CViewMode::TABLE;
    if (table_) {
        table_->set_visible(true);
        table_->setHeaderVisible(true);
    }
    if (image_list_) image_list_->set_visible(false);
    refreshStatusLine();
}

void CardPane::installInputHandlers() {
    if (!table_) return;
    const auto oldKey = table_->on_key_down;
    table_->on_key_down = [this, oldKey](lib::KeyEvent& event) {
        if (oldKey) oldKey(event);
        if (event.handled) return;
        SelectionEvent selection{event.modifiers, false};
        const auto control = hasModifier(event.modifiers, lib::Modifiers::Control);
        if (control && event.key == lib::Key::C) copy(selection);
        else if (control && event.key == lib::Key::X) cut(selection);
        else if (control && event.key == lib::Key::V) paste(selection);
        else if (control && event.key == lib::Key::D) clone(selection);
        else if (event.key == lib::Key::Delete) del(selection);
        else if (event.key == lib::Key::F2 || event.key == lib::Key::Enter) edit();
        else if (event.key == lib::Key::Up && canUp()) up();
        else if (event.key == lib::Key::Down && canDown()) down();
        else return;
        // ペインがショートカットを認識したら、そのキーを消費する。クリ
        // ップボードのコールバックはSelectionEvent::handled
        // を必ずしも変えないため、Ctrl+C/X/V/Dを未処
        // 理のままにするとウィンドウメニューから二重配送される。
        event.handled = true;
    };
    const auto oldPointer = table_->on_pointer;
    table_->on_pointer = [this, oldPointer](lib::PointerEvent& event) {
        if (oldPointer) oldPointer(event);
        if (event.handled) return;
        if (event.action == lib::PointerAction::DoubleClick && canEdit()) edit();
    };
    table_->on_cell_activate = [this](const std::size_t row, const std::size_t) {
        if (select(static_cast<std::ptrdiff_t>(row)) && canEdit()) edit();
    };
}

void CardPane::bindDragDrop() {
    if (!table_ || !comm_.services.drag_drop) return;
    try {
        lib::DropTargetBinding target;
        target.target = std::static_pointer_cast<lib::Widget>(table_);
        target.accepted_formats = {"text/plain", "text/uri-list", "application/x-cwxeditor-table-indexes"};
        target.on_enter = [this](lib::DragEvent& event) {
            event.selected = event.allowed;
            event.handled = true;
        };
        target.on_drop = [this](lib::DragEvent& event) {
            if (!read_only_ && !event.data.files.empty() && comm_.addScenarioFiles) {
                std::vector<String> files;
                for (const auto& file : event.data.files)
                    files.push_back(structs::pathToUtf8(file));
                comm_.addScenarioFiles(files);
                event.selected = lib::DropOperation::Link;
            } else if (canDoImport()) {
                doImport();
                event.selected = lib::DropOperation::Copy;
            }
            event.handled = true;
        };
        comm_.services.drag_drop->bind_target(std::move(target));
    } catch (...) {
    }
}

void CardPane::handleSelection(const std::vector<std::size_t>& indices) {
    selected_.clear();
    for (const auto index : indices)
        if (index < cards_.size()) selected_.push_back(index);
    if (image_list_) {
        image_list_->deselectAll();
        std::vector<int> image_selection;
        image_selection.reserve(selected_.size());
        for (const auto index : selected_)
            image_selection.push_back(static_cast<int>(index));
        image_list_->selectionIndices(image_selection);
    }
    refreshStatusLine();
    if (comm_.refreshToolBar) comm_.refreshToolBar();
}

void CardPane::handleEdit(const std::size_t row, const std::size_t column, String value) {
    if (read_only_ || row >= cards_.size()) return;
    const auto& valueCard = cards_[row];
    if (!valueCard) return;
    if (column == 1U) valueCard->name(std::move(value));
    else if (column == 2U) valueCard->desc(std::move(value));
    refreshStatusLine();
}

bool CardPane::select(const std::ptrdiff_t index) {
    if (canonical_) {
        canonical_->select(index);
        refreshSnapshot();
        return index >= 0 && static_cast<std::size_t>(index) < cards_.size();
    }
    if (index < 0 || static_cast<std::size_t>(index) >= cards_.size()) {
        selected_.clear();
        refreshStatusLine();
        return false;
    }
    if (view_mode_ == CViewMode::LIFE && image_list_) {
        image_list_->deselectAll();
        image_list_->setCursor(static_cast<int>(index), true, true);
        image_list_->callSelectChanged();
    } else if (table_) {
        table_->select(static_cast<std::size_t>(index));
    } else {
        return false;
    }
    return true;
}

bool CardPane::selectById(const std::uint64_t id) {
    if (canonical_) {
        canonical_->selectID(id);
        refreshSnapshot();
        return std::find_if(cards_.begin(), cards_.end(), [id](const CardPtr& value) {
                   return value && value->id() == id;
               }) != cards_.end();
    }
    const auto it = std::find_if(cards_.begin(), cards_.end(),
                                 [id](const CardPtr& value) {
                                     return value && value->id() == id;
                                 });
    return it == cards_.end()
               ? false
               : select(static_cast<std::ptrdiff_t>(std::distance(cards_.begin(), it)));
}

std::vector<CardPtr> CardPane::selectedCards() const {
    if (canonical_) return canonical_->selectedCards();
    std::vector<CardPtr> result;
    result.reserve(selected_.size());
    for (const auto index : selected_)
        if (index < cards_.size() && cards_[index]) result.push_back(cards_[index]);
    return result;
}

std::uint64_t CardPane::nextId() const {
    if (!summary_) return 1U;
    if (owner_type_ == OwnerType::Summary) {
        switch (card_type_) {
        case CardType::Cast: return summary_->newId<CastCard>();
        case CardType::Skill: return summary_->newId<SkillCard>();
        case CardType::Item: return summary_->newId<ItemCard>();
        case CardType::Beast: return summary_->newId<BeastCard>();
        case CardType::Info: return summary_->newId<InfoCard>();
        }
    }
    std::uint64_t result = 1U;
    for (const auto& value : cards_)
        if (value) result = std::max(result, value->id() + 1U);
    return result;
}

CardPtr CardPane::duplicateCard(const CardPtr& source) const {
    if (!source) return {};
    return source->dup();
}

bool CardPane::addCard(const CardPtr& value, const std::ptrdiff_t index) {
    if (!value || !summary_ || read_only_) return false;
    switch (card_type_) {
    case CardType::Cast: {
        const auto typed = std::dynamic_pointer_cast<CastCard>(value);
        if (!typed) return false;
        if (owner_type_ == OwnerType::Summary) {
            if (index < 0) summary_->add(typed, true);
            else summary_->insert(index, typed);
        }
        return true;
    }
    case CardType::Skill: {
        const auto typed = std::dynamic_pointer_cast<SkillCard>(value);
        if (!typed) return false;
        if (owner_type_ == OwnerType::Summary) {
            if (index < 0) summary_->add(typed, true);
            else summary_->insert(index, typed);
        } else if (cast_owner_) {
            if (index < 0) cast_owner_->add(typed, true);
            else cast_owner_->insert(static_cast<std::size_t>(index), typed, true);
        }
        return true;
    }
    case CardType::Item: {
        const auto typed = std::dynamic_pointer_cast<ItemCard>(value);
        if (!typed) return false;
        if (owner_type_ == OwnerType::Summary) {
            if (index < 0) summary_->add(typed, true);
            else summary_->insert(index, typed);
        } else if (cast_owner_) {
            if (index < 0) cast_owner_->add(typed, true);
            else cast_owner_->insert(static_cast<std::size_t>(index), typed, true);
        }
        return true;
    }
    case CardType::Beast: {
        const auto typed = std::dynamic_pointer_cast<BeastCard>(value);
        if (!typed) return false;
        if (owner_type_ == OwnerType::Summary) {
            if (index < 0) summary_->add(typed, true);
            else summary_->insert(index, typed);
        } else if (cast_owner_) {
            if (index < 0) cast_owner_->add(typed, true);
            else cast_owner_->insert(static_cast<std::size_t>(index), typed, true);
        }
        return true;
    }
    case CardType::Info: {
        const auto typed = std::dynamic_pointer_cast<InfoCard>(value);
        if (!typed) return false;
        if (owner_type_ == OwnerType::Summary) {
            if (index < 0) summary_->add(typed, true);
            else summary_->insert(index, typed);
        }
        return true;
    }
    }
    return false;
}

void CardPane::removeCard(const CardPtr& value) {
    if (!value || !summary_ || read_only_) return;
    switch (card_type_) {
    case CardType::Cast:
        if (auto typed = std::dynamic_pointer_cast<CastCard>(value);
            owner_type_ == OwnerType::Summary && typed) summary_->remove(typed);
        break;
    case CardType::Skill:
        if (auto typed = std::dynamic_pointer_cast<SkillCard>(value); typed) {
            if (owner_type_ == OwnerType::Summary) summary_->remove(typed);
            else if (cast_owner_) cast_owner_->remove(typed);
        }
        break;
    case CardType::Item:
        if (auto typed = std::dynamic_pointer_cast<ItemCard>(value); typed) {
            if (owner_type_ == OwnerType::Summary) summary_->remove(typed);
            else if (cast_owner_) cast_owner_->remove(typed);
        }
        break;
    case CardType::Beast:
        if (auto typed = std::dynamic_pointer_cast<BeastCard>(value); typed) {
            if (owner_type_ == OwnerType::Summary) summary_->remove(typed);
            else if (cast_owner_) cast_owner_->remove(typed);
        }
        break;
    case CardType::Info:
        if (auto typed = std::dynamic_pointer_cast<InfoCard>(value);
            owner_type_ == OwnerType::Summary && typed) summary_->remove(typed);
        break;
    }
}

void CardPane::swapCard(const std::size_t first, const std::size_t second) {
    if (!summary_ || first == second) return;
    if (owner_type_ == OwnerType::Summary) {
        switch (card_type_) {
        case CardType::Cast: summary_->swapCast(first, second); break;
        case CardType::Skill: summary_->swapSkill(first, second); break;
        case CardType::Item: summary_->swapItem(first, second); break;
        case CardType::Beast: summary_->swapBeast(first, second); break;
        case CardType::Info: summary_->swapInfo(first, second); break;
        }
    } else if (cast_owner_) {
        switch (card_type_) {
        case CardType::Skill: cast_owner_->swapSkill(first, second); break;
        case CardType::Item: cast_owner_->swapItem(first, second); break;
        case CardType::Beast: cast_owner_->swapBeast(first, second); break;
        case CardType::Cast:
        case CardType::Info: break;
        }
    }
}

void CardPane::rebuildOwnerOrder() {
    if (!summary_ || cards_.empty()) return;
    if (owner_type_ == OwnerType::Summary) {
        switch (card_type_) {
        case CardType::Cast: {
            auto values = summary_->casts();
            for (std::size_t target = 0; target < cards_.size(); ++target) {
                const auto current = indexOf(values, cards_[target]);
                if (current >= 0 && static_cast<std::size_t>(current) != target) {
                    summary_->swapCast(current, static_cast<std::ptrdiff_t>(target));
                    values = summary_->casts();
                }
            }
            break;
        }
        case CardType::Skill: {
            auto values = summary_->skills();
            for (std::size_t target = 0; target < cards_.size(); ++target) {
                const auto current = indexOf(values, cards_[target]);
                if (current >= 0 && static_cast<std::size_t>(current) != target) {
                    summary_->swapSkill(current, static_cast<std::ptrdiff_t>(target));
                    values = summary_->skills();
                }
            }
            break;
        }
        case CardType::Item: {
            auto values = summary_->items();
            for (std::size_t target = 0; target < cards_.size(); ++target) {
                const auto current = indexOf(values, cards_[target]);
                if (current >= 0 && static_cast<std::size_t>(current) != target) {
                    summary_->swapItem(current, static_cast<std::ptrdiff_t>(target));
                    values = summary_->items();
                }
            }
            break;
        }
        case CardType::Beast: {
            auto values = summary_->beasts();
            for (std::size_t target = 0; target < cards_.size(); ++target) {
                const auto current = indexOf(values, cards_[target]);
                if (current >= 0 && static_cast<std::size_t>(current) != target) {
                    summary_->swapBeast(current, static_cast<std::ptrdiff_t>(target));
                    values = summary_->beasts();
                }
            }
            break;
        }
        case CardType::Info: {
            auto values = summary_->infos();
            for (std::size_t target = 0; target < cards_.size(); ++target) {
                const auto current = indexOf(values, cards_[target]);
                if (current >= 0 && static_cast<std::size_t>(current) != target) {
                    summary_->swapInfo(current, static_cast<std::ptrdiff_t>(target));
                    values = summary_->infos();
                }
            }
            break;
        }
        }
    } else if (cast_owner_) {
        switch (card_type_) {
        case CardType::Skill: {
            auto values = cast_owner_->skills();
            for (std::size_t target = 0U; target < cards_.size(); ++target) {
                const auto current = indexOf(values, cards_[target]);
                if (current >= 0 && static_cast<std::size_t>(current) != target) {
                    cast_owner_->swapSkill(current, static_cast<std::ptrdiff_t>(target));
                    values = cast_owner_->skills();
                }
            }
            break;
        }
        case CardType::Item: {
            auto values = cast_owner_->items();
            for (std::size_t target = 0U; target < cards_.size(); ++target) {
                const auto current = indexOf(values, cards_[target]);
                if (current >= 0 && static_cast<std::size_t>(current) != target) {
                    cast_owner_->swapItem(current, static_cast<std::ptrdiff_t>(target));
                    values = cast_owner_->items();
                }
            }
            break;
        }
        case CardType::Beast: {
            auto values = cast_owner_->beasts();
            for (std::size_t target = 0U; target < cards_.size(); ++target) {
                const auto current = indexOf(values, cards_[target]);
                if (current >= 0 && static_cast<std::size_t>(current) != target) {
                    cast_owner_->swapBeast(current, static_cast<std::ptrdiff_t>(target));
                    values = cast_owner_->beasts();
                }
            }
            break;
        }
        case CardType::Cast:
        case CardType::Info: break;
        }
    }
}

void CardPane::create() {
    if (canonical_) {
        canonical_->create();
        refreshSnapshot();
        return;
    }
    if (!canCreate()) return;
    const auto value = typedCard(card_type_, nextId(),
                                 "New " + cardName(card_type_, prop_));
    if (!addCard(value)) return;
    refresh();
    if (!cards_.empty()) select(static_cast<std::ptrdiff_t>(cards_.size() - 1U));
}

bool CardPane::canDoTCPD() const noexcept {
    if (canonical_) return canonical_->canDoTCPD();
    return !read_only_ && summary_ != nullptr && !selected_.empty();
}

void CardPane::notifyAction(const std::function<void()>& action,
                            SelectionEvent& event) {
    if (!action) return;
    action();
    event.handled = true;
}

void CardPane::copy(SelectionEvent& event) {
    if (canonical_) {
        canonical_cardpane::SelectionEvent translated{event.modifiers, event.handled};
        canonical_->copy(translated);
        event.handled = translated.handled;
        refreshSnapshot();
        return;
    }
    if (!canDoC()) return;
    if (comm_.services.clipboard) {
        lib::ClipboardData data;
        data.text = joinPaths(selectedCards());
        comm_.services.clipboard->write(data);
    }
    event.handled = true;
}

void CardPane::cut(SelectionEvent& event) {
    if (canonical_) {
        canonical_cardpane::SelectionEvent translated{event.modifiers, event.handled};
        canonical_->cut(translated);
        event.handled = translated.handled;
        refreshSnapshot();
        return;
    }
    if (!canDoTCPD()) return;
    copy(event);
    del(event);
}

void CardPane::paste(SelectionEvent& event) {
    if (canonical_) {
        canonical_cardpane::SelectionEvent translated{event.modifiers, event.handled};
        canonical_->paste(translated);
        event.handled = translated.handled;
        refreshSnapshot();
        return;
    }
    if (!canDoP() || !comm_.services.clipboard) return;
    const auto data = comm_.services.clipboard->read();
    if (data.text.empty() && data.files.empty()) return;
    clone(event);
}

void CardPane::del(SelectionEvent& event) {
    if (canonical_) {
        canonical_cardpane::SelectionEvent translated{event.modifiers, event.handled};
        canonical_->del(translated);
        event.handled = translated.handled;
        refreshSnapshot();
        return;
    }
    notifyAction([this] {
        if (!canDoTCPD()) return;
        const auto values = selectedCards();
        for (const auto& value : values) removeCard(value);
        refresh();
    }, event);
}

void CardPane::clone(SelectionEvent& event) {
    if (canonical_) {
        canonical_cardpane::SelectionEvent translated{event.modifiers, event.handled};
        canonical_->clone(translated);
        event.handled = translated.handled;
        refreshSnapshot();
        return;
    }
    notifyAction([this] {
        if (!canDoClone()) return;
        const auto values = selectedCards();
        const auto oldSize = cards_.size();
        for (const auto& value : values) addCard(duplicateCard(value));
        refresh();
        if (!cards_.empty())
            select(static_cast<std::ptrdiff_t>(std::min(oldSize, cards_.size() - 1U)));
    }, event);
}

bool CardPane::canEditHand() const noexcept {
    if (canonical_) return canonical_->canEditHand();
    return card_type_ == CardType::Cast && selected_.size() == 1U &&
           std::dynamic_pointer_cast<CastCard>(card(selected_.front())) != nullptr;
}

void CardPane::editHand() {
    if (canonical_) {
        const auto canonical = canonical_;
        const std::weak_ptr<int> life = callback_lifetime_;
        canonical->editHand();
        if (!life.expired()) refreshSnapshot();
        return;
    }
    if (!canEditHand()) return;
    const auto cast = std::dynamic_pointer_cast<CastCard>(card(selected_.front()));
    if (open_hand_) open_hand_(cast);
    else if (comm_.openHands && summary_) comm_.openHands(prop_, summary_, cast, toc_, true);
}

bool CardPane::canEditUseEvent() const noexcept {
    if (canonical_) return canonical_->canEditUseEvent();
    return (card_type_ == CardType::Skill || card_type_ == CardType::Item ||
            card_type_ == CardType::Beast) && selected_.size() == 1U &&
           card(selected_.front()) != nullptr;
}

void CardPane::editUseEvent(const bool canDuplicate) {
    if (canonical_) {
        canonical_->editUseEvent(canDuplicate);
        return;
    }
    if (!canEditUseEvent()) return;
    const auto effect = std::dynamic_pointer_cast<EffectCard>(card(selected_.front()));
    if (comm_.openUseEvents && effect && summary_)
        comm_.openUseEvents(prop_, summary_, effect, true, canDuplicate);
    else if (comm_.editEvent && effect) comm_.editEvent(effect, canDuplicate);
}

bool CardPane::canSelectConnectedResource() const noexcept {
    if (canonical_) return canonical_->canSelectConnectedResource();
    return !selected_.empty() && comm_.openCWXPath != nullptr;
}

void CardPane::selectConnectedResource() {
    if (canonical_) {
        canonical_->selectConnectedResource();
        return;
    }
    if (!canSelectConnectedResource()) return;
    const auto value = card(selected_.front());
    if (value) comm_.openCWXPath(cardPath(value), true);
}

bool CardPane::canRemoveRef() const noexcept {
    if (canonical_) return canonical_->canRemoveRef();
    return owner_type_ == OwnerType::Cast &&
           (card_type_ == CardType::Skill || card_type_ == CardType::Item ||
            card_type_ == CardType::Beast) && !selected_.empty() && !read_only_;
}

void CardPane::removeRef() {
    if (canonical_) {
        const auto canonical = canonical_;
        const std::weak_ptr<int> life = callback_lifetime_;
        canonical->removeRef();
        if (!life.expired()) refreshSnapshot();
        return;
    }
    if (!canRemoveRef()) return;
    for (const auto& value : selectedCards()) {
        if (auto effect = std::dynamic_pointer_cast<EffectCard>(value)) {
            if (card_type_ == CardType::Skill) {
                if (auto skill = std::dynamic_pointer_cast<SkillCard>(effect)) skill->linkId(0U);
            } else if (card_type_ == CardType::Item) {
                if (auto item = std::dynamic_pointer_cast<ItemCard>(effect)) item->linkId(0U);
            } else if (card_type_ == CardType::Beast) {
                if (auto beast = std::dynamic_pointer_cast<BeastCard>(effect)) beast->linkId(0U);
            }
        }
    }
    refresh();
}

bool CardPane::canWriteComment() const noexcept {
    if (canonical_) return canonical_->canWriteComment();
    return !read_only_ && selected_.size() == 1U;
}
void CardPane::incSearch() {
    const auto canonical = canonical_;
    if (canonical) canonical->incSearch();
}

void CardPane::writeComment() {
    if (canonical_) {
        const auto canonical = canonical_;
        const std::weak_ptr<int> life = callback_lifetime_;
        canonical->writeComment();
        if (!life.expired()) refreshSnapshot();
        return;
    }
    if (!canWriteComment()) return;
    const auto value = card(selected_.front());
    if (comm_.editComment && value) {
        const auto next = comm_.editComment(summary_, value, value->comment());
        if (next) value->comment(*next);
    }
}

bool CardPane::canEditEvent() const noexcept {
    if (canonical_) return canonical_->canEditUseEvent();
    return canEditUseEvent();
}

void CardPane::editEvent(const bool canDuplicate) {
    if (canonical_) {
        canonical_->editUseEvent(canDuplicate);
        return;
    }
    if (canEditUseEvent()) editUseEvent(canDuplicate);
}

bool CardPane::canReplaceID() const noexcept {
    if (canonical_) return canonical_->canReplaceID();
    return !read_only_ && summary_ != nullptr && !selected_.empty();
}

bool CardPane::canUndo() const noexcept {
    return canonical_ && canonical_->canUndo();
}

bool CardPane::canRedo() const noexcept {
    return canonical_ && canonical_->canRedo();
}

void CardPane::undo() {
    if (!canonical_) return;
    canonical_->undo();
    refreshSnapshot();
}

void CardPane::redo() {
    if (!canonical_) return;
    canonical_->redo();
    refreshSnapshot();
}

void CardPane::setUndoLimit(const std::size_t value) {
    prop_.undoMax = value;
    if (canonical_) canonical_->setUndoLimit(value);
}

void CardPane::replaceID() {
    if (canonical_) {
        canonical_->replaceID();
        refreshSnapshot();
        return;
    }
    if (!canReplaceID()) return;
    reNumbering(1U, 1U);
}

void CardPane::reNumbering() {
    // 選択カードへのコマンドは対話式のままとする。全件への操作は別経路。
    if (canonical_) {
        canonical_->reNumbering();
        refreshSnapshot();
        return;
    }
    reNumbering(1U, 1U);
}

void CardPane::reNumbering(const std::uint64_t first, const std::uint64_t step) {
    if (canonical_) {
        // Dの全件の番号振り直しは、ダイアログを開かず最
        // 初のカードから始める。ほかの刻み値は既存の対話
        // 経路を維持し、連続IDの一括操作だけを扱う。
        if (step == 1U) canonical_->reNumbering(0, first);
        else canonical_->reNumbering();
        refreshSnapshot();
        return;
    }
    if (!canReNumbering()) return;
    auto id = first;
    for (const auto& value : cards_) {
        if (value) value->id(id);
        id += step;
    }
    if (summary_) summary_->changed();
    refreshTable();
}

bool CardPane::canUp() const noexcept {
    if (canonical_) return canonical_->canUp();
    return !read_only_ && selected_.size() == 1U && selected_.front() > 0U;
}

bool CardPane::canDown() const noexcept {
    if (canonical_) return canonical_->canDown();
    return !read_only_ && selected_.size() == 1U &&
           selected_.front() + 1U < cards_.size();
}

void CardPane::up() {
    if (canonical_) {
        canonical_->up();
        refreshSnapshot();
        return;
    }
    if (!canUp()) return;
    const auto index = selected_.front();
    swapCard(index, index - 1U);
    std::swap(cards_[index], cards_[index - 1U]);
    refreshTable();
    select(static_cast<std::ptrdiff_t>(index - 1U));
}

void CardPane::down() {
    if (canonical_) {
        canonical_->down();
        refreshSnapshot();
        return;
    }
    if (!canDown()) return;
    const auto index = selected_.front();
    swapCard(index, index + 1U);
    std::swap(cards_[index], cards_[index + 1U]);
    refreshTable();
    select(static_cast<std::ptrdiff_t>(index + 1U));
}

String CardPane::cardPath(const CardPtr& value) const {
    if (!value) return {};
    auto result = value->cwxPath(true);
    if (result.empty()) result = cardCategory(card_type_) + ":id:" + numberText(value->id());
    return result;
}

std::vector<String> CardPane::openedCWXPath() const {
    if (canonical_) return canonical_->openedCWXPath();
    std::vector<String> result;
    for (const auto& value : selectedCards()) {
        const auto pathValue = cardPath(value);
        if (!pathValue.empty()) result.push_back(pathValue);
    }
    return result;
}

bool CardPane::openCWXPath(const StringView value, const bool shellActivate) {
    if (canonical_) return canonical_->openCWXPath(value, shellActivate);
    const auto category = path::cpcategory(value);
    if (category == cardCategory(card_type_) + "view") {
        if (comm_.forceFocus && component_)
            comm_.forceFocus(std::static_pointer_cast<lib::Widget>(component_), shellActivate);
        return true;
    }
    const auto base = cardCategory(card_type_);
    if (category != base && category != base + ":id") return false;
    const auto index = path::cpindex(value);
    const bool byId = category == base + ":id";
    bool selected = byId ? selectById(index) : select(static_cast<std::ptrdiff_t>(index));
    if (!selected) return false;
    const auto bottom = path::cpbottom(value);
    if (!bottom.empty() && comm_.openCWXPath)
        return comm_.openCWXPath(value, shellActivate);
    if (comm_.refreshToolBar) comm_.refreshToolBar();
    return true;
}

void CardPane::doImport() {
    if (canonical_) {
        const auto canonical = canonical_;
        const std::weak_ptr<int> life = callback_lifetime_;
        canonical->doImport();
        if (!life.expired()) refreshSnapshot();
        return;
    }
    if (!canDoImport() || !comm_.importCards) return;
    comm_.importCards(selectedCards());
}

void CardPane::edit() {
    if (canonical_) {
        const auto canonical = canonical_;
        const std::weak_ptr<int> life = callback_lifetime_;
        canonical->edit();
        if (!life.expired()) refreshSnapshot();
        return;
    }
    if (!canEdit()) return;
    const auto value = card(selected_.front());
    if (comm_.editCard && value)
        comm_.editCard(card_type_, summary_, owner_, value, read_only_, {}, {}, {}, false);
}

void CardPane::selectAll() {
    if (canonical_) {
        canonical_->selectAll();
        refreshSnapshot();
        return;
    }
    if (table_) table_->selectAll();
}

AreaTable::AreaTable(Commons comm, Props prop, const bool readOnly, SummaryPtr toc)
    : comm_(std::move(comm)), prop_(std::move(prop)), read_only_(readOnly || prop_.readOnly),
      toc_(std::move(toc)) {
    if (prop_.areaTableContext && prop_.editorProps) {
        canonical_ = std::make_shared<canonical_areatable::AreaTable>(
            *prop_.areaTableContext, *prop_.editorProps, read_only_, toc_.get());
    }
}

PanelPtr AreaTable::construct(const Parent& parent) {
    if (component_) return component_;
    parent_ = parent;
    if (canonical_) {
        canonical_->construct(parent);
        component_ = canonical_->panel();
        table_view_ = canonical_->table();
        if (summary_) canonical_->summary(summary_.get());
        status_line_ = canonical_->statusLine();
        return component_;
    }
    component_ = std::make_shared<lib::Panel>();
    component_->set_layout(std::make_shared<lib::FillLayout>());
    table_ = customtable::rangeSelectableTable(component_.get(),
                                                customtable::STYLE_MULTI_SELECT |
                                                customtable::STYLE_HEADER,
                                                comm_.services);
    table_view_ = table_;
    // Dの読み取り専用取り込み表は、ID・名前・使用数のコンパ
    // クトな列を使う。C++専用のPath列を置くと表示領域
    // より広くなり、ヘッダー下に横スクロールバーが常時出る。
    table_->addColumn(prop_.areaIdLabel, prop_.importAreaIdColumn);
    table_->addColumn(prop_.areaNameLabel, prop_.importAreaNameColumn);
    table_->addColumn(prop_.areaCountLabel, prop_.importAreaCountColumn);
    table_->addSelectionListener([this](const std::vector<std::size_t>& values) {
        selected_ = values.empty() ? std::optional<std::size_t>{}
                                   : std::optional<std::size_t>{values.front()};
        refreshStatusLine();
        if (comm_.refreshToolBar) comm_.refreshToolBar();
    });
    table_->on_cell_activate_with_modifiers =
        [this](const std::size_t row, const std::size_t, const lib::Modifiers modifiers) {
        if (row < rows_.size()) {
            selected_ = row;
            bool open_event = prop_.clickIsOpenEvent;
            if (hasModifier(modifiers, lib::Modifiers::Shift)) open_event = !open_event;
            if (open_event) openAreaEvent(true, false);
            else openAreaScene(true, false);
        }
    };
    refresh();
    if (parent && std::find(parent->children().begin(), parent->children().end(),
                            std::static_pointer_cast<lib::Widget>(component_)) ==
                       parent->children().end())
        parent->add(component_);
    return component_;
}

void AreaTable::reconstruct(const Parent& parent) {
    if (!component_) construct(parent);
    else refresh();
}

void AreaTable::summary(SummaryPtr value) {
    summary_ = std::move(value);
    if (canonical_) canonical_->summary(summary_.get());
    refresh();
}

void AreaTable::refreshTable() {
    rows_.clear();
    if (summary_) {
        for (std::size_t index = 0U; index < summary_->areas().size(); ++index)
            rows_.push_back({AreaKind::Area, index});
        for (std::size_t index = 0U; index < summary_->battles().size(); ++index)
            rows_.push_back({AreaKind::Battle, index});
        for (std::size_t index = 0U; index < summary_->packages().size(); ++index)
            rows_.push_back({AreaKind::Package, index});
    }
    if (selected_ && *selected_ >= rows_.size()) selected_.reset();
    if (!table_) {
        refreshStatusLine();
        return;
    }
    std::vector<lib::TableRow> table_rows;
    std::vector<customtable::TableRowState> states;
    table_rows.reserve(rows_.size());
    states.reserve(rows_.size());
    for (const auto rowRef : rows_) {
        lib::TableRow row;
        row.cells.push_back({rowId(rowRef), {}, {}, false});
        row.cells.push_back({rowName(rowRef), {}, {}, false});
        row.cells.push_back({rowUseCount(rowRef), {}, {}, false});
        customtable::TableRowState state;
        state.data = rowRef;
        state.text = {rowId(rowRef), rowName(rowRef), rowUseCount(rowRef)};
        table_rows.push_back(std::move(row));
        states.push_back(std::move(state));
    }
    std::vector<std::size_t> selection;
    if (selected_) selection.push_back(*selected_);
    if (!table_->replaceRows(std::move(table_rows), std::move(selection),
                             std::move(states))) return;
    table_->redraw();
    refreshStatusLine();
}

void AreaTable::refreshStatusLine() {
    status_line_ = ::cwxeditor::cwx::utils::tryFormat(
        prop_.areaStatus, prop_.areasTabName, rows_.size());
}

void AreaTable::refresh() {
    if (canonical_) {
        canonical_->refresh();
        component_ = canonical_->panel();
        table_view_ = canonical_->table();
        status_line_ = canonical_->statusLine();
        return;
    }
    refreshTable();
}

String AreaTable::rowName(const RowRef row) const {
    if (!summary_) return {};
    switch (row.kind) {
    case AreaKind::Area:
        return row.index < summary_->areas().size() && summary_->areas()[row.index]
                   ? summary_->areas()[row.index]->name() : String{};
    case AreaKind::Battle:
        return row.index < summary_->battles().size() && summary_->battles()[row.index]
                   ? summary_->battles()[row.index]->name() : String{};
    case AreaKind::Package:
        return row.index < summary_->packages().size() && summary_->packages()[row.index]
                   ? summary_->packages()[row.index]->name() : String{};
    }
    return {};
}

String AreaTable::rowId(const RowRef row) const {
    const auto value = rowArea(row);
    return value ? numberText(value->id()) : String{};
}

String AreaTable::rowUseCount(const RowRef row) const {
    if (!summary_) return {};
    const auto value = rowArea(row);
    const auto counter = summary_->useCounter();
    if (!value || !counter) return {};
    switch (row.kind) {
    case AreaKind::Area:
        return numberText(counter->get(usecounter::AreaId{value->id()}));
    case AreaKind::Battle:
        return numberText(counter->get(usecounter::BattleId{value->id()}));
    case AreaKind::Package:
        return numberText(counter->get(usecounter::PackageId{value->id()}));
    }
    return {};
}

String AreaTable::rowPath(const RowRef row) const {
    if (!summary_) return {};
    switch (row.kind) {
    case AreaKind::Area:
        return row.index < summary_->areas().size() && summary_->areas()[row.index]
                   ? summary_->areas()[row.index]->cwxPath(true) : String{};
    case AreaKind::Battle:
        return row.index < summary_->battles().size() && summary_->battles()[row.index]
                   ? summary_->battles()[row.index]->cwxPath(true) : String{};
    case AreaKind::Package:
        return row.index < summary_->packages().size() && summary_->packages()[row.index]
                   ? summary_->packages()[row.index]->cwxPath(true) : String{};
    }
    return {};
}

bool AreaTable::canOpenAreaScene() const noexcept {
    if (canonical_) return canonical_->canOpenAreaScene();
    return summary_ != nullptr && selected_.has_value() && *selected_ < rows_.size();
}

bool AreaTable::canOpenAreaEvent() const noexcept {
    if (canonical_) return canonical_->canOpenAreaEvent();
    return canOpenAreaScene();
}

bool AreaTable::openSelected(const bool scene, const bool shellActivate,
                              const bool canDuplicate) {
    if (!canOpenAreaScene()) return false;
    const auto row = rows_[*selected_];
    if (const auto area = rowArea(row); area && comm_.openAreaFrom) {
        comm_.openAreaFrom(summary_, area, scene, shellActivate, canDuplicate);
        return true;
    }
    const auto value = rowPath(rows_[*selected_]);
    if (value.empty()) return false;
    if (comm_.openCWXPath) comm_.openCWXPath(value, shellActivate);
    return true;
}

area::AbstractAreaPtr AreaTable::rowArea(const RowRef& value) const {
    if (!summary_) return {};
    switch (value.kind) {
    case AreaKind::Area:
        return value.index < summary_->areas().size() ? summary_->areas()[value.index]
                                                       : area::AbstractAreaPtr{};
    case AreaKind::Battle:
        return value.index < summary_->battles().size() ? summary_->battles()[value.index]
                                                         : area::AbstractAreaPtr{};
    case AreaKind::Package:
        return value.index < summary_->packages().size() ? summary_->packages()[value.index]
                                                          : area::AbstractAreaPtr{};
    }
    return {};
}

void AreaTable::openAreaScene(const bool shellActivate, const bool canDuplicate) {
    if (canonical_) {
        canonical_->openAreaScene(shellActivate, canDuplicate);
        return;
    }
    (void)openSelected(true, shellActivate, canDuplicate);
}

void AreaTable::openAreaEvent(const bool shellActivate, const bool canDuplicate) {
    if (canonical_) {
        canonical_->openAreaEvent(shellActivate, canDuplicate);
        return;
    }
    (void)openSelected(false, shellActivate, canDuplicate);
}

void AreaTable::edit() {
    // DのCardWindowは、読み取り専用の取り込み元でも選択ペインの
    // プロパティコマンドを転送する。標準表示側がSummaryを区別する。
    const auto view = canonical_;
    if (view) {
        if (view->canEdit()) view->edit();
        return;
    }
    if (canOpenAreaScene()) openAreaScene(true);
}

void AreaTable::editSummary(const Parent&) {
    if (canonical_) {
        canonical_->editSummary(parent_);
        return;
    }
    if (comm_.editSummary) comm_.editSummary();
}

void AreaTable::doImport() {
    const auto canonical = canonical_;
    if (canonical && canonical->canDoImport()) canonical->doImport();
}

bool AreaTable::canDoImport() const noexcept {
    return canonical_ && canonical_->canDoImport();
}

void AreaTable::changeVHSide() {
    if (canonical_) {
        canonical_->changeVHSide();
        component_ = canonical_->panel();
        table_view_ = canonical_->table();
        return;
    }
    if (comm_.changeVHSide) comm_.changeVHSide();
}

bool AreaTable::openCWXPath(const StringView value, const bool shellActivate) {
    if (!summary_) return false;
    if (canonical_) {
        const auto category = path::cpcategory(value);
        const auto index = path::cpindex(value);
        area::AbstractAreaPtr target;
        const auto by_id = category.ends_with(":id");
        const auto find_target = [index, by_id](const auto& values) -> area::AbstractAreaPtr {
            if (!by_id)
                return index < values.size() ? values[index] : area::AbstractAreaPtr{};
            const auto found = std::find_if(values.begin(), values.end(),
                [index](const auto& entry) { return entry && entry->id() == index; });
            return found == values.end() ? area::AbstractAreaPtr{} : *found;
        };
        if (category == "area" || category == "area:id")
            target = find_target(summary_->areas());
        else if (category == "battle" || category == "battle:id")
            target = find_target(summary_->battles());
        else if (category == "package" || category == "package:id")
            target = find_target(summary_->packages());
        if (!target || !canonical_->select(target)) return false;
        if (comm_.openCWXPath && !path::cpempty(path::cpbottom(value)))
            comm_.openCWXPath(value, shellActivate);
        status_line_ = canonical_->statusLine();
        return true;
    }
    const auto category = path::cpcategory(value);
    RowRef target;
    bool found = false;
    if (category == "area" || category == "area:id") {
        const auto index = path::cpindex(value);
        if (category == "area:id") {
            for (std::size_t i = 0U; i < rows_.size(); ++i)
                if (rows_[i].kind == AreaKind::Area &&
                    rowPath(rows_[i]).find(":" + numberText(index)) != String::npos) {
                    target = rows_[i]; selected_ = i; found = true; break;
                }
        } else if (index < summary_->areas().size()) {
            target = {AreaKind::Area, index};
            selected_ = std::find(rows_.begin(), rows_.end(), target) - rows_.begin();
            found = true;
        }
    } else if (category == "battle" || category == "battle:id") {
        const auto index = path::cpindex(value);
        if (index < summary_->battles().size() && category == "battle") {
            target = {AreaKind::Battle, index};
            selected_ = std::find(rows_.begin(), rows_.end(), target) - rows_.begin();
            found = true;
        }
    } else if (category == "package" || category == "package:id") {
        const auto index = path::cpindex(value);
        if (index < summary_->packages().size() && category == "package") {
            target = {AreaKind::Package, index};
            selected_ = std::find(rows_.begin(), rows_.end(), target) - rows_.begin();
            found = true;
        }
    }
    if (!found) return false;
    if (table_ && selected_) table_->select(*selected_);
    if (comm_.openCWXPath && !path::cpempty(path::cpbottom(value)))
        comm_.openCWXPath(value, shellActivate);
    return true;
}

bool AreaTable::canDoTCPD() const noexcept {
    if (canonical_) return canonical_->canDoTCPD();
    return !read_only_ && summary_ != nullptr && selected_.has_value();
}

void AreaTable::notifyAction(const std::function<void()>& action,
                             SelectionEvent& event) {
    if (!action || !canDoTCPD()) return;
    action();
    event.handled = true;
}

void AreaTable::copy(SelectionEvent& event) {
    if (canonical_) {
        canonical_areatable::AreaTableSelectionEvent converted{"copy", false};
        canonical_->copy(converted);
        event.handled = converted.handled;
        status_line_ = canonical_->statusLine();
        return;
    }
    if (!canDoC() || !comm_.services.clipboard || !selected_) return;
    lib::ClipboardData data;
    data.text = rowPath(rows_[*selected_]);
    comm_.services.clipboard->write(data);
    event.handled = true;
}

void AreaTable::cut(SelectionEvent& event) {
    if (canonical_) {
        canonical_areatable::AreaTableSelectionEvent converted{"cut", false};
        canonical_->cut(converted);
        event.handled = converted.handled;
        status_line_ = canonical_->statusLine();
        return;
    }
    copy(event);
    if (event.handled) del(event);
}

void AreaTable::paste(SelectionEvent& event) {
    if (canonical_) {
        canonical_areatable::AreaTableSelectionEvent converted{"paste", false};
        canonical_->paste(converted);
        event.handled = converted.handled;
        status_line_ = canonical_->statusLine();
        return;
    }
    notifyAction([this] { if (summary_) summary_->changed(); }, event);
}

void AreaTable::del(SelectionEvent& event) {
    if (canonical_) {
        canonical_areatable::AreaTableSelectionEvent converted{"delete", false};
        canonical_->del(converted);
        event.handled = converted.handled;
        status_line_ = canonical_->statusLine();
        return;
    }
    notifyAction([this] {
        if (!summary_ || !selected_ || *selected_ >= rows_.size()) return;
        const auto row = rows_[*selected_];
        switch (row.kind) {
        case AreaKind::Area:
            if (row.index < summary_->areas().size()) summary_->remove(summary_->areas()[row.index]);
            break;
        case AreaKind::Battle:
            if (row.index < summary_->battles().size()) summary_->remove(summary_->battles()[row.index]);
            break;
        case AreaKind::Package:
            if (row.index < summary_->packages().size()) summary_->remove(summary_->packages()[row.index]);
            break;
        }
        selected_.reset();
        refresh();
    }, event);
}

void AreaTable::clone(SelectionEvent& event) {
    if (canonical_) {
        canonical_areatable::AreaTableSelectionEvent converted{"clone", false};
        canonical_->clone(converted);
        event.handled = converted.handled;
        status_line_ = canonical_->statusLine();
        return;
    }
    notifyAction([this] { if (summary_) summary_->changed(); }, event);
}

void AreaTable::selectAll() {
    if (canonical_) {
        canonical_->selectAll();
        status_line_ = canonical_->statusLine();
    } else if (table_) {
        table_->selectAll();
    }
}

bool AreaTable::canSelectAll() const noexcept {
    return canonical_ ? canonical_->canSelectAll()
                      : table_ && !table_->rows.empty();
}

bool AreaTable::canUp() const noexcept {
    if (canonical_) return canonical_->canUp();
    return !read_only_ && selected_ && *selected_ > 0U;
}

bool AreaTable::canDown() const noexcept {
    if (canonical_) return canonical_->canDown();
    return !read_only_ && selected_ && *selected_ + 1U < rows_.size();
}

void AreaTable::up() {
    if (canonical_) {
        canonical_->up();
        status_line_ = canonical_->statusLine();
        return;
    }
    if (!canUp()) return;
    const auto index = *selected_;
    if (index < rows_.size()) {
        const auto row = rows_[index];
        if (row.kind == AreaKind::Area && row.index > 0U) summary_->swapArea(row.index, row.index - 1U);
        if (row.kind == AreaKind::Battle && row.index > 0U) summary_->swapBattle(row.index, row.index - 1U);
        if (row.kind == AreaKind::Package && row.index > 0U) summary_->swapPackage(row.index, row.index - 1U);
        refresh();
        selected_ = index - 1U;
    }
}

void AreaTable::down() {
    if (canonical_) {
        canonical_->down();
        status_line_ = canonical_->statusLine();
        return;
    }
    if (!canDown()) return;
    const auto index = *selected_;
    if (index < rows_.size()) {
        const auto row = rows_[index];
        if (row.kind == AreaKind::Area && row.index + 1U < summary_->areas().size()) summary_->swapArea(row.index, row.index + 1U);
        if (row.kind == AreaKind::Battle && row.index + 1U < summary_->battles().size()) summary_->swapBattle(row.index, row.index + 1U);
        if (row.kind == AreaKind::Package && row.index + 1U < summary_->packages().size()) summary_->swapPackage(row.index, row.index + 1U);
        refresh();
        selected_ = std::min(index + 1U, rows_.size() - 1U);
    }
}

CardWindow::CardWindow(Commons comm, Props prop, const CardWindowKind kind, Parent parent)
    : comm_(std::move(comm)), prop_(std::move(prop)), kind_(kind), parent_(std::move(parent)) {
    owner_type_ = (kind_ == CardWindowKind::Hand ||
                   kind_ == CardWindowKind::ImportSourceHand)
                      ? OwnerType::Cast : OwnerType::Summary;
    if (parent_) construct1(parent_), construct2();
    else initPane();
}

CardWindow::CardWindow(Commons comm, Props prop, const CardWindowKind kind,
                       SummaryPtr summary, Parent parent)
    : comm_(std::move(comm)), prop_(std::move(prop)), kind_(kind),
      summary_(std::move(summary)), parent_(std::move(parent)) {
    owner_type_ = (kind_ == CardWindowKind::Hand ||
                   kind_ == CardWindowKind::ImportSourceHand)
                      ? OwnerType::Cast : OwnerType::Summary;
    owner_ = asPath(summary_);
    if (parent_) construct1(parent_), construct2();
    else initPane();
}

CardWindow::CardWindow(Commons comm, Props prop, const CardWindowKind kind,
                       Parent parent, SummaryPtr summary, CWXPathPtr owner,
                       SummaryPtr toc)
    : comm_(std::move(comm)), prop_(std::move(prop)), kind_(kind),
      summary_(std::move(summary)), owner_(std::move(owner)), toc_(std::move(toc)),
      parent_(std::move(parent)) {
    owner_type_ = (kind_ == CardWindowKind::Hand ||
                   kind_ == CardWindowKind::ImportSourceHand)
                      ? OwnerType::Cast : OwnerType::Summary;
    if (parent_) construct1(parent_), construct2();
    else initPane();
    refreshAll(summary_, owner_);
}

CardWindow::~CardWindow() {
    callback_lifetime_.reset();
    // C++のWindowは組み込み描画面を通じてCardWindowより長く生存し
    // 得る。通知のデリゲートも終了コールバックも、thisを参照したまま残さない。
    disconnectLifecycleHandlers();
    if (win_) win_->on_close_requested = {};
    try {
        publishSourceClose();
    } catch (...) {
        // 破棄処理から例外を送出しない寿命管理の境界を維持する。
    }
}

void CardWindow::construct(Commons comm, Props prop, SummaryPtr summary,
                             const Parent& parent) {
    disconnectLifecycleHandlers();
    comm_ = std::move(comm);
    prop_ = std::move(prop);
    summary_ = std::move(summary);
    owner_ = owner_type_ == OwnerType::Cast ? owner_ : asPath(summary_);
    parent_ = parent;
    if (!win_) construct1(parent_), construct2();
    else {
        installLifecycleHandlers();
        refreshAll(summary_, owner_);
    }
}

void CardWindow::reconstruct(const Parent& parent) {
    // 読み取り専用の取り込み手札も、ドッキング前に遅延生成のUIを構築する必要がある。
    if (win_) return;
    parent_ = parent;
    construct1(parent_);
    construct2();
    refreshAll(summary_, owner_);
}

void CardWindow::initPane() {
    if (!panes_.empty()) return;
    for (const auto type : cardTypes()) {
        auto pane_comm = comm_;
        // DのCardWindow::construct2は、ローカルのタブ間だけで列幅を共有する。
        if (multiTab() && (!editMode() || owner_type_ != OwnerType::Summary)) {
            if (!local_column_width_event_)
                local_column_width_event_ = std::make_shared<canonical_cardpane::Signal<
                    const void*, canonical_cardpane::CardTableColumn, float>>();
            pane_comm.columnWidthEvent = local_column_width_event_;
            pane_comm.localColumnWidthSync = true;
        }
        auto paneValue = std::make_shared<CardPane>(
            std::move(pane_comm), prop_, summary_, Parent{}, owner_type_, type, !editMode(), toc_,
            // Dは読み取り専用の取り込みペインだけにopenHandを渡す。通常のCastペイン
            // はCardPane::editHandからCommons::openHandsを使
            // う。取り込み用コールバックはeditModeを拒否するため、通常の経路に使わない。
            type == CardType::Cast && !editMode() ? [this](const CastCardPtr& value) {
                openHand(value);
            } : std::function<void(const CastCardPtr&)>{});
        paneValue->owner(owner_);
        panes_.push_back(paneValue);
        pane_table_[type] = paneValue;
    }
}

void CardWindow::construct1(const Parent& parent) {
    if (win_) return;
    parent_ = parent;
    win_ = std::make_shared<lib::Window>();
    comp_ = std::make_shared<lib::Panel>();
    comp_->set_layout(std::make_shared<lib::FillLayout>());
    win_->add(comp_);
    if (parent_) parent_->add(win_);
    if (multiTab()) {
        tabf_ = std::make_shared<lib::TabView>();
        const bool all_mode = lib::editor_window_host_options().all_mode;
        tabf_->header_visible = !all_mode;
        if (all_mode) {
            auto content = std::make_shared<lib::Panel>();
            auto content_layout = std::make_shared<lib::LinearLayout>(
                lib::Orientation::Vertical);
            content_layout->padding = {};
            content_layout->spacing = 0.0F;
            content->set_layout(content_layout);

            auto selector_row = std::make_shared<lib::Panel>();
            auto selector_layout = std::make_shared<lib::LinearLayout>(
                lib::Orientation::Horizontal);
            // ALLは選択バーの上下の合計余白を論理2画素
            // 減らす。上下対称に1画素ずつ減らし、カテゴリ
            // 選択を中央に保ちながらカード領域を広げる。
            selector_layout->padding = {2.0F, 2.0F, 1.0F, 2.0F};
            selector_layout->padding.top = -1.0F;
            selector_layout->padding.bottom = -1.0F;
            selector_layout->spacing = 2.0F;
            selector_layout->cross_axis = lib::Align::Center;
            selector_row->set_layout(selector_layout);

            page_selector_ = std::make_shared<lib::ComboBox>();
            page_selector_->accessibility_name = withArea()
                ? "外部シナリオのカード" : "手札カード";
            page_selector_->visible_item_count = 7U;
            page_selector_->row_height_scale = 1.25F;
            lib::LayoutItem selector_item;
            selector_item.minimum = {100.0F, 0.0F};
            selector_item.horizontal = lib::Align::Stretch;
            selector_item.grow = 1.0F;
            page_selector_->set_layout_item(selector_item);
            page_selector_->on_selection_changed =
                [this](const std::optional<std::size_t> selected) {
                    if (!selected || !tabf_ || *selected >= tabf_->pages.size()) return;
                    selectTabPage(*selected);
                    refreshStatusLine();
                    if (comm_.refreshToolBar) comm_.refreshToolBar();
                };
            selector_row->add(page_selector_);
            content->add(selector_row);
            lib::LayoutItem pages_item;
            pages_item.horizontal = lib::Align::Stretch;
            pages_item.vertical = lib::Align::Stretch;
            pages_item.grow = 1.0F;
            tabf_->set_layout_item(pages_item);
            content->add(tabf_);
            comp_->add(content);
        } else {
            comp_->add(tabf_);
        }
        tabf_->on_selection_changed = [this](std::optional<std::size_t> value) {
            tab_selection_ = value.value_or(0U);
            if (page_selector_) {
                page_selector_->selected_index = tab_selection_;
                if (comm_.services.gui)
                    comm_.services.gui->request_redraw(page_selector_);
            }
            refreshStatusLine();
            if (comm_.refreshToolBar) comm_.refreshToolBar();
        };
    }
    drop_scenario_.window = this;
    if (editMode() && owner_type_ == OwnerType::Summary && comm_.services.drag_drop) {
        try {
            lib::DropTargetBinding target;
            target.target = std::static_pointer_cast<lib::Widget>(comp_);
            target.accepted_formats = {"text/uri-list", "application/x-cwxeditor-scenario"};
            target.on_enter = [this](lib::DragEvent& event) { drop_scenario_.dragEnter(event); };
            target.on_drop = [this](lib::DragEvent& event) { drop_scenario_.drop(event); };
            comm_.services.drag_drop->bind_target(std::move(target));
        } catch (...) {
        }
    }
}

std::vector<CardType> CardWindow::cardTypes() const {
    switch (kind_) {
    case CardWindowKind::Cast: return {CardType::Cast};
    case CardWindowKind::Skill: return {CardType::Skill};
    case CardWindowKind::Item: return {CardType::Item};
    case CardWindowKind::Beast: return {CardType::Beast};
    case CardWindowKind::Info: return {CardType::Info};
    case CardWindowKind::ImportSource:
        return {CardType::Cast, CardType::Skill, CardType::Item, CardType::Beast, CardType::Info};
    case CardWindowKind::Hand:
    case CardWindowKind::ImportSourceHand:
        return {CardType::Skill, CardType::Item, CardType::Beast};
    }
    return {};
}

bool CardWindow::multiTab() const noexcept {
    return kind_ == CardWindowKind::ImportSource ||
           kind_ == CardWindowKind::Hand ||
           kind_ == CardWindowKind::ImportSourceHand;
}

void CardWindow::newPane() {
    if (panes_.empty()) initPane();
    for (const auto& paneValue : panes_) paneValue->construct({});
}

void CardWindow::construct2() {
    newPane();
    if (withArea()) {
        if (!areas_) areas_ = std::make_shared<AreaTable>(comm_, prop_, !editMode(), toc_);
        areas_->construct({});
        areas_->summary(summary_);
    }
    if (tabf_) {
        std::vector<lib::TabPage> pages;
        if (areas_) pages.push_back({prop_.areasTabName, prop_.tableViewImage,
                                     areas_->panel(), false});
        for (const auto& paneValue : panes_) {
            const auto image = cardImage(paneValue->cardType(), prop_);
            pages.push_back({cardName(paneValue->cardType(), prop_), image,
                             paneValue->widget(), false});
        }
        std::vector<lib::ChoiceItem> selector_items;
        if (page_selector_) {
            selector_items.reserve(pages.size());
            for (std::size_t index = 0; index < pages.size(); ++index)
                selector_items.push_back({pages[index].title, std::to_string(index),
                                          pages[index].icon, true});
        }
        (void)lib::replace_tab_pages(tabf_, std::move(pages), 0U);
        selectTabPage(0U);
        if (page_selector_)
            (void)lib::replace_combo_items(page_selector_, std::move(selector_items), 0U);
    } else if (comp_ && !panes_.empty() && panes_.front() && panes_.front()->widget()) {
        comp_->clear();
        comp_->add(panes_.front()->widget());
    }
    if (prop_.cardLife && !prop_.cardDetails) showCardLife();
    else if (prop_.cardDetails) showCardTable();
    else showCardList();
    installLifecycleHandlers();
}

void CardWindow::selectTabPage(const std::size_t index) {
    tab_selection_ = index;
    if (tabf_) (void)lib::select_tab_page(*tabf_, index, false);
    if (page_selector_) {
        page_selector_->selected_index = index;
        if (comm_.services.gui)
            comm_.services.gui->request_redraw(page_selector_);
    }
}

void CardWindow::installLifecycleHandlers() {
    disconnectLifecycleHandlers();
    close_requested_ = false;
    source_close_published_ = false;
    // cardwindow.d:246-253は、読み取り専用の取り込みウィンド
    // ウも含め、シナリオの改名・移動時にShellとドックのタイトルを更新する。
    ref_scenario_name_connection_ = comm_.refScenarioName.add(
        [this] { refreshTitle(); });
    ref_scenario_path_connection_ = comm_.refScenarioPath.add(
        [this] { refreshTitle(); });
    if (owner_type_ == OwnerType::Cast) {
        if (editMode()) {
            ref_cast_connection_ = comm_.refCast.add(
                [this](const CastCardPtr& owner) { refOwner(owner); });
            del_cast_connection_ = comm_.delCast.add(
                [this](const CastCardPtr& owner) { delOwner(owner); });
        } else {
            close_adds_connection_ = comm_.closeAdds.add(
                [this](const SummaryPtr& importable) { closeAdds(importable); });
        }
    } else if (!editMode() && win_) {
        // Dは読み取り専用の取り込み元の破棄時にcloseAddsを
        // 通知する。共通Windowのホスト終了境界を対応させる。
        win_->on_close_requested = [this] { publishSourceClose(); };
    }
}

void CardWindow::disconnectLifecycleHandlers() noexcept {
    if (close_adds_connection_) comm_.closeAdds.remove(close_adds_connection_);
    if (ref_cast_connection_) comm_.refCast.remove(ref_cast_connection_);
    if (del_cast_connection_) comm_.delCast.remove(del_cast_connection_);
    if (ref_scenario_name_connection_)
        comm_.refScenarioName.remove(ref_scenario_name_connection_);
    if (ref_scenario_path_connection_)
        comm_.refScenarioPath.remove(ref_scenario_path_connection_);
    close_adds_connection_ = 0U;
    ref_cast_connection_ = 0U;
    del_cast_connection_ = 0U;
    ref_scenario_name_connection_ = 0U;
    ref_scenario_path_connection_ = 0U;
}

void CardWindow::closeForOwnerLifecycle() {
    if (close_requested_) return;
    close_requested_ = true;
    disconnectLifecycleHandlers();
    if (comm_.close && win_)
        comm_.close(std::static_pointer_cast<lib::Widget>(win_));
}

void CardWindow::publishSourceClose() {
    if (source_close_published_ || editMode() || owner_type_ != OwnerType::Summary ||
        !summary_)
        return;
    source_close_published_ = true;
    comm_.closeAdds.call(summary_);
}

void CardWindow::closeAdds(SummaryPtr importable) {
    if (editMode() || owner_type_ != OwnerType::Cast || !importable ||
        importable != summary_)
        return;
    closeForOwnerLifecycle();
}

void CardWindow::refOwner(CastCardPtr owner) {
    if (!editMode() || owner_type_ != OwnerType::Cast || !owner ||
        owner != std::dynamic_pointer_cast<CastCard>(owner_))
        return;
    refreshM();
}

void CardWindow::delOwner(CastCardPtr owner) {
    if (!editMode() || owner_type_ != OwnerType::Cast || !owner ||
        owner != std::dynamic_pointer_cast<CastCard>(owner_))
        return;
    closeForOwnerLifecycle();
}

WidgetPtr CardWindow::focusControl() const noexcept {
    if (const auto paneValue = selectedPane(false); paneValue) {
        if (const auto canonical = paneValue->canonical()) return canonical->inputWidget();
        if (paneValue->widget()) return std::static_pointer_cast<lib::Widget>(paneValue->widget());
    }
    if (areas_ && areas_->panel()) return std::static_pointer_cast<lib::Widget>(areas_->panel());
    return win_ ? std::static_pointer_cast<lib::Widget>(win_) : WidgetPtr{};
}

bool CardWindow::isSelectedAreaTable() const noexcept {
    return withArea() && tabf_ && tab_selection_ == 0U;
}

std::size_t CardWindow::selectionCardIndex() const noexcept {
    if (withArea()) return tab_selection_ == 0U ? static_cast<std::size_t>(-1) : tab_selection_ - 1U;
    return tab_selection_;
}

TCPD* CardWindow::tcpd() const noexcept {
    if (isSelectedAreaTable()) return areas_.get();
    const auto index = selectionCardIndex();
    return index < panes_.size() ? panes_[index].get() : nullptr;
}

CardPanePtr CardWindow::selectedPane(const bool includeArea) const noexcept {
    if (includeArea && isSelectedAreaTable()) return {};
    const auto index = selectionCardIndex();
    return index < panes_.size() ? panes_[index] : CardPanePtr{};
}

CardPanePtr CardWindow::pane(const CardType type) const noexcept {
    const auto it = pane_table_.find(type);
    return it == pane_table_.end() ? CardPanePtr{} : it->second;
}

void CardWindow::refreshStatusLine() {
    const auto* current = tcpd();
    status_line_ = current ? current->statusLine() : String{};
    if (comm_.setStatusLine && comp_)
        comm_.setStatusLine(std::static_pointer_cast<lib::Widget>(comp_), status_line_);
}

void CardWindow::setStatusLine(String status) {
    status_line_ = std::move(status);
    if (comm_.setStatusLine && comp_)
        comm_.setStatusLine(std::static_pointer_cast<lib::Widget>(comp_), status_line_);
}

void CardWindow::refreshM() {
    for (const auto& paneValue : panes_) paneValue->refresh();
    if (areas_) areas_->refresh();
    refreshTitle();
    refreshStatusLine();
}

void CardWindow::reNumberingAll() {
    if (!editMode()) return;
    for (const auto& paneValue : panes_) paneValue->reNumbering(1U, 1U);
}

void CardWindow::showCardLife() {
    view_mode_ = CViewMode::LIFE;
    prop_.cardLife = true;
    prop_.cardDetails = false;
    for (const auto& paneValue : panes_) paneValue->showCardLife();
}

void CardWindow::showCardList() {
    view_mode_ = CViewMode::CARD;
    prop_.cardLife = false;
    prop_.cardDetails = false;
    for (const auto& paneValue : panes_) paneValue->showCardList();
}

void CardWindow::showCardTable() {
    view_mode_ = CViewMode::TABLE;
    prop_.cardLife = false;
    prop_.cardDetails = true;
    for (const auto& paneValue : panes_)
        if (paneValue) paneValue->showCardTable();
}

CardPanePtr CardWindow::openPane(const CardType cardType, const bool shellActivate) {
    if (!editMode()) return {};
    if (owner_type_ == OwnerType::Summary && comm_.openCardPane) {
        if (auto result = comm_.openCardPane(cardType, shellActivate)) return result;
    }
    const auto result = pane(cardType);
    if (!result) return {};
    if (tabf_) {
        const auto values = cardTypes();
        const auto it = std::find(values.begin(), values.end(), cardType);
        if (it != values.end()) {
            selectTabPage(static_cast<std::size_t>(std::distance(values.begin(), it)) +
                          (withArea() ? 1U : 0U));
        }
    }
    if (comm_.forceFocus && result->widget())
        comm_.forceFocus(std::static_pointer_cast<lib::Widget>(result->widget()), shellActivate);
    return result;
}

CardPanePtr CardWindow::paneCast() const noexcept { return pane(CardType::Cast); }
CardPanePtr CardWindow::paneSkill() const noexcept { return pane(CardType::Skill); }
CardPanePtr CardWindow::paneItem() const noexcept { return pane(CardType::Item); }
CardPanePtr CardWindow::paneBeast() const noexcept { return pane(CardType::Beast); }
CardPanePtr CardWindow::paneInfo() const noexcept { return pane(CardType::Info); }

void CardWindow::create(const CardType cardType) {
    if (const auto value = pane(cardType); value && value->canCreate()) value->create();
}

bool CardWindow::canCreateCast() const noexcept {
    const auto value = pane(CardType::Cast);
    return editMode() && value && value->canCreate();
}
bool CardWindow::canCreateSkill() const noexcept {
    const auto value = pane(CardType::Skill);
    return editMode() && value && value->canCreate();
}
bool CardWindow::canCreateItem() const noexcept {
    const auto value = pane(CardType::Item);
    return editMode() && value && value->canCreate();
}
bool CardWindow::canCreateBeast() const noexcept {
    const auto value = pane(CardType::Beast);
    return editMode() && value && value->canCreate();
}
bool CardWindow::canCreateInfo() const noexcept {
    const auto value = pane(CardType::Info);
    return editMode() && value && value->canCreate();
}

lib::ResourceId CardWindow::image() const noexcept {
    switch (kind_) {
    case CardWindowKind::Cast: return prop_.castImage;
    case CardWindowKind::Skill: return prop_.skillImage;
    case CardWindowKind::Item: return prop_.itemImage;
    case CardWindowKind::Beast: return prop_.beastImage;
    case CardWindowKind::Info: return prop_.infoImage;
    case CardWindowKind::Hand: return prop_.openHandImage;
    case CardWindowKind::ImportSource:
    case CardWindowKind::ImportSourceHand: return prop_.importSourceImage;
    }
    return 0U;
}

String CardWindow::title() const {
    switch (kind_) {
    case CardWindowKind::Cast:
        return ::cwxeditor::cwx::utils::tryFormat(prop_.cardTabName, prop_.castName);
    case CardWindowKind::Skill:
        return ::cwxeditor::cwx::utils::tryFormat(prop_.cardTabName, prop_.skillName);
    case CardWindowKind::Item:
        return ::cwxeditor::cwx::utils::tryFormat(prop_.cardTabName, prop_.itemName);
    case CardWindowKind::Beast:
        return ::cwxeditor::cwx::utils::tryFormat(prop_.cardTabName, prop_.beastName);
    case CardWindowKind::Info:
        return ::cwxeditor::cwx::utils::tryFormat(prop_.cardTabName, prop_.infoName);
    case CardWindowKind::Hand:
    case CardWindowKind::ImportSourceHand: {
        const auto cast = std::dynamic_pointer_cast<CastCard>(owner_);
        return cast ? ::cwxeditor::cwx::utils::tryFormat(
                          prop_.handCardTabName, cast->id(), cast->name())
                    : String{};
    }
    case CardWindowKind::ImportSource:
        return summary_ ? ::cwxeditor::cwx::utils::tryFormat(
                              prop_.importSourceTabName, summary_->scenarioName(),
                              summary_->scenarioPath())
                        : String{};
    }
    return {};
}

void CardWindow::refreshTitle() {
    if (win_) {
        win_->title = title();
        if (comm_.setTitle)
            comm_.setTitle(std::static_pointer_cast<lib::Widget>(win_), win_->title);
    }
}

void CardWindow::onHostClose() { publishSourceClose(); }

void CardWindow::refresh(SummaryPtr summary) {
    if (owner_type_ != OwnerType::Summary || !editMode()) return;
    summary_ = std::move(summary);
    refreshAll(summary_, asPath(summary_));
}

void CardWindow::refresh(SummaryPtr summary, CastCardPtr owner) {
    if (owner_type_ != OwnerType::Cast || !editMode()) return;
    refreshAll(std::move(summary), asPath(owner));
}

void CardWindow::refreshAll(SummaryPtr summary, CWXPathPtr owner) {
    summary_ = std::move(summary);
    owner_ = std::move(owner);
    for (const auto& paneValue : panes_) paneValue->refreshAll(summary_, owner_);
    if (areas_) areas_->summary(summary_);
    refreshTitle();
    refreshStatusLine();
}

void CardWindow::refreshUseCount() {
    for (const auto& paneValue : panes_) {
        if (!paneValue) continue;
        const auto canonical = paneValue->canonical();
        if (canonical) canonical->refreshUseCount();
    }
}

void CardWindow::cut(SelectionEvent& event) {
    if (editMode()) {
        if (auto* value = tcpd(); value && value->canDoTCPD()) value->cut(event);
    }
}
void CardWindow::copy(SelectionEvent& event) {
    if (auto* value = tcpd(); value && value->canDoC()) value->copy(event);
}
void CardWindow::paste(SelectionEvent& event) {
    if (editMode()) {
        if (auto* value = tcpd(); value && value->canDoP()) value->paste(event);
    }
}
void CardWindow::del(SelectionEvent& event) {
    if (editMode()) {
        if (auto* value = tcpd(); value && value->canDoD()) value->del(event);
    }
}
void CardWindow::clone(SelectionEvent& event) {
    if (editMode()) {
        if (auto* value = tcpd(); value && value->canDoClone()) value->clone(event);
    }
}
bool CardWindow::canDoT() const noexcept {
    const auto* value = tcpd();
    return editMode() && value && value->canDoT();
}
bool CardWindow::canDoC() const noexcept {
    const auto* value = tcpd();
    return value && value->canDoC();
}
bool CardWindow::canDoP() const noexcept {
    const auto* value = tcpd();
    return editMode() && value && value->canDoP();
}
bool CardWindow::canDoD() const noexcept {
    const auto* value = tcpd();
    return editMode() && value && value->canDoD();
}
bool CardWindow::canDoClone() const noexcept {
    const auto* value = tcpd();
    return editMode() && value && value->canDoClone();
}
void CardWindow::edit() {
    if (auto* value = tcpd(); value && value->canEdit()) value->edit();
}
bool CardWindow::canEdit() const noexcept {
    const auto* value = tcpd();
    return value && value->canEdit();
}
void CardWindow::editHand() {
    if (const auto value = selectedPane(false); value && value->canEditHand()) value->editHand();
}
bool CardWindow::canEditHand() const noexcept {
    const auto value = selectedPane(false);
    return value && value->canEditHand();
}
void CardWindow::reNumbering() {
    if (editMode()) if (auto* value = tcpd(); value && value->canReNumbering()) value->reNumbering();
}
bool CardWindow::canReNumbering() const noexcept {
    const auto* value = tcpd();
    return editMode() && value && value->canReNumbering();
}
bool CardWindow::canUndo() const noexcept {
    const auto* value = tcpd();
    return editMode() && value && value->canUndo();
}
bool CardWindow::canRedo() const noexcept {
    const auto* value = tcpd();
    return editMode() && value && value->canRedo();
}
void CardWindow::undo() {
    if (canUndo()) tcpd()->undo();
}
void CardWindow::redo() {
    if (canRedo()) tcpd()->redo();
}

void CardWindow::setUndoLimit(const std::size_t value) {
    prop_.undoMax = value;
    for (const auto& pane : panes_)
        if (pane) pane->setUndoLimit(value);
}
bool CardWindow::canUp() const noexcept {
    const auto* value = tcpd();
    return editMode() && value && value->canUp();
}
bool CardWindow::canDown() const noexcept {
    const auto* value = tcpd();
    return editMode() && value && value->canDown();
}
void CardWindow::up() {
    if (canUp()) tcpd()->up();
}
void CardWindow::down() {
    if (canDown()) tcpd()->down();
}
void CardWindow::replaceID() {
    if (canReplaceID()) tcpd()->replaceID();
}
bool CardWindow::canReplaceID() const noexcept {
    const auto* value = tcpd();
    return editMode() && value && value->canReplaceID();
}
void CardWindow::selectConnectedResource() {
    if (canSelectConnectedResource()) selectedPane(false)->selectConnectedResource();
}
bool CardWindow::canSelectConnectedResource() const noexcept {
    const auto value = selectedPane(false);
    return editMode() && value && value->canSelectConnectedResource();
}
void CardWindow::doImport() {
    if (editMode()) return;
    if (isSelectedAreaTable()) {
        const auto areas = areas_;
        if (areas && areas->canDoImport()) areas->doImport();
        return;
    }
    const auto pane = selectedPane(false);
    if (pane && pane->canDoImport()) pane->doImport();
}
bool CardWindow::canDoImport() const noexcept {
    if (editMode()) return false;
    if (isSelectedAreaTable()) return areas_ && areas_->canDoImport();
    const auto value = selectedPane(false);
    return value && value->canDoImport();
}
void CardWindow::selectAll() {
    if (auto value = selectedPane(true); value) value->selectAll();
    else if (areas_ && isSelectedAreaTable()) areas_->selectAll();
}
bool CardWindow::canSelectAll() const noexcept {
    if (const auto value = selectedPane(true); value) return value->canSelectAll();
    return areas_ && isSelectedAreaTable() && areas_->canSelectAll();
}
void CardWindow::removeRef() {
    if (canRemoveRef()) selectedPane(false)->removeRef();
}
bool CardWindow::canRemoveRef() const noexcept {
    const auto value = selectedPane(false);
    return editMode() && owner_type_ == OwnerType::Cast && value &&
           value->canRemoveRef();
}
void CardWindow::writeComment() {
    if (canWriteComment()) selectedPane(false)->writeComment();
}
bool CardWindow::canWriteComment() const noexcept {
    const auto value = selectedPane(false);
    return editMode() && value && value->canWriteComment();
}
void CardWindow::incSearch() {
    if (const auto value = selectedPane(false)) value->incSearch();
}
bool CardWindow::canIncSearch() const noexcept {
    const auto value = selectedPane(false);
    return value && value->canIncSearch();
}
void CardWindow::editEvent(const bool canDuplicate) {
    if (isSelectedAreaTable()) {
        if (areas_ && areas_->canOpenAreaEvent()) areas_->openAreaEvent(true, canDuplicate);
    } else editUseEvent(canDuplicate);
}
bool CardWindow::canEditEvent() const noexcept {
    if (isSelectedAreaTable()) return areas_ && areas_->canOpenAreaEvent();
    return canEditUseEvent();
}
void CardWindow::editUseEvent(const bool canDuplicate) {
    const auto value = selectedPane(false);
    if (value && value->canEditUseEvent()) value->editUseEvent(canDuplicate);
}
bool CardWindow::canEditUseEvent() const noexcept {
    const auto value = selectedPane(false);
    return value && value->canEditUseEvent();
}

void CardWindow::changeVHSide() {
    if (auto* areas = dynamic_cast<AreaTable*>(tcpd());
        areas && areas->canChangeVH())
        areas->changeVHSide();
}

bool CardWindow::canChangeVH() const noexcept {
    const auto* areas = dynamic_cast<const AreaTable*>(tcpd());
    return areas && areas->canChangeVH();
}
bool CardWindow::isSelected() const noexcept {
    const auto* value = tcpd();
    return value && (dynamic_cast<const CardPane*>(value) ? static_cast<const CardPane*>(value)->isSelected()
                                                         : static_cast<const AreaTable*>(value)->isSelected());
}

bool CardWindow::openCWXPathEff(const CardType type, String value,
                                const bool shellActivate) {
    const auto valuePane = pane(type);
    if (!valuePane) return false;
    const auto category = path::cpcategory(value);
    const auto name = cardCategory(type);
    const auto preparePane = [&] {
        const std::weak_ptr<int> lifetime = callback_lifetime_;
        const auto stopOpen = comm_.stopOpen;
        const auto forceFocus = comm_.forceFocus;
        const auto expected_summary = summary_;
        const auto expected_owner = owner_;
        const auto current = [this, lifetime, stopOpen, expected_summary, expected_owner] {
            if (lifetime.expired()) return false;
            const bool stopped = stopOpen && stopOpen();
            if (lifetime.expired() || stopped) return false;
            return summary_ == expected_summary && owner_ == expected_owner;
        };
        if (!current()) return false;

        std::optional<std::size_t> targetPage;
        if (tabf_) {
            const auto all = cardTypes();
            const auto it = std::find(all.begin(), all.end(), type);
            if (it != all.end()) {
                targetPage = static_cast<std::size_t>(std::distance(all.begin(), it)) +
                             (withArea() ? 1U : 0U);
            }
        }
        const auto canonical = valuePane->canonical();
        const auto input = canonical ? canonical->inputWidget()
            : std::static_pointer_cast<lib::Widget>(valuePane->widget());
        const bool noFocus = path::cphasattr(value, "nofocus");

        if (targetPage) selectTabPage(*targetPage);
        if (!current()) return false;
        if (!noFocus && forceFocus && input)
            forceFocus(input, shellActivate);
        // フォーカスの公開で、このCardWindowやホストが同期的に終了する場合がある。
        return current();
    };
    if (category == name + "view") {
        if (!preparePane()) return false;
        return valuePane->openCWXPath(value, shellActivate);
    }
    if (category != name && category != name + ":id") return false;
    const auto index = path::cpindex(value);
    const auto card = category == name + ":id" ? valuePane->cardById(index)
                                               : valuePane->card(index);
    if (!card) return false;
    const auto bottom = path::cpbottom(value);
    const auto tail = path::cpcategory(bottom);
    const bool effectCardType = type == CardType::Skill || type == CardType::Item ||
                                type == CardType::Beast;
    if (path::cpempty(bottom) || (effectCardType && tail == "motion")) {
        if (!preparePane()) return false;
        return valuePane->openCWXPath(value, shellActivate);
    }
    if (type == CardType::Cast && comm_.openHandsPath &&
        (tail == "skillcard" || tail == "skillcard:id" || tail == "skillcardview" ||
         tail == "itemcard" || tail == "itemcard:id" || tail == "itemcardview" ||
         tail == "beastcard" || tail == "beastcard:id" || tail == "beastcardview")) {
        if (!preparePane()) return false;
        const auto openHandsPath = comm_.openHandsPath;
        const auto targetSummary = valuePane->summary();
        const auto targetOwner = std::dynamic_pointer_cast<CastCard>(card);
        const auto targetToc = valuePane->toc();
        return openHandsPath(targetSummary, targetOwner, targetToc, bottom,
                             shellActivate);
    }
    if (effectCardType && (tail == "event" || tail == "variable") &&
        comm_.openUseEventsPath) {
        if (!preparePane()) return false;
        const auto openUseEventsPath = comm_.openUseEventsPath;
        const auto targetSummary = valuePane->summary();
        const auto targetCard = std::dynamic_pointer_cast<EffectCard>(card);
        return openUseEventsPath(targetSummary, targetCard, bottom, shellActivate);
    }
    return false;
}
bool CardWindow::openCWXPath(const StringView value, const bool shellActivate) {
    const auto category = path::cpcategory(value);
    const std::vector<std::pair<CardType, String>> types = {
        {CardType::Cast, "castcard"}, {CardType::Skill, "skillcard"},
        {CardType::Item, "itemcard"}, {CardType::Beast, "beastcard"},
        {CardType::Info, "infocard"}};
    for (const auto& [type, name] : types) {
        if (category == name || category == name + ":id" ||
            category == name + "view") {
            if (pane(type))
                return openCWXPathEff(type, String(value), shellActivate);
        }
    }
    if (areas_ && withArea()) return areas_->openCWXPath(value, shellActivate);
    return false;
}

std::vector<String> CardWindow::openedCWXPath() const {
    if (!editMode()) return {};
    std::vector<String> result;
    const auto active = selectionCardIndex();
    for (std::size_t index = 0U; index < panes_.size(); ++index) {
        if (index == active) continue;
        const auto values = panes_[index]->openedCWXPath();
        result.insert(result.end(), values.begin(), values.end());
    }
    if (active < panes_.size()) {
        const auto values = panes_[active]->openedCWXPath();
        result.insert(result.end(), values.begin(), values.end());
    }
    return result;
}

void CardWindow::openHand(const CastCardPtr& cardValue) {
    if (!cardValue || editMode()) return;
    if (comm_.openHands && summary_) comm_.openHands(prop_, summary_, cardValue, toc_, true);
}

void CardWindow::DropScenario::dragEnter(lib::DragEvent& event) const {
    if (!window || !window->editMode()) return;
    event.selected = lib::DropOperation::Link;
    event.handled = true;
}

void CardWindow::DropScenario::drop(lib::DragEvent& event) const {
    if (!window || !window->editMode()) return;
    if (!event.data.files.empty() && window->comm_.addScenarioFiles) {
        std::vector<String> files;
        for (const auto& file : event.data.files)
            files.push_back(structs::pathToUtf8(file));
        window->comm_.addScenarioFiles(files);
        event.selected = lib::DropOperation::Link;
    } else if (window->comm_.addScenario) {
        window->comm_.addScenario();
        event.selected = lib::DropOperation::Link;
    }
    event.handled = true;
}

void AddCard::openScenario(Commons comm, Props prop, Parent parent, Status status,
                            SummaryPtr summary, SummaryPtr toc, AddScenario addScenario) {
    if (status) status("Select scenario");
    if (comm.addScenario) comm.addScenario();
    if (summary && addScenario) {
        WindowList result;
        auto window = std::make_shared<CardWindow>(comm, prop, CardWindowKind::ImportSource,
                                                    parent, summary, asPath(summary), toc);
        result.push_back(std::move(window));
        addScenario(std::move(result));
    }
}

void AddCard::openScenario(Commons comm, Props prop, Parent parent, Status status,
                            SummaryPtr summary, SummaryPtr toc,
                            const std::vector<String>& files, AddScenario addScenario,
                            lib::Action failure) {
    if (status) status("Loading scenarios");
    std::vector<SummaryPtr> summaries;
    if (comm.loadSummaries) summaries = comm.loadSummaries(files);
    if (summaries.empty() && summary) summaries.push_back(summary);
    if (summaries.empty()) {
        if (failure) failure();
        return;
    }
    WindowList result;
    for (const auto& value : summaries) {
        if (!value) continue;
        result.push_back(std::make_shared<CardWindow>(
            comm, prop, CardWindowKind::ImportSource, parent, value, asPath(value), toc));
    }
    if (addScenario) addScenario(std::move(result));
    if (status) status("Scenarios loaded");
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::cardwindow
