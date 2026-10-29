#pragma once

// cwx/editor/gui/dwt/replacedialog.d
// のC++20移植。 多種類のシナリオ構造
// へのアクセスをReplaceSource
// の通知へ隠し、 このクラスは検索・置換状
// 態、表、範囲ツリー、クリップボード、Undo
// を管理する。 ウィンドウツールキットか
// ら独立しつつ、Summary/UseCounter
// 側から全項目を公開できる。

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/coupon.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/incsearch.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/namewindow.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/undo.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/types.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::summary {
class Summary;
[[nodiscard]] lib::Owner<path::CWXPath> cwxPlace(lib::Owner<path::CWXPath> value);
}

namespace cwxeditor::cwx::skin {
class Skin;
}

namespace cwxeditor::cwx::editor::gui::dwt::commons {
class Commons;
}

namespace cwxeditor::cwx::editor::gui::dwt::replacedialog {

namespace lib = ::cwxeditor::lib;
namespace path = ::cwxeditor::cwx::path;
namespace card = ::cwxeditor::cwx::card;
namespace coupon = ::cwxeditor::cwx::coupon;
namespace system = ::cwxeditor::cwx::system;
namespace types = ::cwxeditor::cwx::types;
namespace usecounter = ::cwxeditor::cwx::usecounter;
namespace customtable = ::cwxeditor::cwx::editor::gui::dwt::customtable;
namespace incsearch = ::cwxeditor::cwx::editor::gui::dwt::incsearch;
namespace undo = ::cwxeditor::cwx::editor::gui::dwt::undo;
namespace namewindow = ::cwxeditor::cwx::editor::gui::dwt::namewindow;
namespace commons = ::cwxeditor::cwx::editor::gui::dwt::commons;
namespace summary = ::cwxeditor::cwx::summary;
namespace skin = ::cwxeditor::cwx::skin;

using String = lib::String;
using StringView = lib::StringView;
using Widget = lib::Widget;
using WidgetPtr = lib::WidgetPtr;
using Parent = lib::Container*;
using Shell = lib::Widget*;
using WindowPtr = std::shared_ptr<lib::Window>;
using CWXPath = path::CWXPath;
using CWXPathPtr = lib::Owner<CWXPath>;
using Summary = ::cwxeditor::cwx::summary::Summary;
using SummaryPtr = lib::Owner<Summary>;
using Skin = ::cwxeditor::cwx::skin::Skin;
using ResourceId = lib::ResourceId;
using FKeyCode = system::FKeyCode;
using Coupon = coupon::Coupon;
using CouponPtr = lib::Owner<Coupon>;
using CardImage = card::CardImage;
using CardImagePtr = lib::Owner<CardImage>;
using ClipboardBackend = lib::ClipboardBackend;
using Commons = namewindow::Commons;

using FlagId = usecounter::FlagId;
using StepId = usecounter::StepId;
using VariantId = usecounter::VariantId;
using AreaId = usecounter::AreaId;
using BattleId = usecounter::BattleId;
using PackageId = usecounter::PackageId;
using CastId = usecounter::CastId;
using SkillId = usecounter::SkillId;
using ItemId = usecounter::ItemId;
using BeastId = usecounter::BeastId;
using InfoId = usecounter::InfoId;
using CouponId = usecounter::CouponId;
using GossipId = usecounter::GossipId;
using CompleteStampId = usecounter::CompleteStampId;
using KeyCodeId = usecounter::KeyCodeId;
using CellNameId = usecounter::CellNameId;
using CardGroupId = usecounter::CardGroupId;

/// replacedialog.dと同じ順の検索タブ。
enum class Tab : std::uint8_t {
    Text,
    Id,
    Path,
    Contents,
    Coupon,
    Unused,
    Error,
    Grep,
};

enum class SearchMode : std::uint8_t { Plain, Regex, Wildcard };

/// IDタブが使うIDの分類。数値IDは64ビット全体
/// を保ち、 文字列IDをネイティブ整数へ狭めない。
enum class IDKind : std::uint8_t {
    Area,
    Battle,
    Package,
    Cast,
    Skill,
    Item,
    Beast,
    Info,
    Flag,
    Step,
    Variant,
    Coupon,
    Gossip,
    CompleteStamp,
    KeyCode,
    CellName,
    CardGroup,
};

struct CWXPathString {
    String scPath;
    CWXPathPtr parent;
    CWXPathPtr path;
    String array;

    CWXPathString() = default;
    CWXPathString(String sc_path, CWXPathPtr parent_path, CWXPathPtr value,
                  String array_text = {})
        : scPath(std::move(sc_path)),
          parent(std::move(parent_path)),
          path(std::move(value)),
          array(std::move(array_text)) {}
};

struct FilePathString {
    String scPath;
    String array;

    FilePathString() = default;
    FilePathString(String sc_path, String array_text = {})
        : scPath(std::move(sc_path)), array(std::move(array_text)) {}
};

class FKeyCodesUndo final : public undo::TUndo<std::vector<FKeyCode>> {
public:
    using Setter = undo::TUndo<std::vector<FKeyCode>>::Setter;

    FKeyCodesUndo(std::vector<FKeyCode> old_value,
                  std::vector<FKeyCode> new_value, Setter setter);
};

class CouponsUndo final : public undo::TUndo<std::vector<CouponPtr>> {
public:
    using Setter = undo::TUndo<std::vector<CouponPtr>>::Setter;

    CouponsUndo(std::vector<CouponPtr> old_value,
                std::vector<CouponPtr> new_value, Setter setter);
};

class CardImagesUndo final : public undo::TUndo<std::vector<CardImagePtr>> {
public:
    using Setter = undo::TUndo<std::vector<CardImagePtr>>::Setter;

