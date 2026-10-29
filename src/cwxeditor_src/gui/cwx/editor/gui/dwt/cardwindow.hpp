#pragma once

// cwx/editor/gui/dwt/cardwindow.d
// のC++20移植。複数のカードペインを所有
// して操作を振り分ける。 Dの役割分担を保ち
// 、共通libの部品・イベント・ドラッグと、
// 移植済みのCard/Summaryを使う。

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areatable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/cardlist.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/cardpane.hpp"
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
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::cardwindow {

namespace lib = ::cwxeditor::lib;
namespace area = ::cwxeditor::cwx::area;
namespace card = ::cwxeditor::cwx::card;
namespace path = ::cwxeditor::cwx::path;
namespace summary_model = ::cwxeditor::cwx::summary;
namespace customtable = ::cwxeditor::cwx::editor::gui::dwt::customtable;
namespace canonical_areatable = ::cwxeditor::cwx::editor::gui::dwt::areatable;
namespace cardlist = ::cwxeditor::cwx::editor::gui::dwt::cardlist;
namespace canonical_cardpane = ::cwxeditor::cwx::editor::gui::dwt::cardpane;
namespace comment = ::cwxeditor::cwx::editor::gui::dwt::comment;

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

using Parent = lib::Owner<lib::Container>;
using WidgetPtr = lib::WidgetPtr;
using PanelPtr = lib::Owner<lib::Panel>;
using WindowPtr = lib::Owner<lib::Window>;
using TabViewPtr = lib::Owner<lib::TabView>;
using TableModelPtr = customtable::TableModelPtr;

enum class CardWindowKind {
    Cast,
    Skill,
    Item,
    Beast,
    Info,
    Hand,
    ImportSource,
    ImportSourceHand,
};

enum class CardType { Cast, Skill, Item, Beast, Info };
enum class OwnerType { Summary, Cast };
enum class CViewMode { INIT, LIFE, CARD, TABLE };

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
        const auto token = state_->next_token++;
        state_->entries.push_back({token, std::move(callback)});
        return token;
    }
    void remove(Token token) noexcept {
        state_->entries.erase(std::remove_if(state_->entries.begin(), state_->entries.end(),
                                      [token](const Entry& entry) {
                                           return entry.token == token;
                                       }), state_->entries.end());
    }
    void call(Arguments... arguments) const {
        const auto entries = state_->entries;
        for (const auto& entry : entries)
            if (entry.callback) entry.callback(arguments...);
    }

private:
    struct Entry {
        Token token{};
        Callback callback;
    };
    struct State {
        std::vector<Entry> entries;
        Token next_token{1U};
    };
    // Dのデリゲート項目は参照として振る舞うため、 Commons
    // のコピーからも元の値へ登録したハンドラーに通知する。
    std::shared_ptr<State> state_{std::make_shared<State>()};
};

struct Props {
    String cardTabName{"Cards: %s"};
    String handCardTabName{"Hand %llu: %s"};
    String importSourceTabName{"Import: %s (%s)"};
    String castName{"Cast"};
    String skillName{"Skill"};
    String itemName{"Item"};
    String beastName{"Beast"};
    String infoName{"Info"};
    String areasTabName{"Areas"};
    String areaIdLabel{"ID"};
    String areaNameLabel{"Name"};
    String areaCountLabel{"Use count"};
    String cardLifeStatus{"Cards: %zu"};
    String cardSelectedStatus{"Cards: %zu, selected: %llu"};
    String cardListStatus{"Cards: %zu, selected: %zu"};
    String cardTableStatus{"Cards: %zu, selected: %zu"};
    // Dの位置指定のareaStatusメッセージに合
    // わせ、 表のラベルを1番目、行数を2番目に渡す。
    String areaStatus{"%2$s %1$s"};
    String idLabel{"ID"};
    String nameLabel{"Name"};
    String levelLabel{"Level"};
    String descriptionLabel{"Description"};
    String useCountLabel{"Use count"};
    String useNumberLabel{"Uses"};
    float importAreaIdColumn{50.0F};
    float importAreaNameColumn{190.0F};
    float importAreaCountColumn{60.0F};
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
    lib::ResourceId castImage{};
    lib::ResourceId skillImage{};
    lib::ResourceId itemImage{};
    lib::ResourceId beastImage{};
    lib::ResourceId infoImage{};
    lib::ResourceId handImage{};
    lib::ResourceId handEmptyImage{};
    lib::ResourceId eventTreeImage{};
    lib::ResourceId eventTreeEmptyImage{};
    lib::ResourceId commentImage{};
    lib::ResourceId openHandImage{};
    lib::ResourceId importSourceImage{};
    lib::ResourceId tableViewImage{};
    bool cardLife{};
    bool cardDetails{};
    bool showHandMark{true};
    bool showHandMarkAlways{true};
    bool showEventTreeMark{true};
    bool showEventTreeMarkAlways{true};
    bool ignoreEmptyStart{};
    bool clickIsOpenEvent{};
    bool readOnly{};
    std::size_t undoMax{64U};
    // MainWindowが正式なAreaTableの依存先を渡す
    // 。接続を所有しない参照にし、 軽量・ヘッドレスのCardWindow
    // テストは局所的な代替を使えるようにする。 製品ではディ
    // レクトリツリーと三列表を持つ、一つの完全な実装を使う。
    canonical_areatable::AreaTableContext* areaTableContext{};
    canonical_areatable::Props* editorProps{};
};

