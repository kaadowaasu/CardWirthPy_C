#pragma once

// cwx/warning.dのC++20移
// 植。シナリオ構造の共通診断を提供する。
// 各ノード型は複数の相互参照するモジュール
// にあるため、ここでは所有しない前方宣言に
// とどめる。 detail::PathInspector
// を通じて、Dのwarningsの型別
// 処理を各モデルから受け取る。 このモ
// ジュールの具体的なインターフェースだ
// けで行える診断は、ここで実装する。

#include "cwxeditor_src/core/cwx/expression.hpp"
#include "cwxeditor_src/core/cwx/msgutils.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/sjis.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <functional>
#include <span>

namespace cwxeditor::cwx::summary {
class Summary;
}  // namespace cwxeditor::cwx::summary

namespace cwxeditor::cwx::event {
class Content;
}  // namespace cwxeditor::cwx::event

namespace cwxeditor::cwx::skin {
class Skin;
}  // namespace cwxeditor::cwx::skin

namespace cwxeditor::cwx::warning {

// 型付きのイベント編集ではDの項目別警告一覧を使う。 シナリ
// オ全体の検証では、対象が空の場合の追加の検索診断も残す。
enum class WarningDomain {
    ScenarioValidation,
    TypedEventDialog,
};

using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;
using WarningList = ::cwxeditor::lib::Array<String>;
using CProps = ::cwxeditor::cwx::props::CProps;
using Skin = ::cwxeditor::cwx::skin::Skin;
using Summary = ::cwxeditor::cwx::summary::Summary;
using SummaryOwner = ::cwxeditor::lib::Owner<Summary>;
using CWXPath = ::cwxeditor::cwx::path::CWXPath;
using UseCounter = ::cwxeditor::cwx::usecounter::UseCounter;
using Content = ::cwxeditor::cwx::event::Content;
using ExprError = ::cwxeditor::cwx::expression::ExprError;
using CNextType = ::cwxeditor::cwx::types::CNextType;
using Status = ::cwxeditor::cwx::types::Status;

struct TextWarnings {
    WarningList all;
    WarningList noDup;
};

namespace detail {

/// 文字列・SJIS検査に必要な小さなスナップショット。Summary
/// は借用する。 nullまたは未接続のSummary
/// に対しては、空のスナップショットを返せる。
struct SummaryInfo {
    String scenarioPath;
    bool legacy = false;
};

enum class VariableKind {
    Flag,
    Step,
    Variant,
};

/// 具体的なモデルが揃った時点で、event/card/summary
/// 側から渡すコールバック。 warning.d::warnings
/// の大きな型別分岐に対応する。 受け渡しを値に限定し、この
/// モジュールをまたぐ所有権・インクルードの循環を避ける。
using PathInspector = ::cwxeditor::lib::Callback<WarningList(
    const CProps&, const Skin&, const Summary*, const CWXPath&, bool,
    StringView, StringView)>;
using DomainPathInspector = ::cwxeditor::lib::Callback<WarningList(
    const CProps&, const Skin&, const Summary*, const CWXPath&, bool,
    StringView, StringView, WarningDomain)>;

/// textWarningsで使う変数検索。Dはシナリオのフラ
/// グ階層と参照数の構造を調べる。 検索をコールバックにし、warning.hpp
/// がevent/card全体を読み込まずに、 変数
/// が存在しない場合の同じ診断を行えるようにする。
using VariableFinder = ::cwxeditor::lib::Callback<bool(
    const Summary*, const UseCounter*, VariableKind, StringView)>;

using SummaryInfoProvider =
    ::cwxeditor::lib::Callback<SummaryInfo(const Summary*)>;
using CharacterTalkerInspector =
    ::cwxeditor::lib::Callback<bool(const Content&)>;
using MessageProvider = ::cwxeditor::lib::Callback<String(
    const ::cwxeditor::cwx::props::Msgs&, StringView)>;
using SkinPathFinder = ::cwxeditor::lib::Callback<bool(
    const Skin&, StringView, StringView, StringView, StringView)>;
using SkinSpecialCharacterFinder = ::cwxeditor::lib::Callback<bool(
    const Skin&, char32_t)>;
using SkinImageWarningProvider = ::cwxeditor::lib::Callback<WarningList(
    const CProps&, const Skin&, StringView, bool, bool, StringView)>;

/// モデルとの接続を登録する。空のコールバックなら共通の代替動作へ
/// 戻り、 モデル固有の警告を出さず、変数検索は未発見として扱う。
void setPathInspector(PathInspector inspector);
void setDomainPathInspector(DomainPathInspector inspector);
void setVariableFinder(VariableFinder finder);
void setSummaryInfoProvider(SummaryInfoProvider provider);
void setCharacterTalkerInspector(CharacterTalkerInspector inspector);
void setMessageProvider(MessageProvider provider);
void setSkinPathFinder(SkinPathFinder finder);
void setSkinSpecialCharacterFinder(SkinSpecialCharacterFinder finder);
void setSkinImageWarningProvider(SkinImageWarningProvider provider);

[[nodiscard]] WarningList inspectPath(const CProps& prop, const Skin& skin,
                                      const Summary* summ,
                                      const CWXPath& path, bool isClassic,
                                      StringView wsnVer, StringView targVer);
[[nodiscard]] WarningList inspectPath(const CProps& prop, const Skin& skin,
                                      const Summary* summ,
                                      const CWXPath& path, bool isClassic,
                                      StringView wsnVer, StringView targVer,
                                      WarningDomain domain);
[[nodiscard]] bool findVariable(const Summary* summ, const UseCounter* uc,
                                VariableKind kind, StringView name);
[[nodiscard]] SummaryInfo summaryInfo(const Summary* summ);
[[nodiscard]] bool inspectCharacterTalker(const Content& evt);
[[nodiscard]] bool skinPathFound(const Skin& skin, StringView path,
                                 StringView scenarioPath, StringView wsnVer,
                                 StringView kind);
[[nodiscard]] bool skinHasSpecialCharacter(const Skin& skin, char32_t c);
[[nodiscard]] WarningList skinImageWarnings(const CProps& prop,
                                            const Skin& skin, StringView path,
                                            bool isClassic, bool includeType,
                                            StringView targVer);

/// Dのフィールドキーから翻訳済みメッセージを一つ取得する。msgs::Msgs
/// の項目は公開されているが、 この検索で生成されたフィー
/// ルド名への依存を避け、読み込んだ言語設定を反映する。
[[nodiscard]] String message(const ::cwxeditor::cwx::props::Msgs& msgs,
                             StringView key);
[[nodiscard]] String format(const ::cwxeditor::cwx::props::Msgs& msgs,
                            StringView key, StringView value);
[[nodiscard]] String format(const ::cwxeditor::cwx::props::Msgs& msgs,
                            StringView key, StringView value1,
                            StringView value2);

}  // namespace detail

/// パスを検査し、該当する警告をすべて返す。 summはDのSummary
/// 参照と同じ条件でnullを許し、pathは借用する。
[[nodiscard]] WarningList warnings(const CProps& prop, const Skin& skin,
                                   const Summary* summ, const CWXPath& path,
                                   bool isClassic, StringView wsnVer,
                                   StringView targVer);
[[nodiscard]] WarningList warnings(const CProps& prop, const Skin& skin,
                                   const Summary* summ, const CWXPath& path,
                                   bool isClassic, StringView wsnVer,
                                   StringView targVer, WarningDomain domain);
[[nodiscard]] WarningList warnings(const CProps& prop, const Skin& skin,
                                   const SummaryOwner& summ, const CWXPath& path,
                                   bool isClassic, StringView wsnVer,
                                   StringView targVer);
[[nodiscard]] WarningList warnings(const CProps& prop, const Skin& skin,
                                   const SummaryOwner& summ, const CWXPath& path,
                                   bool isClassic, StringView wsnVer,
                                   StringView targVer, WarningDomain domain);

[[nodiscard]] WarningList exprErrorToWarnings(
    const CProps& prop, StringView expr,
    const ::cwxeditor::lib::Array<ExprError>& exprErrs);

[[nodiscard]] bool hasCharacterTalker(const Content& evt);

[[nodiscard]] TextWarnings textWarnings(
    const CProps& prop, const Skin& skin, const Summary* summ,
    const UseCounter* uc, bool isClassic, StringView wsnVer,
    StringView targVer, StringView text, StringView flags,
    StringView steps, StringView variants, StringView fonts,
    StringView colors, ::cwxeditor::lib::Dictionary<String, bool>& wFlags,
    ::cwxeditor::lib::Dictionary<String, bool>& wSteps,
    ::cwxeditor::lib::Dictionary<String, bool>& wVariants,
    ::cwxeditor::lib::Dictionary<String, bool>& wFonts,
    ::cwxeditor::lib::Dictionary<char, bool>& wColors);
[[nodiscard]] TextWarnings textWarnings(
    const CProps& prop, const Skin& skin, const SummaryOwner& summ,
    const UseCounter* uc, bool isClassic, StringView wsnVer,
    StringView targVer, StringView text,
    std::span<const String> flags, std::span<const String> steps,
    std::span<const String> variants, std::span<const String> fonts,
    std::span<const char> colors,
    ::cwxeditor::lib::Dictionary<String, bool>& wFlags,
    ::cwxeditor::lib::Dictionary<String, bool>& wSteps,
    ::cwxeditor::lib::Dictionary<String, bool>& wVariants,
    ::cwxeditor::lib::Dictionary<String, bool>& wFonts,
    ::cwxeditor::lib::Dictionary<char, bool>& wColors);

/// Dのstring[]/char[] APIに対応する配列版。単
/// 一メッセージには上のStringView版を使える。 既に配列
/// を抽出した呼び出し元には、この形式を正式なC++表現とする。
[[nodiscard]] TextWarnings textWarnings(
    const CProps& prop, const Skin& skin, const Summary* summ,
    const UseCounter* uc, bool isClassic, StringView wsnVer,
    StringView targVer, StringView text,
    std::span<const String> flags, std::span<const String> steps,
    std::span<const String> variants, std::span<const String> fonts,
    std::span<const char> colors,
    ::cwxeditor::lib::Dictionary<String, bool>& wFlags,
    ::cwxeditor::lib::Dictionary<String, bool>& wSteps,
    ::cwxeditor::lib::Dictionary<String, bool>& wVariants,
    ::cwxeditor::lib::Dictionary<String, bool>& wFonts,
    ::cwxeditor::lib::Dictionary<char, bool>& wColors);

[[nodiscard]] WarningList couponWarnings(const CProps& prop,
                                         bool isClassic, StringView wsnVer,
                                         StringView targVer, StringView coupon,
                                         bool getLose, StringView itemName);

[[nodiscard]] String warningNextCondition(const CProps& prop,
                                          CNextType cNextType,
                                          StringView name);
[[nodiscard]] String warningBoolCondition(const CProps& prop,
                                          StringView text);
[[nodiscard]] String warningNumberCondition(const CProps& prop,
                                            StringView text);
[[nodiscard]] String warningTrioCondition(const CProps& prop,
                                          StringView text);
[[nodiscard]] WarningList warningInconsistency(
    const CProps& prop, std::span<const Status> statuses);
[[nodiscard]] WarningList fileExtensionWarnings(const CProps& prop,
                                                 StringView filePath);
[[nodiscard]] WarningList sjisWarnings(const CProps& prop,
                                       const Summary* summ, StringView text,
                                       StringView name);
[[nodiscard]] WarningList sjisWarnings(const CProps& prop,
                                       const SummaryOwner& summ, StringView text,
                                       StringView name);
[[nodiscard]] WarningList sjisWarnings(const CProps& prop, bool legacy,
                                       StringView text, StringView name);

}  // namespace cwxeditor::cwx::warning
