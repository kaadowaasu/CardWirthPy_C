#pragma once

// cwx/editor/gui/dwt/cardpane.d
// のC++20移植。 カードモデルとの接続、二つ
// の表・一覧、ドラッグ、クリップボード、Undo
// 履歴を組み合わせる。 それぞれの役割を分け、
// 共通libのインターフェースだけを公開する。

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/comment.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/cardlist.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <algorithm>
#include <any>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::variables { class FlexEtcProps; }
namespace cwxeditor::cwx::editor::gui::dwt::dprops { class Props; }
namespace cwxeditor::cwx::editor::gui::dwt::absdialog { class AbsDialog; }
namespace cwxeditor::cwx::editor::gui::dwt::areaviewutils { class Preview; }

namespace cwxeditor::cwx::editor::gui::dwt::cardpane {
namespace variables = ::cwxeditor::cwx::variables;

namespace lib = ::cwxeditor::lib;
namespace card = ::cwxeditor::cwx::card;
namespace path = ::cwxeditor::cwx::path;
namespace summary_model = ::cwxeditor::cwx::summary;
namespace customtable = ::cwxeditor::cwx::editor::gui::dwt::customtable;
namespace cardlist = ::cwxeditor::cwx::editor::gui::dwt::cardlist;

using String = std::string;
using StringView = std::string_view;
using Summary = summary_model::Summary;
using SummaryPtr = lib::Owner<Summary>;
using CWXPath = path::CWXPath;
using CWXPathPtr = lib::Owner<CWXPath>;
using Card = card::Card;
using CardPtr = lib::Owner<Card>;
using CastCard = card::CastCard;
using CastCardPtr = lib::Owner<CastCard>;
using SkillCard = card::SkillCard;
using SkillCardPtr = lib::Owner<SkillCard>;
using ItemCard = card::ItemCard;
using ItemCardPtr = lib::Owner<ItemCard>;
using BeastCard = card::BeastCard;
using BeastCardPtr = lib::Owner<BeastCard>;
using InfoCard = card::InfoCard;
using InfoCardPtr = lib::Owner<InfoCard>;
using EffectCard = card::EffectCard;
using EffectCardPtr = lib::Owner<EffectCard>;
using MenuID = ::cwxeditor::cwx::types::MenuID;
using CardImage = card::CardImage;
using CardImagePtr = lib::Owner<CardImage>;
using XNode = xml::XNode;
using XMLInfo = system::XMLInfo;
using Parent = lib::Owner<lib::Container>;
using PanelPtr = lib::Owner<lib::Panel>;
using WidgetPtr = lib::WidgetPtr;
using TableModelPtr = customtable::TableModelPtr;
using TableColumnPtr = customtable::TableColumnPtr;

enum class CViewMode { INIT, LIFE, CARD, TABLE };
enum class CardTableColumn { ID, Name, Desc, UC, Num };
enum class CardType { Cast, Skill, Item, Beast, Info };
enum class OwnerType { Summary, Cast };

// 素材処理はカードドロップの一部だが、カード自
// 体を拒否するのは明示的なユーザーの中止だけ。
// ファイル欠落や個別のコピー失敗は別に報告
// し、復元したカードは取り込めるようにする。
enum class MaterialCopyResult {
    ContinuedWithoutCopy,
    Copied,
    CopiedWithErrors,
    Cancelled,
};

struct SelectionEvent {
    lib::Modifiers modifiers{lib::Modifiers::None};
    bool handled{};
};

template <typename... Arguments>
class Signal {
public:
    using Callback = std::function<void(Arguments...)>;
    using Token = std::uint64_t;