    CardImagesUndo(std::vector<CardImagePtr> old_value,
                   std::vector<CardImagePtr> new_value, Setter setter);
};

struct ReplaceMessages {
    String dialogTitle{"検索と置換"};
    String search{"検索(&F)"};
    String replace{"全て置換(&R)"};
    String close{"閉じる(&C)"};
    String cancel{"キャンセル(&C)"};
    String searchResultEmpty{"0件の検索結果"};
    String replaceResultEmpty{"0箇所の置換"};
    String searchResult{"%1$s件の検索結果(%2$s)"};
    String searchResultGrep1{"%1$s件のシナリオ内の%2$s件の検索結果(%3$sを読込中...)"};
    String searchResultGrep2{"%1$s件のシナリオ内の%2$s件の検索結果(%3$sを検索中...)"};
    String searchResultGrep3{"%1$s件のシナリオ内の%2$s件の検索結果(%3$s)"};
    String replaceResult{"%1$s箇所の置換(%2$s)"};
    String cautionOfReplace{"%1$sを%2$sに置換します。よろしいですか？"};
    String searchRange{"検索対象"};
    String localVariableUsers{"ローカル変数"};
    String startUsers{"スタート使用箇所"};
    String rangeSummary{"シナリオ情報"};
    String rangeVariables{"状態変数"};
    String allCheckRange{"全てチェック/全てチェックを外す"};
    String allCheck{"全てチェック/全てチェックを外す(&L)"};
    String allSelect{"全て選択/全て選択を外す(&L)"};
    String searchResultRealtime{"リアルタイム更新(&R)"};
    String searchOpenDialog{"検索結果へジャンプする時、ダイアログを開く(&J)"};
    String searchOpenDialogHint{"Shiftキーで一時的に切り替え"};
    String searchResultColumnMain{"マッチ箇所"};
    String searchResultColumnParent{"所属"};
    String searchResultColumnDescription{"解説"};
    String searchResultColumnCoupon{"称号・名称"};
    String searchResultColumnCouponCount{"利用数"};
    String searchResultColumnError{"誤り箇所"};
    String searchResultColumnScenario{"シナリオ"};
    String tabText{"テキスト検索"};
    String tabID{"ID検索"};
    String tabPath{"素材検索"};
    String tabContents{"コンテント検索"};
    String tabCoupon{"称号・名称一覧"};
    String tabUnused{"未使用検索"};
    String tabError{"誤り検索"};
    String tabGrep{"外部シナリオ"};
    String from{"検索(置換前)"};
    String to{"置換後"};
    String parent{"親"};
    String range{"範囲"};

    String textGroup{"検索/置換するテキスト"};
    String textTargetGroup{"検索/置換対象"};
    String conditionGroup{"検索条件"};
    String notIgnoreCase{"大文字と小文字を区別する(&C)"};
    String exact{"完全一致(&X)"};
    String ignoreReturnCode{"改行と前後の空白を無視(&I) (置換はできません)"};
    String wildcard{"ワイルドカード(&W) (* = 任意文字列, ? = 任意1文字, \\* = *, \\? = ?, \\\\ = \\)"};
    String regex{"正規表現(&E) (. = 任意1文字, * = 直前の文字の任意数繰返し, $1 = 1つめの文字列グループ ...)"};
    String wildcardDescription{"ワイルドカードが使用できます(* = 任意文字列, ? = 任意1文字, \\* = *, \\? = ?, \\\\ = \\)"};
    String incrementalSearch{"絞り込み検索"};

    String idGroup{"検索/置換対象"};
    String idKind{"対象"};
    String directID{"IDを直接指定"};
    std::array<String, 17> idKinds{
        "エリア", "バトル", "パッケージ", "キャストカード",
        "特殊技能カード", "アイテムカード", "召喚獣カード", "情報カード",
        "フラグ", "ステップ", "コモン", "クーポン", "ゴシップ",
        "終了印", "キーコード", "セル名称", "カードグループ"};

    String pathGroup{"検索/置換する素材"};
    String grepTextGroup{"検索するテキスト"};
    String grepFrom{"検索"};
    String grepTargetGroup{"検索対象"};
    String grepDirectoryTitle{"外部シナリオ検索"};
    String grepDirectoryDescription{"検索対象のシナリオが含まれるフォルダを選択してください。"};
    String grepBrowse{"..."};
    String grepOpen{"開く"};
    String grepCurrent{"現フォルダ"};
    String grepSubDirectory{"サブフォルダも検索する"};

    String errorDescription{"重複する分岐(フラグ分岐が両方ともTRUEになっている等)・条件クーポンに抜けがあるセリフコンテント・存在しない素材を参照しているコンテント・基準とするバージョンのCardWirthに存在しない機能の使用等を検出します。"};
    String errorConditionGroup{"検出条件"};
    String errorTargetVersion{"基準とするバージョン"};
    String errorTargetVersionNotSet{"[自動選択]"};
    String errorTargetVersionHint{"※ 自動選択では、シナリオのデータバージョン及び対象エンジン設定が採用されます"};
    String unusedHint{"※ 式で動的に生成された名前で状態変数を参照している場合は、使用を検出できません"};

    std::array<String, 22> textTargets{
        "貼紙(&1)", "シナリオ名(&2)", "作者名(&3)", "メッセージ(&4)",
        "カード名(&5)", "カード解説(&6)", "イベントテキスト(&7)",
        "スタートコンテント(&8)", "状態変数名(&9)", "変数の値(&A)",
        "式(&B)", "クーポン(&D)", "ゴシップ(&E)", "終了印(&G)",
        "エリア/バトル/パッケージ名(&H)", "キーコード(&I)",
        "セル名称(&K)", "カードグループ(&L)", "ファイル名(&M)",
        "コメント(&N)", "JPTX/テキストセル(&O)", "テキストファイル(&P)"};
    std::array<String, 6> nameTargets{
        "クーポン(&1)", "ゴシップ(&2)", "終了印(&3)",
        "キーコード(&4)", "セル名称(&5)", "カードグループ(&6)"};
    std::array<String, 13> unusedTargets{
        "フラグ(&1)", "ステップ(&2)", "コモン(&3)", "エリア(&4)",
        "バトル(&5)", "パッケージ(&6)", "キャストカード(&7)",
        "特殊技能カード(&8)", "アイテムカード(&9)", "召喚獣カード(&A)",
        "情報カード(&B)", "スタートコンテント(&C)", "素材(&D)"};
};

struct ReplaceOptions {
    static constexpr std::size_t contentTypeCount =
        static_cast<std::size_t>(types::CType::CustomXmlContent) + 1U;

    bool notIgnoreCase{};
    bool exact{};
    bool ignoreReturnCode{};
    bool regex{};
    bool wildcard{};
    bool cautionBeforeReplace{};

