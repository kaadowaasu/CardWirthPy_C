#pragma once

// cwx/editor/gui/dwt/flagspane.d
// の移植可能なC++20実装。 ディレクト
// リツリーと変数表の組み合わせを、モデルと
// lib部品で公開する。 SWT/DWTの
// ハンドルをモデルへ持ち込まず、任意のバッ
// クエンドで同じ部品を描けるようにする。

#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/importutils.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/splitpane.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cwxeditor::cwx::summary {
class Summary;
}
namespace cwxeditor::cwx::props {
class CProps;
}

namespace cwxeditor::cwx::editor::gui::dwt::dprops { class Props; }

namespace cwxeditor::cwx::editor::gui::dwt::incsearch {
class IncSearch;
}

namespace cwxeditor::cwx::editor::gui::dwt::eventview { struct FireCondition; }

namespace cwxeditor::cwx::editor::gui::dwt::flagspane {

namespace core = ::cwxeditor::cwx;
namespace event = ::cwxeditor::cwx::event;
namespace expression = ::cwxeditor::cwx::expression;
namespace flag = ::cwxeditor::cwx::flag;
namespace importutils = ::cwxeditor::cwx::importutils;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;
namespace usecounter = ::cwxeditor::cwx::usecounter;
namespace lib = ::cwxeditor::lib;

using Parent = std::shared_ptr<lib::Container>;
using PanelPtr = std::shared_ptr<lib::Panel>;
using FlagPtr = flag::FlagPtr;
using StepPtr = flag::StepPtr;
using VariantPtr = flag::VariantPtr;
using FlagDirPtr = flag::FlagDirPtr;
using UseCounterPtr = flag::UseCounterPtr;
using FlagList = flag::FlagList;
using StepList = flag::StepList;
using VariantList = flag::VariantList;
using String = lib::String;
using StringView = lib::StringView;

/// コピーしても安全な小さなデリゲート一覧。 Commonsをコピー
/// しても同じ通知元を保持し、Dの参照のようなメンバーに合わせる。
template <typename Signature>
class Signal;

template <typename... Arguments>
class Signal<void(Arguments...)> {
public:
    using Callback = std::function<void(Arguments...)>;
    using Token = std::size_t;

    Signal() : state_(std::make_shared<State>()) {}
    void add(Callback callback) { (void)connect(std::move(callback)); }
    [[nodiscard]] Token connect(Callback callback) {
        if (!callback) return 0;
        const auto token = state_->nextToken++;
        state_->callbacks.emplace_back(token, std::move(callback));
        return token;
    }
    bool remove(Token token) noexcept {
        if (token == 0) return false;
        const auto old_size = state_->callbacks.size();
        std::erase_if(state_->callbacks,
                      [token](const auto& entry) { return entry.first == token; });
        return state_->callbacks.size() != old_size;
    }
    void clear() noexcept { state_->callbacks.clear(); }
    void call(Arguments... arguments) const {
        const auto callbacks = state_->callbacks;
        for (const auto& entry : callbacks)
            if (entry.second) entry.second(arguments...);
    }
    [[nodiscard]] bool empty() const noexcept { return state_->callbacks.empty(); }

private:
    struct State {
        std::vector<std::pair<Token, Callback>> callbacks;
        Token nextToken{1};
    };
    std::shared_ptr<State> state_;
};

enum class VariableKind;

/// 二つの部品が使う、共通の状態・変数の通知。
struct Commons {
    Signal<void()> refScenario;
    Signal<void()> refUndoMax;
    Signal<void()> refUseCount;
    // D の削除後通知を共有先へ届ける。ローカル通知とは別経路にする。
    std::function<void()> publishUseCount{};
    Signal<void()> refreshText;
    Signal<void()> refDataVersion;
    Signal<void()> refSpecialPreview;
    Signal<void()> refSortCondition;
    Signal<void(const FlagList&, const StepList&, const VariantList&)>
        refFlagAndStep;
    Signal<void(const FlagList&, const StepList&, const VariantList&)>
        delFlagAndStep;
    using VariableDeletionCallback =
        std::function<void(const FlagList&, const StepList&, const VariantList&)>;
    // Dは所有者共通のデリゲートへダイアログ監視を登録
    // し、ペイン外の監視とも登録順を保つ。 単独ホスト
    // は、自分が通知する同じ局所シグナルを購読する。
    std::function<lib::Action(VariableDeletionCallback)> subscribeVariableDeletion{};
    [[nodiscard]] lib::Action observeVariableDeletion(VariableDeletionCallback callback) {
        if (subscribeVariableDeletion)
            return subscribeVariableDeletion(std::move(callback));
        auto signal = delFlagAndStep;
        const auto token = signal.connect(std::move(callback));
        return [signal, token]() mutable { signal.remove(token); };
    }
    Signal<void(const FlagDirPtr&)> refFlagDir;
    Signal<void(const FlagDirPtr&)> delFlagDir;
    Signal<void(String, bool)> replaceIDRequest;
    // 汎用ホストとの互換性を保ちつつ、参照検索で対象の型を保持できるようにする。
    Signal<void(String, VariableKind, bool)> typedReplaceIDRequest;
    // ローカル変数の参照は共通の結果ウィンドウを開き、グローバルIDの置換へは送らない。
    std::function<bool(const std::shared_ptr<core::path::CWXPath>&)> findLocalVariableUsers;