    [[nodiscard]] Token add(Callback callback) {
        if (!callback) return 0U;
        const auto token = next_token_++;
        callbacks_.push_back({token, std::move(callback)});
        return token;
    }
    void remove(Token token) noexcept {
        callbacks_.erase(std::remove_if(callbacks_.begin(), callbacks_.end(),
                                        [token](const Entry& value) {
                                            return value.token == token;
                                        }), callbacks_.end());
    }
    void call(Arguments... arguments) const {
        const auto copy = callbacks_;
        for (const auto& value : copy)
            if (value.callback) value.callback(arguments...);
    }

private:
    struct Entry {
        Token token{};
        Callback callback;
    };
    std::vector<Entry> callbacks_;
    Token next_token_{1U};
};

/// ペインが使う共通プロパティ。 ホストはウィンドウAPI
/// を公開せず、翻訳ラベルや保存済みの表設定を指定できる。
struct Props {
    const ::cwxeditor::cwx::props::CProps* coreProps{};
    const variables::FlexEtcProps* coreVars{};
    String cardId{"ID"};
    String cardName{"Name"};
    String cardDesc{"Description"};
    String cardCount{"Count"};
    String level{"Level"};
    String useCount{"Uses"};
    String infinity{"∞"};
    String cardStatus{"Cards: %zu"};
    String cardStatusSelOne{"Cards: %zu, selected: %llu"};
    String cardStatusSelMulti{"Cards: %zu, selected: %zu"};
    String handCardStatus{"Cards: %zu / %zu"};
    String handCardStatusSelOne{"Cards: %zu / %zu, selected: %llu"};
    String handCardStatusSelMulti{"Cards: %zu / %zu, selected: %zu"};
    // CardPane::toolTipは、D互換の位置指定付きtryFormat構文を使う。
    String cardIsReference{"Card %1$s references %2$s"};
    String referencedCardIsNotFound{"Referenced card %1$s is missing"};
    String cardTabName{"Cards: %s"};
    String castName{"Cast"};
    String skillName{"Skill"};
    String itemName{"Item"};
    String beastName{"Beast"};
    String infoName{"Info"};
    String cwxPathPrefix{"card"};
    lib::ResourceId cardImage{};
    lib::ResourceId castImage{};
    lib::ResourceId skillImage{};
    lib::ResourceId itemImage{};
    lib::ResourceId beastImage{};
    lib::ResourceId infoImage{};
    lib::ResourceId handImage{};
    lib::ResourceId handEmptyImage{};
    lib::ResourceId eventTreeImage{};
    lib::ResourceId eventTreeEmptyImage{};
    // DのsetupComment()相当。コメントのあるカード画像へ印を描く。
    lib::ResourceId commentImage{};
    bool logicalSort{};
    bool readOnly{};
    bool showCardListHeader{true};
    bool showCardListTitle{true};
    bool showHandMark{true};
    bool showHandMarkAlways{true};
    bool showEventTreeMark{true};
    bool showEventTreeMarkAlways{true};
    bool ignoreEmptyStart{};
    bool linkCard{true};
    bool xmlCopy{};
    bool ignorePaths{};
    std::uint32_t castLevelMax{10U};
    std::uint32_t skillLevelMax{10U};
    std::uint32_t useCountMax{99U};
    std::size_t nameLimit{64U};
    std::size_t castNameLimit{64U};
    std::size_t undoMax{64U};
    float rowHeight{22.0F};
    float headerHeight{24.0F};
    float cardIdColumn{50.0F};
    float cardNameColumn{100.0F};
    float cardNumberColumn{60.0F};
    float cardDescriptionColumn{280.0F};
    float cardCountColumn{60.0F};
    float handCardIdColumn{50.0F};
    float handCardNameColumn{100.0F};
    float handCardNumberColumn{60.0F};
    float handCardDescriptionColumn{50.0F};
    float importCardIdColumn{50.0F};
    float importCardNameColumn{100.0F};
    float importCardNumberColumn{60.0F};
    float importCardDescriptionColumn{50.0F};
    float importCardCountColumn{30.0F};
    float importHandCardIdColumn{50.0F};
    float importHandCardNameColumn{100.0F};
    float importHandCardNumberColumn{60.0F};
    float importHandCardDescriptionColumn{50.0F};
    int mainCardsSortColumn{};
    int mainCardsSortDirection{1};
    int handCardsSortColumn{};
    int handCardsSortDirection{1};
    int importCardsSortColumn{};
    int importCardsSortDirection{1};
    int importHandCardsSortColumn{};
    int importHandCardsSortDirection{1};
    int cardWidth{95};
    int cardHeight{130};
    int castCardWidth{95};
    int castCardHeight{130};
    int cardsMarginX{5};
    int cardsSpaceX{8};
    int cardsMarginY{5};
    int cardsSpaceY{8};
    int cardsTitleSpace{2};
    int cardsFocusLinePadding{2};
    int cardsDefaultWrap{4};
    dprops::Props* editorProps{};
    std::shared_ptr<lib::Window> dialogOwner{};
};

class CardPane;
using CardPanePtr = lib::Owner<CardPane>;

/// CardPaneが使うダイアログの契約。具体的な画面は適用・終了
/// の通知を接続でき、 ヘッドレスの呼び出し元はモデルを直接使える。
class CardDialog {
public:
    virtual ~CardDialog() = default;
    [[nodiscard]] virtual CardPtr card() const { return card_; }
    [[nodiscard]] virtual bool openCWXPath(StringView value, bool shellActivate);
    [[nodiscard]] virtual bool open();
    virtual void active();
    virtual void forceCancel();
    void card(CardPtr value) { card_ = std::move(value); }
    [[nodiscard]] String openedPath() const { return opened_path_; }
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    [[nodiscard]] bool isActive() const noexcept { return active_; }
    // 中止可能な準備を終え、プロパティを書き込む直前に呼ぶ。
    std::function<void()> applying;
    // ホストの意図を明示する。切り離された既存カードも編集モードとして扱う。
    bool creationMode{};
    std::function<void()> applied;
    std::function<void()> closed;
private:
    CardPtr card_;
    String opened_path_;
    bool open_{};
    bool active_{};
};
using CardDialogPtr = lib::Owner<CardDialog>;

/// DのCommonsのデリゲート群に対応する共通の型。
struct Commons {
    lib::Services services{};
    std::function<String(const SummaryPtr&)> materialPath{};
    std::function<void(StringView)> refPaths{};
    lib::Action refreshToolBar{};
    std::function<bool()> stopOpen{};
    lib::Action refUseCount{};
    // 手札コレクションの変更は、DのrefCastと同様に所有するCastも更
    // 新する。 貼り付け・削除と、それらのUndo/Redoでも同じにする。
    lib::Action refOwner{};
    lib::Action refScenario{};
    lib::Action refUndoMax{};
    lib::Action refShowCardListHeader{};
    lib::Action refShowCardListTitle{};
    lib::Action refImageScale{};
    lib::Action refEventTree{};
    lib::Action refSkin{};
    lib::Action delPaths{};
    lib::Action replText{};
    std::function<void(CardType, bool)> openCardPane{};
    std::function<CardPanePtr(CardType, bool)> openPane{};
    std::function<void(const SummaryPtr&, const CastCardPtr&, bool)> openHands{};
    std::function<CardPanePtr(const SummaryPtr&, const CastCardPtr&, CardType, bool)> openHandsPane{};
    std::function<void(const SummaryPtr&, const EffectCardPtr&, bool)> openUseEvents{};
    std::function<CardDialogPtr(CardType, const SummaryPtr&, const CWXPathPtr&,
                                const CardPtr&, bool)> makeDialog{};
    std::function<void(const SummaryPtr&, const std::vector<String>&)> doImport{};
    std::function<bool(StringView, bool)> openCWXPath{};
    std::function<void(StringView, bool, bool)> openFilePath{};
    std::function<void(CardType, std::uint64_t, bool)> replaceID{};
    std::function<void(const CardPtr&)> refCard{};
    std::function<void(const CWXPathPtr&, const CardPtr&)> delCard{};
    std::function<void(const lib::WidgetPtr&, String)> setStatusLine{};
    std::function<lib::ImageData(const CardPtr&)> cardImageData{};
    // LIFEモードは同じCardList配置を使う
    // が、キャスト画像には初期HPゲージを含める。
    std::function<lib::ImageData(const CardPtr&)> cardLifeImageData{};
    std::function<bool(const CardPtr&)> cardImageHasCaption{};
    std::function<std::shared_ptr<comment::CommentDialog>(const SummaryPtr&, const CardPtr&,
                                                           StringView)> makeCommentDialog{};
    std::function<String(const CardPtr&)> commentTitle{};
    /// 編集・取り込み、Summary・手札の文脈に応じたDのカード一覧の並
    /// べ替え設定を保存する。 方向にはSortDirの固定値1/2を使う。
    std::function<void(bool, bool, int, int)> storeSort{};
    std::function<void(bool, bool, int, float)> storeColumnWidth{};
    std::shared_ptr<Signal<const void*, CardTableColumn, float>> columnWidthEvent{
        std::make_shared<Signal<const void*, CardTableColumn, float>>()};
    // 同じCardWindowの複数タブは列を共有する
    // が、無関係なCardWindowへは通知しない。
    bool localColumnWidthSync{};
    std::function<std::vector<String>(const SummaryPtr&,
                                     const ::cwxeditor::cwx::path::CWXPath&)>
        warnings{};
    lib::ResourceId warningImage{};
    // 正式なペインはツールキットに依存させない。製品側がDのcreateMenuItem
    // と同じ MenuIDの文字列解決を渡し、ポップアップに使う。
    std::function<String(MenuID)> menuText{};
    std::shared_ptr<Signal<bool, bool, CardTableColumn, int>> refSortParamsEvent{
        std::make_shared<Signal<bool, bool, CardTableColumn, int>>()};
    std::shared_ptr<Signal<>> appearanceEvent{};
    std::shared_ptr<Signal<CardPtr, bool>> cardChangedEvent{
        std::make_shared<Signal<CardPtr, bool>>()};
    Signal<CardPtr> refCardEvent;
    Signal<CardPtr> delCardEvent;
    Signal<CardTableColumn, float> refCardTableColumnWidth;
};

/// 共通Editorのペイン間で使う、クリップボード・選択操作のインターフェース。
class TCPD {
public:
    virtual ~TCPD() = default;
    virtual void cut(SelectionEvent&) = 0;
    virtual void copy(SelectionEvent&) = 0;
    virtual void paste(SelectionEvent&) = 0;
    virtual void del(SelectionEvent&) = 0;
    virtual void clone(SelectionEvent&) = 0;
    [[nodiscard]] virtual bool canDoTCPD() const noexcept = 0;
    [[nodiscard]] virtual bool canDoT() const noexcept = 0;
    [[nodiscard]] virtual bool canDoC() const noexcept = 0;
    [[nodiscard]] virtual bool canDoP() const noexcept = 0;
    [[nodiscard]] virtual bool canDoD() const noexcept = 0;
    [[nodiscard]] virtual bool canDoClone() const noexcept = 0;
    virtual void undo() = 0;
    virtual void redo() = 0;
    [[nodiscard]] virtual bool canUndo() const noexcept = 0;
    [[nodiscard]] virtual bool canRedo() const noexcept = 0;
    virtual void replaceID() = 0;
    [[nodiscard]] virtual bool canReplaceID() const noexcept = 0;
    virtual void edit() = 0;
    [[nodiscard]] virtual bool canEdit() const noexcept = 0;
    virtual void reNumbering() = 0;
    [[nodiscard]] virtual bool canReNumbering() const noexcept = 0;
    virtual void up() = 0;
    virtual void down() = 0;
    [[nodiscard]] virtual bool canUp() const noexcept = 0;
    [[nodiscard]] virtual bool canDown() const noexcept = 0;
    [[nodiscard]] virtual const String& statusLine() const noexcept = 0;
    [[nodiscard]] virtual std::vector<String> openedCWXPath() const = 0;
};

class CardPane : public TCPD,
                 public std::enable_shared_from_this<CardPane> {
public:
    CardPane(Commons comm, Props prop, SummaryPtr summary, Parent parent,
             OwnerType ownerType, CardType cardType, customtable::Style style,
             SummaryPtr toc = {},
             std::function<void(const CastCardPtr&)> openHand = {});
    ~CardPane() override;

    CardPane(const CardPane&) = delete;
    CardPane& operator=(const CardPane&) = delete;

    [[nodiscard]] bool editMode() const noexcept { return !toc_; }
    [[nodiscard]] bool canHold() const noexcept {
        return !read_only_ && owner_type_ == OwnerType::Cast &&
               (card_type_ == CardType::Skill || card_type_ == CardType::Item);
    }
    [[nodiscard]] bool useNum() const noexcept { return card_type_ != CardType::Info; }
    [[nodiscard]] int colIndex(CardTableColumn column) const noexcept;

    [[nodiscard]] std::vector<CardPtr> cards() const;
    [[nodiscard]] std::vector<CardPtr> cardsNarrow() const;
    [[nodiscard]] std::size_t narrowCount() const noexcept;
    [[nodiscard]] static std::vector<CardPtr> cardsFrom(CardType type,
                                                        const CWXPathPtr& owner);
    [[nodiscard]] static CardPtr cardFrom(const CWXPathPtr& owner, CardType type,
                                          std::uint64_t id);
    [[nodiscard]] CardPtr card(std::uint64_t id) const;
    [[nodiscard]] CardPtr localCard(std::uint64_t id) const;
    [[nodiscard]] static std::uint64_t linkId(const CardPtr& value) noexcept;
    [[nodiscard]] CardPtr baseCard(const CardPtr& value) const;
    [[nodiscard]] String cardName(const CardPtr& value) const;
    [[nodiscard]] String cardDesc(const CardPtr& value) const;
    [[nodiscard]] std::uint32_t cardNum(const CardPtr& value) const;
    [[nodiscard]] std::vector<CardPtr> pOwnerCards() const;
    [[nodiscard]] CardPtr pOwnerCard(std::uint64_t id) const;
    [[nodiscard]] std::vector<CardType> types() const;
    [[nodiscard]] static String xmlNameFrom(CardType type);
    [[nodiscard]] String xmlName() const;
    [[nodiscard]] String xmlNameM() const;
    [[nodiscard]] CardPtr createFromNode(XNode& node, const XMLInfo& version) const;

    static void change(const lib::Owner<::cwxeditor::cwx::usecounter::UseCounter>& counter,
                       CardType type, std::uint64_t from, std::uint64_t to);
    [[nodiscard]] static int ucGet(
        const lib::Owner<::cwxeditor::cwx::usecounter::UseCounter>& counter,
        CardType type, std::uint64_t id);
    [[nodiscard]] static std::ptrdiff_t indexOf(const CWXPathPtr& owner,
                                                CardType type,
                                                const CardPtr& value);
    static CardPtr insert(const CWXPathPtr& owner, std::ptrdiff_t index,
                       const CardPtr& value, bool preserveIdentity = false);
    static void remove(const CWXPathPtr& owner, const CardPtr& value);
    [[nodiscard]] static std::uint64_t add(const SummaryPtr& owner,
                                           const CardPtr& value);
    [[nodiscard]] static CardPtr add(const CastCardPtr& owner,
                                     const CardPtr& value);
    [[nodiscard]] CardPanePtr openSameLevelPane(CardType type, bool shellActivate);
    [[nodiscard]] static CardType cardTypeFrom(const CardPtr& value);

    [[nodiscard]] PanelPtr construct(const Parent& parent = {});
    void reconstruct(const Parent& parent, customtable::Style style);
    void reconstruct(const Parent& parent = {});
    void constructCardList(const Parent& parent);
    void construct1(Commons comm, Props prop, SummaryPtr summary,
                    OwnerType ownerType, CardType cardType,
                    customtable::Style style);
    void refEventTree();
    void refScenario(SummaryPtr summary);
    void refUndoMax();
    void setUndoLimit(std::size_t value);
    [[nodiscard]] std::size_t undoLimit() const noexcept { return prop_.undoMax; }
    void closePreview();
    [[nodiscard]] bool canIncSearch() const noexcept {
        return inc_search_ && !cards_.empty();
    }
    void incSearch();
    void selectChanged();
    void selectOnly(std::ptrdiff_t index);
    void showCardListImpl(CViewMode mode);
    void updateToolTip();
    void refColumnWidth(CardTableColumn column, float width);

    [[nodiscard]] const CWXPathPtr& owner() const noexcept { return owner_; }
    [[nodiscard]] const SummaryPtr& summary() const noexcept { return summary_; }
    [[nodiscard]] const PanelPtr& pane() const noexcept { return pane_; }
    [[nodiscard]] WidgetPtr widget() const noexcept;
    [[nodiscard]] const TableModelPtr& table() const noexcept { return table_; }
    [[nodiscard]] TableModelPtr cardTable() const noexcept { return table_; }
    [[nodiscard]] WidgetPtr inputWidget() const noexcept {
        return view_mode_ == CViewMode::TABLE
            ? std::static_pointer_cast<lib::Widget>(table_)
            : std::static_pointer_cast<lib::Widget>(image_list_);
    }
    [[nodiscard]] const std::shared_ptr<cardlist::CardList<CardPtr>>& imageList() const noexcept {
        return image_list_;
    }
    [[nodiscard]] std::map<CardTableColumn, TableColumnPtr> columns() const;
    [[nodiscard]] const String& statusLine() const noexcept override { return status_line_; }
    [[nodiscard]] CardType cardType() const noexcept { return card_type_; }
    [[nodiscard]] String ownerId() const;
    [[nodiscard]] SummaryPtr ownerScenario() const;
    [[nodiscard]] String ownerWsnVersion() const;
    [[nodiscard]] String cardTitle(const CardPtr& value) const;

    void refresh();
    void refreshCommentTitles();
    void refresh(const CardPtr& value, bool updateStatus = true);
    void refreshAll(SummaryPtr summary, CWXPathPtr owner);
    void refreshImpl();
    void syncCardListScale();
    void refreshTableItem(std::size_t row, const CardPtr& value);
    void createTableItem(const CardPtr& value, std::ptrdiff_t index = -1);
    void refreshListItem(std::size_t row, const CardPtr& value);
    void refreshIDs();
    void refreshUseCount();
    void refreshStatusLine();
    void refreshR(StringView from = {}, StringView to = {});
    void refList();
    void refTbl();
    void showCardLife();
    void showCardList();
    void showCardTable();
    [[nodiscard]] CViewMode viewMode() const noexcept { return view_mode_; }
    void updateLayout();

    [[nodiscard]] CardTableColumn columnVal(const TableColumnPtr& column) const;
    [[nodiscard]] static int columnToInt(CardTableColumn column) noexcept;
    [[nodiscard]] TableColumnPtr columnFromInt(int column) const;
    void sort();
    void sort(std::vector<CardPtr>& values) const;
    [[nodiscard]] bool compID(const CardPtr& left, const CardPtr& right) const;
    [[nodiscard]] bool compName(const CardPtr& left, const CardPtr& right) const;
    [[nodiscard]] bool compDesc(const CardPtr& left, const CardPtr& right) const;
    [[nodiscard]] bool compUC(const CardPtr& left, const CardPtr& right) const;
    [[nodiscard]] bool compNum(const CardPtr& left, const CardPtr& right) const;
    [[nodiscard]] bool revCompID(const CardPtr& left, const CardPtr& right) const;
    [[nodiscard]] bool revCompName(const CardPtr& left, const CardPtr& right) const;
    [[nodiscard]] bool revCompDesc(const CardPtr& left, const CardPtr& right) const;
    [[nodiscard]] bool revCompUC(const CardPtr& left, const CardPtr& right) const;
    [[nodiscard]] bool revCompNum(const CardPtr& left, const CardPtr& right) const;
    void refSortParams(CardTableColumn column, customtable::SortDirection direction);
    void sorted();

    void nameEditEnd(std::size_t row, String value);
    void numEditEnd(std::size_t row, std::uint32_t value);
    [[nodiscard]] bool canEditT(std::size_t row, std::size_t column) const;
    [[nodiscard]] String toolTip(const CardPtr& value) const;
    void setSearchText(String value);
    [[nodiscard]] const String& searchText() const noexcept { return search_text_; }
    [[nodiscard]] std::vector<String> getWarnings(const CardPtr& value) const;

    void select(const std::vector<std::size_t>& indices);
    void select(std::ptrdiff_t index);
    [[nodiscard]] bool debugTouch(std::size_t index);
    void selectID(std::uint64_t id);
    void selectID(const std::vector<std::uint64_t>& ids);
    [[nodiscard]] std::vector<CardPtr> selectedCards() const;
    [[nodiscard]] std::ptrdiff_t selectionIndex() const noexcept;
    [[nodiscard]] std::vector<std::size_t> selectionIndices() const;
    [[nodiscard]] std::vector<std::uint64_t> selectionIDs() const;
    [[nodiscard]] CardPtr selection() const;
    [[nodiscard]] std::size_t selectionCount() const noexcept;
    void deselectAll();
    void selectAll();
    [[nodiscard]] bool canSelectAll() const noexcept;
    [[nodiscard]] bool isSelected() const noexcept { return !selection_.empty(); }

    void open(bool shellActivate);
    void create();
    [[nodiscard]] bool addFromNode(XNode& node, const XMLInfo& version,
                                   std::optional<std::size_t> dropIndex = {},
                                   bool moveExisting = false);
    void addCards(const std::vector<CardPtr>& values);
    [[nodiscard]] bool addCardsImpl(const std::vector<CardPtr>& values,
                                    bool inPane, bool samePane,
                                    bool sameSummary, bool topLevel,
                                    std::optional<std::size_t> insertIndex = {});
    void pasteRefresh(const std::vector<CardPtr>& values,
                      std::optional<std::vector<CardPtr>> previousTableDisplay);
    void doImport();
    [[nodiscard]] bool canDoImport() const noexcept;
    [[nodiscard]] bool canReNumbering() const noexcept;
    void reNumbering() override;
    void reNumbering(std::ptrdiff_t index, std::uint64_t newId);
    [[nodiscard]] CardDialogPtr edit(const CardPtr& value);
    [[nodiscard]] CardDialogPtr edit(std::ptrdiff_t index);
    void edit() override;
    void editUseEvent(bool canDuplicate = false);
    void editUseEvent(const CardPtr& value, bool canDuplicate = false);
    void editHand();
    void editHandWith(const CastCardPtr& value);
    [[nodiscard]] bool canEdit() const noexcept override;
    [[nodiscard]] bool canEditUseEvent() const noexcept;
    [[nodiscard]] bool canEditHand() const noexcept;
    [[nodiscard]] bool canWriteComment() const noexcept;
    void writeComment();
    void writeCommentImpl(const CardPtr& value);
    void hold(bool checked);
    void removeRef();
    [[nodiscard]] bool canRemoveRef() const noexcept;
    [[nodiscard]] bool canSelectConnectedResource() const noexcept;
    void selectConnectedResource();

    void moveCards(const std::vector<CardPtr>& values, std::ptrdiff_t index,
                   bool store = true);
    void selectMovedCards(const std::vector<CardPtr>& values);
    static void moveCards(const CWXPathPtr& owner, CardType type,
                          const std::vector<CardPtr>& values,
                          std::ptrdiff_t index);
    void refreshLink(CardPtr& value, bool samePane, bool sameSummary,
                     bool topLevel);
    void insert(const CardPtr& value, bool move);
    void refCard(const CardPtr& value, bool updateStatus = true);
    static void refCard(CardPane* view, Commons& comm, const CardPtr& value,
                        bool updateStatus = true);
    void delCard(const CardPtr& value);
    static void delCard(CardPane* view, Commons& comm, const CWXPathPtr& owner,
                        const CardPtr& value);
    static void delImpl(CardPane* view, Commons& comm, const CWXPathPtr& owner,
                        const CardPtr& value);

    void toNode(XNode& parent, const std::vector<CardPtr>& values, bool del) const;
    [[nodiscard]] String toXML(const std::vector<CardPtr>& values, bool del) const;
    [[nodiscard]] MaterialCopyResult qCardMaterialCopy(
        const XNode& node, const std::vector<CardPtr>& values) const;

    [[nodiscard]] bool canUp() const noexcept override;
    [[nodiscard]] bool canDown() const noexcept override;
    void up() override;
    void down() override;
    [[nodiscard]] bool canUndo() const noexcept override;
    [[nodiscard]] bool canRedo() const noexcept override;
    void undo() override;
    void redo() override;
    [[nodiscard]] bool canReplaceID() const noexcept override;
    void replaceID() override;
    bool enterEdit();
    void cancelEdit();
    [[nodiscard]] std::vector<String> openedCWXPath() const override;

    void cut(SelectionEvent&) override;
    void copy(SelectionEvent&) override;
    void paste(SelectionEvent&) override;
    void del(SelectionEvent&) override;
    void clone(SelectionEvent&) override;
    [[nodiscard]] bool canDoTCPD() const noexcept override;
    [[nodiscard]] bool canDoT() const noexcept override;
    [[nodiscard]] bool canDoC() const noexcept override;
    [[nodiscard]] bool canDoP() const noexcept override;
    [[nodiscard]] bool canDoD() const noexcept override;
    [[nodiscard]] bool canDoClone() const noexcept override;

    [[nodiscard]] bool openCWXPath(StringView value, bool shellActivate);
    void copyImpl(bool del);
    void copySelectionXML();
    void editM();
    void numCreateEditor(std::size_t row, std::size_t column);
    void listEditEnd(std::size_t row, String value);
    void listCreateEditor(const CardPtr& value);

private:
    void beginCardListNameEdit(const CardPtr& value);
    struct MaterialCopyPlan {
        using Counter = ::cwxeditor::cwx::usecounter::UseCounter;
        using PathId = ::cwxeditor::cwx::usecounter::PathId;

        struct MaterialPath {
            PathId id;
            String value;
            std::filesystem::path source;
            bool binary_image{};
        };

        std::vector<CardPtr> values;
        std::shared_ptr<Counter> counter;
        std::vector<MaterialPath> candidates;
        std::vector<String> normal_source_names;
        String source_name;
        String source_author;
        std::filesystem::path target_root;
        std::size_t binary_count{};

        ~MaterialCopyPlan();
        void detach() noexcept;
    };

    [[nodiscard]] std::vector<CardPtr> createCardsFromNode(
        XNode& node, const XMLInfo& version) const;
    [[nodiscard]] std::shared_ptr<MaterialCopyPlan> prepareMaterialCopyPlan(
        const XNode& node, std::vector<CardPtr> values) const;
    [[nodiscard]] MaterialCopyResult applyMaterialCopy(
        const std::shared_ptr<MaterialCopyPlan>& plan,
        bool externalizeBinary, bool copyReferences, bool showWarning) const;
    [[nodiscard]] bool beginExternalMaterialDrop(
        XNode& node, const XMLInfo& version,
        std::optional<std::size_t> dropIndex);
    void selectMovedCardsImpl(const std::vector<CardPtr>& values,
                              bool revealLast);
    void showMaterialCopyWarningAsync() const;
    [[nodiscard]] lib::DropOperation performDrop(
        const lib::ClipboardData& data, lib::DropOperation selected,
        std::optional<std::size_t> dropIndex);

    struct UndoRecord {
        std::function<void()> undo;
        std::function<void()> redo;
        String label;
        // フォーカス通知の前に取得する、任意の単独再生用スナップショット。
        std::function<void()> beforeReplay;
    };
    class CPUndo {
    public:
        CPUndo(CardPane* view, std::function<void()> undo,
               std::function<void()> redo);
        virtual ~CPUndo() = default;
        virtual void undo();
        virtual void redo();
        virtual void dispose();
        [[nodiscard]] CardPane* view() const noexcept { return view_; }
    private:
        CardPane* view_{};
        std::function<void()> undo_;
        std::function<void()> redo_;
    };
    class UndoIDs final : public CPUndo {
    public:
        UndoIDs(CardPane* view, std::function<void()> undo,
                std::function<void()> redo)
            : CPUndo(view, std::move(undo), std::move(redo)) {}
    };
    class UndoEdit final : public CPUndo {
    public:
        UndoEdit(CardPane* view, std::function<void()> undo,
                 std::function<void()> redo)
            : CPUndo(view, std::move(undo), std::move(redo)) {}
    };
    class UndoSwap final : public CPUndo {
    public:
        UndoSwap(CardPane* view, std::function<void()> undo,
                 std::function<void()> redo)
            : CPUndo(view, std::move(undo), std::move(redo)) {}
    };
    class UndoMove final : public CPUndo {
    public:
        UndoMove(CardPane* view, std::function<void()> undo,
                 std::function<void()> redo)
            : CPUndo(view, std::move(undo), std::move(redo)) {}
    };
    class UndoInsertDelete final : public CPUndo {
    public:
        UndoInsertDelete(CardPane* view, std::function<void()> undo,
                         std::function<void()> redo)
            : CPUndo(view, std::move(undo), std::move(redo)) {}
    };

    Commons comm_;
    Props prop_;
    SummaryPtr summary_;
    CWXPathPtr owner_;
    SummaryPtr toc_;
    Parent parent_;
    PanelPtr pane_;
    TableModelPtr table_;
    std::shared_ptr<cardlist::CardList<CardPtr>> image_list_;
    float card_list_image_scale_{};
    std::shared_ptr<lib::Menu> context_menu_;
    std::map<CardTableColumn, TableColumnPtr> columns_;
    std::unique_ptr<customtable::FullTableColumn> full_column_;
    std::unique_ptr<customtable::TableSorter<CardPtr>> id_sorter_;
    std::unique_ptr<customtable::TableSorter<CardPtr>> name_sorter_;
    std::unique_ptr<customtable::TableSorter<CardPtr>> desc_sorter_;
    std::unique_ptr<customtable::TableSorter<CardPtr>> uc_sorter_;
    std::unique_ptr<customtable::TableSorter<CardPtr>> num_sorter_;
    OwnerType owner_type_{OwnerType::Summary};
    CardType card_type_{CardType::Cast};
    customtable::Style style_{};
    CViewMode view_mode_{CViewMode::INIT};
    bool read_only_{};
    String id_;
    String status_line_;
    String search_text_;
    std::shared_ptr<incsearch::IncSearch> inc_search_;
    std::shared_ptr<absdialog::AbsDialog> renumber_dialog_;
    std::uint64_t sort_params_token_{};
    std::uint64_t appearance_token_{};
    std::uint64_t card_changed_token_{};
    std::shared_ptr<int> renumber_lifetime_{std::make_shared<int>(0)};
    std::uint64_t owner_binding_generation_{};
    // 一覧を明示的に接続し直したら、文書・所有者が同じ
    // でも処理中の選択一括オープンを終了する。 ダイア
    // ログの接続は、実際の文書置き換えだけに連動する。
    std::uint64_t open_cohort_generation_{};
    std::vector<CardPtr> cards_;
    std::vector<std::size_t> selection_;
    std::function<void(const CastCardPtr&)> open_hand_;
    CardPtr open_event_target_;
    std::shared_ptr<areaviewutils::Preview> card_preview_;
    CardPtr preview_card_;
    std::uint64_t preview_revision_{};
    void previewTrigger(lib::Point local);

    std::vector<UndoRecord> undo_;
    std::size_t undo_position_{};
    // フォーカス通知が履歴を変更した場合、フォーカス前のスナップショットを無効にする。
    std::uint64_t undo_history_revision_{};
    std::map<Card*, CardDialogPtr> edit_dialogs_;
    std::map<Card*, std::shared_ptr<comment::CommentDialog>> comment_dialogs_;
    bool sorting_{};
    std::uint64_t render_revision_{};
    bool editing_{};
    bool finishing_inline_edit_{};
    std::uint64_t selection_token_{};
    std::uint64_t edit_token_{};
    std::uint64_t resize_token_{};
    std::uint64_t shared_resize_token_{};
    std::shared_ptr<bool> resizing_column_{std::make_shared<bool>(false)};
    void columnResized(CardTableColumn column, float width, bool broadcast);

    void pushUndo(UndoRecord record);
    void notifyCollectionChanged();
    void restoreIDState(const std::vector<std::uint64_t>& ids, std::size_t first = 0U);
    void installInputHandlers();
    void showContextMenu(const lib::WidgetPtr& owner, lib::Point position);
    void rebuildContextMenu();
    void bindDragDrop();
    [[nodiscard]] bool idSorted() const;
    [[nodiscard]] bool acceptsCardDrop(bool samePane) const;
    void setSelectionFromTable(const std::vector<std::size_t>& indices);
    void syncSelectionControls(bool reveal = true);
    void selectIndices(const std::vector<std::size_t>& indices, bool reveal);
    [[nodiscard]] lib::TableRow prepareRow(const CardPtr& value) const;
    void setRows(std::vector<CardPtr> values, std::vector<std::size_t> selected,
                 const CardPtr& changed = {});
    void setViewMode(CViewMode mode);
    [[nodiscard]] std::uint64_t nextId() const;
    [[nodiscard]] CardPtr duplicateCard(const CardPtr& value) const;
    [[nodiscard]] CardPtr addCard(const CardPtr& value, std::ptrdiff_t index = -1);
    void removeCard(const CardPtr& value);
    [[nodiscard]] String cardPath(const CardPtr& value) const;
    [[nodiscard]] bool ownerIsSummary() const noexcept;
    [[nodiscard]] String ownerScenarioId() const;
    [[nodiscard]] String ownerScenarioPath() const;
    [[nodiscard]] String ownerScenarioName() const;
    [[nodiscard]] String ownerScenarioAuthor() const;
    [[nodiscard]] CardPtr makeCard(CardType type, std::uint64_t id,
                                    String name = {}, String desc = {}) const;
    [[nodiscard]] bool sameCard(const CardPtr& left, const CardPtr& right) const noexcept;
    void swapCards(std::size_t first, std::size_t second);

    void updateCardSelection();
    void notifyToolbar();
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::cardpane
