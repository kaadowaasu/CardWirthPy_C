#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"

#include "cwxeditor_src/core/cwx/binary.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/imagesize.hpp"
#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace cwxeditor::cwx::warning {
namespace {

using ::cwxeditor::cwx::utils::tryFormat;

[[nodiscard]] String utf8CodePoint(const char32_t value) {
    String result;
    if (value <= 0x7fU) {
        result.push_back(static_cast<char>(value));
    } else if (value <= 0x7ffU) {
        result.push_back(static_cast<char>(0xc0U | (value >> 6U)));
        result.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    } else if (value <= 0xffffU) {
        result.push_back(static_cast<char>(0xe0U | (value >> 12U)));
        result.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
        result.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    } else if (value <= 0x10ffffU) {
        result.push_back(static_cast<char>(0xf0U | (value >> 18U)));
        result.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3fU)));
        result.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3fU)));
        result.push_back(static_cast<char>(0x80U | (value & 0x3fU)));
    }
    return result;
}

template <typename Fn>
void forEachCodePoint(StringView text, Fn&& fn) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index++]);
        char32_t value = 0;
        std::size_t continuation = 0;
        if (first < 0x80U) {
            value = first;
        } else if ((first & 0xe0U) == 0xc0U) {
            value = first & 0x1fU;
            continuation = 1;
        } else if ((first & 0xf0U) == 0xe0U) {
            value = first & 0x0fU;
            continuation = 2;
        } else if ((first & 0xf8U) == 0xf0U) {
            value = first & 0x07U;
            continuation = 3;
        } else {
            fn(static_cast<char32_t>(0xfffdU));
            continue;
        }
        bool valid = true;
        for (std::size_t n = 0; n < continuation; ++n) {
            if (index >= text.size()) {
                valid = false;
                break;
            }
            const auto next = static_cast<unsigned char>(text[index++]);
            if ((next & 0xc0U) != 0x80U) {
                valid = false;
                break;
            }
            value = (value << 6U) | (next & 0x3fU);
        }
        if (!valid || value > 0x10ffffU ||
            (value >= 0xd800U && value <= 0xdfffU)) {
            fn(static_cast<char32_t>(0xfffdU));
        } else {
            fn(value);
        }
    }
}

[[nodiscard]] bool targetWsn(const CProps& prop, bool classic,
                             StringView dataVersion, StringView version) {
    return prop.isTargetVersion(classic, dataVersion, version);
}

[[nodiscard]] String message(const CProps& prop, StringView key) {
    return detail::message(prop.requiredMsgs(), key);
}

[[nodiscard]] String formatMessage(const CProps& prop, StringView key,
                                   StringView value) {
    return detail::format(prop.requiredMsgs(), key, value);
}

[[nodiscard]] String formatMessage(const CProps& prop, StringView key,
                                   StringView value1, StringView value2) {
    return detail::format(prop.requiredMsgs(), key, value1, value2);
}

[[nodiscard]] String formatMessage(const CProps& prop, StringView key,
                                   const std::uint64_t value) {
    return tryFormat(message(prop, key), std::to_string(value));
}

[[nodiscard]] String formatMessage(const CProps& prop, StringView key,
                                   const std::int64_t value) {
    return tryFormat(message(prop, key), std::to_string(value));
}

[[nodiscard]] String asciiLower(StringView text) {
    String result(text);
    for (char& value : result) {
        if (value >= 'A' && value <= 'Z') {
            value = static_cast<char>(value - 'A' + 'a');
        }
    }
    return result;
}

template <typename T>
void append(WarningList& out, const std::vector<T>& values) {
    out.insert(out.end(), values.begin(), values.end());
}

void append(WarningList& out, String value) {
    if (!value.empty()) out.emplace_back(std::move(value));
}

void append(WarningList& out, std::span<const String> values) {
    out.insert(out.end(), values.begin(), values.end());
}

[[nodiscard]] bool containsStatus(std::span<const Status> statuses,
                                  Status status) {
    return std::find(statuses.begin(), statuses.end(), status) != statuses.end();
}

}  // namespace