class CardPane;
class AreaTable;
using CardPanePtr = lib::Owner<CardPane>;
using AreaTablePtr = lib::Owner<AreaTable>;

/// DのウィンドウのCommonsデリゲート群に代わる共通コ
/// ールバック。 ホストは任意の通知を省略でき、その場合も局
/// 所モデルの操作はSummary/Cardと表を更新する。
struct Commons {
    lib::Services services{};
    std::function<String(const SummaryPtr&)> materialPath{};
    std::function<void(StringView)> refPaths{};
    lib::Action refreshToolBar{};
    std::function<bool()> stopOpen{};
    lib::Action refUseCount{};
    lib::Action addScenario{};
    std::function<void(const std::vector<String>&)> addScenarioFiles{};
    std::function<CardPanePtr(CardType, bool)> openCardPane{};
    std::function<void(const Props&, const SummaryPtr&, const CastCardPtr&,
                       const SummaryPtr&, bool)> openHands{};
    std::function<CardPanePtr(const Props&, const SummaryPtr&, const CastCardPtr&,
                             const SummaryPtr&, CardType, bool)> openHandsPane{};
    std::function<void(const Props&, const SummaryPtr&, const EffectCardPtr&,
                       bool, bool)> openUseEvents{};
    // 操作元の情報を保ち、実際の子ペインの移動結果を返す。
    std::function<bool(const SummaryPtr&, const CastCardPtr&, const SummaryPtr&,
                       StringView, bool)> openHandsPath{};
    std::function<bool(const SummaryPtr&, const EffectCardPtr&,
                       StringView, bool)> openUseEventsPath{};
    std::function<void(CardType, const SummaryPtr&, const CWXPathPtr&,
                       const CardPtr&, bool, lib::Action, lib::Action, lib::Action, bool)> editCard{};
    std::function<bool(const CardPtr&, StringView, bool)> openCardDialogPath{};
    std::function<void(const CardPtr&)> refCard{};
    std::function<void(const CWXPathPtr&, const CardPtr&)> delCard{};
    std::function<void(const CardPtr&, bool)> editEvent{};
    std::function<std::optional<String>(const SummaryPtr&, const CardPtr&,
                                         StringView)> editComment{};
    std::function<lib::Owner<comment::CommentDialog>(const SummaryPtr&, const CardPtr&,
                                                      StringView)> makeCommentDialog{};
    std::function<String(const CardPtr&)> commentTitle{};
    std::shared_ptr<canonical_cardpane::Signal<>> appearanceEvent{
        std::make_shared<canonical_cardpane::Signal<>>()};
    std::shared_ptr<canonical_cardpane::Signal<CardPtr, bool>> cardChangedEvent{
        std::make_shared<canonical_cardpane::Signal<CardPtr, bool>>()};
    std::function<void(bool, bool, int, int)> storeSort{};
    std::function<void(bool, bool, int, float)> storeColumnWidth{};
    std::shared_ptr<canonical_cardpane::Signal<const void*, canonical_cardpane::CardTableColumn, float>> columnWidthEvent{
        std::make_shared<canonical_cardpane::Signal<const void*, canonical_cardpane::CardTableColumn, float>>()};
    bool localColumnWidthSync{};
    std::shared_ptr<cardpane::Signal<bool, bool, cardpane::CardTableColumn, int>> refSortParamsEvent{
        std::make_shared<cardpane::Signal<bool, bool, cardpane::CardTableColumn, int>>()};
    std::function<std::vector<String>(const SummaryPtr&, const path::CWXPath&)>
        warnings{};
    lib::ResourceId warningImage{};
    std::function<String(::cwxeditor::cwx::types::MenuID)> menuText{};
    std::function<void(CardType, std::uint64_t, bool)> replaceID{};
    std::function<void(const std::vector<CardPtr>&)> importCards{};
    std::function<void(const SummaryPtr&, const SummaryPtr&,
                       const std::vector<String>&)> doImport{};
    std::function<lib::ImageData(const CardPtr&)> cardImageData{};
    std::function<lib::ImageData(const CardPtr&)> cardLifeImageData{};
    // スキンが選ばれている場合、キャストの名前描画は共通の画像提供元が行う。
    std::function<bool(const CardPtr&)> cardImageHasCaption{};
    std::function<std::vector<SummaryPtr>(const std::vector<String>&)> loadSummaries{};
    std::function<void(const WidgetPtr&, bool)> forceFocus{};
    std::function<bool(StringView, bool)> openCWXPath{};
    std::function<bool(StringView, bool, bool)> openFilePath{};
    // 取り込み元のArea行は、そのSummaryの同一性を保
    // つ。 パスだけの通知でメインシナリオを基準に解決すると、
    // 同じArea IDがある場合に別のタブを開いてしまう。
    std::function<void(const SummaryPtr&, const area::AbstractAreaPtr&, bool,
                       bool, bool)> openAreaFrom{};
    std::function<void(const WidgetPtr&, String)> setStatusLine{};
    std::function<void(const WidgetPtr&, String)> setTitle{};
    std::function<void(const WidgetPtr&)> close{};
    std::function<void()> changeVHSide{};
    std::function<void()> editSummary{};
    std::function<void()> selectSummary;
    Signal<> refScenarioName;
    Signal<> refScenarioPath;
    Signal<CastCardPtr> refCast;
    Signal<CastCardPtr> delCast;
    Signal<SummaryPtr> closeAdds;
};

