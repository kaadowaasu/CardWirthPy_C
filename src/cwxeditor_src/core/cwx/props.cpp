#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"

#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <filesystem>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace cwxeditor::cwx::props {
namespace {

// Dの言語別メッセージ一覧はreadText（UTF-8）を使う。Shift-JIS
// 優先の契約を持つファイル形式向けの共通読み込み処理は変更しない。
String readCatalogUtf8(const utils::Path& path) {
    const auto bytes = utils::readBinary(path);
    String text(bytes.begin(), bytes.end());
    enumutils::validateUtf8(text);
    return text;
}

std::uint32_t cardCapacity(std::uint32_t level, std::uint32_t divisor,
                           std::uint32_t extra) {
    // Dはuintで計算し、ビット列をintとして再解釈してから符号付きの上限
    // と比較する。ここではC++の符号付きオーバーフローや縮小変換を避ける。
    const std::uint32_t bits = (level + 1U) / divisor + extra;
    const auto result = std::bit_cast<std::int32_t>(bits);
    return result <= 10 ? static_cast<std::uint32_t>(result) : 10U;
}

String pathToUtf8(const utils::Path& value) {
    const auto text = value.u8string();
    return String(reinterpret_cast<const char*>(text.data()), text.size());
}

utils::Path fsPath(const StringView value) {
    if (value.empty()) return {};
#if defined(_WIN32)
    const auto* first = reinterpret_cast<const char8_t*>(value.data());
    return utils::Path(std::u8string(first, first + value.size()));
#else
    return utils::Path(String(value));
#endif
}

CFont makeFont(String name, const std::uint32_t point, const bool bold,
               const bool italic) {
    return CFont{std::move(name), point, bold, italic};
}

}  // namespace

lib::Array<String> Looks::fontFiles() const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d19(__FILE__, __LINE__);
    const String separator(1, std::filesystem::path::preferred_separator);
    return {
        "Data" + separator + "Font" + separator + "gothic.ttf",
        "Data" + separator + "Font" + separator + "mincho.ttf",
        "Data" + separator + "Font" + separator + "uigothic.ttf",
    };
}

CPoint Looks::castCardNamePoint() const { return CPoint{5, 5}; }
CPoint Looks::menuCardNamePoint() const { return CPoint{5, 5}; }
CPoint Looks::cardNamePoint() const { return CPoint{5, 5}; }
CSize Looks::cardSize() const { return CSize{74, 94}; }
CSize Looks::summarySize() const { return CSize{400, 370}; }
CPoint Looks::summaryImageXY() const { return CPoint{163, 70}; }
std::int32_t Looks::summaryLevelY() const { return 15; }
std::int32_t Looks::summaryTitleY() const { return 35; }
CPoint Looks::summaryDescXY() const { return CPoint{65, 180}; }
std::int32_t Looks::summaryDescLen() const { return 40; }
std::int32_t Looks::summaryDescLine() const { return 11; }
std::int32_t Looks::summaryPageY() const { return 342; }
CRGB Looks::summaryLevelColor() const { return CRGB{32, 128, 128}; }
CInsets Looks::castCardInsets() const { return CInsets{18, 11, 18, 10}; }
CInsets Looks::menuCardInsets() const { return CInsets{13, 3, 3, 3}; }
CInsets Looks::cardInsets() const { return menuCardInsets(); }

lib::Array<CPoint> Looks::partyCardXY() const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d42(__FILE__, __LINE__);
    return {
        CPoint{9, 285},   CPoint{113, 285}, CPoint{217, 285},
        CPoint{321, 285}, CPoint{425, 285}, CPoint{529, 285},
    };
}

std::size_t Looks::partyMax() const { return 6; }
CRect Looks::cardNameArea() const { return CRect{5, 5, 70, 15}; }
CRect Looks::castCardNameArea() const { return CRect{5, 5, 85, 15}; }
CRect Looks::messageBounds() const { return CRect{81, 50, 470, 180}; }
CRect Looks::singleLineMessageBounds() const { return CRect{81, 50, 470, 40}; }
std::int32_t Looks::messageButtonHeight() const { return 25; }

CPoint Looks::messageStartPos(const bool legacy, const bool withTalker,
                              const bool centerX) const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d59(__FILE__, __LINE__);
    (void)legacy;
    if (centerX) {
        ::cwxeditor::cwx::perf::TraceScope trace_props_d60(__FILE__, __LINE__);
        return withTalker ? CPoint{50, 10} : CPoint{0, 10};
    }
    {
        ::cwxeditor::cwx::perf::TraceScope trace_props_d62(__FILE__, __LINE__);
    return withTalker ? CPoint{115, 10} : CPoint{15, 10};
    }
}