namespace detail {
namespace {

[[nodiscard]] DomainPathInspector& pathInspectorStorage() {
    static DomainPathInspector value;
    return value;
}
[[nodiscard]] VariableFinder& variableFinderStorage() {
    static VariableFinder value;
    return value;
}
[[nodiscard]] SummaryInfoProvider& summaryInfoStorage() {
    static SummaryInfoProvider value;
    return value;
}
[[nodiscard]] CharacterTalkerInspector& characterTalkerStorage() {
    static CharacterTalkerInspector value;
    return value;
}
[[nodiscard]] MessageProvider& messageProviderStorage() {
    static MessageProvider value;
    return value;
}
[[nodiscard]] SkinPathFinder& skinPathFinderStorage() {
    static SkinPathFinder value;
    return value;
}
[[nodiscard]] SkinSpecialCharacterFinder& skinSpecialCharacterStorage() {
    static SkinSpecialCharacterFinder value;
    return value;
}
[[nodiscard]] SkinImageWarningProvider& skinImageWarningStorage() {
    static SkinImageWarningProvider value;
    return value;
}

}  // namespace

void setPathInspector(PathInspector inspector) {
    if (!inspector) {
        pathInspectorStorage() = {};
        return;
    }
    pathInspectorStorage() =
        [inspector = std::move(inspector)](
            const CProps& prop, const Skin& skin, const Summary* summ,
            const CWXPath& path, const bool isClassic, const StringView wsnVer,
            const StringView targVer, const WarningDomain) {
            return inspector(prop, skin, summ, path, isClassic, wsnVer,
                             targVer);
        };
}

void setDomainPathInspector(DomainPathInspector inspector) {
    pathInspectorStorage() = std::move(inspector);
}

void setVariableFinder(VariableFinder finder) {
    variableFinderStorage() = std::move(finder);
}

void setSummaryInfoProvider(SummaryInfoProvider provider) {
    summaryInfoStorage() = std::move(provider);
}

void setCharacterTalkerInspector(CharacterTalkerInspector inspector) {
    characterTalkerStorage() = std::move(inspector);
}

void setMessageProvider(MessageProvider provider) {
    messageProviderStorage() = std::move(provider);
}

void setSkinPathFinder(SkinPathFinder finder) {
    skinPathFinderStorage() = std::move(finder);
}

void setSkinSpecialCharacterFinder(SkinSpecialCharacterFinder finder) {
    skinSpecialCharacterStorage() = std::move(finder);
}

void setSkinImageWarningProvider(SkinImageWarningProvider provider) {
    skinImageWarningStorage() = std::move(provider);
}

WarningList inspectPath(const CProps& prop, const Skin& skin,
                        const Summary* summ, const CWXPath& path,
                        const bool isClassic, const StringView wsnVer,
                        const StringView targVer) {
    return inspectPath(prop, skin, summ, path, isClassic, wsnVer, targVer,
                       WarningDomain::ScenarioValidation);
}

WarningList inspectPath(const CProps& prop, const Skin& skin,
                        const Summary* summ, const CWXPath& path,
                        const bool isClassic, const StringView wsnVer,
                        const StringView targVer,
                        const WarningDomain domain) {
    const auto& inspector = pathInspectorStorage();
    if (!inspector) return {};
    return inspector(prop, skin, summ, path, isClassic, wsnVer, targVer,
                     domain);
}

bool findVariable(const Summary* summ, const UseCounter* uc,
                  const VariableKind kind, const StringView name) {
    const auto& finder = variableFinderStorage();
    return finder ? finder(summ, uc, kind, name) : false;
}

SummaryInfo summaryInfo(const Summary* summ) {
    const auto& provider = summaryInfoStorage();
    if (!summ || !provider) return {};
    return provider(summ);
}

bool inspectCharacterTalker(const Content& evt) {
    const auto& inspector = characterTalkerStorage();
    // ネイティブの接続部が、Dの具体的なContentへのアクセスを提供する。未
    // 登録の接続部を、正常に検査して問題がなかった結果の代わりにしてはならない。
    if (!inspector) throw std::logic_error("Character talker inspector is not registered");
    return inspector(evt);
}

bool skinPathFound(const Skin& skin, const StringView path,
                   const StringView scenarioPath, const StringView wsnVer,
                   const StringView kind) {
    const auto& finder = skinPathFinderStorage();
    return finder ? finder(skin, path, scenarioPath, wsnVer, kind) : false;
}

bool skinHasSpecialCharacter(const Skin& skin, const char32_t codepoint) {
    const auto& finder = skinSpecialCharacterStorage();
    return finder ? finder(skin, codepoint) : false;
}

WarningList skinImageWarnings(const CProps& prop, const Skin& skin,
                              const StringView path, const bool isClassic,
                              const bool includeType, const StringView targVer) {
    const auto& provider = skinImageWarningStorage();
    return provider ? provider(prop, skin, path, isClassic, includeType, targVer)
                    : WarningList{};
}

String message(const ::cwxeditor::cwx::props::Msgs& msgs, const StringView key) {
    const auto& provider = messageProviderStorage();
    if (provider) return provider(msgs, key);
    const auto& catalog = static_cast<const ::cwxeditor::cwx::msgs::Msgs&>(msgs);
    // クーポンの警告も含め、Dの全メッセージをメッセージ一覧から解決
    // する。表が不完全だと警告欄に未翻訳のフィールド名が表示される。
    if (const auto* value = catalog.findMessage(key)) return value->value;
    return String(key);
}

String format(const ::cwxeditor::cwx::props::Msgs& msgs, const StringView key,
              const StringView value) {
    return utils::tryFormat(message(msgs, key), value);
}

String format(const ::cwxeditor::cwx::props::Msgs& msgs, const StringView key,
              const StringView value1, const StringView value2) {
    return utils::tryFormat(message(msgs, key), value1, value2);
}

}  // namespace detail