class TCPD {
public:
    virtual ~TCPD() = default;
    virtual void cut(SelectionEvent&) {}
    virtual void copy(SelectionEvent&) {}
    virtual void paste(SelectionEvent&) {}
    virtual void del(SelectionEvent&) {}
    virtual void clone(SelectionEvent&) {}
    [[nodiscard]] virtual bool canDoTCPD() const noexcept { return false; }
    [[nodiscard]] virtual bool canDoT() const noexcept { return false; }
    [[nodiscard]] virtual bool canDoC() const noexcept { return false; }
    [[nodiscard]] virtual bool canDoP() const noexcept { return false; }
    [[nodiscard]] virtual bool canDoD() const noexcept { return false; }
    [[nodiscard]] virtual bool canDoClone() const noexcept { return false; }
    [[nodiscard]] virtual bool canUndo() const noexcept { return false; }
    [[nodiscard]] virtual bool canRedo() const noexcept { return false; }
    virtual void undo() {}
    virtual void redo() {}
    virtual void replaceID() {}
    [[nodiscard]] virtual bool canReplaceID() const noexcept { return false; }
    [[nodiscard]] virtual bool canEdit() const noexcept { return false; }
    virtual void edit() {}
    [[nodiscard]] virtual bool canReNumbering() const noexcept { return false; }
    virtual void reNumbering() {}
    [[nodiscard]] virtual bool canUp() const noexcept { return false; }
    [[nodiscard]] virtual bool canDown() const noexcept { return false; }
    virtual void up() {}
    virtual void down() {}
    [[nodiscard]] virtual const String& statusLine() const noexcept {
        static const String empty;
        return empty;
    }
    [[nodiscard]] virtual std::vector<String> openedCWXPath() const { return {}; }
    [[nodiscard]] virtual bool openCWXPath(StringView, bool) { return false; }
};

class CardPane final : public TCPD {
public:
    CardPane(Commons comm = {}, Props prop = {}, SummaryPtr summary = {},
             Parent parent = {}, OwnerType ownerType = OwnerType::Summary,
             CardType cardType = CardType::Cast, bool readOnly = false,
             SummaryPtr toc = {}, std::function<void(const CastCardPtr&)> openHand = {});
    ~CardPane() override { callback_lifetime_.reset(); }

    CardPane(const CardPane&) = delete;
    CardPane& operator=(const CardPane&) = delete;

