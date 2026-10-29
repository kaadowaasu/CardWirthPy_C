#pragma once

// cwx/editor/gui/dwt/eventview.d
// の移植可能なC++20実装。 EventView
// はモデルを中心に管理し、元のSWT部
// 品に代わって通常のlib部品を公開す
// る。 このヘッダーはSWT・Win32
// ・生成済み互換型を必要としない。

#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaviewutils.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/flagtable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/flagspane.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/events.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::eventview {

namespace event = ::cwxeditor::cwx::event;
namespace flagmodel = ::cwxeditor::cwx::flag;
namespace lib = ::cwxeditor::lib;
namespace pane = ::cwxeditor::cwx::editor::gui::dwt::flagspane;
namespace variables = ::cwxeditor::cwx::editor::gui::dwt::flagtable;
namespace types = ::cwxeditor::cwx::types;
namespace xml = ::cwxeditor::cwx::xml;

using String = lib::String;
using StringView = lib::StringView;
using Parent = pane::Parent;
using PanelPtr = pane::PanelPtr;
using EventTreePtr = event::EventTreePtr;
using ContentPtr = event::ContentPtr;
using EventTreeOwner = event::EventTreeOwner;
using EventTreeOwnerPtr = lib::Owner<EventTreeOwner>;
using UndoManager = pane::UndoManager;
using Commons = pane::Commons;
using Props = pane::Props;
using TreeItemId = lib::TreeItemId;

/// Dがラッパーで保持していた二つの値を、
/// 共通モデルでは実際の値として直接持つ。
using KeyCodeObj = String;
using RoundObj = std::uint32_t;

enum class FireKind {
    Enter,
    Escape,
    Lose,
    EveryRound,
    RoundEnd,
    Round0,
    KeyCode,
    Round,
};

struct FireCondition {
    FireKind kind{FireKind::Enter};
    event::FKeyCode keyCode{};
    std::uint32_t round{};

    [[nodiscard]] static FireCondition enter() noexcept { return {FireKind::Enter, {}, 0}; }
    [[nodiscard]] static FireCondition escape() noexcept { return {FireKind::Escape, {}, 0}; }
    [[nodiscard]] static FireCondition lose() noexcept { return {FireKind::Lose, {}, 0}; }
    [[nodiscard]] static FireCondition everyRound() noexcept { return {FireKind::EveryRound, {}, 0}; }
    [[nodiscard]] static FireCondition roundEnd() noexcept { return {FireKind::RoundEnd, {}, 0}; }
    [[nodiscard]] static FireCondition round0() noexcept { return {FireKind::Round0, {}, 0}; }
    [[nodiscard]] static FireCondition withKeyCode(event::FKeyCode value) {
        FireCondition result;
        result.kind = FireKind::KeyCode;
        result.keyCode = std::move(value);
        return result;
    }
    [[nodiscard]] static FireCondition withRound(std::uint32_t value) noexcept {
        FireCondition result;
        result.kind = FireKind::Round;
        result.round = value;
        return result;
    }
};

struct ContentToolState {
    bool arrow{true};
    types::CType type{types::CType::TalkMessage};
    // FlexEtcPropsの指定。0は即時、1は選択、2は継続。
    std::int32_t put_mode{1};
    bool auto_open{true};
    bool insert_first{};
};

struct EventMessages {
    String events{"イベント"};
    String event{"イベント"};
    String playerCard{"プレイヤーカード"};
    String enter{"到着"};
    String escape{"逃走"};
    String lose{"敗北"};
    String everyRound{"毎ラウンド"};
    String roundEnd{"ラウンド終了"};
    String round0{"バトル開始"};
    String keyCode{};
    String round{"ラウンド "};
    String enterTree{"到着"};
    String selectTree{"クリック"};
    String deadTree{"死亡"};
    String victoryTree{"勝利"};
    String packageTree{"パッケージイベント"};
    String useTree{"使用時イベント"};
    String escapeTree{"逃走"};
    String loseTree{"敗北"};
    String everyRoundTree{"毎ラウンド"};
    String roundEndTree{"ラウンド終了"};
    String round0Tree{"バトル開始"};
};

struct NodeInfo {
    enum class Kind { Owner, EventTree, Fire, Content };

    Kind kind{Kind::Owner};
    EventTreeOwner* owner{};
    EventTreePtr tree{};
    ContentPtr content{};
    ContentPtr parentContent{};
    FireCondition fire{};
    std::size_t ownerIndex{};
    std::size_t treeIndex{};
    std::size_t contentIndex{};
};

/// イベント編集のモデルと共通ビュー。
class EventView final {
public:
    struct SelectionSnapshot {
        String treeId;
        String fireTreeId;
        std::size_t ownerIndex{};
        std::size_t treeIndex{std::numeric_limits<std::size_t>::max()};
        std::vector<std::size_t> contentPath;
        bool contentActive{};
        std::optional<FireCondition> fire;
    };
    void historySelectionSource(
        std::function<std::optional<SelectionSnapshot>()> source) {
        historySelectionSource_ = std::move(source);
    }
    // 履歴の再生では、作成元のウィンドウが終了していても現在の他のビューを読む。
    void historyReplaySelectionSource(
        std::function<std::optional<SelectionSnapshot>()> source) {
        historyReplaySelectionSource_ = std::move(source);
    }
    [[nodiscard]] SelectionSnapshot selectionSnapshot(bool useHistorySource = true) const;
    // Area/Battleの共有履歴は、作成元のEventViewラッ
    // パーより長く残る場合がある。 有効な別ビューへの復元を通常の更新・
    // モデル通知から分け、終了した作成元を呼ばずに再生できるようにする。
    void restoreHistorySelectionForReplay(const SelectionSnapshot& selection);
    void historySelectionRestorer(
        std::function<void(const SelectionSnapshot&)> restore) {
        historySelectionRestorer_ = std::move(restore);
    }
    pane::Signal<void()> selectionChanged;
    pane::Signal<void()> statusChanged;
    pane::Signal<void()> modelChanged;
    pane::Signal<void(const EventTreePtr&)> eventTreeChanged;
    pane::Signal<void(const ContentPtr&)> contentChanged;
    pane::Signal<void(const ContentToolState&)> contentToolStateChanged;
    pane::Signal<void(bool)> detailSettingChanged;
    pane::Signal<void(bool)> lineNumberSettingChanged;
    pane::Signal<void(int)> slopeSettingChanged;
    pane::Signal<void(std::vector<std::int32_t>)> contentsOrderChanged;
    void applyDisplayPreferences(bool detail, bool lineNumber, int slope,
                                 int slopeMaximum = -1);
    void applyEventTreeSlope(int value);
    // DのSummaryのEventEditorスタイル通知は再描画を要求する
    // 。 描画時に現在のEditor設定を調べ、変わっていれば行を再構成する。
    void eventEditorStyleChanged();