WarningList warnings(const CProps& prop, const Skin& skin, const Summary* summ,
                     const CWXPath& path, const bool isClassic,
                     const StringView wsnVer, const StringView targVer) {
    return warnings(prop, skin, summ, path, isClassic, wsnVer, targVer,
                    WarningDomain::ScenarioValidation);
}

WarningList warnings(const CProps& prop, const Skin& skin, const Summary* summ,
                     const CWXPath& path, const bool isClassic,
                     const StringView wsnVer, StringView targVer,
                     const WarningDomain domain) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d34(__FILE__, __LINE__);
    if (!isClassic) targVer = {};
    return detail::inspectPath(prop, skin, summ, path, isClassic, wsnVer,
                               targVer, domain);
}

WarningList warnings(const CProps& prop, const Skin& skin,
                     const SummaryOwner& summ, const CWXPath& path,
                     const bool isClassic, const StringView wsnVer,
                     const StringView targVer) {
    return warnings(prop, skin, summ.get(), path, isClassic, wsnVer, targVer,
                    WarningDomain::ScenarioValidation);
}

WarningList warnings(const CProps& prop, const Skin& skin,
                     const SummaryOwner& summ, const CWXPath& path,
                     const bool isClassic, const StringView wsnVer,
                     const StringView targVer, const WarningDomain domain) {
    return warnings(prop, skin, summ.get(), path, isClassic, wsnVer, targVer,
                    domain);
}

WarningList exprErrorToWarnings(
    const CProps& prop, const StringView expr,
    const ::cwxeditor::lib::Array<ExprError>& exprErrs) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1341(__FILE__, __LINE__);
    WarningList result;
    std::vector<String> lines;
    for (const auto& error : exprErrs) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1344(__FILE__, __LINE__);
        if (lines.empty()) {
            lines = utils::splitLines(expr);
        }
        String line;
        // Dは要求された行を直接添字で参照する。不正な外部診断
        // でも、範囲外アクセスを起こさず表示できるようにする。
        if (error.errLine == 0 || error.errLine > lines.size()) {
            line = message(prop, "warningExpressionEnd");
        } else {
            line = lines[error.errLine - 1U];
            if (error.errPos >= line.size()) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1347(__FILE__, __LINE__);
                line += message(prop, "warningExpressionEnd");
            } else {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1349(__FILE__, __LINE__);
                const auto offset = error.errPos == 0 ? 0U : error.errPos - 1U;
                line.insert(offset, message(prop, "warningExpressionPosition"));
            }
        }
        result.emplace_back(formatMessage(prop, "warningExpression",
                                           error.message, line));
    }
    return result;
}