    bool summary{true};
    bool scenario{};
    bool author{};
    bool message{true};
    bool cardName{true};
    bool cardDescription{true};
    bool eventText{true};
    bool start{true};
    bool variableName{true};
    bool variableValue{true};
    bool expression{true};
    bool coupon{true};
    bool gossip{true};
    bool endScenario{true};
    bool areaName{true};
    bool keyCode{true};
    bool cellName{true};
    bool cardGroup{true};
    bool file{};
    bool comment{true};
    bool jptx{};
    bool textFile{};

    std::array<bool, contentTypeCount> contents{};

    bool replaceNameCoupon{true};
    bool replaceNameGossip{true};
    bool replaceNameEndScenario{true};
    bool replaceNameKeyCode{true};
    bool replaceNameCellName{true};
    bool replaceNameCardGroup{true};

    bool unusedFlag{true};
    bool unusedStep{true};
    bool unusedVariant{true};
    bool unusedArea{true};
    bool unusedBattle{true};
    bool unusedPackage{true};
    bool unusedCast{true};
    bool unusedSkill{true};
    bool unusedItem{true};
    bool unusedBeast{true};
    bool unusedInfo{true};
    bool unusedStart{true};
    bool unusedPath{true};

    bool grepSubDirectory{true};
    bool searchResultRealtime{};
    bool searchOpenDialog{};
    bool flagDirectoryOnRange{};
    bool showAreaDirectoryTree{};
    bool showRouteOfSearchResult{};
    bool logicalSort{true};

    std::size_t searchPlan{};
    std::size_t searchIDKind{};
    float replaceRangeSashLeft{3.0F};
    float replaceRangeSashRight{1.0F};
    std::size_t searchResultRefreshCount{100U};
    std::size_t searchHistoryMax{50U};
    std::size_t undoMaxReplace{100U};
    std::vector<String> searchHistories;
    std::vector<String> replaceHistories;
    std::vector<String> grepDirectoryHistories;
    String grepDirectory;
    String searchErrorTargetVersion;
};

struct ReplaceGeometry {
    lib::Rect bounds{};
    bool maximized{};

    [[nodiscard]] bool valid() const noexcept {
        return bounds.width > 0.0F && bounds.height > 0.0F;
    }
};

enum class ExternalOpenKind { ScenarioCWXPath, File, JPy1, JPDC };

/// 外部のシナリオ・文書に属する検索結果の識別情報
/// 。 シナリオ、相対ファイル、CWXPathを分
/// け、JPy1/JPDCや別シナリオ内の対象を開
/// くときに 表示文字列から逆算しないようにする。
struct ExternalOpenTarget {
    ExternalOpenKind kind{ExternalOpenKind::ScenarioCWXPath};
    String scenarioPath;
    String relativePath;
    String cwxPath;
    std::size_t line{};
};

/// シナリオのアダプターが公開する項目。read/writeは任意
/// で、読み取り専用でも検索へ参加できる。 D形式の検索表示は外部
/// Summaryが有効な間に不変の値として取得し、 開くための型
/// 付き識別情報はExternalOpenTargetに分離する。
struct ResultPresentation {
    String main;
    String parent;
    String description;
    ResourceId image{};
    ResourceId parentImage{};
    ResourceId descriptionImage{};
};

struct TextTarget {
    String path;
    String parentPath;
    String kind;
    String description;
    String text;
    std::size_t resultCount{1U};
    ResourceId image{};
    std::optional<types::CType> contentType;
    bool enabled{true};
    bool replaceable{true};
    CWXPathPtr owner;
    // クーポンタブの一行は、複数のUserが共有する一つの名前
    // を表す。 最初の所有者だけで代替せず全所有者を保持し、D
    // と同じく範囲ツリーで選んだUserだけを集計・改名する。
    std::vector<CWXPathPtr> rangeOwners;
    // 表示上のグループ分けは、出現件数やUndoと独立
    // させる。 空なら提供元の対象ごとの行表示を保つ。
    String resultGroup;
    bool wholeScenarioListing{};
    bool variablePath{};
    bool filePath{};
    // 構造配列は一つの不変のスナップショットとして置き換える。
    // 同じ配列を走査中に、添字付きの設定関数で要素を削除しない。
    std::function<std::vector<String>()> readParts;
    std::function<bool(const std::vector<String>&)> writeParts;
    // 変数定義を先に変え、列挙後に正確な参照スナップショ
    // ットを移す。 Dの書き込みを遅らせる区切りを保つ。
    std::function<undo::UndoArray()> finishWrite;
    std::optional<IDKind> nameIdKind;
    std::function<String()> read;
    std::function<bool(String)> write;
    /// 外部検索の結果はCWXパスではなく、ホスト側で開
    /// く。 TextTargetとしても扱い、一括の提
    /// 供元で通常の結果公開・絞り込み経路を共用する。
    std::optional<ExternalOpenTarget> externalOpenTarget;

    std::optional<ResultPresentation> presentation;