    EventView(Commons comm, Props prop, EventTreeOwner& area,
              UndoManager& undo, bool readOnly = false);
    EventView(Commons comm, Props prop, EventTreeOwner* area,
              UndoManager& undo, bool readOnly = false);
    EventView(Commons comm, Props prop, EventTreeOwnerPtr area,
              UndoManager& undo, bool readOnly = false);
    ~EventView();

    EventView(const EventView&) = delete;
    EventView& operator=(const EventView&) = delete;

    [[nodiscard]] PanelPtr createControl(const Parent& parent = {});
    [[nodiscard]] PanelPtr control() const noexcept { return panel_; }
    // このパネルは実際の独自描画のEventTreeViewを配置する。 EventView
    // の外枠や、イベント・発火条件の移動用表示とは別にする。
    [[nodiscard]] PanelPtr eventTreeControl() const noexcept { return rightPanel_; }
    [[nodiscard]] PanelPtr contentsPaletteControl() const noexcept { return paletteHost_; }
    [[nodiscard]] std::shared_ptr<lib::Window> contentsPaletteWindow() const noexcept;
    void contentsPaletteOwner(const std::shared_ptr<lib::Window>& owner);
    [[nodiscard]] PanelPtr ensureContentsPalette() {
        const std::weak_ptr<const int> life = paletteLifetime_.token;
        if (!paletteHost_ && rightPanel_) createContentsPalette(rightPanel_);
        if (!life.expired()) showFloatingPalette();
        return life.expired() ? PanelPtr{} : paletteHost_;
    }
    [[nodiscard]] bool contentsPaletteSingleton() const noexcept {
        return prop_.etc.contentsAutoHide || prop_.etc.contentsFloat;
    }
    [[nodiscard]] std::shared_ptr<void> releaseContentsPalette();
    [[nodiscard]] PanelPtr adoptContentsPalette(std::shared_ptr<void> payload);
    void rebuildContentsPalette();
    // ALLではホーム操作の下へ小型のイベント移動欄を
    // 描く。 返すパネルは、通常のEventView
    // 上部と同じ有効なコマンドツールバーを所有する。
    [[nodiscard]] std::shared_ptr<lib::Panel> allCompactToolbar();
    [[nodiscard]] std::shared_ptr<lib::TreeView> tree() const noexcept { return cards_; }
    [[nodiscard]] std::shared_ptr<lib::TreeView> eventTreeView() const noexcept {
        return contents_;
    }
    /// DのEventTreeViewは列状のツリー
    /// を独自描画する。 共通側も、上の保持中の階層
    /// モデルに加え、見えている一覧部品を公開する。
    [[nodiscard]] std::shared_ptr<lib::TableView> eventTable() const noexcept {
        return contentTable_;
    }

    [[nodiscard]] Commons& commons() noexcept { return comm_; }
    [[nodiscard]] const Commons& commons() const noexcept { return comm_; }
    [[nodiscard]] Props& props() noexcept { return prop_; }
    [[nodiscard]] const Props& props() const noexcept { return prop_; }
    [[nodiscard]] EventTreeOwner* owner() const noexcept { return area_; }
    [[nodiscard]] UndoManager* undoManager() const noexcept { return undo_; }
    [[nodiscard]] bool readOnly() const noexcept { return readOnly_; }
    [[nodiscard]] const variables::FlagTable* localVariablesTable() const noexcept {
        return localVariables_.get();
    }
    void setReadOnly(bool value) noexcept;