bool hasCharacterTalker(const Content& evt) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1359(__FILE__, __LINE__);
    return detail::inspectCharacterTalker(evt);
}

TextWarnings textWarnings(
    const CProps& prop, const Skin& skin, const Summary* summ,
    const UseCounter* uc, const bool isClassic, const StringView wsnVer,
    const StringView targVer, const StringView text,
    std::span<const String> flags, std::span<const String> steps,
    std::span<const String> variants, std::span<const String> fonts,
    std::span<const char> colors,
    ::cwxeditor::lib::Dictionary<String, bool>& wFlags,
    ::cwxeditor::lib::Dictionary<String, bool>& wSteps,
    ::cwxeditor::lib::Dictionary<String, bool>& wVariants,
    ::cwxeditor::lib::Dictionary<String, bool>& wFonts,
    ::cwxeditor::lib::Dictionary<char, bool>& wColors) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1383(__FILE__, __LINE__);
    TextWarnings result;
    const auto info = detail::summaryInfo(summ);
    const bool is160 = isClassic &&
                       std::find(versioninfo::CLASSIC_VERSIONS.begin(),
                                 versioninfo::CLASSIC_VERSIONS.end(), targVer) !=
                           versioninfo::CLASSIC_VERSIONS.end() &&
                       prop.targetVersion("1.60", targVer);

    const auto sjis = sjisWarnings(prop, isClassic, text,
                                   message(prop, "message"));
    append(result.all, sjis);
    append(result.noDup, sjis);

    for (const auto& font : fonts) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1396(__FILE__, __LINE__);
        const auto codepoint = msgutils::decodeFontPath(font);
        if (detail::skinHasSpecialCharacter(skin, codepoint)) continue;
        bool put = false;
        if (isClassic && !sjis::isSJIS1ByteChar(codepoint)) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1400(__FILE__, __LINE__);
            const auto code = String("#") + utf8CodePoint(codepoint);
            const auto warning = formatMessage(
                prop, "searchErrorSPFontIsNotSJIS1ByteChar", code);
            result.all.push_back(warning);
            if (!wFonts[font]) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1403(__FILE__, __LINE__);
                result.noDup.push_back(warning);
                put = true;
            }
        }
        if (!detail::skinPathFound(skin, font, info.scenarioPath, wsnVer,
                                   "image")) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1408(__FILE__, __LINE__);
            const auto code = String("#") + utf8CodePoint(codepoint);
            const auto warning = formatMessage(prop, "searchErrorSPFontNotFound",
                                               code);
            result.all.push_back(warning);
            if (!wFonts[font]) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1411(__FILE__, __LINE__);
                result.noDup.push_back(warning);
                put = true;
            }
        }
        if (put) wFonts[font] = true;
    }

    for (const auto& flag : flags) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1418(__FILE__, __LINE__);
        if (detail::findVariable(summ, uc, detail::VariableKind::Flag, flag)) {
            continue;
        }
        {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1419(__FILE__, __LINE__);
        const auto warning = formatMessage(prop, "searchErrorFlagNotFound", flag);
        result.all.push_back(warning);
        if (!wFlags[flag]) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1422(__FILE__, __LINE__);
            result.noDup.push_back(warning);
            wFlags[flag] = true;
        }
            }
}

    for (const auto& step : steps) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1428(__FILE__, __LINE__);
        if (detail::findVariable(summ, uc, detail::VariableKind::Step, step)) {
            continue;
        }
        {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1429(__FILE__, __LINE__);
        String warning;
        const auto prefix = prop.sys().prefixSystemVarName();
        if (step.starts_with(prefix)) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1431(__FILE__, __LINE__);
            // Dはここでstd.uni.icmpを使い、Unicodeの完全な
            // ケースフォールドも適用する。ASCIIだけの小文字化では、予約
            // システム変数名としてDが許可するUnicode表記を見落とす。
            if (enumutils::compareIgnoreCase(
                    prop.sys().selectedPlayerCardNumber(), step) == 0) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1432(__FILE__, __LINE__);
                if (!targetWsn(prop, isClassic, wsnVer, "2") && !is160) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1434(__FILE__, __LINE__);
                    warning = message(prop, "warningSelectedPlayerCardNumber");
                }
            } else {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1437(__FILE__, __LINE__);
                bool playerCard = false;
                for (std::uint32_t index = 1; index <= prop.looks().partyMax();
                     ++index) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1439(__FILE__, __LINE__);
                    if (enumutils::compareIgnoreCase(
                            prop.sys().playerCardName(index), step) == 0) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1440(__FILE__, __LINE__);
                        playerCard = true;
                        if (!targetWsn(prop, isClassic, wsnVer, "2") && !is160) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1442(__FILE__, __LINE__);
                            warning = formatMessage(prop, "warningPlayerCardName",
                                                    std::to_string(index));
                        }
                        break;
                    }
                }
                if (!playerCard) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1449(__FILE__, __LINE__);
                    warning = formatMessage(prop, "warningUnknownSystemValue",
                                            step);
                }
            }
        } else {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1453(__FILE__, __LINE__);
            warning = formatMessage(prop, "searchErrorStepNotFound", step);
        }
        if (warning.empty()) continue;
        result.all.push_back(warning);
        if (!wSteps[step]) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1458(__FILE__, __LINE__);
            result.noDup.push_back(warning);
            wSteps[step] = true;
        }
            }
}

    for (const auto& variant : variants) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1464(__FILE__, __LINE__);
        if (detail::findVariable(summ, uc, detail::VariableKind::Variant,
                                 variant)) {
            continue;
        }
        {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1465(__FILE__, __LINE__);
        const auto warning = formatMessage(prop, "searchErrorVariantNotFound",
                                           variant);
        result.all.push_back(warning);
        if (!wVariants[variant]) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1468(__FILE__, __LINE__);
            result.noDup.push_back(warning);
            wVariants[variant] = true;
        }
            }
}

    if (isClassic && !prop.targetVersion("1.50", targVer)) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1474(__FILE__, __LINE__);
        for (const char color : colors) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1475(__FILE__, __LINE__);
            const auto upper = static_cast<char>(
                std::toupper(static_cast<unsigned char>(color)));
            if (upper != 'O' && upper != 'P' && upper != 'L' && upper != 'D') {
                continue;
            }
            const String colorName = String("&") + color;
            const auto warning = formatMessage(prop, "warningTextColor",
                                               colorName, "1.50");
            result.all.push_back(warning);
            if (!wColors[color]) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1480(__FILE__, __LINE__);
                result.noDup.push_back(warning);
                wColors[color] = true;
            }
        }
    }
    return result;
}