    [[nodiscard]] PanelPtr construct(const Parent& parent = {});
    void reconstruct(const Parent& parent = {});
    [[nodiscard]] const PanelPtr& widget() const noexcept { return component_; }
    [[nodiscard]] const TableModelPtr& table() const noexcept { return table_; }
    [[nodiscard]] const canonical_cardpane::CardPanePtr& canonical() const noexcept {
        return canonical_;
    }
    [[nodiscard]] std::vector<customtable::TableColumnPtr> columns() const;
    [[nodiscard]] CardType cardType() const noexcept { return card_type_; }
    [[nodiscard]] OwnerType ownerType() const noexcept { return owner_type_; }
    [[nodiscard]] const std::vector<CardPtr>& cards() const noexcept { return cards_; }
    [[nodiscard]] CardPtr card(std::size_t index) const noexcept;
    [[nodiscard]] CardPtr cardById(std::uint64_t id) const noexcept;
    [[nodiscard]] const String& statusLine() const noexcept { return status_line_; }

    void summary(SummaryPtr value) {
        summary_ = std::move(value);
        if (canonical_) canonical_->refreshAll(summary_, owner_);
        refresh();
    }
    [[nodiscard]] SummaryPtr summary() const noexcept { return summary_; }
    void owner(CWXPathPtr value) {
        owner_ = std::move(value);
        cast_owner_ = std::dynamic_pointer_cast<CastCard>(owner_);
        if (canonical_) canonical_->refreshAll(summary_, owner_);
        refresh();
    }
    [[nodiscard]] CWXPathPtr owner() const noexcept { return owner_; }
    void toc(SummaryPtr value) noexcept { toc_ = std::move(value); }
    [[nodiscard]] SummaryPtr toc() const noexcept { return toc_; }

    void refresh();
    void refreshAll(const SummaryPtr& summary, const CWXPathPtr& owner);
    void constructView();
    void showCardLife();
    void showCardList();
    void showCardTable();
    [[nodiscard]] CViewMode viewMode() const noexcept { return view_mode_; }

    void create();
    [[nodiscard]] bool canCreate() const noexcept {
        return canonical_ ? (!read_only_ && owner_ != nullptr)
                          : (!read_only_ && summary_ != nullptr);
    }
    [[nodiscard]] bool select(std::ptrdiff_t index);
    [[nodiscard]] bool debugTouch(std::size_t index) {
        return canonical_ ? canonical_->debugTouch(index) : select(static_cast<std::ptrdiff_t>(index));
    }
    [[nodiscard]] bool selectById(std::uint64_t id);
    [[nodiscard]] const std::vector<std::size_t>& selectedIndices() const noexcept {
        return selected_;
    }
    [[nodiscard]] bool isSelected() const noexcept {
        return canonical_ ? canonical_->isSelected() : !selected_.empty();
    }

    void editHand();
    [[nodiscard]] bool canEditHand() const noexcept;
    void editUseEvent(bool canDuplicate = false);
    [[nodiscard]] bool canEditUseEvent() const noexcept;
    void doImport();
    [[nodiscard]] bool canDoImport() const noexcept {
        return read_only_ && !selected_.empty();
    }
    void selectConnectedResource();
    [[nodiscard]] bool canSelectConnectedResource() const noexcept;
    void selectAll();
    [[nodiscard]] bool canSelectAll() const noexcept {
        return canonical_ ? canonical_->canSelectAll() : !cards_.empty();
    }
    void removeRef();
    [[nodiscard]] bool canRemoveRef() const noexcept;
    void writeComment();
    [[nodiscard]] bool canWriteComment() const noexcept;
    void incSearch();
    [[nodiscard]] bool canIncSearch() const noexcept {
        return canonical_ && canonical_->canIncSearch();
    }
    void editEvent(bool canDuplicate = false);
    [[nodiscard]] bool canEditEvent() const noexcept;
    [[nodiscard]] bool openCWXPath(StringView value, bool shellActivate) override;