std::uint32_t Looks::messageLineHeight() const { return 22; }
std::uint32_t Looks::messageCharWidth() const { return 20; }
CPoint Looks::messageTalkerPos() const { return CPoint{15, 43}; }
std::uint32_t Looks::selectionBarMaxWithMessage() const { return 7; }
std::uint32_t Looks::selectionBarMax() const { return 13; }
std::int32_t Looks::aptVeryHigh() const { return 15; }
std::int32_t Looks::aptHigh() const { return 9; }
std::int32_t Looks::aptNormal() const { return 3; }
CPoint Looks::handXY() const { return CPoint{69, 105}; }
CPoint Looks::handXYWithLifeBar() const { return CPoint{69, 90}; }
CPoint Looks::useStoneXY() const { return CPoint{60, 75}; }
CPoint Looks::aptStoneXY() const { return CPoint{60, 90}; }
CPoint Looks::eventTreeXY() const { return CPoint{7, 90}; }
CPoint Looks::eventTreeXYWithCount() const { return CPoint{7, 71}; }
CPoint Looks::premiumXY() const { return CPoint{5, 5}; }

std::uint32_t Looks::itemCardMaxNum(const std::uint32_t lev) const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d86(__FILE__, __LINE__);
    return cardCapacity(lev, 2, 2);
}

std::uint32_t Looks::skillCardMaxNum(const std::uint32_t lev) const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d90(__FILE__, __LINE__);
    return cardCapacity(lev, 2, 2);
}

std::uint32_t Looks::beastCardMaxNum(const std::uint32_t lev) const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d94(__FILE__, __LINE__);
    return cardCapacity(lev, 4, 1);
}

std::int32_t Looks::cardDescLen() const { return 39; }
std::int32_t Looks::cardDescLine(const bool classic) const { return classic ? 8 : 9; }
std::int32_t Looks::messageImageLen() const { return 34; }
std::int32_t Looks::messageLen() const { return 44; }
std::int32_t Looks::messageLine() const { return 7; }
String Looks::openChars() const { return "\"'(<[`{‘“〈《≪「『【〔（＜［｛｢"; }
String Looks::closeChars() const {
    return "!\"'),.:;>?]`}゜’”′″、。々＞》≫」』】〕〟゛°ゝゞヽヾ〻！），．：；＞？］｝｡｣､ﾞﾟぁぃぅぇぉァィゥェォｧｨｩｪｫヵっッｯゃゅょャュョｬｭｮゎヮㇵㇶㇷㇸㇹㇺ…―ーｰ";
}
String Looks::wordRegex() const {
    return R"(([a-z0-9_]\p{M}*)+|([ａ-ｚＡ-Ｚ０-９＿]\p{M}*)+|.\p{M}*)";
}

std::int32_t Looks::stepMaxCount() const { return 10; }
std::uint32_t Looks::castNameLimit() const { return 14; }
std::uint32_t Looks::nameLimit() const { return 12; }

std::uint32_t Looks::lifeCalc(const std::uint32_t lev, const std::uint32_t vit,
                              const std::uint32_t spi) const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d113(__FILE__, __LINE__);
    const auto life = (static_cast<double>(lev) + 1.0) *
                          (static_cast<double>(vit) / 2.0 + 4.0) +
                      static_cast<double>(spi) / 2.0;
    return static_cast<std::uint32_t>(life);
}

std::uint32_t Looks::physicalCutMin() const { return 1; }
std::uint32_t Looks::physicalCutMaxBase() const { return 6; }
std::uint32_t Looks::physicalNormal() const { return 6; }
lib::Array<std::uint32_t> Looks::physicalBorders() const { return {1, 6, 12}; }
std::uint32_t Looks::mentalCut() const { return 4; }
lib::Array<std::uint32_t> Looks::mentalBorders() const { return {2}; }

std::int32_t Looks::skillPrice(const std::int32_t lev) const {
    // Dは両方のint演算を32ビットで折り返す。符号なし演算でそのビッ
    // ト列を保持し、C++の符号付きオーバーフローの未定義動作を避ける。
    const std::uint32_t bits = (static_cast<std::uint32_t>(lev) + 2U) * 200U;
    return std::bit_cast<std::int32_t>(bits);
}

std::int32_t Looks::beastPrice() const { return 500; }
std::int32_t Looks::keyCodesMaxLegacy() const { return 5; }
std::uint32_t Looks::motionRoundDefault() const { return 10; }
std::uint32_t Looks::stoneBorder() const { return 20; }
std::uint32_t Looks::idMax() const { return 99999; }
CSize Looks::viewSize() const { return CSize{632, 420}; }
std::uint32_t Looks::partyTop() const { return 280; }