    void id(String value) { id_ = std::move(value); }
    [[nodiscard]] const String& id() const noexcept { return id_; }
    void ownerTitle(String value);
    [[nodiscard]] const String& ownerTitle() const noexcept { return ownerTitle_; }
    void setEditAction(lib::Action value) { editAction_ = std::move(value); }
    /// 発火条件の変更を記録する前に、同じ所有者のペインで保留中の編集を確定する。
    void setBeforeFireEdit(std::function<bool()> value) { beforeFireEdit_ = std::move(value); }
    /// このモデルを共有する全EventWindowの保留編集を取り消す。
    void setCancelSameOwnerEditors(std::function<bool()> value) {
        cancelSameOwnerEditors_ = std::move(value);
    }
    /// 詳細ビューの履歴操作は、所有するEventWindowを通す。
    void setHistoryReplayCoordinator(std::function<void(lib::Action)> value) {
        historyReplayCoordinator_ = std::move(value);
    }
    void setAutoOpenContentAction(
        std::function<bool(const ContentPtr&, const ContentPtr&)> value) {
        autoOpenContentAction_ = std::move(value);
    }
    void setCreateEventAction(lib::Action value) {
        createEventAction_ = std::move(value);
    }
    void setWriteCommentAction(lib::Action value) {
        writeCommentAction_ = std::move(value);
    }
    /// ホストはD相当の未解決変数・エラー画面を処理したらtrueを返す。 最後
    /// のフラグが真なら左一覧への通常の貼り付けを新規イベントにし、偽ならグラフ
    /// ・テンプレート挿入を保つ。 戻り値がfalseなら直接コンパイルする。
    void setScriptTemplateResolver(std::function<bool(StringView, bool, bool)> value) {
        scriptTemplateResolver_ = std::move(value);
    }
    // 既存の二引数のテンプレート解決用呼び出し元を維持する。
    void setScriptTemplateResolver(std::function<bool(StringView, bool)> value) {
        scriptTemplateResolver_ = value
            ? std::function<bool(StringView, bool, bool)>{
                  [value = std::move(value)](StringView script, bool insert, bool) {
                      return value(script, insert);
                  }}
            : std::function<bool(StringView, bool, bool)>{};
    }
    bool pasteCompiledTemplateContents(std::vector<ContentPtr> contents,
                                      bool tryInsert, bool createEvent = false);
    [[nodiscard]] ContentToolState contentToolState() const noexcept;
    void applyContentToolState(const ContentToolState& value);
    void localVariables(flagmodel::FlagDirPtr root,
                        flagmodel::UseCounterPtr useCounter);

    [[nodiscard]] bool initial();
    void refresh(EventTreeOwner* excludedOwner = nullptr);
    // DのEventTreeView.refreshCard()と同じく、外部モデ
    // ル通知ではラベル・警告を更新し、 実行中のグラフの行内編集は終わらせない。
    void refreshExternalState();
    void refreshTopStart();
    void refreshTrees();
    void replText() { refresh(); }
    void refUndoMax() { comm_.refUndoMax.call(); }
    void editEnter();
    void editCancel();
    [[nodiscard]] bool editing() const noexcept { return editing_; }
    void movingShell() noexcept {}
    void moveShell() noexcept {}

    [[nodiscard]] TreeItemId selection() const noexcept { return selectedId_; }
    bool select(TreeItemId id);
    bool selectContent(TreeItemId id);
    /// 独自描画のイベントツリーで、現在見えるコンテント行を選ぶ。
    /// ポインターの当たり判定と同じ、平坦化された行順を使う。
    bool selectVisibleContent(std::size_t index);
    bool selection(std::size_t index);
    [[nodiscard]] EventTreeOwner* selectionParent() const noexcept;
    [[nodiscard]] EventTreePtr selectionEventTree() const noexcept;
    [[nodiscard]] EventTreePtr displayedEventTree() const noexcept { return selectedTree_; }
    // DのEventTreeView.selectionと同じく、キーボードフォーカスによらず右ペインの選択を保つ。
    [[nodiscard]] ContentPtr selectionContent() const noexcept;
    // キーボードフォーカスに従う汎用コマンドは、このアクセサーを明示的に使う。
    [[nodiscard]] ContentPtr activeSelectionContent() const noexcept;
    [[nodiscard]] bool rightPaneActive() const noexcept;
    [[nodiscard]] bool commentUsesRightPane() const noexcept;
    [[nodiscard]] ContentPtr displayedContent() const noexcept { return selectedContent_; }
    [[nodiscard]] std::optional<FireCondition> selectionFire() const;
    [[nodiscard]] std::vector<std::size_t> selectionPath() const;
    [[nodiscard]] std::vector<std::size_t> contentPath() const;
    [[nodiscard]] TreeItemId findItem(const EventTreePtr& tree) const noexcept;
    [[nodiscard]] const NodeInfo* nodeInfo(TreeItemId id) const noexcept;
    [[nodiscard]] std::vector<TreeItemId> search(StringView text);
    [[nodiscard]] const std::vector<TreeItemId>& searchResults() const noexcept {
        return searchResults_;
    }
    bool searchNext();
    void clearSearch() noexcept;

    [[nodiscard]] String statusLine() const;
    void refreshStatusLine();
    [[nodiscard]] bool toolWindowOpen() const noexcept { return toolWindowOpen_; }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& ignitionKindControl() const
        noexcept { return treeKind_; }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& ignitionValueControl() const
        noexcept { return ignition_; }
    [[nodiscard]] const std::shared_ptr<lib::NumberEdit>& roundIgnitionControl() const
        noexcept { return roundIgnition_; }
    [[nodiscard]] const std::shared_ptr<lib::ComboBox>& ignitionTimingControl() const
        noexcept { return keyCodeTiming_; }
    void openToolWindow();
    void closeToolWindow();

    [[nodiscard]] bool canCreateEvent() const noexcept;
    [[nodiscard]] bool canCreateEventWithIgnition() const;
    // 左下の炎の操作ボタン（発火条件を追加）。
    [[nodiscard]] bool canAddFire() const;
    void createEvent();
    void createEventWithIgnition();
    [[nodiscard]] EventTreePtr createEventWithIgnition(
        const std::vector<ContentPtr>& starts);
    [[nodiscard]] EventTreePtr appendTree(EventTreePtr tree,
                                           std::size_t index = static_cast<std::size_t>(-1),
                                           EventTreeOwner* explicit_owner = nullptr);
    bool deleteEvent(const EventTreePtr& tree = {});
    bool cloneEvent(const EventTreePtr& tree = {});
    bool setTreeName(String value, const EventTreePtr& tree = {});
    bool setComment(String value, EventTreeOwner* owner = nullptr,
                    const EventTreePtr& tree = {});
    [[nodiscard]] String comment(EventTreeOwner* owner = nullptr,
                                 const EventTreePtr& tree = {}) const;