    void cut(SelectionEvent& event) override;
    void copy(SelectionEvent& event) override;
    void paste(SelectionEvent& event) override;
    void del(SelectionEvent& event) override;
    void clone(SelectionEvent& event) override;
    [[nodiscard]] bool canDoTCPD() const noexcept override;
    [[nodiscard]] bool canDoT() const noexcept override { return canDoTCPD(); }
    [[nodiscard]] bool canDoC() const noexcept override {
        return canonical_ ? canonical_->canDoC()
                          : summary_ != nullptr && !selected_.empty();
    }
    [[nodiscard]] bool canDoP() const noexcept override {
        return canonical_ ? canonical_->canDoP() : canDoTCPD();
    }
    [[nodiscard]] bool canDoD() const noexcept override {
        return canonical_ ? canonical_->canDoD() : canDoTCPD();
    }
    [[nodiscard]] bool canDoClone() const noexcept override {
        return canonical_ ? canonical_->canDoClone() : canDoTCPD();
    }
    [[nodiscard]] bool canUndo() const noexcept override;
    [[nodiscard]] bool canRedo() const noexcept override;
    void replaceID() override;
    [[nodiscard]] bool canReplaceID() const noexcept override;
    [[nodiscard]] std::vector<String> openedCWXPath() const override;
    [[nodiscard]] bool canEdit() const noexcept override {
        return canonical_ ? canonical_->canEdit()
                          : !read_only_ && selected_.size() == 1U;
    }
    void edit() override;
    [[nodiscard]] bool canReNumbering() const noexcept override {
        return canonical_ ? canonical_->canReNumbering()
                          : !read_only_ && summary_ != nullptr && !cards_.empty();
    }
    void reNumbering(std::uint64_t first, std::uint64_t step);
    void reNumbering() override;
    [[nodiscard]] bool canUp() const noexcept override;
    [[nodiscard]] bool canDown() const noexcept override;
    void up() override;
    void down() override;
    void undo() override;
    void redo() override;
    void setUndoLimit(std::size_t value);

private:
    Commons comm_;
    Props prop_;
    Parent parent_{};
    PanelPtr component_{};
    TableModelPtr table_{};
    std::shared_ptr<cardlist::CardList<CardPtr>> image_list_{};
    customtable::TableColumnPtr id_column_{};
    customtable::TableColumnPtr name_column_{};
    customtable::TableColumnPtr level_column_{};
    customtable::TableColumnPtr desc_column_{};
    customtable::TableColumnPtr use_count_column_{};
    std::unique_ptr<customtable::FullTableColumn> full_column_{};
    std::unique_ptr<customtable::TableSorter<CardPtr>> id_sorter_{};
    std::unique_ptr<customtable::TableSorter<CardPtr>> name_sorter_{};
    SummaryPtr summary_{};
    CWXPathPtr owner_{};
    CastCardPtr cast_owner_{};
    SummaryPtr toc_{};
    OwnerType owner_type_{OwnerType::Summary};
    CardType card_type_{CardType::Cast};
    bool read_only_{};
    std::function<void(const CastCardPtr&)> open_hand_{};
    std::vector<CardPtr> cards_{};
    std::vector<std::size_t> selected_{};
    CViewMode view_mode_{CViewMode::INIT};
    String status_line_{};
    std::uint64_t selection_token_{};
    canonical_cardpane::CardPanePtr canonical_{};
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};

    void refreshCards();
    void refreshSnapshot();
    void refreshTable();
    void refreshStatusLine();
    void installInputHandlers();
    void bindDragDrop();
    void rebuildOwnerOrder();
    void handleSelection(const std::vector<std::size_t>& indices);
    void handleEdit(std::size_t row, std::size_t column, String value);
    void notifyAction(const std::function<void()>& action, SelectionEvent& event);
    [[nodiscard]] std::uint64_t nextId() const;
    [[nodiscard]] String cardPath(const CardPtr& value) const;
    [[nodiscard]] std::vector<CardPtr> selectedCards() const;
    [[nodiscard]] CardPtr duplicateCard(const CardPtr& source) const;
    [[nodiscard]] bool addCard(const CardPtr& value, std::ptrdiff_t index = -1);
    void removeCard(const CardPtr& value);
    void swapCard(std::size_t first, std::size_t second);
    [[nodiscard]] String formatStatus() const;
    [[nodiscard]] static String cardTypeName(CardType type, const Props& prop);
};

class AreaTable final : public TCPD {
public:
    AreaTable(Commons comm = {}, Props prop = {}, bool readOnly = false,
              SummaryPtr toc = {});
    ~AreaTable() override = default;

    [[nodiscard]] PanelPtr construct(const Parent& parent = {});
    void reconstruct(const Parent& parent = {});
    [[nodiscard]] const PanelPtr& panel() const noexcept { return component_; }
    // 軽量な旧テスト用APIのために残す。正式なAreaTableはcustomtable::TableModel
    // でなく 共通のTableViewを所有するため、
    // 製品の呼び出し元はtableView()を使う。
    [[nodiscard]] const TableModelPtr& table() const noexcept { return table_; }
    [[nodiscard]] const std::shared_ptr<lib::TableView>& tableView() const noexcept {
        return table_view_;
    }
    void summary(SummaryPtr value);
    [[nodiscard]] SummaryPtr summary() const noexcept { return summary_; }
    void refresh();
    [[nodiscard]] const String& statusLine() const noexcept override { return status_line_; }
    [[nodiscard]] bool isSelected() const noexcept {
        return canonical_ ? canonical_->isSelected() : selected_.has_value();
    }
    [[nodiscard]] bool canOpenAreaScene() const noexcept;
    [[nodiscard]] bool canOpenAreaEvent() const noexcept;
    void openAreaScene(bool shellActivate, bool canDuplicate = false);
    void openAreaEvent(bool shellActivate, bool canDuplicate = false);
    void edit() override;
    [[nodiscard]] bool canEdit() const noexcept override {
        return canonical_ ? canonical_->canEdit() : canOpenAreaScene();
    }
    void editSummary(const Parent& parent = {});
    void doImport();
    [[nodiscard]] bool canDoImport() const noexcept;
    void changeVHSide();
    [[nodiscard]] bool canChangeVH() const noexcept {
        return canonical_ ? canonical_->canChangeVH() : summary_ != nullptr;
    }
    [[nodiscard]] bool openCWXPath(StringView value, bool shellActivate) override;