String Looks::alternativeFont(String face) const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d135(__FILE__, __LINE__);
    if (!face.empty() && face.front() == '@') {
        String result("@");
        result += alternativeFont(face.substr(1));
        return result;
    }
    if (face == "ＭＳ ゴシック") return "IPAゴシック";
    if (face == "ＭＳ Ｐゴシック") return "IPA Pゴシック";
    if (face == "ＭＳ 明朝") return "IPA明朝";
    if (face == "ＭＳ Ｐ明朝") return "IPA P明朝";
    if (face == "MS UI Gothic") return "IPA UIゴシック";
    return face;
}

String Looks::monospace() const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d153(__FILE__, __LINE__);
#if defined(_WIN32)
    return "ＭＳ ゴシック";
#else
    return "IPAゴシック";
#endif
}

String Looks::gothic(const bool legacy) {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d161(__FILE__, __LINE__);
    (void)legacy;
#if defined(_WIN32)
    return "ＭＳ ゴシック";
#else
    return "IPAゴシック";
#endif
}

String Looks::pgothic(const bool legacy) {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d168(__FILE__, __LINE__);
    (void)legacy;
#if defined(_WIN32)
    return "ＭＳ Ｐゴシック";
#else
    return "IPA Pゴシック";
#endif
}

String Looks::mincho(const bool legacy) {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d175(__FILE__, __LINE__);
    (void)legacy;
#if defined(_WIN32)
    return "ＭＳ 明朝";
#else
    return "IPA明朝";
#endif
}

String Looks::pmincho(const bool legacy) {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d182(__FILE__, __LINE__);
    (void)legacy;
#if defined(_WIN32)
    return "ＭＳ Ｐ明朝";
#else
    return "IPA P明朝";
#endif
}

String Looks::uigothic(const bool legacy) {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d189(__FILE__, __LINE__);
    (void)legacy;
#if defined(_WIN32)
    return "MS UI Gothic";
#else
    return "IPA UIゴシック";
#endif
}

CFont Looks::textDlgFont(const std::uint32_t defSize) const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d196(__FILE__, __LINE__);
    return makeFont(gothic(true), defSize == 0U ? 12U : defSize, false, false);
}

CFont Looks::castCardNameFont(const bool legacy) const {
    return makeFont(uigothic(legacy), 12, true, false);
}

CFont Looks::castCardLevelFont(const bool legacy) const {
    return makeFont(mincho(legacy), 33, true, true);
}

CFont Looks::pcNumberFont(const bool legacy) const {
    return makeFont(mincho(legacy), 42, true, false);
}

CInsets Looks::castCardLevelInsets() const { return CInsets{2, 8, 0, 0}; }
CRGB Looks::castCardLevelColor() const { return CRGB{0, 0, 0, 128}; }
CPoint Looks::castLifeBarPoint() const { return CPoint{8, 110}; }
std::int32_t Looks::statusX() const { return 7; }
std::uint32_t Looks::statusVerMax() const { return 6; }

CFont Looks::statusTimeFont(const bool legacy, const std::uint32_t number) const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d207(__FILE__, __LINE__);
    if (number >= 100U) {
        ::cwxeditor::cwx::perf::TraceScope trace_props_d208(__FILE__, __LINE__);
        return makeFont(pgothic(legacy), 8, false, false);
    }
    if (number >= 10U) {
        ::cwxeditor::cwx::perf::TraceScope trace_props_d210(__FILE__, __LINE__);
        return makeFont(pgothic(legacy), 10, false, false);
    }
    {
        ::cwxeditor::cwx::perf::TraceScope trace_props_d212(__FILE__, __LINE__);
    return makeFont(pgothic(legacy), 12, false, false);
    }
}

CFont Looks::skillCardLevelFont(const bool legacy) const {
    return makeFont(mincho(legacy), 27, true, true);
}