    [[nodiscard]] std::vector<FireCondition> fireConditions(
        const EventTreePtr& tree = {}) const;
    bool addFire(const FireCondition& fire, const EventTreePtr& tree = {});
    bool removeFire(const FireCondition& fire, const EventTreePtr& tree = {});
    bool createEventFire(const FireCondition& fire);
    bool addManyRounds(const std::vector<std::uint32_t>& rounds);
    bool setKeyCodeMatchingType(types::MatchingType value);
    [[nodiscard]] bool canSetKeyCodeMatchingType(types::MatchingType value) const noexcept;
    [[nodiscard]] bool canSetKeyCodeTiming(types::FKCKind value) const noexcept;
    bool setKeyCodeTiming(types::FKCKind value);
    bool copyConvertedKeyCode(types::FKCKind value);
    bool copySelectionXML(bool shallow);
    [[nodiscard]] std::size_t keyCodesIndex(const EventTreePtr& tree = {}) const;
    [[nodiscard]] String fireText(const FireCondition& fire,
                                  const EventTreePtr& tree = {}) const;
    static std::vector<String> eventTreeNames(const Props& prop,
                                              const event::EventTree& tree);

    [[nodiscard]] bool canUp() const noexcept;
    [[nodiscard]] bool canDown() const noexcept;
    bool up();
    bool down();
    bool swapToParent();
    // DのEventTreeView.swapToPCImpl()
    // 相当。parと唯一の子nextを交換し、 nextをparの
    // 位置へ、parをnextの子へ移す。分岐文字列も一緒に移す。
    void swapParentChildContent(const ContentPtr& par, const ContentPtr& next);
    bool swapToChild();

    [[nodiscard]] bool canEdit() const noexcept;
    [[nodiscard]] bool canEditSelection() const noexcept;
    [[nodiscard]] bool editSelection();
    bool editEnd(TreeItemId id, String text);
    [[nodiscard]] bool canWriteComment() const noexcept;
    bool writeComment(String text);
    /// 後からのグラフ選択に左右されず、モードレスのコメント編集が保持するコンテントを確定する。 D
    /// のEventTreeView.writeComment/UndoContentに対応する。
    bool writeContentComment(const ContentPtr& content, String text);
    [[nodiscard]] bool addContent(const ContentPtr& content,
                                  const ContentPtr& parent = {});
    [[nodiscard]] bool insertContent(std::size_t index, const ContentPtr& content,
                                     const ContentPtr& parent = {});
    [[nodiscard]] bool canDeleteContent(const ContentPtr& content = {}) const noexcept;
    [[nodiscard]] bool deleteContent(const ContentPtr& content = {});
    [[nodiscard]] bool moveContent(const ContentPtr& content, int delta);
    [[nodiscard]] bool cut1Content();
    [[nodiscard]] bool copy1Content();
    [[nodiscard]] bool pasteInsert();
    [[nodiscard]] bool pasteInsert(lib::CommandGesture gesture);
    [[nodiscard]] bool del1Content();
    [[nodiscard]] bool canCut1Content() const noexcept;
    [[nodiscard]] bool canDel1Content() const noexcept;
    [[nodiscard]] bool canCopy1Content() const noexcept;
    [[nodiscard]] bool canPasteInsert() const noexcept;
    [[nodiscard]] bool canSwapToParent() const noexcept;
    [[nodiscard]] bool canSwapToChild() const noexcept;
    [[nodiscard]] bool canWrapTree() const noexcept;
    bool wrapTree();
    [[nodiscard]] bool canFindStartUsers() const noexcept;
    bool findStartUsers();
    [[nodiscard]] bool canConvert(types::CType type,
                                  const ContentPtr& content = {}) const;
    bool convert(types::CType type, const ContentPtr& content = {});
    [[nodiscard]] std::vector<types::CType> conversionTypes() const;
    bool pasteScript(StringView script, bool tryInsert = true,
                     bool createEvent = false);
    void edit();
    /// モーダルでのEventTreeプロパティ編集を、グラフ
    /// 変更と同じスナップショット・Undo・通知処理で行う。
    bool editEventTree(const std::function<bool()>& action);
    // コンテントだけの履歴では、元のEventTreeと発火条件を保つ。
    bool replaceEventContents(const EventTreePtr& tree,
                              const std::vector<ContentPtr>& starts);
    bool convertStartToPackage(const ContentPtr& start,
                               const std::function<std::uint64_t()>& createPackage);
    // モードレスのコンテント画面は、適用で何度も変更を公開できる。
    // 適用ごとに基準状態を持ち、DのEventTreeView.appliedEdit
    // と同じく各変更に独立したUndoを作る。
    [[nodiscard]] bool beginExternalContentEdit(const ContentPtr& content);
    void recordExternalChildNames(const ContentPtr& content,
        const std::vector<std::pair<ContentPtr, String>>& changes);
    bool commitExternalContentEdit(const ContentPtr& content);
    bool cancelExternalContentEdit(const ContentPtr& content);
    void endExternalContentEdit(const ContentPtr& content) noexcept;
    [[nodiscard]] bool hasPendingContentCreate(
        const ContentPtr& content = {}) const noexcept;
    bool commitPendingContentCreate(const ContentPtr& content);
    bool cancelPendingContentCreate(const ContentPtr& content = {}) noexcept;
    /// 保存済みのツールボックス設定で配置する。 ポインター・コマン
    /// ド操作の一時的な修飾には、下の非公開オーバーロードを使う。
    [[nodiscard]] bool placeSelectedContentTool(bool insertBefore = false);

