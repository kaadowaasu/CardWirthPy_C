#pragma once

// cwx/props.dのC++20
// 移植。MsgsとSummaryは前
// 方宣言にとどめ、 下の小さなアクセ
// ス用インターフェースで参照する。
// 生成済みの互換ヘッダーに依存せ
// ず、D版の所有関係と動作を保つ。

#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/shared_dictionary.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>

namespace cwxeditor::cwx::skin { class Skin; }

namespace cwxeditor::cwx::msgs {
class Msgs;
}  // namespace cwxeditor::cwx::msgs

namespace cwxeditor::cwx::summary {
class Summary;
}  // namespace cwxeditor::cwx::summary

namespace cwxeditor::cwx::props {

using String = lib::String;
using StringView = lib::StringView;
using Msgs = msgs::Msgs;
using Summary = summary::Summary;
using CPoint = structs::CPoint;
using CSize = structs::CSize;
using CRect = structs::CRect;
using CInsets = structs::CInsets;
using CRGB = structs::CRGB;
using CFont = structs::CFont;

namespace detail {

// これらは具体的なmsgs/summaryモジュール
// 、またはそれらを所有する統合層で実装する。 必要な
// 操作だけを公開し、props.cppが未完成の生成
// ヘッダーを読み込まずにコンパイルできるようにする。
[[nodiscard]] lib::Owner<Msgs> makeDefaultMsgs();
[[nodiscard]] lib::Owner<Msgs> msgsFromXML(StringView xml,
                                             std::uint64_t dataVersion);
[[nodiscard]] String msgsLocale(const Msgs& msgs);
[[nodiscard]] bool summaryIsTargetVersion(const Summary& summary,
                                           StringView version);
[[nodiscard]] bool summaryIsTargetVersionWith(bool legacy,
                                               StringView dataVersion,
                                               StringView version);

}  // namespace detail

class Looks {
public:
    [[nodiscard]] lib::Array<String> fontFiles() const;
    [[nodiscard]] CPoint castCardNamePoint() const;
    [[nodiscard]] CPoint menuCardNamePoint() const;
    [[nodiscard]] CPoint cardNamePoint() const;
    [[nodiscard]] CSize cardSize() const;
    [[nodiscard]] CSize summarySize() const;
    [[nodiscard]] CPoint summaryImageXY() const;
    [[nodiscard]] std::int32_t summaryLevelY() const;
    [[nodiscard]] std::int32_t summaryTitleY() const;
    [[nodiscard]] CPoint summaryDescXY() const;
    [[nodiscard]] std::int32_t summaryDescLen() const;
    [[nodiscard]] std::int32_t summaryDescLine() const;
    [[nodiscard]] std::int32_t summaryPageY() const;
    [[nodiscard]] CRGB summaryLevelColor() const;
    [[nodiscard]] CInsets castCardInsets() const;
    [[nodiscard]] CInsets menuCardInsets() const;
    [[nodiscard]] CInsets cardInsets() const;
    [[nodiscard]] lib::Array<CPoint> partyCardXY() const;
    [[nodiscard]] std::size_t partyMax() const;
    [[nodiscard]] CRect cardNameArea() const;
    [[nodiscard]] CRect castCardNameArea() const;
    [[nodiscard]] CRect messageBounds() const;
    [[nodiscard]] CRect singleLineMessageBounds() const;
    [[nodiscard]] std::int32_t messageButtonHeight() const;
    [[nodiscard]] CPoint messageStartPos(bool legacy, bool withTalker,
                                         bool centerX) const;
    [[nodiscard]] std::uint32_t messageLineHeight() const;
    [[nodiscard]] std::uint32_t messageCharWidth() const;
    [[nodiscard]] CPoint messageTalkerPos() const;
    [[nodiscard]] std::uint32_t selectionBarMaxWithMessage() const;
    [[nodiscard]] std::uint32_t selectionBarMax() const;
    [[nodiscard]] std::int32_t aptVeryHigh() const;
    [[nodiscard]] std::int32_t aptHigh() const;
    [[nodiscard]] std::int32_t aptNormal() const;
    [[nodiscard]] CPoint handXY() const;
    [[nodiscard]] CPoint handXYWithLifeBar() const;
    [[nodiscard]] CPoint useStoneXY() const;
    [[nodiscard]] CPoint aptStoneXY() const;
    [[nodiscard]] CPoint eventTreeXY() const;
    [[nodiscard]] CPoint eventTreeXYWithCount() const;
    [[nodiscard]] CPoint premiumXY() const;
    [[nodiscard]] std::uint32_t itemCardMaxNum(std::uint32_t lev) const;
    [[nodiscard]] std::uint32_t skillCardMaxNum(std::uint32_t lev) const;
    [[nodiscard]] std::uint32_t beastCardMaxNum(std::uint32_t lev) const;
    [[nodiscard]] std::int32_t cardDescLen() const;
    [[nodiscard]] std::int32_t cardDescLine(bool classic) const;
    [[nodiscard]] std::int32_t messageImageLen() const;
    [[nodiscard]] std::int32_t messageLen() const;
    [[nodiscard]] std::int32_t messageLine() const;
    [[nodiscard]] String openChars() const;
    [[nodiscard]] String closeChars() const;
    [[nodiscard]] String wordRegex() const;
    [[nodiscard]] std::int32_t stepMaxCount() const;
    [[nodiscard]] std::uint32_t castNameLimit() const;
    [[nodiscard]] std::uint32_t nameLimit() const;
    [[nodiscard]] std::uint32_t lifeCalc(std::uint32_t lev,
                                         std::uint32_t vit,
                                         std::uint32_t spi) const;
    [[nodiscard]] std::uint32_t physicalCutMin() const;
    [[nodiscard]] std::uint32_t physicalCutMaxBase() const;
    [[nodiscard]] std::uint32_t physicalNormal() const;
    [[nodiscard]] lib::Array<std::uint32_t> physicalBorders() const;
    [[nodiscard]] std::uint32_t mentalCut() const;
    [[nodiscard]] lib::Array<std::uint32_t> mentalBorders() const;
    [[nodiscard]] std::int32_t skillPrice(std::int32_t lev) const;
    [[nodiscard]] std::int32_t beastPrice() const;
    [[nodiscard]] std::int32_t keyCodesMaxLegacy() const;
    [[nodiscard]] std::uint32_t motionRoundDefault() const;
    [[nodiscard]] std::uint32_t stoneBorder() const;
    [[nodiscard]] std::uint32_t idMax() const;
    [[nodiscard]] CSize viewSize() const;
    [[nodiscard]] std::uint32_t partyTop() const;
    [[nodiscard]] String alternativeFont(String face) const;
    [[nodiscard]] String monospace() const;