    // Propsの生ポインターを保持せず、有効なホストを通じて解決する。
    std::function<dprops::Props*()> editorProps{};
    lib::Services services{};
    std::function<bool()> xmlCopyProvider{};
    /// D互換のポップアップは、メインメニューと同じMenuID
    /// の表示・コマンド登録を使う。 選択と変更状態はペイン
    /// が所有し、ホストは共通ラベルとコマンドだけを渡す。
    std::function<String(types::MenuID)> menuText{};
    std::function<String(StringView)> commentTitle{};
    std::function<void(types::MenuID)> menuAction{};
    std::function<bool(types::MenuID)> menuEnabled{};
    /// 同じMenuIDに対するポップアップのアイコン。DのcreateMenuItemの画像に対応する。
    std::function<lib::ResourceId(types::MenuID)> menuImage{};
    UseCounterPtr useCounter{};
    std::weak_ptr<::cwxeditor::cwx::summary::Summary> summary{};
    std::weak_ptr<lib::Window> dialogOwner{};
    // モードレス画面を開くたびに、ホストの現在の停止要求を読む。 関
    // 数経由で取得し、最初の画面がホストへ再入しても最新状態を使う。
    std::function<bool()> stopOpen{};
    lib::Action refreshToolBar{};
    std::function<void(const lib::WidgetPtr&, StringView)> setStatusLine{};
    std::function<lib::Array<event::ContentPtr>(StringView)>
        compileEventTemplate{};
    std::function<String(types::CType)> contentName{};
    std::function<String(types::CType)> contentDesc{};
    std::function<lib::ResourceId(types::CType)> contentImage{};
    std::function<lib::ResourceId(StringView)> eventViewImage{};
    lib::Action returnToScene{};
    std::function<bool(const event::ContentPtr&)> findStartUsers{};
    lib::Action openDuplicate{};
    String duplicateViewText{};
    std::function<String(const event::Content&)> contentText{};
    lib::Array<String> standardSelections{};
    std::function<lib::Array<String>()> standardSelectionsProvider{};
    // DのBranchMultiCoupon編集と同じく、共通のcreateCouponComboの候補元を使う。
    std::function<std::vector<String>(const event::ContentPtr&)>
        couponChoicesProvider{};
    std::function<std::shared_ptr<void>(lib::Action)> couponChangesBinding{};
    std::function<std::shared_ptr<void>(lib::Action)> selectionChangesBinding{};
    std::function<std::shared_ptr<void>(const std::shared_ptr<lib::TextEdit>&,
                                        const std::shared_ptr<lib::ComboBox>&)>
        setupSelectionEditor{};
    std::function<String(bool, std::optional<std::uint64_t>, std::optional<String>)> branchLocationText{};
    // パレットの配置と型変換に使う、共通のホスト初期化。
    std::function<event::ContentPtr(types::CType)> initialContent{};
    std::function<void(const event::ContentPtr&, types::CType)> convertContent{};
    const ::cwxeditor::cwx::props::CProps* modelProps{};
    std::function<String(const ::cwxeditor::cwx::event::ContentPtr&, const String&)> branchPreviewText{};
    std::function<String(const std::vector<types::CastRange>&)> branchCastRangesText{};
    std::function<String(types::Range, const String&)> branchRangeText{};
    std::function<void(const event::ContentPtr&)> deletingContent{};
    // 軽量なペイン表示はCProps/Skinを所有しないた
    // め、シナリオ診断はホストから渡す。 EventView
    // は、自分が公開する正確なツリー・表の行へ警告を付ける。
    std::function<lib::Array<String>(const event::CWXPath&)> warnings{};
    std::function<lib::Array<String>(const event::EventTree&,
        const ::cwxeditor::cwx::editor::gui::dwt::eventview::FireCondition&)> ignitionWarnings{};
    lib::ResourceId warningImage{};
    std::function<lib::Array<structs::EvTemplate>()>
        globalEventTemplates{};
    std::function<lib::Array<structs::EvTemplate>()>
        scenarioEventTemplates{};
    lib::Action editScenarioEventTemplates{};
    String eventTemplatesText{"イベントテンプレート"};
    String editScenarioEventTemplatesText{"シナリオ固有イベントテンプレート"};

    // 周囲のEditorアプリが必要なEventViewコ
    // マンド。 部品の境界へまとめ、Windows・SDL
    // がシナリオモデルやパッケージ作成を知らなくても、 同
    // じD互換のコンテキストメニューを使えるようにする。
    lib::Action toScript{};
    lib::Action toScriptOne{};
    lib::Action toScriptAll{};
    lib::Action startToPackage{};
    lib::Action eventToPackage{};
    lib::Action addManyRounds{};
    lib::Action selectConnectedResource{};
    /// DのcreateKeyCodeComboと同じく、現在のスキンの標準キ
    /// ーコード、 シナリオ内で既に使われているキーコードの順で候補を渡す。
    lib::Callback<lib::Array<String>()> fireKeyCodes{};
    std::function<String(StringView, types::FKCKind)> convertFireKeyCode{};
    lib::Callback<bool()> canStartToPackage{};
    lib::Callback<bool()> canEventToPackage{};
    lib::Callback<bool()> canAddManyRounds{};
    lib::Callback<bool()> canSelectConnectedResource{};
    // EventTreeViewはOwnerとContentの両方の
    // メニューを公開する。 右側のContentコマンドを分け、古い
    // フォーカスではなく、開いたメニューの対象から資源の経路を選ぶ。
    std::function<void()> selectConnectedContentResource{};
    lib::Callback<bool()> canSelectConnectedContentResource{};