TextWarnings textWarnings(
    const CProps& prop, const Skin& skin, const SummaryOwner& summ,
    const UseCounter* uc, const bool isClassic, const StringView wsnVer,
    const StringView targVer, const StringView text,
    const std::span<const String> flags, const std::span<const String> steps,
    const std::span<const String> variants,
    const std::span<const String> fonts, const std::span<const char> colors,
    ::cwxeditor::lib::Dictionary<String, bool>& wFlags,
    ::cwxeditor::lib::Dictionary<String, bool>& wSteps,
    ::cwxeditor::lib::Dictionary<String, bool>& wVariants,
    ::cwxeditor::lib::Dictionary<String, bool>& wFonts,
    ::cwxeditor::lib::Dictionary<char, bool>& wColors) {
    return textWarnings(prop, skin, summ.get(), uc, isClassic, wsnVer, targVer,
                        text, flags, steps, variants, fonts, colors, wFlags,
                        wSteps, wVariants, wFonts, wColors);
}

TextWarnings textWarnings(
    const CProps& prop, const Skin& skin, const Summary* summ,
    const UseCounter* uc, const bool isClassic, const StringView wsnVer,
    const StringView targVer, const StringView text, const StringView flags,
    const StringView steps, const StringView variants, const StringView fonts,
    const StringView colors,
    ::cwxeditor::lib::Dictionary<String, bool>& wFlags,
    ::cwxeditor::lib::Dictionary<String, bool>& wSteps,
    ::cwxeditor::lib::Dictionary<String, bool>& wVariants,
    ::cwxeditor::lib::Dictionary<String, bool>& wFonts,
    ::cwxeditor::lib::Dictionary<char, bool>& wColors) {
    const std::array<String, 1> flagValues{String(flags)};
    const std::array<String, 1> stepValues{String(steps)};
    const std::array<String, 1> variantValues{String(variants)};
    const std::array<String, 1> fontValues{String(fonts)};
    return textWarnings(prop, skin, summ, uc, isClassic, wsnVer, targVer, text,
                        flagValues, stepValues, variantValues, fontValues,
                        std::span<const char>(colors.data(), colors.size()),
                        wFlags, wSteps, wVariants, wFonts, wColors);
}