    void cut(SelectionEvent& event) override;
    void copy(SelectionEvent& event) override;
    void paste(SelectionEvent& event) override;
    void del(SelectionEvent& event) override;
    void clone(SelectionEvent& event) override;
    [[nodiscard]] bool canDoTCPD() const noexcept override;
    [[nodiscard]] bool canDoT() const noexcept override {
        // 取り込み表は読み取り専用でもコピーできるため、 広いTCPD
        // の範囲ではなく、共通AreaTableの切り取り可否を使う。
        return canonical_ ? canonical_->canDoT() : canDoTCPD();
    }
    [[nodiscard]] bool canDoC() const noexcept override {
        return canonical_ ? canonical_->canDoC()
                          : summary_ != nullptr && selected_.has_value();
    }
    [[nodiscard]] bool canDoP() const noexcept override {
        return canonical_ ? canonical_->canDoP() : canDoTCPD();
    }
    [[nodiscard]] bool canDoD() const noexcept override {
        return canonical_ ? canonical_->canDoD() : canDoTCPD();
    }
    [[nodiscard]] bool canDoClone() const noexcept override {
        return canonical_ ? canonical_->canDoClone() : canDoTCPD();
    }
    void selectAll();
    [[nodiscard]] bool canSelectAll() const noexcept;
    [[nodiscard]] bool canUp() const noexcept override;
    [[nodiscard]] bool canDown() const noexcept override;
    void up() override;
    void down() override;

private:
    enum class AreaKind { Area, Battle, Package };
    struct RowRef {
        AreaKind kind{AreaKind::Area};
        std::size_t index{};
        friend bool operator==(const RowRef&, const RowRef&) = default;
    };
    Commons comm_;
    Props prop_;
    bool read_only_{};
    Parent parent_{};
    PanelPtr component_{};
    std::shared_ptr<canonical_areatable::AreaTable> canonical_{};
    std::shared_ptr<lib::TableView> table_view_{};
    TableModelPtr table_{};
    SummaryPtr summary_{};
    SummaryPtr toc_{};
    std::vector<RowRef> rows_{};
    std::optional<std::size_t> selected_{};
    String status_line_{};
    std::uint64_t selection_token_{};

    void refreshTable();
    void refreshStatusLine();
    [[nodiscard]] String rowPath(RowRef row) const;
    [[nodiscard]] String rowName(RowRef row) const;
    [[nodiscard]] String rowId(RowRef row) const;
    [[nodiscard]] String rowUseCount(RowRef row) const;
    [[nodiscard]] bool openSelected(bool scene, bool shellActivate,
                                    bool canDuplicate);
    [[nodiscard]] area::AbstractAreaPtr rowArea(const RowRef& value) const;
    void notifyAction(const std::function<void()>& action, SelectionEvent& event);
};