    [[nodiscard]] String value() const { return read ? read() : text; }
    [[nodiscard]] bool assign(String value_to_write) {
        if (write) return write(std::move(value_to_write));
        if (!replaceable) return false;
        text = std::move(value_to_write);
        return true;
    }
};

/// 中止可能な一覧提供元が返す結果。戻る前に複
/// 数回に分けて通知でき、 中止した場合も、そ
/// れまでの有用な部分結果は公開済みでよい。
struct ReplaceProviderResult {
    bool completed{true};
    bool cancelled{};
    std::size_t scanned{};
};

enum class GrepPhase { Loading, Loaded, Finished };
struct GrepProgress {
    GrepPhase phase{GrepPhase::Finished};
    std::size_t successfulScenarios{};
    String path;
    bool refresh{};
};

struct IdTarget;

/// 不変の検索状態と、ホストがモデルを走査中に使える二つ
/// の安全なコールバック。 checkpointは共通UI
/// を処理して中止を確認し、publishBatchは表
/// 示済みの行を無効にせず 確定した結果群を追加する。
struct ReplaceQueryContext {
    SummaryPtr summary;
    /// 現在の検索式。一括の提供元も内蔵のText/Path
    /// 走査と同じ値を使う。 提供元側に別のコピ
    /// ーを持つと、古い式で外部検索してしまう。
    String query;
    ReplaceOptions options;
    std::vector<CWXPathPtr> selectedRange;
    String errorVersion;
    String grepDirectory;
    bool grepSubDirectory{};
    /// Dのテキストファイル指定で、シナリオ走査に使う拡張子。保存先
    /// はPropsだが、 提供元がOS固有のウィンドウ・コントロ
    /// ーラーを参照せずに済むよう、検索スナップショットへ含める。
    std::vector<String> plainTextFileExtensions;
    std::function<bool()> checkpoint;
    std::function<bool(const GrepProgress&)> grepProgress;
    std::function<bool(std::vector<TextTarget>)> publishBatch;
    /// ID対象にも同じ一括反映の通知経路を用意する。
    /// IDを表示文字列へ変えて型付きの設定・Undo
    /// を失わずに、大きな走査を中止可能にする。
    std::function<bool(std::vector<IdTarget>)> publishIdBatch;
};

struct IdTarget {
    IDKind kind{IDKind::Area};
    String value;
    String path;
    String parentPath;
    String description;
    ResourceId image{};
    bool enabled{true};
    bool replaceable{true};
    CWXPathPtr owner;
    std::function<String()> read;
    std::function<bool(String)> write;

    [[nodiscard]] String currentValue() const { return read ? read() : value; }
    [[nodiscard]] bool assign(String value_to_write) {
        if (write) return write(std::move(value_to_write));
        if (!replaceable) return false;
        value = std::move(value_to_write);
        return true;
    }
};

struct RangeNode {
    CWXPathPtr path;
    String label;
    String cwxPath;
    ResourceId image{};
    bool checked{true};
    bool expanded{true};
    std::vector<std::shared_ptr<RangeNode>> children;
    String matchName;
    bool alwaysVisible{};
};

/// replacedialog.dの構造走査に対応する提供元。
/// 空配列も有効な結果とし、 通知が指定されていない場合だけ
/// Summaryの代替を使う。 現在その代替があるのはText
/// ・Path・Contents・ID・Couponのみ。Unused
/// ・Error・Grepは ホストから渡す必要があり
/// 、不在時は誤解を招く空結果でなく検索不可にする。
struct ReplaceSource {
    using TextProvider = std::function<std::vector<TextTarget>()>;
    using IdProvider = std::function<std::vector<IdTarget>(IDKind)>;
    using RangeProvider = std::function<std::vector<std::shared_ptr<RangeNode>>()>;
    /// 選択ディレクトリと再帰検索フラグを受け取る。
    using GrepProvider = std::function<std::vector<TextTarget>(StringView, bool)>;
    using BatchProvider = std::function<ReplaceProviderResult(ReplaceQueryContext&)>;

    TextProvider textTargets;
    TextProvider pathTargets;
    TextProvider contentsTargets;
    TextProvider couponTargets;
    TextProvider unusedTargets;
    TextProvider errorTargets;
    GrepProvider grepTargets;
    /// 段階的な提供元があれば従来の提供元より優先する。移
    /// 行前の呼び出し元には配列を返す版を残す。 Text/Path/ID
    /// でも使えるが、従来の配列版と異なり、 列挙
    /// 中にcontext.checkpointを
    /// 確認し、確定した結果群だけを通知すること。
    BatchProvider textBatchTargets;
    BatchProvider pathBatchTargets;
    BatchProvider idBatchTargets;
    BatchProvider unusedBatchTargets;
    BatchProvider errorBatchTargets;
    BatchProvider grepBatchTargets;
    IdProvider idTargets;
    RangeProvider rangeTree;

    /// ホストのモデル構造が変わった後、次の検索前に呼ぶ。
    lib::Action refreshTargets;
    /// CWXパスへ編集ビューを開く。boolはホ
    /// ストが使う別のオープンモードを指定する。
    std::function<bool(StringView, bool)> openCWXPath;
    /// DのgetSymbols()相当。結果オブジェクトの表示文字
    /// 列とアイコンを返す。 descは一致列で使う説明形式を選ぶ。
    std::function<bool(const CWXPath&, bool desc, String& text, ResourceId& image)> symbols;
    /// ExternalOpenTargetを開く。外部ファイルとシナ
    /// リオのモデルパスを混同しないよう、 CWXPathのコールバッ
    /// クとは分ける。 boolはDのopenDialogモード（searchOpenDialog
    /// != Shift）に対応する。
    std::function<bool(const ExternalOpenTarget&, bool openDialog)> openExternal;
};

struct ReplaceProperties;
using ReplacePropertiesStore =
    std::function<void(const ReplaceProperties&)>;
using ErrorVersionProvider =
    std::function<std::vector<lib::ChoiceItem>(const Summary*)>;

struct ReplaceProperties {
    /// モードレスの置換画面向けの任意の通知元。nullなら
    /// 単独の取得専用APIを維持する。 指定するホストは
    /// 、通知元より先にダイアログを終了させる必要がある。
    commons::Commons* editor_commons{};
    std::function<std::size_t()> textUndoLimit;
    ReplaceMessages messages;
    ReplaceOptions options;
    ReplaceSource source;
    ResourceId dialogImage{};
    std::array<ResourceId, ReplaceOptions::contentTypeCount> contentImages{};
    std::vector<lib::ChoiceItem> errorVersions;
    ErrorVersionProvider errorVersionProvider;
    /// JPTX・通常ファイルの一括走査で使う保存済みの
    /// 拡張子。 チェック欄や画面ごとの可変状態ではない
    /// ため、ReplaceOptionsの外へ置く。
    std::vector<String> plainTextFileExtensions{ ".txt", ".ini", ".sli" };
    ReplaceGeometry geometry;
    lib::Size defaultSize{};
    /// ALLはこのモードレス画面をアプリの表示領域内へ置く。
    /// 広いデスクトップ用の配置を復元せず、表示領域に収める。
    bool webAllScreenFit{};
    // DのsearchResultTable*/searchResultColumn*
    // 設定。 変更可能な検索条件とは分け、結果列のサイズ
    // 変更時に保存するダイアログの配置情報として扱う。
    lib::Size resultTableSize{400.0F, 200.0F};
    float resultColumnMain{400.0F};
    float resultColumnParent{200.0F};
    float resultColumnCouponCount{60.0F};
    float resultColumnErrorDescription{500.0F};
    float resultColumnScenario{500.0F};
    ReplacePropertiesStore storeProperties;
    std::function<ResourceId(const CWXPath*)> rangeImage;
    ResourceId rangeDirectoryImage{};
    /// 外部検索の「フォルダーを開く」ボタンの画像。DのMenuID.OpenDirに対応する。
    ResourceId openDirectoryImage{};
};

using Props = ReplaceProperties;

// EventViewと結果メニューの検索で共用する、Startの利用元の列挙。
[[nodiscard]] std::vector<CWXPathPtr> startUserPaths(const lib::Owner<event::Content>& start);

struct ResultRow {
    CWXPathPtr referenceObject;
    String main;
    String parent;
    String description;
    String before;
    String after;
    String cwxPath;
    std::size_t count{};
    ResourceId image{};
    ResourceId parentImage{};
    ResourceId descriptionImage{};
    bool idResult{};
    std::shared_ptr<TextTarget> textTarget;
    std::shared_ptr<IdTarget> idTarget;
    std::optional<ExternalOpenTarget> externalOpenTarget;
};

struct CouponParams {
    ResourceId image{};
    String name;
    std::size_t count{};
};

class ReplaceDialog;

/// 一回のダイアログ操作で行った置換をまとめる。
/// DのUndoReplと同じく、Undo/Redo
/// はまとめた操作の後で参照の通知を呼ぶ。
class UndoRepl final : public undo::Undo {
public:
    UndoRepl(undo::UndoArray operations, bool reverse,
             lib::Action ref_call = {}, ReplaceDialog* owner = nullptr,
             bool id_changed = false,
             std::optional<namewindow::NameIDKind> name_id_kind = std::nullopt);
    void undo() override;
    void redo() override;
    void dispose() override;

private:
    undo::UndoArray operations_;
    bool reverse_{true};
    lib::Action ref_call_;
    ReplaceDialog* owner_{};
    bool id_changed_{};
    std::optional<namewindow::NameIDKind> name_id_kind_{};
};

/// コピー元のパス情報を持つ置換群。 ホストがUndo
/// の説明を表示するため、メタデータを保持する。
class RUndo final : public undo::Undo {
public:
    RUndo(CWXPathPtr parent, CWXPathPtr path, String cwx_path,
          undo::UndoArray operations);
    RUndo(String file_path, undo::UndoArray operations);