CRGB Looks::skillCardLevelColor() const { return CRGB{0, 0, 0, 128}; }
CInsets Looks::skillCardLevelInsets() const { return CInsets{2, 8, 0, 0}; }
CFont Looks::menuCardNameFont(const bool legacy) const { return castCardNameFont(legacy); }
CFont Looks::cardNameFont(const bool legacy) const { return castCardNameFont(legacy); }
CFont Looks::useCountFont(const bool legacy) const {
    return makeFont(mincho(legacy), 18, true, false);
}
CPoint Looks::useCountPoint() const { return CPoint{10, 90}; }
CRGB Looks::recycleNumColor() const { return CRGB{255, 255, 0}; }
CFont Looks::summaryLevelFont(const bool legacy) const {
    return makeFont(pmincho(legacy), 13, true, true);
}
CFont Looks::summaryTitleFont(const bool legacy) const {
    return makeFont(pmincho(legacy), 21, true, false);
}
CFont Looks::summaryDescFont(const bool legacy) const {
    return makeFont(mincho(legacy), 13, true, false);
}
std::uint32_t Looks::summaryDescLineHeightClassic() const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d229(__FILE__, __LINE__); return 15; }
CFont Looks::summaryPageFont(const bool legacy) const {
    return makeFont(mincho(legacy), 12, true, false);
}
CFont Looks::cardDescFont(const bool legacy) const {
    return makeFont(gothic(legacy), 13, false, false);
}
CFont Looks::messageFont(const bool legacy) const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d234(__FILE__, __LINE__);
    return makeFont(mincho(legacy), 22, true, false);
}

CFont Looks::messageSelectFont(const bool legacy) const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d240(__FILE__, __LINE__);
#if defined(_WIN32)
    if (legacy) return makeFont(pgothic(legacy), 14, true, false);
#else
    (void)legacy;
#endif
    return makeFont(pgothic(legacy), 14, false, false);
}

CRGB Looks::pcCellForeColor() const { return CRGB{0, 0, 0}; }
CRGB Looks::pcCellBackColor() const { return CRGB{255, 255, 255, 192}; }
CFont Looks::scriptErrorFont(const std::uint32_t defSize) const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d250(__FILE__, __LINE__);
    return textDlgFont(defSize);
}

CProps::CProps(String appPath, const system::System& sys)
    : sys_(sys) {
    // Dのソース行番号の代わりにネイティブの診断用処理段階を使う。System
    // は借用参照。共有SkinContextは一つの有効なLooksと、状態
    // を持たないSystemラッパーの安定して保持されるコピーを提供する。
    String progress = "CProps";
    try {
        progress += " - appPath/sys";
        appPath_ = std::move(appPath);
        progress += " - Msgs";
        msgs_ = detail::makeDefaultMsgs();
        progress += " - Looks";
        skin_context_ = std::make_shared<detail::SkinContext>(sys_);
        progress += " - ready";
    } catch (...) {
        // Dはここでメモリ確保の失敗も含むThrowableを捕捉し、失敗した
        // 段階と元のエラーを診断してから、新しいExceptionを送出する。
        (void)utils::printStackTrace();
        utils::fdebugln(progress);
        try {
            throw;
        } catch (const std::exception& error) {
            utils::fdebugln(error.what());
        } catch (...) {
            utils::fdebugln("Unknown CProps initialization failure");
        }
        throw std::runtime_error(progress);
    }
}

CProps::~CProps() = default;

String CProps::appPath() const { return appPath_; }

void CProps::defaultSkinPath(String value) {
    const auto encoded = fsPath(value).lexically_normal().generic_u8string();
    defaultSkinPath_.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}
const system::System& CProps::sys() const noexcept { return sys_; }

lib::Owner<const Msgs> CProps::msgs() const noexcept {
    // Dはnullを許容するconstクラス参照を返す。後でCProps
    // が自身の参照を置換しても、選んだメッセージ一覧を保持する。
    return msgs_;
}

void CProps::msgs(lib::Owner<Msgs> value) { msgs_ = std::move(value); }

const Msgs& CProps::requiredMsgs() const {
    const auto catalog = msgs();
    if (!catalog) throw std::runtime_error("CProps has no message catalog");
    return *catalog;
}

const Looks& CProps::looks() const noexcept { return skin_context_->looks; }

String CProps::toAppAbs(const StringView pathValue) const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d292(__FILE__, __LINE__);
    if (utils::pathIsAbsolute(pathValue)) return utils::nabs(pathValue);
    const auto path = utils::buildPath(utils::pathDirName(appPath_), pathValue);
    return utils::nabs(StringView(path));
}

void CProps::loadMsgs(const StringView relPath) {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d298(__FILE__, __LINE__);
    const auto absolute = toAppAbs(relPath);
    try {
        ::cwxeditor::cwx::perf::TraceScope trace_props_d300(__FILE__, __LINE__);
        const auto path = fsPath(absolute);
        auto loaded = detail::msgsFromXML(readCatalogUtf8(path), 0);
        if (loaded) msgs_ = std::move(loaded);
    } catch (const std::exception& error) {
        // Dは失敗を記録し、現在のメッセージ一覧を保持する。不
        // 正な翻訳を診断できるよう、この両方の契約を維持する。
        utils::debugln("CProps::loadMsgs failed: ", absolute, ": ", error.what());
    } catch (...) {
        utils::debugln("CProps::loadMsgs failed: ", absolute,
                       ": unknown exception");
    }
}