    /// バックエンドのクリップボードは任意。モデルにはメモリ内の代替
    /// を持たせ、 ヘッドレステストでも表示実装でも同じ動作にする。
    [[nodiscard]] std::shared_ptr<lib::ClipboardBackend> clipboard() const
        noexcept {
        return services.clipboard;
    }
};

struct Messages {
    String flag{"Flag"};
    String step{"Step"};
    String variant{"Variant"};
    String flagName{"Name"};
    String flagInit{"Value"};
    String variableInitializationShort{"Init"};
    String variableInitializationNameLeave{"シナリオ終了時"};
    String variableInitializationNameEventExit{"イベント終了時"};
    String variableInitializationNameBattleStart{"戦闘開始時"};
    String variableInitializationNameBattleEnd{"戦闘終了時"};
    String variableInitializationNameNone{"初期化しない"};
    String variableInitializationShortNameLeave{"シナリオ終了"};
    String variableInitializationShortNameEventExit{"イベント終了"};
    String variableInitializationShortNameBattleStart{"戦闘開始"};
    String variableInitializationShortNameBattleEnd{"戦闘終了"};
    String variableInitializationShortNameNone{"しない"};
    String flagCount{"Uses"};
    String flagDirRoot{"Flags"};
    String flagDirNew{"New Folder"};
    String newFlag{"Flag"};
    String newStep{"Step"};
    String newVariant{"Variant"};
    String flagTrue{"True"};
    String flagFalse{"False"};
    // DのFlagDirTreeは、Flagをオン・オフにするそれぞれに別のMsgs項目を使う。
    String setFlagTrue{"TRUEを設定"};
    String setFlagFalse{"FALSEを設定"};
    String stepName{"ステップ名"};
    String stepInitial{"初期値"};
    String stepCount{"段階数"};
    String flagEditName{"フラグ名"};
    String flagInitial{"初期値"};
    String variantName{"コモン名"};
    String numberValue{"数値"};
    String stringValue{"文字列値"};
    String booleanValue{"真偽値"};
    String variableInitialization{"初期化タイミング"};
    String expandSpecialCharacters{"特殊文字を展開する"};
    String setStepValue{"%1$sを設定"};
    String valueNumber{"番号"};
    String valueName{"値"};
    String flagDialogTitle{"フラグの設定"};
    String stepDialogTitle{"ステップの設定"};
    String variantDialogTitle{"コモンの設定"};
    String variantValueHint{"書式:\n数値 = 9999\n文字列 = \"文字列\"\n真偽値 = TRUE または FALSE\n※ 文字列内に\"を入れる時は\"\"と記述"};
    String warningInvalidVariantValue{"書式が正しくありません。\n\n%1$s"};
    String warningLimitForNumberOfVariant{
        "数値の桁数が大きすぎるため、ビット数の制限による誤差が発生する可能性があります。"};
    String dialogOK{"OK"};
    String dialogCancel{"キャンセル(C)"};
    String dialogApply{"適用(A)"};
    String dialogClose{"閉じる"};
    String statusFormat{"%s=%d"};
    String statusSelectedFormat{"%s selected=%d"};
    // DのeventTreeNamesは、Start名の予約に現在のカタログを使う。
    String enterTree{"到着"};
    String selectTree{"クリック"};
    String deadTree{"死亡"};
    String victoryTree{"勝利"};
    String packageTree{"パッケージイベント"};
    String playerCard{"プレイヤーカード"};
    String useTree{"使用イベント"};
    String escapeTree{"逃走"};
    String loseTree{"敗北"};
    String everyRoundTree{"毎ラウンド"};
    String roundEndTree{"ラウンド終了"};
    String round0Tree{"バトル開始"};
    String keyCodeTree{"[%1$s]"};
    String roundTree{"ラウンド %1$s"};
};

struct EventTreeMessages {
    String enterTree{"到着"};
    String selectTree{"クリック"};
    String deadTree{"死亡"};
    String victoryTree{"勝利"};
    String packageTree{"パッケージイベント"};
    String playerCard{"プレイヤーカード"};
    String useTree{"使用イベント"};
    String escapeTree{"逃走"};
    String loseTree{"敗北"};
    String everyRoundTree{"毎ラウンド"};
    String roundEndTree{"ラウンド終了"};
    String round0Tree{"バトル開始"};
    String keyCodeTree{"[%1$s]"};
    String roundTree{"ラウンド %1$s"};
};

struct FlagCreationDefaults {
    bool initial{true};
    std::vector<String> trues;
    std::vector<String> falses;
};

struct EtcProperties {
    bool startIncrementalSearchWhenKeyDown{true};
    std::function<bool()> startIncrementalSearchWhenKeyDownProvider;
    std::int32_t incrementalSearchBoxWidth{100};
    std::int32_t incrementalSearchType{0};
    std::function<void(std::int32_t)> storeIncrementalSearchType{};