    void undo() override;
    void redo() override;
    void dispose() override;

    [[nodiscard]] const String& cwxPath() const noexcept { return cwx_path_; }
    [[nodiscard]] const String& filePath() const noexcept { return file_path_; }
    [[nodiscard]] const CWXPathPtr& parent() const noexcept { return parent_; }
    [[nodiscard]] const CWXPathPtr& path() const noexcept { return path_; }

private:
    CWXPathPtr parent_;
    CWXPathPtr path_;
    String cwx_path_;
    String file_path_;
    undo::UndoArray operations_;
};

template <typename T>
class SortableCWXPath {
public:
    CWXPathPtr parent;
    T obj{};
    String cwxPath;
    std::vector<String> path;

    SortableCWXPath() = default;
    explicit SortableCWXPath(T value) : obj(std::move(value)) {
        initialiseFromObject();
    }
    SortableCWXPath(CWXPathPtr parent_path, T value, String cwx_path)
        : parent(std::move(parent_path)),
          obj(std::move(value)),
          cwxPath(std::move(cwx_path)) {}

    static void sortedPaths(
        std::span<const T> values,
        const std::function<void(const SortableCWXPath<T>&)>& yield) {
        std::vector<SortableCWXPath<T>> sorted;
        sorted.reserve(values.size());
        for (const auto& value : values) sorted.emplace_back(value);
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& left, const auto& right) {
                      return path::cpcmp(left.path, right.path) < 0;
                  });
        for (const auto& value : sorted) {
            if (yield) yield(value);
        }
    }

private:
    void initialiseFromObject() {
        if constexpr (requires(const T& value) { value->cwxPath(true); }) {
            if (obj) {
                cwxPath = obj->cwxPath(true);
                path = ::cwxeditor::cwx::path::cpsplit(cwxPath);
                if constexpr (requires { obj->ownerHandle(); })
                    parent = ::cwxeditor::cwx::summary::cwxPlace(obj->ownerHandle());
                else if constexpr (std::is_convertible_v<T, CWXPathPtr>)
                    parent = ::cwxeditor::cwx::summary::cwxPlace(obj);
                else
                    // 所有しないアダプターには、安全な自身の所有ハンド
                    // ルがない。 無理に作らず、従来の代替処理を使う。
                    parent = obj->cwxParent().lock();
            }
        } else if constexpr (requires(const T& value) { value.cwxPath(true); }) {
            cwxPath = obj.cwxPath(true);
            path = ::cwxeditor::cwx::path::cpsplit(cwxPath);
        }
    }
};

class ReplaceDialog {
public:
    friend class UndoRepl;

    using Table = customtable::TableModel;
    using TablePtr = customtable::TableModelPtr;
    using RangeMap = std::unordered_map<const CWXPath*, bool>;
    using ReplaceAction = lib::Action;

    ReplaceDialog(Commons comm, ReplaceProperties properties = {},
                  Shell parent_shell = nullptr, SummaryPtr summ = {},
                  ReplaceAction send_reload_props = {});
    ~ReplaceDialog();

    ReplaceDialog(const ReplaceDialog&) = delete;
    ReplaceDialog& operator=(const ReplaceDialog&) = delete;

    [[nodiscard]] Shell widget() const noexcept { return window_.get(); }
    [[nodiscard]] const WindowPtr& window() const noexcept { return window_; }
    [[nodiscard]] Parent parent() const noexcept { return parent_; }

    void summary(SummaryPtr value);
    void setSummary(SummaryPtr value) { summary(std::move(value)); }
    [[nodiscard]] SummaryPtr summary() const noexcept { return summary_; }
    void open();
    void close();

    void replaceText(String from, bool start = false);
    void replacePath(String from, bool start = false);
    void replaceID(String from, IDKind kind, bool start = false);