    [[nodiscard]] bool canDoTCPD() const noexcept { return !readOnly_; }
    [[nodiscard]] bool canDoT() const noexcept {
        return !readOnly_ && (rightPaneActive() ? canDeleteContent() : canCopySelection());
    }
    [[nodiscard]] bool canDoC() const noexcept { return canCopySelection(); }
    [[nodiscard]] bool canDoP() const noexcept { return !readOnly_ && clipboardAvailable(); }
    [[nodiscard]] bool canDoD() const noexcept { return canDoT(); }
    [[nodiscard]] bool canDoClone() const noexcept { return !readOnly_ && canCopySelection(); }

    [[nodiscard]] bool copy();
    [[nodiscard]] bool cut();
    [[nodiscard]] bool paste();
    [[nodiscard]] bool clone();
    [[nodiscard]] bool pasteData(const lib::ClipboardData& data);
    [[nodiscard]] lib::ClipboardData clipboardData() const;
    [[nodiscard]] String itemToXML(TreeItemId id = 0, bool canFire = true,
                                   bool paneId = false) const;

    void beginDrag();
    [[nodiscard]] lib::ClipboardData dragData() const;
    bool drop(lib::DragEvent& event);
    [[nodiscard]] lib::DragSourceBinding dragSourceBinding();
    [[nodiscard]] lib::DropTargetBinding dropTargetBinding();

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    void undo();
    void redo();

    bool forceSel(const std::vector<std::size_t>& eventPath);
    bool openCWXPath(StringView path, bool shellActivate = false);
    [[nodiscard]] std::vector<String> openedCWXPath() const;
    [[nodiscard]] bool canSelectCurrentEvent() const noexcept;
    void selectCurrentEvent();
    [[nodiscard]] bool canSelectConnectedResource() const noexcept;
    void selectConnectedResource();

    [[nodiscard]] static std::vector<EventTreeOwner*> etos(EventTreeOwner& owner);
    [[nodiscard]] static EventView* mainEventView(const std::vector<EventView*>& views) noexcept;

    // ホストは弱参照の操作元解決処理を渡す。 履歴は操作元のラッ
    // パーだけを保持し、動作中のネイティブホストを所有しない。
    void historyOrigin(std::function<std::shared_ptr<void>()> origin) { historyOrigin_ = std::move(origin); }
    void historyReplayed(lib::Action replayed) { historyReplayed_ = std::move(replayed); }
    // Dと同じく、所有者ツリーのカードのホバーはホストの現在のシナリオスキンとデバッグビューを使う。
    void cardPreviewResolvers(std::function<areaviewutils::Skin::Ptr()> skin,
                              std::function<bool()> enemyDebug) {
        cardPreviewSkin_ = std::move(skin);
        cardPreviewEnemyDebug_ = std::move(enemyDebug);
    }
    void historyModelOwner(EventTreeOwnerPtr owner) { areaHolder_ = std::move(owner); }
    void historyModelNotify(lib::Action notify) { historyModelNotify_ = std::move(notify); }
    void historyModelDeleting(std::function<void(const ContentPtr&)> notify) { historyModelDeleting_ = std::move(notify); }
    void retireHistoryPresentation();

private:
    void closeCardPreview() noexcept;
    void refreshImpl(EventTreeOwner* excludedOwner, bool cancelInlineEditors);
    void triggerCardPreview(const lib::PointerEvent& event);
    void refreshHoverStatus(const lib::PointerEvent& event);
    std::shared_ptr<areaviewutils::Preview> cardPreview_;
    std::shared_ptr<areaviewutils::AbstractSpCard> cardPreviewOwner_;
    std::function<areaviewutils::Skin::Ptr()> cardPreviewSkin_;
    std::function<bool()> cardPreviewEnemyDebug_;
    std::uint64_t cardPreviewGeneration_{};
    void editEventTreeSlope(int value);
    [[nodiscard]] bool canMoveContentSibling(const ContentPtr& content, int delta) const noexcept;
    struct ClipboardPayload {
        String kind;
        xml::XNode node;
        // Dと同じく、コピーしたコンテントに元の所有者のnext種別を記録する。
        String lastNextType;
    };
    struct OwnerSnapshot {
        EventTreeOwner* owner{};
        std::vector<EventTreePtr> trees;
    };