    [[nodiscard]] static String gothic(bool legacy);
    [[nodiscard]] static String pgothic(bool legacy);
    [[nodiscard]] static String mincho(bool legacy);
    [[nodiscard]] static String pmincho(bool legacy);
    [[nodiscard]] static String uigothic(bool legacy);

    [[nodiscard]] CFont textDlgFont(std::uint32_t defSize) const;
    [[nodiscard]] CFont castCardNameFont(bool legacy) const;
    [[nodiscard]] CFont castCardLevelFont(bool legacy) const;
    [[nodiscard]] CFont pcNumberFont(bool legacy) const;
    [[nodiscard]] CInsets castCardLevelInsets() const;
    [[nodiscard]] CRGB castCardLevelColor() const;
    [[nodiscard]] CPoint castLifeBarPoint() const;
    [[nodiscard]] std::int32_t statusX() const;
    [[nodiscard]] std::uint32_t statusVerMax() const;
    [[nodiscard]] CFont statusTimeFont(bool legacy, std::uint32_t number) const;
    [[nodiscard]] CFont skillCardLevelFont(bool legacy) const;
    [[nodiscard]] CRGB skillCardLevelColor() const;
    [[nodiscard]] CInsets skillCardLevelInsets() const;
    [[nodiscard]] CFont menuCardNameFont(bool legacy) const;
    [[nodiscard]] CFont cardNameFont(bool legacy) const;
    [[nodiscard]] CFont useCountFont(bool legacy) const;
    [[nodiscard]] CPoint useCountPoint() const;
    [[nodiscard]] CRGB recycleNumColor() const;
    [[nodiscard]] CFont summaryLevelFont(bool legacy) const;
    [[nodiscard]] CFont summaryTitleFont(bool legacy) const;
    [[nodiscard]] CFont summaryDescFont(bool legacy) const;
    [[nodiscard]] std::uint32_t summaryDescLineHeightClassic() const;
    [[nodiscard]] CFont summaryPageFont(bool legacy) const;
    [[nodiscard]] CFont cardDescFont(bool legacy) const;
    [[nodiscard]] CFont messageFont(bool legacy) const;
    [[nodiscard]] CFont messageSelectFont(bool legacy) const;
    [[nodiscard]] CRGB pcCellForeColor() const;
    [[nodiscard]] CRGB pcCellBackColor() const;
    [[nodiscard]] CFont scriptErrorFont(std::uint32_t defSize) const;
};

namespace detail {
// ネイティブのCPropsはスタック上にも置けるため、SkinはCProps
// 自体ではなく、 ここで読む依存先を保持する。現在のSystem
// は状態を持たないので、 所有する値コピーでも同じ動作を保てる。
struct SkinContext {
    explicit SkinContext(const system::System& source)
        : system(std::make_shared<const system::System>(source)) {}