    template <typename ID>
    void replaceID(const ID& from, bool start = false) {
        replaceID(idToString(from), idKind<ID>(), start);
    }

    void setFindResult(CWXPathPtr parent, std::span<const CWXPathPtr> paths,
                       StringView kind);
    void setFindResult(CWXPathPtr parent, const std::vector<CWXPathPtr>& paths,
                       StringView kind) {
        setFindResult(std::move(parent), std::span<const CWXPathPtr>(paths), kind);
    }

    void setSource(ReplaceSource source);
    [[nodiscard]] const ReplaceSource& source() const noexcept { return source_; }
    [[nodiscard]] const ReplaceProperties& properties() const noexcept {
        return properties_;
    }
    [[nodiscard]] ReplaceProperties& properties() noexcept { return properties_; }

    void setTab(Tab value);
    [[nodiscard]] Tab currentTab() const noexcept { return tab_; }
    [[nodiscard]] std::size_t currentTabIndex() const noexcept {
        return static_cast<std::size_t>(tab_);
    }

    void setFromText(String value);
    void setToText(String value);
    void setFromPath(String value);
    void setToPath(String value);
    [[nodiscard]] const String& fromText() const noexcept { return from_text_; }
    [[nodiscard]] const String& toText() const noexcept { return to_text_; }
    [[nodiscard]] const String& fromPath() const noexcept { return from_path_; }
    [[nodiscard]] const String& toPath() const noexcept { return to_path_; }

    void setOptions(ReplaceOptions value);
    void loadProperties(ReplaceProperties value);
    void storeProperties();
    [[nodiscard]] const ReplaceOptions& options() const noexcept {
        return properties_.options;
    }

    [[nodiscard]] bool canFind() const;
    [[nodiscard]] bool canReplace() const;
    [[nodiscard]] bool canCancel() const noexcept { return in_proc_; }
    [[nodiscard]] bool canUndo() const noexcept { return undo_.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return undo_.canRedo(); }
    [[nodiscard]] bool isOpen() const noexcept { return window_ && window_->visible(); }

    void search();
    void replace();
    void cancel() noexcept { cancel_ = true; }
    void undo();
    void redo();
    void changed();
    void reset(bool remove_columns = false);
    bool ignoreMod{false};
    [[nodiscard]] bool catchMod() const noexcept { return !ignoreMod; }

    [[nodiscard]] const std::vector<ResultRow>& results() const noexcept {
        return results_;
    }
    [[nodiscard]] std::vector<std::size_t> selectedResultIndices() const;
    [[nodiscard]] const String& statusText() const noexcept { return status_text_; }
    [[nodiscard]] const TablePtr& resultTable() const noexcept { return result_table_; }
    [[nodiscard]] const std::shared_ptr<lib::TreeView>& rangeView() const noexcept {
        return range_view_;
    }
    [[nodiscard]] const RangeMap& rangeTable() const noexcept { return range_table_; }
    void setRangeChecked(CWXPathPtr path, bool checked);
    [[nodiscard]] std::vector<CWXPathPtr> searchRange() const;
    void refreshRangeTree(bool clear_checked = false);
    void refreshRangeTree2() { refreshRangeTree(false); }
    void refreshRangeAllCheck();
    void setVisibleRangeChecked(bool checked);
    void setRangeSearchText(String text);
    void incSearch();
    [[nodiscard]] bool rangeSearchOpen() const noexcept {
        return inc_search_ && inc_search_->isOpen();
    }
    void resultRedraw(bool value);

    [[nodiscard]] static std::vector<CWXPathPtr> rangeTree(SummaryPtr summ);
    [[nodiscard]] static bool enumerateRangeTree(
        SummaryPtr summ, const std::function<bool(CWXPathPtr)>& emit);
    [[nodiscard]] static bool dec(const CWXPathPtr& value,
                                  const RangeMap& range);

    void searchAll(CWXPathPtr parent, CWXPathPtr value, std::size_t& count,
                   const std::function<void(CWXPathPtr, CWXPathPtr,
                                            std::size_t&, StringView)>& callback,
                   String cwx_path = {}) const;

    [[nodiscard]] std::pair<String, bool> initText(String from, String to);
    void exitText();
    [[nodiscard]] String fTextRepl(StringView value) const;
    [[nodiscard]] std::size_t fTextCount(StringView value) const;

    [[nodiscard]] bool cautionReplace(StringView name1, StringView name2) const;
    [[nodiscard]] bool openPath(bool shift = false);
    [[nodiscard]] bool canOpenPath() const;
    [[nodiscard]] ResourceId fimage(StringView file, const Skin* skin = nullptr) const;
    /// DのaddResultImpl/getPathParamsと同じ、CWXPath結果の表示。
    void applyResultSymbols(ResultRow& row) const;
    void copyResult();

    [[nodiscard]] undo::UndoOwner store(String file_path, undo::UndoArray operations);
    [[nodiscard]] undo::UndoOwner store(CWXPathPtr parent, CWXPathPtr path,
                                        String cwx_path,
                                        undo::UndoArray operations);

    [[nodiscard]] bool repl(CWXPathPtr parent, CWXPathPtr path, String cwx_path,
                            String text, std::function<void(String)> set,
                            std::size_t& count, undo::UndoArray& operations,
                            bool store_to_array = true,
                            bool variable_path = false);
    [[nodiscard]] bool replFilePath(String text, std::function<void(String)> set,
                                    std::size_t& count,
                                    undo::UndoArray& operations);
    [[nodiscard]] bool replImagePaths(
        std::vector<CardImagePtr> images,
        std::function<void(std::vector<CardImagePtr>)> set,
        std::size_t& count, undo::UndoArray& operations);
    [[nodiscard]] bool replFontsInText(CWXPathPtr parent, TextTarget target,
                                       std::size_t& count,
                                       undo::UndoArray& operations);
    [[nodiscard]] bool replFlagsInText(CWXPathPtr parent, TextTarget target,
                                       std::size_t& count,
                                       undo::UndoArray& operations);
    [[nodiscard]] bool replContent(CWXPathPtr parent, TextTarget target,
                                   String cwx_path, std::size_t& count);