    [[nodiscard]] EventTreePtr selectedTreeOr(const EventTreePtr& tree) const noexcept;
    [[nodiscard]] ContentPtr selectedContentOr(const ContentPtr& content) const noexcept;
    // Undo/RedoはEventTreeの深いコピーを復元する。 モデル変
    // 更で選択を保持する前に、キャッシュした選択を現在のツリーから解決し直す。
    [[nodiscard]] ContentPtr canonicalSelectedContent();
    [[nodiscard]] EventTreeOwner* selectedOwnerOr(EventTreeOwner* owner) const noexcept;
    [[nodiscard]] std::vector<OwnerSnapshot> snapshot() const;
    void restore(const std::vector<OwnerSnapshot>& values,
                 const SelectionSnapshot& selection);
    bool mutate(const std::function<bool()>& action, bool useCount = true,
                bool recordUndo = true);
    void restoreSelection(const SelectionSnapshot& selection);
    bool mutateSubtree(const ContentPtr& content, const std::function<bool()>& action);
    void notify(EventTreePtr tree = {}, ContentPtr content = {}, bool useCount = true);
    void ensureWidgets(const Parent& parent);
    void createTopBar();
    void createContentsPalette(const std::shared_ptr<lib::Container>& parent);
    void rebuildContentTable();
    bool refreshGraphPreferences();
    void rebuildGraphRows();
    struct GraphEditSpec {
        String value;
        std::vector<lib::ChoiceItem> choices;
        bool editable_choice{};
        std::optional<float> minimum_width;
        std::function<std::vector<lib::ChoiceItem>()> refresh_choices{};
        std::function<std::shared_ptr<void>(lib::Action)> refresh_binding{};
    };
    // Dと同じく、確定ではEventEdit.minimumWidthをリセットし、中止では保持する。
    std::optional<float> graphEditorMinimumWidth_;
    [[nodiscard]] std::optional<GraphEditSpec> graphEditSpec(
        const ContentPtr& content) const;
    void paintEventGraph(lib::DrawingContext& context, lib::Rect dirty);
    void handleEventGraphPointer(lib::PointerEvent& event);
    [[nodiscard]] float eventGraphDetailX(float canvasWidth) const noexcept;
    void showEventContextMenu(const lib::WidgetPtr& owner, lib::Point position);
    void handleEventGraphKey(lib::KeyEvent& event);
    void invalidateEventGraph();
    void revealSelectedEventContent();
    void refreshToolbarState();
    // DのEventTreeView.selContentTool/ContentsToolBox.updateCursor
    // 相当。 コンテントの配置ツールが有効な間、ツリー・グラフ・ツールバーのカーソルを
    // そのコンテントのアイコン（images.cursor(CType)）にする。
    void updateContentToolCursor();
    void applyContentToolCursor(lib::Widget& widget) const;
    void publishContentToolState();
    void refreshIgnitionChoices();
    [[nodiscard]] std::optional<FireCondition> ignitionFromControls() const;
    [[nodiscard]] EventTreePtr createEventWithIgnition(
        const std::vector<ContentPtr>& starts,
        const std::optional<FireCondition>& ignition);
    [[nodiscard]] bool contentTypeAllowed(types::CType type) const;
    void selectContentTool(types::CType type);
    [[nodiscard]] bool placeSelectedContentToolWithOptions(bool insertBefore,
                                                            bool insertFirst,
                                                            bool autoEdit);
    enum class PutMode { Select, Quick, Continue };
    [[nodiscard]] lib::ResourceId contentImage(types::CType type) const;
    [[nodiscard]] lib::ResourceId viewImage(StringView name,
                                            types::CType fallback) const;
    [[nodiscard]] String contentName(types::CType type) const;
    [[nodiscard]] String contentDescription(types::CType type) const;
    [[nodiscard]] lib::ResourceId templateContentImage(StringView script) const;
    [[nodiscard]] String eventText(const ContentPtr& parent,
                                   const ContentPtr& content) const;
    [[nodiscard]] String ownerDisplayName() const;
    [[nodiscard]] String ownerDisplayName(const EventTreeOwner& owner) const;
    [[nodiscard]] EventTreePtr findTreeByObjectId(StringView id) const;
    [[nodiscard]] String selectedContentId() const;
    [[nodiscard]] std::uint32_t startUseCount(const ContentPtr& content) const;
    using NodeMap = std::unordered_map<TreeItemId, NodeInfo>;
    using TreeIdMap = std::unordered_map<const event::EventTree*, TreeItemId>;
    using ContentIdMap = std::unordered_map<const event::Content*, TreeItemId>;
    void rebuildCards(EventTreeOwner* excludedOwner = nullptr);
    void refreshEventTreeImages();
    void rebuildContents();
    TreeItemId appendCardNode(NodeMap& nodes, TreeIdMap& treeIds,
                              std::vector<lib::TreeNode>& treeNodes,
                              TreeItemId& nextId, const NodeInfo& info,
                              TreeItemId parent, String text,
                              bool expanded = false);
    TreeItemId appendContentNode(NodeMap& nodes, ContentIdMap& contentIds,
                                 std::vector<lib::TreeNode>& treeNodes,
                                 TreeItemId& nextId, const ContentPtr& content,
                                 const ContentPtr& parent, TreeItemId parentId,
                                 std::size_t index,
                                 std::unordered_set<const event::Content*>& visiting);
    void appendFireNodes(NodeMap& nodes, TreeIdMap& treeIds,
                         std::vector<lib::TreeNode>& treeNodes,
                         TreeItemId& nextId, const EventTreePtr& tree,
                         TreeItemId treeId);
    void selectFallback();
    // 選択されたCards行を解決する。Cardsの選択を公開していない
    // ヘッドレス・APIの呼び出し元には、 現在の行を使う代替を残す。
    [[nodiscard]] TreeItemId leftOrActiveSelectionId() const noexcept;
    [[nodiscard]] String contentText(const ContentPtr& content) const;
    [[nodiscard]] String eventTreeNameForIgnition(
        EventTreeOwner& owner, const FireCondition& fire) const;
    [[nodiscard]] String eventTreeDisplayName(
        EventTreeOwner& owner, const event::EventTree& tree) const;
    [[nodiscard]] String eventTreeDisplayLabel(
        EventTreeOwner& owner, const event::EventTree& tree) const;
    [[nodiscard]] bool contains(const ContentPtr& root, const ContentPtr& value) const;
    [[nodiscard]] bool canCopySelection() const noexcept;
    [[nodiscard]] std::optional<ClipboardPayload> parseClipboard(
        StringView text) const;
    [[nodiscard]] String makeClipboard(StringView kind, const xml::XNode& node) const;
    bool pastePayload(const ClipboardPayload& payload, bool tryInsert = false);
    bool pasteWithInsertFirst(bool value, bool tryInsert = false);
    bool pasteText(StringView text, bool tryInsert);
    bool pasteEvent(const EventTreePtr& tree);
    bool pasteContent(const EventTreePtr& tree, bool tryInsert = false,
                      StringView lastNextType = {});
    bool pasteContents(const std::vector<ContentPtr>& contents, bool tryInsert,
                       StringView lastNextType = {});
    // DのEventTreeView.adjustText()相当。evtを
    // ownerの下へ追加する前に、 next種別の契約が変わったか、兄弟が
    // 同じ分岐文字列を使っていれば文字列を消す。 Content::add
    // が空いている値を選べるようにし、例えば○分岐の2番目の子を×にする。
    void adjustText(const ContentPtr& owner, const ContentPtr& evt,
                    StringView lastNextType) const;
    bool pasteFire(const ClipboardPayload& payload);
    [[nodiscard]] bool clipboardOffersPaste() const noexcept;
    [[nodiscard]] bool clipboardAvailable() const noexcept;
    void writeClipboard(const lib::ClipboardData& data);
    [[nodiscard]] lib::ClipboardData readClipboard() const;
    void bindEventListDragDrop();
    bool dropEventList(lib::DragEvent& event);
    void bindDragDrop();
    void unbindDragDrop();