    lib::Owner<const system::System> system;
    Looks looks;
};

// 寿命を確認するテスト用のアサーションを、CPropsの公開APIから分離する。
struct SkinContextTestAccess;
}  // namespace detail

class CProps {
public:
    CProps(String appPath, const system::System& sys);
    ~CProps();

    CProps(const CProps&) = delete;
    CProps& operator=(const CProps&) = delete;
    CProps(CProps&&) = delete;
    CProps& operator=(CProps&&) = delete;

    /// アプリケーションの実行ファイルのパス。
    [[nodiscard]] String appPath() const;
    // ALLは外部のEngine実行ファイルとは独立して、UserData
    // のスキンを提供する。 空なら従来の単独起動Engineの探索を行う。
    void defaultSkinPath(String value);
    [[nodiscard]] const String& defaultSkinPath() const noexcept { return defaultSkinPath_; }
    /// CardWirthのシステム情報。CPropsはシステムを所有しない。
    [[nodiscard]] const system::System& sys() const noexcept;
    /// 各種メッセージ情報。
    [[nodiscard]] lib::Owner<const Msgs> msgs() const noexcept;
    void msgs(lib::Owner<Msgs> value);
    /// 明示的に必要とする呼び出し元へ、現在のカタログを貸し出す。 置き換
    /// えをまたいで保持するには、msgs()が返すOwnerを保持する。
    [[nodiscard]] const Msgs& requiredMsgs() const;
    /// 各種外観情報。
    [[nodiscard]] const Looks& looks() const noexcept;

    /// pathをアプリケーションの実行ファイルからの相対パスと見做して
    /// フルパスに変換する。
    [[nodiscard]] String toAppAbs(StringView path) const;
    /// 言語ファイルを読み込む。
    void loadMsgs(StringView relPath);
    /// 言語ファイルの一覧を得る。
    [[nodiscard]] lib::Dictionary<String, lib::Owner<Msgs>>
    msgsTable(StringView languageDir,
              lib::Dictionary<String, String>& msgsTableFile,
              String& defLocale) const;

    /// verがターゲットとなる環境のバージョンtargVer以下であればtrue。
    [[nodiscard]] bool targetVersion(StringView ver,
                                     StringView targVer) const noexcept;
    /// summが存在する場合はwsnVer以上かを返し、それ以外は
    /// 対象バージョンがCardWirthPyか否かを返す。
    [[nodiscard]] bool isTargetVersion(const lib::Owner<Summary>& summ,
                                       StringView targVer,
                                       StringView wsnVer) const;
    [[nodiscard]] bool isTargetVersion(bool legacy,
                                       StringView dataVersion,
                                       StringView wsnVer) const;

private:
    friend class ::cwxeditor::cwx::skin::Skin;
    // コンテキストを公開せず、解放を観測するための限定したテスト用の入口。
    friend struct detail::SkinContextTestAccess;
    const system::System& sys_;
    lib::Owner<Msgs> msgs_;
    lib::Owner<detail::SkinContext> skin_context_;
    String appPath_;
    String defaultSkinPath_;
    // 埋め込み時の探索設定はこのセッションに限定する。 CPropsの寿命を超え
    // てSkinを保持する場合は、Skinが共有コンテキストを独立して保持する。
    mutable std::mutex skin_tables_mutex_;
    mutable std::map<std::pair<String, String>,
        lib::SharedDictionary<String, lib::Owner<::cwxeditor::cwx::skin::Skin>>> skin_tables_;
    mutable std::map<String,
        lib::Owner<::cwxeditor::cwx::skin::Skin>> empty_skins_;
};

}  // namespace cwxeditor::cwx::props