WarningList couponWarnings(const CProps& prop, const bool isClassic,
                           const StringView wsnVer, const StringView targVer,
                           const StringView coupon, const bool getLose,
                           const StringView itemName) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1494(__FILE__, __LINE__);
    WarningList result = sjisWarnings(prop, isClassic, coupon, itemName);
    const auto& sys = prop.sys();
    if (coupon == sys.userCoupon() || coupon == sys.eventTargetCoupon() ||
        coupon == sys.effectOutOfTargetCoupon()) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1497(__FILE__, __LINE__);
        if (!targetWsn(prop, isClassic, wsnVer, "2")) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1498(__FILE__, __LINE__);
            result.push_back(formatMessage(prop, "warningWsnSystemCoupon",
                                           coupon, "2"));
        }
        if (getLose) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1501(__FILE__, __LINE__);
            result.push_back(formatMessage(prop, "warningCanNotGetSetCoupon",
                                           coupon));
        }
    } else if (coupon == sys.effectTargetCoupon()) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1504(__FILE__, __LINE__);
        if (!targetWsn(prop, isClassic, wsnVer, "2")) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1505(__FILE__, __LINE__);
            result.push_back(formatMessage(prop, "warningWsnSystemCoupon",
                                           coupon, "2"));
        }
    } else if (getLose && coupon.starts_with(sys.couponSystem())) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1508(__FILE__, __LINE__);
        result.push_back(formatMessage(prop, "warningSystemCoupon", coupon));
    }
    return result;
}

String warningNextCondition(const CProps& prop, const CNextType cNextType,
                            const StringView name) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1516(__FILE__, __LINE__);
    switch (cNextType) {
    case CNextType::None:
        return name.empty() ? String{} : formatMessage(prop, "invalidNextName", name);
    case CNextType::Text:
    case CNextType::Coupon:
        return {};
    case CNextType::Bool:
        return warningBoolCondition(prop, name);
    case CNextType::Step:
    case CNextType::IdArea:
    case CNextType::IdBattle:
        return warningNumberCondition(prop, name);
    case CNextType::Trio:
        return warningTrioCondition(prop, name);
    }
    return {};
}

String warningBoolCondition(const CProps& prop, const StringView text) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1534(__FILE__, __LINE__);
    const auto& sys = prop.sys();
    if (text == sys.evtChildTrue() || text == sys.evtChildFalse()) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1535(__FILE__, __LINE__);return {}; }
    if (text.empty()) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1537(__FILE__, __LINE__);return message(prop, "unknownBranchConditionNoText"); }
    {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1539(__FILE__, __LINE__);return formatMessage(prop, "unknownBranchCondition", text); }
}