lib::Dictionary<String, lib::Owner<Msgs>> CProps::msgsTable(
    const StringView languageDir, lib::Dictionary<String, String>& msgsTableFile,
    String& defLocale) const {
    // Dの入力文字列の記述子を、出力引数の初期化・書き込みをまたいで保持する。出力
    // 引数が呼び出し側のlanguageDir文字列と同じ領域を指す場合がある。
    const String languageInput(languageDir);
    defLocale.clear();
    ::cwxeditor::cwx::perf::TraceScope trace_props_d310(__FILE__, __LINE__);
    lib::Dictionary<String, lib::Owner<Msgs>> result;
    auto defaultMsgs = detail::makeDefaultMsgs();
    if (!defaultMsgs) throw std::runtime_error("default message catalog is unavailable");

    defLocale = enumutils::lowerCase(detail::msgsLocale(*defaultMsgs));
    msgsTableFile[defLocale] = String{};

    try {
        ::cwxeditor::cwx::perf::TraceScope trace_props_d316(__FILE__, __LINE__);
        const auto directory = toAppAbs(languageInput);
        const auto directoryPath = fsPath(directory);
        if (utils::pathExists(directoryPath)) {
            for (const auto& file : utils::clistdir(directoryPath)) {
                ::cwxeditor::cwx::perf::TraceScope trace_props_d319(__FILE__, __LINE__);
                if (!utils::cfnmatch(utils::pathExtension(file), ".xml")) continue;
                try {
                    ::cwxeditor::cwx::perf::TraceScope trace_props_d321(__FILE__, __LINE__);
                    const auto xml = readCatalogUtf8(fsPath(utils::buildPath(directory, file)));
                    // 値を返すXMLアダプターは、不正なXMLで例外を送出する。
                    // 既存のネイティブ側の修正を維持し、Dの後続のnullクラス
                    // のErrorを起こす代わりに、不正な任意の翻訳を飛ばす。
                    auto loaded = detail::msgsFromXML(xml, 0);
                    if (!loaded) continue;
                    const String locale = enumutils::lowerCase(detail::msgsLocale(*loaded));
                    if (defLocale == locale) {
                        ::cwxeditor::cwx::perf::TraceScope trace_props_d325(__FILE__, __LINE__);
                        defaultMsgs = std::move(loaded);
                    } else {
                        ::cwxeditor::cwx::perf::TraceScope trace_props_d327(__FILE__, __LINE__);
                        result[locale] = std::move(loaded);
                    }
                    msgsTableFile[locale] = file;
                } catch (const std::bad_alloc&) {
                    // Dのcatch(Exception)は、メモリ確保のErrorを捕捉しない。
                    throw;
                } catch (const std::exception& error) {
                    (void)utils::printStackTrace();
                    utils::debugln(error.what());
                }
            }
        }
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& error) {
        (void)utils::printStackTrace();
        utils::debugln(error.what());
    }

    result[defLocale] = std::move(defaultMsgs);
    return result;
}

bool CProps::targetVersion(const StringView ver, const StringView targVer) const noexcept {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d348(__FILE__, __LINE__);
    const auto versionRank = [](const StringView value, const bool isTarget) {
        if (value == "1.20") return 0;
        if (value == "1.28") return 1;
        if (value == "1.29") return 2;
        if (value == "1.30") return 3;
        if (value == "1.50") return 4;
        if (value == "1.60") return 5;
        return isTarget ? std::numeric_limits<std::int32_t>::max()
                        : std::numeric_limits<std::int32_t>::min();
    };
    return versionRank(ver, false) <= versionRank(targVer, true);
}

bool CProps::isTargetVersion(const lib::Owner<Summary>& summ,
                             const StringView targVer,
                             const StringView wsnVer) const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d362(__FILE__, __LINE__);
    if (summ) return detail::summaryIsTargetVersion(*summ, wsnVer);
    return targVer == "CardWirthPy";
}

bool CProps::isTargetVersion(const bool legacy, const StringView dataVersion,
                             const StringView wsnVer) const {
    ::cwxeditor::cwx::perf::TraceScope trace_props_d367(__FILE__, __LINE__);
    return detail::summaryIsTargetVersionWith(legacy, dataVersion, wsnVer);
}

}  // namespace cwxeditor::cwx::props
