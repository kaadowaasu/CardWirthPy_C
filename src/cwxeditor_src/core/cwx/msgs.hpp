#pragma once
// cwx/msgs.dのC++20移植。 メッセージの記録は、Dのプロパティキー
// ・既定値とXMLFuncs!(..., "message")の扱いを維持する。

#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::msgs {

using String = ::cwxeditor::lib::String;
using XmlNode = ::cwxeditor::cwx::xml::XNode;

#if defined(_WIN32)
inline const String CARD_WIRTH_PY_EXE = "CardWirthPy.exe";
inline const String CWX_EDITOR_EXE = "cwxeditor.exe";
inline const String DIR = "フォルダ";
#else
inline const String CARD_WIRTH_PY_EXE = "CardWirthPy";
inline const String CWX_EDITOR_EXE = "cwxeditor";
inline const String DIR = "ディレクトリ";
#endif

namespace detail {
template <typename... Parts>
String concat(const Parts&... parts) {
    String result;
    (result.append(parts), ...);
    return result;
}
}  // namespace detail

class Field {
public:
    virtual ~Field() = default;
    virtual void toNode(XmlNode& node, bool writeAll) const = 0;
    virtual void fromNode(const XmlNode& node, std::uint64_t dataVersion) = 0;
};

class Message : public Field {
public:
    String KEY;
    String INIT;
    String value;
    bool readValue{false};

    Message() = default;
    Message(String key, String initial)
        : KEY(std::move(key)), INIT(initial), value(std::move(initial)) {}

    Message(const Message&) = default;
    Message(Message&&) noexcept = default;
    Message& operator=(const Message&) = default;
    Message& operator=(Message&&) noexcept = default;
    Message& operator=(const String& next) { value = next; return *this; }
    Message& operator=(String&& next) noexcept { value = std::move(next); return *this; }
    operator const String&() const noexcept { return value; }
    operator String&() noexcept { return value; }
    void reset() { value = INIT; readValue = false; }

    void toNode(XmlNode& node, bool writeAll) const override {
        if (writeAll || value != INIT) node.newElement(KEY, value);
    }
    void fromNode(const XmlNode& node, std::uint64_t) override {
        const auto child = node.child(KEY, false);
        if (child.valid()) { value = child.value(); readValue = true; }
    }
};
using Msg = Message;

template <typename T>
class Attribute : public Field {
public:
    String ATTR_KEY;
    T INIT{};
    T value{};
    bool readValue{false};

    Attribute() = default;
    Attribute(String key, T initial)
        : ATTR_KEY(std::move(key)), INIT(initial), value(std::move(initial)) {}

    Attribute(const Attribute&) = default;
    Attribute(Attribute&&) noexcept = default;
    Attribute& operator=(const Attribute&) = default;
    Attribute& operator=(Attribute&&) noexcept = default;
    Attribute& operator=(const T& next) { value = next; return *this; }
    operator const T&() const noexcept { return value; }
    operator T&() noexcept { return value; }
    void reset() { value = INIT; readValue = false; }

    void toNode(XmlNode& node, bool writeAll) const override {
        if (writeAll || value != INIT) {
            const String text = ::cwxeditor::lib::detail::xml_to_string(value);
            node.new_attr(ATTR_KEY, std::string_view(text));
        }
    }
    void fromNode(const XmlNode& node, std::uint64_t) override {
        const auto attr = node.attribute(ATTR_KEY);
        if (attr.has_value()) {
            value = ::cwxeditor::lib::detail::xml_from_string<T>(*attr);
            readValue = true;
        }
    }
};

class AssociativeMessage : public Field {
public:
    String AA_KEY;
    String KEY_NAME;
    String VALUE_NAME;
    using Map = std::map<String, String>;
    Map value;

    AssociativeMessage() = default;
    AssociativeMessage(String key, String keyName, String valueName)
        : AA_KEY(std::move(key)), KEY_NAME(std::move(keyName)),
          VALUE_NAME(std::move(valueName)) {}

    operator const Map&() const noexcept { return value; }
    operator Map&() noexcept { return value; }

    [[nodiscard]] String get(const std::string_view key,
                             const std::string_view fallback = {}) const {
        const auto found = value.find(String(key));
        return found != value.end() ? found->second : String(fallback);
    }

    void toNode(XmlNode& node, bool) const override {
        auto container = node.newElement(AA_KEY);
        for (const auto& [key, val] : value) {
            auto child = container.newElement(VALUE_NAME, val);
            child.new_attr(KEY_NAME, std::string_view(key));
        }
    }
    void fromNode(const XmlNode& node, std::uint64_t) override {
        const auto container = node.child(AA_KEY, false);
        if (!container.valid()) return;
        for (const auto& child : container.element_children()) {
            if (child.name() != VALUE_NAME) continue;
            const auto key = child.attribute(KEY_NAME);
            if (key.has_value()) value[*key] = child.value();
        }
    }
};
using AAMsg = AssociativeMessage;

#define CWX_MESSAGE_FIELDS(X) \
    X(application, "application", "CWXEditor_C") \
    X(localeName, "localeName", "日本語") \
    X(dlgTitVersion, "dlgTitVersion", "バージョン情報") \
    X(appDesc, "appDesc", "CardWirthPy / CardWirth向けシナリオエディタ") \
    X(dlgTitUsage, "dlgTitUsage", "使い方 - CWXEditor_C") \
    X(usage, "usage", detail::concat("使い方: cwxeditor [-help | -putlangfile <PATH> | -conf <PATH>\n", "                   | -create <NAME> [<SKIN TYPE> [<SKIN NAME>]] | -createclassic <NAME> [<PATH>]\n", "                   | -selectfile <PATH> | -noload] <SCENARIO> [<CWXPath ...>]\n", "オプション:\n", "  -help         起動オプションの説明を表示して終了します。\n", "  -putlangfile <PATH> デフォルトの言語設定ファイル<PAHT>を出力して終了します。\n", "  -conf <PATH>  指定されたパスの基本設定ファイルを使用します。\n", "  -create        <NAME> [<SKIN TYPE> [<SKIN NAME>]]  起動後にシナリオを新規作成します。\n", "  -createclassic <NAME> [<PATH>]  起動後、<PATH>で指定されたフォルダに\n", "                                  クラシックなシナリオを新規作成します。\n", "  -selectfile  <PATH> 指定されたファイルをファイルビューで選択します。\n", "  -noload       起動後、前回終了時の編集状態を復元しません。\n", "  <SCENARIO>    起動と同時に指定されたシナリオを開きます。\n", "                (*.wsn/Summary.xml/Summary.wsm/[フォルダ])\n", "OpenID:\n", "  -a <ID>       シナリオを開いた後、<ID>で指定したIDのエリアを開きます。\n", "  -b <ID>       シナリオを開いた後、<ID>で指定したIDのバトルを開きます。\n", "  -p <ID>       シナリオを開いた後、<ID>で指定したIDのパッケージを開きます。\n", "CWXPath:\n", "  <CWXPath>     シナリオを開いた後、<CWXPath>で指定したリソースを開きます。")) \
    X(dlgTitError, "dlgTitError", "エラー - CWXEditor_C") \
    X(dlgTitWarning, "dlgTitWarning", "警告 - CWXEditor_C") \
    X(dlgTitQuestion, "dlgTitQuestion", "確認 - CWXEditor_C") \
    X(unknownError, "unknownError", detail::concat("処理の途中でCWXEditor_Cの制作者が意図していないエラーが発生しました。", "データが壊れている可能性を考慮して、シナリオを保存せずに終了する事をお勧めします。\n", "エラーの内容は%1$sに記録されます。")) \
    X(shutdown, "shutdown", "強制終了") \
    X(warningWindowsHandleCount, "warningWindowsHandleCount", "大量のタブやウィンドウを開いているため、Windowsハンドルを大量に消費しています。\nこのままハンドルを消費し続けると強制終了する恐れがあるため、不要なタブやウィンドウを閉じる事をお勧めします。") \
    X(targetVersion, "targetVersion", "対象エンジン") \
    X(targetVersionHint, "targetVersion", "※ 警告と誤り検索の結果に影響します") \
    X(cardWirthPy, "cardWirthPy", "CardWirthPy") \
    X(cardWirthWithVersion, "cardWirthWithVersion", "CardWirth %1$s") \
    X(dlgTextOK, "dlgTextOK", "&OK") \
    X(dlgTextApply, "dlgTextApply", "適用(&A)") \
    X(dlgTextCancel, "dlgTextCancel", "キャンセル(&C)") \
    X(dlgTextClose, "dlgTextClose", "閉じる(&C)") \
    X(apply, "apply", "適用") \
    X(del, "del", "削除") \
    X(filterAll, "filterAll", "全てのファイル (*.*)") \
    X(fileCopyError, "fileCopyError", "%1$sのコピー中にエラーが発生しました。") \
    X(reloadError, "reloadError", "%1$sの再読込中にエラーが発生しました。") \
    X(loadProgress, "loadProgress", "%2$s%% 完了 - %1$sを展開中") \
    X(loading, "loading", "%1$sの読込みを開始") \
    X(loadingWithFile, "loading", "%1$sを読込んでいます... (%2$s)") \
    X(loaded, "loaded", "%1$sの読込みを完了") \
    X(loadedCount, "loadedCount", "%1$s件の読込みを完了") \
    X(reconstructionStatus, "reconstructionStatus", "編集状態を復元中 (%1$s/%2$s)") \
    X(cwxPathOpenError, "cwxPathOpenError", "パス [%1$s] を開けません。") \
    X(filePathOpenError, "filePathOpenError", "パス [%1$s] を開けません。") \
    X(editScenarioHistory, "editScenarioHistory", "履歴とブックマークの編集") \
    X(editImportHistory, "editImportHistory", "追加元の履歴とブックマークの編集") \
    X(scenarioBookmarkHint, "scenarioBookmarkHint", "ブックマークに登録したいシナリオはチェックしてください。") \
    X(historyScenarioFileName, "historyScenarioFileName", "ファイル名") \
    X(historyScenarioPath, "historyScenarioPath", "場所") \
    X(warningHistoryTooMany, "warningHistoryTooMany", "履歴が多すぎます(%1$s件)。%2$s件を超えた分は適用時に破棄されます。") \
    X(editExecutedPartyHistory, "editExecutedPartyHistory", "パーティの履歴とブックマークの編集") \
    X(executionPartyBookmarkHint, "executionPartyBookmarkHint", "ブックマークに登録したいパーティはチェックしてください。") \
    X(executionPartyName, "executionPartyName", "パーティ") \
    X(executionPartyYado, "executionPartyYado", "拠点") \
    X(executionPartyEngine, "executionPartyEngine", "エンジン") \
    X(loadSkinError, "loadSkinError", detail::concat("デフォルトのスキン「%1$s」が見つかりません。\n", CARD_WIRTH_PY_EXE, "本体の場所が正しくないか、Data", DIR, "が正しく配置されていない可能性があります。\nこのまま開始すると、一部リソース画像が非表示になります。")) \
    X(useDefaultSkin, "useDefaultSkin", "スキン「%1$s」が見つかりません。\nデフォルトのスキン「%1$s」を使用します。") \
    X(scenarioName, "scenarioName", "シナリオ名") \
    X(type, "type", "タイプ") \
    X(initialize, "initialize", "初期設定") \
    X(classic, "classic", "クラシック") \
    X(scenarioTemplate, "scenarioTemplate", "テンプレート") \
    X(templateDesc, "templateDesc", "%1$s [%2$s]") \
    X(noTemplate, "noTemplate", "テンプレート無し") \
    X(createClassicDir, "createClassicDir", "シナリオの作成先") \
    X(newClassicDir, "newClassicDir", "シナリオ作成先の選択") \
    X(newClassicDirDesc, "newClassicDirDesc", detail::concat("シナリオを作成する", DIR, "を選択してください。")) \
    X(notEmptyDir, "notEmptyDir", "%1$sは空ではありません。\n本当にここでシナリオを作成しますか？") \
    X(createScenarioNameDir, "createScenarioNameDir", detail::concat("シナリオの", DIR, "を新規作成する")) \
    X(notClassicWarning, "notClassicWarning", "※ 「クラシック」以外のタイプはWSN形式となります") \
    X(dlgTitSaveBitmapImage, "dlgTitSaveBitmapImage", "格納イメージの保存") \
    X(filterBitmapImage, "filterBitmapImage", "ビットマップイメージ (*.bmp)") \
    X(filterJPEGImage, "filterJPEGImage", "JPEGイメージ (*.jpg)") \
    X(filterGIFImage, "filterGIFImage", "GIFイメージ (*.gif)") \
    X(filterTIFFImage, "filterTIFFImage", "TIFFイメージ (*.tiff)") \
    X(filterPNGImage, "filterPNGImage", "PNGイメージ (*.png;*.apng)") \
    X(filterICOImage, "filterICOImage", "アイコン (*.ico)") \
    X(filterWEBPImage, "filterWEBPImage", "WebPイメージ (*.webp)") \
    X(dlgMsgIncludeImage, "dlgMsgIncludeImage", "%1$sをシナリオファイル内にコピーしますか？\n(元のファイルは削除されません)") \
    X(dlgMsgExcludeImage, "dlgMsgExcludeImage", "WSN形式のシナリオでは格納イメージは使用できません。\n格納イメージを外部化しますか？\n(外部化しなかった場合、格納イメージは消滅します)") \
    X(cardImagePosition, "cardImagePosition", "イメージの配置方式:") \
    X(cardImagePositionNameDefault, "cardImagePositionNameDefault", "指定しない") \
    X(cardImagePositionNameCenter, "cardImagePositionNameCenter", "中央寄せ") \
    X(cardImagePositionNameTopLeft, "cardImagePositionNameTopLeft", "左上寄せ") \
    X(dlgTitImageLayerWindow, "dlgTitImageLayerWindow", "レイヤの編集") \
    X(dlgTitImageLayerWindowReadOnly, "dlgTitImageLayerWindowReadOnly", "レイヤの一覧") \
    X(layerName, "layerName", "レイヤ %1$s") \
    X(warningLayer, "warningLayer", "レイヤの指定はWsn.1以降の形式のシナリオでしか行えません。") \
    X(layerValues, "layerValues", "%1$s = 背景セル\n%2$s = メニュー・エネミーカード\n%3$s = プレイヤーカード\n%4$s = メッセージ") \
    X(warningBgImageSmoothing, "warningBgImageSmoothing", "背景セルを滑らかに拡大・縮小するかの指定はWsn.2以降の形式のシナリオでしか行えません。") \
    X(smoothing, "smoothing", "サイズ変更時の処理") \
    X(smoothingNameDefault, "smoothingNameDefault", "指定しない") \
    X(smoothingNameTrue, "smoothingNameTrue", "滑らかにする") \
    X(smoothingNameFalse, "smoothingNameFalse", "滑らかにしない") \
    X(newFolder, "newFolder", detail::concat("新規", DIR)) \
    X(dlgMsgDeleteFile, "dlgMsgDeleteFile", "%1$sを完全に削除しますか？") \
    X(dlgMsgDeleteFiles, "dlgMsgDeleteFiles", "%1$s個の項目を完全に削除しますか？") \
    X(dlgMsgDeleteFileRecycle, "dlgMsgDeleteFileRecycle", "%1$sをごみ箱に移動しますか？") \
    X(dlgMsgDeleteFilesRecycle, "dlgMsgDeleteFilesRecycle", "%1$s個の項目をごみ箱に移動しますか？") \
    X(dlgMsgDeleteUnuse, "dlgMsgDeleteUnuse", detail::concat("%1$s個の未使用ファイル・", DIR, "を完全に削除しますか？")) \
    X(dlgMsgDeleteRecycleUnuse, "dlgMsgDeleteRecycleUnuse", detail::concat("%1$s個の未使用ファイル・", DIR, "をごみ箱に移動しますか？")) \
    X(findUnusedHint, "findUnusedHint", "※ 式で動的に生成された名前で状態変数を参照している場合は、使用を検出できません") \
    X(image, "image", "イメージ") \
    X(pathDef, "pathDef", "デフォルト") \
    X(pathWsnBasic, "pathWsnBasic", "%1$s 標準") \
    X(imageNone, "imageNone", "イメージ無し") \
    X(fileNone, "fileNone", "ファイルを選択") \
    X(imageIncluding, "imageIncluding", "イメージ格納") \
    X(pcNumber, "pcNumber", "プレイヤー%1$s") \
    X(pc, "pc", "%1$s番目のメンバ") \
    X(bgmStop, "bgmStop", "BGM停止") \
    X(continueBGM, "continueBGM", "再生中のBGMを継続") \
    X(bgmNone, "bgmNone", "BGM無し") \
    X(useNoCardSizeImage, "useNoCardSizeImage", "%1$s×%2$s以外も許容") \
    X(excludeCardSizeImage, "excludeCardSizeImage", "カードイメージを除外する") \
    X(dlgMsgForceCancelDialogs, "dlgMsgForceCancelDialogs", "%1$s件のダイアログが変更されたまま適用されていません。無視して操作を続行しますか？") \
    X(dlgMsgForceCancelDialogsQuit, "dlgMsgForceCancelDialogsQuit", "%1$s件のダイアログが変更されたまま適用されていません。無視して終了しますか？") \
    X(dlgMsgIsSaveBeforeReload, "dlgMsgIsSaveBeforeReload", "「%1$s」は変更されています。再読込しますか？") \
    X(dlgMsgIsSaveBeforeExit, "dlgMsgIsSaveBeforeExit", "「%1$s」は変更されています。保存しますか？") \
    X(dlgMsgDropFile, "dlgMsgDropFile", detail::concat("%1$sをシナリオ", DIR, "にコピーしますか？")) \
    X(dlgMsgDropFiles, "dlgMsgDropFiles", detail::concat("%1$s個のファイルをシナリオ", DIR, "にコピーしますか？")) \
    X(dlgMsgDropOverWriteFile, "dlgMsgDropOverWriteFile", "%1$sはすでに存在します。上書きしますか？") \
    X(dlgMsgDropOverWriteFiles, "dlgMsgDropOverWriteFiles", "%1$s個の項目がすでに存在します。上書きしますか？") \
    X(dlgTitDropFiles, "dlgTitDropFiles", "素材ファイルの追加") \
    X(dlgMsgCopyError, "dlgMsgCopyError", "いくつかのファイルのコピーに失敗しました。") \
    X(dlgMsgCopyMaterial1, "dlgMsgCopyMaterial1", "格納イメージを参照に変更してコピーしますか？") \
    X(dlgMsgCopyMaterial2, "dlgMsgCopyMaterial2", "参照された素材もコピーしますか？\n%1$s") \
    X(dlgMsgCopyMaterial3, "dlgMsgCopyMaterial3", "参照された素材もコピーしますか？\n%1$s個のファイル") \
    X(incSearchContains, "incSearchContains", "名前の一部") \
    X(incSearchWildcard, "incSearchWildcard", "ワイルドカード") \
    X(incSearchRegex, "incSearchRegex", "正規表現") \
    X(dlgTitSettings, "dlgTitSettings", "CWXEditor_Cの設定") \
    X(refreshS, "refreshS", "更新") \
    X(summary, "summary", "シナリオの設定") \
    X(area, "area", "エリア") \
    X(battle, "battle", "バトル") \
    X(cwPackage, "cwPackage", "パッケージ") \
    X(dlgTitReplaceText, "dlgTitReplaceText", "検索と置換") \
    X(replForText, "replForText", "テキスト検索") \
    X(replForID, "replForID", "ID検索") \
    X(replForPath, "replForPath", "素材検索") \
    X(replContents, "replContents", "コンテント検索") \
    X(replForCoupon, "replForCoupon", "称号・名称一覧") \
    X(replForUnuse, "replForUnuse", "未使用検索") \
    X(replForError, "replForError", "誤り検索") \
    X(replGrep, "replGrep", "外部シナリオ") \
    X(replStartUsers, "replStartUsers", "スタートの参照") \
    X(replLocalVariables, "replLocalVariables", "ローカル変数の参照") \
    X(searchRange, "searchRange", "検索対象") \
    X(flagsAndSteps, "flagsAndSteps", "フラグとステップ") \
    X(allCheckRange, "allCheckRange", "全てチェック/全てチェックを外す") \
    X(allCheck, "allCheck", "全てチェック/全てチェックを外す(&L)") \
    X(allSelect, "allSelect", "全て選択/全て選択を外す(&L)") \
    X(replError, "replError", "重複する分岐(フラグ分岐が両方ともTRUEになっている等)・条件クーポンに抜けがあるセリフコンテント・存在しない素材を参照しているコンテント・基準とするバージョンのCardWirthに存在しない機能の使用等を検出します。") \
    X(searchErrorCondition, "searchErrorCondition", "検出条件") \
    X(searchErrorTargetVersion, "searchErrorTargetVersion", "基準とするバージョン") \
    X(searchErrorTargetVersionNotSet, "searchErrorTargetVersionNotSet", "[自動選択]") \
    X(searchErrorTargetVersionHint, "searchErrorTargetVersionHint", "※ 自動選択では、シナリオのデータバージョン及び対象エンジン設定が採用されます") \
    X(searchErrorTargetVersionName, "searchErrorTargetVersionName", "%1$s (%2$s)") \
    X(replFrom, "replFrom", "検索(置換前)") \
    X(replTo, "replTo", "置換後") \
    X(grepFrom, "replFrom", "検索") \
    X(replText, "replText", "検索/置換するテキスト") \
    X(replTextTarget, "replTextTarget", "検索/置換対象") \
    X(replTextSummary, "replTextSummary", "貼紙") \
    X(replTextMessage, "replTextMessage", "メッセージ") \
    X(replTextCardName, "replTextCardName", "カード名") \
    X(replTextCardDesc, "replTextCardDesc", "カード解説") \
    X(replTextEventText, "replTextEventText", "イベントテキスト") \
    X(replTextStart, "replTextStart", "スタートコンテント") \
    X(replTextVariableName, "replTextVariableName", "状態変数名") \
    X(replTextVariableValue, "replTextVariableValue", "変数の値") \
    X(replTextExpression, "replTextExpression", "式") \
    X(replTextCoupon, "replTextCoupon", "クーポン") \
    X(replTextGossip, "replTextGossip", "ゴシップ") \
    X(replTextEndScenario, "replTextEndScenario", "終了印") \
    X(replTextAreaName, "replTextAreaName", "エリア/バトル/パッケージ名") \
    X(replTextKeyCode, "replTextKeyCode", "キーコード") \
    X(replTextCellName, "replTextCellName", "セル名称") \
    X(replTextCardGroup, "replTextCardGroup", "カードグループ") \
    X(replTextFile, "replTextFile", "ファイル名") \
    X(replTextComment, "replTextComment", "コメント") \
    X(replTextJptx, "replTextJptx", "JPTX/テキストセル") \
    X(replTextTextFile, "replTextTextFile", "テキストファイル") \
    X(replTextScenario, "replTextScenario", "シナリオ名") \
    X(replTextAuthor, "replTextAuthor", "作者名") \
    X(replacePlainTextHint, "replacePlainTextHint", "文字コード Shift JIS または UTF-8 のみ有効\n(%1$s)") \
    X(replacePlainTextHintNoExt, "replacePlainTextHintNoExt", "文字コード Shift JIS または UTF-8 のみ有効") \
    X(replID, "replID", "検索/置換対象") \
    X(replIDKind, "replIDKind", "対象") \
    X(replIDArea, "replIDArea", "エリア") \
    X(replIDBattle, "replIDBattle", "バトル") \
    X(replIDPackage, "replIDPackage", "パッケージ") \
    X(replIDCast, "replIDCast", "キャストカード") \
    X(replIDSkill, "replIDSkill", "特殊技能カード") \
    X(replIDItem, "replIDItem", "アイテムカード") \
    X(replIDBeast, "replIDBeast", "召喚獣カード") \
    X(replIDInfo, "replIDInfo", "情報カード") \
    X(replIDFlag, "replIDFlag", "フラグ") \
    X(replIDStep, "replIDStep", "ステップ") \
    X(replIDVariant, "replIDVariant", "コモン") \
    X(replIDCoupon, "replIDCoupon", "クーポン") \
    X(replIDGossip, "replIDGossip", "ゴシップ") \
    X(replIDCompleteStamp, "replIDCompleteStamp", "終了印") \
    X(replIDKeyCode, "replIDKeyCode", "キーコード") \
    X(replIDCellName, "replIDCellName", "セル名称") \
    X(replIDCardGroup, "replIDCardGroup", "カードグループ") \
    X(replSetID, "replSetID", "IDを直接指定") \
    X(replPath, "replPath", "検索/置換する素材") \
    X(replUnuseTarget, "replUnuseTarget", "検索対象") \
    X(replUnuseFlag, "replUnuseFlag", "フラグ") \
    X(replUnuseStep, "replUnuseStep", "ステップ") \
    X(replUnuseVariant, "replUnuseVariant", "コモン") \
    X(replUnuseArea, "replUnuseArea", "エリア") \
    X(replUnuseBattle, "replUnuseBattle", "バトル") \
    X(replUnusePackage, "replUnusePackage", "パッケージ") \
    X(replUnuseCast, "replUnuseCast", "キャストカード") \
    X(replUnuseSkill, "replUnuseSkill", "特殊技能カード") \
    X(replUnuseItem, "replUnuseItem", "アイテムカード") \
    X(replUnuseBeast, "replUnuseBeast", "召喚獣カード") \
    X(replUnuseInfo, "replUnuseInfo", "情報カード") \
    X(replUnuseStart, "replUnuseStart", "スタートコンテント") \
    X(replUnusePath, "replUnusePath", "素材") \
    X(replNotIgnoreCase, "replNotIgnoreCase", "大文字と小文字を区別する(&C)") \
    X(replRegExp, "replRegExp", "正規表現(&E) (. = 任意1文字, * = 直前の文字の任意数繰返し, $1 = 1つめの文字列グループ ...)") \
    X(regexError, "regexError", "正規表現が正しくありません。") \
    X(replWildcard, "replWildcard", "ワイルドカード(&W) (* = 任意文字列, ? = 任意1文字, \\* = *, \\? = ?, \\\\ = \\)") \
    X(replExactMatch, "replExactMatch", "完全一致(&X)") \
    X(replIgnoreReturnCode, "replIgnoreReturnCode", "改行と前後の空白を無視(&I) (置換はできません)") \
    X(wildcardDesc, "wildcardDesc", "ワイルドカードが使用できます(* = 任意文字列, ? = 任意1文字, \\* = *, \\? = ?, \\\\ = \\)") \
    X(replCond, "replCond", "検索条件") \
    X(search, "search", "検索(&F)") \
    X(replace, "replace", "全て置換(&R)") \
    X(cautionOfReplace, "cautionOfReplace", "%1$sを%2$sに置換します。よろしいですか？") \
    X(replaceValue, "replaceValue", "「%1$s」") \
    X(emptyText, "emptyText", "空文字列") \
    X(emptyPath, "emptyPath", "空のパス") \
    X(idValue, "idValue", "ID:%1$sの%2$s") \
    X(commentText, "commentText", "コメント [ %1$s ]") \
    X(searchCancel, "searchCancel", "キャンセル(&C)") \
    X(searchResultEmpty, "searchResultEmpty", "0件の検索結果") \
    X(searchResult, "searchResult", "%1$s件の検索結果(%2$s)") \
    X(searchResultGrep1, "searchResultGrep1", "%1$s件のシナリオ内の%2$s件の検索結果(%3$sを読込中...)") \
    X(searchResultGrep2, "searchResultGrep2", "%1$s件のシナリオ内の%2$s件の検索結果(%3$sを検索中...)") \
    X(searchResultGrep3, "searchResultGrep3", "%1$s件のシナリオ内の%2$s件の検索結果(%3$s)") \
    X(searchResultRealtime, "searchResultRealtime", "リアルタイム更新(&R)") \
    X(replResultEmpty, "replResultEmpty", "0箇所の置換") \
    X(replResult, "replResult", "%1$s箇所の置換(%2$s)") \
    X(replaceUndo, "replaceUndo", "%1$s件を元に戻しました") \
    X(replaceRedo, "replaceRedo", "%1$s件をやり直しました") \
    X(grepText, "grepText", "検索するテキスト") \
    X(grepTarget, "grepTarget", "検索対象") \
    X(grepDir, "grepDir", "外部シナリオ検索") \
    X(grepDirDesc, "grepDirDesc", detail::concat("検索対象のシナリオが含まれる", DIR, "を選択してください。")) \
    X(grepCurrent, "grepCurrent", detail::concat("現", DIR)) \
    X(grepSubDir, "grepSubDir", detail::concat("サブ", DIR, "も検索する")) \
    X(grepScenario, "grepScenario", "%1$s[%2$s]") \
    X(searchResultColumnMain, "searchResultColumnMain", "マッチ箇所") \
    X(searchResultColumnParent, "searchResultColumnParent", "所属") \
    X(searchResultColumnCoupon, "searchResultColumnCoupon", "称号・名称") \
    X(searchResultColumnCouponCount, "searchResultColumnCouponCount", "利用数") \
    X(searchResultColumnError, "searchResultColumnError", "誤り箇所") \
    X(searchResultColumnErrorDesc, "searchResultColumnErrorDesc", "解説") \
    X(searchResultColumnScenario, "searchResultColumnScenario", "シナリオ") \
    X(searchResultSummary, "searchResultSummary", "シナリオの概要 - %1$s") \
    X(searchResultImageCell, "searchResultBgImage", "背景画像「%1$s」") \
    X(searchResultTextCell, "searchResultTextCell", "テキストセル「%1$s」") \
    X(searchResultColorCell, "searchResultColorCell", "カラーセル「%1$s」") \
    X(searchResultPCCell, "searchResultPCCell", "プレイヤーキャラクタセル「%1$s」") \
    X(searchResultIds, "searchResultIds", "%1$s「%2$s.%3$s」") \
    X(searchResultIdsWithDesc, "searchResultIdsWithDesc", "%1$s「%2$s.%3$s」 - %4$s") \
    X(searchResultFlag, "searchResultFlag", "フラグ「%1$s」") \
    X(searchResultStep, "searchResultStep", "ステップ「%1$s」") \
    X(searchResultVariant, "searchResultVariant", "コモン「%1$s」") \
    X(searchResultFlagDir, "searchResultFlagDir", "ディレクトリ「%1$s」") \
    X(searchResultEventTree, "searchResultEventTree", "イベントツリー「%1$s」") \
    X(searchResultMenuCard, "searchResultMenuCard", "メニューカード「%1$s」") \
    X(searchResultMenuCardWithDesc, "searchResultMenuCardWithDesc", "メニューカード「%1$s」 - %2$s") \
    X(searchResultEnemyCard, "searchResultEnemyCard", "エネミーカード「%1$s」") \
    X(searchResultJpy1, "searchResultJpy1", "JPY1ファイル「%1$s」") \
    X(searchResultJpdc, "searchResultJpdc", "JPDCファイル「%1$s」") \
    X(searchErrorReversalLevel, "searchErrorReversalLevel", "対象レベルの上限と下限が逆転しています。") \
    X(warningNoLevelMax, "warningNoLevelMax", "最大対象レベルが設定されていません。") \
    X(warningNoLevelMin, "warningNoLevelMin", "最小対象レベルが設定されていません。") \
    X(searchErrorNoImage, "searchErrorNoImage", "イメージが指定されていません。") \
    X(searchErrorImageNotFound, "searchErrorImageNotFound", "存在しないイメージファイル(%1$s)が指定されています。") \
    X(searchErrorBGMNotFound, "searchErrorBGMNotFound", "存在しないBGMファイル(%1$s)が指定されています。") \
    X(searchErrorSENotFound, "searchErrorSENotFound", "存在しない効果音ファイル(%1$s)が指定されています。") \
    X(searchErrorStartAreaNotFound, "searchErrorStartAreaNotFound", "開始エリアが設定されていません。") \
    X(searchErrorFlagNotFound, "searchErrorFlagNotFound", "存在しないフラグ(%1$s)が指定されています。") \
    X(searchErrorStepNotFound, "searchErrorStepNotFound", "存在しないステップ(%1$s)が指定されています。") \
    X(searchErrorStepValueNotFound, "searchErrorStepValueNotFound", "存在しないステップ値(%1$s)が指定されています。") \
    X(searchErrorVariantNotFound, "searchErrorVariantNotFound", "存在しないコモン(%1$s)が指定されています。") \
    X(searchErrorDupNextContent, "searchErrorDupNextContent", "分岐条件が重複しています。") \
    X(searchErrorSPFontIsNotSJIS1ByteChar, "searchErrorSPFontIsNotSJIS1ByteChar", "「%1$s」は無効です。クラシックなシナリオの特殊フォント指定にはShift JISの1バイト文字しか使用できません。") \
    X(searchErrorSPFontNotFound, "searchErrorSPFontNotFound", "特殊フォントイメージ(%1$s)が見つかりません。") \
    X(searchErrorNoRCouponsDialog, "searchErrorNoRCouponsDialog", "最終項目以外にクーポン指定無し項目があります。") \
    X(searchErrorAreaNotFound, "searchErrorAreaNotFound", "存在しないエリア(ID:%1$s)が指定されています。") \
    X(searchErrorBattleNotFound, "searchErrorBattleNotFound", "存在しないバトル(ID:%1$s)が指定されています。") \
    X(searchErrorPackageNotFound, "searchErrorPackageNotFound", "存在しないパッケージ(ID:%1$s)が指定されています。") \
    X(searchErrorCastNotFound, "searchErrorCastNotFound", "存在しないキャストカード(ID:%1$s)が指定されています。") \
    X(searchErrorSkillNotFound, "searchErrorSkillNotFound", "存在しない特殊技能カード(ID:%1$s)が指定されています。") \
    X(searchErrorItemNotFound, "searchErrorItemNotFound", "存在しないアイテムカード(ID:%1$s)が指定されています。") \
    X(searchErrorBeastNotFound, "searchErrorBeastNotFound", "存在しない召喚獣カード(ID:%1$s)が指定されています。") \
    X(searchErrorInfoNotFound, "searchErrorInfoNotFound", "存在しない情報カード(ID:%1$s)が指定されています。") \
    X(searchErrorStartNotFound, "searchErrorStartNotFound", "存在しないスタートコンテント「%1$s」が指定されています。") \
    X(searchErrorLinkIdSkillNotFound, "searchErrorLinkIdSkillNotFound", "参照先の特殊技能カード(ID:%1$s)が見つかりません。") \
    X(searchErrorLinkIdItemNotFound, "searchErrorLinkIdItemNotFound", "参照先のアイテムカード(ID:%1$s)が見つかりません。") \
    X(searchErrorLinkIdBeastNotFound, "searchErrorLinkIdBeastNotFound", "参照先の召喚獣カード(ID:%1$s)が見つかりません。") \
    X(searchErrorEmptyFile, "searchErrorEmptyFile", "ファイルの内容が存在しません。") \
    X(searchErrorDupFile, "searchErrorDupFile", "同一のファイル「%1$s」が存在します。") \
    X(searchErrorSourceIsTarget, "searchErrorSourceIsTarget", "ソース変数とターゲット変数が同一です。") \
    X(searchErrorSystemCoupon, "searchErrorSystemCoupon", "「%1$s」はシステムクーポンとして扱われるため、正しく機能しない場合があります。") \
    X(searchErrorSystemVariable, "searchErrorSystemVariable", "「%1$s」はシステム変数として扱われるため、正しく機能しない場合があります。") \
    X(searchErrorKeyCodeMatchingAll, "searchErrorKeyCodeMatchingAll", "「MatchingType=All」はシステムで使用されているキーコードのため、正しく機能しない場合があります。") \
    X(searchErrorBranchRoundInArea, "searchErrorBranchRoundInArea", "ラウンド分岐がエリアイベントで使用されています。") \
    X(searchErrorNoArea, "searchErrorNoArea", "エリアが指定されていません。") \
    X(searchErrorNoBattle, "searchErrorNoBattle", "バトルが指定されていません。") \
    X(searchErrorNoPackage, "searchErrorNoPackage", "パッケージが指定されていません。") \
    X(searchErrorNoSkill, "searchErrorNoSkill", "特殊技能カードが指定されていません。") \
    X(searchErrorNoItem, "searchErrorNoItem", "アイテムカードが指定されていません。") \
    X(searchErrorNoCast, "searchErrorNoCast", "キャストカードが指定されていません。") \
    X(searchErrorNoBeast, "searchErrorNoBeast", "召喚獣カードが指定されていません。") \
    X(searchErrorNoInfo, "searchErrorNoInfo", "情報カードが指定されていません。") \
    X(searchErrorNoFlag, "searchErrorNoFlag", "フラグが指定されていません。") \
    X(searchErrorNoStep, "searchErrorNoStep", "ステップが指定されていません。") \
    X(searchErrorNoVariant, "searchErrorNoVariant", "コモンが指定されていません。") \
    X(searchErrorNoExpressionTarget, "searchErrorNoExpressionTarget", "代入先が指定されていません。") \
    X(searchErrorNoSoundPath, "searchErrorNoSoundPath", "効果音が指定されていません。") \
    X(searchErrorNoCoupon, "searchErrorNoCoupon", "クーポンが指定されていません。") \
    X(searchErrorNoGossip, "searchErrorNoGossip", "ゴシップが指定されていません。") \
    X(searchErrorNoCompleteStamp, "searchErrorNoCompleteStamp", "シナリオ名が指定されていません。") \
    X(searchErrorNoKeyCode, "searchErrorNoKeyCode", "キーコードが指定されていません。") \
    X(searchErrorNoCellName, "searchErrorNoCellName", "セル名称が指定されていません。") \
    X(searchErrorNoCardGroup, "searchErrorNoCardGroup", "カードグループが指定されていません。") \
    X(searchErrorNoExpression, "searchErrorNoExpression", "式がありません。") \
    X(searchOpenDialog, "searchOpenDialog", "検索結果へジャンプする時、ダイアログを開く(&J)") \
    X(searchOpenDialogHint, "searchOpenDialogHint", "Shiftキーで一時的に切り替え") \
    X(dlgTitContent, "dlgTitContent", "イベントの設定 [ %1$s ]") \
    X(contentInitializer, "contentInitializer", "コンテントの初期値") \
    X(baseInitializers, "baseInitializers", "基本") \
    X(editedInitializer, "editedInitializer", "*") \
    X(afterClear, "afterClear", "シナリオ終了後") \
    X(afterClearEndMark, "afterClearEndMark", "シナリオに済印を付ける") \
    X(afterClearNoEndMark, "afterClearNoEndMark", "何もしない") \
    X(message, "message", "メッセージ") \
    X(couponName, "couponName", "クーポン名") \
    X(couponValue, "couponValue", "得点") \
    X(couponValueRange, "couponValueRange", "(%1$s～%2$s)") \
    X(resultType, "resultType", "成功条件") \
    X(resultTypeNormal, "resultTypeNormal", "所有している") \
    X(resultTypeInvert, "resultTypeInvert", "所有していない") \
    X(matchingConditionForKeyCode, "matchingConditionForKeyCode", "カードのマッチング") \
    X(matchingConditionForKeyCodeNormalDesc, "matchingConditionForKeyCodeNormalDesc", "キーコードを含む場合に成功") \
    X(matchingConditionForKeyCodeInvertDesc, "matchingConditionForKeyCodeInvertDesc", "キーコードを含まない場合に成功") \
    X(matchedCard, "matchedCard", "マッチしたカード") \
    X(resultTypeNormalDesc, "resultTypeNormalDesc", "所有している場合に成功") \
    X(resultTypeInvertDesc, "resultTypeInvertDesc", "所有していない場合に成功") \
    X(resultTypeNormalForStatus, "resultTypeNormalForStatus", "状態を持つ") \
    X(resultTypeInvertForStatus, "resultTypeInvertForStatus", "状態を持たない") \
    X(resultTypeInvertForAbility, "resultTypeInvertForAbility", "能力不足時に成功する") \
    X(resultTypeNormalForCondition, "resultTypeNormalForCondition", "条件に合う") \
    X(resultTypeInvertForCondition, "resultTypeInvertForCondition", "条件に合わない") \
    X(range, "range", "適用範囲") \
    X(couponForRange, "couponForRange", "適用範囲の称号") \
    X(matchingType, "matchingType", "マッチングタイプ") \
    X(gossipName, "gossipName", "ゴシップ名") \
    X(endName, "endName", "シナリオ名") \
    X(cardType, "cardType", "カードの種類") \
    X(keyCode, "keyCode", "キーコード") \
    X(talker, "talker", "話者") \
    X(initValue, "initValue", "初期点") \
    X(toneCoupons, "toneCoupons", "口調条件") \
    X(valued, "valued", "評価条件") \
    X(valuedTalkerMaxIsLess0, "valuedTalkerMaxIsLess0", "最大値 = %1$s (発言しない)\n最小値 = %2$s (発言しない)") \
    X(valuedTalkerMaxMin, "valuedTalkerMaxMin", "最大値 = %1$s\n最小値 = %2$s") \
    X(valuedTalkerMaxMinLess0, "valuedTalkerMaxMinLess0", "最大値 = %1$s\n最小値 = %2$s (発言しない)") \
    X(selectMemberValuedMaxIsLess0, "selectMemberValuedMaxIsLess0", "最大値 = %1$s (選択失敗)\n最小値 = %2$s (選択失敗)") \
    X(selectMemberValuedMaxMin, "selectMemberValuedMaxMin", "最大値 = %1$s\n最小値 = %2$s") \
    X(selectMemberValuedMaxMinLess0, "selectMemberValuedMaxMinLess0", "最大値 = %1$s\n最小値 = %2$s (選択失敗)") \
    X(cellName, "cellName", "対象セル名称") \
    X(targetCardGroup, "targetCardGroup", "対象カードグループ") \
    X(moveCell, "moveCell", "位置の変更") \
    X(moveCard, "moveCard", "位置の変更") \
    X(resizeCell, "resizeCell", "サイズの変更") \
    X(horizontalValue, "horizontalValue", "横の値") \
    X(verticalValue, "verticalValue", "縦の値") \
    X(selectFoundCard, "selectFoundCard", "見つかったカードを選択状態にする") \
    X(selectTalker, "selectTalker", "話者を選択状態にする") \
    X(invertResult, "invertResult", "条件に合わない場合に成功とする") \
    X(matchingCondition, "matchingCondition", "マッチ条件") \
    X(changeLayer, "changeLayer", "レイヤを変更する") \
    X(changeScale, "changeScale", "拡大率を変更する") \
    X(matchingConditionNameHas, "matchingConditionNameHas", "保有") \
    X(matchingConditionNameHasNot, "matchingConditionNameHasNot", "不保有") \
    X(matchingConditionKeyCodeDescHas, "matchingConditionKeyCodeDescHas", "指定キーコードを持つ") \
    X(matchingConditionKeyCodeDescHasNot, "matchingConditionKeyCodeDescHasNot", "指定キーコードを持たない") \
    X(canNotGetMusicLength, "canNotGetMusicLength", "SDL方式では音声の長さを取得できません") \
    X(couponHide, "couponHide", "隠蔽クーポン") \
    X(couponTypeDescNormal, "couponTypeDescNormal", "ノーマル") \
    X(couponTypeDescHide, "couponTypeDescHide", "隠蔽") \
    X(couponTypeDescSystem, "couponTypeDescSystem", "システム") \
    X(couponTypeDescDur, "couponTypeDescDur", "時限") \
    X(couponTypeDescDurBattle, "couponTypeDescDurBattle", "戦闘中時限") \
    X(couponTypeLongDescNormal, "couponTypeLongDescNormal", "ノーマル") \
    X(couponTypeLongDescHide, "couponTypeLongDescHide", "[%1$s...] 隠蔽(称号一覧で非表示)") \
    X(couponTypeLongDescSystem, "couponTypeLongDescSystem", "[%1$s...] システム") \
    X(couponTypeLongDescDur, "couponTypeLongDescDur", "[%1$s...] 時限(点数分の時間経過及びシナリオ終了時に消滅)") \
    X(couponTypeLongDescDurBattle, "couponTypeLongDescDurBattle", "[%1$s...] 戦闘中時限(点数分の時間経過及び戦闘終了時に消滅)") \
    X(couponTypeShortDescNormal, "couponTypeShortDescNormal", "一般") \
    X(couponTypeShortDescHide, "couponTypeShortDescHide", "隠蔽") \
    X(couponTypeShortDescSystem, "couponTypeShortDescSystem", "特殊") \
    X(couponTypeShortDescDur, "couponTypeShortDescDur", "時限") \
    X(couponTypeShortDescDurBattle, "couponTypeShortDescDurBattle", "戦闘") \
    X(couponTypeNameNormal, "couponTypeNameNormal", "通常") \
    X(couponTypeNameHide, "couponTypeNameHide", "隠蔽") \
    X(couponTypeNameSystem, "couponTypeNameSystem", "システム") \
    X(couponTypeNameDur, "couponTypeNameDur", "時限") \
    X(couponTypeNameDurBattle, "couponTypeNameDurBattle", "戦時") \
    X(matchingTypeNameAnd, "matchingTypeNameAnd", "全てに一致") \
    X(matchingTypeNameOr, "matchingTypeNameOr", "どれか一つに一致") \
    X(imageMessage, "imageMessage", "イメージ付きメッセージ") \
    X(noImageMessage, "noImageMessage", "イメージ無しメッセージ") \
    X(singleLineMessage, "singleLineMessage", "単行メッセージ") \
    X(spCharsTitle, "spCharsTitle", "特殊文字") \
    X(colorW, "colorW", "デフォルト(&W)") \
    X(colorR, "colorR", "赤色(&R)") \
    X(colorB, "colorB", "青色(&B)") \
    X(colorG, "colorG", "緑色(&G)") \
    X(colorY, "colorY", "黄色(&Y)") \
    X(colorO, "colorO", "橙色(&O)") \
    X(colorP, "colorP", "紫色(&P)") \
    X(colorL, "colorL", "明るい灰色(&L)") \
    X(colorD, "colorD", "暗い灰色(&D)") \
    X(scTalkerNameSelected, "scTalkerNameSelected", "選択メンバ名(#M)") \
    X(scTalkerNameUnselected, "scTalkerNameUnselected", "選択外ランダムメンバ名(#U)") \
    X(scTalkerNameRandom, "scTalkerNameRandom", "ランダムメンバ名(#R)") \
    X(scTalkerNameCard, "scTalkerNameCard", "選択カード名(#C)") \
    X(scTalkerNameNarration, "scTalkerNameNarration", "話者無し") \
    X(scTalkerNameImage, "scTalkerNameImage", "画像") \
    X(scTalkerNameValued, "scTalkerNameValued", "評価メンバ") \
    X(scRef, "scRef", "話者(#I)") \
    X(scTeam, "scTeam", "チーム名(#T)") \
    X(scYado, "scYado", "宿屋名(#Y)") \
    X(selectedPlayerCardNumber, "selectedPlayerCardNumber", "選択メンバ番号($??SelectedPlayer$)") \
    X(playerCardName, "playerCardName", "プレイヤーカード名%1$s($??Player%1$s$)") \
    X(addMsgRefFlag, "addMsgRefFlag", "フラグ参照の追加") \
    X(addMsgRefStep, "addMsgRefStep", "ステップ参照の追加") \
    X(addMsgRefVariant, "addMsgRefVariant", "コモン参照の追加") \
    X(addMsgRefImageFont, "addMsgRefImageFont", "画像参照の追加") \
    X(setTalkerCoupon, "setTalkerCoupon", "追加") \
    X(messagePreview, "messagePreview", "プレビュー(&P)") \
    X(dlgTitMessagePreview, "dlgTitMessagePreview", "プレビュー") \
    X(messageVarKindColumn, "messageVarKindColumn", "状態変数") \
    X(messageVarValueColumn, "messageVarValueColumn", "サンプル値") \
    X(warningSPCharsInSelections, "warningSPCharsInSelections", "選択肢内の特殊文字は、CardWirth 1.50より前のバージョンでは展開されません。") \
    X(warningSelectedPlayerCardNumber, "warningSelectedPlayerCardNumber", "選択メンバ番号($??SelectedPlayer$)の表示は、Wsn.2以降の形式のシナリオでしか行えません。") \
    X(warningPlayerCardName, "warningPlayerCardName", "プレイヤーカード名($??Player%1$s$)の表示は、Wsn.2以降の形式のシナリオでしか行えません。") \
    X(warningUnknownSystemValue, "warningUnknownSystemValue", "未知のシステム値(%1$s)です。") \
    X(warningSelectedPlayerValue, "warningSelectedPlayerValue", "選択メンバ番号の代入は、Wsn.2以降の形式のシナリオでしか行えません。") \
    X(transition, "transition", "背景切替方式") \
    X(transitionNameDefault, "transitionNameDefault", "プレイヤーの設定を使用") \
    X(transitionNameNone, "transitionNameNone", "アニメーション無し") \
    X(transitionNameBlinds, "transitionNameBlinds", "短冊(スレッド)式") \
    X(transitionNamePixelDissolve, "transitionNamePixelDissolve", "ドット置換(シェーブ)式") \
    X(transitionNameFade, "transitionNameFade", "色置換(フェード)式") \
    X(transitionSpeed, "transitionSpeed", "背景切替ウェイト") \
    X(cardSpeed, "cardSpeed", "アニメーション") \
    X(overrideAnimationSpeed, "overrideAnimationSpeed", "速度を指定する") \
    X(overrideCardAnimationSpeed, "overrideCardAnimationSpeed", "カードのアニメーション速度を指定する") \
    X(forceOverrideCardAnimationSpeed, "forceOverrideCardAnimationSpeed", "カード本体の設定より優先する") \
    X(warningCardAnimationSpeed, "warningCardAnimationSpeed", "カードのアニメーション速度の指定はWsn.4以降の形式のシナリオでしか行えません。") \
    X(waitName, "waitName", "空白時間(0.1秒単位)") \
    X(moneyName, "moneyName", "金額") \
    X(randomName, "randomName", "確率(%)") \
    X(partyNumName, "partyNumName", "パーティの人数") \
    X(judgeTarget, "judgeTarget", "判定対象") \
    X(flag, "flag", "フラグ") \
    X(step, "step", "ステップ") \
    X(variant, "variant", "コモン") \
    X(flagValue, "flagValue", "値") \
    X(stepValue, "stepValue", "段階") \
    X(spChars, "spChars", "特殊文字") \
    X(warningLimitForNumberOfVariant, "warningLimitForNumberOfVariant", "数値の桁数が大きすぎるため、ビット数の制限による誤差が発生する可能性があります。") \
    X(expandSPChars, "expandSPChars", "特殊文字を展開する") \
    X(expandSPCharsHint, "expandSPCharsHint", "#M = 選択メンバ名, #R = ランダムメンバ名 ...") \
    X(selectMember, "selectMember", "選択対象") \
    X(activeMember, "activeMember", "動けるメンバから選択") \
    X(allMember, "allMember", "パーティ全員から選択") \
    X(selectMethod, "selectMethod", "選択方法") \
    X(manualMethod, "manualMethod", "手動で選択") \
    X(randomMethod, "randomMethod", "ランダムで選択") \
    X(valuedMethod, "valuedMethod", "評価条件で選択") \
    X(judgeSleep, "judgeSleep", "眠り判定") \
    X(sleepDisabled, "sleepDisabled", "睡眠・呪縛者無効") \
    X(sleepEnabled, "sleepEnabled", "睡眠・呪縛者有効") \
    X(selectedLevel, "selectedLevel", "現在選択中のメンバ") \
    X(allMemberLevel, "allMemberLevel", "パーティ全員の平均値") \
    X(judgeLevel, "judgeLevel", "判定レベル") \
    X(judgeState, "judgeState", "判定状態") \
    X(stateHint, "stateHint", "ヒント") \
    X(cardNumber, "cardNumber", "枚数") \
    X(cardAllDelete, "cardAllDelete", "全て削除する") \
    X(cardEventRange, "cardEventRange", "適用範囲") \
    X(transitionType, "transitionType", "背景切替方式") \
    X(substituteSource, "flagSubstituteSource", "ソース変数(代入元)") \
    X(substituteTarget, "flagSubstituteTarget", "ターゲット変数(代入先)") \
    X(cmpSource, "flagCmpSource", "ソース変数(比較元)") \
    X(cmpTarget, "flagCmpTarget", "ターゲット変数(比較先)") \
    X(randomSelectHasLevel, "randomSelectHasLevel", "レベルを限定") \
    X(randomSelectHasStatus, "randomSelectHasStatus", "状態を限定") \
    X(stepValueIs, "stepValueIs", "ステップ「%1$s」が[%2$s]") \
    X(roundCondition, "roundCondition", "ラウンド条件") \
    X(roundIs, "roundIs", "バトルが") \
    X(roundCmpIs, "roundCmpIs", "ラウンド") \
    X(doAnime, "doAnime", "JPY1アニメーションを実行する") \
    X(ignoreEffectBooster, "ignoreEffectBooster", "エフェクトブースター関係のセルを無視する") \
    X(selectionColumns, "selectionColumns", "選択肢の列数") \
    X(centeringX, "centeringX", "横方向に中央寄せ") \
    X(centeringXOn, "centeringXOn", "横の中央寄せ") \
    X(centeringY, "centeringY", "縦方向に中央寄せ") \
    X(centeringYOn, "centeringYOn", "縦の中央寄せ") \
    X(centeringXYOn, "centeringXYOn", "縦横の中央寄せ") \
    X(boundaryCheck, "boundaryCheck", "禁則処理") \
    X(boundaryCheckOn, "boundaryCheckOn", "禁則処理あり") \
    X(boundaryCheckDesc, "boundaryCheckDesc", "禁則処理の結果は将来変化するかもしれません。\nメッセージの行数には余裕を持たせておく事をお勧めします。") \
    X(consumeCard, "consumeCard", "イベントを発火させたカードの使用回数を消費する") \
    X(environmentStatus, "environmentStatus", "状況の設定") \
    X(environmentStatusNameNotSet, "environmentStatusNameNotSet", "設定しない") \
    X(environmentStatusNameEnable, "environmentStatusNameEnable", "使用可能にする") \
    X(environmentStatusNameDisable, "environmentStatusNameDisable", "使用不能にする") \
    X(backpack, "backpack", "荷物袋") \
    X(environmentStatusNameEnabledNotSet, "environmentStatusNameEnabledNotSet", "設定しない") \
    X(environmentStatusNameEnabledEnable, "environmentStatusNameEnabledEnable", "有効にする") \
    X(environmentStatusNameEnabledDisable, "environmentStatusNameEnabledDisable", "無効にする") \
    X(gameOver, "gameOver", "敗北・ゲームオーバー") \
    X(runAway, "runAway", "逃走") \
    X(warningEnvironmentEnabled, "warningEnvironmentEnabled", "%1$sの有効・無効はWsn.%2$s以降の形式のシナリオでしか設定できません。") \
    X(expression, "expression", "式") \
    X(operators, "operators", "演算子:") \
    X(operatorList, "operatorList", "加算: A + B, 減算: A - B, 乗算: A * B, 除算: A / B, 剰余: A % B, 文字列連結: A ~ B, 一致: A = B, 不一致: A <> B, 大小比較: A <= B, A >= B, A < B, A > B, 論理演算: A and B, A or B, 真偽値反転: not A") \
    X(expressionTarget, "expressionTarget", "設定対象") \
    X(selectFunction, "selectFunction", "関数") \
    X(functionDeclaration, "functionDeclaration", "書式") \
    X(functionDescription, "functionDescription", "解説") \
    X(functionExample, "functionExample", "例") \
    X(insertFlagIntoExpression, "insertFlagIntoExpression", "フラグ名または関数の挿入") \
    X(insertFlagPath, "insertFlagPath", "フラグ名の挿入") \
    X(insertStepIntoExpression, "insertStepIntoExpression", "ステップ名または関数の挿入") \
    X(insertStepPath, "insertStepPath", "ステップ名の挿入") \
    X(insertVariantIntoExpression, "insertVariantIntoExpression", "コモン名または参照の挿入") \
    X(insertVariantPath, "insertVariantPath", "コモン名の挿入") \
    X(insertVariantReference, "insertVariantReference", "コモン参照の挿入") \
    X(insertFunctionIntoExpression, "insertFunctionIntoExpression", "関数の挿入") \
    X(insertFunctionIntoExpressionButton, "insertFunctionIntoExpressionButton", "式へ挿入(&I)") \
    X(functionArgumentName, "functionArgumentName", "引数") \
    X(functionArgumentType, "functionArgumentType", "タイプ") \
    X(functionArgumentValue, "functionArgumentValue", "値") \
    X(constantValue, "constantValue", "直接入力") \
    X(variantReference, "variantReference", "コモン参照") \
    X(noArgument, "noArgument", "指定しない") \
    X(variableType, "variableType", "状態変数タイプ") \
    X(variableTypeNameFlag, "variableTypeNameFlag", "フラグ") \
    X(variableTypeNameStep, "variableTypeNameStep", "ステップ") \
    X(variableTypeNameVariant, "variableTypeNameVariant", "コモン") \
    X(functionCategory, "functionCategory", "分類") \
    X(functionSearch, "functionSearch", "関数検索") \
    X(calculateExpressionResult, "calculateExpressionResult", "演算結果を得る") \
    X(expressionResultUnavailable, "expressionResultUnavailable", "演算結果を取得できません。") \
    X(allFunctions, "allFunctions", "全て") \
    X(functionCategoryNameStringOperation, "functionCategoryNameStringOperation", "文字列操作") \
    X(functionCategoryNameNumberOperation, "functionCategoryNameNumberOperation", "数値操作") \
    X(functionCategoryNameListOperation, "functionCategoryNameListOperation", "リスト操作") \
    X(functionCategoryNameConversion, "functionCategoryNameConversion", "型変換") \
    X(functionCategoryNameVariableOperation, "functionCategoryNameVariableOperation", "状態変数") \
    X(functionCategoryNameCastInformation, "functionCategoryNameCastInformation", "キャラクター情報") \
    X(functionCategoryNameCardInformation, "functionCategoryNameCardInformation", "カード情報") \
    X(functionCategoryNameCouponInformation, "functionCategoryNameCouponInformation", "称号情報") \
    X(functionCategoryNamePlayingInformation, "functionCategoryNamePlayingOperation", "プレイ情報") \
    X(functionCategoryNameEtc, "functionCategoryNameEtc", "その他") \
    X(blendModeNameNormal, "blendModeNameNormal", "通常") \
    X(blendModeNameMask, "blendModeNameMask", "マスク") \
    X(blendModeNameAdd, "blendModeNameAdd", "加算") \
    X(blendModeNameSubtract, "blendModeNameSubtract", "減算") \
    X(blendModeNameMultiply, "blendModeNameMultiply", "乗算") \
    X(gradientDirNameNone, "gradientDirNameNone", "グラデーション無し") \
    X(gradientDirNameLeftToRight, "gradientDirNameLeftToRight", "左から右へ") \
    X(gradientDirNameTopToBottom, "gradientDirNameTopToBottom", "上から下へ") \
    X(moveColors, "moveColors", "色") \
    X(borderingTypeNameNone, "borderingTypeNameNone", "縁取り無し") \
    X(borderingTypeNameOutline, "borderingTypeNameOutline", "形式1") \
    X(borderingTypeNameInline, "borderingTypeNameInline", "形式2") \
    X(coordinateTypeNameNone, "coordinateTypeNameNone", "変更無し") \
    X(coordinateTypeNameAbsolute, "coordinateTypeNameAbsolute", "直接指定") \
    X(coordinateTypeNameRelative, "coordinateTypeNameRelative", "現在値基準") \
    X(coordinateTypeNamePercentage, "coordinateTypeNamePercentage", "パーセンテージ") \
    X(startAction, "startAction", "戦闘行動開始タイミング") \
    X(startActionNameNow, "startActionNameNow", "すぐに行動") \
    X(startActionNameCurrentRound, "startActionNameCurrentRound", "加入ラウンドから行動") \
    X(startActionNameNextRound, "startActionNameNextRound", "次のラウンドから行動") \
    X(igniteTitle, "igniteTitle", "イベント発火の有無") \
    X(ignite, "ignite", "効果適用時にキーコードイベントと死亡イベントを発火させる") \
    X(igniteTrue, "igniteTrue", "イベント発火あり") \
    X(igniteFalse, "igniteFalse", "イベント発火無し") \
    X(igniteHint, "igniteHint", "※ 発火したイベント内で選択メンバが変更される可能性があります") \
    X(refAbilityTitle, "refAbilityTitle", "能力参照") \
    X(refAbility, "refAbility", "成功率と効果値の計算に選択中のメンバのレベルと能力を使用する") \
    X(absorbSettings, "absorbSettings", "吸収効果") \
    X(absorbToSelected, "absorbToSelected", "選択メンバを吸収者にする") \
    X(evtArrow, "evtArrow", "イベント編集") \
    X(allExpanded, "allExpanded", "全て開く/全て閉じる(&E)") \
    X(evtAutoOpen, "evtAutoOpen", "配置と同時に編集") \
    X(evtInsertFirst, "evtInsertFirst", "他の子コンテントより前に配置") \
    X(eventTreeViewHint, "eventTreeViewHint", "Shift+配置: 選択コンテントの上へ挿入, Alt+配置: 「配置と同時に編集」設定を反転, Ctrl+配置: 「他の子コンテントより前に配置」設定を反転") \
    X(contentNameForWarning, "contentNameForWarning", "コンテント名") \
    X(contentNameStart, "contentNameStart", "スタート") \
    X(contentNameStartBattle, "contentNameStartBattle", "バトル開始") \
    X(contentNameEnd, "contentNameEnd", "シナリオクリア") \
    X(contentNameEndBadEnd, "contentNameEndBadEnd", "敗北・ゲームオーバー") \
    X(contentNameChangeArea, "contentNameChangeArea", "エリア移動") \
    X(contentNameChangeBgImage, "contentNameChangeBgImage", "背景変更") \
    X(contentNameEffect, "contentNameEffect", "効果") \
    X(contentNameEffectBreak, "contentNameEffectBreak", "効果中断") \
    X(contentNameLinkStart, "contentNameLinkStart", "スタートへのリンク") \
    X(contentNameLinkPackage, "contentNameLinkPackage", "パッケージへのリンク") \
    X(contentNameTalkMessage, "contentNameTalkMessage", "メッセージ") \
    X(contentNameTalkDialog, "contentNameTalkDialog", "セリフ") \
    X(contentNamePlayBgm, "contentNamePlayBgm", "BGM変更") \
    X(contentNamePlaySound, "contentNamePlaySound", "効果音") \
    X(contentNameWait, "contentNameWait", "空白時間挿入") \
    X(contentNameElapseTime, "contentNameElapseTime", "時間経過") \
    X(contentNameCallStart, "contentNameCallStart", "スタートの呼び出し") \
    X(contentNameCallPackage, "contentNameCallPackage", "パッケージの呼び出し") \
    X(contentNameBranchFlag, "contentNameBranchFlag", "フラグ分岐") \
    X(contentNameBranchMultiStep, "contentNameBranchMultiStep", "ステップ多岐分岐") \
    X(contentNameBranchStep, "contentNameBranchStep", "ステップ上下分岐") \
    X(contentNameBranchSelect, "contentNameBranchSelect", "メンバ選択分岐") \
    X(contentNameBranchAbility, "contentNameBranchAbility", "能力判定分岐") \
    X(contentNameBranchRandom, "contentNameBranchRandom", "ランダム分岐") \
    X(contentNameBranchLevel, "contentNameBranchLevel", "レベル判定分岐") \
    X(contentNameBranchStatus, "contentNameBranchStatus", "状態判定分岐") \
    X(contentNameBranchPartyNumber, "contentNameBranchPartyNumber", "人数判定分岐") \
    X(contentNameBranchArea, "contentNameBranchArea", "エリア分岐") \
    X(contentNameBranchBattle, "contentNameBranchBattle", "バトル分岐") \
    X(contentNameBranchIsBattle, "contentNameBranchIsBattle", "バトル判定分岐") \
    X(contentNameBranchCast, "contentNameBranchCast", "キャスト存在分岐") \
    X(contentNameBranchItem, "contentNameBranchItem", "アイテム所持分岐") \
    X(contentNameBranchSkill, "contentNameBranchSkill", "スキル所持分岐") \
    X(contentNameBranchInfo, "contentNameBranchInfo", "情報所持分岐") \
    X(contentNameBranchBeast, "contentNameBranchBeast", "召喚獣存在分岐") \
    X(contentNameBranchMoney, "contentNameBranchMoney", "所持金分岐") \
    X(contentNameBranchCoupon, "contentNameBranchCoupon", "クーポン分岐") \
    X(contentNameBranchCompleteStamp, "contentNameBranchCompleteStamp", "終了シナリオ分岐") \
    X(contentNameBranchGossip, "contentNameBranchGossip", "ゴシップ分岐") \
    X(contentNameSetFlag, "contentNameSetFlag", "フラグ変更") \
    X(contentNameSetStep, "contentNameSetStep", "ステップ変更") \
    X(contentNameSetStepUp, "contentNameSetStepUp", "ステップ増加") \
    X(contentNameSetStepDown, "contentNameSetStepDown", "ステップ減少") \
    X(contentNameReverseFlag, "contentNameReverseFlag", "フラグ反転") \
    X(contentNameCheckFlag, "contentNameCheckFlag", "フラグ判定") \
    X(contentNameGetCast, "contentNameGetCast", "キャスト加入") \
    X(contentNameGetItem, "contentNameGetItem", "アイテム入手") \
    X(contentNameGetSkill, "contentNameGetSkill", "スキル取得") \
    X(contentNameGetInfo, "contentNameGetInfo", "情報入手") \
    X(contentNameGetBeast, "contentNameGetBeast", "召喚獣獲得") \
    X(contentNameGetMoney, "contentNameGetMoney", "所持金増加") \
    X(contentNameGetCoupon, "contentNameGetCoupon", "クーポン取得") \
    X(contentNameGetCompleteStamp, "contentNameGetCompleteStamp", "終了シナリオ設定") \
    X(contentNameGetGossip, "contentNameGetGossip", "ゴシップ追加") \
    X(contentNameLoseCast, "contentNameLoseCast", "キャスト離脱") \
    X(contentNameLoseItem, "contentNameLoseItem", "アイテム喪失") \
    X(contentNameLoseSkill, "contentNameLoseSkill", "スキル喪失") \
    X(contentNameLoseInfo, "contentNameLoseInfo", "情報喪失") \
    X(contentNameLoseBeast, "contentNameLoseBeast", "召喚獣消去") \
    X(contentNameLoseMoney, "contentNameLoseMoney", "所持金減少") \
    X(contentNameLoseCoupon, "contentNameLoseCoupon", "クーポン削除") \
    X(contentNameLoseCompleteStamp, "contentNameLoseCompleteStamp", "終了シナリオ削除") \
    X(contentNameLoseGossip, "contentNameLoseGossip", "ゴシップ削除") \
    X(contentNameShowParty, "contentNameShowParty", "パーティ表示") \
    X(contentNameHideParty, "contentNameHideParty", "パーティ隠蔽") \
    X(contentNameRedisplay, "contentNameRedisplay", "画面再構築") \
    X(contentNameSubstituteStep, "contentNameSubstituteStep", "ステップ代入") \
    X(contentNameSubstituteFlag, "contentNameSubstituteFlag", "フラグ代入") \
    X(contentNameBranchStepCmp, "contentNameBranchStepCmp", "ステップ比較分岐") \
    X(contentNameBranchFlagCmp, "contentNameBranchFlagCmp", "フラグ比較分岐") \
    X(contentNameBranchRandomSelect, "contentNameBranchRandomSelect", "ランダム選択") \
    X(contentNameBranchKeyCode, "contentNameBranchKeyCode", "キーコード所持分岐") \
    X(contentNameCheckStep, "contentNameCheckStep", "ステップ判定") \
    X(contentNameBranchRound, "contentNameBranchRound", "ラウンド分岐") \
    X(contentNameMoveBgImage, "contentNameMoveBgImage", "背景再配置") \
    X(contentNameReplaceBgImage, "contentNameReplaceBgImage", "背景置換") \
    X(contentNameLoseBgImage, "contentNameLoseBgImage", "背景削除") \
    X(contentNameBranchMultiCoupon, "contentNameBranchMultiCoupon", "クーポン多岐分岐") \
    X(contentNameBranchMultiRandom, "contentNameBranchMultiRandom", "ランダム多岐分岐") \
    X(contentNameMoveCard, "contentNameMoveCard", "カード再配置") \
    X(contentNameChangeEnvironment, "contentNameChangeEnvironment", "状況設定") \
    X(contentNameBranchVariant, "contentNameBranchVariant", "コモン分岐") \
    X(contentNameSetVariant, "contentNameSetVariant", "コモン設定") \
    X(contentNameCheckVariant, "contentNameCheckVariant", "コモン判定") \
    X(contentNameCustomXmlContent, "contentNameCustomXmlContent", "XMLコンテンツ") \
    X(contentDescStart, "contentDescStart", "イベントツリーの起点です。") \
    X(contentDescStartBattle, "contentDescStartBattle", "イベントを終了して任意のバトルを開始します。バトル開始後もイベントを続けたい場合は、開始するバトルの「バトル開始」イベントを作成してください。") \
    X(contentDescEnd, "contentDescEnd", "シナリオを終了します。済印をつけた場合、このシナリオは解決済みとなって再プレイできなくなります。") \
    X(contentDescEndBadEnd, "contentDescEndBadEnd", "ゲームオーバーにしてシナリオを終了します。バトル中に実行した場合は、敗北扱いとなります。") \
    X(contentDescChangeArea, "contentDescChangeArea", "イベントを終了して任意のエリアへ移動します。移動後もイベントを続けたい場合は、移動先エリアの「到着」イベントを作成してください。") \
    X(contentDescChangeBgImage, "contentDescChangeBgImage", "背景セルを追加もしくは全て交換し、背景を再描画します。画面全体を覆う背景イメージを最背面に配置した場合、これまで配置されていた背景セルは全て消去されます。") \
    X(contentDescEffect, "contentDescEffect", "ダメージ・回復・状態変更などの効果を任意のキャラクタに対して発生させます。") \
    X(contentDescEffectBreak, "contentDescEffectBreak", "イベントを終了します。使用時イベント中に使った場合は、カードの使用自体が中止されます。") \
    X(contentDescLinkStart, "contentDescLinkStart", "現在のイベントツリーの実行を終了し、任意のイベントツリーを実行します。") \
    X(contentDescLinkPackage, "contentDescLinkPackage", "現在のイベントの実行を終了し、任意のパッケージイベントを実行します。") \
    X(contentDescTalkMessage, "contentDescTalkMessage", "メッセージを表示します。複数の後続コンテントがある場合は、選択肢が表示されます。") \
    X(contentDescTalkDialog, "contentDescTalkDialog", "メッセージを表示します。話者の所持クーポンによってメッセージの内容を変える事ができます。複数の後続コンテントがある場合は、選択肢が表示されます。") \
    X(contentDescPlayBgm, "contentDescPlayBgm", "BGMを再生もしくは停止します。") \
    X(contentDescPlaySound, "contentDescPlaySound", "効果音を再生します。") \
    X(contentDescWait, "contentDescWait", "演出などのために、何も起こらない待ち時間を任意秒数入れます。") \
    X(contentDescElapseTime, "contentDescElapseTime", "ゲーム内時間を経過させ、効果時間の減少や毒のダメージなどを発生させます。バトルラウンドは経過しません。") \
    X(contentDescCallStart, "contentDescCallStart", "任意のイベントツリーを実行し、実行後に現在のイベントツリーへ処理を戻します。") \
    X(contentDescCallPackage, "contentDescCallPackage", "任意のパッケージイベントを実行し、実行後に現在のイベントへ処理を戻します。") \
    X(contentDescBranchFlag, "contentDescBranchFlag", "フラグの値によって処理を分岐させます。") \
    X(contentDescBranchMultiStep, "contentDescBranchMultiStep", "ステップの値がいくつかによって処理を分岐させます。") \
    X(contentDescBranchStep, "contentDescBranchStep", "ステップの値が任意の値以上・未満のどちらかによって処理を分岐させます。") \
    X(contentDescBranchSelect, "contentDescBranchSelect", "手動または自動でキャラクタを選択し、選択に成功したか、キャンセルもしくは選択に失敗したかによって処理を分岐させます。") \
    X(contentDescBranchAbility, "contentDescBranchAbility", "キャラクタの能力に応じた成功判定を行い、成功したか失敗したかによって処理を分岐させます。判定に成功したキャラクタは選択状態になります。") \
    X(contentDescBranchRandom, "contentDescBranchRandom", "任意の確率でランダムに処理を分岐させます。") \
    X(contentDescBranchLevel, "contentDescBranchLevel", "キャラクタのレベルが任意の値以上・未満のどちらかによって処理を分岐させます。") \
    X(contentDescBranchStatus, "contentDescBranchStatus", "キャラクタが任意の状態かそうでないかによって処理を分岐させます。") \
    X(contentDescBranchPartyNumber, "contentDescBranchPartyNumber", "パーティの人数が任意の数以上・未満のどちらかによって処理を分岐させます。") \
    X(contentDescBranchArea, "contentDescBranchArea", "現在どのエリアにいるかによって処理を分岐させます。バトル中の場合は、バトル直前にいたエリアによって判定を行います。") \
    X(contentDescBranchBattle, "contentDescBranchBattle", "現在どのバトルを実行中かによって処理を分岐させます。バトル中でない場合は、常に「その他」へ分岐します。") \
    X(contentDescBranchIsBattle, "contentDescBranchIsBattle", "現在バトル中かそうでないかによって処理を分岐させます。") \
    X(contentDescBranchCast, "contentDescBranchCast", "任意のキャストが同行中かそうでないかによって処理を分岐させます。") \
    X(contentDescBranchItem, "contentDescBranchItem", "任意のアイテムカードを所持しているかいないかによって処理を分岐させます。所持判定において、名前と解説が一致するカードは同一とみなされます。カードを所持しているキャラクタは選択状態になります。") \
    X(contentDescBranchSkill, "contentDescBranchSkill", "任意の特殊技能カードを所持しているかいないかによって処理を分岐させます。所持判定において、名前と解説が一致するカードは同一とみなされます。カードを所持しているキャラクタは選択状態になります。") \
    X(contentDescBranchInfo, "contentDescBranchInfo", "任意の情報カードを所持しているかいないかによって処理を分岐させます。") \
    X(contentDescBranchBeast, "contentDescBranchBeast", "任意の召喚獣を所持しているかいないかによって処理を分岐させます。所持判定において、名前と解説が一致するカードは同一とみなされます。カードを所持しているキャラクタは選択状態になります。") \
    X(contentDescBranchMoney, "contentDescBranchMoney", "パーティが任意の金額を所持しているかいないかによって処理を分岐させます。") \
    X(contentDescBranchCoupon, "contentDescBranchCoupon", "キャラクタが任意のクーポン(称号)を所持しているかいないかによって処理を分岐させます。クーポンを所持しているキャラクタは選択状態になります。") \
    X(contentDescBranchCompleteStamp, "contentDescBranchCompleteStamp", "任意のシナリオに済印がついているかいないかによって処理を分岐させます。") \
    X(contentDescBranchGossip, "contentDescBranchGossip", "任意のゴシップ(宿全体で所持するクーポン)があるかないかによって処理を分岐させます。") \
    X(contentDescSetFlag, "contentDescSetFlag", "任意のフラグの値を設定します。指定されたフラグを設定しているカードは、TRUEに設定した時に表示され、FALSEに設定した時に非表示にされます。背景セルの表示可否は内部的に変更され、「画面再構築」コンテントなどで背景の再描画を行った時に反映されます。") \
    X(contentDescSetStep, "contentDescSetStep", "任意のステップの値を設定します。") \
    X(contentDescSetStepUp, "contentDescSetStepUp", "任意のステップの値を1段階増加させます。すでに段階数の最大に達している場合は何もしません。") \
    X(contentDescSetStepDown, "contentDescSetStepDown", "任意のステップの値を1段階減少させます。すでに段階数の最小に達している場合は何もしません。") \
    X(contentDescReverseFlag, "contentDescReverseFlag", "任意のフラグの値がTRUEの時はFALSEに、FALSEの時はTRUEに設定します。") \
    X(contentDescCheckFlag, "contentDescCheckFlag", "任意のフラグの値がTRUEの時だけ後続のイベントを実行します。メッセージやセリフの選択肢として使用した場合は、TRUEの時だけ選択肢が表示されます。") \
    X(contentDescGetCast, "contentDescGetCast", "任意のキャストをパーティに同行させます。すでに同行している場合は何もしません。") \
    X(contentDescGetItem, "contentDescGetItem", "任意のアイテムカードを、キャラクタの所持カードや荷物袋に追加します。所持カードに追加しようとしてできなかった場合は、荷物袋に入ります。") \
    X(contentDescGetSkill, "contentDescGetSkill", "任意の特殊技能カードを、キャラクタの所持カードや荷物袋に追加します。所持カードに追加しようとしてできなかった場合は、荷物袋に入ります。") \
    X(contentDescGetInfo, "contentDescGetInfo", "任意の情報カードを入手します。すでに入手済みの場合は何もしません。") \
    X(contentDescGetBeast, "contentDescGetBeast", "任意の召喚獣カードを、キャラクタの所持カードや荷物袋に追加します。所持カードに追加しようとしてできなかった場合は、荷物袋に入ります。") \
    X(contentDescGetMoney, "contentDescGetMoney", "パーティの所持金を任意額だけ増加させます。") \
    X(contentDescGetCoupon, "contentDescGetCoupon", "任意のクーポン(称号)をキャラクタの経歴に追加します。すでに同一名称のクーポンがある場合は、上書きされます。") \
    X(contentDescGetCompleteStamp, "contentDescGetCompleteStamp", "任意のシナリオに済印をつけます。") \
    X(contentDescGetGossip, "contentDescGetGossip", "任意のゴシップ(宿全体で所持するクーポン)を追加します。") \
    X(contentDescLoseCast, "contentDescLoseCast", "任意のキャストの同行を解除します。") \
    X(contentDescLoseItem, "contentDescLoseItem", "任意のアイテムカードを所持カードや荷物袋から削除します。削除対象かどうかの判定において、名前と解説が一致するカードは同一と見なされます。") \
    X(contentDescLoseSkill, "contentDescLoseSkill", "任意の特殊技能カードを所持カードや荷物袋から削除します。削除対象かどうかの判定において、名前と解説が一致するカードは同一と見なされます。") \
    X(contentDescLoseInfo, "contentDescLoseInfo", "任意の情報カードを削除します。") \
    X(contentDescLoseBeast, "contentDescLoseBeast", "任意の召喚獣カードを所持カードや荷物袋から削除します。削除対象かどうかの判定において、名前と解説が一致するカードは同一と見なされます。") \
    X(contentDescLoseMoney, "contentDescLoseMoney", "パーティの所持金を任意額だけ減少させます。") \
    X(contentDescLoseCoupon, "contentDescLoseCoupon", "任意のクーポン(称号)をキャラクタの経歴から除去します。") \
    X(contentDescLoseCompleteStamp, "contentDescLoseCompleteStamp", "任意のシナリオの済印を除去します。") \
    X(contentDescLoseGossip, "contentDescLoseGossip", "任意のゴシップ(宿全体で所持するクーポン)を削除します。") \
    X(contentDescShowParty, "contentDescShowParty", "隠蔽しているパーティを表示します。") \
    X(contentDescHideParty, "contentDescHideParty", "表示中のパーティを画面下へ動かして隠蔽します。") \
    X(contentDescRedisplay, "contentDescRedisplay", "背景を再描画し、フラグ変更などで表示・非表示状態が内部的に変更された背景セルの状態を画面に反映します。") \
    X(contentDescSubstituteStep, "contentDescSubstituteStep", "任意のステップの値またはランダムな値を別のステップへ代入します。") \
    X(contentDescSubstituteFlag, "contentDescSubstituteFlag", "任意のフラグの値またはランダムな値を別のフラグへ代入します。") \
    X(contentDescBranchStepCmp, "contentDescBranchStepCmp", "任意の2つのステップの値を比較した結果によって処理を分岐させます。") \
    X(contentDescBranchFlagCmp, "contentDescBranchFlagCmp", "任意の2つのフラグの値を比較した結果によって処理を分岐させます。") \
    X(contentDescBranchRandomSelect, "contentDescBranchRandomSelect", "パーティメンバ・エネミー・同行キャストの中から任意の条件で選択を行い、選択に成功したか失敗したかによって処理を分岐させます。") \
    X(contentDescBranchKeyCode, "contentDescBranchKeyCode", "キャラクタ・荷物袋・パーティ全体で任意のキーコードを持つカードを所持しているかいないかによって処理を分岐させます。該当カードを所持しているキャラクタは選択状態になります。") \
    X(contentDescCheckStep, "contentDescCheckStep", "任意のステップが指定された状態の時だけ後続のイベントを実行します。メッセージやセリフの選択肢として使用した場合は、条件を満たす時だけ選択肢が表示されます。") \
    X(contentDescBranchRound, "contentDescBranchRound", "現在のバトルのラウンド数が任意の値以上・未満のどちらかによって処理を分岐させます。バトル中でない場合は、常に未満側へ分岐します。") \
    X(contentDescMoveBgImage, "contentDescMoveBgImage", "セル名称のつけられた背景セルを移動・サイズ変更します。") \
    X(contentDescReplaceBgImage, "contentDescReplaceBgImage", "セル名称のつけられた背景セルを新しい背景セルに置換します。") \
    X(contentDescLoseBgImage, "contentDescLoseBgImage", "セル名称のつけられた背景セルを削除します。") \
    X(contentDescBranchMultiCoupon, "contentDescBranchMultiCoupon", "選択中のメンバがどのクーポン(称号)を所有しているかによって処理を分岐させます。どれも所有していない場合は「全て所有していない」へ分岐します。") \
    X(contentDescBranchMultiRandom, "contentDescBranchMultiRandom", "複数の後続コンテントへ等確率でランダムで分岐します。") \
    X(contentDescMoveCard, "contentDescMoveCard", "メニューカードやエネミーカードを移動・サイズ変更します。") \
    X(contentDescChangeEnvironment, "contentDescChangeEnvironment", "荷物袋の使用の可否など、パーティの状況を設定します。") \
    X(contentDescBranchVariant, "contentDescBranchVariant", "式の結果の真偽値によって処理を分岐します。") \
    X(contentDescSetVariant, "contentDescSetVariant", "式の結果をコモン・フラグ・ステップへ代入します。式の結果は、フラグへ代入する場合は真偽値に、ステップへ代入する場合は数値にする必要があります。") \
    X(contentDescCheckVariant, "contentDescCheckVariant", "式の結果がTRUEの時だけ後続のイベントを実行します。メッセージやセリフの選択肢として使用した場合は、条件を満たす時だけ選択肢が表示されます。") \
    X(contentDescCustomXmlContent, "contentDescCustomXmlContent", "カスタムXMLコンテンツ。XMLタイプ・変数・割当・文字列を指定します。") \
    X(contentDescWsnN, "contentDescWsnN", "%1$sWsn.%2$s以降のシナリオ形式で使用可能です。") \
    X(msnGroupVitality, "msnGroupVitality", "生命力") \
    X(msnGroupPhysical, "msnGroupPhysical", "肉体") \
    X(msnGroupSkill, "msnGroupSkill", "技能") \
    X(msnGroupMental, "msnGroupMental", "精神") \
    X(msnGroupMagic, "msnGroupMagic", "魔法") \
    X(msnGroupEnhance, "msnGroupEnhance", "能力") \
    X(msnGroupVanish, "msnGroupVanish", "消滅") \
    X(msnGroupCard, "msnGroupCard", "カード") \
    X(msnGroupBeast, "msnGroupBeast", "召喚") \
    X(msnGroupEtc, "msnGroupEtc", "その他") \
    X(msnDelete, "msnDelete", "効果削除") \
    X(msnDesc, "msnDesc", "%1$s - %2$s") \
    X(motionNameHeal, "motionNameHeal", "回復") \
    X(motionNameDamage, "motionNameDamage", "ダメージ") \
    X(motionNameAbsorb, "motionNameAbsorb", "吸収") \
    X(motionNameParalyze, "motionNameParalyze", "麻痺") \
    X(motionNameDisParalyze, "motionNameDisParalyze", "麻痺解除") \
    X(motionNamePoison, "motionNamePoison", "中毒") \
    X(motionNameDisPoison, "motionNameDisPoison", "中毒解除") \
    X(motionNameGetSkillPower, "motionNameGetSkillPower", "精神力回復") \
    X(motionNameLoseSkillPower, "motionNameLoseSkillPower", "精神力喪失") \
    X(motionNameSleep, "motionNameSleep", "睡眠状態") \
    X(motionNameConfuse, "motionNameConfuse", "混乱状態") \
    X(motionNameOverheat, "motionNameOverheat", "激昂状態") \
    X(motionNameBrave, "motionNameBrave", "勇敢状態") \
    X(motionNamePanic, "motionNamePanic", "恐慌状態") \
    X(motionNameNormal, "motionNameNormal", "正常状態") \
    X(motionNameBind, "motionNameBind", "呪縛") \
    X(motionNameDisBind, "motionNameDisBind", "呪縛解除") \
    X(motionNameSilence, "motionNameSilence", "沈黙") \
    X(motionNameDisSilence, "motionNameDisSilence", "沈黙解除") \
    X(motionNameFaceUp, "motionNameFaceUp", "暴露") \
    X(motionNameFaceDown, "motionNameFaceDown", "暴露解除") \
    X(motionNameAntiMagic, "motionNameAntiMagic", "魔法無効化") \
    X(motionNameDisAntiMagic, "motionNameDisAntiMagic", "魔法無効化解除") \
    X(motionNameEnhanceAction, "motionNameEnhanceAction", "行動力変化") \
    X(motionNameEnhanceAvoid, "motionNameEnhanceAvoid", "回避力変化") \
    X(motionNameEnhanceDefense, "motionNameEnhanceDefense", "防御力変化") \
    X(motionNameEnhanceResist, "motionNameEnhanceResist", "抵抗力変化") \
    X(motionNameVanishTarget, "motionNameVanishTarget", "対象消去") \
    X(motionNameVanishCard, "motionNameVanishCard", "手札消去") \
    X(motionNameVanishBeast, "motionNameVanishBeast", "召喚獣消去") \
    X(motionNameDealAttackCard, "motionNameDealAttackCard", "通常攻撃") \
    X(motionNameDealPowerfulAttackCard, "motionNameDealPowerfulAttackCard", "渾身の一撃") \
    X(motionNameDealCriticalAttackCard, "motionNameDealCriticalAttackCard", "会心の一撃") \
    X(motionNameDealFeintCard, "motionNameDealFeintCard", "フェイント") \
    X(motionNameDealDefenseCard, "motionNameDealDefenseCard", "防御") \
    X(motionNameDealDistanceCard, "motionNameDealDistanceCard", "見切り") \
    X(motionNameDealConfuseCard, "motionNameDealConfuseCard", "混乱") \
    X(motionNameDealSkillCard, "motionNameDealSkillCard", "特殊技能") \
    X(motionNameSummonBeast, "motionNameSummonBeast", "召喚獣召喚") \
    X(motionNameCancelAction, "motionNameCancelAction", "行動キャンセル") \
    X(motionNameNoEffect, "motionNameNoEffect", "効果無し") \
    X(motionDescHeal, "motionDescHeal", "対象の生命点を増加させます。") \
    X(motionDescDamage, "motionDescDamage", "対象の生命点を減少させます。") \
    X(motionDescAbsorb, "motionDescAbsorb", "対象の生命点を減少させた分だけ使用者の生命点を回復させます。") \
    X(motionDescParalyze, "motionDescParalyze", "対象を麻痺させます。麻痺の値が20を超えた場合は石化扱いとなります。麻痺の最大値は40です。全員が麻痺状態になった場合は全滅します。") \
    X(motionDescDisParalyze, "motionDescDisParalyze", "対象の麻痺を緩和します。") \
    X(motionDescPoison, "motionDescPoison", "対象を中毒状態にします。中毒状態のキャラクタは時間経過でダメージを受けます。中毒の最大値は40です。") \
    X(motionDescDisPoison, "motionDescDisPoison", "対象の中毒を緩和します。") \
    X(motionDescGetSkillPower, "motionDescGetSkillPower", "対象の特殊技能の使用回数を増加させます。") \
    X(motionDescLoseSkillPower, "motionDescLoseSkillPower", "対象の特殊技能の使用回数を減少させます。") \
    X(motionDescSleep, "motionDescSleep", "対象を睡眠状態にします。睡眠状態のキャラクタは、行動・回避・抵抗ができなくなります。") \
    X(motionDescConfuse, "motionDescConfuse", "対象を混乱状態にします。混乱状態のキャラクタは、手札に「混乱」が配付されます。") \
    X(motionDescOverheat, "motionDescOverheat", "対象を激昂状態にします。激昂状態のキャラクタは、手札に「渾身の一撃」が配付されます。") \
    X(motionDescBrave, "motionDescBrave", "対象を勇敢状態にします。勇敢状態のキャラクタは、手札に「防御」「見切り」が配付されなくなります。") \
    X(motionDescPanic, "motionDescPanic", "対象を恐慌状態にします。恐慌状態のキャラクタは、手札に「防御」「見切り」だけが配付されます。") \
    X(motionDescNormal, "motionDescNormal", "対象の精神状態を通常に戻します。") \
    X(motionDescBind, "motionDescBind", "対象を呪縛します。呪縛されたキャラクタは、行動・回避ができなくなります。") \
    X(motionDescDisBind, "motionDescDisBind", "対象の呪縛を解除します。") \
    X(motionDescSilence, "motionDescSilence", "対象を沈黙状態にします。沈黙状態のキャラクタは、発声が必要なカードを使用できなくなります。") \
    X(motionDescDisSilence, "motionDescDisSilence", "対象の沈黙状態を解除します。") \
    X(motionDescFaceUp, "motionDescFaceUp", "対象を暴露状態にします。暴露状態のキャラクタは、そのステータスや次ラウンドの行動を見る事ができます。") \
    X(motionDescFaceDown, "motionDescFaceDown", "対象の暴露状態を解除します。") \
    X(motionDescAntiMagic, "motionDescAntiMagic", "対象を魔法無効化状態にします。魔法無効化状態のキャラクタは魔法・物理的魔法属性の効果を受けなくなり、これらの属性のカードを使用できなくなります。") \
    X(motionDescDisAntiMagic, "motionDescDisAntiMagic", "対象の魔法無効化状態を解除します。") \
    X(motionDescEnhanceAction, "motionDescEnhanceAction", "対象の行動力を変化させます。行動力の増減は行動の成功判定や能力判定の結果に影響します。") \
    X(motionDescEnhanceAvoid, "motionDescEnhanceAvoid", "対象の回避力を変化させます。回避力の増減は効果の回避判定の結果に影響します。+10の場合は必ず回避が成功します。-10の場合は必ず失敗します。") \
    X(motionDescEnhanceDefense, "motionDescEnhanceDefense", "対象の防御力を変化させます。防御力の増減はダメージ計算の結果に影響します。+10の場合はダメージを受けません。-10の場合は4倍のダメージを受けます。") \
    X(motionDescEnhanceResist, "motionDescEnhanceResist", "対象の抵抗力を変化させます。抵抗力の増減は効果への抵抗判定の結果に影響します。+10の場合は必ず抵抗が成功します。-10の場合は必ず失敗します。") \
    X(motionDescVanishTarget, "motionDescVanishTarget", "対象を消去します。消去されたキャラクタが戻ってくる事はありません。") \
    X(motionDescVanishCard, "motionDescVanishCard", "対象の手札を消去します。対象は、次のラウンドで消去された分だけ山札からカードを引きます。") \
    X(motionDescVanishBeast, "motionDescVanishBeast", "対象が所持する召喚獣を消去します。付帯能力(召喚獣召喚効果以外で付与された召喚獣カード)は消去されません。") \
    X(motionDescDealAttackCard, "motionDescDealAttackCard", "対象の山札の一番上に「攻撃」を配付します。") \
    X(motionDescDealPowerfulAttackCard, "motionDescDealPowerfulAttackCard", "対象の山札の一番上に「渾身の一撃」を配付します。") \
    X(motionDescDealCriticalAttackCard, "motionDescDealCriticalAttackCard", "対象の山札の一番上に「会心の一撃」を配付します。") \
    X(motionDescDealFeintCard, "motionDescDealFeintCard", "対象の山札の一番上に「フェイント」を配付します。") \
    X(motionDescDealDefenseCard, "motionDescDealDefenseCard", "対象の山札の一番上に「防御」を配付します。「防御」を選択したキャラクタは抵抗力が増加し、受けるダメージが減少します。") \
    X(motionDescDealDistanceCard, "motionDescDealDistanceCard", "対象の山札の一番上に「見切り」を配付します。「見切り」を選択したキャラクタは回避力が増加します。") \
    X(motionDescDealConfuseCard, "motionDescDealConfuseCard", "対象の山札の一番上に「混乱」を配付します。「混乱」を選択したキャラクタは、選択したラウンド中の行動・回避・抵抗ができなくなります。") \
    X(motionDescDealSkillCard, "motionDescDealSkillCard", "対象の山札の一番上に、対象が使用できる特殊技能カードを配付します。使用回数が尽きている場合は配付されません。") \
    X(motionDescSummonBeast, "motionDescSummonBeast", "対象に任意の召喚獣カードを付与します。") \
    X(motionDescCancelAction, "motionDescCancelAction", "対象の現在のラウンドの行動をキャンセルします。すでに行動済みの場合は何もしません。") \
    X(motionDescNoEffect, "motionDescNoEffect", "いかなる効果も発生しません。視覚効果だけを適用したいなどの時に使用します。") \
    X(have, "have", "所有") \
    X(haveNot, "haveNot", "不所有") \
    X(contains, "contains", "含む") \
    X(notContains, "notContains", "含まない") \
    X(exists, "exists", "存在する") \
    X(notExists, "notExists", "存在しない") \
    X(matchCard, "matchCard", "一致") \
    X(notMatchCard, "notMatchCard", "不一致") \
    X(haveForStatus, "haveForStatus", "状態か") \
    X(haveNotForStatus, "haveNotForStatus", "状態でないか") \
    X(transitionWithSpeed, "transitionWithSpeed", "切替方式 = %1$s ウェイト = %2$s") \
    X(transitionNoSpeed, "transitionNoSpeed", "切替方式 = %1$s") \
    X(ctStart, "ctStart", "スタートコンテント「%1$s」") \
    X(ctStartBattle, "ctStartBattle", "バトルの開始「%1$s」") \
    X(ctChangeArea, "ctChangeArea", "エリア移動「%1$s」 切替方式 = %2$s ウェイト = %3$s") \
    X(ctChangeAreaWithTransition, "ctChangeAreaWithTransition", "エリア移動「%1$s」 %2$s") \
    X(ctChangeAreaClassic, "ctChangeAreaClassic", "エリア移動「%1$s」") \
    X(ctEndComplete, "ctEndComplete", "済印をつけて終了") \
    X(ctEndNoComplete, "ctEndNoComplete", "済印をつけずに終了") \
    X(ctGameOver, "ctGameOver", "敗北・ゲームオーバーコンテント") \
    X(ctChangeBgImageWithTransition, "ctChangeBgImageWithTransition", "背景ファイル = %1$s %2$s") \
    X(ctChangeBgImageClassic, "ctChangeBgImageClassic", "背景ファイル = %1$s") \
    X(ctChangeBgImageFile, "ctChangeBgImageFile", "[%1$s]") \
    X(ctEffectSound, "ctEffectSound", "「%1$s」を再生") \
    X(ctInitialEffect, "ctInitialEffect", "初期効果あり") \
    X(ctInitialEffectWithSound, "ctInitialEffectWithSound", "初期効果あり(%1$s)") \
    X(ctIgnite, "ctIgnite", "イベント発火あり") \
    X(ctIgniteWithKeyCode, "ctIgniteWithKeyCode", "イベント発火あり(%1$s)") \
    X(ctRefAbility, "ctRefAbility", "参照能力 = %1$sと%2$s") \
    X(ctAbsorbToSelected, "ctAbsorbToSelected", "吸収者 = 選択メンバ") \
    X(ctEffectSimple, "ctEffectSimple", "%1$s レベル%2$s %3$s/%4$s 成功率%5$s%6$s 効果 = %7$s") \
    X(ctEffectWith, "ctEffectWith", "%1$s レベル%2$s %3$s/%4$s 成功率%5$s%6$s 効果 = %7$s %8$s") \
    X(ctEffectMotion, "ctEffectMotion", "[%1$s]") \
    X(ctKeyCodes, "ctKeyCodes", "キーコード = %1$s") \
    X(ctNoKeyCode, "ctNoKeyCode", "キーコード無し") \
    X(ctEffectBreakConsumeCard, "ctEffectBreakConsumeCard", "効果中断") \
    X(ctEffectBreakNoConsumeCard, "ctEffectBreakNoConsumeCard", "効果中断(使用中のカードを消費しない)") \
    X(cardVisualWithSpeed, "cardVisualWithSpeed", "%1$s(速度 = %2$s)") \
    X(cardVisualWithOverrideSpeed, "cardVisualWithOverrideSpeed", "%1$s(速度 = %2$s カードより優先)") \
    X(ctLinkStart, "ctLinkStart", "スタートコンテント「%1$s」へのリンク") \
    X(ctLinkPackage, "ctLinkPackage", "パッケージ「%1$s」へのリンク") \
    X(ctTalkMessageImage, "ctTalkMessageImage", "[%1$s]") \
    X(ctTalkMessage, "ctTalkMessage", "%1$s: %2$s") \
    X(ctTalkMessageWithAttrs, "ctTalkMessageWithAttrs", "%1$s: %2$s (%3$s)") \
    X(ctTalkMessageNarration, "ctTalkMessageNarration", "%1$s") \
    X(ctTalkMessageNarrationWithAttrs, "ctTalkMessageNarrationWithAttrs", "%1$s (%2$s)") \
    X(ctTalkMessageSingleLine, "ctTalkMessageSingleLine", "[単行]: %1$s") \
    X(ctTalkMessageSingleLineWithAttrs, "ctTalkMessageSingleLineWithAttrs", "[単行]: %1$s (%2$s)") \
    X(ctTalkDialog, "ctTalkDialog", "%1$s %2$s: %3$s") \
    X(ctTalkDialogWithAttrs, "ctTalkDialogWithAttrs", "%1$s %2$s: %3$s (%4$s)") \
    X(ctTalkDialogNoCoupon, "ctTalkDialogNoCoupon", "%1$s: %2$s") \
    X(ctTalkDialogNoCouponWithAttrs, "ctTalkDialogNoCouponWithAttrs", "%1$s: %2$s (%3$s)") \
    X(ctColumns, "ctColumns", "%1$s列の選択肢") \
    X(ctSelectTalker, "ctSelectTalker", "話者を選択する") \
    X(ctPlayBGM, "ctPlayBGM", "BGMとして「%1$s」を演奏 Ch. = %2$s フェードイン時間 = %3$s × 0.1秒 音量 = %4$s%% ループ回数 = %5$s") \
    X(ctStopBGM, "ctStopBGM", "BGM停止 Ch. = %1$s フェードアウト時間 = %2$s × 0.1秒") \
    X(ctPlaySound, "ctPlaySound", "効果音「%1$s」を鳴らす Ch. = %2$s フェードイン時間 = %3$s × 0.1秒 音量 = %4$s%% ループ回数 = %5$s") \
    X(ctWait, "ctWait", "空白時間 = %1$s × 0.1秒") \
    X(ctElapseTime, "ctElapseTime", "ターン数経過コンテント") \
    X(ctCallStart, "ctCallStart", "スタートコンテント「%1$s」のコール") \
    X(ctCallPackage, "ctCallPackage", "パッケージ「%1$s」のコール") \
    X(ctBranchFlag, "ctBranchFlag", "フラグ「%1$s」の値で分岐") \
    X(ctBranchMultiStep, "ctBranchMultiStep", "ステップ「%1$s」の値で分岐") \
    X(ctBranchStep, "ctBranchStep", "ステップ「%1$s」の値が[%2$s]以上・未満で分岐") \
    X(ctBranchSelectAll, "ctBranchSelectAll", "パーティ全員") \
    X(ctBranchSelectActive, "ctBranchSelectActive", "動けるメンバ") \
    X(ctBranchSelectAuto, "ctBranchSelectAuto", "ランダム") \
    X(ctBranchSelectManual, "ctBranchSelectManual", "手動") \
    X(ctBranchSelectValued, "ctBranchSelectValued", "評価条件(%1$s)") \
    X(initialValue, "initialValue", "初期値 = %1$s") \
    X(couponValues, "couponValues", "%1$s = %2$s") \
    X(ctBranchSelect, "ctBranchSelect", "%1$sから%2$sでメンバを選択") \
    X(ctBranchAbility, "ctBranchAbility", "%1$s(%2$s)の%3$sと%4$sで%5$s判定(レベル%6$s)") \
    X(ctBranchRandom, "ctBranchRandom", "確率 = %1$s%%") \
    X(ctBranchLevelAverage, "ctBranchLevelAverage", "パーティ全員") \
    X(ctBranchLevelSelected, "ctBranchLevelSelected", "選択中のメンバ") \
    X(ctBranchLevel, "ctBranchLevel", "%1$sのレベルが%2$s以上・未満で分岐") \
    X(ctBranchStatus, "ctBranchStatus", "%1$sが%2$s%3$s否かで分岐") \
    X(ctBranchPartyNumber, "ctBranchPartyNumber", "人数 = %1$s人") \
    X(ctBranchArea, "ctBranchArea", "エリア分岐コンテント") \
    X(ctBranchBattle, "ctBranchBattle", "バトル分岐コンテント") \
    X(ctBranchIsBattle, "ctBranchIsBattle", "戦闘中判定分岐コンテント") \
    X(ctBranchCast, "ctBranchCast", "キャストカード「%1$s」の同行有無で分岐") \
    X(ctBranchSkill, "ctBranchSkill", "特殊技能カード「%1$s」の%2$sで分岐(%3$sに%4$s枚) %5$s") \
    X(ctBranchItem, "ctBranchItem", "アイテムカード「%1$s」の%2$sで分岐(%3$sに%4$s枚) %5$s") \
    X(ctBranchBeast, "ctBranchBeast", "召喚獣カード「%1$s」の%2$sで分岐(%3$sに%4$s枚) %5$s") \
    X(ctBranchInfo, "ctBranchInfo", "情報カード「%1$s」の有無で分岐") \
    X(ctBranchMoney, "ctBranchMoney", "分岐金額 = %1$ssp") \
    X(ctBranchCoupon, "ctBranchCoupon", "称号「%1$s」の%2$sで分岐(%3$s)") \
    X(ctBranchCouponExpandSPChars, "ctBranchCouponExpandSPChars", "称号「%1$s」の%2$sで分岐(%3$s) 特殊文字を展開する") \
    X(ctBranchCouponMulti, "ctBranchCouponMulti", "称号%1$sの%2$sの%3$sで分岐(%4$s)") \
    X(ctBranchCouponMultiExpandSPChars, "ctBranchCouponMultiExpandSPChars", "称号%1$sの%2$sの%3$sで分岐(%4$s) 特殊文字を展開する") \
    X(ctBranchCompleteStamp, "ctBranchCompleteStamp", "シナリオ「%1$s」が終了済みか否かで分岐") \
    X(ctBranchGossip, "ctBranchGossip", "ゴシップ「%1$s」の有無で分岐") \
    X(ctBranchGossipExpandSPChars, "ctBranchGossipExpandSPChars", "ゴシップ「%1$s」の有無で分岐 特殊文字を展開する") \
    X(ctSetFlag, "ctSetFlag", "フラグ「%1$s」を[%2$s]に変更 %3$s") \
    X(ctCardSpeed, "ctCardSpeed", "カード速度 = %1$s") \
    X(ctCardSpeedDefault, "ctCardSpeedDefault", "標準") \
    X(ctOverrideCardSpeed, "ctOverrideCardSpeed", "カード速度 = %1$s (カード本体より優先)") \
    X(ctSetStep, "ctSetStep", "ステップ「%1$s」を[%2$s]に変更") \
    X(ctSetStepUp, "ctSetStepUp", "ステップ「%1$s」の値を1増加") \
    X(ctSetStepDown, "ctSetStepDown", "ステップ「%1$s」の値を1減少") \
    X(ctReverseFlag, "ctReverseFlag", "フラグ「%1$s」の値を反転 %2$s") \
    X(ctCheckFlag, "ctCheckFlag", "フラグ「%1$s」の値が[%2$s]であれば後続のイベントが出現") \
    X(ctGetCast, "ctGetCast", "キャストカード「%1$s」を同行させる(%2$s)") \
    X(ctGetSkill, "ctGetSkill", "特殊技能カード「%1$s」を獲得(%2$sに%3$s枚)") \
    X(ctGetItem, "ctGetItem", "アイテムカード「%1$s」を獲得(%2$sに%3$s枚)") \
    X(ctGetBeast, "ctGetBeast", "召喚獣カード「%1$s」を獲得(%2$sに%3$s枚)") \
    X(ctGetInfo, "ctGetInfo", "情報カード「%1$s」を獲得") \
    X(ctGetMoney, "ctGetMoney", "獲得金額 = %1$ssp") \
    X(ctGetCoupon, "ctGetCoupon", "称号「%1$s(%2$s)」を獲得(%3$s)") \
    X(ctGetCouponExpandSPChars, "ctGetCouponExpandSPChars", "称号「%1$s(%2$s)」を獲得(%3$s) 特殊文字を展開する") \
    X(ctGetCompleteStamp, "ctGetCompleteStamp", "シナリオ「%1$s」を終了済みにする") \
    X(ctGetGossip, "ctGetGossip", "ゴシップ「%1$s」を獲得") \
    X(ctGetGossipExpandSPChars, "ctGetGossipExpandSPChars", "ゴシップ「%1$s」を獲得 特殊文字を展開する") \
    X(ctLoseCardAll, "ctLoseCardAll", "全て") \
    X(ctLoseCardCount, "ctLoseCardCount", "%1$s枚") \
    X(ctLoseCast, "ctLoseCast", "キャストカード「%1$s」の同行を解除") \
    X(ctLoseSkill, "ctLoseSkill", "特殊技能カード「%1$s」を喪失(%2$sから%3$s)") \
    X(ctLoseItem, "ctLoseItem", "アイテムカード「%1$s」を喪失(%2$sから%3$s)") \
    X(ctLoseBeast, "ctLoseBeast", "召喚獣カード「%1$s」を喪失(%2$sから%3$s)") \
    X(ctLoseInfo, "ctLoseInfo", "情報カード「%1$s」を喪失") \
    X(ctLoseMoney, "ctLoseMoney", "喪失金額 = %1$ssp") \
    X(ctLoseCoupon, "ctLoseCoupon", "称号「%1$s」を喪失(%2$s)") \
    X(ctLoseCouponExpandSPChars, "ctLoseCouponExpandSPChars", "称号「%1$s」を喪失(%2$s) 特殊文字を展開する") \
    X(ctLoseCompleteStamp, "ctLoseCompleteStamp", "シナリオ「%1$s」の終了印を削除") \
    X(ctLoseGossip, "ctLoseGossip", "ゴシップ「%1$s」を喪失") \
    X(ctLoseGossipExpandSPChars, "ctLoseGossipExpandSPChars", "ゴシップ「%1$s」を喪失 特殊文字を展開する") \
    X(ctShowParty, "ctShowParty", "パーティの表示 %1$s") \
    X(ctHideParty, "ctHideParty", "パーティの隠蔽 %1$s") \
    X(ctRedisplayWithTransition, "ctRedisplayWithTransition", "%1$s") \
    X(ctRedisplayClassic, "ctRedisplayClassic", "画面再構築コンテント") \
    X(ctSubstituteStep, "ctSubstituteStep", "ステップ [%1$s] の値をステップ [%2$s] に代入") \
    X(ctSubstituteFlag, "ctSubstituteFlag", "フラグ [%1$s] の値をフラグ [%2$s] に代入 %3$s") \
    X(ctBranchStepCmp, "ctBranchStepCmp", "ステップ [%1$s] と [%2$s] の値を比較") \
    X(ctBranchFlagCmp, "ctBranchFlagCmp", "フラグ [%1$s] と [%2$s] の値を比較") \
    X(ctSubstituteStepFromRandom, "ctSubstituteStepFromRandom", "ランダム値をステップ [%1$s] に代入") \
    X(ctSubstituteStepFromSelectedPlayer, "ctSubstituteStepFromSelectedPlayer", "選択メンバ番号をステップ [%1$s] に代入") \
    X(ctSubstituteFlagFromRandom, "ctSubstituteFlagFromRandom", "ランダム値をフラグ [%1$s] に代入 %2$s") \
    X(randomValue, "randomValue", "ランダム値") \
    X(selectedPlayerValue, "selectedPlayerValue", "選択メンバ番号") \
    X(ctRandomSelect, "ctRandomSelect", "%2$sのキャラクタを選択(%1$s)") \
    X(ctRandomSelectN, "ctRandomSelectN", "キャラクタを選択(%1$s)") \
    X(ctRandomSelectInvert, "ctRandomSelectInvert", "%2$sでないキャラクタを選択(%1$s)") \
    X(castRange0, "castRange0", "対象無し") \
    X(castRange1, "castRange1", "%1$s全体") \
    X(castRange2, "castRange2", "%1$s全体または%2$s全体") \
    X(castRange3, "castRange3", "フィールド全体") \
    X(ctBranchKeyCodeWithConditionAllType, "ctBranchKeyCodeWithConditionAllType", "キーコード「%1$s」を%2$sカードの%3$sで分岐(%4$s) %5$s") \
    X(ctBranchKeyCodeWithCondition, "ctBranchKeyCodeWithCondition", "キーコード「%1$s」を%2$s%3$sカードの%4$sで分岐(%5$s) %6$s") \
    X(ctBranchKeyCodeWithConditionAllTypeForSelectedCard, "ctBranchKeyCodeWithConditionAllTypeForSelectedCard", "キーコード「%1$s」を%2$s選択カードが%3$sか否かで分岐") \
    X(ctBranchKeyCodeWithConditionForSelectedCard, "ctBranchKeyCodeWithConditionForSelectedCard", "キーコード「%1$s」を%2$s%3$sの選択カードが%4$sか否かで分岐") \
    X(ctSelectFoundCard, "ctSelectFoundCard", "該当カードを選択する") \
    X(noSelectFoundCard, "noSelectFoundCard", "該当カードを選択しない") \
    X(ctCheckStep, "ctCheckStep", "ステップ「%1$s」が[%2$s]%3$s後続のイベントが出現") \
    X(ctBranchRound, "ctBranchRound", "バトルが%1$sラウンド%2$sか否かで分岐") \
    X(ctMoveAndResizeBgImageWithTransition, "ctMoveAndResizeBgImageWithTransition", "背景「%1$s」を%2$sで右へ%3$s、下へ%4$sポイント移動し、%5$sで%6$s×%7$sにリサイズ %8$s") \
    X(ctMoveBgImageWithTransition, "ctMoveBgImageWithTransition", "背景「%1$s」を%2$sで右へ%3$s、下へ%4$sポイント移動 %5$s") \
    X(ctResizeBgImageWithTransition, "ctResizeBgImageWithTransition", "背景「%1$s」を%2$sで%3$s×%4$sにリサイズ %5$s") \
    X(ctMoveAndResizeBgImageClassic, "ctMoveAndResizeBgImageClassic", "背景「%1$s」を%2$sで右へ%3$s、下へ%4$sポイント移動し、%5$sで%6$s×%7$sにリサイズ") \
    X(ctMoveBgImageClassic, "ctMoveBgImageClassic", "背景「%1$s」を%2$sで右へ%3$s、下へ%4$sポイント移動") \
    X(ctResizeBgImageClassic, "ctResizeBgImageClassic", "背景「%1$s」を%2$sで%3$s×%4$sにリサイズ") \
    X(ctMoveBgImageNoSet, "ctMoveBgImageNoSet", "背景「%1$s」に対して何も行わない") \
    X(ctReplaceBgImageWithTransition, "ctReplaceBgImageWithTransition", "背景「%1$s」を置換 背景ファイル = %2$s %3$s") \
    X(ctReplaceBgImageClassic, "ctReplaceBgImageClassic", "背景「%1$s」を置換 背景ファイル = %2$s") \
    X(ctLoseBgImageWithTransition, "ctLoseBgImageWithTransition", "背景「%1$s」を削除 %2$s") \
    X(ctLoseBgImageClassic, "ctLoseBgImageClassic", "背景「%1$s」を削除") \
    X(ctBranchMultiCoupon, "ctBranchMultiCoupon", "%1$sの称号所有状態で分岐") \
    X(ctBranchMultiCouponExpandSPChars, "ctBranchMultiCouponExpandSPChars", "%1$sの称号所有状態で分岐 特殊文字を展開する") \
    X(ctBranchMultiRandom, "ctBranchMultiRandom", "ランダム多岐分岐コンテント") \
    X(couponNames, "couponNames", "「%1$s」") \
    X(couponNamesSeparator, "couponNamesSeparator", "") \
    X(matchingTypeAnd, "matchingTypeAnd", "全て") \
    X(matchingTypeOr, "matchingTypeOr", "どれか一つ") \
    X(ctMoveCard, "ctMoveCard", "カードグループ「%1$s」を%2$sで右へ%3$s、下へ%4$sポイント移動(スケール = %5$s レイヤ = %6$s) %7$s") \
    X(ctMoveCardNoSet, "ctMoveCardNoSet", "カードグループ「%1$s」を移動しない(スケール = %2$s レイヤ = %3$s) %4$s") \
    X(noChangeScale, "noChangeScale", "変更無し") \
    X(noChangeLayer, "noChangeLayer", "変更無し") \
    X(ctChangeEnvironmentNotSet, "ctChangeEnvironmentNotSet", "状況を変更しない") \
    X(ctChangeEnvironment, "ctChangeEnvironment", "%1$s") \
    X(ctEnvironmentEnabled, "ctEnvironmentEnabled", "%1$sを%2$s") \
    X(ctBranchVariant, "ctBranchVariant", "式〔 %1$s 〕") \
    X(ctSetVariant, "ctSetVariant", "%1$s =〔 %2$s 〕") \
    X(ctFlagName, "ctFlagName", "フラグ「%1$s」") \
    X(ctStepName, "ctStepName", "ステップ「%1$s」") \
    X(ctVariantName, "ctVariantName", "コモン「%1$s」") \
    X(ctCheckVariant, "ctCheckVariant", "式〔 %1$s 〕") \
    X(ctBranchSkillForSelectedCard, "ctBranchSkillForSelectedCard", "特殊技能カード「%1$s」と選択カードの%2$sで分岐") \
    X(ctBranchItemForSelectedCard, "ctBranchItemForSelectedCard", "アイテムカード「%1$s」と選択カードの%2$sで分岐") \
    X(ctBranchBeastForSelectedCard, "ctBranchBeastForSelectedCard", "召喚獣カード「%1$s」と選択カードの%2$sで分岐") \
    X(ctGetSkillForSelectedCard, "ctGetSkillForSelectedCard", "特殊技能カード「%1$s」を獲得(選択カードと交換)") \
    X(ctGetItemForSelectedCard, "ctGetItemForSelectedCard", "アイテムカード「%1$s」を獲得(選択カードと交換)") \
    X(ctGetBeastForSelectedCard, "ctGetBeastForSelectedCard", "召喚獣カード「%1$s」を獲得(選択カードと交換)") \
    X(ctLoseSkillForSelectedCard, "ctLoseSkillForSelectedCard", "特殊技能カードを喪失(選択カード)") \
    X(ctLoseItemForSelectedCard, "ctLoseItemForSelectedCard", "アイテムカードを喪失(選択カード)") \
    X(ctLoseBeastForSelectedCard, "ctLoseBeastForSelectedCard", "召喚獣カードを喪失(選択カード)") \
    X(nameWithID, "nameWithID", "%1$s.%2$s") \
    X(defaultStartName, "defaultStartName", "イベント開始") \
    X(oggMayNotCorrespond, "oggMayNotCorrespond", "Oggはプレイヤーの環境によって再生できない事があります。") \
    X(mp3LoopMayNotCorrespond, "mp3LoopMayNotCorrespond", "MP3はプレイヤーの環境によってループ再生されない事があります。") \
    X(playingOption, "playingOption", "再生オプション") \
    X(playingVolume, "playingVolume", "音量") \
    X(playingVolumePer, "playingVolumePer", "%") \
    X(loopCount, "loopCount", "ループ回数") \
    X(loopCountHint, "loopCountHint", "(0 = ∞)") \
    X(playingChannel, "playingChannel", "Ch.") \
    X(mainChannel, "mainChannel", "主音声") \
    X(subChannel, "subChannel", "副音声") \
    X(fadeIn, "fadeIn", "フェードイン時間") \
    X(fadeInHint, "fadeInHint", "× 0.1秒") \
    X(warningVolume, "warningVolume", "音量の設定はWsn.1以降の形式のシナリオしか使用できません。") \
    X(warningLoopCount, "warningLoopCount", "ループ回数の設定はWsn.1以降の形式のシナリオしか使用できません。") \
    X(warningChannel, "warningChannel", "再生チャンネルの設定はWsn.1以降の形式のシナリオしか使用できません。") \
    X(warningFadeIn, "warningFadeIn", "フェードイン時間の設定はWsn.1以降の形式のシナリオしか使用できません。") \
    X(warningContinueBGM, "warningContinueBGM", "バトル開始時のBGM継続はWsn.3以降の形式のシナリオでしか機能しません。") \
    X(mainWindowName, "mainWindowName", "%1$s [ %2$s ] - CWXEditor_C") \
    X(mainWindowNameChanged, "mainWindowNameChanged", "*%1$s [ %2$s ] - CWXEditor_C") \
    X(mainWindowNameEmpty, "mainWindowNameEmpty", "CWXEditor_C") \
    X(errorExecEngine, "errorExecEngine", "%1$sの起動に失敗しました。") \
    X(dlgTitNewScenario, "dlgTitNewScenario", "新規シナリオの作成") \
    X(dlgTitNewScenarioAtNewWin, "dlgTitNewScenarioAtNewWin", "新しいウィンドウで新規シナリオの作成") \
    X(dlgTitOpenScenario, "dlgTitOpenScenario", "シナリオを開く") \
    X(dlgTitOpenScenarioAtNewWin, "dlgTitOpenScenarioAtNewWin", "新しいウィンドウでシナリオを開く") \
    X(filterScenario, "filterScenario", "シナリオファイル (%1$s)") \
    X(filterParts, "filterParts", "エリア・カードファイル (%1$s)") \
    X(dlgTitSaveScenario, "dlgTitSaveScenario", "名前を付けて保存") \
    X(filterScenarioSave, "filterScenarioSave", "WSN(XML)形式のシナリオ (*.wsn)") \
    X(filterScenarioSaveNewDir, "filterScenarioSaveNewDir", "展開されたWSN(XML)形式シナリオ (フォルダを新規作成)") \
    X(filterScenarioSaveDir, "filterScenarioSaveDir", "展開されたWSN(XML)形式シナリオ (Summary.xml)") \
    X(filterScenarioSaveClassic, "filterScenarioSaveClassic", "クラシックシナリオ (Summary.wsm)") \
    X(filterScenarioSaveClassic7, "filterScenarioSaveClassic7", "展開されたCardWirthNext 1.60形式シナリオ (フォルダを新規作成)") \
    X(filterScenarioSaveZip, "filterScenarioSaveZip", "ZIP圧縮されたクラシックシナリオ (*.zip)") \
    X(filterScenarioSaveCab, "filterScenarioSaveCab", "CAB圧縮されたクラシックシナリオ (*.cab)") \
    X(filterScenarioSaveZip7, "filterScenarioSaveZip7", "ZIP圧縮されたCardWirthNext 1.60形式としてエクスポート (*.zip)") \
    X(filterScenarioSaveCab7, "filterScenarioSaveCab7", "CAB圧縮されたCardWirthNext 1.60形式としてエクスポート (*.cab)") \
    X(warningXToClassic, "warningXToClassic", "WSN(XML)形式のシナリオをクラシック形式に変換すると一部データが失われる可能性がある他、対応していない形式の素材で不具合が発生する恐れがあります。\nクラシック形式で保存しますか？") \
    X(saveToNotEmptyDir, "saveToNotEmptyDir", "%1$sは空ではありません。\n本当にここにシナリオを保存しますか？") \
    X(notScenario, "notScenario", "%1$sはシナリオ圧縮ファイルではありません") \
    X(zipError, "zipError", "%1$sの展開に失敗しました。") \
    X(loadError, "loadError", "%1$sの読み込みに失敗しました。") \
    X(saveError, "saveError", "%1$sの保存に失敗しました。") \
    X(loadErrorStatus, "loadErrorStatus", "%1$sの読み込みに失敗") \
    X(loadErrorStatusCount, "loadErrorStatusCount", "%1$s件のシナリオの読み込みに失敗") \
    X(scenarioNotFound, "scenarioNotFound", "%1$sは存在しないか、シナリオではありません。履歴から削除しますか？") \
    X(scenarioNotFoundForBookmark, "scenarioNotFoundForBookmark", "%1$sは存在しないか、シナリオではありません。ブックマークを削除しますか？") \
    X(dlgTitScenarioLoadErrors, "dlgTitScenarioLoadErrors", "シナリオファイル読込エラー") \
    X(scenarioLoadErrors, "scenarioLoadErrors", "「%1$s(%2$s)」の読み込み中に、以下のファイルでエラーが発生しました。これらのファイルは無視され、次回の保存時に削除されます。\nこのままシナリオを開いてよろしいですか？") \
    X(scenarioLoadErrorsNoFileName, "scenarioLoadErrorsNoFileName", "「%1$s」の読み込み中に、以下のファイルでエラーが発生しました。これらのファイルは無視され、次回の保存時に削除されます。\nこのままシナリオを開いてよろしいですか？") \
    X(areasTabName, "areasTabName", "テーブル") \
    X(areaStatus, "areaStatus", "%2$s件の%1$s") \
    X(flagTabName, "flagTabName", "状態変数") \
    X(flagStatus, "flagStatus", "%2$s個の%1$s") \
    X(flagStatusSel, "flagStatusSel", "%1$s (%2$s個を選択)") \
    X(scenarioView, "scenarioView", "シナリオビューリスト") \
    X(variableView, "variableView", "状態変数インスペクタ") \
    X(reNumberingAll, "reNumberingAll", "全てのエリアやカードのIDを1から振り直します。\nよろしいですか？") \
    X(dlgTitReNumbering, "dlgTitReNumbering", "IDの振り直し") \
    X(reNumbering, "reNumbering", "IDの振り直し") \
    X(reNumbering1, "reNumbering1", "%1$s「%2$s」以降のIDを") \
    X(reNumbering2, "reNumbering2", "番から順に振り直す") \
    X(areaDirRoot, "areaDirRoot", "Scenario") \
    X(areaDirNew, "areaDirNew", "新規フォルダ") \
    X(areaId, "areaId", "ID") \
    X(areaName, "areaName", "名称") \
    X(areaCount, "areaCount", "利用数") \
    X(areaNew, "areaNew", "新規エリア") \
    X(battleNew, "battleNew", "新規バトル") \
    X(packageNew, "packageNew", "新規パッケージ") \
    X(areaDirName, "areaDirName", "フォルダ名") \
    X(flagDirRoot, "flagDirRoot", "Data") \
    X(flagDirNew, "flagDirNew", "新規フォルダ") \
    X(flagName, "flagName", "名称") \
    X(flagInit, "flagInit", "初期値") \
    X(flagCount, "flagCount", "利用数") \
    X(variableInitialization, "variableInitialization", "初期化タイミング") \
    X(variableInitializationNameLeave, "variableInitializationNameLeave", "シナリオ終了時") \
    X(variableInitializationNameEventExit, "variableInitializationNameEventExit", "イベント終了時") \
    X(variableInitializationNameBattleStart, "variableInitializationNameBattleStart", "戦闘開始時") \
    X(variableInitializationNameBattleEnd, "variableInitializationNameBattleEnd", "戦闘終了時") \
    X(variableInitializationNameNone, "variableInitializationNameNone", "初期化しない") \
    X(variableInitializationShort, "variableInitializationShort", "初期化") \
    X(variableInitializationShortNameLeave, "variableInitializationShortNameLeave", "シナリオ終了") \
    X(variableInitializationShortNameEventExit, "variableInitializationShortNameEventExit", "イベント終了") \
    X(variableInitializationShortNameBattleStart, "variableInitializationShortNameBattleStart", "戦闘開始") \
    X(variableInitializationShortNameBattleEnd, "variableInitializationShortNameBattleEnd", "戦闘終了") \
    X(variableInitializationShortNameNone, "variableInitializationShortNameNone", "しない") \
    X(warningVariableInitialization, "warningVariableInitialization", "初期化タイミングの指定は、Wsn.4以降の形式のシナリオでしか行なえません。") \
    X(dlgTitFlag, "dlgTitFlag", "フラグの設定") \
    X(dlgLblFlagName, "dlgLblFlagName", "フラグ名") \
    X(dlgLblFlagInit, "dlgLblFlagInit", "初期値") \
    X(dlgLblFlagTrue, "dlgLblFlagTrue", "TRUE") \
    X(dlgLblFlagFalse, "dlgLblFlagFalse", "FALSE") \
    X(flagOnValue, "flagOnValue", "TRUEの値") \
    X(flagOffValue, "flagOffValue", "FALSEの値") \
    X(dlgTitStep, "dlgTitStep", "ステップの設定") \
    X(dlgLblStepName, "dlgLblStepName", "ステップ名") \
    X(dlgLblStepInit, "dlgLblStepInit", "初期値") \
    X(stepValueForWarning, "stepValueForWarning", "値") \
    X(stepCount, "stepCount", "段階数") \
    X(dlgTitVariant, "dlgTitVariant", "コモンの設定") \
    X(variantName, "variantName", "コモン名") \
    X(numberValue, "numberValue", "数値") \
    X(stringValue, "stringValue", "文字列値") \
    X(booleanValue, "booleanValue", "真偽値") \
    X(listValue, "listValue", "リスト") \
    X(structValue, "structValue", "構造体(%$1s)") \
    X(variantValueHint, "variantValueHint", "書式:\n数値 = 9999\n文字列 = \"文字列\"\n真偽値 = TRUE または FALSE\n※ 文字列内に\"を入れる時は\"\"と記述") \
    X(warningInvalidVariantValue, "warningInvalidVariantValue", "書式が正しくありません。\n\n%1$s") \
    X(dlgTitSummary, "dlgTitSummary", "概略の設定 - [ %1$s ]") \
    X(summaryPreview, "summaryPreview", "表示イメージ") \
    X(baseData, "baseData", "基本データ") \
    X(etcData, "etcData", "詳細データ") \
    X(useKeyInAction, "useKeyInAction", "アクションイベントでキー入力を使用する") \
    X(noConsumeUnmatchedMenuCard, "noConsumeUnmatchedMenuCard", "メニューカードへのカード使用時に無効であれば消費しない") \
    X(targetLevelSame, "targetLevelSame", "対象レベル：%1$s") \
    X(targetLevelHL, "targetLevelHL", "対象レベル：%1$s ～ %2$s") \
    X(targetLevelL, "targetLevelL", "対象レベル：%1$s ～") \
    X(targetLevelH, "targetLevelH", "対象レベル： ～ %1$s") \
    X(summaryPageDummy, "summaryPageDummy", "1/1") \
    X(title, "title", "シナリオタイトル") \
    X(author, "author", "作者名") \
    X(targetLevel, "targetLevel", "対象レベル") \
    X(desc, "desc", "解説") \
    X(levSep, "levSep", "～") \
    X(qualification, "qualification", "シナリオ出現条件") \
    X(rCouponNum, "rCouponNum", "必要数") \
    X(rCoupons, "rCoupons", "必要とする称号") \
    X(startArea, "startArea", "シナリオ開始エリア") \
    X(scenarioType, "scenarioType", "シナリオタイプ") \
    X(sTypeXML, "sTypeXML", "スキンを指定") \
    X(sTypeClassic, "sTypeClassic", "クラシックエンジンを使用") \
    X(dataVersion, "dataVersion", "データバージョン") \
    X(dataVersionName, "dataVersionName", "%1$s - %2$s") \
    X(loadScaledImage, "loadScaledImage", "スケーリングされたイメージを使用する") \
    X(loadScaledImageHint, "loadScaledImageHint", "\"image.bmp\"という名前のファイルを2倍スケールで表示する時、\n\"image.x2.bmp\"があれば代わりに表示します") \
    X(warningIncludedPNGImage, "warningIncludedPNGImage", "CardWirth 1.50以前では、格納されたPNGイメージの読み込みでエラーが発生します。メニューカード・背景セル・メッセージの話者以外では使用しないでください。") \
    X(warningIncludedGIFImage, "warningIncludedGIFImage", "CardWirth 1.50以前では、格納されたGIFイメージの読み込みでエラーが発生します。メニューカード・背景セル・メッセージの話者以外では使用しないでください。") \
    X(pngMayNotCorrespond, "pngMayNotCorrespond", "CardWirth 1.29以前では、PNGイメージの読み込みでエラーが発生します。") \
    X(gifMayNotCorrespond, "gifMayNotCorrespond", "CardWirth 1.29以前では、GIFイメージの読み込みでエラーが発生します。") \
    X(warningInvalidFileExtensionImage, "warningInvalidFileExtensionImage", "イメージとして使用できない拡張子のファイルが指定されています。") \
    X(warningInvalidFileExtensionSound, "warningInvalidFileExtensionSound", "音声として使用できない拡張子のファイルが指定されています。") \
    X(warningInvalidSJISCharacter, "warningInvalidSJISCharacter", "クラシックなシナリオで使用できない文字「%1$s」が含まれています。") \
    X(warningInvalidSJISCharacterWithName, "warningInvalidSJISCharacterWithName", "%1$sにクラシックなシナリオで使用できない文字「%2$s」が含まれています。") \
    X(noRefArea, "noRefArea", "カード配置参照無し") \
    X(areaViewFlagDesc, "areaViewFlagDesc", "フラグ") \
    X(areaViewRefAreaDesc, "areaViewRefAreaDesc", "参照") \
    X(refFlags, "refFlags", "参照フラグ") \
    X(allCheckFlag, "allCheckFlag", "全TRUE/FALSE") \
    X(inheritBackground, "inheritBackground", "背景継承あり") \
    X(inheritBackgroundHint, "inheritBackgroundHint", "これまでに表示されている背景セルを消去せず、新しいセルを追加します。\nフラグ・セル名称・レイヤ設定の無いフルサイズのイメージセルを1枚目に設定した場合は、\n背景継承を行いません。") \
    X(notInheritBackground, "notInheritBackground", "背景継承無し") \
    X(notInheritBackgroundHint, "notInheritBackgroundHint", "これまでに表示されている背景セルを消去して新しいセルを表示します。\nフラグ・セル名称・レイヤ設定の無いフルサイズのイメージセルを1枚目に設定した場合は、\n背景継承を行いません。") \
    X(left, "left", "X") \
    X(top, "top", "Y") \
    X(width, "width", "幅") \
    X(height, "height", "高") \
    X(scale, "scale", "拡大率") \
    X(scalePer, "scalePer", "%") \
    X(layer, "layer", "レイヤ") \
    X(layerHint, "layerHint", "(標準 = %1$s)") \
    X(bgImageForeground, "bgImageForeground", "カードよりも前に表示") \
    X(bgImageCellName, "bgImageCellName", "セル名称") \
    X(hasActions, "hasActions", "アクションの有無") \
    X(areaViewStatus, "areaViewStatus", "%1$s [%2$s] - %3$s") \
    X(areaViewStatusNoSummary, "areaViewStatusNoSummary", "%1$s [%2$s]") \
    X(areaViewStatusNoFlag, "areaViewStatusNoFlag", "フラグ指定無し") \
    X(areaViewStatusWithFlag, "areaViewStatusWithFlag", "フラグ = %1$s") \
    X(areaViewStatusWithLayer, "areaViewStatusWithLayer", "レイヤ = %1$s") \
    X(areaViewStatusWithCardSpeed, "areaViewStatusWithCardSpeed", "速度 = %1$s") \
    X(areaViewStatusImageIncluding, "areaViewStatusImageIncluding", "イメージ格納") \
    X(areaViewStatusSelCard, "areaViewStatusSelCard", "%1$s枚のカード") \
    X(areaViewStatusSelBack, "areaViewStatusSelBack", "%1$s枚の背景") \
    X(areaViewStatusEnemyCard, "areaViewStatusEnemyCard", "%1$s.%2$s") \
    X(viewNameSceneTab, "viewNameSceneTab", "%2$s.%3$s") \
    X(viewNameEventTab, "viewNameEventTab", "%2$s.%3$s") \
    X(cardCount, "cardCount", "利用数") \
    X(cardAndBackView, "cardAndBackView", "カードと背景") \
    X(enemyCardView, "enemyCardView", "エネミーカード") \
    X(menuCards, "menuCards", "カード") \
    X(enemyCards, "enemyCards", "カード") \
    X(backs, "backs", "背景") \
    X(eventView, "eventView", "イベント") \
    X(menuCard, "menuCard", "メニューカード") \
    X(enemyCard, "enemyCard", "エネミーカード") \
    X(back, "back", "背景画像") \
    X(textCell, "textCell", "テキストセル") \
    X(colorCell, "colorCell", "カラーセル") \
    X(pcCell, "pcCell", "プレイヤーキャラクタセル") \
    X(pcCellNoSet, "pcCellNoSet", "(指定無し)") \
    X(cellPCNumber, "cellPCNumber", "表示するキャラクタ") \
    X(cellNoSet, "cellNoSet", "(指定無し)") \
    X(cardGroup, "cardGroup", "カードグループ") \
    X(nameWithCellName, "nameWithCellName", "[%1$s] %2$s") \
    X(nameWithCardGroup, "nameWithCardGroup", "[%1$s] %2$s") \
    X(dlgTitDropCard, "dlgTitDropCard", "カード画像の追加") \
    X(dlgMsgDropCard, "dlgMsgDropCard", detail::concat("カード画像をシナリオ", DIR, "にコピーしますか？\n%1$s")) \
    X(dlgTitDropBack, "dlgTitDropBack", "背景画像の追加") \
    X(dlgMsgDropBack, "dlgMsgDropBack", detail::concat("背景画像をシナリオ", DIR, "にコピーしますか？\n%1$s")) \
    X(refFlag, "refFlag", "フラグ参照先") \
    X(noFlagRef, "noFlagRef", "参照無し") \
    X(cardPosition, "cardPosition", "カード位置") \
    X(backPosition, "backPosition", "配置") \
    X(bgImageSettings, "bgImageSettings", "簡単設定") \
    X(bgImageSettingCustom, "bgImageSettingCustom", "カスタム") \
    X(bgImageSettingOriginal, "bgImageSettingOriginal", "元のサイズ") \
    X(enemyCardBase, "enemyCardBase", "基本設定") \
    X(dlgTitMenuCard, "dlgTitMenuCard", "メニューカードの設定 [ %1$s ]") \
    X(dlgTitNewMenuCard, "dlgTitNewMenuCard", "メニューカードの作成") \
    X(dlgTitBgImage, "dlgTitBgImage", "背景画像の設定") \
    X(dlgTitNewBgImage, "dlgTitNewBgImage", "背景画像の作成") \
    X(dlgTitTextCell, "dlgTitTextCell", "テキストセルの設定") \
    X(dlgTitNewTextCell, "dlgTitNewTextCell", "テキストセルの作成") \
    X(dlgTitColorCell, "dlgTitColorCell", "カラーセルの設定") \
    X(dlgTitNewColorCell, "dlgTitNewColorCell", "カラーセルの作成") \
    X(dlgTitPCCell, "dlgTitPCCell", "プレイヤーキャラクタセルの設定") \
    X(dlgTitNewPCCell, "dlgTitNewPCCell", "プレイヤーキャラクタセルの作成") \
    X(dlgTitEnemyCard, "dlgTitEnemyCard", "エネミーカードの設定 [ %1$s ]") \
    X(dlgTitNewEnemyCard, "dlgTitNewEnemyCard", "エネミーカードの作成") \
    X(alphaChannel, "alphaChannel", "不透明度:") \
    X(blendMode, "blendMode", "合成方法") \
    X(colorCellBaseColor, "colorCellBaseColor", "基本色") \
    X(gradient, "gradient", "グラデーション") \
    X(direction, "direction", "方向:") \
    X(endColor, "endColor", "終端色") \
    X(text, "text", "テキスト") \
    X(font, "font", "フォント") \
    X(size, "size", "サイズ:") \
    X(pixel, "pixel", "ピクセル") \
    X(fontColor, "fontColor", "テキスト色") \
    X(fontStyle, "fontStyle", "書式") \
    X(bold, "bold", "太字") \
    X(italic, "italic", "斜体") \
    X(underline, "underline", "下線") \
    X(strike, "strike", "取り消し線") \
    X(vertical, "vertical", "縦書き") \
    X(antialias, "antialias", "アンチエイリアス") \
    X(bordering, "bordering", "縁取り") \
    X(borderingWidth, "borderingWidth", "幅:") \
    X(borderingColor, "borderingColor", "縁取り色") \
    X(pcCellExpanding, "pcCellExpanding", "セルに合わせて拡大・縮小する") \
    X(overrideEnemyCardVisual, "overrideEnemyCardVisual", "外観の上書き") \
    X(updateType, "updateType", "再表示時の処理") \
    X(updateTypeNameFixed, "updateTypeNameFixed", "最初に表示した内容に固定") \
    X(updateTypeNameVariables, "updateTypeNameVariables", "状態変数値を更新する") \
    X(updateTypeNameAll, "updateTypeNameAll", "全て更新する") \
    X(warningUpdateTypeNotFixed, "warningUpdateTypeNotFixed", "テキストセルの内容の更新は、クラシックなシナリオでは行う事ができません。") \
    X(warningUpdateType, "warningUpdateType", "テキストセルの更新方法の指定は、Wsn.4以降の形式のシナリオでしか行なえません。") \
    X(warningAntialiasedTextCell, "warningAntialiasedTextCell", "テキストセルの文字のアンチエイリアスはWsn.4以降の形式のシナリオでしか行なえません。") \
    X(areaViewKeyboardHint, "areaViewKeyboardHint", "選択+上下左右: 移動, Shift+上下左右: サイズ変更, Ctrl+Altで10ピクセル単位操作, Alt+クリックで範囲選択開始, 中クリックで背後のアイテムを選択") \
    X(couponTabName, "couponTabName", "クーポン") \
    X(gossipTabName, "gossipTabName", "ゴシップ") \
    X(completeStampTabName, "completeStampTabName", "終了印") \
    X(keyCodeTabName, "keyCodeTabName", "キーコード") \
    X(cellNameTabName, "cellNameTabName", "セル名称") \
    X(cardGroupTabName, "cardGroupTabName", "カードグループ") \
    X(idName, "idName", "名称") \
    X(idCount, "idCount", "利用数") \
    X(idStatus, "idStatus", "%2$s件の%1$s") \
    X(idStatusSel, "idStatusSel", "%1$s (%2$s件を選択)") \
    X(eventName, "eventName", "イベント名") \
    X(playerCard, "playerCard", "プレイヤーカード") \
    X(tools, "tools", "イベントコンテント") \
    X(startEnter, "startEnter", "到着") \
    X(startSelect, "startSelect", "クリック") \
    X(startDead, "startDead", "死亡") \
    X(startVictory, "startVictory", "勝利") \
    X(startEscape, "startEscape", "逃走") \
    X(startLose, "startLose", "敗北") \
    X(startEveryRound, "startEveryRound", "毎ラウンド") \
    X(startRoundEnd, "startRoundEnd", "ラウンド終了") \
    X(startRound0, "startRound0", "バトル開始") \
    X(startPackage, "startPackage", "パッケージ") \
    X(startUse, "startUse", "使用時") \
    X(startRound, "startRound", "ラウンド = %1$s") \
    X(dlgTitNewEvent, "dlgTitNewEvent", "イベントの作成") \
    X(dlgTitEventTree, "dlgTitEventTree", "イベントの設定 [ %1$s ]") \
    X(addRound, "addRound", "ラウンドの追加") \
    X(delRound, "delRound", "ラウンドの削除") \
    X(warningNoIgnition, "warningNoIgnition", "イベントの発火条件がありません。") \
    X(warningKeyCodeMatchingTypeAnd, "warningKeyCodeMatchingTypeAnd", "キーコードのマッチング条件「全てに一致」は、CardWirth 1.50より前のバージョンでは使用できません。") \
    X(localVariables, "localVariables", "ローカル変数") \
    X(localVariablesOwner, "localVariablesOwner", "[%1$s.%2$s]") \
    X(keyCodeTimingUse, "keyCodeTimingUse", "使用") \
    X(keyCodeTimingSuccess, "keyCodeTimingSuccess", "成功") \
    X(keyCodeTimingFailure, "keyCodeTimingFailure", "失敗") \
    X(keyCodeTimingHasNot, "keyCodeTimingHasNot", "不保有") \
    X(manyRounds, "manyRounds", "追加する発火ラウンドの範囲") \
    X(dlgTitAddManyRounds, "dlgTitAddManyRounds", "追加する発火ラウンドの範囲") \
    X(roundSep, "roundSep", "～") \
    X(enterTree, "enterTree", "到着") \
    X(selectTree, "selectTree", "クリック") \
    X(deadTree, "deadTree", "死亡") \
    X(victoryTree, "victoryTree", "勝利") \
    X(escapeTree, "escapeTree", "逃走") \
    X(loseTree, "loseTree", "敗北") \
    X(everyRoundTree, "everyRoundTree", "毎ラウンド") \
    X(roundEndTree, "roundEndTree", "ラウンド終了") \
    X(round0Tree, "round0Tree", "バトル開始") \
    X(packageTree, "packageTree", "パッケージイベント") \
    X(useTree, "useTree", "使用時イベント") \
    X(keyCodeTree, "keyCodeTree", "[%1$s]") \
    X(roundTree, "roundTree", "ラウンド %1$s") \
    X(eventTreeKindSystem, "eventTreeKindSystem", "システム") \
    X(eventTreeKindKeyCode, "eventTreeKindKeyCode", "キーコード") \
    X(eventTreeKindRound, "eventTreeKindRound", "ラウンド") \
    X(eventTreeSlope, "eventTreeSlope", "傾き") \
    X(startUseCount, "startUseCount", "利用数") \
    X(unknownBranchCondition, "unknownBranchCondition", "不明な分岐条件「%1$s」が指定されています。") \
    X(unknownBranchConditionNoText, "unknownBranchConditionNoText", "分岐条件が空文字列になっています。") \
    X(invalidNextName, "invalidNextName", "不正なコンテント名「%1$s」が指定されています。") \
    X(flagOn, "flagOn", "TRUE") \
    X(flagOff, "flagOff", "FALSE") \
    X(evtChildBrVar, "evtChildBrVar", "%1$s = %2$s") \
    X(etc, "etc", "その他") \
    X(stepMoreThan, "stepMoreThan", "ステップ「%1$s」が「%2$s」以上") \
    X(stepLessThan, "stepLessThan", "ステップ「%1$s」が「%2$s」未満") \
    X(partyAll, "partyAll", "パーティ全員") \
    X(partyActive, "partyActive", "動けるメンバ") \
    X(autoSelect, "autoSelect", "自動") \
    X(manualSelect, "manualSelect", "手動") \
    X(valuedSelect, "valuedSelect", "評価条件") \
    X(selectMemberSuccess, "selectMemberSuccess", "%1$sから%2$sでキャラクタを選択") \
    X(selectMemberCancel, "selectMemberCancel", "%1$sから%2$sでのキャラクタ選択をキャンセル") \
    X(selectMemberFailure, "selectMemberFailure", "%1$sから%2$sでのキャラクタ選択に失敗") \
    X(branchAbilitySuccess, "branchAbilitySuccess", "%1$sがレベル%2$sで%3$sと%4$sで行う%5$s判定に成功") \
    X(branchAbilityFailure, "branchAbilityFailure", "%1$sがレベル%2$sで%3$sと%4$sで行う%5$s判定に失敗") \
    X(abilityHighest, "abilityHighest", "能力") \
    X(abilityLowest, "abilityLowest", "能力不足") \
    X(branchRandomSuccess, "branchRandomSuccess", "%1$s%%成功") \
    X(branchRandomFailure, "branchRandomFailure", "%1$s%%失敗") \
    X(levelAverage, "levelAverage", "パーティ全員の平均値") \
    X(levelSelected, "levelSelected", "選択中のメンバ") \
    X(branchLevelSuccess, "branchLevelSuccess", "%1$sがレベル%2$s以上") \
    X(branchLevelFailure, "branchLevelFailure", "%1$sがレベル%2$s未満") \
    X(branchStatusSuccess, "branchStatusSuccess", "%1$sが「%2$s」状態である") \
    X(branchStatusFailure, "branchStatusFailure", "%1$sが「%2$s」状態でない") \
    X(branchNumberSuccess, "branchNumberSuccess", "パーティに%1$s人以上いる") \
    X(branchNumberFailure, "branchNumberFailure", "パーティは%1$s人未満") \
    X(branchArea, "branchArea", "エリア = %1$s") \
    X(branchAreaWithId, "branchAreaWithId", "エリア = %1$s.%2$s") \
    X(branchBattle, "branchBattle", "バトル = %1$s") \
    X(branchBattleWithId, "branchBattleWithId", "バトル = %1$s.%2$s") \
    X(branchOnBattleSuccess, "branchOnBattleSuccess", "イベント発生時の状況が戦闘中") \
    X(branchOnBattleFailure, "branchOnBattleFailure", "イベント発生時の状況が戦闘中以外") \
    X(branchCastSuccess, "branchCastSuccess", "「%1$s」が加わっている") \
    X(branchCastFailure, "branchCastFailure", "「%1$s」が加わっていない") \
    X(branchEffectCardSuccess, "branchEffectCardSuccess", "「%2$s」を所有している(%1$s)") \
    X(branchEffectCardFailure, "branchEffectCardFailure", "「%2$s」を所有していない(%1$s)") \
    X(branchEffectCardForSelectedCardSuccess, "branchEffectCardForSelectedCardSuccess", "「%1$s」と選択カードが一致する") \
    X(branchEffectCardForSelectedCardFailure, "branchEffectCardForSelectedCardFailure", "「%1$s」と選択カードが一致しない") \
    X(branchInfoSuccess, "branchInfoSuccess", "「%1$s」を所有している") \
    X(branchInfoFailure, "branchInfoFailure", "「%1$s」を所有していない") \
    X(branchMoneySuccess, "branchMoneySuccess", "%1$ssp以上所持している") \
    X(branchMoneyFailure, "branchMoneyFailure", "%1$ssp以上所持していない") \
    X(branchCouponSuccess, "branchCouponSuccess", "称号「%2$s」を所有している(%1$s)") \
    X(branchCouponFailure, "branchCouponFailure", "称号「%2$s」を所有していない(%1$s)") \
    X(branchCouponMultiSuccess, "branchCouponMultiSuccess", "称号%2$sを%3$s所有している(%1$s)") \
    X(branchCouponMultiFailure, "branchCouponMultiFailure", "称号%2$sを%3$s所有していない(%1$s)") \
    X(branchCompleteSuccess, "branchCompleteSuccess", "シナリオ「%1$s」が終了済みである") \
    X(branchCompleteFailure, "branchCompleteFailure", "シナリオ「%1$s」が終了済みでない") \
    X(branchGossipSuccess, "branchGossipSuccess", "ゴシップ「%1$s」が宿屋にある") \
    X(branchGossipFailure, "branchGossipFailure", "ゴシップ「%1$s」が宿屋に無い") \
    X(branchStepCmpGreater, "branchStepCmpGreater", "ステップ「%1$s」が「%2$s」より大きい") \
    X(branchStepCmpLesser, "branchStepCmpLesser", "ステップ「%1$s」が「%2$s」より小さい") \
    X(branchStepCmpEq, "branchStepCmpEq", "ステップ「%1$s」が「%2$s」と同値") \
    X(xmlBranchStepValueGreater, "xmlBranchStepValueGreater", "FROMステップはTOステップより大きい") \
    X(xmlBranchStepValueLesser, "xmlBranchStepValueLesser", "FROMステップ1はTOステップ2より小さい") \
    X(xmlBranchStepValueEq, "xmlBranchStepValueEq", "FROMステップ1はTOステップ2と同値") \
    X(branchFlagCmpNotEq, "branchFlagCmpNotEq", "フラグ「%1$s」と「%2$s」の値が異なる") \
    X(branchFlagCmpEq, "branchFlagCmpEq", "フラグ「%1$s」が「%2$s」と同値") \
    X(branchRandomSelectSuccess, "branchRandomSelectSuccess", "%2$sのキャラクタを選択(%1$s)") \
    X(branchRandomSelectFailure, "branchRandomSelectFailure", "%2$sのキャラクタ選択に失敗(%1$s)") \
    X(branchRandomSelectSuccessInvert, "branchRandomSelectSuccessInvert", "%2$sでないキャラクタを選択(%1$s)") \
    X(branchRandomSelectFailureInvert, "branchRandomSelectFailureInvert", "%2$sでないキャラクタ選択に失敗(%1$s)") \
    X(branchRandomSelectSuccessN, "branchRandomSelectSuccessN", "キャラクタを選択(%1$s)") \
    X(branchRandomSelectFailureN, "branchRandomSelectFailureN", "キャラクタ選択に失敗(%1$s)") \
    X(randomSelectCondition1, "randomSelectCondition1", "レベル%1$s～%2$s") \
    X(randomSelectCondition2, "randomSelectCondition2", "状態が%1$s") \
    X(randomSelectCondition3, "randomSelectCondition3", "レベル%1$s～%2$sで状態が%3$s") \
    X(branchKeyCodeWithConditionAllTypeSuccess, "branchKeyCodeWithConditionAllTypeSuccess", "キーコード「%1$s」を%2$sカードを所有している(%3$s)") \
    X(branchKeyCodeWithConditionAllTypeFailure, "branchKeyCodeWithConditionAllTypeFailure", "キーコード「%1$s」を%2$sカードを所有していない(%3$s)") \
    X(branchKeyCodeWithConditionSuccess, "branchKeyCodeWithConditionSuccess", "キーコード「%1$s」を%2$s%3$sを所有している(%4$s)") \
    X(branchKeyCodeWithConditionFailure, "branchKeyCodeWithConditionFailure", "キーコード「%1$s」を%2$s%3$sを所有していない(%4$s)") \
    X(branchKeyCodeWithConditionAllTypeForSelectedCardSuccess, "branchKeyCodeWithConditionAllTypeForSelectedCardSuccess", "キーコード「%1$s」を%2$s選択カードが存在する") \
    X(branchKeyCodeWithConditionAllTypeForSelectedCardFailure, "branchKeyCodeWithConditionAllTypeForSelectedCardFailure", "キーコード「%1$s」を%2$s選択カードが存在しない") \
    X(branchKeyCodeWithConditionForSelectedCardSuccess, "branchKeyCodeWithConditionForSelectedCardSuccess", "キーコード「%1$s」を%2$s%3$sの選択カードが存在する") \
    X(branchKeyCodeWithConditionForSelectedCardFailure, "branchKeyCodeWithConditionForSelectedCardFailure", "キーコード「%1$s」を%2$s%3$sの選択カードが存在しない") \
    X(targetIsSkill, "targetIsSkill", "特殊技能") \
    X(targetIsItem, "targetIsItem", "アイテム") \
    X(targetIsBeast, "targetIsBeast", "召喚獣") \
    X(targetIsHand, "targetIsHand", "手札") \
    X(targetSeparator, "targetSeparator", "・") \
    X(branchRound, "branchRound", "バトルが%1$sラウンド%2$s") \
    X(branchMultiCouponSuccess, "branchMultiCouponSuccess", "%1$sが称号「%2$s」を所有している") \
    X(branchMultiCouponFailure, "branchMultiCouponFailure", "%1$sが全ての称号を所有していない") \
    X(xmlBranchMultiCouponSuccess, "xmlBranchMultiCouponSuccess", "対象が称号「%2$s」を所有している") \
    X(xmlBranchMultiCouponFailure, "xmlBranchMultiCouponFailure", "対象が全ての称号を所有していない") \
    X(branchVariantSuccess, "branchVariantSuccess", "TRUE =〔 %1$s 〕") \
    X(branchVariantFailure, "branchVariantFailure", "FALSE =〔 %1$s 〕") \
    X(physicalNameDex, "physicalNameDex", "器用度") \
    X(physicalNameAgl, "physicalNameAgl", "敏捷度") \
    X(physicalNameInt, "physicalNameInt", "知力") \
    X(physicalNameStr, "physicalNameStr", "筋力") \
    X(physicalNameVit, "physicalNameVit", "生命力") \
    X(physicalNameMin, "physicalNameMin", "精神力") \
    X(mentalNameAggressive, "mentalNameAggressive", "好戦性") \
    X(mentalNameUnaggressive, "mentalNameUnaggressive", "平和性") \
    X(mentalNameCheerful, "mentalNameCheerful", "社交性") \
    X(mentalNameUncheerful, "mentalNameUncheerful", "内向性") \
    X(mentalNameBrave, "mentalNameBrave", "勇猛性") \
    X(mentalNameUnbrave, "mentalNameUnbrave", "臆病性") \
    X(mentalNameCautious, "mentalNameCautious", "慎重性") \
    X(mentalNameUncautious, "mentalNameUncautious", "大胆性") \
    X(mentalNameTrickish, "mentalNameTrickish", "狡猾性") \
    X(mentalNameUntrickish, "mentalNameUntrickish", "正直性") \
    X(statusNameActive, "statusNameActive", "行動可能") \
    X(statusNameInactive, "statusNameInactive", "行動不可") \
    X(statusNameAlive, "statusNameAlive", "生存") \
    X(statusNameDead, "statusNameDead", "非生存") \
    X(statusNameFine, "statusNameFine", "健康") \
    X(statusNameInjured, "statusNameInjured", "負傷") \
    X(statusNameHeavyInjured, "statusNameHeavyInjured", "重傷") \
    X(statusNameUnconscious, "statusNameUnconscious", "意識不明") \
    X(statusNamePoison, "statusNamePoison", "中毒") \
    X(statusNameSleep, "statusNameSleep", "眠り") \
    X(statusNameBind, "statusNameBind", "呪縛") \
    X(statusNameParalyze, "statusNameParalyze", "麻痺/石化") \
    X(statusNameConfuse, "statusNameConfuse", "混乱") \
    X(statusNameOverheat, "statusNameOverheat", "激昂") \
    X(statusNameBrave, "statusNameBrave", "勇敢") \
    X(statusNamePanic, "statusNamePanic", "恐慌") \
    X(statusNameSilence, "statusNameSilence", "沈黙") \
    X(statusNameFaceUp, "statusNameFaceUp", "暴露") \
    X(statusNameAntiMagic, "statusNameAntiMagic", "魔法無効化") \
    X(statusNameUpAction, "statusNameUpAction", "行動力上昇") \
    X(statusNameUpAvoid, "statusNameUpAvoid", "回避力上昇") \
    X(statusNameUpResist, "statusNameUpResist", "抵抗力上昇") \
    X(statusNameUpDefense, "statusNameUpDefense", "防御力上昇") \
    X(statusNameDownAction, "statusNameDownAction", "行動力低下") \
    X(statusNameDownAvoid, "statusNameDownAvoid", "回避力低下") \
    X(statusNameDownResist, "statusNameDownResist", "抵抗力低下") \
    X(statusNameDownDefense, "statusNameDownDefense", "防御力低下") \
    X(statusNameNone, "statusNameNone", "状態指定無し") \
    X(effectTypeElement, "effectTypeElement", "%1$s属性") \
    X(effectTypeNamePhysic, "effectTypeNamePhysic", "物理") \
    X(effectTypeNameMagic, "effectTypeNameMagic", "魔法") \
    X(effectTypeNameMagicalPhysic, "effectTypeNameMagicalPhysic", "魔法的物理") \
    X(effectTypeNamePhysicalMagic, "effectTypeNamePhysicalMagic", "物理的魔法") \
    X(effectTypeNameNone, "effectTypeNameNone", "無") \
    X(effectTypeDescPhysic, "effectTypeDescPhysic", "武器が効かない存在には無効") \
    X(effectTypeDescMagic, "effectTypeDescMagic", "魔法が効かない存在には無効") \
    X(effectTypeDescMagicalPhysic, "effectTypeDescMagicalPhysic", "武器と魔法の両方が効かない存在には無効") \
    X(effectTypeDescPhysicalMagic, "effectTypeDescPhysicalMagic", "武器と魔法のどちらかが効かない存在には無効") \
    X(effectTypeDescNone, "effectTypeDescNone", "全ての存在に有効") \
    X(resistNameAvoid, "resistNameAvoid", "回避属性") \
    X(resistNameResist, "resistNameResist", "抵抗属性") \
    X(resistNameUnfail, "resistNameUnfail", "必中属性") \
    X(resistDescAvoid, "resistDescAvoid", "回避された場合は効果無し") \
    X(resistDescResist, "resistDescResist", "抵抗された場合は効果半減") \
    X(resistDescUnfail, "resistDescUnfail", "絶対成功") \
    X(cardTargetNameNone, "cardTargetNameNone", "対象無し") \
    X(cardTargetNameUser, "cardTargetNameUser", "使用者") \
    X(cardTargetNameParty, "cardTargetNameParty", "味方") \
    X(cardTargetNameEnemy, "cardTargetNameEnemy", "敵方") \
    X(cardTargetNameBoth, "cardTargetNameBoth", "双方") \
    X(cardTargetOne, "cardTargetOne", "一体") \
    X(cardTargetAll, "cardTargetAll", "全体") \
    X(cardVisualNameNone, "cardVisualNameNone", "視覚効果無し") \
    X(cardVisualNameReverse, "cardVisualNameReverse", "対象を反転") \
    X(cardVisualNameHorizontal, "cardVisualNameHorizontal", "対象を横に震動") \
    X(cardVisualNameVertical, "cardVisualNameVertical", "対象を縦に震動") \
    X(premiumNameNormal, "premiumNameNormal", "日用品 (破棄可)") \
    X(premiumNameRare, "premiumNameRare", "希少品 (破棄可)") \
    X(premiumNamePremium, "premiumNamePremium", "貴重品 (破棄不可)") \
    X(enhanceNameAction, "enhanceNameAction", "行動") \
    X(enhanceNameAvoid, "enhanceNameAvoid", "回避") \
    X(enhanceNameResist, "enhanceNameResist", "抵抗") \
    X(enhanceNameDefense, "enhanceNameDefense", "防御") \
    X(mentality, "mentality", "精神状態") \
    X(mentalityNameNormal, "mentalityNameNormal", "正常") \
    X(mentalityNameSleep, "mentalityNameSleep", "睡眠") \
    X(mentalityNameConfuse, "mentalityNameConfuse", "混乱") \
    X(mentalityNameOverheat, "mentalityNameOverheat", "激昂") \
    X(mentalityNameBrave, "mentalityNameBrave", "勇敢") \
    X(mentalityNamePanic, "mentalityNamePanic", "恐慌") \
    X(enhanceBonus, "enhanceBonus", "%1$sボーナス") \
    X(statusActive, "statusActive", "※ 行動可能 = (健康 | 負傷 | 重傷 | 中毒)") \
    X(statusInactive, "statusInactive", "※ 行動不可 = (意識不明 | 麻痺/石化 | 呪縛 | 眠り)") \
    X(statusAlive, "statusAlive", "※ 生存 = (健康 | 負傷 | 重傷 | 中毒 | 呪縛 | 眠り)") \
    X(statusDead, "statusDead", "※ 非生存 = (意識不明 | 麻痺/石化)") \
    X(targetNameSelected, "targetNameSelected", "選択中のメンバ") \
    X(targetNameUnselected, "targetNameUnselected", "選択中以外のメンバ") \
    X(targetNameRandom, "targetNameRandom", "誰か一人") \
    X(targetNameParty, "targetNameParty", "パーティ全員") \
    X(talkerNameSelected, "talkerNameSelected", "選択中") \
    X(talkerNameUnselected, "talkerNameUnselected", "選択中以外") \
    X(talkerNameRandom, "talkerNameRandom", "ランダム") \
    X(talkerNameCard, "talkerNameCard", "カード") \
    X(talkerNameNarration, "talkerNameNarration", "話者無し") \
    X(talkerNameImage, "talkerNameImage", "画像") \
    X(talkerNameValued, "talkerNameValued", "評価メンバ") \
    X(rangeNameSelected, "rangeNameSelected", "現在選択中のメンバ") \
    X(rangeNameRandom, "rangeNameRandom", "パーティの誰か一人") \
    X(rangeNameParty, "rangeNameParty", "パーティの全員") \
    X(rangeNameBackpack, "rangeNameBackpack", "荷物袋") \
    X(rangeNamePartyAndBackpack, "rangeNamePartyAndBackpack", "全体(荷物袋含む)") \
    X(rangeNameField, "rangeNameField", "フィールド全体") \
    X(rangeNameCouponHolder, "rangeNameCouponHolder", "称号所有者") \
    X(rangeNameCardTarget, "rangeNameCardTarget", "カードの使用対象") \
    X(rangeNameSelectedCard, "rangeNameSelectedCard", "選択カード") \
    X(rangeNameNpc, "rangeNameNpc", "同行キャスト") \
    X(rangeWithCoupon, "rangeWithCoupon", "称号所有者(%1$s)") \
    X(rangeWithNoCoupon, "rangeWithNoCoupon", "称号所有者(指定無し)") \
    X(rangeDescField, "rangeDescField", "パーティ・荷物袋・エネミーカードを含む") \
    X(rangeDescCardTarget, "rangeDescCardTarget", "使用時イベント中でない場合、対象無しになります") \
    X(rangeNameSelectedCardForReplace, "rangeNameSelectedCardForReplace", "選択カード(交換)") \
    X(castRangeNameParty, "castRangeNameParty", "パーティ") \
    X(castRangeNameEnemy, "castRangeNameEnemy", "敵") \
    X(castRangeNameNpc, "castRangeNameNpc", "同行キャスト") \
    X(damageTypeNameLevelRatio, "damageTypeNameLevelRatio", "レベルに対応する値") \
    X(damageTypeNameNormal, "damageTypeNameNormal", "値の直接入力") \
    X(damageTypeNameMax, "damageTypeNameMax", "最大値処理") \
    X(damageTypeNameFixed, "damageTypeNameFixed", "固定値") \
    X(elementNameAll, "elementNameAll", "全") \
    X(elementNameHealth, "elementNameHealth", "肉体") \
    X(elementNameMind, "elementNameMind", "精神") \
    X(elementNameMiracle, "elementNameMiracle", "神聖") \
    X(elementNameMagic, "elementNameMagic", "魔力") \
    X(elementNameFire, "elementNameFire", "炎") \
    X(elementNameIce, "elementNameIce", "冷気") \
    X(elementNameHearing, "elementNameHearing", "音") \
    X(elementNameVision, "elementNameVision", "視覚") \
    X(elementNameElectronic, "elementNameElectronic", "電子") \
    X(elementEffectiveAll, "elementEffectiveAll", "全ての存在に有効") \
    X(elementNoEffective, "elementNoEffective", "%1$s存在には無効") \
    X(elementEffective, "elementEffective", "%1$sに有効") \
    X(elementWeaknessOrResist, "elementWeaknessOrResist", "対象によって弱点または無効") \
    X(sexUnknown, "sexUnknown", "謎/？") \
    X(periodUnknown, "periodUnknown", "不明") \
    X(natureUnknown, "natureUnknown", "その他") \
    X(effectCardTypeNameAll, "effectCardTypeNameAll", "全てのカード") \
    X(effectCardTypeNameSkill, "effectCardTypeNameSkill", "特殊技能カード") \
    X(effectCardTypeNameItem, "effectCardTypeNameItem", "アイテムカード") \
    X(effectCardTypeNameBeast, "effectCardTypeNameBeast", "召喚獣カード") \
    X(effectCardTypeNameHand, "effectCardTypeNameHand", "戦闘時の手札") \
    X(comparison4NameEq, "comparison4NameEq", "であれば") \
    X(comparison4NameNe, "comparison4NameNe", "でなければ") \
    X(comparison4NameLt, "comparison4NameLt", "より大きければ") \
    X(comparison4NameGt, "comparison4NameGt", "より小さければ") \
    X(comparison3NameEq, "comparison3NameEq", "である") \
    X(comparison3NameLt, "comparison3NameLt", "より大きい") \
    X(comparison3NameGt, "comparison3NameGt", "より小さい") \
    X(comparison3FalseNameEq, "comparison3FalseNameEq", "ではない") \
    X(comparison3FalseNameLt, "comparison3FalseNameLt", "以下") \
    X(comparison3FalseNameGt, "comparison3FalseNameGt", "以上") \
    X(dlgTitComment, "dlgTitComment", "コメントの記述") \
    X(dlgTitCommentWith, "dlgTitCommentWith", "[ %1$s ] へのコメント") \
    X(cardTabName, "cardTabName", "%1$s") \
    X(handCardTabName, "handCardTabName", "%1$s.%2$s") \
    X(importSourceTabName, "importSourceTabName", "%1$s") \
    X(cardTitle, "cardTitle", "%1$s.%2$s") \
    X(dlgTitImportOption, "dlgTitImportOption", "参照先のインポート") \
    X(importOptionMaterials, "importOptionMaterials", "外部素材") \
    X(importOptionVariables, "importOptionVariables", "状態変数") \
    X(importOptionCasts, "importOptionCasts", "キャストカード") \
    X(importOptionSkills, "importOptionSkills", "特殊技能カード") \
    X(importOptionItems, "importOptionItems", "アイテムカード") \
    X(importOptionBeasts, "importOptionBeasts", "召喚獣カード") \
    X(importOptionInfos, "importOptionInfos", "情報カード") \
    X(importOptionAreas, "importOptionAreas", "エリア") \
    X(importOptionBattles, "importOptionBattles", "バトル") \
    X(importOptionPackages, "importOptionPackages", "パッケージ") \
    X(importOptionIncludedFiles, "importOptionIncludedFiles", "格納カードイメージ") \
    X(importOptionIncludedBgImages, "importOptionIncludedBgImages", "格納背景イメージ") \
    X(importOptionHands, "importOptionHands", "キャストの持ち札") \
    X(importOptionBeastsInMotions, "importOptionBeastsInMotions", "効果中の召喚獣") \
    X(importTypeIncludedNameExclude, "importTypeIncludedNameExclude", "格納であれば外部出力する") \
    X(importTypeIncludedNameInclude, "importTypeIncludedNameInclude", "参照であれば格納する") \
    X(importTypeIncludedNameAsIs, "importTypeIncludedNameAsIs", "そのままにする") \
    X(importTypeReference1NameRename, "importTypeReference1NameRename", "重複した場合は名前を変更する") \
    X(importTypeReference1NameNoOverwrite, "importTypeReference1NameNoOverwrite", "重複した場合はインポートしない") \
    X(importTypeReference1NameOverwrite, "importTypeReference1NameOverwrite", "重複した場合は上書きする") \
    X(importTypeReference1NameNoImport, "importTypeReference1NameNoImport", "インポートしない") \
    X(importTypeReference2NameRename, "importTypeReference1NameRename", "新しいIDでインポートする") \
    X(importTypeReference2NameNoImport, "importTypeReference1NameNoImport", "インポートしない") \
    X(importOverwriteScenarioInfo, "importOverwriteScenarioInfo", "カードのシナリオ名及びシナリオ作者の情報を書き換える") \
    X(dlgTitAddScenario, "dlgTitAddScenario", "インポート元の選択") \
    X(dlgTitImportResult, "dlgTitImportResult", "インポート対象の選択") \
    X(importResourceList, "importResourceList", "次のリソースのうち、チェックを入れたものがインポートされます。") \
    X(overwriteMark, "overwriteMark", "%1$s (上書き)") \
    X(cardIsReference, "cardIsReference", "「%1$s.%2$s」を参照しています") \
    X(referencedCardIsNotFound, "referencedCardIsNotFound", "参照先のカードが見つかりません(ID:%1$s)") \
    X(cardStatus, "cardStatus", "%1$s枚のカード") \
    X(cardStatusSelOne, "cardStatusSelOne", "%1$s枚のカード (ID = %2$s)") \
    X(cardStatusSelMulti, "cardStatusSelMulti", "%1$s枚のカード (%2$s枚を選択中)") \
    X(handCardStatus, "handCardStatus", "%1$s枚のカード (有効枚数 = %2$s)") \
    X(handCardStatusSelOne, "handCardStatusSelOne", "%1$s枚のカード (有効枚数 = %2$s) (ID = %3$s)") \
    X(handCardStatusSelMulti, "handCardStatusSelMulti", "%1$s枚のカード (有効枚数 = %2$s) (%3$s枚を選択中)") \
    X(showStatusTime, "showStatusTime", "状態の強度と持続時間") \
    X(showStatusTimeNameAlways, "showStatusTimeNameAlways", "常に表示") \
    X(showStatusTimeNameWithSkin, "showStatusTimeNameWithSkin", "スキン使用時のみ表示") \
    X(showStatusTimeNameNo, "showStatusTimeNameNo", "表示しない") \
    X(cwCast, "cwCast", "キャスト") \
    X(skill, "skill", "特殊技能") \
    X(item, "item", "アイテム") \
    X(beast, "beast", "召喚獣") \
    X(info, "info", "情報") \
    X(noSelectImage, "noSelectImage", "(指定無し)") \
    X(noImage, "noImage", "存在しないイメージ(パス:%1$s)") \
    X(noSelectBGM, "noSelectBGM", "(指定無し)") \
    X(noBGM, "noBGM", "存在しないBGM(パス:%1$s)") \
    X(noSelectSE, "noSelectSE", "(指定無し)") \
    X(noSE, "noSE", "存在しない効果音(パス:%1$s)") \
    X(noSelectArea, "noSelectArea", "(指定無し)") \
    X(noArea, "noArea", "存在しないエリア(ID:%1$s)") \
    X(noSelectBattle, "noSelectBattle", "(指定無し)") \
    X(noBattle, "noBattle", "存在しないバトル(ID:%1$s)") \
    X(noSelectPackage, "noSelectPackage", "(指定無し)") \
    X(noPackage, "noPackage", "存在しないパッケージ(ID:%1$s)") \
    X(noSelectCast, "noSelectCast", "(指定無し)") \
    X(noCast, "noCast", "存在しないキャストカード(ID:%1$s)") \
    X(noSelectSkill, "noSelectSkill", "(指定無し)") \
    X(noSkill, "noSkill", "存在しない特殊技能カード(ID:%1$s)") \
    X(noSelectItem, "noSelectItem", "(指定無し)") \
    X(noItem, "noItem", "存在しないアイテムカード(ID:%1$s)") \
    X(noSelectBeast, "noSelectBeast", "(指定無し)") \
    X(noBeast, "noBeast", "存在しない召喚獣カード(ID:%1$s)") \
    X(noSelectInfo, "noSelectInfo", "(指定無し)") \
    X(noInfo, "noInfo", "存在しない情報カード(ID:%1$s)") \
    X(noSelectFlag, "noSelectFlag", "(指定無し)") \
    X(noFlag, "noFlag", "存在しないフラグ(パス:%1$s)") \
    X(noSelectStep, "noSelectStep", "(指定無し)") \
    X(noStep, "noStep", "存在しないステップ(パス:%1$s)") \
    X(noSelectVariant, "noSelectVariant", "(指定無し)") \
    X(noVariant, "noVariant", "存在しないコモン(パス:%1$s)") \
    X(noSelectStart, "noSelectStart", "(指定無し)") \
    X(noStart, "noStart", "存在しないスタートコンテント(パス:%1$s)") \
    X(noSelectCoupon, "noSelectCoupon", "(指定無し)") \
    X(noSelectCompleteStamp, "noSelectCompleteStamp", "(指定無し)") \
    X(noSelectGossip, "noSelectGossip", "(指定無し)") \
    X(noSelectCellName, "noSelectCellName", "(指定無し)") \
    X(noSelectCardGroup, "noSelectCardGroup", "(指定無し)") \
    X(noSelectTarget, "noSelectTarget", "(指定無し)") \
    X(noEffect, "noEffect", "(指定無し)") \
    X(noKeyCode, "noKeyCode", "(指定無し)") \
    X(noText, "noText", "(文章無し)") \
    X(noExpression, "noExpression", "(式無し)") \
    X(noInformation, "noInformation", "(情報を取得できません)") \
    X(cardId, "cardId", "ID") \
    X(cardName, "cardName", "名称") \
    X(cardDesc, "cardDesc", "説明") \
    X(infinity, "infinity", "∞") \
    X(dlgTitNewCast, "dlgTitNewCast", "キャストカードの作成") \
    X(dlgTitNewSkill, "dlgTitNewSkill", "特殊技能カードの作成") \
    X(dlgTitNewItem, "dlgTitNewItem", "アイテムカードの作成") \
    X(dlgTitNewBeast, "dlgTitNewBeast", "召喚獣カードの作成") \
    X(dlgTitNewInfo, "dlgTitNewInfo", "情報カードの作成") \
    X(dlgTitCast, "dlgTitCast", "キャストカードの設定 [ %1$s ]") \
    X(dlgTitSkill, "dlgTitSkill", "特殊技能カードの設定 [ %1$s ]") \
    X(dlgTitItem, "dlgTitItem", "アイテムカードの設定 [ %1$s ]") \
    X(dlgTitBeast, "dlgTitBeast", "召喚獣カードの設定 [ %1$s ]") \
    X(dlgTitInfo, "dlgTitInfo", "情報カードの設定 [ %1$s ]") \
    X(name, "name", "名前") \
    X(nameLimit, "nameLimit", "(%2$s文字まで)") \
    X(level, "level", "レベル") \
    X(life, "life", "体力") \
    X(lifeCalc, "lifeCalc", "標準値") \
    X(history, "history", "経歴") \
    X(coupons, "coupons", "経歴") \
    X(addCoupon, "addCoupon", "新規クーポンの追加") \
    X(altCoupon, "altCoupon", "クーポンの上書き") \
    X(delCoupon, "delCoupon", "クーポンの削除") \
    X(sexTitle, "sexTitle", "性別") \
    X(periodTitle, "periodTitle", "年代") \
    X(race, "race", "種族") \
    X(noRace, "noRace", "未指定") \
    X(natureTitle, "natureTitle", "素質") \
    X(makingsTitle, "makingsTitle", "特性") \
    X(tolerant, "tolerant", "対属性") \
    X(tolerantBase, "tolerantBase", "対カード属性") \
    X(tolerantElement, "tolerantElement", "対効果属性") \
    X(resistWeapon, "resistWeapon", "武器が効かない") \
    X(resistMagic, "resistMagic", "魔法が効かない") \
    X(undead, "undead", "命を持たない") \
    X(automaton, "automaton", "心を持たない") \
    X(unholy, "unholy", "不浄な存在") \
    X(constructure, "constructure", "魔法生物") \
    X(resistText, "resistText", "%1$sに耐性を持つ") \
    X(weaknessText, "weaknessText", "%1$sに弱い") \
    X(descResistWeapon, "descResistWeapon", "(物理属性のカードが無効)") \
    X(descResistMagic, "descResistMagic", "(魔法属性のカードが無効)") \
    X(descEffective, "descEffective", "(%1$s属性の効果が有効)") \
    X(descResist, "descResist", "(%1$s属性の効果が無効)") \
    X(descWeakness, "descWeakness", "(%1$s属性の効果に影響)") \
    X(basicResist, "basicResist", "標準値") \
    X(physicalParams, "physicalParams", "身体能力") \
    X(physicalSum, "physicalSum", "合計値: %1$s") \
    X(physicalCalc, "physicalCalc", "標準値") \
    X(mentalParams, "mentalParams", "精神傾向") \
    X(mentalCalc, "mentalCalc", "標準値") \
    X(castEnhance, "castEnhance", "能力修正") \
    X(basicEnhance, "basicEnhance", "標準値") \
    X(aptitude, "aptitude", "適性") \
    X(actionOrder, "actionOrder", "先駆け") \
    X(actionOrderDesc, "actionOrderDesc", "バトルでの行動順に影響します") \
    X(resilience, "resilience", "回復力") \
    X(resilienceDesc, "resilienceDesc", "中毒や麻痺の回復速度に影響します") \
    X(avoidance, "avoidance", "回避力") \
    X(avoidanceDesc, "avoidanceDesc", "回避の成功率に影響します") \
    X(resistance, "resistance", "抵抗力") \
    X(resistanceDesc, "resistanceDesc", "抵抗の成功率に影響します") \
    X(runAwaySpeed, "runAwaySpeed", "逃げ足") \
    X(runAwaySpeedDesc, "runAwaySpeedDesc", "パーティの逃走の成功率に影響します") \
    X(aptitudeHint, "aptitudeHint", "%1$s(%2$s+%3$s)") \
    X(liveStatus, "liveStatus", "初期状態") \
    X(lifeAndMentality, "lifeAndMentality", "体力と精神状態") \
    X(enhanceLiveBonus, "enhanceLiveBonus", "能力ボーナス/ペナルティ") \
    X(enhanceLiveBonusNameAction, "enhanceLiveBonusNameAction", "行動") \
    X(enhanceLiveBonusNameAvoid, "enhanceLiveBonusNameAvoid", "回避") \
    X(enhanceLiveBonusNameResist, "enhanceLiveBonusNameResist", "抵抗") \
    X(enhanceLiveBonusNameDefense, "enhanceLiveBonusNameDefense", "防御") \
    X(useMax, "useMax", "最大値を使用") \
    X(status, "status", "異常状態") \
    X(paralyze, "paralyze", "麻痺/石化") \
    X(poison, "poison", "中毒") \
    X(bind, "bind", "呪縛") \
    X(silence, "silence", "沈黙") \
    X(faceUp, "faceUp", "暴露") \
    X(antiMagic, "antiMagic", "魔法無効") \
    X(unitValue, "unitValue", "点") \
    X(unitRound, "unitRound", "ラウンド") \
    X(resetLiveStatus, "resetLiveStatus", "通常状態に戻す") \
    X(showStyleNameInvisible, "showStyleNameInvisible", "表示しない") \
    X(showStyleNameCenter, "showStyleNameCenter", "画面中央に表示") \
    X(showStyleNameFrontOfUser, "showStyleNameFrontOfUser", "使用者の手前に表示") \
    X(workConditionGroup, "workConditionGroup", "発動条件") \
    X(needSpell, "needSpell", "沈黙時に発動不可") \
    X(needSpellAndActive, "needSpellAndActive", "沈黙・行動不能時に発動不可") \
    X(elementProps, "elementProps", "効果属性") \
    X(linkOption, "linkOption", "参照設定") \
    X(beastMaxNest, "beastMaxNest", "ネスト可能回数") \
    X(showStyleForBeastCard, "showStyleForBeastCard", "発動時の視覚効果") \
    X(warningShowStyleForBeastCard, "warningShowStyleForBeastCard", "発動時の視覚効果の設定は、Wsn.4以降の形式のシナリオでしか行えません。") \
    X(resistProps, "resistProps", "抵抗属性") \
    X(aptPhysical, "aptPhysical", "身体的要素") \
    X(aptMental, "aptMental", "精神的要素") \
    X(skillLevel, "skillLevel", "技能レベル") \
    X(useCount, "useCount", "使用回数") \
    X(useCountGroup, "useCountGroup", "使用可能回数") \
    X(useCountRange, "useCountRange", "(0～%1$s : 0 = ∞)") \
    X(useCountCur, "useCountCur", "現在") \
    X(useCountMax, "useCountMax", "最大") \
    X(useCountIsMax, "useCountIsMax", "最大回数を使用") \
    X(price, "price", "価格") \
    X(priceAuto, "priceAuto", "(参考用)") \
    X(useModify, "useModify", "使用時 能力値修正") \
    X(haveModify, "haveModify", "所有時 能力値修正") \
    X(motionKind, "motionKind", "効果種別") \
    X(motionElement, "motionElement", "属性") \
    X(motionDamageType, "motionDamageType", "タイプ") \
    X(calcType, "calcType", "効果値タイプ") \
    X(motionValue, "motionValue", "値") \
    X(motionBeast, "motionBeast", "召喚するカード") \
    X(beastNone, "beastNone", "召喚獣無し") \
    X(setBeast, "setBeast", "選択") \
    X(motionRound, "motionRound", "継続時間 (ラウンド数)") \
    X(motionEnhValue, "motionEnhValue", "変化値") \
    X(effectTarget, "effectTarget", "効果目標") \
    X(effectRange, "effectRange", "効果範囲") \
    X(effectVisual, "effectVisual", "視覚効果") \
    X(cardPremium, "cardPremium", "カードの価値") \
    X(successRate, "successRate", "成功率修正値") \
    X(allFail, "allFail", "絶対失敗\n(-5)") \
    X(allSuccess, "allSuccess", "絶対成功\n(+5)") \
    X(se, "se", "効果音") \
    X(se1, "se1", "初期効果") \
    X(se2, "se2", "二次効果") \
    X(initialEffectAndSound, "initialEffectAndSound", "効果音と初期効果") \
    X(hasInitialEffect, "hasInitialEffect", "色反転と初期効果音の再生を行う") \
    X(warningInitialEffect, "warningInitialEffect", "色反転と初期効果音再生の指定はWsn.4以降の形式のシナリオでしか行えません。") \
    X(warningAbsorbToSelected, "warningAbsorbToSelected", "吸収者の指定はWsn.4以降の形式のシナリオでしか行えません。") \
    X(soundNone, "soundNone", "効果音無し") \
    X(noSelect, "noSelect", "指定無し") \
    X(keyCodes, "keyCodes", "イベント発火のキーコード") \
    X(addKeyCode, "addKeyCode", "キーコードの追加") \
    X(delKeyCode, "delKeyCode", "キーコードの削除") \
    X(selectFeatures, "selectFeatures", "キーコードの自動生成") \
    X(selectFeaturesHint, "selectFeaturesHint", "特徴と効果からキーコードを自動生成します。\n全体的な特徴を選択してください。") \
    X(addKeyCodesError, "addKeyCodesError", "追加できなかったキーコード") \
    X(addKeyCodesErrorDesc, "addKeyCodesErrorDesc", "クラシックなシナリオではキーコードが%1$s件までに制限されているため、次のキーコードは追加できませんでした。") \
    X(behaviorOfBeastCard, "behaviorOfBeastCard", "発動/消滅") \
    X(invocationCondition, "invocationCondition", "発動条件") \
    X(removalCondition, "removalCondition", "消滅条件") \
    X(removeWithUnconscious, "removeWithUnconscious", "意識不明時に消滅する") \
    X(warningNotDefaultSE, "warningNotDefaultSE", "標準以外の効果音はシナリオの外では鳴らない可能性があります。") \
    X(warningEffectTypeNone, "warningEffectTypeNone", "無属性のカードをシナリオ外に持ち出した場合、予期せぬ動作の原因になります。") \
    X(warningVanishCast, "warningVanishCast", "神聖属性以外の対象消去効果を持つカードをシナリオ外に持ち出した場合、予期せぬ動作の原因になります。") \
    X(warningNameLenOver, "warningNameLenOver", "名前の長さが%2$s文字を超えています。メッセージにカード名が表示された際に不具合が発生する可能性があります。") \
    X(warningPCNumberClassic, "warningPCNumberClassic", "プレイヤーキャラクタのイメージはCardWirth 1.30より前のバージョンでは表示されません。") \
    X(warningUnknownContent, "warningUnknownContent", "イベント [%1$s] はCardWirth %2$sより前のバージョンでは使用できません。") \
    X(warningUnknownContentWsn, "warningUnknownContentWsn", "イベント [%1$s] はWsn.%2$sより前のバージョンでは使用できません。") \
    X(warningBranchCouponAtField, "warningBranchCouponAtField", "フィールド全体でのクーポン所持判定は、CardWirth 1.30より前のバージョンでは使用できません。") \
    X(warningBranchCouponAtNpc, "warningBranchCouponAtNpc", "同行キャストのクーポン所持判定は、Wsn.5以降の形式のシナリオでしか行えません。") \
    X(warningSexCoupon, "warningSexCoupon", "「%1$s」は性別を示すクーポンであるため、経歴に使用できません。") \
    X(warningPeriodCoupon, "warningPeriodCoupon", "「%1$s」は年代を示すクーポンであるため、経歴に使用できません。") \
    X(warningNatureCoupon, "warningNatureCoupon", "「%1$s」は素質を示すクーポンであるため、経歴に使用できません。") \
    X(warningMakingCoupon, "warningMakingCoupon", "「%1$s」は特性を示すクーポンであるため、経歴に使用できません。") \
    X(warningSystemVarName, "warningSystemVarName", "%1$sで始まる名前の状態変数は、プレイヤーの環境によっては正しく機能しない事があります。") \
    X(warningSystemCoupon, "warningSystemCoupon", "%1$sで始まる名前のクーポンを操作する事はできません。") \
    X(warningSystemCouponForHistory, "warningSystemCouponForHistory", "キャストカードに設定されたシステムクーポンは正常に動作しない可能性があります。") \
    X(warningBranchStatusMental, "warningBranchStatusMental", "%1$s状態の判定は、CardWirth %2$sより前のバージョンでは使用できません。") \
    X(warningValuedTalker, "warningValuedTalker", "評価メンバは、CardWirth 1.50より前のバージョンでは使用できません。") \
    X(warningTextCell, "warningTextCell", "テキストセルは、CardWirth 1.50より前のバージョンでは使用できません。") \
    X(warningColorCell, "warningColorCell", "カラーセルは、CardWirth 1.50より前のバージョンでは使用できません。") \
    X(warningColorCellNoGradientAlphaBlend160, "warningColorCellNoGradientAlphaBlend160", "CardWirth 1.60では「通常」以外の合成方法で不透明度255未満、かつグラデーション無しの時に表示がおかしくなります。基本色と終端色が同色のグラデーションにする事で回避できます。") \
    X(warningPCCell, "warningPCCell", "プレイヤーキャラクタセルは、Wsn.1以降の形式のシナリオでしか使用できません。") \
    X(warningExpandedPCCell, "warningExpandedPCCell", "セルに合わせて拡大・縮小するプレイヤーキャラクタセルは、Wsn.1以降の形式のシナリオでしか使用できません。") \
    X(warningEffectBoosterFileWithReplaceBgImage, "warningEffectBoosterFileWithReplaceBgImage", "エフェクトブースター関係の背景セルは無視するように指定されています。") \
    X(warningBgImageIncluded, "warningBgImageIncluded", "背景セルのイメージ格納は、CardWirth 1.60より前のバージョンでは行えません。") \
    X(warningBgImageCellName, "warningBgImageCellName", "背景セル名称は、Wsn.1以降の形式のシナリオでしか設定できません。") \
    X(warningCardGroup, "warningCardGroup", "カードグループは、Wsn.3以降の形式のシナリオでしか設定できません。") \
    X(warningSelectionBarIsMany, "warningSelectionBarIsMany", "選択肢が%1$s行ありますが、%2$s行までしか表示できません。") \
    X(warningEveryRound, "warningEveryRound", "イベント発火条件「毎ラウンド」は、CardWirth 1.50より前のバージョンでは使用できません。") \
    X(warningRoundEnd, "warningRoundEnd", "イベント発火条件「ラウンド終了」は、Wsn.4以降の形式のシナリオでしか機能しません。") \
    X(warningRound0, "warningRound0", "イベント発火条件「バトル開始」は、CardWirth 1.50より前のバージョンでは使用できません。") \
    X(warningUnknownMotion, "warningUnknownMotion", "効果 [%1$s] は、CardWirth %2$sより前のバージョンでは使用できません。") \
    X(warningUnknownMotionWsn, "warningUnknownMotionWsn", "効果 [%1$s] は、Wsn.%2$sより前のバージョンでは使用できません。") \
    X(warningTextColor, "warningTextColor", "テキスト色 [%1$s] は、CardWirth %2$sより前のバージョンでは使用できません。") \
    X(warningStepCount, "warningStepCount", "%1$s段階以外のステップはクラシックなシナリオでは使用できません。") \
    X(warningStepOverCount, "warningStepOverCount", "ステップ「%1$s」の段階数 [%2$s] より大きなステップ値 [%3$s] が指定されています。") \
    X(warningKeyCodeCount, "warningKeyCodeCount", "%1$s件より多くのキーコードはクラシックなシナリオでは設定できません。") \
    X(warningHasNotKeyCode, "warningHasNotKeyCode", "キーコードの不保有発火条件はCardWirth 1.50より前のバージョンでは機能しません。") \
    X(warningValuedSelectionMethod, "warningValuedSelectionMethod", "評価条件によるメンバ選択分岐は、Wsn.1以降の形式のシナリオしか行えません。") \
    X(warningSkillPowerWithFixedValue, "warningSkillPowerWithFixedValue", "精神力操作の固定値指定は、Wsn.1以降の形式のシナリオしか行えません。") \
    X(warningIncludedImage, "warningIncludedImage", "WSN形式のシナリオでは格納イメージは使用できません。") \
    X(warningTransitionType, "warningTransitionType", "背景切替方式の指定は、WSN形式のシナリオでしか行なえません。") \
    X(warningSingleLineMessage, "warningSingleLineMessage", "単行メッセージは、Wsn.5以降の形式のシナリオでしか使用できません。") \
    X(warningSelectionColumns, "warningSelectionColumns", "選択肢の複数列表示は、Wsn.1以降の形式のシナリオしか行えません。") \
    X(warningRunAwayCard, "warningRunAwayCard", "キーコード「%1$s」付きのカードは死亡イベントを発生させないため、シナリオを誤動作させる可能性があります。") \
    X(warningEndOrChangeAreaInRound0, "warningEndOrChangeAreaInRound0", "クラシックエンジンのバグにより、バトル開始イベント中にシナリオ終了やエリア移動を行うと、プレイヤーのデータの破損を含めた異常が発生する可能性があります。") \
    X(warningWsnSystemCoupon, "warningWsnSystemCoupon", "システムクーポン「%1$s」はWsn.%2$s以降の形式のシナリオでしか機能しません。") \
    X(warningCanNotGetSetCoupon, "warningCanNotGetSetCoupon", "システムクーポン「%1$s」を操作する事はできません。") \
    X(warningCardImagePosition, "warningCardImagePosition", "イメージの配置形式の指定は、Wsn.2以降の形式のシナリオでしか行えません。") \
    X(warningStartAction, "warningStartAction", "戦闘行動開始タイミングの指定は、Wsn.2以降の形式のシナリオしか行えません。") \
    X(warningIgnite, "warningIgnite", "イベントの発火有無の指定は、Wsn.2以降の形式のシナリオしか行えません。") \
    X(warningIgnoreKeyCode, "warningIgnoreKeyCode", "イベント発火無しに指定されているため、設定されたキーコードは機能しません。") \
    X(warningBranchKeyCodeAtClassic, "warningBranchKeyCodeAtClassic", "クラシックなシナリオにおけるキーコード所持分岐は、カードの種類に特殊技能・アイテム及び手札・召喚獣のいずれか単独、またはそれら全てを指定しなければ正しく機能しません。") \
    X(warningBranchKeyCodeAtWsn1, "warningBranchKeyCodeAtWsn1", "Wsn.1以前のシナリオにおけるキーコード所持分岐は、カードの種類に特殊技能・アイテム・召喚獣のいずれか単独、またはそれら全てを指定しなければ正しく機能しません。") \
    X(warningBranchKeyCodeWithItem, "warningBranchKeyCodeWithItem", "クラシックなシナリオにおけるキーコード所持分岐でカードの種類にアイテムが含まれている場合は、戦闘時の手札も検索対象となります。") \
    X(warningLoadScaledImage, "warningLoadScaledImage", "スケーリングされたイメージファイルの読み込みは、Wsn.2以降の形式のシナリオに対応したエンジンでしか機能しません。") \
    X(warningPlayerCardEvents, "warningPlayerCardEvents", "プレイヤーカードに対するイベント設定は、Wsn.2以降の形式のシナリオしか行えません。") \
    X(warningCouponHolder, "warningCouponHolder", "%1$sコンテントにおける適用範囲 [称号所有者] の指定は、Wsn.%2$s以降の形式のシナリオしか行えません。") \
    X(warningNoHoldingCoupon, "warningNoHoldingCoupon", "範囲指定用の称号が設定されていません。") \
    X(warningCardTarget, "warningCardTarget", "適用範囲 [カードの使用対象] の指定は、Wsn.2以降の形式のシナリオしか行えません。") \
    X(warningRefAbility, "warningRefAbility", "選択メンバの能力参照は、Wsn.2以降の形式のシナリオでしか行えません。") \
    X(warningCenteringX, "warningCenteringX", "横方向の中央寄せ表示は、Wsn.2以降の形式のシナリオでしか行えません。") \
    X(warningCenteringY, "warningCenteringY", "縦方向の中央寄せ表示は、Wsn.2以降の形式のシナリオでしか行えません。") \
    X(warningBoundaryCheck, "warningBoundaryCheck", "メッセージの禁則処理は、Wsn.2以降の形式のシナリオでしか行えません。") \
    X(warningBranchCouponMulti, "warningBranchCouponMulti", "クーポン分岐のクーポンの複数指定は、Wsn.2以降の形式のシナリオしか行えません。") \
    X(warningExpandSPChars, "warningExpandSPChars", "状態変数内の特殊文字の展開は、Wsn.2以降の形式のシナリオしか行えません。") \
    X(warningExpandSPCharsInCoupon, "warningExpandSPCharsInCoupon", "クーポン内の特殊文字の展開は、Wsn.4以降の形式のシナリオしか行えません。") \
    X(warningExpandSPCharsInGossip, "warningExpandSPCharsInGossip", "ゴシップ内の特殊文字の展開は、Wsn.4以降の形式のシナリオしか行えません。") \
    X(warningExpandSPCharsInMenuCardName, "warningExpandSPCharsInMenuCardName", "メニューカード名内の特殊文字の展開は、Wsn.4以降の形式のシナリオしか行えません。") \
    X(warningSelectCard, "warningSelectCard", "選択カードの変更は、Wsn.3以降の形式のシナリオしか行えません。") \
    X(warningRangeSelectedCard, "warningRangeSelectedCard", "選択カードは、Wsn.3以降の形式のシナリオでしか指定できません。") \
    X(warningSelectTalker, "warningSelectTalker", "話者の選択は、Wsn.3以降の形式のシナリオしか行えません。") \
    X(warningInvertResult, "warningInvertResult", "条件に合わない場合に成功とする事は、Wsn.4以降の形式のシナリオでしかできません。") \
    X(warningInvertResultBranchKeyCode, "warningInvertResultBranchKeyCode", "条件該当カードの不保有で成功とする事は、Wsn.4以降の形式のシナリオでしかできません。") \
    X(warningHasNotBranchKeyCode, "warningHasNotBranchKeyCode", "キーコードを持たないカードで成功とする事は、Wsn.5以降の形式のシナリオでしかできません。") \
    X(warningInvocationCondition, "warningInvocationCondition", "「生存」以外の条件で発動する召喚獣は、Wsn.3以降の形式のシナリオでしか機能しません。") \
    X(warningRemoveWithUnconscious, "warningRemoveWithUnconscious", "意識不明時に消滅しない召喚獣は、Wsn.3以降の形式のシナリオでしか機能しません。") \
    X(warningInconsistencyStatus, "warningInconsistencyStatus", "%1$s状態と%2$s状態を同時に設定する事はできません。") \
    X(warningUnconsciousCondition, "warningUnconsciousCondition", "意識不明時に消滅するにもかかわらず意識不明状態が発動条件になっています。") \
    X(warningPossibleToRunAway, "warningPossibleToRunAway", "バトルの逃走不可設定はWsn.3以降の形式のシナリオでしか機能しません。") \
    X(warningNoIgniteRunAway, "warningNoIgniteRunAway", "逃走できないバトルのイベントに逃走発火条件が設定されています。") \
    X(warningConsumeCard, "warningConsumeCard", "カード消費の抑止は、Wsn.3以降の形式のシナリオでしか機能しません。") \
    X(warningOverrideEnemyCardName, "warningOverrideEnemyCardName", "エネミーカードの名前の上書きは、Wsn.4以降の形式のシナリオしか行えません。") \
    X(warningOverrideEnemyCardImage, "warningOverrideEnemyCardImage", "エネミーカードのイメージの上書きは、Wsn.4以降の形式のシナリオしか行えません。") \
    X(warningVariant, "warningVariant", "コモンはWsn.4以降の形式のシナリオでしか使用できません。") \
    X(warningLocalVariablesOfEffectCard, "warningLocalVariablesOfEffectCard", "カードのローカル変数はWsn.4以降の形式のシナリオでしか使用できません。") \
    X(warningExchangeIsItemInClassic, "warningExchangeIsItemInClassic", "クラシックなシナリオでは、最初のアイテムカードとして「%1$s」と同名のカードを所有していると、本来の「%1$s」が配付されません。") \
    X(warningNoActionCard, "warningNoActionCard", "エネミーカードのアクション有無の指定は、Wsn.4以降の形式のシナリオでしか行えません。") \
    X(warningLayerV7, "warningLayerV7", "前面セルにはレイヤ値%1$sを指定してください。") \
    X(warningReplaceBgImageV7, "warningReplaceBgImageV7", "CardWirthNext 1.60の背景置換コンテントでは、JPY1のアニメーションは実行されず、エフェクトブースターのセルは無視されます。") \
    X(warningLoseBgImageV7, "warningLoseBgImageV7", "CardWirthNext 1.60の背景置換コンテントでは、JPY1のアニメーションは実行され、エフェクトブースターのセルは無視されます。") \
    X(warningMoveBgImageV7, "warningMoveBgImageV7", "CardWirthNext 1.60の背景置換コンテントでは、JPY1のアニメーションは実行され、エフェクトブースターのセルは無視されます。") \
    X(warningMoveCardV7, "warningMoveCardV7", "CardWirthNext 1.60のカード移動コンテントでは、拡大率変更・レイヤ変更・アニメーション有無以外のアニメーション速度指定は行えません。") \
    X(warningChangeEnvironmentV7, "warningChangeEnvironmentV7", "CardWirthNext 1.60の禁止コンテントでは、禁止状態を未指定にする事はできません。") \
    X(warningIgnoreKeyCodeV7, "warningIgnoreKeyCodeV7", "CardWirthNext 1.60では、効果コンテントにキーコードは指定できません。") \
    X(warningIgniteDeadEvent, "warningIgniteDeadEvent", "CardWirthNext 1.60では、効果コンテントで死亡イベントが発火します。") \
    X(warningIgniteNoDeadEvent, "warningIgniteNoDeadEvent", "効果コンテントに死亡イベント発火要因となる効果が含まれていません。") \
    X(warningCheckFlagV7, "warningCheckFlagV7", "CardWirthNext 1.60では、フラグ値の変更の直後にフラグ判定を行うと、値の変化が判定結果に反映されません。") \
    X(warningCheckStepV7, "warningCheckStepV7", "CardWirthNext 1.60では、ステップ値の変更の直後にステップ判定を行うと、値の変化が判定結果に反映されません。") \
    X(unknownStepValue, "unknownStepValue", "存在しないステップ値(%1$s)") \
    X(card, "card", "カード") \
    X(apt, "apt", "要素") \
    X(useCountAndDesc, "useCountAndDesc", "使用回数/解説") \
    X(levelAndDesc, "levelAndDesc", "レベル/解説") \
    X(useBonus, "useBonus", "使用ボーナス") \
    X(haveBonus, "haveBonus", "所持ボーナス") \
    X(motion, "motion", "効果") \
    X(cardProps, "cardProps", "属性") \
    X(settings, "settings", "設定") \
    X(seAndKeyCode, "seAndKeyCode", "効果音/キーコード") \
    X(eventIgnite, "eventIgnite", "イベント発火") \
    X(rangeHint, "rangeHint", "(%1$s～%2$s)") \
    X(source, "source", "出典") \
    X(sourceScenario, "sourceScenario", "シナリオ名") \
    X(sourceAuthor, "sourceAuthor", "シナリオ作者") \
    X(resetSource, "resetSource", "現在のシナリオを出典に設定") \
    X(diffSource, "diffSource", "出典のシナリオ名と作者名が現在のシナリオと異なるため、使用時イベントのカード入手、エリア移動、パッケージのコール等は実行されません。") \
    X(exportedImageName, "exportedImageName", "%1$s") \
    X(exportedImageNameWithAuthor, "exportedImageNameWithAuthor", "%1$s(%2$s)") \
    X(exportedImageNameWithCard, "exportedImageNameWithCard", "%1$s_%2$s") \
    X(dirTabName, "dirTabName", "ファイル") \
    X(dirStatus, "dirStatus", "%1$s個のファイル (%2$s)") \
    X(dirStatusSel, "dirStatusSel", "%1$s個のファイル (%2$s) (%3$s個を選択中)") \
    X(dirName, "dirName", detail::concat(DIR, "名")) \
    X(fileName, "fileName", "ファイル名") \
    X(fileExt, "fileExt", "拡張子") \
    X(fileCount, "fileCount", "利用数") \
    X(errorExec, "errorExec", "%1$sの起動に失敗しました。") \
    X(filterDescZip, "filterDescZip", "ZIP アーカイブ (*.zip)") \
    X(filterDescCab, "filterDescCab", "CAB アーカイブ (*.cab)") \
    X(filterDescWsn, "filterDescWsn", "シナリオファイル (*.wsn)") \
    X(dlgTitCreateArchive, "dlgTitCreateArchive", "シナリオの圧縮") \
    X(failedCreateArchive, "failedCreateArchive", "シナリオの圧縮に失敗") \
    X(dlgMsgIsSaveBeforeCreateArchive, "dlgMsgIsSaveBeforeCreateArchive", "「%1$s」は変更されています。保存しますか？") \
    X(warningFileExtension, "warningFileExtension", "このファイルの本来の拡張子は「*%1$s」です。") \
    X(baseSettings, "baseSettings", "基本設定") \
    X(reference, "reference", "...") \
    X(enginePath, "enginePath", "CardWirthPy_Cの実行ファイルの場所") \
    X(filterEnginePath, "filterEnginePath", "CardWirthPy (%1$s)") \
    X(findEnginePath, "findEnginePath", "シナリオの場所から自動的に探す") \
    X(dlgTitEnginePath, "dlgTitEnginePath", "%1$sの場所") \
    X(tempDir, "tempDir", "シナリオの一時展開先") \
    X(tempDirDesc, "tempDirDesc", "wsn圧縮されたシナリオの一時的な展開先を選択してください。") \
    X(backupDir, "backupDir", "自動バックアップ") \
    X(backupEnabled, "backupEnabled", "自動バックアップを行う") \
    X(autoSave, "autoSave", "バックアップ時に上書き保存する") \
    X(backupArchived, "backupArchived", "圧縮してバックアップする") \
    X(backupRefAuthor, "backupRefAuthor", "作者が一致したシナリオのみバックアップする") \
    X(backupPath, "backupPath", "保存先") \
    X(backupDirDesc, "backupDirDesc", detail::concat("シナリオを定期的に自動バックアップする", DIR, "を選択してください。")) \
    X(backupIntervalTime, "backupIntervalTime", "時間間隔で行う") \
    X(backupIntervalEdit, "backupIntervalEdit", "編集回数で行う") \
    X(minute, "minute", "分") \
    X(count, "count", "回") \
    X(backupCount, "backupCount", "最大保存数") \
    X(backupBeforeSaveDir, "backupBeforeSaveDir", "保存時バックアップ") \
    X(backupBeforeSave, "backupBeforeSave", "保存時バックアップ") \
    X(backupBeforeSaveDirDesc, "backupDirDesc", detail::concat("シナリオファイルのバックアップコピーを作成する", DIR, "を選択してください。")) \
    X(backupBeforeSaveEnabled, "backupBeforeSaveEnabled", "保存時にシナリオファイルのバックアップコピーを作成する") \
    X(backupBeforeSavePath, "backupBeforeSavePath", "保存先") \
    X(skin, "skin", "スキン") \
    X(scenarioAuthor, "scenarioAuthor", "シナリオ作者(新規作成時に自動設定されます)") \
    X(startAreaName, "startAreaName", "新規作成時の開始エリア名(空欄 = 開始エリア無し)") \
    X(historiesSettings, "historiesSettings", "履歴") \
    X(openHistoryMax, "openHistoryMax", "シナリオ履歴保存件数") \
    X(openHistoryClear, "openHistoryClear", "クリア") \
    X(dlgMsgHistoryClear, "dlgMsgHistoryClear", "シナリオ履歴を削除してよろしいですか？") \
    X(searchHistoryMax, "searchHistoryMax", "検索/置換履歴保存件数") \
    X(searchHistoryClear, "searchHistoryClear", "クリア") \
    X(dlgMsgSearchHistoryClear, "dlgMsgSearchHistoryClear", "検索/置換履歴を削除してよろしいですか？") \
    X(executedPartiesMax, "executedPartiesMax", "パーティ履歴保存件数") \
    X(executedPartiesClear, "executedPartiesClear", "クリア") \
    X(dlgMsgExecutedPartiesClear, "dlgMsgExecutedPartiesClear", "パーティ履歴を削除してよろしいですか？") \
    X(ignorePaths, "ignorePaths", "無視ファイル(改行区切り)") \
    X(etcSettings, "etcSettings", "その他") \
    X(etcSettingsTitle, "etcSettingsTitle", "詳細") \
    X(previewScale, "previewScale", "拡大率") \
    X(imageScale, "imageScale", "表示") \
    X(drawingScale, "drawingScale", "描画") \
    X(uiTextScale, "uiTextScale", "UI文字") \
    X(uiTextScaleValue, "uiTextScaleValue", "%s%%") \
    X(imageScaleValue, "imageScaleValue", "%s倍") \
    X(languageSetting, "languageSetting", "言語") \
    X(languageSystem, "languageSystem", "システムの言語") \
    X(languageCaution, "languageCaution", "※ 次回起動時から適用されます") \
    X(etcSettingsCommon, "etcSettingsCommon", "全般") \
    X(showImagePreview, "showImagePreview", "カードや背景のプレビュー表示を行う") \
    X(maskCardImagePreview, "maskCardImagePreview", "カードサイズの画像のプレビュー表示で背景を透明化する") \
    X(switchTabWheel, "switchTabWheel", "マウスホイールでタブ切替を行う") \
    X(closeTabWithMiddleClick, "closeTabWithMiddleClick", "中ボタンクリックでタブを閉じる") \
    X(openTabAtRightOfCurrentTab, "openTabAtRightOfCurrentTab", "新しいタブを現在のタブの直後に開く") \
    X(showCloseButtonAllTab, "showCloseButtonAllTab", "全てのタブに閉じるボタンを表示する") \
    X(comboListVisible, "comboListVisible", "コンボボックスでの編集開始時にリストを開く") \
    X(showCurrentValueOnTopAlways, "showCurrentValueOnTopAlways", "コンボボックスで現在の値を常に最上段に表示する") \
    X(editTriggerTypeIsQuick, "editTriggerTypeIsQuick", "選択項目のクリックですぐにテキストの編集を開始する") \
    X(xmlCopy, "xmlCopy", "コピーや切り取りを常にXML形式で行う") \
    X(logicalSort, "logicalSort", "数値参照型ソートを行う(1, 10, 2, 3, ... → 1, 2, 3, 10, ...)") \
    X(canVanishWorkAreaInMainWindow, "canVanishWorkAreaInMainWindow", "サブウィンドウに編集エリアがあればメインウィンドウの編集エリアを閉じる") \
    X(useCoolBar, "useCoolBar", "配置変更可能なツールバーを使用する") \
    X(etcSettingsLoad, "etcSettingsLoad", "読込と保存") \
    X(saveSkinName, "saveSkinName", "WSN(XML)形式のシナリオでスキンタイプに加えてスキン名称も保存する") \
    X(saveNeedChanged, "saveNeedChanged", "変更があった時だけ上書き保存を有効にする") \
    X(applyDialogsBeforeSave, "applyDialogsBeforeSave", "保存前にダイアログの編集内容を適用する") \
    X(doubleIO, "doubleIO", "分割読込・保存を行う(デュアルコア以上の環境で高速化)") \
    X(cautionToScenarioLoadErrors, "cautionToScenarioLoadErrors", "シナリオの一部ファイルの読み込みに失敗した場合に警告する") \
    X(archiveInNewThread, "archiveInNewThread", "保存時の圧縮を別スレッドで行う(圧縮シナリオの保存の高速化)") \
    X(saveChangedOnlyWsn, "saveChangedOnlyWsn", "WSN形式のシナリオの上書き時に更新されたファイルだけを保存する") \
    X(saveChangedOnlyClassic, "saveChangedOnlyClassic", "クラシックなシナリオの上書き時に更新されたファイルだけを保存する") \
    X(expandXMLs, "expandXMLs", "圧縮されたシナリオの読込み時にXMLファイルを展開する") \
    X(xmlFileNameIsIDOnly, "xmlFileNameIsIDOnly", "XMLファイルの名前にエリア名などを含めずIDのみで設定する") \
    X(replaceClassicResourceExtension, "replaceClassicResourceExtension", "クラシックなシナリオの保存時にスキン付属リソースの拡張子を変換する") \
    X(saveInnerImagePath, "saveInnerImagePath", "クラシックなシナリオで格納イメージのファイルパスを保存する") \
    X(addNewClassicEngine, "addNewClassicEngine", "未知のクラシックエンジンを見つけたら記憶する") \
    X(openLastScenario, "openLastScenario", "終了時に開いていたシナリオを次の起動時に開く") \
    X(reconstruction, "reconstruction", "シナリオごとにタブの配置を記憶する") \
    X(etcSettingsFile, "etcSettingsFile", "ファイルの追跡") \
    X(traceDirectories, "traceDirectories", detail::concat("ファイル・", DIR, "の変更を自動的に追跡する")) \
    X(autoUpdateJpy1File, "autoUpdateJpy1File", "ファイル名が変更された時に関係するJPY1・JPDCファイルを自動的に更新する") \
    X(etcSettingsFind, "etcSettingsFind", "検索と置換") \
    X(startIncrementalSearchWhenKeyDown, "startIncrementalSearchWhenKeyDown", "何かキーを押した時に絞り込み検索を開始する") \
    X(cautionBeforeReplace, "cautionBeforeReplace", "全て置換する前に確認ダイアログを表示する") \
    X(etcSettingsTable, "etcSettingsTable", "テーブルビューの設定") \
    X(showAreaDirTree, "showAreaDirTree", "テーブルビューを階層表示する") \
    X(showSummaryInAreaTable, "showSummaryInAreaTable", "テーブルビューにシナリオの概要を表示する") \
    X(clickIsOpenEvent, "clickIsOpenEvent", "左クリックでイベントビューを開く") \
    X(incrementNewAreaName, "incrementNewAreaName", "エリア等の作成時に既存のアイテムと重複しない名前を生成する") \
    X(etcSettingsScene, "etcSettingsScene", "シーンビューの設定") \
    X(switchFixedWithCheckBox, "switchFixedWithCheckBox", "チェックボックスで個別のカードと背景セルの固定状態を切り替える") \
    X(resizingImageWithRatio, "resizingImageWithRatio", "複数のカードと背景セルのサイズ変更時に相対的な位置とサイズを維持する") \
    X(showSceneViewSelectionFilter, "showSceneViewSelectionFilter", "選択されたカードの上に半透明のカーテンをかける") \
    X(drawXORSelectionLine, "drawXORSelectionLine", "選択枠を色の反転で描画する") \
    X(smoothingCard, "smoothingCard", "カードのサイズ変更時にスムージングを行う") \
    X(ignoreBackgroundInRange, "ignoreBackgroundInRange", "範囲選択でフルサイズの背景セルを無視する") \
    X(copyDesc, "copyDesc", "カードをエリアに貼り付け・ドロップした時、解説もコピーする") \
    X(showItemNumberOfSceneAndEventView, "showItemNumberOfSceneAndEventView", "メニュー・エネミーカード及び背景セルの番号を表示する") \
    X(showCardStatusUnderPointer, "showCardStatusUnderPointer", "マウスカーソル下のメニュー・エネミーカードの情報をステータスバーに表示する") \
    X(etcSettingsEvent, "etcSettingsEvent", "イベントビューの設定") \
    X(showContentsGroupName, "showContentsGroupName", "コンテンツボックスにグループ名を表示する") \
    X(showEventContentDescription, "showEventContentDescription", "イベントコンテントの解説をツールチップで表示する") \
    X(contentsFloat, "contentsFloat", "コンテンツボックスを別ウィンドウで表示する") \
    X(contentsAutoHide, "contentsAutoHide", "コンテンツボックスを自動的に隠す") \
    X(straightEventTreeView, "straightEventTreeView", "イベントツリーを垂直に表示する") \
    X(forceIndentBranchContent, "forceIndentBranchContent", "分岐コンテントの後続コンテントは必ず右へ移動する") \
    X(showTerminalMark, "showTerminalMark", "イベントツリーの終端を明示する") \
    X(showTargetStartLineNumber, "showTargetStartLineNumber", "スタートへのリンク・コールの関係を行番号で表示する") \
    X(classicStyleTree, "classicStyleTree", "イベントツリーの開閉ボタンを省略する") \
    X(clickIconIsStartEdit, "clickIconIsStartEdit", "イベントコンテントのアイコンのクリックでダイアログを開く") \
    X(adjustContentName, "adjustContentName", "イベントコンテントの移動時にテキストを再設定する") \
    X(showVariableValuesInEventText, "showVariableValuesInEventText", "選択肢のテキスト内の変数をプレビュー表示する(#M -> [選択中]...)") \
    X(editSelectionWithCombo, "editSelectionWithCombo", "選択肢を編集する時、「標準の選択肢」をコンボボックスで選択可能にする") \
    X(refCardsAtEditBgImage, "refCardsAtEditBgImage", "背景変更コンテントの編集を開始する際、最初からカード配置の参照を行う") \
    X(floatMessagePreview, "floatMessagePreview", "セリフ・メッセージのプレビューをフロートさせる") \
    X(useMessageWindowColorInTextContentDialog, "useMessageWindowColorInTextContentDialog", "セリフ・メッセージの編集欄でメッセージウィンドウの色を使用する") \
    X(selectVariableWithTree, "selectVariableWithTree", "状態変数選択ビューでディレクトリの階層表示を行う") \
    X(useNamesAfterStandard, "useNamesAfterStandard", "シナリオで使用中の称号・キーコードを標準の称号・キーコードの後に配置する") \
    X(expandChooserItems, "expandChooserItems", "エリアや状態変数等を選択するビューでは全て開いた状態を初期状態とする") \
    X(restorePositionOfEventTreeView, "restorePositionOfEventTreeView", "イベントごとにイベントツリービューの表示位置を記憶する") \
    X(xmlContentStr, "xmlContentStr", "XML 文字列") \
    X(xmlTypeLabel, "xmlTypeLabel", "XML タイプ") \
    X(xmlTypeChangeBgImage, "xmlTypeChangeBgImage", "外観: 背景変更") \
    X(xmlTypeMoveBgImage, "xmlTypeMoveBgImage", "外観: 背景再配置") \
    X(xmlTypeLoseBgImage, "xmlTypeLoseBgImage", "外観: 背景削除") \
    X(xmlTypeReplaceBgImage, "xmlTypeReplaceBgImage", "外観: 背景置換") \
    X(xmlTypeMoveCard, "xmlTypeMoveCard", "外観: カード再配置") \
    X(xmlTypeMultipleMoveLose, "xmlTypeMultipleMoveLose", "外観: 複数背景再配置/削除") \
    X(xmlTypeEffect, "xmlTypeEffect", "基本: 効果") \
    X(xmlTypeTalkMessage, "xmlTypeTalkMessage", "基本: メッセージ") \
    X(xmlTypeTalkDialog, "xmlTypeTalkDialog", "基本: セリフ") \
    X(xmlTypeInputDialog, "xmlTypeInputDialog", "特殊: 入力ダイアログ") \
    X(xmlTypeChangeEnemy, "xmlTypeChangeEnemy", "特殊: 敵カード変更") \
    X(xmlTypePlayBgm, "xmlTypePlayBgm", "基本: BGM再生") \
    X(xmlTypePlaySound, "xmlTypePlaySound", "基本: 効果音") \
    X(xmlTypeWait, "xmlTypeWait", "基本: 空白時間挿入") \
    X(xmlTypeSetFlag, "xmlTypeSetFlag", "変数: フラグ設定") \
    X(xmlTypeReverseFlag, "xmlTypeReverseFlag", "変数: フラグ反転") \
    X(xmlTypeSubstituteFlag, "xmlTypeSubstituteFlag", "変数: フラグ代入") \
    X(xmlTypeBranchFlag, "xmlTypeBranchFlag", "変数: フラグ分岐") \
    X(xmlTypeBranchFlagValue, "xmlTypeBranchFlagValue", "変数: フラグ比較分岐") \
    X(xmlTypeCheckFlag, "xmlTypeCheckFlag", "変数: フラグ判定") \
    X(xmlTypeSetStep, "xmlTypeSetStep", "変数: ステップ変更") \
    X(xmlTypeSetStepUp, "xmlTypeSetStepUp", "変数: ステップ増加") \
    X(xmlTypeSetStepDown, "xmlTypeSetStepDown", "変数: ステップ減少") \
    X(xmlTypeSubstituteStep, "xmlTypeSubstituteStep", "変数: ステップ代入") \
    X(xmlTypeBranchStep, "xmlTypeBranchStep", "変数: ステップ上下分岐") \
    X(xmlTypeBranchStepValue, "xmlTypeBranchStepValue", "変数: ステップ比較分岐") \
    X(xmlTypeCheckStep, "xmlTypeCheckStep", "変数: ステップ判定") \
    X(xmlTypeSetVariantStr, "xmlTypeSetVariantStr", "演算: コモン文字列設定") \
    X(xmlTypeCheckVariant, "xmlTypeCheckVariant", "演算: コモン判定") \
    X(xmlTypeBranchSelect, "xmlTypeBranchSelect", "状況: メンバ選択分岐") \
    X(xmlTypeBranchAbility, "xmlTypeBranchAbility", "状況: 能力分岐") \
    X(xmlTypeBranchRandom, "xmlTypeBranchRandom", "状況: ランダム分岐") \
    X(xmlTypeBranchLevel, "xmlTypeBranchLevel", "状況: レベル分岐") \
    X(xmlTypeBranchStatus, "xmlTypeBranchStatus", "状況: ステータス分岐") \
    X(xmlTypeBranchPartyNumber, "xmlTypeBranchPartyNumber", "状況: パーティ人数分岐") \
    X(xmlTypeBranchRound, "xmlTypeBranchRound", "状況: ラウンド分岐") \
    X(xmlTypeBranchCast, "xmlTypeBranchCast", "保有: キャスト存在分岐") \
    X(xmlTypeBranchItem, "xmlTypeBranchItem", "保有: アイテム所持分岐") \
    X(xmlTypeBranchSkill, "xmlTypeBranchSkill", "保有: スキル所持分岐") \
    X(xmlTypeBranchBeast, "xmlTypeBranchBeast", "保有: 召喚獣存在分岐") \
    X(xmlTypeBranchInfo, "xmlTypeBranchInfo", "保有: 情報所持分岐") \
    X(xmlTypeBranchMoney, "xmlTypeBranchMoney", "保有: 所持金分岐") \
    X(xmlTypeBranchCoupon, "xmlTypeBranchCoupon", "保有: クーポン分岐") \
    X(xmlTypeBranchMultiCoupon, "xmlTypeBranchMultiCoupon", "保有: クーポン多岐分岐") \
    X(xmlTypeBranchCompleteStamp, "xmlTypeBranchCompleteStamp", "保有: 終了シナリオ分岐") \
    X(xmlTypeBranchGossip, "xmlTypeBranchGossip", "保有: ゴシップ分岐") \
    X(xmlTypeBranchKeyCode, "xmlTypeBranchKeyCode", "保有: キーコード所持分岐") \
    X(xmlTypeGetCast, "xmlTypeGetCast", "取得: キャスト加入") \
    X(xmlTypeGetItem, "xmlTypeGetItem", "取得: アイテム入手") \
    X(xmlTypeGetSkill, "xmlTypeGetSkill", "取得: スキル取得") \
    X(xmlTypeGetBeast, "xmlTypeGetBeast", "取得: 召喚獣獲得") \
    X(xmlTypeGetInfo, "xmlTypeGetInfo", "取得: 情報入手") \
    X(xmlTypeGetMoney, "xmlTypeGetMoney", "取得: 所持金増加") \
    X(xmlTypeGetCoupon, "xmlTypeGetCoupon", "取得: クーポン取得") \
    X(xmlTypeGetCompleteStamp, "xmlTypeGetCompleteStamp", "取得: 終了シナリオ設定") \
    X(xmlTypeGetGossip, "xmlTypeGetGossip", "取得: ゴシップ追加") \
    X(xmlTypeLoseCast, "xmlTypeLoseCast", "喪失: キャスト離脱") \
    X(xmlTypeLoseItem, "xmlTypeLoseItem", "喪失: アイテム喪失") \
    X(xmlTypeLoseSkill, "xmlTypeLoseSkill", "喪失: スキル喪失") \
    X(xmlTypeLoseBeast, "xmlTypeLoseBeast", "喪失: 召喚獣消去") \
    X(xmlTypeLoseInfo, "xmlTypeLoseInfo", "喪失: 情報消失") \
    X(xmlTypeLoseMoney, "xmlTypeLoseMoney", "喪失: 所持金減少") \
    X(xmlTypeLoseCoupon, "xmlTypeLoseCoupon", "喪失: クーポン削除") \
    X(xmlTypeLoseCompleteStamp, "xmlTypeLoseCompleteStamp", "喪失: 終了シナリオ削除") \
    X(xmlTypeLoseGossip, "xmlTypeLoseGossip", "喪失: ゴシップ削除") \
    X(xmlTypeSetActionEvent, "xmlTypeSetActionEvent", "特殊: アクション設定") \
    X(xmlTypeEndAction, "xmlTypeEndAction", "特殊: アクション終了") \
    X(variableLabel, "variableLabel", "変数") \
    X(xmlAssignOn, "xmlAssignOn", "On") \
    X(xmlAssignOff, "xmlAssignOff", "Off") \
    X(xmlAssignLabel, "xmlAssignLabel", "文字列中の変数に代入する") \
    X(xmlUseVarLabel, "xmlUseVarLabel", "文字列の代わりに変数を使う") \
    X(xmlUseVarOn, "xmlUseVarOn", "On") \
    X(xmlUseVarOff, "xmlUseVarOff", "Off") \
    X(initializeLabel, "initializeLabel", "初期化") \
    X(openReferenceLabel, "openReferenceLabel", "参考を開く") \
    X(referenceCopyAllLabel, "referenceCopyAllLabel", "全体コピー") \
    X(confirmInitialize, "confirmInitialize", "XMLテンプレートで初期化してもよろしいですか?") \
    X(xmlTypeNotSelected, "xmlTypeNotSelected", "XMLタイプを選択してください") \
    X(etcSettingsCard, "etcSettingsCard", "カードビューの設定") \
    X(showHandMark, "showHandMark", "キャストカードの詳細情報表示時に所有カードの有無を表示する") \
    X(showEventTreeMark, "showEventTreeMark", "効果系カードの詳細情報表示時に使用時イベントの有無を表示する") \
    X(ignoreEmptyStart, "ignoreEmptyStart", "空のイベントツリーしか持たない使用時イベントは無視する") \
    X(showCardListHeader, "showCardListHeader", "カードの画像表示時にヘッダを表示する") \
    X(showCardListTitle, "showCardListTitle", "カードの画像表示時にIDと名前を表示する") \
    X(showSkillCardLevel, "showSkillCardLevel", "カードの画像表示時に特殊技能カードのレベルを表示する") \
    X(showMotionDescription, "showMotionDescription", "効果の解説をツールチップで表示する") \
    X(showSpNature, "showSpNature", "特殊型を表示する") \
    X(radarStyleParams, "radarStyleParams", "レーダー型コントロールでパラメータ値を設定する") \
    X(showAptitudeOnCastCardEditor, "showAptitudeOnCastCardEditor", "キャストカードの身体能力と精神傾向の編集時にで各種適性を表示する") \
    X(linkCard, "linkCard", "クラシックなシナリオでキャストの所有カードや召喚対象カードを参照で設定する") \
    X(soundPlayType, "soundPlayType", "BGM再生方式") \
    X(soundPlayTypeDef, "soundPlayTypeDef", "自動選択") \
    X(soundPlayTypeSDL, "soundPlayTypeSDL", "SDL_mixer(旧方式)") \
    X(soundPlayTypeMCI, "soundPlayTypeMCI", "WinMM(CardWirth方式)") \
    X(soundPlayTypeApp, "soundPlayTypeApp", "関連付けされたアプリケーションで開く") \
    X(soundEffectPlayType, "soundEffectPlayType", "効果音再生方式") \
    X(soundPlaySameBGM, "soundPlaySameBGM", "BGMに合わせる") \
    X(soundVolume, "soundVolume", "音量") \
    X(soundVolumePer, "soundVolumePer", "%") \
    X(soundCaution, "soundCaution", "※ WinMM方式の時、音量は反映されません") \
    X(flagInitValue, "flagInitValue", "フラグの初期値") \
    X(stepInitValue, "stepInitValue", "ステップの初期値") \
    X(stepValueName, "stepValueName", "ステップ値($N = 数値)") \
    X(keyBind, "keyBind", "キーバインド") \
    X(mnemonic, "mnemonic", "アクセスキー") \
    X(hotkey, "hotkey", "ショートカット") \
    X(wallpaper, "wallpaper", "エディタの壁紙") \
    X(filterImage, "filterImage", "画像ファイル (*.bmp;*.jpg;*.jpeg;*.png;*.apng;*.webp;*.tif;*.tiff;*.ico;*.icon)") \
    X(dlgTitWallpaper, "dlgTitWallpaper", "壁紙画像の選択") \
    X(wallpaperStyle, "wallpaperStyle", "表示形式") \
    X(wallpaperStyleNameCenter, "wallpaperStyleNameCenter", "中央に表示") \
    X(wallpaperStyleNameTile, "wallpaperStyleNameTile", "並べて表示") \
    X(wallpaperStyleNameExpandFull, "wallpaperStyleNameExpandFull", "拡大して表示") \
    X(wallpaperStyleNameExpand, "wallpaperStyleNameExpand", "はみ出さないように拡大") \
    X(bgImageAndSelections, "bgImageAndSelections", "背景と選択肢") \
    X(standardSelections, "standardSelections", "標準の選択肢") \
    X(standardKeyCode, "standardKeyCode", "標準のキーコード") \
    X(keyCodesByFeatures, "keyCodesByFeatures", "特徴とキーコードの対応") \
    X(cardFeature, "cardFeature", "カードの特徴") \
    X(keyCodesByMotions, "keyCodesByMotions", "効果とキーコードの対応") \
    X(noElement, "noElement", "指定無し") \
    X(elementOverrides, "elementOverrides", "効果属性の上書き") \
    X(elementOverrideName, "elementOverrideName", "属性名") \
    X(elementOverrideTargetType, "elementOverrideTargetType", "対属性") \
    X(elementAllTargetType, "elementAllTargetType", "(対属性無し)") \
    X(elementWeaknessAndResistTargetType, "elementWeaknessAndResistTargetType", "(弱点または無効)") \
    X(selectElementIcon, "selectElementIcon", "効果属性のアイコンを選択") \
    X(errorEnginePath, "errorEnginePath", "%1$sの場所が正しくありません。") \
    X(errorTempPath, "errorTempPath", "一時展開先が正しくありません。") \
    X(errorBackupPath, "errorBackupPath", "自動バックアップ先が正しくありません。") \
    X(errorBackupBeforeSavePath, "errorBackupBeforeSavePath", "保存時バックアップ先が正しくありません。") \
    X(sNew, "sNew", "新規作成") \
    X(sAlt, "sAlt", "上書き") \
    X(sDel, "sDel", "削除") \
    X(dlgMsgForceApply, "dlgMsgForceApply", "次の設定が変更されたまま適用されていません。適用して設定を更新しますか？\n%1$s") \
    X(dlgMsgForceApplySingle, "dlgMsgForceApplySingle", "%1$sが変更されたまま適用されていません。適用して設定を更新しますか？") \
    X(dlgMsgForceApplySelection, "dlgMsgForceApplySelection", "「%1$s」が変更されたまま適用されていません。適用して他の項目を選択しますか？") \
    X(noNameData, "noNameData", "(名前無しの項目)") \
    X(outerToolsAndClassicEngines, "outerToolsAndClassicEngines", "外部ツールとクラシックエンジン") \
    X(outerToolsTitle, "outerToolsTitle", "外部ツールの設定") \
    X(outerToolName, "outerToolName", "外部ツール名") \
    X(outerToolCommand, "outerToolCommand", "コマンド") \
    X(dlgTitOuterTool, "dlgTitOuterTool", "外部ツールの選択") \
    X(toolsHint1, "toolsHint1", "$F = ファイル名") \
    X(toolsHint3, "toolsHint3", "$$ = $") \
    X(outerToolWorkDir, "outerToolWorkDir", detail::concat("作業", DIR)) \
    X(toolWorkDir, "toolWorkDir", detail::concat("作業", DIR, "の選択")) \
    X(toolWorkDirDesc, "toolWorkDirDesc", detail::concat("外部ツールの作業", DIR, "を選択してください。")) \
    X(toolsHint2, "toolsHint2", detail::concat("$S = シナリオの", DIR)) \
    X(templates, "templates", "テンプレート") \
    X(eventTemplatesTitle, "eventTemplatesTitle", "イベントテンプレートの設定") \
    X(eventTemplateName, "eventTemplateName", "テンプレート名") \
    X(eventTemplateScript, "eventTemplateScript", "スクリプト") \
    X(scenarioTemplatesTitle, "scenarioTemplatesTitle", "シナリオテンプレートの設定") \
    X(scenarioTemplateName, "scenarioTemplateName", "テンプレート名") \
    X(scenarioTemplatePath, "scenarioTemplatePath", "シナリオの場所") \
    X(dlgTitScTemplate, "dlgTitScTemplate", "テンプレートシナリオの選択") \
    X(exeFileDescExe, "exeFileDescExe", "実行ファイル (*.exe)") \
    X(exeFileDescAll, "exeFileDescAll", "全てのファイル (*.*)") \
    X(classicEnginesTitle, "classicEnginesTitle", "クラシックエンジンの設定") \
    X(classicEngineName, "classicEngineName", "エンジン名") \
    X(classicEnginePath, "classicEnginePath", "実行ファイルパス") \
    X(classicEngineDataDirName, "classicEngineDataDirName", "データフォルダ") \
    X(classicEngineDataDirNameDesc, "classicEngineDataDirNameDesc", "クラシックエンジンのデータフォルダを選択してください。") \
    X(classicEngineExecute, "classicEngineExecute", "代替実行ファイル") \
    X(classicEngineHint1, "classicEngineHint1", "※ 代替実行ファイルを指定すると、エンジン本体の代わりに実行されます") \
    X(dlgTitClassicEnginePath, "dlgTitClassicEnginePath", "クラシックエンジンの選択") \
    X(dlgTitClassicEngineExecute, "dlgTitClassicEngineExecute", "代替実行ファイルの選択") \
    X(featureName, "featureName", "システム文字列") \
    X(dlgTitFeatureName, "dlgTitFeatureName", "システム文字列") \
    X(featureDefaultName, "featureDefaultName", "標準") \
    X(featureVariantName, "featureVariantName", "バリアント") \
    X(featureManualName, "featureManualName", "ユーザ設定") \
    X(selectSkinType, "selectSkinType", "設定対象のスキンタイプ") \
    X(defaultSettings, "defaultSettings", "デフォルト") \
    X(editSkinTypeName, "editSkinTypeName", "スキンタイプ名の編集") \
    X(editSkinTypeNameDesc, "editSkinTypeNameDesc", "スキンタイプの名前") \
    X(addSkinType, "addSkinType", "スキンタイプの追加") \
    X(addSkinTypeDesc, "addSkinTypeDesc", "追加するスキンタイプの名前") \
    X(delSkinType, "delSkinType", "スキンタイプ別設定の削除") \
    X(delSkinTypeDesc, "delSkinTypeDesc", "スキンタイプ「%1$s」の設定を削除してよろしいですか？") \
    X(cloneSkinType, "cloneSkinType", "スキンタイプ別設定の複製") \
    X(cloneSkinTypeDesc, "cloneSkinTypeDesc", "複製先のスキンタイプの名前") \
    X(overrideDefaultSettings, "overrideDefaultSettings", "スキンタイプ別設定によるデフォルト設定の上書き") \
    X(overrideBgImagesSettings, "overrideBgImagesSettings", "背景設定を上書きする") \
    X(overrideSelectionsSettings, "overrideSelectionsSettings", "選択肢設定を上書きする") \
    X(overrideKeyCodesSettings, "overrideKeyCodesSettings", "キーコード設定を上書きする") \
    X(bgImagesDefault, "bgImagesDefault", "デフォルト背景") \
    X(setBgImagesDefault, "setBgImagesDefault", "デフォルト背景の設定...") \
    X(dlgTitBgImagesDefault, "dlgTitBgImagesDefault", "デフォルト背景の設定") \
    X(systemSounds, "systemSounds", "システム音声") \
    X(soundSaved, "soundSaved", "保存完了") \
    X(playableSounds, "playableSounds", "サウンドファイル (%1$s)") \
    X(dlgTitSystemSound, "dlgTitSystemSound", "システム音声の選択") \
    X(undoMax, "undoMax", "「元に戻す」回数") \
    X(undoMaxMainView, "undoMaxMainView", "エリア/カード/フラグ") \
    X(undoMaxEvent, "undoMaxEvent", "メニュー/エネミー/背景/イベント") \
    X(undoMaxReplace, "undoMaxReplace", "置換") \
    X(undoMaxEtc, "undoMaxEtc", "テキスト/その他") \
    X(dialogStatus, "dialogStatus", "セリフコンテントのステータス") \
    X(dialogStatusNameTop, "dialogStatusNameTop", "最上位のセリフ") \
    X(dialogStatusNameUnder, "dialogStatusNameUnder", "最下位のセリフ") \
    X(dialogStatusNameUnderWithCoupon, "dialogStatusNameUnderWithCoupon", "最下位のセリフ(条件クーポン設定あり)") \
    X(dlgTitEvTemplates, "dlgTitEvTemplates", "イベントテンプレート - %1$s") \
    X(eventTemplateHint1, "eventTemplateHint1", "下記のようにスクリプトの冒頭に値の無い変数を置くと、テンプレートからの配置時に値を設定できます。") \
    X(eventTemplateHint2, "eventTemplateHint2", "$時間\nwait $時間") \
    X(dlgTitCustomizeToolBar, "dlgTitCustomizeToolBar", "ツールバーの編集") \
    X(toolBarName, "toolBarName", "ツールバー (%1$s)") \
    X(toolGroupName, "toolGroupName", "グループ (%1$s)") \
    X(dlgTitScriptError, "dlgTitScriptError", "CWXスクリプトエラー") \
    X(scriptError, "scriptError", "CWXスクリプトのコンパイル中にエラーが発生しました。") \
    X(scriptErrorOver100Error, "scriptErrorOver100Error", "エラーが100件を超えたため、スクリプトの解析を終了します。") \
    X(scriptErrorInvalidToken, "scriptErrorInvalidToken", "スクリプトに使用できない文字が含まれています。") \
    X(scriptErrorInvalidSyntax, "scriptErrorInvalidSyntax", "構文が正しくありません。") \
    X(scriptErrorInvalidString, "scriptErrorInvalidString", "ここに文字列が必要です。") \
    X(scriptErrorUnCloseString, "scriptErrorUnCloseString", "文字列が閉じられていません。") \
    X(scriptErrorUnOpenComment, "scriptErrorUnOpenComment", "コメントは開始されていません。") \
    X(scriptErrorUnCloseComment, "scriptErrorUnCloseComment", "コメントが閉じられていません。") \
    X(scriptErrorInvalidNumber, "scriptErrorInvalidNumber", "数値が正しくありません。") \
    X(scriptErrorCloseBracketNotFound, "scriptErrorCloseBracketNotFound", "閉じ括弧が見つかりません。") \
    X(scriptErrorCloseParenNotFound, "scriptErrorCloseParenNotFound", "閉じ括弧が見つかりません。") \
    X(scriptErrorZeroDivision, "scriptErrorZeroDivision", "0で除算を行いました。") \
    X(scriptErrorInvalidAttr, "scriptErrorInvalidAttr", "属性が正しくありません。") \
    X(scriptErrorInvalidVar, "scriptErrorInvalidVar", "変数が正しくありません。") \
    X(scriptErrorInvalidVarVal, "scriptErrorInvalidVarVal", "変数の値が正しくありません。") \
    X(scriptErrorNoStartText, "scriptErrorNoStartText", "スタートコンテントの名前がありません。") \
    X(scriptErrorInvalidStatement, "scriptErrorInvalidStatement", "文が正しくありません。") \
    X(scriptErrorInvalidBranch, "scriptErrorInvalidBranch", "分岐の構成が正しくありません。") \
    X(scriptErrorNoIfContents, "scriptErrorNoIfContents", "分岐先のコンテントが見つかりません。") \
    X(scriptErrorInvalidKeyword, "scriptErrorInvalidKeyword", "未知のキーワードです。") \
    X(scriptErrorInvalidKeyword2, "scriptErrorInvalidKeyword2", "キーワードが正しくありません。") \
    X(scriptErrorInvalidValuesOpen, "scriptErrorInvalidValuesOpen", "パラメータ列ではありません。") \
    X(scriptErrorInvalidValuesClose, "scriptErrorInvalidValuesClose", "閉じ括弧が見つかりません。") \
    X(scriptErrorNoVarSet, "scriptErrorNoVarSet", "変数に値をセットしていません。") \
    X(scriptErrorNoVarVal, "scriptErrorNoVarVal", "変数の値がありません。") \
    X(scriptErrorInvalidCalc, "scriptErrorInvalidCalc", "計算式が不正です。") \
    X(scriptErrorInvalidBoolVal, "scriptErrorInvalidBoolVal", "キーワードが正しくありません。") \
    X(scriptErrorInvalidTransition, "scriptErrorInvalidTransition", "未知の画面切替方式です。") \
    X(scriptErrorInvalidRange, "scriptErrorInvalidRange", "未知の範囲です。") \
    X(scriptErrorInvalidStatus, "scriptErrorInvalidStatus", "未知のステータスです。") \
    X(scriptErrorInvalidTarget, "scriptErrorInvalidTarget", "未知のターゲットです。") \
    X(scriptErrorInvalidEffectType, "scriptErrorInvalidEffectType", "未知の効果属性です。") \
    X(scriptErrorInvalidResist, "scriptErrorInvalidResist", "未知の命中属性です。") \
    X(scriptErrorInvalidCardVisual, "scriptErrorInvalidCardVisual", "未知の視覚効果です。") \
    X(scriptErrorInvalidMental, "scriptErrorInvalidMental", "未知の精神要素です。") \
    X(scriptErrorInvalidPhysical, "scriptErrorInvalidPhysical", "未知の肉体要素です。") \
    X(scriptErrorInvalidMotionType, "scriptErrorInvalidMotionType", "未知の効果タイプです。") \
    X(scriptErrorInvalidMotion, "scriptErrorInvalidMotion", "効果が正しくありません。") \
    X(scriptErrorInvalidElement, "scriptErrorInvalidElement", "未知の属性です。") \
    X(scriptErrorInvalidDamageType, "scriptErrorInvalidDamageType", "未知の効果値タイプです。") \
    X(scriptErrorInvalidUpDownSkillPowerType, "scriptErrorInvalidUpDownSkillPowerType", "未知の精神力増減タイプです。") \
    X(scriptErrorInvalidEffectCardType, "scriptErrorInvalidEffectCardType", "未知の効果カードタイプです。") \
    X(scriptErrorInvalidComparison4, "scriptErrorInvalidComparison4", "未知の比較条件です。") \
    X(scriptErrorInvalidComparison3, "scriptErrorInvalidComparison3", "未知の比較条件です。") \
    X(scriptErrorInvalidBgImage, "scriptErrorInvalidBgImage", "背景画像が正しくありません。") \
    X(scriptErrorInvalidColor, "scriptErrorInvalidColor", "色が正しくありません。") \
    X(scriptErrorInvalidBlendMode, "scriptErrorInvalidBlendMode", "未知の合成方式です。") \
    X(scriptErrorInvalidGradientDir, "scriptErrorInvalidGradientDir", "未知のグラデーション方向です。") \
    X(scriptErrorInvalidSelectionMethod, "scriptErrorInvalidSelectionMethod", "未知のメンバ選択方法です。") \
    X(scriptErrorInvalidDialog, "scriptErrorInvalidDialog", "セリフが正しくありません。") \
    X(scriptErrorInvalidTalker, "scriptErrorInvalidTalker", "話者が正しくありません。") \
    X(scriptErrorInvalidCoupon, "scriptErrorInvalidCoupon", "評価条件が正しくありません。") \
    X(scriptErrorUndefinedSymbol, "scriptErrorUndefinedSymbol", "未知のシンボルです。") \
    X(scriptErrorInvalidSif, "scriptErrorInvalidSif", "ここにsifが現れる事はできません。") \
    X(scriptErrorInvalidCommand, "scriptErrorInvalidCommand", "命令が正しくありません。") \
    X(scriptErrorCanNotHaveContent, "scriptErrorCanNotHaveContent", "%1$sコンテントが後続コンテントを持つ事はできません。") \
    X(scriptErrorInvalidStr, "scriptErrorInvalidStr", "文字列が正しくありません。") \
    X(scriptErrorReqNumber, "scriptErrorReqNumber", "ここに数値が必要です。") \
    X(scriptErrorReqID, "scriptErrorReqID", "ここにIDが必要です。") \
    X(scriptErrorUndefinedVar, "scriptErrorUndefinedVar", "存在しない変数です。") \
    X(scriptErrorInvalidValue, "scriptErrorInvalidValue", "値が正しくありません。") \
    X(scriptErrorInvalidCoordinateType, "scriptErrorInvalidCoordinateType", "未知の位置・サイズ形式です。") \
    X(scriptErrorInvalidArray, "scriptErrorInvalidArray", "ここに配列が必要です。") \
    X(scriptErrorSystem, "scriptErrorSystem", "サイズが大きすぎるため、CWXスクリプトをコンパイルできません。") \
    X(scriptErrorInvalidCardImagePosition, "scriptErrorInvalidCardImagePosition", "イメージの配置形式が正しくありません。") \
    X(scriptErrorInvalidStartAction, "scriptErrorInvalidStartAction", "未知の戦闘行動開始タイミングです。") \
    X(scriptErrorInvalidMatchingType, "scriptErrorInvalidMatchingType", "未知の判定条件です。") \
    X(scriptErrorInvalidMatchingCondition, "scriptErrorInvalidMatchingCondition", "未知のマッチング条件です。") \
    X(scriptErrorInvalidUpdateType, "scriptErrorInvalidUpdateType", "未知の更新タイプです。") \
    X(scriptErrorInvalidEnvironmentStatus, "scriptErrorInvalidEnvironmentStatus", "未知の状況です。") \
    X(scriptErrorInvalidVariableType, "scriptErrorInvalidVariableType", "未知の状態変数です。") \
    X(scriptErrorInvalidAbsorbTo, "scriptErrorInvalidAbsorbTo", "未知の吸収者です。") \
    X(dlgTitScriptVarSet, "dlgTitScriptVarSet", "値が未決定の変数の設定") \
    X(scriptVarSet, "scriptVarSet", "変数に値を入力") \
    X(scriptVarNameColumn, "scriptVarNameColumn", "変数名") \
    X(scriptVarValueColumn, "scriptVarValueColumn", "値") \
    X(material, "material", "ファイル") \
    X(coupon, "coupon", "クーポン") \
    X(gossip, "gossip", "ゴシップ") \
    X(completeStamp, "completeStamp", "終了印") \
    X(jpyErrorInfoWithoutFile, "jpyErrorInfoWithoutFile", "%1$s (%2$s 行目)") \
    X(jpyError, "jpyError", "%1$s\n%2$s の %3$s 行目") \
    X(jpyErrorDupSection, "jpyErrorDupSection", "セクション名が重複してます: %1$s") \
    X(jpyErrorInvalidPoint, "jpyErrorInvalidPoint", "位置の書式が正しくありません: %1$s") \
    X(jpyErrorInvalidRect, "jpyErrorInvalidRect", "位置とサイズの書式が正しくありません: %1$s") \
    X(jpyErrorInvalidRGB, "jpyErrorInvalidRGB", "値が正しくありません: %1$s") \
    X(jpyErrorInvalidEnum, "jpyErrorInvalidEnum", "数値でなければなりません: %1$s") \
    X(jpyErrorInvalidStr, "jpyErrorInvalidStr", "文字コードが正しくありません: %1$s") \
    X(jpyErrorInvalidInt, "jpyErrorInvalidInt", "数値でなければなりません: %1$s") \
    X(jpyErrorInvalidBool, "jpyErrorInvalidBool", "真偽値が正しくありません: %1$s") \
    X(jpyErrorInvalidEncoding, "jpyErrorInvalidEncoding", "文字コードが不正です。") \
    X(jpyErrorInvalidLine, "jpyErrorInvalidLine", "コマンドか値のどちらかが欠けています: %1$s") \
    X(jpyErrorInvalidCommand, "jpyErrorInvalidCommand", "コマンドが正しくありません: %1$s") \
    X(jpyErrorInvalidStartTag, "jpyErrorInvalidStartTag", "開始タグが正しくありません: %1$s") \
    X(jpyErrorLabelNotFound, "jpyErrorLabelNotFound", "セクションがありません。") \
    X(expressionErrorFunctionIsNotExistsInTargetVersion, "expressionErrorFunctionIsNotExistsInTargetVersion", "%1$sはWsn.%2$s以降でなければ使用できません。") \
    X(expressionErrorStructureIsNotExistsInTargetVersion, "expressionErrorStructureIsNotExistsInTargetVersion", "%1$sはWsn.%2$s以降でなければ使用できません。") \
    X(expressionErrorStructureMemberIsNotExistsInTargetVersion, "expressionErrorStructureMemberIsNotExistsInTargetVersion", "%1$s.%2$sはWsn.%3$s以降でなければ使用できません。") \
    X(expressionErrorPermissionOfStructure, "expressionErrorPermissionOfStructure", "%1$sは関数で生成しなければなりません。") \
    X(expressionErrorPermissionOfStructureMember, "expressionErrorPermissionOfStructureMember", "%1$s.%2$sは参照できません。") \
    X(expressionErrorFunctionIsNotDefined, "expressionErrorFunctionIsNotDefined", "%1$sという名前の関数はありません。") \
    X(expressionErrorValueIsNotString, "expressionErrorValueIsNotString", "演算対象が文字列ではありません。") \
    X(expressionErrorValueIsNotNumber, "expressionErrorValueIsNotNumber", "演算対象が数値ではありません。") \
    X(expressionErrorValueIsNotBoolean, "expressionErrorValueIsNotBoolean", "演算対象が真偽値ではありません。") \
    X(expressionErrorValueIsNotList, "expressionErrorValueIsNotList", "演算対象がリストではありません。") \
    X(expressionErrorValueIsNotStructure, "expressionErrorValueIsNotStructure", "演算対象が構造体ではありません。") \
    X(expressionErrorDivisionByZero, "expressionErrorDivisionByZero", "ゼロによる除算が行われました。") \
    X(expressionErrorInvalidCharacter, "expressionErrorInvalidCharacter", "式に使用できない文字が含まれています。") \
    X(expressionErrorInvalidFunctionCall, "expressionErrorInvalidFunctionCall", "関数の呼び出し方が正しくありません。") \
    X(expressionErrorNeedOpenParen, "expressionErrorNeedOpenParen", "開き括弧が必要です。") \
    X(expressionErrorNoArgument, "expressionErrorNoArgument", "引数がありません。") \
    X(expressionErrorNeedBoolean, "expressionErrorNeedBoolean", "真偽値が必要です。") \
    X(expressionErrorNeedSymbolOrNumber, "expressionErrorNeedSymbolOrNumber", "数値または関数呼び出しが必要です。") \
    X(expressionErrorNeedOperator, "expressionErrorNeedOperator", "演算子が必要です。") \
    X(expressionErrorInvalidNumber, "expressionErrorInvalidNumber", "数値が大きすぎます。") \
    X(expressionErrorInvalidSemantics, "expressionErrorInvalidSemantics", "式の構文が正しくありません。") \
    X(expressionErrorUnknownSymbol, "expressionErrorUnknownSymbol", "未知のシンボルです: %1$s") \
    X(expressionErrorInvalidArgumentCount, "expressionErrorInvalidArgumentCount", "%1$sの引数は%2$s件でなければなりません。") \
    X(expressionErrorInvalidArgumentCount2, "expressionErrorInvalidArgumentCount2", "%1$sの引数は%2$s～%3$s件でなければなりません。") \
    X(expressionErrorArgumentIsNotNumber, "expressionErrorArgumentIsNotNumber", "%1$sの%2$s番目の引数は数値でなければなりません。") \
    X(expressionErrorArgumentIsNotString, "expressionErrorArgumentIsNotString", "%1$sの%2$s番目の引数は文字列でなければなりません。") \
    X(expressionErrorArgumentIsNotBoolean, "expressionErrorArgumentIsNotBoolean", "%1$sの%2$s番目の引数は真偽値でなければなりません。") \
    X(expressionErrorArgumentIsNotList, "expressionErrorArgumentIsNotList", "%1$sの%2$s番目の引数はリストでなければなりません。") \
    X(expressionErrorArgumentIsNotStructure, "expressionErrorArgumentIsNotStructure", "%1$sの%2$s番目の引数は%3$sでなければなりません。") \
    X(expressionErrorMinimumValue, "expressionErrorMinimumValue", "%1$sの%2$s番目の引数は%3$s以上でなければなりません: %4$s < %3$s") \
    X(expressionErrorNoArgumentWithFunctionName, "expressionErrorNoArgumentWithFunctionName", "%1$sの引数がありません。") \
    X(expressionErrorConversionToNumber, "expressionErrorConversionToNumber", "数値に変換できません: %1$s") \
    X(expressionErrorConversionToString, "expressionErrorConversionToString", "リストまたは構造体は文字列に変換できません: %1$s") \
    X(expressionErrorVariantNotFound, "expressionErrorVariantNotFound", "コモン「%1$s」は存在しません。") \
    X(expressionErrorFlagNotFound, "expressionErrorFlagNotFound", "フラグ「%1$s」は存在しません。") \
    X(expressionErrorStepNotFound, "expressionErrorStepNotFound", "ステップ「%1$s」は存在しません。") \
    X(expressionErrorInvalidStepValue, "expressionErrorInvalidStepValue", "ステップ「%1$s」の最大値より大きな値です: %2$s") \
    X(expressionErrorListIndexIsOutOfRange, "expressionErrorListIndexIsOutOfRange", "リストに%1$s番目の要素は存在しません: リストの長さ = %2$s") \
    X(expressionErrorCompareDifferentStructures, "expressionErrorCompareDifferentStructures", "異なる種類の構造体を比較しようとしました: %1$s <> %2$s") \
    X(expressionErrorTokenIsNotStructureMemberName, "expressionErrorTokenIsNotStructureMemberName", "構造体のメンバ名が正しくありません: %1$s") \
    X(expressionErrorSymbolIsNotStructureMemberNameOne, "expressionErrorSymbolIsNotStructureMemberNameOne", "%1$sのメンバ名が正しくありません。%2$sのみ参照可能です。") \
    X(expressionErrorSymbolIsNotStructureMemberName, "expressionErrorSymbolIsNotStructureMemberName", "%1$sのメンバ名が正しくありません。%2$sのいずれかである必要があります。") \
    X(expressionErrorStructureHasNotPublicMember, "expressionErrorStructureHasNotPublicMember", "%1$sには参照可能なメンバがありません。") \
    X(warningExpression, "warningExpression", "%1$s → %2$s") \
    X(warningExpressionPosition, "warningExpressionPosition", " >>>") \
    X(warningExpressionEnd, "warningExpressionEnd", " <<<") \
    X(warningExpressionNeedBooleanReturnType, "warningExpressionNeedBooleanReturnType", "式の結果は%1$sですが、%2$sコンテントでは真偽値でなければなりません。") \
    X(warningExpressionToFlag, "warningExpressionToFlag", "式の結果は%1$sになるため、フラグへ代入できません。") \
    X(warningExpressionToStep, "warningExpressionToStep", "式の結果は%1$sになるため、ステップへ代入できません。") \
    X(functionNameWithDescription, "functionNameWithDescription", "%1$s - %2$s") \
    X(argNumber, "argNumber", "数値") \
    X(argString, "argString", "文字列") \
    X(argFlag, "argFlag", "フラグ名") \
    X(argStep, "argStep", "ステップ名") \
    X(argVariant, "argVariant", "コモン名") \
    X(argBoolean, "argBoolean", "真偽値") \
    X(argList, "argList", "リスト") \
    X(argExpression, "argExpression", "式") \
    X(argStatus, "argStatus", "状態") \
    X(argCardInfo, "argCardInfo", "カード情報") \
    X(argAny, "argAny", "任意の値") \
    X(argNumberOrString, "argNumberOrString", "文字列または数値") \
    X(argNumberOrList, "argNumberOrList", "数値またはリスト") \
    X(functionAs, "functionAs", "%1$s → %2$s") \
    X(funcAdditionalDescSpecialCharacters, "funcAdditionalDescSpecialCharacters", "次の特殊文字が使用できます。\n* = 任意文字列, ? = 任意1文字, [ABC] = A・B・Cのいずれか1文字, [!ABC] = A・B・Cのいずれでもない1文字。\n特殊文字を普通の文字のように検索したい時は[[]のように[]で囲います。") \
    X(funcAdditionalDescStatuses, "funcAdditionalDescStatuses", "状態は次のキーワードで指定してください。\nPOISON(中毒), SLEEP(睡眠), BIND(呪縛), PARALYZE(麻痺・石化), CONFUSE(混乱), OVERHEAT(激昂), BRAVE(勇敢), PANIC(恐慌), SILENCE(沈黙), FACEUP(暴露), ANTIMAGIC(魔法無効化), ENHACTION(行動力変化), ENHAVOID(回避力変化), ENHRESIST(抵抗力変化), ENHDEFENSE(防御力変化)") \
    X(funcDescLen, "funcDescLen", "文字列の長さ(文字数)を返します。") \
    X(funcDescFind, "funcDescFind", "対象文字列内に検索文字列があった場合は位置(1～)を返します。見つからなかった場合は0を返します。") \
    X(funcDescLeft, "funcDescLeft", "文字列の左側を返します。") \
    X(funcDescRight, "funcDescRight", "文字列の右側を返します。") \
    X(funcDescMid, "funcDescMid", "文字列の中間部分を返します。長さを省略した場合は、指定位置より右側を返します。") \
    X(funcDescStr, "funcDescStr", "任意の値を文字列に変換します。") \
    X(funcDescValue, "funcDescValue", "任意の値を数値に変換します。") \
    X(funcDescInt, "funcDescInt", "任意の値を整数に変換します。小数点以下の値は切り捨てられます。") \
    X(funcDescIf, "funcDescIf", "真偽値がTRUEであれば2つめの引数を、FALSEであれば3つめの引数を返します。") \
    X(funcDescIsStr, "funcDescIsStr", "引数が文字列ならTRUEを返します。") \
    X(funcDescIsNumber, "funcDescIsNumber", "引数が数値ならTRUEを返します。") \
    X(funcDescIsList, "funcDescIsList", "引数がリストならTRUEを返します。") \
    X(funcDescDice, "funcDescDice", "任意面数のダイスを振って結果の値を返します。") \
    X(funcDescMax, "funcDescMax", "引数の中で最大の数値を返します。") \
    X(funcDescMin, "funcDescMin", "引数の中で最小の数値を返します。") \
    X(funcDescVar, "funcDescVar", "コモンの値を返します。@\"コモン名\"と書く事もできます。") \
    X(funcDescFlagValue, "funcDescFlagValue", "フラグの値(真偽値)を返します。") \
    X(funcDescFlagText, "funcDescFlagText", "フラグの値のテキストを返します。2つめの引数を省略した場合は、フラグの現在値のテキストを返します。") \
    X(funcDescStepValue, "funcDescStepValue", "ステップの値(数値)を返します。") \
    X(funcDescHasVariantValue, "funcDescHasVariantValue", "指定したカードに指定名のコモンがあるかを判定します。") \
    X(funcDescHasStep, "funcDescHasStep", "指定したカードに指定名のステップがあるかを判定します。") \
    X(funcDescHasFlag, "funcDescHasFlag", "指定したカードに指定名のフラグがあるかを判定します。") \
    X(funcDescSetVariantValueOfCard, "funcDescSetVariantValueOfCard", "選択カードの変数を変更します。") \
    X(funcDescSetStepValueOfCard, "funcDescSetStepValueOfCard", "選択カードのステップを変更します。") \
    X(funcDescSetFlagValueOfCard, "funcDescSetFlagValueOfCard", "選択カードのフラグを変更します。") \
    X(funcDescStepText, "funcDescStepText", "ステップの値のテキストを返します。2つめの引数を省略した場合は、ステップの現在値のテキストを返します。") \
    X(funcDescStepMax, "funcDescStepMax", "ステップの最大値を返します。") \
    X(funcDescCardMaxUseLimit, "funcDescCardMaxUseLimit", "選択カードの最大値を返す") \
    X(funcDescSelected, "funcDescSelected", "選択メンバの番号を1以上の数値で返します。選択メンバがいない場合は0を返します。") \
    X(funcDescLatestUseCardOwner, "funcDescLatestUseCardOwner", "最新の使用中カードイベントの使用者の番号を1以上の数値で返します。該当する使用中カードがない場合は0を返します。") \
    X(funcDescCharExists, "funcDescCharExists", "指定したキャラクター番号のキャラクターが存在すればTRUE、存在しなければFALSEを返します。") \
    X(funcDescCastType, "funcDescCastType", "キャラクターのタイプを返します(PLAYER=プレイヤー, ENEMY=エネミー, FRIEND=同行キャスト)。該当者がいない場合は0を返します。") \
    X(funcDescCastName, "funcDescCastName", "キャラクターの名前を返します。該当者がいない場合は空文字列を返します。") \
    X(funcDescFindCoupon, "funcDescFindCoupon", "キャラクターの所持するクーポンを検索し、クーポン番号を返します。見つからなかった場合は0を返します。\n%1$s") \
    X(funcDescCouponText, "funcDescCouponText", "キャラクターの所持するクーポンの名前を返します。見つからなかった場合は空文字列を返します。") \
    X(funcDescFindGossip, "funcDescFindGossip", "ゴシップを検索し、ゴシップ番号を返します。見つからなかった場合は0を返します。\n%1$s") \
    X(funcDescGossipText, "funcDescGossipText", "ゴシップの名前を返します。見つからなかった場合は空文字列を返します。") \
    X(funcDescPartyName, "funcDescPartyName", "操作中のパーティの名前を返します。") \
    X(funcDescList, "funcDescList", "リストを生成します。") \
    X(funcDescAt, "funcDescAt", "リストの要素を返します。") \
    X(funcDescLLen, "funcDescLLen", "リストの長さ(要素数)を返します。") \
    X(funcDescLFind, "funcDescLFind", "対象リスト内に検索値があった場合は位置(1～)を返します。見つからなかった場合は0を返します。") \
    X(funcDescLLeft, "funcDescLLeft", "リストの左側を返します。") \
    X(funcDescLRight, "funcDescLRight", "リストの右側を返します。") \
    X(funcDescLMid, "funcDescLMid", "リストの中間部分を返します。長さを省略した場合は、指定位置より右側を返します。") \
    X(funcDescPartyMoney, "funcDescPartyMoney", "操作中のパーティの所持金を返します。") \
    X(funcDescPartyNumber, "funcDescPartyNumber", "操作中のパーティの人数を返します。") \
    X(funcDescYadoName, "funcDescYadoName", "操作中の拠点名を返します。") \
    X(funcDescSkinType, "funcDescSkinType", "操作中のスキン種別を返します。以下のスキン種別が一般に知られていますが、プレイヤーの環境によってはこれ以外の値が返される場合もあります。\n\"MedievalFantasy\"(中世ファンタジーⅠ型), \"Modern\"(現代Ⅰ型), \"School\"(現代学園ものバリアントエンジン), \"Oedo\"(カアドワアス大江戸バリアント), \"ScienceFiction\"(SFバリアント), \"Monsters\"(妖魔バリアント)") \
    X(funcDescBattleRound, "funcDescBattleRound", "現在のバトルのラウンド数を返します。バトル中ではない場合は-1を返します。") \
    X(funcDescCastLevel, "funcDescCastLevel", "キャラクターのレベルを返します。該当者がいない場合は0を返します。") \
    X(funcDescCouponValue, "funcDescCouponValue", "キャラクターの所持するクーポンの点数を返します。該当者またはクーポンが見つからなかった場合は0を返します。") \
    X(funcDescLifeRatio, "funcDescLifeRatio", "キャラクターのライフ残量を0.0～1.0の割合で返します。該当者がいない場合は-1を返します。") \
    X(funcDescStatusValue, "funcDescStatusValue", "キャラクターの状態の強度・修正値を返します。該当者がいない・指定した状態ではない場合は0を返します。強度・修正値を持たない状態にある場合は残りラウンド数を返します。\n%1$s") \
    X(funcDescStatusRound, "funcDescStatusRound", "キャラクターの状態の残りラウンド数を返します。該当者がいない・指定した状態ではない場合は0を返します。\n%1$s") \
    X(funcDescSelectedCard, "funcDescSelectedCard", "選択カードのカード情報を返します。選択カードが存在しない場合は無効なカード情報を返します。") \
    X(funcDescCardName, "funcDescCardName", "カード名を返します。カード情報が無効な場合は空文字列を返します。") \
    X(funcDescCardType, "funcDescCardType", "カードのタイプを返します(SKILL=特殊技能, ITEM=アイテム, BEAST=召喚獣, ACTIONCARD=アクションカード)。カード情報が無効な場合は0を返します。") \
    X(funcDescCardRarity, "funcDescCardRarity", "カードの希少度を返します(RARE=レア, PREMIER=プレミア)。一般カードの場合は0を返します。カード情報が無効な場合は-1を返します。") \
    X(funcDescCardPrice, "funcDescCardPrice", "カードの価格を返します。カード情報が無効な場合は-1を返します。") \
    X(funcDescCardLevel, "funcDescCardLevel", "特殊技能カードのレベルを返します。カード情報が無効か、特殊技能カードでない場合は-1を返します。") \
    X(funcDescCardCount, "funcDescCardCount", "カードの残り使用回数を返します。カード情報が無効な場合は-1を返します。\n使用回数0のアイテムカードと召喚獣は無限に使用できるカードとなりますが、リサイクルカードの場合は例外として使用できない状態になります。") \
    X(funcDescFindKeyCode, "funcDescFindKeyCode", "カードのキーコードを検索し、キーコード番号(1～)を返します。空文字列のキーコードは検索されません。見つからなかった場合は0を返します。\n%1$s") \
    X(funcDescKeyCodeText, "funcFindKeyCodeText", "カードのキーコード名を返します。見つからなかった場合は空文字列を返します。") \
    X(funcDescList_e, "funcDescList_e", "即時評価されたリストを生成します。引数は可変個です。") \
    X(funcDescFor, "funcDescFor", "for文処理を行います。引数1は数字, リスト or リストのリスト, 引数2は変数を指す文字列 or そのリスト, 引数3は計算式") \
    X(funcDescSwitch, "funcDescSwitch", "第一引数の値によって、第三引数のリスト内の対応する式だけを評価して返します。第二引数はデフォルト式です。") \
    X(funcDescSum, "funcDescSum", "リスト中の数値要素を合計して返します。第2引数 convert=1 で文字列を数値に変換して合算します。") \
    X(funcDescEval, "funcDescEval", "文字列を式として評価します。リストまたは構造体の場合は、その要素を再帰的に評価します。") \
    X(funcDescFormat, "funcDescFormat", "第1引数の文字列にある{}を、後続の引数の文字列表現で順番に置き換えます。") \
    X(funcDescFloor, "funcDescFloor", "小数点以下を切り捨てた数値を返します。") \
    X(funcDescCeil, "funcDescCeil", "小数点以下を切り上げた数値を返します。") \
    X(funcDescSind, "funcDescSind", "度数法での正弦を返します（引数は度）。") \
    X(funcDescCosd, "funcDescCosd", "度数法での余弦を返します（引数は度）。") \
    X(funcDescTand, "funcDescTand", "度数法での正接を返します（引数は度）。余弦が0に近い場合はエラーになります。") \
    X(funcDescDistance, "funcDescDistance", "2点間のユークリッド距離を返します。DISTANCE(x1, y1, x2, y2)") \
    X(funcDescParseInt, "funcDescParseInt", "引数を整数に変換して返します。文字列は数値として解釈し、最後に切り捨てます。変換できない場合は第2引数を返します。") \
    X(funcDescParseFloat, "funcDescParseFloat", "引数を数値に変換して返します。文字列は数値として解釈します。変換できない場合は第2引数を返します。") \
    X(funcDescAndD, "funcDescAndD", "複数の真偽値をAND演算します（深さ優先）。引数は1個以上のブールです。エディタではダミー実装です。") \
    X(funcDescOrD, "funcDescOrD", "複数の真偽値をOR演算します（深さ優先）。引数は1個以上のブールです。エディタではダミー実装です。") \
    X(funcDescInPolygon, "funcDescInPolygon", "指定した座標がポリゴン内にあるかを判定します。第1引数は頂点座標のリスト、第2引数は判定対象の座標です。") \
    X(funcDescGetRandomList, "funcDescGetRandomList", "リストからランダムに要素を抽出して返します。開始位置と長さを指定できます。") \
    X(funcDescConcatStrings, "funcDescConcatStrings", "リストの要素を文字列化して結合します。第2引数で区切り文字を指定できます。") \
    X(funcDescPickByIndices, "funcDescPickByIndices", "リストから指定したインデックスの要素を抽出して新しいリストを返します。") \
    X(funcDescLGetRandom, "funcDescLGetRandom", "リストからランダムに1つの要素を選んで返します。") \
    X(funcDescLSetMulti, "funcDescLSetMulti", "変数に格納されたリストの複数位置に値を設定します。") \
    X(funcDescLRemove, "funcDescLRemove", "変数に格納されたリストの指定位置の要素を削除します。") \
    X(funcDescLRemoveMulti, "funcDescLRemoveMulti", "変数に格納されたリストから複数のインデックスの要素を削除します。") \
    X(funcDescLInsert, "funcDescLInsert", "変数に格納されたリストの指定位置に要素を挿入します。") \
    X(funcDescLUnique, "funcDescLUnique", "リスト内の重複要素を削除して一意化します。") \
    X(funcDescLRemoveAll, "funcDescLRemoveAll", "リスト内で指定した値を全て削除します。") \
    X(funcDescLCount, "funcDescLCount", "リスト内または変数のリスト内で指定値の出現回数を返します。") \
    X(funcDescLSet, "funcDescLSet", "第1引数で指定した変数に格納されたリストの指定位置の要素を別の値に置換します。第2引数には数値または数値のリストを指定できます。数値のリストを指定する場合、その変数の値はリストのリストでなければなりません。") \
    X(funcDescContain, "funcDescContain", "第1引数のリストが第2引数の全要素を含むかを判定します。") \
    X(funcDescScenarioName, "funcDescScenarioName", "現在のシナリオの名前を返します。") \
    X(funcDescDateTime, "funcDescDateTime", "現在の日時をリスト形式で返します。") \
    X(funcDescUnixTime, "funcDescUnixTime", "UNIXTIMEを取得") \
    X(funcDescMouseX, "funcDescMouseX", "アクションタッチ時のX座標を返します。") \
    X(funcDescMouseY, "funcDescMouseY", "アクションタッチ時のY座標を返します。") \
    X(funcShortDescLen, "funcShortDescLen", "文字列の長さを返す") \
    X(funcShortDescFind, "funcDescFind", "文字列内を検索") \
    X(funcShortDescLeft, "funcShortDescLeft", "文字列の左側を返す") \
    X(funcShortDescRight, "funcShortDescRight", "文字列の右側を返す") \
    X(funcShortDescMid, "funcShortDescMid", "文字列の中間部分を返す") \
    X(funcShortDescStr, "funcShortDescStr", "文字列へ変換") \
    X(funcShortDescValue, "funcShortDescValue", "数値へ変換") \
    X(funcShortDescInt, "funcShortDescInt", "整数へ変換") \
    X(funcShortDescIf, "funcShortDescIf", "真偽値でどちらかの値を返す") \
    X(funcShortDescIsStr, "funcShortDescIsStr", "文字列かを判定") \
    X(funcShortDescIsNumber, "funcShortDescIsNumber", "数値かを判定") \
    X(funcShortDescIsList, "funcShortDescIsList", "リストかを判定") \
    X(funcShortDescDice, "funcShortDescDice", "ダイスを振る") \
    X(funcShortDescMax, "funcShortDescMax", "最大値を返す") \
    X(funcShortDescMin, "funcShortDescMin", "最小値を返す") \
    X(funcShortDescVar, "funcShortDescVar", "コモンの値を返す") \
    X(funcShortDescFlagValue, "funcShortDescFlagValue", "フラグの値を返す") \
    X(funcShortDescFlagText, "funcShortDescFlagText", "フラグのテキストを返す") \
    X(funcShortDescStepValue, "funcShortDescStepValue", "ステップの値を返す") \
    X(funcShortDescHasVariantValue, "funcShortDescHasVariantValue", "カードのコモンの有無を判定") \
    X(funcShortDescHasStep, "funcShortDescHasStep", "カードのステップの有無を判定") \
    X(funcShortDescHasFlag, "funcShortDescHasFlag", "カードのフラグの有無を判定") \
    X(funcShortDescSetVariantValueOfCard, "funcShortDescSetVariantValueOfCard", "選択カードの変数を変更") \
    X(funcShortDescSetStepValueOfCard, "funcShortDescSetStepValueOfCard", "選択カードのステップを変更") \
    X(funcShortDescSetFlagValueOfCard, "funcShortDescSetFlagValueOfCard", "選択カードのフラグを変更") \
    X(funcShortDescStepText, "funcShortDescStepText", "ステップのテキストを返す") \
    X(funcShortDescStepMax, "funcShortDescStepMax", "ステップの最大値を返す") \
    X(funcShortDescCardMaxUseLimit, "funcShortDescCardMaxUseLimit", "選択カードの最大値を返す") \
    X(funcShortDescSelected, "funcShortDescSelected", "選択メンバを返す") \
    X(funcShortDescLatestUseCardOwner, "funcShortDescLatestUseCardOwner", "最新の使用カードの使用者を返す") \
    X(funcShortDescCharExists, "funcShortDescCharExists", "キャラクターの存在を判定") \
    X(funcShortDescCastType, "funcShortDescCastType", "キャラクターのタイプを返す") \
    X(funcShortDescCastName, "funcShortDescCastName", "キャラクターの名前を返す") \
    X(funcShortDescFindCoupon, "funcShortDescFindCoupon", "クーポンを検索") \
    X(funcShortDescCouponText, "funcShortDescCouponText", "クーポン名を取得") \
    X(funcShortDescFindGossip, "funcShortDescFindGossip", "ゴシップを検索") \
    X(funcShortDescGossipText, "funcShortDescGossipText", "ゴシップ名を取得") \
    X(funcShortDescPartyName, "funcShortDescPartyName", "パーティ名を取得") \
    X(funcShortDescList, "funcShortDescList", "リストを生成する") \
    X(funcShortDescAt, "funcShortDescAt", "リストの要素を返す") \
    X(funcShortDescLLen, "funcShortDescLLen", "リストの長さを返す") \
    X(funcShortDescLFind, "funcDescLFind", "リスト内を検索") \
    X(funcShortDescLLeft, "funcShortDescLLeft", "リストの左側を返す") \
    X(funcShortDescLRight, "funcShortDescLRight", "リストの右側を返す") \
    X(funcShortDescLMid, "funcShortDescLMid", "リストの中間部分を返す") \
    X(funcShortDescPartyMoney, "funcShortDescPartyMoney", "パーティの所持金を取得") \
    X(funcShortDescPartyNumber, "funcShortDescPartyNumber", "パーティの人数を取得") \
    X(funcShortDescYadoName, "funcShortDescYadoName", "拠点名を取得") \
    X(funcShortDescSkinType, "funcShortDescSkinType", "スキン種別を取得") \
    X(funcShortDescBattleRound, "funcShortDescBattleRound", "バトルラウンドを取得") \
    X(funcShortDescCastLevel, "funcShortDescCastLevel", "キャラクターのレベルを返す") \
    X(funcShortDescCouponValue, "funcShortDescCouponValue", "クーポンの点数を返す") \
    X(funcShortDescLifeRatio, "funcShortDescLifeRatio", "キャラクターのライフ残量を返す") \
    X(funcShortDescStatusValue, "funcShortDescStatusValue", "キャラクターの状態の強度・修正値を返す") \
    X(funcShortDescStatusRound, "funcShortDescStatusRound", "キャラクターの状態の残ラウンド数を返す") \
    X(funcShortDescSelectedCard, "funcShortDescSelectedCard", "選択カードを返す。") \
    X(funcShortDescCardName, "funcShortDescCardName", "カード名を返す。") \
    X(funcShortDescCardType, "funcShortDescCardType", "カードのタイプを返す。") \
    X(funcShortDescCardRarity, "funcShortDescCardRarity", "カードの希少度を返す。") \
    X(funcShortDescCardPrice, "funcShortDescCardPrice", "カードの価格を返す。") \
    X(funcShortDescCardLevel, "funcShortDescCardLevel", "カードのレベルを返す。") \
    X(funcShortDescCardCount, "funcShortDescCardCount", "カードの使用回数を返す。") \
    X(funcShortDescFindKeyCode, "funcShortDescFindKeyCode", "キーコードを検索") \
    X(funcShortDescKeyCodeText, "funcShortDescKeyCodeText", "キーコード名を取得") \
    X(funcShortDescList_e, "funcShortDescList_e", "即時評価リスト") \
    X(funcShortDescFor, "funcShortDescFor", "for文処理") \
    X(funcShortDescSwitch, "funcShortDescSwitch", "switch風分岐") \
    X(funcShortDescSum, "funcShortDescSum", "リスト合計") \
    X(funcShortDescEval, "funcShortDescEval", "式を評価") \
    X(funcShortDescFormat, "funcShortDescFormat", "文字列を整形") \
    X(funcShortDescFloor, "funcShortDescFloor", "切り捨て（floor）") \
    X(funcShortDescCeil, "funcShortDescCeil", "切り上げ（ceil）") \
    X(funcShortDescSind, "funcShortDescSind", "正弦（sind）") \
    X(funcShortDescCosd, "funcShortDescCosd", "余弦（cosd）") \
    X(funcShortDescTand, "funcShortDescTand", "正接（tand）") \
    X(funcShortDescDistance, "funcShortDescDistance", "距離（distance）") \
    X(funcShortDescParseInt, "funcShortDescParseInt", "整数変換（parseint）") \
    X(funcShortDescParseFloat, "funcShortDescParseFloat", "数値変換（parsefloat）") \
    X(funcShortDescAndD, "funcShortDescAndD", "論理AND（深さ優先）") \
    X(funcShortDescOrD, "funcShortDescOrD", "論理OR（深さ優先）") \
    X(funcShortDescInPolygon, "funcShortDescInPolygon", "ポリゴン内判定") \
    X(funcShortDescGetRandomList, "funcShortDescGetRandomList", "ランダムリスト取得") \
    X(funcShortDescConcatStrings, "funcShortDescConcatStrings", "文字列連結") \
    X(funcShortDescPickByIndices, "funcShortDescPickByIndices", "インデックス抽出") \
    X(funcShortDescLGetRandom, "funcShortDescLGetRandom", "ランダム選択") \
    X(funcShortDescLSetMulti, "funcShortDescLSetMulti", "複数要素更新") \
    X(funcShortDescLRemove, "funcShortDescLRemove", "要素削除") \
    X(funcShortDescLRemoveMulti, "funcShortDescLRemoveMulti", "複数要素削除") \
    X(funcShortDescLInsert, "funcShortDescLInsert", "要素挿入") \
    X(funcShortDescLUnique, "funcShortDescLUnique", "重複削除") \
    X(funcShortDescLRemoveAll, "funcShortDescLRemoveAll", "値を全削除") \
    X(funcShortDescLCount, "funcShortDescLCount", "出現回数") \
    X(funcShortDescLSet, "funcShortDescLSet", "要素置換") \
    X(funcShortDescContain, "funcShortDescContain", "包含判定") \
    X(funcShortDescScenarioName, "funcShortDescScenarioName", "シナリオ名取得") \
    X(funcShortDescDateTime, "funcShortDescDateTime", "日時取得") \
    X(funcShortDescUnixTime, "funcShortDescUnixTime", "UNIXTIMEを取得") \
    X(funcShortDescMouseX, "funcShortDescMouseX", "タッチX座標") \
    X(funcShortDescMouseY, "funcShortDescMouseY", "タッチY座標") \
    X(expressionErrorCosineIsZero, "expressionErrorCosineIsZero", "余弦が0に近いため正接を計算できません。") \
    X(funcExampleLen, "funcExampleLen", "LEN(\"対象文字列\") = 5") \
    X(funcExampleFind, "funcExampleFind", "FIND(\"文字\", \"対象文字列\") = 3") \
    X(funcExampleLeft, "funcExampleLeft", "LEFT(\"対象文字列\", 2) = \"対象\"") \
    X(funcExampleRight, "funcExampleRight", "RIGHT(\"対象文字列\", 3) = \"文字列\"") \
    X(funcExampleMid, "funcExampleMid", "MID(\"対象文字列\", 3, 2) = \"文字\"") \
    X(funcExampleStr, "funcExampleStr", "STR(999.9) = \"999.9\"") \
    X(funcExampleValue, "funcExampleValue", "VALUE(\"999.9\") = 999.9") \
    X(funcExampleInt, "funcExampleInt", "INT(\"999.9\") = 999") \
    X(funcExampleIf, "funcExampleIf", "IF(42 = 99, \"真\", \"偽\") = \"偽\"") \
    X(funcExampleIsStr, "funcExampleIsStr", "IS_STR(\"文字列\") = TRUE") \
    X(funcExampleIsNumber, "funcExampleIsNumber", "IS_NUMBER(42) = TRUE") \
    X(funcExampleIsList, "funcExampleIsList", "IS_LIST(LIST(1, 2)) = TRUE") \
    X(funcExampleDice, "funcExampleDice", "DICE(2, 6) = 12") \
    X(funcExampleMax, "funcExampleMax", "MAX(1, 23, 4, 5) = 23") \
    X(funcExampleMin, "funcExampleMin", "MIN(12, 3, 4, 5) = 3") \
    X(funcExampleVar, "funcExampleVar", "VAR(\"コモンA\") = \"コモンAの値\"") \
    X(funcExampleFlagValue, "funcExampleFlagValue", "FLAGVALUE(\"フラグA\") = TRUE") \
    X(funcExampleFlagText, "funcExampleFlagText", "FLAGTEXT(\"フラグA\", TRUE) = \"真\"") \
    X(funcExampleStepValue, "funcExampleStepValue", "STEPVALUE(\"ステップA\") = 5") \
    X(funcExampleHasVariantValue, "funcExampleHasVariantValue", "HAS_VARIANTVALUE(SELECTEDCARD(), \"コモンA\") = TRUE") \
    X(funcExampleHasStep, "funcExampleHasStep", "HAS_STEP(SELECTEDCARD(), \"ステップA\") = TRUE") \
    X(funcExampleHasFlag, "funcExampleHasFlag", "HAS_FLAG(SELECTEDCARD(), \"フラグA\") = TRUE") \
    X(funcExampleSetVariantValueOfCard, "funcExampleSetVariantValueOfCard", "SETVARIANTVALUEOFCARD(SELECTEDCARD(), \"変数A\", 1) = TRUE") \
    X(funcExampleSetStepValueOfCard, "funcExampleSetStepValueOfCard", "SETSTEPVALUEOFCARD(SELECTEDCARD(), \"ステップA\", 1) = TRUE") \
    X(funcExampleSetFlagValueOfCard, "funcExampleSetFlagValueOfCard", "SETFLAGVALUEOFCARD(SELECTEDCARD(), \"フラグA\", TRUE) = TRUE") \
    X(funcExampleStepText, "funcExampleStepText", "STEPTEXT(\"ステップA\", 5) = \"Step - 5\"") \
    X(funcExampleStepMax, "funcExampleStepMax", "STEPMAX(\"ステップA\") = 9") \
    X(funcExampleCardMaxUseLimit, "funcExampleCardMaxUseLimit", "CARDMAXUSELIMIT(選択カード) = 3") \
    X(funcExampleSelected, "funcExampleSelected", "SELECTED() <> 0") \
    X(funcExampleLatestUseCardOwner, "funcExampleLatestUseCardOwner", "LATESTUSECARDOWNER() <> 0") \
    X(funcExampleCharExists, "funcExampleCharExists", "CHAREXISTS(SELECTED()) = TRUE") \
    X(funcExampleCastType, "funcExampleCastType", "CASTTYPE(SELECTED()) = ENEMY") \
    X(funcExampleCastName, "funcExampleCastName", "CASTNAME(SELECTED()) = \"コボルト\"") \
    X(funcExampleFindCoupon, "funcExampleFindCoupon", "FINDCOUPON(SELECTED(), \"*退治\") <> 0") \
    X(funcExampleCouponText, "funcExampleCouponText", "COUPONTEXT(SELECTED(), FINDCOUPON(SELECTED(), \"*退治\")) = \"ゴブリン退治\"") \
    X(funcExampleFindGossip, "funcExampleFindGossip", "FINDGOSSIP(\"*生存\") <> 0") \
    X(funcExampleGossipText, "funcExampleGossipText", "GOSSIPTEXT(FINDGOSSIP(\"*生存\")) = \"ゴブリン生存\"") \
    X(funcExamplePartyName, "funcExamplePartyName", "PARTYNAME()") \
    X(funcExampleList, "funcExampleList", "LIST(42, \"STR\", TRUE)") \
    X(funcExampleAt, "funcExampleAt", "AT(LIST(41, 42, 43), 2) = 42") \
    X(funcExampleLLen, "funcExampleLLen", "LLEN(LIST(1, 2, 3, 5)) = 4") \
    X(funcExampleLFind, "funcExampleLFind", "LFIND(42, LIST(41, 42, 43)) = 2") \
    X(funcExampleLLeft, "funcExampleLLeft", "LEFT(LIST(1, 2, 3), 2) = LIST(1, 2)") \
    X(funcExampleLRight, "funcExampleLRight", "RIGHT(LIST(1, 2, 3), 2) = LIST(2, 3)") \
    X(funcExampleLMid, "funcExampleLMid", "MID(LIST(1, 2, 3, 4, 5), 2, 3) = LIST(2, 3, 4)") \
    X(funcExamplePartyMoney, "funcExamplePartyMoney", "PARTYMONEY() < 4000") \
    X(funcExamplePartyNumber, "funcExamplePartyNumber", "PARTYNUMBER() = 1") \
    X(funcExampleYadoName, "funcExampleYadoName", "YADONAME()") \
    X(funcExampleSkinType, "funcExampleSkinType", "SKINTYPE() = \"MedievalFantasy\"") \
    X(funcExampleBattleRound, "funcExampleBattleRound", "BATTLEROUND() = 1") \
    X(funcExampleCastLevel, "funcExampleCastLevel", "CASTLEVEL(SELECTED()) = 1") \
    X(funcExampleCouponValue, "funcExampleCouponValue", "COUPONVALUE(SELECTED(), \"ゴブリン退治\")") \
    X(funcExampleLifeRatio, "funcExampleLifeRatio", "LIFERATIO(SELECTED()) < 0.5") \
    X(funcExampleStatusValue, "funcExampleStatusValue", "STATUSVALUE(SELECTED(), PARALYZE) >= 20") \
    X(funcExampleStatusRound, "funcExampleStatusRound", "STATUSROUND(SELECTED(), SLEEP) = 1") \
    X(funcExampleSelectedCard, "funcExampleSelectedCard", "SELECTEDCARD()") \
    X(funcExampleCardName, "funcExampleCardName", "CARDNAME(SELECTEDCARD()) = \"居合斬り\"") \
    X(funcExampleCardType, "funcExampleCardType", "CARDTYPE(SELECTEDCARD()) = SKILL") \
    X(funcExampleCardRarity, "funcExampleCardRarity", "CARDRARITY(SELECTEDCARD()) = PREMIER") \
    X(funcExampleCardPrice, "funcExampleCardPrice", "CARDPRICE(SELECTEDCARD()) < 2200") \
    X(funcExampleCardLevel, "funcExampleCardLevel", "CARDLEVEL(SELECTEDCARD()) < 3") \
    X(funcExampleCardCount, "funcExampleCardCount", "CARDCOUNT(SELECTEDCARD()) = 0") \
    X(funcExampleFindKeyCode, "funcExampleFindKeyCode", "FINDKEYCODE(SELECTEDCARD(), \"*攻撃\") <> 0") \
    X(funcExampleKeyCodeText, "funcExampleKeyCodeText", "KEYCODETEXT(SELECTEDCARD(), 1)") \
    X(funcExampleList_e, "funcExampleList_e", "LIST_E(42, \"STR\", TRUE)") \
    X(funcExampleFor, "funcExampleFor", "FOR(4, \"汎用\\1\", @\"汎用\\1\" + 10)") \
    X(funcExampleSwitch, "funcExampleSwitch", "SWITCH(2, \"デフォルト\", LIST(\"A\", \"B\", \"C\")) = \"B\"") \
    X(funcExampleSum, "funcExampleSum", "SUM(LIST(1, 2, 3)) = 6") \
    X(funcExampleEval, "funcExampleEval", "EVAL(\"1 + 2 * 3\") = 7") \
    X(funcExampleFormat, "funcExampleFormat", "FORMAT(\"{}:{}\", \"A\", 2) = \"A:2\"") \
    X(funcExampleFloor, "funcExampleFloor", "FLOOR(3.7) = 3") \
    X(funcExampleCeil, "funcExampleCeil", "CEIL(3.2) = 4") \
    X(funcExampleSind, "funcExampleSind", "SIND(30) = 0.5") \
    X(funcExampleCosd, "funcExampleCosd", "COSD(60) = 0.5") \
    X(funcExampleTand, "funcExampleTand", "TAND(45) = 1") \
    X(funcExampleDistance, "funcExampleDistance", "DISTANCE(0,0,3,4) = 5") \
    X(funcExampleParseInt, "funcExampleParseInt", "PARSEINT(\"3.9\", 0) = 3") \
    X(funcExampleParseFloat, "funcExampleParseFloat", "PARSEFLOAT(\"3.14\", 0) = 3.14") \
    X(funcExampleAndD, "funcExampleAndD", "AND_D(TRUE, TRUE) = TRUE") \
    X(funcExampleOrD, "funcExampleOrD", "OR_D(FALSE, TRUE) = TRUE") \
    X(funcExampleInPolygon, "funcExampleInPolygon", "inpolygon(LIST(LIST(0,0),LIST(10,0),LIST(10,10),LIST(0,10)), LIST(5,5)) = TRUE") \
    X(funcExampleGetRandomList, "funcExampleGetRandomList", "GETRANDOMLIST(1, 10, 3) = LIST(3, 7, 1)") \
    X(funcExampleConcatStrings, "funcExampleConcatStrings", "CONCATSTRINGS(LIST(\"A\", 1, true)) = \"A1TRUE\"") \
    X(funcExamplePickByIndices, "funcExamplePickByIndices", "PICKBYINDICES(LIST(\"A\", \"B\", \"C\"), LIST(1,3)) = LIST(\"A\", \"C\")") \
    X(funcExampleLGetRandom, "funcExampleLGetRandom", "LGETRANDOM(LIST(1,2,3,4)) = 3") \
    X(funcExampleLSetMulti, "funcExampleLSetMulti", "LSETMULTI(\"var1\", LIST(1,3), LIST(\"A\", \"B\")) = TRUE") \
    X(funcExampleLRemove, "funcExampleLRemove", "LREMOVE(\"var1\", 2) = TRUE") \
    X(funcExampleLRemoveMulti, "funcExampleLRemoveMulti", "LREMOVEMULTI(\"var1\", LIST(1,3)) = TRUE") \
    X(funcExampleLInsert, "funcExampleLInsert", "LINSERT(\"var1\", 2, \"X\") = TRUE") \
    X(funcExampleLUnique, "funcExampleLUnique", "LUNIQUE(LIST(1,2,3,4,1)) = LIST(1,2,3,4)") \
    X(funcExampleLRemoveAll, "funcExampleLRemoveAll", "LREMOVEALL(\"var1\", \"A\") = TRUE") \
    X(funcExampleLCount, "funcExampleLCount", "LCOUNT(\"A\", LIST(\"A\", \"B\", \"A\")) = 2") \
    X(funcExampleLSet, "funcExampleLSet", "LSET(\"var1\", 2, \"X\") = TRUE") \
    X(funcExampleContain, "funcExampleContain", "CONTAIN(LIST(0, 0, 100, 100), LIST(50,50)) = TRUE") \
    X(funcExampleScenarioName, "funcExampleScenarioName", "SCENARIONAME() = \"サンプルシナリオ\"") \
    X(funcExampleDateTime, "funcExampleDateTime", "DATETIME() = LIST(2026, 7, 1, 12, 34, 56)") \
    X(funcExampleUnixTime, "funcExampleUnixTime", "UNIXTIME() = 1782864000000") \
    X(funcExampleMouseX, "funcExampleMouseX", "MOUSEX() = 100") \
    X(funcExampleMouseY, "funcExampleMouseY", "MOUSEY() = 200") \
    X(exprStringDesc, "exprStringDesc", "文字列") \
    X(exprFindStringDesc, "exprFindStringDesc", "検索文字列") \
    X(exprTargetStringDesc, "exprTargetStringDesc", "対象文字列") \
    X(exprStringLengthDesc, "exprStringLengthDesc", "長さ(文字数)") \
    X(exprStringPositionDesc, "exprStringPositionDesc", "位置(1～)") \
    X(exprNumberDesc, "exprNumberDesc", "数値") \
    X(exprValueArgDesc, "exprValueArgDesc", "文字列または数値") \
    X(exprBooleanDesc, "exprBooleanDesc", "真偽値") \
    X(exprIfTrueDesc, "exprIfTrueDesc", "TRUEの結果") \
    X(exprIfFalseDesc, "exprIfFalseDesc", "FALSEの結果") \
    X(exprDiceTimesDesc, "exprDiceTimesDesc", "個数") \
    X(exprDiceSidesDesc, "exprDiceSidesDesc", "面数") \
    X(exprVariableLengthNumberDesc, "exprVariableLengthNumberDesc", "数値 %1$s") \
    X(exprVariantDesc, "exprVariantDesc", "コモン名") \
    X(exprFlagDesc, "exprFlagDesc", "フラグ名") \
    X(exprFlagValueDesc, "exprFlagValueDesc", "フラグ値") \
    X(exprStepDesc, "exprStepDesc", "ステップ名") \
    X(exprStepValueDesc, "exprStepValueDesc", "ステップ値") \
    X(exprCastNumberDesc, "exprCastNumberDesc", "キャラクター番号") \
    X(exprFindPatternDesc, "exprFindPatternDesc", "検索文字列") \
    X(exprCouponNameDesc, "exprCouponNameDesc", "クーポン名") \
    X(exprCouponNumberDesc, "exprCouponNumberDesc", "クーポン番号") \
    X(exprGossipNumberDesc, "exprGossipNumberDesc", "ゴシップ番号") \
    X(exprKeyCodeNumberDesc, "exprKeyCodeNumberDesc", "キーコード番号") \
    X(exprFindStartPositionDesc, "exprFindStartPositionDesc", "検索開始位置") \
    X(exprListDesc, "exprListDesc", "リスト") \
    X(exprAnyFindValueDesc, "exprAnyFindValueDesc", "検索値") \
    X(exprListLengthDesc, "exprListLengthDesc", "長さ(要素数)") \
    X(exprListPositionDesc, "exprListPositionDesc", "位置(1～)") \
    X(exprCardInfoDesc, "exprCardInfoDesc", "カード情報") \
    X(exprCastStatusDesc, "exprCastStatusDesc", "状態") \
    X(exprAnyValueDesc, "exprAnyValueDesc", "任意の値") \
    X(exprSwitchIndexDesc, "exprSwitchIndexDesc", "分岐のインデックス(1～)") \
    X(exprConvertFlagDesc, "exprConvertFlagDesc", "変換フラグ(0: 変換しない、1: 文字列を数値に変換)") \
    X(menuTextNone, "menuTextNone", "") \
    X(menuTextFile, "menuTextFile", "ファイル") \
    X(menuTextEdit, "menuTextEdit", "編集") \
    X(menuTextView, "menuTextView", "表示") \
    X(menuTextTool, "menuTextTool", "ツール") \
    X(menuTextTable, "menuTextTable", "テーブル") \
    X(menuTextVariable, "menuTextVariable", "状態変数") \
    X(menuTextHelp, "menuTextHelp", "ヘルプ") \
    X(menuTextCard, "menuTextCard", "カード") \
    X(menuTextCardsAndBacks, "menuTextCardsAndBacks", "カードと背景") \
    X(menuTextDelNotUsedFile, "menuTextDelNotUsedFile", "未使用のファイルを削除") \
    X(menuTextCreateSubWindow, "menuTextCreateSubWindow", "新しいウィンドウで開く") \
    X(menuTextLeftPane, "menuTextLeftPane", "左のタブ") \
    X(menuTextRightPane, "menuTextRightPane", "右のタブ") \
    X(menuTextClosePane, "menuTextClosePane", "閉じる") \
    X(menuTextClosePaneExcept, "menuTextClosePaneExcept", "他のタブを閉じる") \
    X(menuTextClosePaneLeft, "menuTextClosePaneLeft", "左側のタブを閉じる") \
    X(menuTextClosePaneRight, "menuTextClosePaneRight", "右側のタブを閉じる") \
    X(menuTextClosePaneAll, "menuTextClosePaneAll", "全てのタブを閉じる") \
    X(menuTextNew, "menuTextNew", "新規作成") \
    X(menuTextOpen, "menuTextOpen", "開く") \
    X(menuTextNewAtNewWindow, "menuTextNewAtNewWindow", "新しいウィンドウで新規作成") \
    X(menuTextOpenAtNewWindow, "menuTextOpenAtNewWindow", "新しいウィンドウで開く") \
    X(menuTextClose, "menuTextClose", "閉じる") \
    X(menuTextCloseWin, "menuTextCloseWin", "閉じる") \
    X(menuTextSave, "menuTextSave", "上書き保存") \
    X(menuTextSaveAs, "menuTextSaveAs", "名前を付けて保存") \
    X(menuTextReload, "menuTextReload", "再読込") \
    X(menuTextEditScenarioHistory, "menuTextEditScenarioHistory", "履歴とブックマークの編集") \
    X(menuTextEditImportHistory, "menuTextEditImportHistory", "履歴とブックマークの編集") \
    X(menuTextEditExecutedPartyHistory, "menuTextEditExecutedPartyHistory", "履歴とブックマークの編集") \
    X(menuTextPutParty, "menuTextPutParty", "パーティを選択して追加") \
    X(menuTextOpenDir, "menuTextOpenDir", detail::concat("シナリオの", DIR, "を開く")) \
    X(menuTextOpenBackupDir, "menuTextOpenBackupDir", detail::concat("バックアップ", DIR, "を開く")) \
    X(menuTextOpenPlace, "menuTextOpenPlace", "ファイルの場所を開く") \
    X(menuTextSaveImage, "menuTextSaveImage", "格納イメージをファイルに保存") \
    X(menuTextIncludeImage, "menuTextIncludeImage", "イメージを格納する") \
    X(menuTextLookImages, "menuTextLookImages", "画像を一覧表示") \
    X(menuTextEditLayers, "menuTextEditLayers", "レイヤの編集") \
    X(menuTextAddLayer, "menuTextAddLayer", "レイヤの追加") \
    X(menuTextRemoveLayer, "menuTextRemoveLayer", "レイヤの削除") \
    X(menuTextChangeVH, "menuTextChangeVH", "分割領域の縦横を切替") \
    X(menuTextSelectConnectedResource, "menuTextSelectConnectedResource", "関係するリソースを選択") \
    X(menuTextFind, "menuTextFind", "検索と置換") \
    X(menuTextFindID, "menuTextFindID", "参照を検索") \
    X(menuTextIncSearch, "menuTextIncSearch", "絞り込み検索") \
    X(menuTextCloseIncSearch, "menuTextCloseIncSearch", "閉じる") \
    X(menuTextEditProp, "menuTextEditProp", "編集") \
    X(menuTextShowProp, "menuTextShowProp", "詳細") \
    X(menuTextRefresh, "menuTextRefresh", "最新の情報に更新") \
    X(menuTextUndo, "menuTextUndo", "元に戻す") \
    X(menuTextRedo, "menuTextRedo", "やり直し") \
    X(menuTextCut, "menuTextCut", "切り取り") \
    X(menuTextCopy, "menuTextCopy", "コピー") \
    X(menuTextPaste, "menuTextPaste", "貼り付け") \
    X(menuTextDelete, "menuTextDelete", "削除") \
    X(menuTextCut1Content, "menuTextCut1Content", "1コンテント切り取り") \
    X(menuTextCopy1Content, "menuTextCopy1Content", "1コンテントコピー") \
    X(menuTextDelete1Content, "menuTextDelete1Content", "1コンテント削除") \
    X(menuTextPasteInsert, "menuTextPasteInsert", "クリップボードから挿入") \
    X(menuTextClone, "menuTextClone", "複製") \
    X(menuTextSelectAll, "menuTextSelectAll", "全て選択") \
    X(menuTextCopyAll, "menuTextCopyAll", "全てコピー") \
    X(menuTextToXMLText, "menuTextToXMLText", "コピーしたデータをXMLに変換") \
    X(menuTextAddItem, "menuTextAddItem", "追加") \
    X(menuTextDelItem, "menuTextDelItem", "削除") \
    X(menuTextTableView, "menuTextTableView", "テーブルビュー") \
    X(menuTextVarView, "menuTextVarView", "状態変数ビュー") \
    X(menuTextCardView, "menuTextCardView", "カードビュー") \
    X(menuTextCastView, "menuTextCastView", "キャストカードビュー") \
    X(menuTextSkillView, "menuTextSkillView", "特殊技能カードビュー") \
    X(menuTextItemView, "menuTextItemView", "アイテムカードビュー") \
    X(menuTextBeastView, "menuTextBeastView", "召喚獣カードビュー") \
    X(menuTextInfoView, "menuTextInfoView", "情報カードビュー") \
    X(menuTextFileView, "menuTextFileView", "ファイルビュー") \
    X(menuTextCouponView, "menuTextCouponView", "クーポンビュー") \
    X(menuTextGossipView, "menuTextGossipView", "ゴシップビュー") \
    X(menuTextCompleteStampView, "menuTextCompleteStampView", "終了印ビュー") \
    X(menuTextKeyCodeView, "menuTextKeyCodeView", "キーコードビュー") \
    X(menuTextCellNameView, "menuTextCellNameView", "セル名称ビュー") \
    X(menuTextCardGroupView, "menuTextCardGroupView", "カードグループビュー") \
    X(menuTextExecEngine, "menuTextExecEngine", "エンジン起動") \
    X(menuTextExecEngineAuto, "menuTextExecEngineAuto", "自動選択") \
    X(menuTextExecEngineMain, "menuTextExecEngineMain", "CardWirthPy") \
    X(menuTextExecEngineWithParty, "menuTextExecEngineWithParty", "シナリオを開始") \
    X(menuTextExecEngineWithLastParty, "menuTextExecEngineWithLastParty", "前回のパーティで開始") \
    X(menuTextDeleteNotExistsParties, "menuTextDeleteNotExistsParties", "存在しないパーティを履歴から削除") \
    X(menuTextOuterTools, "menuTextOuterTools", "外部ツール") \
    X(menuTextSettings, "menuTextSettings", "エディタ設定") \
    X(menuTextVersionInfo, "menuTextVersionInfo", "バージョン情報") \
    X(menuTextLockToolBar, "menuTextLockToolBar", "ツールバーを固定") \
    X(menuTextResetToolBar, "menuTextResetToolBar", "配置をリセット") \
    X(menuTextCopyAsText, "menuTextCopyAsText", "テキストとしてコピー") \
    X(menuTextOpenAtView, "menuTextOpenAtView", "ビューで開く") \
    X(menuTextEventToPackage, "menuTextEventToPackage", "このイベントをパッケージ化する") \
    X(menuTextStartToPackage, "menuTextStartToPackage", "このツリーをパッケージ化する") \
    X(menuTextWrapTree, "menuTextWrapTree", "ここから別のツリーにする") \
    X(menuTextCreateContent, "menuTextCreateContent", "コンテントの作成") \
    X(menuTextConvertContent, "menuTextConvertContent", "変換") \
    X(menuTextCGroupTerminal, "menuTextCGroupTerminal", "開始/終端") \
    X(menuTextCGroupStandard, "menuTextCGroupStandard", "基本") \
    X(menuTextCGroupData, "menuTextCGroupData", "変数操作/分岐") \
    X(menuTextCGroupUtility, "menuTextCGroupUtility", "状況分岐") \
    X(menuTextCGroupBranch, "menuTextCGroupBranch", "保有分岐") \
    X(menuTextCGroupGet, "menuTextCGroupGet", "取得") \
    X(menuTextCGroupLost, "menuTextCGroupLost", "喪失") \
    X(menuTextCGroupVisual, "menuTextCGroupVisual", "外観操作") \
    X(menuTextCGroupVariant, "menuTextCGroupVariant", "演算") \
    X(menuTextEditSummary, "menuTextEditSummary", "シナリオの設定") \
    X(menuTextNewAreaDir, "menuTextNewAreaDir", "フォルダの作成") \
    X(menuTextNewArea, "menuTextNewArea", "エリアの作成") \
    X(menuTextNewBattle, "menuTextNewBattle", "バトルの作成") \
    X(menuTextNewPackage, "menuTextNewPackage", "パッケージの作成") \
    X(menuTextReNumberingAll, "menuTextReNumberingAll", "全てのIDを1から振り直す") \
    X(menuTextReNumbering, "menuTextReNumbering", "IDの振り直し") \
    X(menuTextEditScene, "menuTextEditScene", "シーンビューを開く") \
    X(menuTextEditSceneDup, "menuTextEditSceneDup", "新しいシーンビューを開く") \
    X(menuTextEditEvent, "menuTextEditEvent", "イベントビューを開く") \
    X(menuTextEditEventDup, "menuTextEditEventDup", "新しいイベントビューを開く") \
    X(menuTextSetStartArea, "menuTextSetStartArea", "開始エリアにする") \
    X(menuTextNewFlagDir, "menuTextNewFlagDir", "フォルダの作成") \
    X(menuTextNewFlag, "menuTextNewFlag", "フラグの作成") \
    X(menuTextNewStep, "menuTextNewStep", "ステップの作成") \
    X(menuTextNewVariant, "menuTextNewVariant", "コモンの作成") \
    X(menuTextCreateStepValues, "menuTextCreateStepValues", "ステップ値の自動生成") \
    X(menuTextPutSPChar, "menuTextPutSPChar", "特殊文字の挿入") \
    X(menuTextPutFlagValue, "menuTextPutFlagValue", "フラグ値") \
    X(menuTextPutStepValue, "menuTextPutStepValue", "ステップ値") \
    X(menuTextPutVariantValue, "menuTextPutVariantValue", "コモン値") \
    X(menuTextPutColor, "menuTextPutColor", "文字色") \
    X(menuTextPutSkinSPChar, "menuTextPutSkinSPChar", "記号") \
    X(menuTextPutImageFont, "menuTextPutImageFont", "画像参照") \
    X(menuTextCreateVariableEventTree, "menuTextCreateVariableEventTree", "イベントツリーとしてコピー") \
    X(menuTextInitVariablesTree, "menuTextInitVariablesTree", "初期値の設定") \
    X(menuTextCopyVariablePath, "menuTextCopyVariablePath", "状態変数のパスをコピー") \
    X(menuTextUp, "menuTextUp", "選択中のアイテムを上へ移動") \
    X(menuTextDown, "menuTextDown", "選択中のアイテムを下へ移動") \
    X(menuTextConvertCouponType, "menuTextConvertCouponType", "クーポンの種類の変更") \
    X(menuTextConvertCouponTypeNormal, "menuTextConvertCouponTypeNormal", "ノーマル") \
    X(menuTextConvertCouponTypeHide, "menuTextConvertCouponTypeHide", "[＿...] 隠蔽") \
    X(menuTextConvertCouponTypeDur, "menuTextConvertCouponTypeDur", "[：...] 時限") \
    X(menuTextConvertCouponTypeDurBattle, "menuTextConvertCouponTypeDurBattle", "[；...] 戦闘中時限") \
    X(menuTextConvertCouponTypeSystem, "menuTextConvertCouponTypeSystem", "[＠...] システム") \
    X(menuTextCopyTypeConvertedCoupon, "menuTextCopyTypeConvertedCoupon", "種類を指定してコピー") \
    X(menuTextCopyTypeConvertedCouponNormal, "menuTextCopyTypeConvertedCouponNormal", "ノーマル") \
    X(menuTextCopyTypeConvertedCouponHide, "menuTextCopyTypeConvertedCouponHide", "[＿...] 隠蔽") \
    X(menuTextCopyTypeConvertedCouponDur, "menuTextCopyTypeConvertedCouponDur", "[：...] 時限") \
    X(menuTextCopyTypeConvertedCouponDurBattle, "menuTextCopyTypeConvertedCouponDurBattle", "[；...] 戦闘中時限") \
    X(menuTextCopyTypeConvertedCouponSystem, "menuTextCopyTypeConvertedCouponSystem", "[＠...] システム") \
    X(menuTextAddInitialCoupons, "menuTextAddInitialCoupons", "種族と年代の初期クーポンを追加") \
    X(menuTextReverseSignOfValues, "menuTextReverseSignOfValues", "得点の正負を反転する") \
    X(menuTextReverse, "menuTextReverse", "逆順にする") \
    X(menuTextSwapToParent, "menuTextSwapToParent", "親コンテントと入れ替える") \
    X(menuTextSwapToChild, "menuTextSwapToChild", "子コンテントと入れ替える") \
    X(menuTextOverDialog, "menuTextOverDialog", "上のセリフへ移動") \
    X(menuTextUnderDialog, "menuTextUnderDialog", "下のセリフへ移動") \
    X(menuTextCreateDialog, "menuTextCreateDialog", "セリフの作成") \
    X(menuTextDeleteDialog, "menuTextDeleteDialog", "セリフの削除") \
    X(menuTextCopyToAllDialogs, "menuTextCopyToAllDialogs", "セリフを全体にコピー") \
    X(menuTextCopyToUpperDialogs, "menuTextCopyToUpperDialogs", "セリフを上方にコピー") \
    X(menuTextCopyToLowerDialogs, "menuTextCopyToLowerDialogs", "セリフを下方にコピー") \
    X(menuTextShowParty, "menuTextShowParty", "パーティカードの表示") \
    X(menuTextShowMsg, "menuTextShowMsg", "メッセージ枠の表示") \
    X(menuTextShowRefCards, "menuTextShowRefCards", "カード参照の表示") \
    X(menuTextFixedCards, "menuTextFixedCards", "カードの固定") \
    X(menuTextFixedCells, "menuTextFixedCells", "背景の固定") \
    X(menuTextFixedBackground, "menuTextFixedBackground", "最初のフルサイズ背景の固定") \
    X(menuTextShowGrid, "menuTextShowGrid", "グリッドの表示") \
    X(menuTextShowEnemyCardProp, "menuTextShowEnemyCardProp", "レベルとライフを表示") \
    X(menuTextShowCard, "menuTextShowCard", "カードの表示") \
    X(menuTextShowBack, "menuTextShowBack", "背景の表示") \
    X(menuTextNewMenuCard, "menuTextNewMenuCard", "メニューカードの作成") \
    X(menuTextNewEnemyCard, "menuTextNewEnemyCard", "エネミーカードの作成") \
    X(menuTextNewBack, "menuTextNewBack", "背景の作成") \
    X(menuTextNewTextCell, "menuTextNewTextCell", "テキストセルの作成") \
    X(menuTextNewColorCell, "menuTextNewColorCell", "カラーセルの作成") \
    X(menuTextNewPCCell, "menuTextNewPCCell", "プレイヤーキャラクタセルの作成") \
    X(menuTextAutoArrange, "menuTextAutoArrange", "カードを自動的に並べる") \
    X(menuTextManualArrange, "menuTextManualArrange", "カードの位置を自分で決定する") \
    X(menuTextPossibleToRunAway, "menuTextPossibleToRunAway", "パーティの逃走を許可") \
    X(menuTextSortWithPosition, "menuTextSortWithPosition", "左上から右下の順で整列") \
    X(menuTextMask, "menuTextMask", "透明色を使用") \
    X(menuTextEscape, "menuTextEscape", "エネミーの逃走の有無") \
    X(menuTextChangePos, "menuTextChangePos", "位置とサイズの変更") \
    X(menuTextPosTop, "menuTextPosTop", "上に揃える") \
    X(menuTextPosBottom, "menuTextPosBottom", "下に揃える") \
    X(menuTextPosLeft, "menuTextPosLeft", "左に揃える") \
    X(menuTextPosRight, "menuTextPosRight", "右に揃える") \
    X(menuTextPosEven, "menuTextPosEven", "等間隔に並べる") \
    X(menuTextNearTop, "menuTextNearTop", "上へ寄せる") \
    X(menuTextNearBottom, "menuTextNearBottom", "下へ寄せる") \
    X(menuTextNearLeft, "menuTextNearLeft", "左へ寄せる") \
    X(menuTextNearRight, "menuTextNearRight", "右へ寄せる") \
    X(menuTextNearCenterH, "menuTextNearCenterH", "横方向の中央へ寄せる") \
    X(menuTextNearCenterV, "menuTextNearCenterV", "縦方向の中央へ寄せる") \
    X(menuTextNearCenter, "menuTextNearCenter", "中央へ寄せる") \
    X(menuTextScaleMin, "menuTextScaleMin", "最小のカードスケール") \
    X(menuTextScaleMiddle, "menuTextScaleMiddle", "標準のカードスケール") \
    X(menuTextScaleMax, "menuTextScaleMax", "最大のカードスケール") \
    X(menuTextScaleBig, "menuTextScaleBig", "大きく揃える") \
    X(menuTextScaleSmall, "menuTextScaleSmall", "小さく揃える") \
    X(menuTextExpandBack, "menuTextExpandBack", "背景セルを最大化") \
    X(menuTextCopyColor1ToColor2, "menuTextCopyColor1ToColor2", "基本色を終端色へコピー") \
    X(menuTextCopyColor2ToColor1, "menuTextCopyColor2ToColor1", "終端色を基本色へコピー") \
    X(menuTextExchangeColors, "menuTextExchangeColors", "終端色と基本色を交換") \
    X(menuTextStopBGM, "menuTextStopBGM", "%1$sの再生を停止") \
    X(menuTextPlayBGM, "menuTextPlayBGM", "再生") \
    X(menuTextNewEvent, "menuTextNewEvent", "イベントの作成") \
    X(menuTextNewEventWithDialog, "menuTextNewEventWithDialog", "イベントの作成") \
    X(menuTextKeyCodeTiming, "menuTextKeyCodeTiming", "発火タイミングの変更") \
    X(menuTextKeyCodeTimingUse, "menuTextKeyCodeTimingUse", "使用") \
    X(menuTextKeyCodeTimingSuccess, "menuTextKeyCodeTimingSuccess", "[...○] 成功") \
    X(menuTextKeyCodeTimingFailure, "menuTextKeyCodeTimingFailure", "[...×] 失敗") \
    X(menuTextKeyCodeTimingHasNot, "menuTextKeyCodeTimingHasNot", "[！...] 不保有") \
    X(menuTextCopyTimingConvertedKeyCode, "menuTextCopyTimingConvertedKeyCode", "発火タイミングを指定してコピー") \
    X(menuTextCopyTimingConvertedKeyCodeUse, "menuTextCopyTimingConvertedKeyCodeUse", "使用") \
    X(menuTextCopyTimingConvertedKeyCodeSuccess, "menuTextCopyTimingConvertedKeyCodeSuccess", "[...○] 成功") \
    X(menuTextCopyTimingConvertedKeyCodeFailure, "menuTextCopyTimingConvertedKeyCodeFailure", "[...×] 失敗") \
    X(menuTextCopyTimingConvertedKeyCodeHasNot, "menuTextCopyTimingConvertedKeyCodeHasNot", "[！...] 不保有") \
    X(menuTextAddKeyCodesByFeatures, "menuAddKeyCodesByFeatures", "特徴と効果からキーコードを生成") \
    X(menuTextKeyCodeCond, "menuTextKeyCodeCond", "キーコード発火条件") \
    X(menuTextKeyCodeCondOr, "menuTextKeyCodeCondOr", "どれか一つに一致") \
    X(menuTextKeyCodeCondAnd, "menuTextKeyCodeCondAnd", "全てに一致") \
    X(menuTextAddRangeOfRound, "menuTextAddRangeOfRound", "複数のラウンドを追加") \
    X(menuTextOpenAtTableView, "menuTextOpenAtTableView", "テーブルビューで開く") \
    X(menuTextOpenAtVarView, "menuTextOpenAtVarView", "状態変数ビューで開く") \
    X(menuTextOpenAtCardView, "menuTextOpenAtCardView", "カードビューで開く") \
    X(menuTextOpenAtFileView, "menuTextOpenAtFileView", "ファイルビューで開く") \
    X(menuTextOpenAtEventView, "menuTextOpenAtEventView", "イベントビューで開く") \
    X(menuTextComment, "menuTextComment", "コメントを記述") \
    X(menuTextShowCardProp, "menuTextShowCardProp", "カード表示") \
    X(menuTextShowCardImage, "menuTextShowCardImage", "簡略カード表示") \
    X(menuTextShowCardDetail, "menuTextShowCardDetail", "詳細表示") \
    X(menuTextOpenImportSource, "menuTextOpenImportSource", "外部シナリオから追加") \
    X(menuTextSelectImportSource, "menuTextSelectImportSource", "追加元のシナリオを選択") \
    X(menuTextNewCast, "menuTextNewCast", "キャストカードの作成") \
    X(menuTextNewSkill, "menuTextNewSkill", "特殊技能カードの作成") \
    X(menuTextNewItem, "menuTextNewItem", "アイテムカードの作成") \
    X(menuTextNewBeast, "menuTextNewBeast", "召喚獣カードの作成") \
    X(menuTextNewInfo, "menuTextNewInfo", "情報カードの作成") \
    X(menuTextImport, "menuTextImport", "シナリオに追加") \
    X(menuTextOpenHand, "menuTextOpenHand", "所有カード") \
    X(menuTextAddHand, "menuTextAddHand", "所有カードの追加") \
    X(menuTextRemoveRef, "menuTextRemoveRef", "参照から格納へ変更する") \
    X(menuTextEditEventAtTimeOfUsing, "menuTextEditEventAtTimeOfUsing", "使用時イベント等の設定") \
    X(menuTextHold, "menuTextHold", "カードのホールド") \
    X(menuTextPlaySE, "menuTextPlaySE", "再生") \
    X(menuTextStopSE, "menuTextStopSE", "停止") \
    X(menuTextNewDir, "menuTextNewDir", detail::concat("新規", DIR)) \
    X(menuTextCopyFilePath, "menuTextCopyFilePath", "素材のパスをコピー") \
    X(menuTextCreateArchive, "menuTextCreateArchive", "シナリオを圧縮") \
    X(menuTextPutQuick, "menuTextPutQuick", "すぐに配置する") \
    X(menuTextPutSelect, "menuTextPutSelect", "配置先を選択する") \
    X(menuTextPutContinue, "menuTextPutContinue", "配置先を選択して連続で配置する") \
    X(menuTextToScript, "menuTextToScript", "スクリプトに変換してコピー") \
    X(menuTextToScript1Content, "menuTextToScript1Content", "1コンテントをスクリプトに変換") \
    X(menuTextToScriptAll, "menuTextToScriptAll", "全てをスクリプトに変換してコピー") \
    X(menuTextEvTemplates, "menuTextEvTemplates", "イベントテンプレート") \
    X(menuTextEvTemplatesOfScenario, "menuTextEvTemplatesOfScenario", "シナリオのテンプレートを編集") \
    X(menuTextEditSelection, "menuTextEditSelection", "選択肢の編集") \
    X(menuTextExpand, "menuTextExpand", "ツリーを開く") \
    X(menuTextCollapse, "menuTextCollapse", "ツリーを閉じる") \
    X(menuTextSelectCurrentEvent, "menuTextSelectCurrentEvent", "表示中のイベントを選択") \
    X(menuTextResetValues, "menuTextResetValues", "初期値に戻す") \
    X(menuTextResetValuesAll, "menuTextResetValuesAll", "全て初期値に戻す") \
    X(menuTextCustomizeToolBar, "menuTextCustomizeToolBar", "ツールバーの編集") \
    X(menuTextAddTool, "menuTextAddTool", "追加") \
    X(menuTextAddToolBar, "menuTextAddToolBar", "バーの追加") \
    X(menuTextAddToolGroup, "menuTextAddToolGroup", "グループの追加") \
    X(menuTextResetToolBarSettings, "menuTextResetToolBarSettings", "初期設定に戻す") \
    X(menuTextDeleteNotExistsHistory, "menuTextDeleteNotExistsHistory", "存在しないシナリオを履歴から削除") \
    X(menuTextSelectIcon, "menuTextSelectIcon", "アイコンを選択") \
    X(menuTextSelectPresetIcon, "menuTextSelectPresetIcon", "既成のアイコンから選択") \
    X(menuTextDeleteIcon, "menuTextDeleteIcon", "アイコンの削除") \
    X(execEngineWithLastParty, "execEngineWithLastParty", "前回のパーティで開始\n(%1$s > %2$s > %3$s)") \
    X(execEngineWithParty, "execEngineWithParty", "%1$s > %2$s > %3$s") \
    X(upSelection, "upSelection", "上へ") \
    X(downSelection, "downSelection", "下へ") \
    X(setFlagTrue, "setFlagTrue", "TRUEを設定") \
    X(setFlagFalse, "setFlagFalse", "FALSEを設定") \
    X(setStepValue, "setStepValue", "%1$sを設定") \
    X(bgm, "bgm", "BGM") \
    X(newIgnition, "newIgnition", "イベント発火条件の作成") \
    X(expandTree, "expandTree", "全コンテントツリーを開く") \
    X(foldTree, "foldTree", "全コンテントツリーを閉じる") \
    X(showEventTreeDetail, "showEventTreeDetail", "イベントコンテントの詳細を表示") \
    X(showEventTreeLineNumber, "showEventTreeLineNumber", "行番号を表示") \
    X(convertFromNextWarning, "convertFromNextWarning", "@@@NEXT: %1$s") \
    X(convertFromNextWarningBackpackOnly, "convertFromNextWarningBackpackOnly", "「荷物袋のみ」の指定") \
    X(convertFromNextWarningBackpackOfSelectedMember, "convertFromNextWarningBackpackOfSelectedMember", "「選択メンバのバックパック」の指定") \
    X(convertFromNextWarningNoForbid, "convertFromNextWarningNoForbid", "制限コンテント(制限解除)") \
    X(convertFromNextWarningForbidSave, "convertFromNextWarningForbidSave", "セーブ・中断の制限") \
    X(convertFromNextWarningForbidCamp, "convertFromNextWarningForbidCamp", "キャンプの制限") \
    X(convertFromNextWarningWaitMillis, "convertFromNextWarningWaitMillis", "ミリ秒単位の空白時間(%1$sミリ秒)")
#define CWX_ATTRIBUTE_FIELDS(X) \
    X(locale, String, "locale", "ja-JP") \
    X(version_, std::uint64_t, "version", ::cwxeditor::cwx::versioninfo::APP_VERSION_NUM)
#define CWX_AA_FIELDS(X) \
    X(sex, "sex", "key", "name") \
    X(period, "period", "key", "name") \
    X(nature, "nature", "key", "name") \
    X(makings, "makings", "key", "name")
#define CWX_DECLARE_MESSAGE(name, key, initial) Message name{key, initial};
#define CWX_DECLARE_ATTRIBUTE(name, type, key, initial) Attribute<type> name{key, initial};
#define CWX_DECLARE_AA(name, key, keyName, valueName) AssociativeMessage name{key, keyName, valueName};

class Msgs {
private:
    std::vector<Field*> fields_;

    void registerField(Field& field) { fields_.push_back(&field); }
    void registerAllFields() {
        fields_.clear();
#define CWX_REGISTER_MESSAGE(name, key, initial) registerField(name);
        CWX_MESSAGE_FIELDS(CWX_REGISTER_MESSAGE)
#undef CWX_REGISTER_MESSAGE
#define CWX_REGISTER_ATTRIBUTE(name, type, key, initial) registerField(name);
        CWX_ATTRIBUTE_FIELDS(CWX_REGISTER_ATTRIBUTE)
#undef CWX_REGISTER_ATTRIBUTE
#define CWX_REGISTER_AA(name, key, keyName, valueName) registerField(name);
        CWX_AA_FIELDS(CWX_REGISTER_AA)
#undef CWX_REGISTER_AA
    }

    void applyProductBranding() {
        constexpr std::string_view old_name{"CWXEditor"};
        constexpr std::string_view new_name{"CWXEditor_C"};
        for (auto* field : fields_) {
            auto* message_field = dynamic_cast<Message*>(field);
            if (!message_field) continue;
            std::size_t offset{};
            while ((offset = message_field->value.find(old_name, offset)) != String::npos) {
                const auto suffix = offset + old_name.size();
                if (message_field->value.compare(suffix, 2U, "_C") == 0) {
                    offset = suffix + 2U;
                    continue;
                }
                message_field->value.replace(offset, old_name.size(), new_name);
                offset += new_name.size();
            }
        }
    }

public:
    CWX_ATTRIBUTE_FIELDS(CWX_DECLARE_ATTRIBUTE)
    CWX_MESSAGE_FIELDS(CWX_DECLARE_MESSAGE)
    CWX_AA_FIELDS(CWX_DECLARE_AA)

    Msgs() {
        registerAllFields();

        sex.value = {
            {"♂", "♂"},
            {"♀", "♀"}
        };
        period.value = {
            {"子供", "子供"},
            {"若者", "若者"},
            {"大人", "大人"},
            {"老人", "老人"}
        };
        nature.value = {
            {"他種族", "他種族"},
            {"標準型", "標準型"},
            {"理性型", "理性型"},
            {"参謀型", "参謀型"},
            {"妖木族", "妖木族"},
            {"知将型", "知将型"},
            {"隠密型", "隠密型"},
            {"人獣族", "人獣族"},
            {"万能型", "万能型"},
            {"秀才型", "秀才型"},
            {"小悪魔", "小悪魔"},
            {"策士型", "策士型"},
            {"根性型", "根性型"},
            {"剣客型", "剣客型"},
            {"悪鬼族", "悪鬼族"},
            {"勇将型", "勇将型"},
            {"熱血型", "熱血型"},
            {"蜥蜴族", "蜥蜴族"},
            {"豪傑型", "豪傑型"},
            {"秀英型", "秀英型"},
            {"人狼族", "人狼族"},
            {"英明型", "英明型"},
            {"剣豪型", "剣豪型"},
            {"鬼人族", "鬼人族"},
            {"無双型", "無双型"},
            {"賢才型", "賢才型"},
            {"大悪魔", "大悪魔"},
            {"天才型", "天才型"},
            {"努力型", "努力型"},
            {"晩成型", "晩成型"},
            {"妖虫族", "妖虫族"},
            {"凡庸型", "凡庸型"},
            {"超人型", "超人型"},
            {"覇道型", "覇道型"},
            {"闇の者", "闇の者"},
            {"英雄型", "英雄型"},
            {"神竜族", "神竜族"},
            {"神仙型", "神仙型"}
        };
        makings.value = {
            {"秀麗", "秀麗"},
            {"醜悪", "醜悪"},
            {"高貴の出", "高貴の出"},
            {"下賎の出", "下賎の出"},
            {"都会育ち", "都会育ち"},
            {"田舎育ち", "田舎育ち"},
            {"裕福", "裕福"},
            {"貧乏", "貧乏"},
            {"厚き信仰", "厚き信仰"},
            {"不心得者", "不心得者"},
            {"誠実", "誠実"},
            {"不実", "不実"},
            {"冷静沈着", "冷静沈着"},
            {"猪突猛進", "猪突猛進"},
            {"貪欲", "貪欲"},
            {"無欲", "無欲"},
            {"献身的", "献身的"},
            {"利己的", "利己的"},
            {"秩序派", "秩序派"},
            {"混沌派", "混沌派"},
            {"進取派", "進取派"},
            {"保守派", "保守派"},
            {"神経質", "神経質"},
            {"鈍感", "鈍感"},
            {"好奇心旺盛", "好奇心旺盛"},
            {"無頓着", "無頓着"},
            {"過激", "過激"},
            {"穏健", "穏健"},
            {"楽観的", "楽観的"},
            {"悲観的", "悲観的"},
            {"勤勉", "勤勉"},
            {"遊び人", "遊び人"},
            {"陽気", "陽気"},
            {"内気", "内気"},
            {"派手", "派手"},
            {"地味", "地味"},
            {"高慢", "高慢"},
            {"謙虚", "謙虚"},
            {"上品", "上品"},
            {"粗野", "粗野"},
            {"武骨", "武骨"},
            {"繊細", "繊細"},
            {"硬派", "硬派"},
            {"軟派", "軟派"},
            {"お人好し", "お人好し"},
            {"ひねくれ者", "ひねくれ者"},
            {"名誉こそ命", "名誉こそ命"},
            {"愛に生きる", "愛に生きる"}
        };
    }

    Msgs(const Msgs& other)
        : locale(other.locale), version_(other.version_)
#define CWX_COPY_MESSAGE(name, key, initial) , name(other.name)
        CWX_MESSAGE_FIELDS(CWX_COPY_MESSAGE)
#undef CWX_COPY_MESSAGE
#define CWX_COPY_AA(name, key, keyName, valueName) , name(other.name)
        CWX_AA_FIELDS(CWX_COPY_AA)
#undef CWX_COPY_AA
        {
        registerAllFields();
    }

    Msgs(Msgs&& other) noexcept
        : locale(std::move(other.locale)), version_(std::move(other.version_))
#define CWX_MOVE_MESSAGE(name, key, initial) , name(std::move(other.name))
        CWX_MESSAGE_FIELDS(CWX_MOVE_MESSAGE)
#undef CWX_MOVE_MESSAGE
#define CWX_MOVE_AA(name, key, keyName, valueName) , name(std::move(other.name))
        CWX_AA_FIELDS(CWX_MOVE_AA)
#undef CWX_MOVE_AA
        {
        registerAllFields();
    }

    Msgs& operator=(const Msgs& other) {
        if (this != &other) {
            locale = other.locale;
            version_ = other.version_;
#define CWX_ASSIGN_MESSAGE(name, key, initial) name = other.name;
            CWX_MESSAGE_FIELDS(CWX_ASSIGN_MESSAGE)
#undef CWX_ASSIGN_MESSAGE
#define CWX_ASSIGN_AA(name, key, keyName, valueName) name = other.name;
            CWX_AA_FIELDS(CWX_ASSIGN_AA)
#undef CWX_ASSIGN_AA
        }
        return *this;
    }

    Msgs& operator=(Msgs&& other) noexcept {
        if (this != &other) {
            locale = std::move(other.locale);
            version_ = std::move(other.version_);
#define CWX_MOVE_ASSIGN_MESSAGE(name, key, initial) name = std::move(other.name);
            CWX_MESSAGE_FIELDS(CWX_MOVE_ASSIGN_MESSAGE)
#undef CWX_MOVE_ASSIGN_MESSAGE
#define CWX_MOVE_ASSIGN_AA(name, key, keyName, valueName) name = std::move(other.name);
            CWX_AA_FIELDS(CWX_MOVE_ASSIGN_AA)
#undef CWX_MOVE_ASSIGN_AA
        }
        return *this;
    }

    [[nodiscard]] const Message* findMessage(
        const std::string_view key) const noexcept {
        for (const auto* field : fields_) {
            const auto* message = dynamic_cast<const Message*>(field);
            if (message && message->KEY == key) return message;
        }
        return nullptr;
    }

private:
    void toNodeImpl(XmlNode& node, bool writeAll) const {
        for (const auto* field : fields_) field->toNode(node, writeAll);
    }

public:
    void toNode(XmlNode& node, bool writeAll) const {
        auto root = node.newElement("message");
        toNodeImpl(root, writeAll);
    }
    void toNode(XmlNode& node) const { toNode(node, false); }

    static Msgs fromNode(const XmlNode& node, std::uint64_t dataVersion = 0) {
        Msgs result;
        XmlNode root = node;
        if (root.name() != "message") {
            const auto child = root.child("message", false);
            if (!child.valid()) return result;
            root = child;
        }
        for (auto* field : result.fields_) field->fromNode(root, dataVersion);
        // 翻訳カタログのキーはD版と互換にするが、 C++
        // 製品の表示名を旧アプリ名へ戻さないようにする。
        result.applyProductBranding();
        return result;
    }

    String toXML() const;
    String toXML(bool writeAll) const;
    String toXml() const { return toXML(); }
    String toXml(bool writeAll) const { return toXML(writeAll); }
    static Msgs fromXML(const String& xml, std::uint64_t dataVersion = 0);
    static Msgs fromXml(const String& xml, std::uint64_t dataVersion = 0) {
        return fromXML(xml, dataVersion);
    }

    String defaultSelection(const String& s) const;

    template <typename Id>
    String cardImagePositionName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return cardImagePositionNameDefault.value;
        case 1: return cardImagePositionNameCenter.value;
        case 2: return cardImagePositionNameTopLeft.value;
        default: throw std::out_of_range("Unknown cardImagePositionName enum value");
        }
    }

    template <typename Id>
    String smoothingName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return smoothingNameDefault.value;
        case 1: return smoothingNameTrue.value;
        case 2: return smoothingNameFalse.value;
        default: throw std::out_of_range("Unknown smoothingName enum value");
        }
    }

    template <typename Id>
    String matchingConditionName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return matchingConditionNameHas.value;
        case 1: return matchingConditionNameHasNot.value;
        default: throw std::out_of_range("Unknown matchingConditionName enum value");
        }
    }

    template <typename Id>
    String matchingConditionKeyCodeDesc(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return matchingConditionKeyCodeDescHas.value;
        case 1: return matchingConditionKeyCodeDescHasNot.value;
        default: throw std::out_of_range("Unknown matchingConditionKeyCodeDesc enum value");
        }
    }

    template <typename Id>
    String couponTypeDesc(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return couponTypeDescNormal.value;
        case 1: return couponTypeDescHide.value;
        case 2: return couponTypeDescSystem.value;
        case 3: return couponTypeDescDur.value;
        case 4: return couponTypeDescDurBattle.value;
        default: throw std::out_of_range("Unknown couponTypeDesc enum value");
        }
    }

    template <typename Id>
    String couponTypeLongDesc(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return couponTypeLongDescNormal.value;
        case 1: return couponTypeLongDescHide.value;
        case 2: return couponTypeLongDescSystem.value;
        case 3: return couponTypeLongDescDur.value;
        case 4: return couponTypeLongDescDurBattle.value;
        default: throw std::out_of_range("Unknown couponTypeLongDesc enum value");
        }
    }

    template <typename Id>
    String couponTypeShortDesc(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return couponTypeShortDescNormal.value;
        case 1: return couponTypeShortDescHide.value;
        case 2: return couponTypeShortDescSystem.value;
        case 3: return couponTypeShortDescDur.value;
        case 4: return couponTypeShortDescDurBattle.value;
        default: throw std::out_of_range("Unknown couponTypeShortDesc enum value");
        }
    }

    template <typename Id>
    String couponTypeName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return couponTypeNameNormal.value;
        case 1: return couponTypeNameHide.value;
        case 2: return couponTypeNameSystem.value;
        case 3: return couponTypeNameDur.value;
        case 4: return couponTypeNameDurBattle.value;
        default: throw std::out_of_range("Unknown couponTypeName enum value");
        }
    }

    template <typename Id>
    String matchingTypeName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return matchingTypeNameAnd.value;
        case 1: return matchingTypeNameOr.value;
        default: throw std::out_of_range("Unknown matchingTypeName enum value");
        }
    }

    template <typename Id>
    String scTalkerName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return scTalkerNameSelected.value;
        case 1: return scTalkerNameUnselected.value;
        case 2: return scTalkerNameRandom.value;
        case 3: return scTalkerNameCard.value;
        case 4: return scTalkerNameValued.value;
        default: throw std::out_of_range("Unknown scTalkerName enum value");
        }
    }

    template <typename Id>
    String transitionName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return transitionNameDefault.value;
        case 1: return transitionNameNone.value;
        case 2: return transitionNameBlinds.value;
        case 3: return transitionNamePixelDissolve.value;
        case 4: return transitionNameFade.value;
        default: throw std::out_of_range("Unknown transitionName enum value");
        }
    }

    template <typename Id>
    String environmentStatusName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return environmentStatusNameNotSet.value;
        case 1: return environmentStatusNameEnable.value;
        case 2: return environmentStatusNameDisable.value;
        default: throw std::out_of_range("Unknown environmentStatusName enum value");
        }
    }

    template <typename Id>
    String environmentStatusNameEnabled(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return environmentStatusNameEnabledNotSet.value;
        case 1: return environmentStatusNameEnabledEnable.value;
        case 2: return environmentStatusNameEnabledDisable.value;
        default: throw std::out_of_range("Unknown environmentStatusNameEnabled enum value");
        }
    }

    template <typename Id>
    String variableTypeName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return variableTypeNameFlag.value;
        case 1: return variableTypeNameStep.value;
        case 2: return variableTypeNameVariant.value;
        default: throw std::out_of_range("Unknown variableTypeName enum value");
        }
    }

    template <typename Id>
    String functionCategoryName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return functionCategoryNameStringOperation.value;
        case 1: return functionCategoryNameNumberOperation.value;
        case 2: return functionCategoryNameListOperation.value;
        case 3: return functionCategoryNameConversion.value;
        case 4: return functionCategoryNameVariableOperation.value;
        case 5: return functionCategoryNameCastInformation.value;
        case 6: return functionCategoryNameCardInformation.value;
        case 7: return functionCategoryNameCouponInformation.value;
        case 8: return functionCategoryNamePlayingInformation.value;
        case 9: return functionCategoryNameEtc.value;
        default: throw std::out_of_range("Unknown functionCategoryName enum value");
        }
    }

    template <typename Id>
    String blendModeName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return blendModeNameNormal.value;
        case 1: return blendModeNameMask.value;
        case 2: return blendModeNameAdd.value;
        case 3: return blendModeNameSubtract.value;
        case 4: return blendModeNameMultiply.value;
        default: throw std::out_of_range("Unknown blendModeName enum value");
        }
    }

    template <typename Id>
    String gradientDirName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return gradientDirNameNone.value;
        case 1: return gradientDirNameLeftToRight.value;
        case 2: return gradientDirNameTopToBottom.value;
        default: throw std::out_of_range("Unknown gradientDirName enum value");
        }
    }

    template <typename Id>
    String borderingTypeName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return borderingTypeNameNone.value;
        case 1: return borderingTypeNameOutline.value;
        case 2: return borderingTypeNameInline.value;
        default: throw std::out_of_range("Unknown borderingTypeName enum value");
        }
    }

    template <typename Id>
    String coordinateTypeName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return coordinateTypeNameNone.value;
        case 1: return coordinateTypeNameAbsolute.value;
        case 2: return coordinateTypeNameRelative.value;
        case 3: return coordinateTypeNamePercentage.value;
        default: throw std::out_of_range("Unknown coordinateTypeName enum value");
        }
    }

    template <typename Id>
    String startActionName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return startActionNameNow.value;
        case 1: return startActionNameCurrentRound.value;
        case 2: return startActionNameNextRound.value;
        default: throw std::out_of_range("Unknown startActionName enum value");
        }
    }

    template <typename Id>
    String contentName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return contentNameStart.value;
        case 1: return contentNameStartBattle.value;
        case 2: return contentNameEnd.value;
        case 3: return contentNameEndBadEnd.value;
        case 4: return contentNameChangeArea.value;
        case 5: return contentNameChangeBgImage.value;
        case 6: return contentNameEffect.value;
        case 7: return contentNameEffectBreak.value;
        case 8: return contentNameLinkStart.value;
        case 9: return contentNameLinkPackage.value;
        case 10: return contentNameTalkMessage.value;
        case 11: return contentNameTalkDialog.value;
        case 12: return contentNamePlayBgm.value;
        case 13: return contentNamePlaySound.value;
        case 14: return contentNameWait.value;
        case 15: return contentNameElapseTime.value;
        case 16: return contentNameCallStart.value;
        case 17: return contentNameCallPackage.value;
        case 18: return contentNameBranchFlag.value;
        case 19: return contentNameBranchMultiStep.value;
        case 20: return contentNameBranchStep.value;
        case 21: return contentNameBranchSelect.value;
        case 22: return contentNameBranchAbility.value;
        case 23: return contentNameBranchRandom.value;
        case 24: return contentNameBranchLevel.value;
        case 25: return contentNameBranchStatus.value;
        case 26: return contentNameBranchPartyNumber.value;
        case 27: return contentNameBranchArea.value;
        case 28: return contentNameBranchBattle.value;
        case 29: return contentNameBranchIsBattle.value;
        case 30: return contentNameBranchCast.value;
        case 31: return contentNameBranchItem.value;
        case 32: return contentNameBranchSkill.value;
        case 33: return contentNameBranchInfo.value;
        case 34: return contentNameBranchBeast.value;
        case 35: return contentNameBranchMoney.value;
        case 36: return contentNameBranchCoupon.value;
        case 37: return contentNameBranchCompleteStamp.value;
        case 38: return contentNameBranchGossip.value;
        case 39: return contentNameSetFlag.value;
        case 40: return contentNameSetStep.value;
        case 41: return contentNameSetStepUp.value;
        case 42: return contentNameSetStepDown.value;
        case 43: return contentNameReverseFlag.value;
        case 44: return contentNameCheckFlag.value;
        case 45: return contentNameGetCast.value;
        case 46: return contentNameGetItem.value;
        case 47: return contentNameGetSkill.value;
        case 48: return contentNameGetInfo.value;
        case 49: return contentNameGetBeast.value;
        case 50: return contentNameGetMoney.value;
        case 51: return contentNameGetCoupon.value;
        case 52: return contentNameGetCompleteStamp.value;
        case 53: return contentNameGetGossip.value;
        case 54: return contentNameLoseCast.value;
        case 55: return contentNameLoseItem.value;
        case 56: return contentNameLoseSkill.value;
        case 57: return contentNameLoseInfo.value;
        case 58: return contentNameLoseBeast.value;
        case 59: return contentNameLoseMoney.value;
        case 60: return contentNameLoseCoupon.value;
        case 61: return contentNameLoseCompleteStamp.value;
        case 62: return contentNameLoseGossip.value;
        case 63: return contentNameShowParty.value;
        case 64: return contentNameHideParty.value;
        case 65: return contentNameRedisplay.value;
        case 66: return contentNameSubstituteStep.value;
        case 67: return contentNameSubstituteFlag.value;
        case 68: return contentNameBranchStepCmp.value;
        case 69: return contentNameBranchFlagCmp.value;
        case 70: return contentNameBranchRandomSelect.value;
        case 71: return contentNameBranchKeyCode.value;
        case 72: return contentNameCheckStep.value;
        case 73: return contentNameBranchRound.value;
        case 74: return contentNameMoveBgImage.value;
        case 75: return contentNameReplaceBgImage.value;
        case 76: return contentNameLoseBgImage.value;
        case 77: return contentNameBranchMultiCoupon.value;
        case 78: return contentNameBranchMultiRandom.value;
        case 79: return contentNameMoveCard.value;
        case 80: return contentNameChangeEnvironment.value;
        case 81: return contentNameBranchVariant.value;
        case 82: return contentNameSetVariant.value;
        case 83: return contentNameCheckVariant.value;
        case 84: return contentNameCustomXmlContent.value;
        default: throw std::out_of_range("Unknown contentName enum value");
        }
    }

    template <typename Id>
    String contentDesc(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return contentDescStart.value;
        case 1: return contentDescStartBattle.value;
        case 2: return contentDescEnd.value;
        case 3: return contentDescEndBadEnd.value;
        case 4: return contentDescChangeArea.value;
        case 5: return contentDescChangeBgImage.value;
        case 6: return contentDescEffect.value;
        case 7: return contentDescEffectBreak.value;
        case 8: return contentDescLinkStart.value;
        case 9: return contentDescLinkPackage.value;
        case 10: return contentDescTalkMessage.value;
        case 11: return contentDescTalkDialog.value;
        case 12: return contentDescPlayBgm.value;
        case 13: return contentDescPlaySound.value;
        case 14: return contentDescWait.value;
        case 15: return contentDescElapseTime.value;
        case 16: return contentDescCallStart.value;
        case 17: return contentDescCallPackage.value;
        case 18: return contentDescBranchFlag.value;
        case 19: return contentDescBranchMultiStep.value;
        case 20: return contentDescBranchStep.value;
        case 21: return contentDescBranchSelect.value;
        case 22: return contentDescBranchAbility.value;
        case 23: return contentDescBranchRandom.value;
        case 24: return contentDescBranchLevel.value;
        case 25: return contentDescBranchStatus.value;
        case 26: return contentDescBranchPartyNumber.value;
        case 27: return contentDescBranchArea.value;
        case 28: return contentDescBranchBattle.value;
        case 29: return contentDescBranchIsBattle.value;
        case 30: return contentDescBranchCast.value;
        case 31: return contentDescBranchItem.value;
        case 32: return contentDescBranchSkill.value;
        case 33: return contentDescBranchInfo.value;
        case 34: return contentDescBranchBeast.value;
        case 35: return contentDescBranchMoney.value;
        case 36: return contentDescBranchCoupon.value;
        case 37: return contentDescBranchCompleteStamp.value;
        case 38: return contentDescBranchGossip.value;
        case 39: return contentDescSetFlag.value;
        case 40: return contentDescSetStep.value;
        case 41: return contentDescSetStepUp.value;
        case 42: return contentDescSetStepDown.value;
        case 43: return contentDescReverseFlag.value;
        case 44: return contentDescCheckFlag.value;
        case 45: return contentDescGetCast.value;
        case 46: return contentDescGetItem.value;
        case 47: return contentDescGetSkill.value;
        case 48: return contentDescGetInfo.value;
        case 49: return contentDescGetBeast.value;
        case 50: return contentDescGetMoney.value;
        case 51: return contentDescGetCoupon.value;
        case 52: return contentDescGetCompleteStamp.value;
        case 53: return contentDescGetGossip.value;
        case 54: return contentDescLoseCast.value;
        case 55: return contentDescLoseItem.value;
        case 56: return contentDescLoseSkill.value;
        case 57: return contentDescLoseInfo.value;
        case 58: return contentDescLoseBeast.value;
        case 59: return contentDescLoseMoney.value;
        case 60: return contentDescLoseCoupon.value;
        case 61: return contentDescLoseCompleteStamp.value;
        case 62: return contentDescLoseGossip.value;
        case 63: return contentDescShowParty.value;
        case 64: return contentDescHideParty.value;
        case 65: return contentDescRedisplay.value;
        case 66: return contentDescSubstituteStep.value;
        case 67: return contentDescSubstituteFlag.value;
        case 68: return contentDescBranchStepCmp.value;
        case 69: return contentDescBranchFlagCmp.value;
        case 70: return contentDescBranchRandomSelect.value;
        case 71: return contentDescBranchKeyCode.value;
        case 72: return contentDescCheckStep.value;
        case 73: return contentDescBranchRound.value;
        case 74: return contentDescMoveBgImage.value;
        case 75: return contentDescReplaceBgImage.value;
        case 76: return contentDescLoseBgImage.value;
        case 77: return contentDescBranchMultiCoupon.value;
        case 78: return contentDescBranchMultiRandom.value;
        case 79: return contentDescMoveCard.value;
        case 80: return contentDescChangeEnvironment.value;
        case 81: return contentDescBranchVariant.value;
        case 82: return contentDescSetVariant.value;
        case 83: return contentDescCheckVariant.value;
        case 84: return contentDescCustomXmlContent.value;
        default: throw std::out_of_range("Unknown contentDesc enum value");
        }
    }

    template <typename Id>
    String motionName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return motionNameHeal.value;
        case 1: return motionNameDamage.value;
        case 2: return motionNameAbsorb.value;
        case 3: return motionNameParalyze.value;
        case 4: return motionNameDisParalyze.value;
        case 5: return motionNamePoison.value;
        case 6: return motionNameDisPoison.value;
        case 7: return motionNameGetSkillPower.value;
        case 8: return motionNameLoseSkillPower.value;
        case 9: return motionNameSleep.value;
        case 10: return motionNameConfuse.value;
        case 11: return motionNameOverheat.value;
        case 12: return motionNameBrave.value;
        case 13: return motionNamePanic.value;
        case 14: return motionNameNormal.value;
        case 15: return motionNameBind.value;
        case 16: return motionNameDisBind.value;
        case 17: return motionNameSilence.value;
        case 18: return motionNameDisSilence.value;
        case 19: return motionNameFaceUp.value;
        case 20: return motionNameFaceDown.value;
        case 21: return motionNameAntiMagic.value;
        case 22: return motionNameDisAntiMagic.value;
        case 23: return motionNameEnhanceAction.value;
        case 24: return motionNameEnhanceAvoid.value;
        case 25: return motionNameEnhanceResist.value;
        case 26: return motionNameEnhanceDefense.value;
        case 27: return motionNameVanishTarget.value;
        case 28: return motionNameVanishCard.value;
        case 29: return motionNameVanishBeast.value;
        case 30: return motionNameDealAttackCard.value;
        case 31: return motionNameDealPowerfulAttackCard.value;
        case 32: return motionNameDealCriticalAttackCard.value;
        case 33: return motionNameDealFeintCard.value;
        case 34: return motionNameDealDefenseCard.value;
        case 35: return motionNameDealDistanceCard.value;
        case 36: return motionNameDealConfuseCard.value;
        case 37: return motionNameDealSkillCard.value;
        case 38: return motionNameSummonBeast.value;
        case 39: return motionNameCancelAction.value;
        case 40: return motionNameNoEffect.value;
        default: throw std::out_of_range("Unknown motionName enum value");
        }
    }

    template <typename Id>
    String motionDesc(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return motionDescHeal.value;
        case 1: return motionDescDamage.value;
        case 2: return motionDescAbsorb.value;
        case 3: return motionDescParalyze.value;
        case 4: return motionDescDisParalyze.value;
        case 5: return motionDescPoison.value;
        case 6: return motionDescDisPoison.value;
        case 7: return motionDescGetSkillPower.value;
        case 8: return motionDescLoseSkillPower.value;
        case 9: return motionDescSleep.value;
        case 10: return motionDescConfuse.value;
        case 11: return motionDescOverheat.value;
        case 12: return motionDescBrave.value;
        case 13: return motionDescPanic.value;
        case 14: return motionDescNormal.value;
        case 15: return motionDescBind.value;
        case 16: return motionDescDisBind.value;
        case 17: return motionDescSilence.value;
        case 18: return motionDescDisSilence.value;
        case 19: return motionDescFaceUp.value;
        case 20: return motionDescFaceDown.value;
        case 21: return motionDescAntiMagic.value;
        case 22: return motionDescDisAntiMagic.value;
        case 23: return motionDescEnhanceAction.value;
        case 24: return motionDescEnhanceAvoid.value;
        case 25: return motionDescEnhanceResist.value;
        case 26: return motionDescEnhanceDefense.value;
        case 27: return motionDescVanishTarget.value;
        case 28: return motionDescVanishCard.value;
        case 29: return motionDescVanishBeast.value;
        case 30: return motionDescDealAttackCard.value;
        case 31: return motionDescDealPowerfulAttackCard.value;
        case 32: return motionDescDealCriticalAttackCard.value;
        case 33: return motionDescDealFeintCard.value;
        case 34: return motionDescDealDefenseCard.value;
        case 35: return motionDescDealDistanceCard.value;
        case 36: return motionDescDealConfuseCard.value;
        case 37: return motionDescDealSkillCard.value;
        case 38: return motionDescSummonBeast.value;
        case 39: return motionDescCancelAction.value;
        case 40: return motionDescNoEffect.value;
        default: throw std::out_of_range("Unknown motionDesc enum value");
        }
    }

    template <typename Id>
    String updateTypeName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return updateTypeNameFixed.value;
        case 1: return updateTypeNameVariables.value;
        case 2: return updateTypeNameAll.value;
        default: throw std::out_of_range("Unknown updateTypeName enum value");
        }
    }

    template <typename Id>
    String keyCodeTiming(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return keyCodeTimingUse.value;
        case 1: return keyCodeTimingSuccess.value;
        case 2: return keyCodeTimingFailure.value;
        case 3: return keyCodeTimingHasNot.value;
        default: throw std::out_of_range("Unknown keyCodeTiming enum value");
        }
    }

    template <typename Id>
    String physicalName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return physicalNameDex.value;
        case 1: return physicalNameAgl.value;
        case 2: return physicalNameInt.value;
        case 3: return physicalNameStr.value;
        case 4: return physicalNameVit.value;
        case 5: return physicalNameMin.value;
        default: throw std::out_of_range("Unknown physicalName enum value");
        }
    }

    template <typename Id>
    String mentalName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return mentalNameAggressive.value;
        case 1: return mentalNameUnaggressive.value;
        case 2: return mentalNameCheerful.value;
        case 3: return mentalNameUncheerful.value;
        case 4: return mentalNameBrave.value;
        case 5: return mentalNameUnbrave.value;
        case 6: return mentalNameCautious.value;
        case 7: return mentalNameUncautious.value;
        case 8: return mentalNameTrickish.value;
        case 9: return mentalNameUntrickish.value;
        default: throw std::out_of_range("Unknown mentalName enum value");
        }
    }

    template <typename Id>
    String statusName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return statusNameActive.value;
        case 1: return statusNameInactive.value;
        case 2: return statusNameAlive.value;
        case 3: return statusNameDead.value;
        case 4: return statusNameFine.value;
        case 5: return statusNameInjured.value;
        case 6: return statusNameHeavyInjured.value;
        case 7: return statusNameUnconscious.value;
        case 8: return statusNamePoison.value;
        case 9: return statusNameSleep.value;
        case 10: return statusNameBind.value;
        case 11: return statusNameParalyze.value;
        case 12: return statusNameConfuse.value;
        case 13: return statusNameOverheat.value;
        case 14: return statusNameBrave.value;
        case 15: return statusNamePanic.value;
        case 16: return statusNameSilence.value;
        case 17: return statusNameFaceUp.value;
        case 18: return statusNameAntiMagic.value;
        case 19: return statusNameUpAction.value;
        case 20: return statusNameUpAvoid.value;
        case 21: return statusNameUpResist.value;
        case 22: return statusNameUpDefense.value;
        case 23: return statusNameDownAction.value;
        case 24: return statusNameDownAvoid.value;
        case 25: return statusNameDownResist.value;
        case 26: return statusNameDownDefense.value;
        case 27: return statusNameNone.value;
        default: throw std::out_of_range("Unknown statusName enum value");
        }
    }

    template <typename Id>
    String effectTypeName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return effectTypeNamePhysic.value;
        case 1: return effectTypeNameMagic.value;
        case 2: return effectTypeNameMagicalPhysic.value;
        case 3: return effectTypeNamePhysicalMagic.value;
        case 4: return effectTypeNameNone.value;
        default: throw std::out_of_range("Unknown effectTypeName enum value");
        }
    }

    template <typename Id>
    String effectTypeDesc(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return effectTypeDescPhysic.value;
        case 1: return effectTypeDescMagic.value;
        case 2: return effectTypeDescMagicalPhysic.value;
        case 3: return effectTypeDescPhysicalMagic.value;
        case 4: return effectTypeDescNone.value;
        default: throw std::out_of_range("Unknown effectTypeDesc enum value");
        }
    }

    template <typename Id>
    String resistName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return resistNameAvoid.value;
        case 1: return resistNameResist.value;
        case 2: return resistNameUnfail.value;
        default: throw std::out_of_range("Unknown resistName enum value");
        }
    }

    template <typename Id>
    String resistDesc(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return resistDescAvoid.value;
        case 1: return resistDescResist.value;
        case 2: return resistDescUnfail.value;
        default: throw std::out_of_range("Unknown resistDesc enum value");
        }
    }

    template <typename Id>
    String cardTargetName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return cardTargetNameNone.value;
        case 1: return cardTargetNameUser.value;
        case 2: return cardTargetNameParty.value;
        case 3: return cardTargetNameEnemy.value;
        case 4: return cardTargetNameBoth.value;
        default: throw std::out_of_range("Unknown cardTargetName enum value");
        }
    }

    template <typename Id>
    String cardVisualName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return cardVisualNameNone.value;
        case 1: return cardVisualNameReverse.value;
        case 2: return cardVisualNameHorizontal.value;
        case 3: return cardVisualNameVertical.value;
        default: throw std::out_of_range("Unknown cardVisualName enum value");
        }
    }

    template <typename Id>
    String premiumName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return premiumNameNormal.value;
        case 1: return premiumNameRare.value;
        case 2: return premiumNamePremium.value;
        default: throw std::out_of_range("Unknown premiumName enum value");
        }
    }

    template <typename Id>
    String enhanceName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return enhanceNameAction.value;
        case 1: return enhanceNameAvoid.value;
        case 2: return enhanceNameResist.value;
        case 3: return enhanceNameDefense.value;
        default: throw std::out_of_range("Unknown enhanceName enum value");
        }
    }

    template <typename Id>
    String mentalityName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return mentalityNameNormal.value;
        case 1: return mentalityNameSleep.value;
        case 2: return mentalityNameConfuse.value;
        case 3: return mentalityNameOverheat.value;
        case 4: return mentalityNameBrave.value;
        case 5: return mentalityNamePanic.value;
        default: throw std::out_of_range("Unknown mentalityName enum value");
        }
    }

    template <typename Id>
    String targetName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return targetNameSelected.value;
        case 1: return targetNameUnselected.value;
        case 2: return targetNameRandom.value;
        case 3: return targetNameParty.value;
        default: throw std::out_of_range("Unknown targetName enum value");
        }
    }

    template <typename Id>
    String talkerName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return talkerNameSelected.value;
        case 1: return talkerNameUnselected.value;
        case 2: return talkerNameRandom.value;
        case 3: return talkerNameCard.value;
        case 4: return talkerNameValued.value;
        default: throw std::out_of_range("Unknown talkerName enum value");
        }
    }

    template <typename Id>
    String rangeName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return rangeNameSelected.value;
        case 1: return rangeNameRandom.value;
        case 2: return rangeNameParty.value;
        case 3: return rangeNameBackpack.value;
        case 4: return rangeNamePartyAndBackpack.value;
        case 5: return rangeNameField.value;
        case 6: return rangeNameCouponHolder.value;
        case 7: return rangeNameCardTarget.value;
        case 8: return rangeNameSelectedCard.value;
        case 9: return rangeNameNpc.value;
        default: throw std::out_of_range("Unknown rangeName enum value");
        }
    }

    template <typename Id>
    String castRangeName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 1: return castRangeNameParty.value;
        case 2: return castRangeNameEnemy.value;
        case 4: return castRangeNameNpc.value;
        default: throw std::out_of_range("Unknown castRangeName enum value");
        }
    }

    template <typename Id>
    String damageTypeName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return damageTypeNameLevelRatio.value;
        case 1: return damageTypeNameNormal.value;
        case 2: return damageTypeNameMax.value;
        case 3: return damageTypeNameFixed.value;
        default: throw std::out_of_range("Unknown damageTypeName enum value");
        }
    }

    template <typename Id>
    String elementName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return elementNameAll.value;
        case 1: return elementNameHealth.value;
        case 2: return elementNameMind.value;
        case 3: return elementNameMiracle.value;
        case 4: return elementNameMagic.value;
        case 5: return elementNameFire.value;
        case 6: return elementNameIce.value;
        default: throw std::out_of_range("Unknown elementName enum value");
        }
    }

    template <typename Id>
    String effectCardTypeName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return effectCardTypeNameAll.value;
        case 1: return effectCardTypeNameSkill.value;
        case 2: return effectCardTypeNameItem.value;
        case 3: return effectCardTypeNameBeast.value;
        case 4: return effectCardTypeNameHand.value;
        default: throw std::out_of_range("Unknown effectCardTypeName enum value");
        }
    }

    template <typename Id>
    String comparison4Name(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return comparison4NameEq.value;
        case 1: return comparison4NameNe.value;
        case 2: return comparison4NameLt.value;
        case 3: return comparison4NameGt.value;
        default: throw std::out_of_range("Unknown comparison4Name enum value");
        }
    }

    template <typename Id>
    String comparison3Name(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return comparison3NameEq.value;
        case 1: return comparison3NameLt.value;
        case 2: return comparison3NameGt.value;
        default: throw std::out_of_range("Unknown comparison3Name enum value");
        }
    }

    template <typename Id>
    String comparison3FalseName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return comparison3FalseNameEq.value;
        case 1: return comparison3FalseNameLt.value;
        case 2: return comparison3FalseNameGt.value;
        default: throw std::out_of_range("Unknown comparison3FalseName enum value");
        }
    }

    template <typename Id>
    String importTypeIncludedName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return importTypeIncludedNameExclude.value;
        case 1: return importTypeIncludedNameInclude.value;
        case 2: return importTypeIncludedNameAsIs.value;
        default: throw std::out_of_range("Unknown importTypeIncludedName enum value");
        }
    }

    template <typename Id>
    String importTypeReference1Name(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return importTypeReference1NameRename.value;
        case 1: return importTypeReference1NameNoOverwrite.value;
        case 2: return importTypeReference1NameOverwrite.value;
        case 3: return importTypeReference1NameNoImport.value;
        default: throw std::out_of_range("Unknown importTypeReference1Name enum value");
        }
    }

    template <typename Id>
    String importTypeReference2Name(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return importTypeReference2NameRename.value;
        case 1: return importTypeReference2NameNoImport.value;
        default: throw std::out_of_range("Unknown importTypeReference2Name enum value");
        }
    }

    template <typename Id>
    String showStatusTimeName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return showStatusTimeNameNo.value;
        case 1: return showStatusTimeNameAlways.value;
        case 2: return showStatusTimeNameWithSkin.value;
        default: throw std::out_of_range("Unknown showStatusTimeName enum value");
        }
    }

    template <typename Id>
    String enhanceLiveBonusName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return enhanceLiveBonusNameAction.value;
        case 1: return enhanceLiveBonusNameAvoid.value;
        case 2: return enhanceLiveBonusNameResist.value;
        case 3: return enhanceLiveBonusNameDefense.value;
        default: throw std::out_of_range("Unknown enhanceLiveBonusName enum value");
        }
    }

    template <typename Id>
    String showStyleName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return showStyleNameInvisible.value;
        case 1: return showStyleNameCenter.value;
        case 2: return showStyleNameFrontOfUser.value;
        default: throw std::out_of_range("Unknown showStyleName enum value");
        }
    }

    template <typename Id>
    String wallpaperStyleName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return wallpaperStyleNameCenter.value;
        case 1: return wallpaperStyleNameTile.value;
        case 2: return wallpaperStyleNameExpand.value;
        case 3: return wallpaperStyleNameExpandFull.value;
        default: throw std::out_of_range("Unknown wallpaperStyleName enum value");
        }
    }

    template <typename Id>
    String dialogStatusName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return dialogStatusNameTop.value;
        case 1: return dialogStatusNameUnder.value;
        case 2: return dialogStatusNameUnderWithCoupon.value;
        default: throw std::out_of_range("Unknown dialogStatusName enum value");
        }
    }

    template <typename Id>
    String menuText(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        switch (value) {
        case 0: return menuTextNone.value;
        case 1: return menuTextFile.value;
        case 2: return menuTextEdit.value;
        case 3: return menuTextView.value;
        case 4: return menuTextTool.value;
        case 5: return menuTextTable.value;
        case 6: return menuTextVariable.value;
        case 7: return menuTextHelp.value;
        case 8: return menuTextCard.value;
        case 9: return menuTextCardsAndBacks.value;
        case 10: return menuTextDelNotUsedFile.value;
        case 11: return menuTextCreateSubWindow.value;
        case 12: return menuTextLeftPane.value;
        case 13: return menuTextRightPane.value;
        case 14: return menuTextClosePane.value;
        case 15: return menuTextClosePaneExcept.value;
        case 16: return menuTextClosePaneLeft.value;
        case 17: return menuTextClosePaneRight.value;
        case 18: return menuTextClosePaneAll.value;
        case 19: return menuTextNew.value;
        case 20: return menuTextOpen.value;
        case 21: return menuTextNewAtNewWindow.value;
        case 22: return menuTextOpenAtNewWindow.value;
        case 23: return menuTextClose.value;
        case 24: return menuTextCloseWin.value;
        case 25: return menuTextSave.value;
        case 26: return menuTextSaveAs.value;
        case 27: return menuTextReload.value;
        case 28: return menuTextEditScenarioHistory.value;
        case 29: return menuTextEditImportHistory.value;
        case 30: return menuTextEditExecutedPartyHistory.value;
        case 31: return menuTextPutParty.value;
        case 32: return menuTextOpenDir.value;
        case 33: return menuTextOpenBackupDir.value;
        case 34: return menuTextOpenPlace.value;
        case 35: return menuTextSaveImage.value;
        case 36: return menuTextIncludeImage.value;
        case 37: return menuTextLookImages.value;
        case 38: return menuTextEditLayers.value;
        case 39: return menuTextAddLayer.value;
        case 40: return menuTextRemoveLayer.value;
        case 41: return menuTextChangeVH.value;
        case 42: return menuTextSelectConnectedResource.value;
        case 43: return menuTextFind.value;
        case 44: return menuTextFindID.value;
        case 45: return menuTextIncSearch.value;
        case 46: return menuTextCloseIncSearch.value;
        case 47: return menuTextEditProp.value;
        case 48: return menuTextShowProp.value;
        case 49: return menuTextRefresh.value;
        case 50: return menuTextUndo.value;
        case 51: return menuTextRedo.value;
        case 52: return menuTextCut.value;
        case 53: return menuTextCopy.value;
        case 54: return menuTextPaste.value;
        case 55: return menuTextDelete.value;
        case 56: return menuTextCut1Content.value;
        case 57: return menuTextCopy1Content.value;
        case 58: return menuTextDelete1Content.value;
        case 59: return menuTextPasteInsert.value;
        case 60: return menuTextClone.value;
        case 61: return menuTextSelectAll.value;
        case 62: return menuTextCopyAll.value;
        case 63: return menuTextToXMLText.value;
        case 64: return menuTextAddItem.value;
        case 65: return menuTextDelItem.value;
        case 66: return menuTextTableView.value;
        case 67: return menuTextVarView.value;
        case 68: return menuTextCardView.value;
        case 69: return menuTextCastView.value;
        case 70: return menuTextSkillView.value;
        case 71: return menuTextItemView.value;
        case 72: return menuTextBeastView.value;
        case 73: return menuTextInfoView.value;
        case 74: return menuTextFileView.value;
        case 75: return menuTextCouponView.value;
        case 76: return menuTextGossipView.value;
        case 77: return menuTextCompleteStampView.value;
        case 78: return menuTextKeyCodeView.value;
        case 79: return menuTextCellNameView.value;
        case 80: return menuTextCardGroupView.value;
        case 81: return menuTextExecEngine.value;
        case 82: return menuTextExecEngineAuto.value;
        case 83: return menuTextExecEngineMain.value;
        case 84: return menuTextExecEngineWithParty.value;
        case 85: return menuTextExecEngineWithLastParty.value;
        case 86: return menuTextDeleteNotExistsParties.value;
        case 87: return menuTextOuterTools.value;
        case 88: return menuTextSettings.value;
        case 89: return menuTextVersionInfo.value;
        case 90: return menuTextLockToolBar.value;
        case 91: return menuTextResetToolBar.value;
        case 92: return menuTextCopyAsText.value;
        case 93: return menuTextOpenAtView.value;
        case 94: return menuTextEventToPackage.value;
        case 95: return menuTextStartToPackage.value;
        case 96: return menuTextWrapTree.value;
        case 97: return menuTextCreateContent.value;
        case 98: return menuTextConvertContent.value;
        case 99: return menuTextCGroupTerminal.value;
        case 100: return menuTextCGroupStandard.value;
        case 101: return menuTextCGroupData.value;
        case 102: return menuTextCGroupUtility.value;
        case 103: return menuTextCGroupBranch.value;
        case 104: return menuTextCGroupGet.value;
        case 105: return menuTextCGroupLost.value;
        case 106: return menuTextCGroupVisual.value;
        case 107: return menuTextCGroupVariant.value;
        case 108: return menuTextEditSummary.value;
        case 109: return menuTextNewAreaDir.value;
        case 110: return menuTextNewArea.value;
        case 111: return menuTextNewBattle.value;
        case 112: return menuTextNewPackage.value;
        case 113: return menuTextReNumberingAll.value;
        case 114: return menuTextReNumbering.value;
        case 115: return menuTextEditScene.value;
        case 116: return menuTextEditSceneDup.value;
        case 117: return menuTextEditEvent.value;
        case 118: return menuTextEditEventDup.value;
        case 119: return menuTextSetStartArea.value;
        case 120: return menuTextNewFlagDir.value;
        case 121: return menuTextNewFlag.value;
        case 122: return menuTextNewStep.value;
        case 123: return menuTextNewVariant.value;
        case 124: return menuTextCreateStepValues.value;
        case 125: return menuTextPutSPChar.value;
        case 126: return menuTextPutFlagValue.value;
        case 127: return menuTextPutStepValue.value;
        case 128: return menuTextPutVariantValue.value;
        case 129: return menuTextPutColor.value;
        case 130: return menuTextPutSkinSPChar.value;
        case 131: return menuTextPutImageFont.value;
        case 132: return menuTextCreateVariableEventTree.value;
        case 133: return menuTextInitVariablesTree.value;
        case 134: return menuTextCopyVariablePath.value;
        case 135: return menuTextUp.value;
        case 136: return menuTextDown.value;
        case 137: return menuTextConvertCouponType.value;
        case 138: return menuTextConvertCouponTypeNormal.value;
        case 139: return menuTextConvertCouponTypeHide.value;
        case 140: return menuTextConvertCouponTypeSystem.value;
        case 141: return menuTextConvertCouponTypeDur.value;
        case 142: return menuTextConvertCouponTypeDurBattle.value;
        case 143: return menuTextCopyTypeConvertedCoupon.value;
        case 144: return menuTextCopyTypeConvertedCouponNormal.value;
        case 145: return menuTextCopyTypeConvertedCouponHide.value;
        case 146: return menuTextCopyTypeConvertedCouponSystem.value;
        case 147: return menuTextCopyTypeConvertedCouponDur.value;
        case 148: return menuTextCopyTypeConvertedCouponDurBattle.value;
        case 149: return menuTextAddInitialCoupons.value;
        case 150: return menuTextReverseSignOfValues.value;
        case 151: return menuTextReverse.value;
        case 152: return menuTextSwapToParent.value;
        case 153: return menuTextSwapToChild.value;
        case 154: return menuTextOverDialog.value;
        case 155: return menuTextUnderDialog.value;
        case 156: return menuTextCreateDialog.value;
        case 157: return menuTextDeleteDialog.value;
        case 158: return menuTextCopyToAllDialogs.value;
        case 159: return menuTextCopyToUpperDialogs.value;
        case 160: return menuTextCopyToLowerDialogs.value;
        case 161: return menuTextShowParty.value;
        case 162: return menuTextShowMsg.value;
        case 163: return menuTextShowRefCards.value;
        case 164: return menuTextFixedCards.value;
        case 165: return menuTextFixedCells.value;
        case 166: return menuTextFixedBackground.value;
        case 167: return menuTextShowGrid.value;
        case 168: return menuTextShowEnemyCardProp.value;
        case 169: return menuTextShowCard.value;
        case 170: return menuTextShowBack.value;
        case 171: return menuTextNewMenuCard.value;
        case 172: return menuTextNewEnemyCard.value;
        case 173: return menuTextNewBack.value;
        case 174: return menuTextNewTextCell.value;
        case 175: return menuTextNewColorCell.value;
        case 176: return menuTextNewPCCell.value;
        case 177: return menuTextAutoArrange.value;
        case 178: return menuTextManualArrange.value;
        case 179: return menuTextPossibleToRunAway.value;
        case 180: return menuTextSortWithPosition.value;
        case 181: return menuTextMask.value;
        case 182: return menuTextEscape.value;
        case 183: return menuTextChangePos.value;
        case 184: return menuTextPosTop.value;
        case 185: return menuTextPosBottom.value;
        case 186: return menuTextPosLeft.value;
        case 187: return menuTextPosRight.value;
        case 188: return menuTextPosEven.value;
        case 189: return menuTextNearTop.value;
        case 190: return menuTextNearBottom.value;
        case 191: return menuTextNearLeft.value;
        case 192: return menuTextNearRight.value;
        case 193: return menuTextNearCenterH.value;
        case 194: return menuTextNearCenterV.value;
        case 195: return menuTextNearCenter.value;
        case 196: return menuTextScaleMin.value;
        case 197: return menuTextScaleMiddle.value;
        case 198: return menuTextScaleMax.value;
        case 199: return menuTextScaleBig.value;
        case 200: return menuTextScaleSmall.value;
        case 201: return menuTextExpandBack.value;
        case 202: return menuTextCopyColor1ToColor2.value;
        case 203: return menuTextCopyColor2ToColor1.value;
        case 204: return menuTextExchangeColors.value;
        case 205: return menuTextStopBGM.value;
        case 206: return menuTextPlayBGM.value;
        case 207: return menuTextNewEvent.value;
        case 208: return menuTextNewEventWithDialog.value;
        case 209: return menuTextKeyCodeTiming.value;
        case 210: return menuTextKeyCodeTimingUse.value;
        case 211: return menuTextKeyCodeTimingSuccess.value;
        case 212: return menuTextKeyCodeTimingFailure.value;
        case 213: return menuTextKeyCodeTimingHasNot.value;
        case 214: return menuTextCopyTimingConvertedKeyCode.value;
        case 215: return menuTextCopyTimingConvertedKeyCodeUse.value;
        case 216: return menuTextCopyTimingConvertedKeyCodeSuccess.value;
        case 217: return menuTextCopyTimingConvertedKeyCodeFailure.value;
        case 218: return menuTextCopyTimingConvertedKeyCodeHasNot.value;
        case 219: return menuTextAddKeyCodesByFeatures.value;
        case 220: return menuTextKeyCodeCond.value;
        case 221: return menuTextKeyCodeCondOr.value;
        case 222: return menuTextKeyCodeCondAnd.value;
        case 223: return menuTextAddRangeOfRound.value;
        case 224: return menuTextOpenAtTableView.value;
        case 225: return menuTextOpenAtVarView.value;
        case 226: return menuTextOpenAtCardView.value;
        case 227: return menuTextOpenAtFileView.value;
        case 228: return menuTextOpenAtEventView.value;
        case 229: return menuTextComment.value;
        case 230: return menuTextShowCardProp.value;
        case 231: return menuTextShowCardImage.value;
        case 232: return menuTextShowCardDetail.value;
        case 233: return menuTextOpenImportSource.value;
        case 234: return menuTextSelectImportSource.value;
        case 235: return menuTextNewCast.value;
        case 236: return menuTextNewSkill.value;
        case 237: return menuTextNewItem.value;
        case 238: return menuTextNewBeast.value;
        case 239: return menuTextNewInfo.value;
        case 240: return menuTextImport.value;
        case 241: return menuTextOpenHand.value;
        case 242: return menuTextAddHand.value;
        case 243: return menuTextRemoveRef.value;
        case 244: return menuTextEditEventAtTimeOfUsing.value;
        case 245: return menuTextHold.value;
        case 246: return menuTextPlaySE.value;
        case 247: return menuTextStopSE.value;
        case 248: return menuTextNewDir.value;
        case 249: return menuTextCopyFilePath.value;
        case 250: return menuTextCreateArchive.value;
        case 251: return menuTextPutQuick.value;
        case 252: return menuTextPutSelect.value;
        case 253: return menuTextPutContinue.value;
        case 254: return menuTextToScript.value;
        case 255: return menuTextToScriptAll.value;
        case 256: return menuTextToScript1Content.value;
        case 257: return menuTextEvTemplates.value;
        case 258: return menuTextEvTemplatesOfScenario.value;
        case 259: return menuTextEditSelection.value;
        case 260: return menuTextExpand.value;
        case 261: return menuTextCollapse.value;
        case 262: return menuTextSelectCurrentEvent.value;
        case 263: return menuTextResetValues.value;
        case 264: return menuTextResetValuesAll.value;
        case 265: return menuTextCustomizeToolBar.value;
        case 266: return menuTextAddTool.value;
        case 267: return menuTextAddToolBar.value;
        case 268: return menuTextAddToolGroup.value;
        case 269: return menuTextResetToolBarSettings.value;
        case 270: return menuTextDeleteNotExistsHistory.value;
        case 271: return menuTextSelectIcon.value;
        case 272: return menuTextSelectPresetIcon.value;
        case 273: return menuTextDeleteIcon.value;
        default: throw std::out_of_range("Unknown menuText enum value");
        }
    }

    template <typename Id>
    String variableInitializationName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        if (value == 0) return variableInitializationNameNone.value;
        if (value == 1) return variableInitializationNameLeave.value;
        String result;
        if ((value & 4) != 0) result = detail::concat(result, result.empty() ? "" : " ", variableInitializationNameBattleStart.value);
        if ((value & 8) != 0) result = detail::concat(result, result.empty() ? "" : " ", variableInitializationNameBattleEnd.value);
        if ((value & 2) != 0) result = detail::concat(result, result.empty() ? "" : " ", variableInitializationNameEventExit.value);
        if ((value & 1) != 0) result = detail::concat(result, result.empty() ? "" : " ", variableInitializationNameLeave.value);
        return result.empty() ? variableInitializationNameLeave.value : result;
    }

    template <typename Id>
    String variableInitializationShortName(Id id) const {
        const auto value = static_cast<std::int64_t>(id);
        if (value == 0) return variableInitializationShortNameNone.value;
        if (value == 1) return variableInitializationShortNameLeave.value;
        String result;
        if ((value & 4) != 0) result = detail::concat(result, result.empty() ? "" : " ", variableInitializationShortNameBattleStart.value);
        if ((value & 8) != 0) result = detail::concat(result, result.empty() ? "" : " ", variableInitializationShortNameBattleEnd.value);
        if ((value & 2) != 0) result = detail::concat(result, result.empty() ? "" : " ", variableInitializationShortNameEventExit.value);
        if ((value & 1) != 0) result = detail::concat(result, result.empty() ? "" : " ", variableInitializationShortNameLeave.value);
        return result.empty() ? variableInitializationShortNameLeave.value : result;
    }
};

#undef CWX_DECLARE_AA
#undef CWX_DECLARE_ATTRIBUTE
#undef CWX_DECLARE_MESSAGE
#undef CWX_AA_FIELDS
#undef CWX_ATTRIBUTE_FIELDS
#undef CWX_MESSAGE_FIELDS

}  // namespace cwxeditor::cwx::msgs