    std::size_t undoMaxMainView{100};
    std::size_t undoMaxEtc{1024};
    // Propsはペインへコピーされるため、ダイアログごとに現在のホスト設定を解決する。
    std::function<std::size_t()> undoMaxEtcProvider{};
    bool editSelectionWithCombo{true};
    float selectionWidth{100.0F};
    // DのeditTriggerType。Quickは選択済みの名前を次のクリ
    // ックで編集し、 既定のSlowはダブルクリック時間を待ってから始める。
    bool editTriggerQuick{};
    bool showEventToolBar{true};
    std::int32_t localVariablesSashL{1};
    std::int32_t localVariablesSashR{1};
    std::function<void(std::int32_t, std::int32_t)> storeLocalVariablesSash{};
    bool flagSashV{false};
    std::int32_t flagSashL{50};
    std::int32_t flagSashR{50};
    std::function<void(bool, std::int32_t, std::int32_t)> storeFlagSash{};
    bool logicalSort{true};
    bool flagInitValue{true};
    std::vector<String> flagTrues{"TRUE"};
    std::vector<String> flagFalses{"FALSE"};
    // 新しく表示する編集画面にだけ適用し、開いている作業コピーは書き換えない。
    std::function<FlagCreationDefaults()> flagDefaultsProvider{};
    String stepValueName{"Step - $N"};
    std::int32_t stepInitValue{0};
    std::size_t stepCountMax{10000};
    std::uint32_t limitForNumberOfVariant{12};
    std::int32_t stepTopSashL{3};
    std::int32_t stepTopSashR{4};
    std::int32_t flagTopSashL{3};
    std::int32_t flagTopSashR{2};
    float valueNumberColumn{50.0F};
    float flagNameColumn{150.0F};
    float flagInitColumn{90.0F};
    float variableInitializationColumn{80.0F};
    float flagCountColumn{60.0F};
    float localFlagNameColumn{150.0F};
    float localFlagInitColumn{90.0F};
    float localVariableInitializationColumn{80.0F};
    float localFlagCountColumn{60.0F};
    float flagNameWidth{150.0F};
    float flagInitWidth{100.0F};
    float flagValueWidth{150.0F};
    float firesWidth{120.0F};
    std::function<void(bool, std::size_t, float)> storeFlagColumn{};
    // EventTreeView/ContentsToolBox
    // の設定。Dはツールボックス作成時にFlexEtcProps
    // から読む。 共通のPropsにも保持し、別
    // の配置モードへ黙って戻らないようにする。
    bool contentsAutoOpen{true};
    std::int32_t contentsPutMode{0};
    bool contentsInsertFirst{false};
    std::vector<std::int32_t> contentsOrder{};
    bool contentsLock{};
    std::vector<std::int32_t> contentsWrapIndices{4, 6, 8};
    bool showContentsGroupName{true};
    bool contentsFloat{};
    bool contentsAutoHide{};
    std::int32_t showContentsBoxHeightWhenNoToolBar{8};
    bool adjustContentName{true};
    bool showEventContentDescription{true};
    bool specifySelectedEventTree{true};
    bool drawContentTreeLine{true};
    bool drawCountOfUseOfStart{true};
    bool showTerminalMark{true};
    bool forceIndentBranchContent{true};
    bool showTargetStartLineNumber{true};
    bool drawContentWarnings{true};
    bool drawContentIconOnDetail{true};
    float eventGraphDetailWidth{200.0F};
    std::function<void(std::int32_t)> storeEventGraphDetailWidth{};
    bool branchRandomFailureFormat{true};
    bool showInputGuide{true};
    bool useCurrentStartName{true};
    String inputGuideText{"○"};
    std::vector<structs::ContentConversionGroup> contentConversionGroups{};
    std::int32_t commentBoxDistance{50};
    std::int32_t warningImageWidth{200};
    lib::Color warningImageColor{255, 128, 128, 255};
    std::int32_t toolTipWidth{300};
    float couponWidth{250.0F};
};

struct LookProperties {
    std::size_t stepMaxCount{10};
};

/// flagspane.dが読むPropsの一
/// 部。具体型にまとめ、 アプリ全体のProps
/// を構成しなくても部品を使えるようにする。
struct Props {
    EtcProperties etc{};
    LookProperties looks{};
    Messages msgs{};
    // Dは現在の親カタログからProps.msgsを読むため、EventViewの発火条件名も最新状態へ追従させる。
    std::function<EventTreeMessages()> eventTreeMessagesProvider{};
    lib::ResourceId flagDirImage{};
    lib::ResourceId flagImage{};
    lib::ResourceId stepImage{};
    lib::ResourceId variantImage{};

    // Editorの値をvar.etc/var.looks
    // 配下にまとめた呼び出し元向けに、Dの表記も残す。
    struct Variables {
        EtcProperties etc{};
        LookProperties looks{};
        lib::Size flagDlg{480.0F, 260.0F};
        lib::Size stepDlg{520.0F, 400.0F};
        lib::Size variantDlg{400.0F, 320.0F};
    } var{};