String warningNumberCondition(const CProps& prop, const StringView text) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1543(__FILE__, __LINE__);
    if (text == prop.sys().evtChildDefault()) return {};
    if (utils::isNumeric(text)) {
        if (utils::detail::parseDInteger<std::uint32_t>(text)) return {};
        // Dは広いisNumeric判定の後にConvExceptionだけ
        // を捕捉する。ネイティブの変換結果でも、その追跡・診断順を維持する。
        (void)utils::printStackTrace();
    }
    if (text.empty()) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1556(__FILE__, __LINE__);return message(prop, "unknownBranchConditionNoText"); }
    {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1558(__FILE__, __LINE__);return formatMessage(prop, "unknownBranchCondition", text); }
}

String warningTrioCondition(const CProps& prop, const StringView text) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1562(__FILE__, __LINE__);
    const auto& sys = prop.sys();
    if (text == sys.evtChildGreater() || text == sys.evtChildLesser() ||
        text == sys.evtChildEq()) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1563(__FILE__, __LINE__);
        return {};
    }
    if (text.empty()) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1565(__FILE__, __LINE__);return message(prop, "unknownBranchConditionNoText"); }
    {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1567(__FILE__, __LINE__);return formatMessage(prop, "unknownBranchCondition", text); }
}

WarningList warningInconsistency(const CProps& prop,
                                 std::span<const Status> statuses) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1574(__FILE__, __LINE__);
    std::array<bool, enumutils::EnumMembers<Status>.size()> present{};
    for (const auto status : statuses) present.at(static_cast<std::size_t>(status)) = true;
    WarningList result;
    const auto add = [&](const Status first, const Status second) {
        if (!present.at(static_cast<std::size_t>(first)) ||
            !present.at(static_cast<std::size_t>(second))) {
            return;
        }
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1579(__FILE__, __LINE__);
        const auto a = static_cast<std::int32_t>(first);
        const auto b = static_cast<std::int32_t>(second);
        const auto left = std::min(a, b);
        const auto right = std::max(a, b);
        const auto leftName = prop.requiredMsgs().statusName(static_cast<Status>(left));
        const auto rightName = prop.requiredMsgs().statusName(static_cast<Status>(right));
        result.push_back(formatMessage(prop, "warningInconsistencyStatus",
                                       leftName, rightName));
    };

    {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1587(__FILE__, __LINE__);    add(Status::Active, Status::Inactive);
    add(Status::Active, Status::Dead);
    add(Status::Active, Status::Unconscious);
    add(Status::Active, Status::Sleep);
    add(Status::Active, Status::Bind);
    add(Status::Active, Status::Paralyze);
    add(Status::Alive, Status::Dead);
    add(Status::Alive, Status::Unconscious);
    add(Status::Alive, Status::Paralyze);
    add(Status::Dead, Status::Sleep);
    add(Status::Dead, Status::Confuse);
    add(Status::Dead, Status::Overheat);
    add(Status::Dead, Status::Brave);
    add(Status::Dead, Status::Panic);
    add(Status::Fine, Status::Injured);
    add(Status::Fine, Status::HeavyInjured);
    add(Status::Fine, Status::Unconscious);
    add(Status::Injured, Status::HeavyInjured);
    add(Status::Injured, Status::Unconscious);
    add(Status::HeavyInjured, Status::Unconscious);
    add(Status::Unconscious, Status::Sleep);
    add(Status::Unconscious, Status::Bind);
    add(Status::Unconscious, Status::Confuse);
    add(Status::Unconscious, Status::Overheat);
    add(Status::Unconscious, Status::Brave);
    add(Status::Unconscious, Status::Panic);
    add(Status::Unconscious, Status::Silence);
    add(Status::Unconscious, Status::FaceUp);
    add(Status::Unconscious, Status::AntiMagic);
    add(Status::Unconscious, Status::UpAction);
    add(Status::Unconscious, Status::UpAvoid);
    add(Status::Unconscious, Status::UpResist);
    add(Status::Unconscious, Status::UpDefense);
    add(Status::Unconscious, Status::DownAction);
    add(Status::Unconscious, Status::DownAvoid);
    add(Status::Unconscious, Status::DownResist);
    add(Status::Unconscious, Status::DownDefense);
    add(Status::Sleep, Status::Paralyze);
    add(Status::Sleep, Status::Confuse);
    add(Status::Sleep, Status::Overheat);
    add(Status::Sleep, Status::Brave);
    add(Status::Sleep, Status::Panic);
    add(Status::Paralyze, Status::Confuse);
    add(Status::Paralyze, Status::Overheat);
    add(Status::Paralyze, Status::Brave);
    add(Status::Paralyze, Status::Panic);
    add(Status::Confuse, Status::Overheat);
    add(Status::Confuse, Status::Brave);
    add(Status::Confuse, Status::Panic);
    add(Status::Overheat, Status::Brave);
    add(Status::Overheat, Status::Panic);
    add(Status::Brave, Status::Panic);
    add(Status::UpAction, Status::DownAction);
    add(Status::UpAvoid, Status::DownAvoid);
    add(Status::UpResist, Status::DownResist);
    add(Status::UpDefense, Status::DownDefense);
    }

    std::sort(result.begin(), result.end());
    // Dはuniqの遅延範囲を巡回せずに捨てる。即時のerase/unique
    // を適用せず、重複メッセージを維持する。
    return result;
}