    void pushHistory(lib::Action undo, lib::Action redo, bool retainPresentation = true);
    [[nodiscard]] std::function<void(const String&)> treeScalarReplay(
        const EventTreePtr& tree, bool comment);
    [[nodiscard]] std::function<void(const std::vector<OwnerSnapshot>&, const SelectionSnapshot&)> snapshotModelReplay(
        std::function<void(const SelectionSnapshot&)> restorePeers = {});
    [[nodiscard]] std::function<void()> historyOwnerGuard(EventTreeOwner* owner) const;
    std::function<void(const ContentPtr&)> historyModelDeleting_;
    lib::Action historyModelNotify_;
    std::function<std::shared_ptr<void>()> historyOrigin_;
    std::function<std::optional<SelectionSnapshot>()> historySelectionSource_;
    std::function<std::optional<SelectionSnapshot>()> historyReplaySelectionSource_;
    std::function<void(const SelectionSnapshot&)> historySelectionRestorer_;
    std::function<bool()> beforeFireEdit_{};
    std::function<bool()> cancelSameOwnerEditors_{};
    std::function<void(lib::Action)> historyReplayCoordinator_{};
    lib::Action historyReplayed_;
    bool historyPresentationRetired_{};
    Commons comm_;
    Props prop_;
    EventTreeOwner* area_{};
    EventTreeOwnerPtr areaHolder_{};
    UndoManager* undo_{};
    bool readOnly_{};
    bool initialized_{};
    bool editing_{};
    // 開いている名前・選択肢エディターの画面範囲。editSelection()を参照。
    std::function<std::optional<lib::Rect>()> editBounds_;
    [[nodiscard]] bool inlineNameEditActive() const;
    bool toolWindowOpen_{};
    String id_;
    String ownerTitle_;
    flagmodel::FlagDirPtr localVariableRoot_{};
    flagmodel::UseCounterPtr localVariableUseCounter_{};
    EventMessages messages_{};
    lib::Action editAction_{};
    std::function<bool(const ContentPtr&, const ContentPtr&)>
        autoOpenContentAction_{};
    lib::Action createEventAction_{};
    lib::Action writeCommentAction_{};
    std::function<bool(StringView, bool, bool)> scriptTemplateResolver_{};
    std::optional<bool> pasteInsertFirstOverride_{};

    PanelPtr panel_;
    std::shared_ptr<lib::CoolBar> topBarPanel_;
    std::shared_ptr<lib::Panel> all_compact_toolbar_;
    PanelPtr localVariablesPanel_;
    struct PaletteTarget {
        EventView* owner{};
        std::weak_ptr<const int> lifetime;
        [[nodiscard]] EventView* live() const noexcept {
            return lifetime.expired() ? nullptr : owner;
        }
    };
    struct FloatingPalette {
        std::shared_ptr<lib::Window> window;
        std::shared_ptr<lib::GuiBackend> gui;
        lib::Action disconnectOwner;
        bool opened{};
        ~FloatingPalette();
    };
    void attachPaletteHost(const std::shared_ptr<lib::Container>& parent);
    void bindAutoHidePointerObserver();
    void unbindAutoHidePointerObserver() noexcept;
    void showFloatingPalette();
    void saveFloatingPalettePosition();
    [[nodiscard]] lib::Rect clampFloatingPaletteBounds(lib::Rect bounds) const;
    [[nodiscard]] lib::Point floatingPaletteOrigin() const;
    struct PaletteTransfer {
        PanelPtr host;
        std::shared_ptr<FloatingPalette> floating;
        std::shared_ptr<lib::CoolBar> panel;
        PanelPtr trigger;
        std::vector<std::shared_ptr<lib::ToolBar>> bars;
        std::vector<std::shared_ptr<lib::ComboBox>> selectors;
        std::shared_ptr<PaletteTarget> target;
        ContentToolState state;
    };
    lib::WidgetOperationLifetime paletteLifetime_;
    std::shared_ptr<PaletteTarget> paletteTarget_;
    std::uint64_t palettePointerObserver_{};
    PanelPtr paletteHost_;
    std::shared_ptr<FloatingPalette> floatingPalette_;
    PanelPtr rightPanel_;
    std::shared_ptr<lib::CoolBar> palettePanel_;
    std::shared_ptr<lib::Panel> paletteTrigger_;
    std::shared_ptr<lib::SplitView> body_;
    std::shared_ptr<lib::SplitView> leftSplit_;
    std::shared_ptr<lib::TreeView> cards_;
    std::shared_ptr<lib::TreeView> contents_;
    std::shared_ptr<lib::TableView> contentTable_;
    std::shared_ptr<lib::ScrollView> eventGraphScroll_;
    std::shared_ptr<lib::Canvas> eventGraphCanvas_;
    std::shared_ptr<lib::ToolBar> toolbar_;
    std::shared_ptr<lib::ToolBar> viewToolbar_;
    std::vector<std::shared_ptr<lib::ToolBar>> paletteBars_;
    std::vector<std::shared_ptr<lib::ComboBox>> paletteGroupSelectors_;
    std::shared_ptr<lib::ComboBox> treeKind_;
    std::shared_ptr<lib::ComboBox> ignition_;
    std::shared_ptr<lib::NumberEdit> roundIgnition_;
    std::shared_ptr<lib::ComboBox> keyCodeTiming_;
    std::shared_ptr<lib::NumberEdit> slope_;
    std::shared_ptr<lib::StatusBar> status_;
    std::unique_ptr<variables::FlagTable> localVariables_;
    std::vector<TreeItemId> tableItems_;
    struct GraphRow {
        TreeItemId id{};
        ContentPtr content{};
        lib::Array<String> warnings{};
        lib::ResourceId warning_icon{};
        std::size_t row{};
        std::size_t logical_line{};
        std::size_t depth1{};
        std::size_t depth2{};
        float y{};
        std::optional<std::size_t> parent_row{};
        lib::Rect hit{};
    };
    std::vector<GraphRow> graphRows_;
    std::unordered_map<const event::Content*, std::size_t> graphLogicalLines_;
    struct StoredGraphPosition {
        lib::Point offset{};
        std::size_t selected_row{};
    };
    std::unordered_map<String, StoredGraphPosition> storedGraphPositions_;
    float graphRowsScale_{0.0F};
    float graphMeasuredRight_{0.0F};
    std::shared_ptr<void> graphEditBinding_{};
    types::CType contentToolType_{types::CType::TalkMessage};
    bool contentToolArrow_{true};
    // Web版ALLの追加仕様。イベントビュー上のポイン
    // ター位置をキャンバス座標で保持し、 選択中のコンテ
    // ントツールのアイコンをマウスカーソルの左へ描く。
    std::optional<lib::Point> toolCursorPosition_;
    [[nodiscard]] static bool toolCursorIconEnabled() noexcept;
    PutMode contentPutMode_{PutMode::Select};
    bool contentAutoEdit_{};
    bool contentInsertFirst_{};
    int eventTreeSlope_{16};
    int eventTreeSlopeMaximum_{16};
    bool eventTreeSlopeMaximumConfigured_{};
    bool showEventTreeDetail_{true};
    bool showLineNumber_{true};
    float eventGraphDetailWidth_{200.0F};
    bool eventGraphDetailResizeActive_{};
    std::uint64_t eventGraphDetailResizePointer_{};