    Props();
    void synchronize() noexcept;
};

struct SelectionEvent {
    bool handled{false};
};

/// 件数制限付きのコマンド履歴。 ペインの取り込みや、表
/// ・ツリーの破壊的編集にはスナップショット操作を使う。
struct DirectorySnapshot;

class UndoManager final {
public:
    explicit UndoManager(std::size_t maximum = 100);
    // 場面・イベントのペインは、同じDのUndoManagerの履歴位置を共有で
    // きる。 ローカル変数の画面は、上の従来の独立した保存先を作る形式を使う。
    explicit UndoManager(std::shared_ptr<::cwxeditor::cwx::editor::gui::dwt::undo::UndoManager> shared);
    [[nodiscard]] const auto& sharedCore() const noexcept { return shared_core_; }

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    void undo();
    void redo();
    void reset() noexcept;
    void max(std::size_t value);
    [[nodiscard]] std::size_t max() const noexcept {
        return shared_core_ ? shared_core_->max() : maximum_;
    }

    void push(lib::Action undoAction, lib::Action redoAction);
    void beginSnapshot(const FlagDirPtr& root);
    // UseCounterの参照改名など、ツリー以外の変更も、 保
    // 留中のディレクトリスナップショットと同じコマンドへ含める。
    void addSnapshotSideEffect(lib::Action undoAction,
                               lib::Action redoAction);
    void commitSnapshot(const FlagDirPtr& root);
    void addSnapshotTraversal(std::function<void(bool)> before,
                              std::function<void(bool)> after);
    void discardSnapshot() noexcept;

private:
    struct Command {
        lib::Action undo;
        lib::Action redo;
    };

    static FlagDirPtr cloneDirectory(const FlagDirPtr& source,
                                     const UseCounterPtr& counter = {});
    static void restoreDirectory(const FlagDirPtr& target,
                                  const FlagDirPtr& source);

    std::shared_ptr<::cwxeditor::cwx::editor::gui::dwt::undo::UndoManager> shared_core_;
    std::vector<Command> commands_;
    std::size_t cursor_{0};
    std::size_t maximum_{100};
    FlagDirPtr pendingRoot_{};
    std::shared_ptr<DirectorySnapshot> pendingBefore_{};
    lib::Action pendingUndoSideEffect_{};
    lib::Action pendingRedoSideEffect_{};
    std::function<void(bool)> pendingBeforeTraversal_{};
    std::function<void(bool)> pendingAfterTraversal_{};
};

enum class VariableKind { Variant, Step, Flag };

using VariableObject = std::variant<VariantPtr, StepPtr, FlagPtr>;

struct VariableRow {
    VariableKind kind{VariableKind::Flag};
    VariableObject object{};
    String name;
    String value;
    String initialization;
    std::uint32_t useCount{0};
    bool selected{false};
};

class FlagTable final {
public:
    Signal<void()> selectionChanged;
    Signal<void()> createEvent;
    Signal<void()> deleteEvent;

    FlagTable(Commons comm, Props prop, UndoManager& undo, bool local = false,
              bool readOnly = false);
    ~FlagTable();

    [[nodiscard]] PanelPtr createControl(const Parent& parent,
                                         const Parent& incSearchParent = {},
                                         lib::Action gotFocus = {});
    void updateIncSearchParent(const Parent& parent);
    void dialogContext(
        const std::shared_ptr<::cwxeditor::cwx::summary::Summary>& summary,
        const std::shared_ptr<lib::Window>& owner = {});
    [[nodiscard]] PanelPtr widget() const noexcept { return component_; }
    [[nodiscard]] const std::shared_ptr<lib::TableView>& table() const noexcept {
        return table_;
    }

    void refresh();
    void refresh(const std::vector<VariableObject>& selected);
    [[nodiscard]] const std::vector<std::size_t>& selected() const noexcept;
    [[nodiscard]] std::vector<std::size_t> selectedItems(
        FlagList& flags, StepList& steps, VariantList& variants) const;
    [[nodiscard]] FlagList selectionFlags() const;
    [[nodiscard]] StepList selectionSteps() const;
    [[nodiscard]] VariantList selectionVariants() const;
    [[nodiscard]] std::vector<std::ptrdiff_t> selectionFlagIndices() const;
    [[nodiscard]] std::vector<std::ptrdiff_t> selectionStepIndices() const;
    [[nodiscard]] std::vector<std::ptrdiff_t> selectionVariantIndices() const;

    void createFlag();
    void createStep();
    [[nodiscard]] bool canCreateVariant() const;
    void createVariant();
    void edit();
    void edit(const FlagPtr& value);
    void edit(const StepPtr& value);
    void edit(const VariantPtr& value);
    [[nodiscard]] bool canEdit() const noexcept;
    void setDir(const FlagDirPtr& dir, bool forceRefresh = false);
    [[nodiscard]] FlagDirPtr dir() const noexcept { return dir_; }

    void select(const FlagPtr& value, bool deselect = false);
    void select(const StepPtr& value, bool deselect = false);
    void select(const VariantPtr& value, bool deselect = false);
    void deselectAll();

    [[nodiscard]] bool canCopyVariablePath() const noexcept;
    void copyVariablePath();
    // DのFlagTableのポップアップ操作。
    void incSearch();
    void selectAll();
    [[nodiscard]] bool canSelectAll() const noexcept;
    void writeComment();
    [[nodiscard]] bool canWriteComment() const noexcept;
    void copyFlagTree(bool onOff);
    void copyStepTree(std::uint32_t value);
    void copyInitTree();
    void copyFlagReverseTree();
    void copyStepUpTree();
    void copyStepDownTree();
    void dispose() noexcept;
    void useCounter(UseCounterPtr value);
    [[nodiscard]] UseCounterPtr useCounter() const noexcept { return uc_; }