    [[nodiscard]] bool replaceTextTarget(TextTarget target, std::size_t& count,
                                         undo::UndoArray& operations);
    [[nodiscard]] bool replaceIdTarget(IdTarget target, StringView from,
                                       StringView to, std::size_t& count,
                                       undo::UndoArray& operations);
    [[nodiscard]] String getIDName(IDKind kind) const;
    [[nodiscard]] std::optional<std::uint64_t> getIDValue(StringView value) const;
    [[nodiscard]] bool idKindIsString(IDKind kind) const noexcept;
    void setupIDs(bool clear, bool from, bool to);
    void tabChanged();
    void updateFromHistory();
    void updateToHistory();

    /// namewindowや軽量ホストが使いやすい、アダプター向けの置換処理。
    static bool renameCoupon(StringView old_value, StringView new_value,
                             std::vector<String>& names,
                             const std::function<void()>& notify = {});

private:
    class ValueUndo;

    std::vector<std::function<undo::UndoArray()>> deferred_text_writes_;
    Commons comm_;
    ReplaceProperties properties_;
    std::shared_ptr<std::atomic_bool> lifetime_alive_{
        std::make_shared<std::atomic_bool>(true)};
    ReplaceSource source_;
    SummaryPtr summary_;
    ReplaceAction send_reload_props_;
    Parent parent_{};
    WindowPtr window_;
    WindowPtr owner_window_;
    std::shared_ptr<lib::Panel> root_;
    std::shared_ptr<lib::Panel> left_panel_;
    std::shared_ptr<lib::SplitView> split_;
    std::shared_ptr<lib::TabView> tabs_;
    std::shared_ptr<lib::Panel> text_page_;
    std::shared_ptr<lib::Panel> grep_page_;
    std::shared_ptr<lib::Panel> text_from_host_;
    std::shared_ptr<lib::Panel> grep_from_host_;
    std::shared_ptr<lib::GroupBox> text_conditions_group_;
    std::shared_ptr<lib::GroupBox> text_targets_group_;
    std::shared_ptr<lib::ComboBox> from_edit_;
    std::shared_ptr<lib::ComboBox> to_edit_;
    std::shared_ptr<lib::ComboBox> from_path_edit_;
    std::shared_ptr<lib::ComboBox> to_path_edit_;
    std::shared_ptr<lib::ComboBox> grep_directory_edit_;
    std::shared_ptr<lib::Button> grep_open_button_;
    lib::Action grep_open_enabled_refresh_;
    std::shared_ptr<lib::CheckBox> grep_subdirectory_check_;
    std::shared_ptr<lib::ComboBox> id_kind_edit_;
    std::shared_ptr<lib::ComboBox> from_id_edit_;
    std::shared_ptr<lib::ComboBox> to_id_edit_;
    std::shared_ptr<lib::NumberEdit> from_id_value_edit_;
    std::shared_ptr<lib::NumberEdit> to_id_value_edit_;
    std::shared_ptr<lib::ComboBox> error_version_edit_;
    std::shared_ptr<lib::ToolBar> contents_toolbar_;
    std::shared_ptr<lib::CheckBox> contents_all_check_;
    std::shared_ptr<lib::CheckBox> ignore_return_check_;
    std::shared_ptr<lib::CheckBox> regex_check_;
    std::shared_ptr<lib::CheckBox> wildcard_check_;
    std::shared_ptr<lib::CheckBox> realtime_check_;
    std::shared_ptr<lib::CheckBox> open_dialog_check_;
    std::shared_ptr<lib::Label> status_label_;
    std::shared_ptr<lib::TreeView> range_view_;
    std::shared_ptr<lib::CheckBox> range_all_check_;
    std::shared_ptr<lib::Button> find_button_;
    std::shared_ptr<lib::Button> replace_button_;
    std::shared_ptr<lib::Button> close_button_;
    std::shared_ptr<lib::Button> cancel_button_;
    std::shared_ptr<lib::Menu> menu_;
    std::shared_ptr<lib::Menu> range_menu_;
    std::vector<std::shared_ptr<lib::Menu>> input_menus_;
    std::vector<std::shared_ptr<void>> input_text_bindings_;
    std::vector<std::unique_ptr<incsearch::IncSearch>> input_searches_;
    // 絞り込みの述語はIncSearchが所有する。 これらは各履歴
    // の再構成に使う、input_searches_内への借用参照。
    incsearch::IncSearch* from_history_search_{};
    incsearch::IncSearch* to_history_search_{};
    incsearch::IncSearch* from_path_search_{};
    incsearch::IncSearch* to_path_search_{};
    incsearch::IncSearch* from_id_search_{};
    incsearch::IncSearch* to_id_search_{};
    std::vector<std::pair<std::shared_ptr<lib::CheckBox>, bool*>> option_controls_;
    struct CheckGroup {
        std::vector<std::pair<std::shared_ptr<lib::CheckBox>, bool*>> members;
        std::shared_ptr<lib::Separator> separator;
        std::shared_ptr<lib::CheckBox> all;
    };
    std::vector<CheckGroup> check_groups_;
    TablePtr result_table_;
    std::unique_ptr<incsearch::IncSearch> inc_search_;

    Tab tab_{Tab::Text};
    IDKind id_kind_{IDKind::Area};
    String from_text_;
    String to_text_;
    String from_path_;
    String to_path_;
    String from_id_;
    String to_id_;
    String status_text_;
    std::optional<GrepProgress> grep_progress_;
    std::uint64_t operation_generation_{};
    std::vector<ResultRow> results_;
    RangeMap range_table_;
    std::unordered_map<lib::TreeItemId, CWXPathPtr> range_paths_;
    undo::UndoManager undo_;
    bool in_proc_{};
    bool in_undo_{};
    bool publishing_changes_{};
    bool cancel_{};
    bool replace_mode_{};
    bool external_find_results_{};
    Tab result_tab_{Tab::Text};
    bool result_redraw_{true};
    bool text_initialised_{};
    bool caution_confirmed_{};
    commons::Commons* notification_commons_{};
    std::vector<lib::Action> live_scope_disconnects_;
    bool scope_refresh_pending_{};
    bool live_settings_pending_{};
    void requestScopeRefresh();
    void applyLiveSettings();
    std::uint64_t repl_path_connection_{};
    std::uint64_t ref_path_connection_{};
    std::uint64_t ref_paths_connection_{};
    std::uint64_t del_paths_connection_{};
    bool own_model_write_{};
    std::uint64_t changed_connection_{};
    std::uint64_t ref_scenario_connection_{};
    std::uint64_t ref_scenario_path_connection_{};
    std::uint64_t ref_data_version_connection_{};
    std::uint64_t ref_target_version_connection_{};
    std::uint64_t ref_skin_connection_{};
    std::uint64_t repl_text_connection_{};