class CardWindow final : public TCPD,
                         public std::enable_shared_from_this<CardWindow> {
public:
    CardWindow(Commons comm, Props prop, CardWindowKind kind,
               Parent parent = {});
    CardWindow(Commons comm, Props prop, CardWindowKind kind,
               SummaryPtr summary, Parent parent = {});
    CardWindow(Commons comm, Props prop, CardWindowKind kind,
               Parent parent, SummaryPtr summary, CWXPathPtr owner,
               SummaryPtr toc = {});
    ~CardWindow() override;

    CardWindow(const CardWindow&) = delete;
    CardWindow& operator=(const CardWindow&) = delete;

    void reconstruct(const Parent& parent = {});
    void construct(Commons comm, Props prop, SummaryPtr summary,
                   const Parent& parent = {});
    void initPane();
    void newPane();
    [[nodiscard]] const WindowPtr& shell() const noexcept { return win_; }
    [[nodiscard]] const PanelPtr& panel() const noexcept { return comp_; }
    [[nodiscard]] CWXPathPtr owner() const noexcept { return owner_; }
    [[nodiscard]] SummaryPtr summary() const noexcept { return summary_; }
    [[nodiscard]] CardWindowKind kind() const noexcept { return kind_; }
    [[nodiscard]] WidgetPtr focusControl() const noexcept;

    [[nodiscard]] bool editMode() const noexcept { return !toc_; }
    [[nodiscard]] bool withArea() const noexcept {
        return kind_ == CardWindowKind::ImportSource;
    }
    [[nodiscard]] bool multiTab() const noexcept;
    [[nodiscard]] std::vector<CardType> cardTypes() const;
    [[nodiscard]] OwnerType ownerType() const noexcept { return owner_type_; }
    [[nodiscard]] bool isSelectedAreaTable() const noexcept;
    [[nodiscard]] TCPD* tcpd() const noexcept;

    void reNumberingAll();
    void showCardLife();
    void showCardList();
    void showCardTable();
    [[nodiscard]] bool isViewLife() const noexcept { return view_mode_ == CViewMode::LIFE; }
    [[nodiscard]] bool isViewList() const noexcept { return view_mode_ == CViewMode::CARD; }
    [[nodiscard]] bool isViewTable() const noexcept { return view_mode_ == CViewMode::TABLE; }
    void setStatusLine(String status);
    [[nodiscard]] CardPanePtr openPane(CardType cardType, bool shellActivate);
    [[nodiscard]] CardPanePtr paneCast() const noexcept;
    [[nodiscard]] CardPanePtr paneSkill() const noexcept;
    [[nodiscard]] CardPanePtr paneItem() const noexcept;
    [[nodiscard]] CardPanePtr paneBeast() const noexcept;
    [[nodiscard]] CardPanePtr paneInfo() const noexcept;
    void create(CardType cardType);
    void createCast() { create(CardType::Cast); }
    void createSkill() { create(CardType::Skill); }
    void createItem() { create(CardType::Item); }
    void createBeast() { create(CardType::Beast); }
    void createInfo() { create(CardType::Info); }
    [[nodiscard]] bool canCreateCast() const noexcept;
    [[nodiscard]] bool canCreateSkill() const noexcept;
    [[nodiscard]] bool canCreateItem() const noexcept;
    [[nodiscard]] bool canCreateBeast() const noexcept;
    [[nodiscard]] bool canCreateInfo() const noexcept;

    [[nodiscard]] lib::ResourceId image() const noexcept;
    [[nodiscard]] String title() const;
    /// 背後のlib::Windowを直接閉じずに埋め込み・ドックのCardWindow
    /// を閉じる場合も、 Dのdispose相当の終了を通知する。
    void onHostClose();
    [[nodiscard]] std::function<void(String)> statusText() const { return {}; }
    void refreshTitle();
    void refresh(SummaryPtr summary);
    void refresh(SummaryPtr summary, CastCardPtr owner);
    void refreshAll(SummaryPtr summary, CWXPathPtr owner);
    // C++ホストでは、共通の参照数の通知をMainWindowが所有する。
    void refreshUseCount();
    void refreshM();

    void cut(SelectionEvent& event) override;
    void copy(SelectionEvent& event) override;
    void paste(SelectionEvent& event) override;
    void del(SelectionEvent& event) override;
    void clone(SelectionEvent& event) override;
    [[nodiscard]] bool canDoTCPD() const noexcept override { return editMode(); }
    [[nodiscard]] bool canDoT() const noexcept override;
    [[nodiscard]] bool canDoC() const noexcept override;
    [[nodiscard]] bool canDoP() const noexcept override;
    [[nodiscard]] bool canDoD() const noexcept override;
    [[nodiscard]] bool canDoClone() const noexcept override;
    void edit() override;
    [[nodiscard]] bool canEdit() const noexcept override;
    void editHand();
    [[nodiscard]] bool canEditHand() const noexcept;
    void reNumbering() override;
    [[nodiscard]] bool canReNumbering() const noexcept override;
    [[nodiscard]] bool canUndo() const noexcept override;
    [[nodiscard]] bool canRedo() const noexcept override;
    void undo() override;
    void redo() override;
    void setUndoLimit(std::size_t value);
    [[nodiscard]] bool canUp() const noexcept override;
    [[nodiscard]] bool canDown() const noexcept override;
    void up() override;
    void down() override;
    void replaceID() override;
    [[nodiscard]] bool canReplaceID() const noexcept override;
    void selectConnectedResource();
    [[nodiscard]] bool canSelectConnectedResource() const noexcept;
    void doImport();
    [[nodiscard]] bool canDoImport() const noexcept;
    void selectAll();
    [[nodiscard]] bool canSelectAll() const noexcept;
    void removeRef();
    [[nodiscard]] bool canRemoveRef() const noexcept;
    void writeComment();
    [[nodiscard]] bool canWriteComment() const noexcept;
    void incSearch();
    [[nodiscard]] bool canIncSearch() const noexcept;
    void editEvent(bool canDuplicate = false);
    [[nodiscard]] bool canEditEvent() const noexcept;
    void editUseEvent(bool canDuplicate = false);
    [[nodiscard]] bool canEditUseEvent() const noexcept;
    void changeVHSide();
    [[nodiscard]] bool canChangeVH() const noexcept;
    [[nodiscard]] bool isSelected() const noexcept;
    [[nodiscard]] bool openCWXPath(StringView value, bool shellActivate) override;
    [[nodiscard]] std::vector<String> openedCWXPath() const override;

private:
    struct DropScenario {
        CardWindow* window{};
        void dragEnter(lib::DragEvent& event) const;
        void drop(lib::DragEvent& event) const;
    };

    Commons comm_;
    Props prop_;
    CardWindowKind kind_{CardWindowKind::Cast};
    OwnerType owner_type_{OwnerType::Summary};
    CViewMode view_mode_{CViewMode::INIT};
    WindowPtr win_{};
    PanelPtr comp_{};
    TabViewPtr tabf_{};
    std::shared_ptr<lib::ComboBox> page_selector_{};
    std::vector<CardPanePtr> panes_{};
    std::shared_ptr<canonical_cardpane::Signal<const void*,
        canonical_cardpane::CardTableColumn, float>> local_column_width_event_{};
    std::map<CardType, CardPanePtr> pane_table_{};
    AreaTablePtr areas_{};
    SummaryPtr summary_{};
    CWXPathPtr owner_{};
    SummaryPtr toc_{};
    Parent parent_{};
    std::size_t tab_selection_{};
    String status_line_{};
    // パス移動中の再入可能なホスト通知で、CardWindowが終了する場合がある。
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};
    DropScenario drop_scenario_{};
    Signal<SummaryPtr>::Token close_adds_connection_{};
    Signal<CastCardPtr>::Token ref_cast_connection_{};
    Signal<CastCardPtr>::Token del_cast_connection_{};
    Signal<>::Token ref_scenario_name_connection_{};
    Signal<>::Token ref_scenario_path_connection_{};
    bool close_requested_{};
    bool source_close_published_{};

    void construct1(const Parent& parent);
    void construct2();
    void selectTabPage(std::size_t index);
    void installLifecycleHandlers();
    void disconnectLifecycleHandlers() noexcept;
    void closeForOwnerLifecycle();
    void publishSourceClose();
    void closeAdds(SummaryPtr importable);
    void refOwner(CastCardPtr owner);
    void delOwner(CastCardPtr owner);
    void refreshStatusLine();
    [[nodiscard]] std::size_t selectionCardIndex() const noexcept;
    [[nodiscard]] std::size_t areaIndex() const noexcept {
        return withArea() ? 0U : static_cast<std::size_t>(-1);
    }
    [[nodiscard]] CardPanePtr selectedPane(bool includeArea = true) const noexcept;
    [[nodiscard]] bool openCWXPathEff(CardType type, String value,
                                      bool shellActivate);
    [[nodiscard]] CardPanePtr pane(CardType type) const noexcept;
    void openHand(const CastCardPtr& card);
};

class DelTemp {
public:
    explicit DelTemp(SummaryPtr summary) : summary_(std::move(summary)) {}
    void widgetDisposed() {
        if (summary_) summary_->delTemp();
    }

private:
    SummaryPtr summary_;
};

class AddCard {
public:
    using WindowList = std::vector<lib::Owner<CardWindow>>;
    using AddScenario = std::function<void(WindowList)>;
    using Status = std::function<void(String)>;

    static void openScenario(Commons comm, Props prop, Parent parent,
                             Status status, SummaryPtr summary, SummaryPtr toc,
                             AddScenario addScenario);
    static void openScenario(Commons comm, Props prop, Parent parent,
                             Status status, SummaryPtr summary, SummaryPtr toc,
                             const std::vector<String>& files,
                             AddScenario addScenario,
                             lib::Action failure = {});
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::cardwindow