    void cut(SelectionEvent& event);
    void copy(SelectionEvent& event);
    void paste(SelectionEvent& event);
    [[nodiscard]] bool pasteXML(StringView xml);
    void del(SelectionEvent& event);
    void clone(SelectionEvent& event);
    [[nodiscard]] bool canDoTCPD() const noexcept;
    [[nodiscard]] bool canDoT() const noexcept;
    [[nodiscard]] bool canDoC() const noexcept;
    [[nodiscard]] bool canDoP() const noexcept;
    [[nodiscard]] bool canDoD() const noexcept;
    [[nodiscard]] bool canDoClone() const noexcept;

    void undo();
    void redo();
    void cancelEdit() noexcept;
    void enterEdit() noexcept;
    void replaceID();
    [[nodiscard]] bool canReplaceID() const noexcept;
    [[nodiscard]] bool openCWXPath(String path, bool shellActivate);
    [[nodiscard]] std::vector<String> openedCWXPath() const;

    void setSearchText(String value);
    [[nodiscard]] const String& searchText() const noexcept { return search_; }
    [[nodiscard]] const std::vector<VariableRow>& rows() const noexcept {
        return rows_;
    }
    [[nodiscard]] const String& statusLine() const noexcept {
        return statusLine_;
    }
    [[nodiscard]] bool local() const noexcept { return local_; }
    [[nodiscard]] bool readOnly() const noexcept { return readOnly_; }
    [[nodiscard]] const VariableObject& editing() const noexcept {
        return editing_;
    }
    [[nodiscard]] const std::shared_ptr<lib::Dialog>& editorDialog() const
        noexcept {
        // モードレスのプロパティ編集セッションが自分の画面を所有する。 バ
        // ックエンドと同じ取得関数で、最新の有効なセッションを公開する。
        for (auto it = property_sessions_.rbegin(); it != property_sessions_.rend(); ++it) {
            const auto& host = *it;
            if (host && host->edit_shell_ &&
                host->edit_shell_->result == lib::DialogResult::None)
                return host->edit_shell_;
        }
        return edit_shell_;
    }

    // バックエンドがモーダルSWT画面の代わりに使う具体的な編
    // 集補助。 edit()と同じUndo・通知経路を保つ。
    bool renameSelected(String name);
    bool setFlagState(const FlagPtr& value, bool on);
    bool setStepSelection(const StepPtr& value, std::uint32_t selection);
    bool setVariantValue(const VariantPtr& value,
                          const expression::VariantVal& data);
    bool setInitialization(const VariableObject& value,
                           types::VariableInitialization initialization);

    /// 共通ドラッグ元が取得した選択。ツリー側は追加と同じス
    /// ナップショットで旧値を除き、 消費済みにする。操作元
    /// の完了処理で、二つ目のUndoを作らないようにする。
    struct DragSelection {
        FlagDirPtr directory;
        FlagList flags;
        StepList steps;
        VariantList variants;
    };
    [[nodiscard]] const std::optional<DragSelection>& activeDragSelection() const noexcept {
        return active_drag_selection_;
    }
    void consumeActiveDragSelection() noexcept;

private:
    friend class FlagDirTree;
    friend class FlagsPane;
    template <typename Component>
    [[nodiscard]] std::shared_ptr<Component> createComponent(
        lib::ComponentKind kind) const;

    void refreshImpl(const std::vector<VariableObject>* selected);
    void refreshStatusLine();
    void refreshTableSelection(bool notify);
    void refreshUseCount();
    Signal<void()>::Token use_count_connection_{};
    void notifySelection();
    void addRow(std::vector<VariableRow>& rows, VariableKind kind,
                const VariableObject& object,
                String name, String value, String initialization,
                std::uint32_t count);
    [[nodiscard]] bool matchesSearch(const VariableRow& row) const;
    [[nodiscard]] bool selectedObject(const VariableObject& object) const;
    [[nodiscard]] bool selectObject(const VariableObject& object,
                                    bool deselect);
    [[nodiscard]] std::vector<VariableObject> selectedObjects() const;
    void beginMutation();
    void commitMutation();
    std::function<bool(const FlagDirPtr&)> store_history_selection_;
    void writeXMLClipboard(StringView xml);
    void writeClipboard(String text);
    [[nodiscard]] String readClipboard() const;
    [[nodiscard]] bool copySelectionToMemory();
    [[nodiscard]] String selectedXML() const;
    void notifyRefresh();
    void openEditor(const VariableObject& object);
    void closeEditor(lib::DialogResult result) noexcept;
    void closePropertySessions() noexcept;
    void closeCommentDialogs() noexcept;
    void rebuildContextMenu();
    void showContextMenu(const lib::WidgetPtr& owner, lib::Point position);