WarningList fileExtensionWarnings(const CProps& prop,
                                  const StringView filePath) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1683(__FILE__, __LINE__);
    WarningList result;
    try {
        // Dと同様に、両方の形式判定は同じ開いたファイルを調べて位置を戻
        // す。パスで開き直すと、置換された別のファイルを見る場合がある。
        binary::RawFile input(filePath, "rb");
        auto ext = imagesize::getNormalizedImageExt(input);
        if (!ext.empty() &&
            imagesize::normalizedImageExt(utils::pathExtension(filePath)) != ext) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1689(__FILE__, __LINE__);
            result.push_back(formatMessage(prop, "warningFileExtension", ext));
        }
        if (ext.empty()) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1692(__FILE__, __LINE__);
            ext = utils::getNormalizedSoundExt(input);
            if (!ext.empty() && utils::normalizedSoundExt(utils::pathExtension(filePath)) != ext) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1694(__FILE__, __LINE__);
                result.push_back(formatMessage(prop, "warningFileExtension", ext));
            }
        }
    } catch (const std::exception& error) {
        (void)utils::printStackTrace();
        utils::debugln(error.what());
    }
    return result;
}

WarningList sjisWarnings(const CProps& prop, const Summary* summ,
                         const StringView text, const StringView name) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1706(__FILE__, __LINE__);
    return sjisWarnings(prop, detail::summaryInfo(summ).legacy, text, name);
}

WarningList sjisWarnings(const CProps& prop, const SummaryOwner& summ,
                         const StringView text, const StringView name) {
    return sjisWarnings(prop, summ.get(), text, name);
}

WarningList sjisWarnings(const CProps& prop, const bool legacy,
                         const StringView text, const StringView name) {
    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1710(__FILE__, __LINE__);
    if (!legacy) return {};
    WarningList result;
    bool found = false;
    forEachCodePoint(text, [&](const char32_t codepoint) {
        // Dは最初の警告で戻る。それ以後のアダプターの訪問では、Dの処理範囲へ入らない。
        if (found) return;
        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1712(__FILE__, __LINE__);
        if (sjis::canConvToSJIS(codepoint)) return;
        {
            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1713(__FILE__, __LINE__);
            found = true;
            const auto value = utf8CodePoint(codepoint);
            if (name.empty()) {
                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1714(__FILE__, __LINE__);
                result.push_back(formatMessage(prop, "warningInvalidSJISCharacter",
                                               value));
            } else {
                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1716(__FILE__, __LINE__);
                result.push_back(formatMessage(
                    prop, "warningInvalidSJISCharacterWithName", name, value));
            }
        }
    });
    return result;
}

}  // namespace cwxeditor::cwx::warning