    void bindExternalNotifications();
    void unbindExternalNotifications() noexcept;
    void synchronizeExternalInputs(bool refresh_error_versions);
    void buildUi(Shell parent_shell);
    void bindOptions(const std::shared_ptr<lib::Container>& panel);
    std::shared_ptr<lib::GroupBox> addCheckGroup(
        const std::shared_ptr<lib::Panel>& parent, String title,
        std::vector<std::pair<String, bool*>> entries, std::size_t columns,
        std::size_t row, String all_text);
    void refreshCheckGroup(std::size_t index);
    void setCheckGroup(std::size_t index, bool checked);
    void refreshContentsAll();
    void setContentsAll(bool checked);
    [[nodiscard]] CWXPathPtr selectedFindObject() const;
    [[nodiscard]] bool canFindSelectedID() const;
    void findSelectedID();
    void attachIncrementalMenu(const std::shared_ptr<lib::ComboBox>& combo,
                               incsearch::IncSearch::Enabled enabled,
                               incsearch::IncSearch::ModCallback modified = {});
    void refreshHistoryCombos();
    bool addHistory(std::vector<String>& histories, StringView value);
    void warnOpenFailure(StringView path, bool file = false) const;
    void setupPaths();
    void setupErrorVersions();
    void applyGeometry();
    void syncIDControl(bool from);
    void configureResultColumns();
    [[nodiscard]] String currentScenarioDirectory() const;
    void setGrepCurrentDirectory();
    void browseGrepDirectory();
    [[nodiscard]] String grepDirectoryOpenPath() const;
    void openGrepDirectory();
    void addResult(ResultRow row);
    [[nodiscard]] String resultParent(const TextTarget& target) const;
    void setResultStatus(std::size_t count);
    void refResultStatus(std::size_t count, bool force);
    void after(bool changed, bool id_changed,
               std::optional<namewindow::NameIDKind> name_id_kind = std::nullopt);
    void completeUndoRedo(bool redo, bool id_changed,
                          std::optional<namewindow::NameIDKind> name_id_kind = std::nullopt);
    void restoreOperationFocus(const std::weak_ptr<lib::Widget>& prior,
                               std::uint64_t focus_revision,
                               bool replace_operation);
    void replaceImpl(bool replace_mode, bool retire_history = true);
    std::vector<TextTarget> targetsForTab() const;
    std::vector<IdTarget> idTargets() const;
    std::vector<TextTarget> summaryTextTargets() const;
    bool enumerateSummaryText(const std::function<bool(TextTarget)>& emit) const;
    bool enumerateSummaryPath(const std::function<bool(TextTarget)>& emit) const;
    std::vector<TextTarget> summaryPathTargets() const;
    std::vector<TextTarget> summaryCouponTargets() const;
    std::vector<IdTarget> summaryIdTargets(IDKind kind) const;
    bool enumerateSummaryIds(IDKind kind, const std::function<bool(IdTarget)>& emit) const;
    std::vector<std::shared_ptr<RangeNode>> defaultRangeNodes() const;
    bool targetSelected(const TextTarget& target) const;
    bool targetSelected(const IdTarget& target) const;
    void renameCouponResult(std::size_t row, String value);
    void refreshToolbar();
    void refreshSource();
    [[nodiscard]] ReplaceSource::BatchProvider batchProviderForTab() const;
    ReplaceProviderResult runBatchProvider(const ReplaceSource::BatchProvider& provider,
                                           std::size_t& count,
                                           undo::UndoArray& operations);
    ReplaceProviderResult runIdBatchProvider(const ReplaceSource::BatchProvider& provider,
                                             std::size_t& count,
                                             undo::UndoArray& operations,
                                             bool& id_changed);

    template <typename ID>
    static IDKind idKind() {
        using Value = std::remove_cv_t<std::remove_reference_t<ID>>;
        if constexpr (std::is_same_v<Value, AreaId>) return IDKind::Area;
        else if constexpr (std::is_same_v<Value, BattleId>) return IDKind::Battle;
        else if constexpr (std::is_same_v<Value, PackageId>) return IDKind::Package;
        else if constexpr (std::is_same_v<Value, CastId>) return IDKind::Cast;
        else if constexpr (std::is_same_v<Value, SkillId>) return IDKind::Skill;
        else if constexpr (std::is_same_v<Value, ItemId>) return IDKind::Item;
        else if constexpr (std::is_same_v<Value, BeastId>) return IDKind::Beast;
        else if constexpr (std::is_same_v<Value, InfoId>) return IDKind::Info;
        else if constexpr (std::is_same_v<Value, FlagId>) return IDKind::Flag;
        else if constexpr (std::is_same_v<Value, StepId>) return IDKind::Step;
        else if constexpr (std::is_same_v<Value, VariantId>) return IDKind::Variant;
        else if constexpr (std::is_same_v<Value, CouponId>) return IDKind::Coupon;
        else if constexpr (std::is_same_v<Value, GossipId>) return IDKind::Gossip;
        else if constexpr (std::is_same_v<Value, CompleteStampId>) return IDKind::CompleteStamp;
        else if constexpr (std::is_same_v<Value, KeyCodeId>) return IDKind::KeyCode;
        else if constexpr (std::is_same_v<Value, CellNameId>) return IDKind::CellName;
        else if constexpr (std::is_same_v<Value, CardGroupId>) return IDKind::CardGroup;
        else static_assert(std::is_same_v<Value, void>, "unsupported ID type");
    }

    template <typename ID>
    static String idToString(const ID& value) {
        if constexpr (requires { value.toString(); }) return value.toString();
        else if constexpr (std::is_convertible_v<ID, String>) return static_cast<String>(value);
        else if constexpr (std::is_integral_v<ID>) return std::to_string(value);
        else return {};
    }
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::replacedialog