    Commons comm_;
    Props prop_;
    UndoManager* undo_{};
    bool local_{};
    bool readOnly_{};
    UseCounterPtr uc_{};
    FlagDirPtr dir_{};
    PanelPtr component_{};
    std::shared_ptr<lib::TableView> table_{};
    Parent incSearchParent_{};
    String search_{};
    String statusLine_{};
    std::vector<VariableRow> rows_{};
    VariableObject editing_{};
    VariableObject edit_dialog_object_{};
    bool hasEditing_{false};
    std::shared_ptr<lib::Dialog> edit_shell_{};
    std::shared_ptr<lib::Button> edit_ok_button_{};
    std::shared_ptr<lib::Button> edit_cancel_button_{};
    std::shared_ptr<lib::Button> edit_apply_button_{};
    std::shared_ptr<void> edit_dialog_model_{};
    // 各モードレスプロパティ画面は、取得したディレクトリ・モデル状態を所有
    // する。 子ホストは表を作らず、同じ変更・Undoの経路を再利用する。
    bool property_session_host_{};
    std::vector<std::shared_ptr<FlagTable>> property_sessions_{};
    std::vector<std::size_t> selectedIndices_{};
    std::shared_ptr<lib::Menu> context_menu_{};
    std::shared_ptr<incsearch::IncSearch> incSearch_{};
    // 変数をキーにした、開いているcomment::CommentDialogの一覧。
    std::unordered_map<const void*, std::shared_ptr<void>> commentDialogs_{};
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};
    // 外側のDeleteが終了処理中に、削除通知がこの表を破棄する
    // 場合もある。 その間はDeleteの再入防止状態を保持する。
    std::shared_ptr<bool> deletion_in_progress_{std::make_shared<bool>(false)};
    bool disposed_{false};
    std::optional<DragSelection> active_drag_selection_{};
    bool active_drag_consumed_{};
    String drag_pane_id_{};
};

class FlagDirTree final {
public:
    FlagDirTree(Commons comm, Props prop, FlagTable& flags,
                UndoManager& undo);
    ~FlagDirTree();

    [[nodiscard]] PanelPtr createControl(const Parent& parent,
                                         lib::Action gotFocus = {});
    [[nodiscard]] PanelPtr widget() const noexcept { return component_; }
    void refresh();
    void refresh(String selectedPath);
    void select(String path);
    void useCounter(UseCounterPtr value);
    [[nodiscard]] UseCounterPtr useCounter() const noexcept { return uc_; }
    [[nodiscard]] FlagDirPtr current() const noexcept { return current_; }
    void current(const FlagDirPtr& value);
    [[nodiscard]] FlagDirPtr rootDir() const noexcept { return root_; }
    void rootDir(const FlagDirPtr& value);

    void scenarioContext(const std::shared_ptr<::cwxeditor::cwx::summary::Summary>& summary) {
        comm_.summary = summary;
    }

    void createDir();
    void cut(SelectionEvent& event);
    void copy(SelectionEvent& event);
    void paste(SelectionEvent& event);
    void del(SelectionEvent& event);
    void clone(SelectionEvent& event);
    [[nodiscard]] bool canDoTCPD() const noexcept;
    [[nodiscard]] bool canDoT() const noexcept;
    [[nodiscard]] bool canDoC() const noexcept;
    [[nodiscard]] bool canDoP() const noexcept;
    [[nodiscard]] bool canDoD() const noexcept;
    [[nodiscard]] bool canDoClone() const noexcept;

    void copyFlagTree(bool onOff);
    void copyStepTree(std::uint32_t value);
    void copyInitTree();
    void copyFlagReverseTree();
    void copyStepUpTree();
    void copyStepDownTree();
    [[nodiscard]] bool openCWXPath(String path, bool shellActivate);
    [[nodiscard]] std::vector<String> openedCWXPath() const;
    void undo();
    void redo();
    void cancelEdit() noexcept;
    void enterEdit() noexcept;

    // createControlが登録する、転送方式に依存しないドラッ
    // グ通知。 バックエンドはtarget_indexで位置下のツリー
    // 行を渡し、 このクラスが変更前にFlagDirモデルへ解決する。
    void dragEnter(lib::DragEvent& event);
    void dragOver(lib::DragEvent& event);
    void drop(lib::DragEvent& event);

    [[nodiscard]] const std::shared_ptr<lib::TreeView>& tree() const noexcept {
        return tree_;
    }

private:
    template <typename Component>
    [[nodiscard]] std::shared_ptr<Component> createComponent(
        lib::ComponentKind kind) const;
    void refreshTree();
    void appendNode(std::vector<lib::TreeNode>& nodes,
                    lib::TreeItemId& nextNodeId, const FlagDirPtr& dir,
                    lib::TreeItemId parent, bool rootNode = false);
    [[nodiscard]] FlagDirPtr findNode(lib::TreeItemId id) const;
    [[nodiscard]] bool openCWXPathImpl(const FlagDirPtr& dir, String path,
                                       bool shellActivate);
    void pasteXML(SelectionEvent& event, StringView xml);
    [[nodiscard]] bool copyDirectoryToMemory();
    void writeXMLClipboard(StringView xml);
    void writeClipboard(String text);
    [[nodiscard]] String eventTreeXML(event::CType flagType,
                                      event::CType stepType,
                                      event::CType variantType,
                                      bool flagValue,
                                      std::uint32_t stepValue,
                                      bool useValues) const;
    [[nodiscard]] String readClipboard() const;
    void editTreeItem(lib::TreeItemId id, String value);
    void beginCurrentEdit();
    void notifySnapshotRestored();
    void storeSnapshotSelection(const FlagDirPtr& operationDir,
                                const FlagDirPtr& restoredDir = {},
                                bool restoreOnUndo = false);
    void notifyRefresh();
    void rebuildContextMenu();
    void showContextMenu(const lib::WidgetPtr& owner, lib::Point position);
    [[nodiscard]] FlagDirPtr dropTarget(
        std::optional<std::size_t> targetIndex) const;
    [[nodiscard]] bool applyDrop(const FlagDirPtr& target,
                                 const lib::ClipboardData& data,
                                 lib::DropOperation& operation);
    [[nodiscard]] bool applyDrop(const FlagDirPtr& target, StringView xml,
                                 lib::DropOperation& operation);
    [[nodiscard]] bool isDescendant(const FlagDirPtr& possibleChild,
                                    const FlagDirPtr& possibleParent) const;
    void restoreDroppedSource(const FlagDirPtr& source,
                             const FlagDirPtr& parent,
                             std::ptrdiff_t index);
    void notifyDroppedVariables(
        const std::unordered_map<String, FlagPtr>& flags,
        const std::unordered_map<String, StepPtr>& steps,
        const std::unordered_map<String, VariantPtr>& variants);