    TreeItemId nextId_{1};
    TreeItemId rootId_{};
    TreeItemId selectedId_{};
    TreeItemId selectedContentId_{};
    // ALLとタッチは解放時に選択を確定する。単独起動のマウス・ペンは
    // Dと同じくDownで即時選択し、 解放までの操作状態も保持する。
    std::uint64_t pendingClickPointer_{};
    TreeItemId pendingClickContentId_{};
    lib::Point pendingClickStart_{};
    bool pendingClickMoved_{};
    bool pendingClickSelectionCommitted_{};
    // DのTextEditMFListenerと同じく、選
    // 択済みの行の名前をクリックすると行内編集を始める。
    bool pendingClickWasSelected_{};
    std::uint64_t nameEditRequest_{};
    std::optional<lib::TimerId> nameEditTimer_{};
    std::shared_ptr<bool> nameEditLifetime_{std::make_shared<bool>(true)};
    void cancelPendingNameEdit() noexcept;
    std::optional<FireCondition> pendingFireSelection_;
    String pendingFireTreeId_;
    std::optional<NodeInfo> eventListDragSource_;
    bool eventListMoveHandled_{};
    TreeItemId dragSourceId_{};
    // DのEventEditorは有効な移動先へドラッグ中にupdateLightup
    // を呼ぶ。 この一時表示をモデル選択から分け、外へ
    // 出たり中止したりしてもコマンド対象を変えない。
    TreeItemId dragHoverContentId_{};
    // Dの通常のマウス強調は表示だけの状態とし、ドラッグ状態とは分ける。
    std::optional<lib::Point> graphHoverPointerPosition_;
    bool dragMoveHandled_{};
    // Dと同じく、左のイベント・発火条件ツリーと右のコンテン
    // トツリーは独立した部品にする。 両方の選択を表示して
    // いても、コマンドは最後に操作した側だけを対象にする。
    bool contentSelectionActive_{};
    // 左の所有者行を選んでも右ペインの表示を保つ。Dの_etreeに対応する。
    EventTreePtr selectedTree_;
    ContentPtr selectedContent_;
    ContentPtr navigationContent_;
    struct ExternalContentEdit {
        ContentPtr content;
        std::vector<OwnerSnapshot> before;
        SelectionSnapshot before_selection;
        ContentPtr fields_before;
        bool creating{};
        std::vector<std::pair<std::weak_ptr<event::Content>, String>> child_names_before;
    };
    std::unordered_map<const event::Content*, ExternalContentEdit>
        externalContentEdits_;
    struct PendingContentCreate {
        ContentPtr content;
        ContentPtr dialog_parent;
        std::function<bool()> apply;
    };
    std::unordered_map<const event::Content*, PendingContentCreate>
        pendingContentCreates_;
    std::unordered_map<TreeItemId, NodeInfo> nodes_;
    std::unordered_map<const event::EventTree*, TreeItemId> treeIds_;
    std::unordered_map<const event::Content*, TreeItemId> contentIds_;
    std::vector<TreeItemId> searchResults_;
    std::size_t searchCursor_{};
    String searchText_;
    String statusText_;
    TreeItemId hoveredStatusId_{};
    std::uint64_t hoverStatusGeneration_{};
    lib::ClipboardData clipboardMemory_;
    bool hasClipboardMemory_{};
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::eventview