    Commons comm_;
    Props prop_;
    FlagTable* flags_{};
    UndoManager* undo_{};
    UseCounterPtr uc_{};
    FlagDirPtr root_{};
    FlagDirPtr current_{};
    FlagDirPtr drag_source_dir_{};
    PanelPtr component_{};
    std::shared_ptr<lib::TreeView> tree_{};
    std::shared_ptr<lib::Menu> context_menu_{};
    std::shared_ptr<int> callback_lifetime_{std::make_shared<int>(0)};
    Parent parent_{};
    lib::TreeItemId nextNodeId_{1};
    std::unordered_map<String, bool> expanded_paths_{};
};

struct TopLevelPanel {
    lib::Action changeVHAction;
};

class FlagsPane final {
public:
    FlagsPane(Commons comm = {}, Props prop = {});
    ~FlagsPane();

    FlagsPane(const FlagsPane&) = delete;
    FlagsPane& operator=(const FlagsPane&) = delete;

    [[nodiscard]] Parent construct(const Parent& parent = {});
    void setupTLP(TopLevelPanel& panel);
    [[nodiscard]] PanelPtr widget() const noexcept { return component_; }
    [[nodiscard]] const String& statusLine() const noexcept;
    void changeVHSide();
    [[nodiscard]] bool canCopyVariablePath() const noexcept;
    void copyVariablePath();
    void setFlagDirTree(const FlagDirPtr& root, UseCounterPtr uc);
    void setUndoLimit(std::size_t value);
    void dialogContext(
        const std::shared_ptr<::cwxeditor::cwx::summary::Summary>& summary,
        const std::shared_ptr<lib::Window>& owner = {});
    [[nodiscard]] bool openCWXPath(String path, bool shellActivate);
    [[nodiscard]] std::vector<String> openedCWXPath() const;
    void edit();
    [[nodiscard]] bool canEdit() const noexcept;
    [[nodiscard]] FlagDirTree& dirs() noexcept { return dirs_; }
    [[nodiscard]] FlagTable& flags() noexcept { return flags_; }
    [[nodiscard]] const FlagDirTree& dirs() const noexcept { return dirs_; }
    [[nodiscard]] const FlagTable& flags() const noexcept { return flags_; }
    void addFlagsAndSteps(
        const lib::Dictionary<String, lib::Array<importutils::ImportFlag>>& flags,
        const lib::Dictionary<String, lib::Array<importutils::ImportStep>>& steps,
        const lib::Dictionary<String, lib::Array<importutils::ImportVariant>>& variants);
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    void undo();
    void redo();
    void replaceID();
    [[nodiscard]] bool canReplaceID() const noexcept;

    [[nodiscard]] bool canDoTCPD() const noexcept;
    [[nodiscard]] bool canDoT() const noexcept;
    [[nodiscard]] bool canDoC() const noexcept;
    [[nodiscard]] bool canDoP() const noexcept;
    [[nodiscard]] bool canDoD() const noexcept;
    [[nodiscard]] bool canDoClone() const noexcept;
    void cut(SelectionEvent& event);
    void copy(SelectionEvent& event);
    void paste(SelectionEvent& event);
    void del(SelectionEvent& event);
    void clone(SelectionEvent& event);

    void onScenarioChanged();
    void onUndoMaximumChanged();
    void setSearchText(String value);
    [[nodiscard]] bool vertical() const noexcept { return vertical_; }
    [[nodiscard]] const Commons& commons() const noexcept { return comm_; }
    [[nodiscard]] const Props& props() const noexcept { return prop_; }
    void debugFocusVariables() noexcept { rememberFocus(false); }

private:
    void rememberFocus(bool directory);

    Commons comm_;
    Props prop_;
    UndoManager undo_;
    FlagTable flags_;
    FlagDirTree dirs_;
    PanelPtr component_{};
    std::shared_ptr<splitpane::SplitPane> sash_{};
    splitpane::SelectionToken sash_selection_token_{};
    std::shared_ptr<int> sash_lifetime_{std::make_shared<int>(0)};
    void storeSash();
    std::weak_ptr<lib::Container> parent_{};
    bool vertical_{false};
    bool constructed_{false};
    bool focusDirectory_{true};
    Signal<void()>::Token scenarioToken_{0};
    Signal<void()>::Token undoMaxToken_{0};
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::flagspane
