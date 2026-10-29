#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/warning.hpp"
#include "cwxeditor_src/core/cwx/warning_contract.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace cwxeditor::cwx::props::detail {

lib::Owner<Msgs> makeDefaultMsgs() {
    return std::make_shared<msgs::Msgs>();
}

lib::Owner<Msgs> msgsFromXML(const StringView xml,
                             const std::uint64_t dataVersion) {
    return std::make_shared<msgs::Msgs>(msgs::Msgs::fromXML(String(xml), dataVersion));
}

String msgsLocale(const Msgs& messages) {
    return messages.locale.value;
}

}  // namespace cwxeditor::cwx::props::detail

namespace cwxeditor::cwx::skin::detail {

lib::Owner<card::ActionCard> actionCardFromNode(XNode& node,
                                                 const XMLInfo& version) {
    return card::ActionCard::createFromNode(node, version);
}

ActionCardType actionCardType(const card::ActionCard& value) {
    return value.actionCardType();
}

String actionCardName(const card::ActionCard& value, const System&) {
    return value.name();
}

Aptitude actionCardAptitude(const card::ActionCard& value) {
    return Aptitude{value.physical(), value.mental()};
}

String warningMessage(const props::Msgs& messages, const StringView key) {
    if (key == "oggMayNotCorrespond") return messages.oggMayNotCorrespond.value;
    if (key == "mp3LoopMayNotCorrespond") return messages.mp3LoopMayNotCorrespond.value;
    if (key == "warningIncludedPNGImage") return messages.warningIncludedPNGImage.value;
    if (key == "warningIncludedGIFImage") return messages.warningIncludedGIFImage.value;
    if (key == "pngMayNotCorrespond") return messages.pngMayNotCorrespond.value;
    if (key == "gifMayNotCorrespond") return messages.gifMayNotCorrespond.value;
    if (key == "warningInvalidFileExtensionImage") {
        return messages.warningInvalidFileExtensionImage.value;
    }
    if (key == "warningInvalidFileExtensionSound") {
        return messages.warningInvalidFileExtensionSound.value;
    }
    return String(key);
}

}  // namespace cwxeditor::cwx::skin::detail

namespace cwxeditor::cwx {
namespace {

// Dのstd.convの失敗は回復可能なExceptionだが、
// メモリ確保失敗はError。変換失敗と無関係な失敗を区別する。
struct WarningNumberConversionError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct WarningNumberOverflowError final : WarningNumberConversionError {
    using WarningNumberConversionError::WarningNumberConversionError;
};

std::int64_t warningSignedBranchValue(lib::StringView text) {
    // std::from_charsは、Dのto!longが許可する先頭のプラスに対応しない。
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
        if (text.empty() || text.front() < '0' || text.front() > '9')
            throw WarningNumberConversionError("invalid signed branch value");
    }
    if (text.empty())
        throw WarningNumberConversionError("empty signed branch value");
    std::int64_t result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result, 10);
    if (parsed.ec == std::errc::result_out_of_range)
        throw WarningNumberOverflowError("signed branch value overflow");
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw WarningNumberConversionError("invalid signed branch value");
    return result;
}

// PhobosのisNumericはto!uintより広い形式を認め
// る。字句判定を変換とは分けつつ、コアの警告処理と実装を共有する。
bool warningBranchIsNumeric(lib::StringView text) noexcept {
    // モデルの判定と警告用の変換を、同じ字句規則へ集約する。
    return utils::isNumeric(text);
}

std::uint32_t warningUnsignedBranchValue(const lib::StringView text) {
    if (text.empty())
        throw WarningNumberConversionError("empty unsigned branch value");
    std::uint32_t result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result, 10);
    if (parsed.ec == std::errc::result_out_of_range)
        throw WarningNumberOverflowError("unsigned branch value overflow");
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw WarningNumberConversionError("invalid unsigned branch value");
    return result;
}

struct WarningAdapterInstaller final {
    WarningAdapterInstaller() {
        using warning::detail::VariableKind;
        warning::detail::setDomainPathInspector(
            [](const props::CProps& prop, const skin::Skin& skin,
               const summary::Summary* summ, const path::CWXPath& pathValue,
               const bool isClassic, const lib::StringView wsnVersion,
               const lib::StringView targetVersion,
               const warning::WarningDomain domain) {
                warning::WarningList result;
                const auto& messages = prop.requiredMsgs();
                const bool is160 =
                    isClassic &&
                    std::find(versioninfo::CLASSIC_VERSIONS.begin(),
                              versioninfo::CLASSIC_VERSIONS.end(),
                              targetVersion) != versioninfo::CLASSIC_VERSIONS.end() &&
                    prop.targetVersion("1.60", targetVersion);
                const auto append = [&result](warning::WarningList values) {
                    result.insert(result.end(),
                                  std::make_move_iterator(values.begin()),
                                  std::make_move_iterator(values.end()));
                };
                const auto addCardImages = [&](const auto& imagePaths,
                                               const bool includeType) {
                                                   ::cwxeditor::cwx::perf::TraceScope trace_warning_d41(__FILE__, __LINE__);
                    bool warnedPosition = false;
                    for (const auto& image : imagePaths) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d43(__FILE__, __LINE__);
                        // ネイティブのnull項目を許容する拡張は、Dの通常の対象範囲とは区別する。
                        if (!image) continue;
                        if (image->type == types::CardImageType::File) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d44(__FILE__, __LINE__);
                            // 取得処理は不透明なままにする。先に型付きのFileの範囲へ
                            // 入り、符号化パスや分類処理の動作をここで変更・追加しない。
                            const auto imagePath = image->path();
                            if (!imagePath.empty()) {
                            if (!utils::isBinImg(imagePath) &&
                                skin.findPath(imagePath, skin.extImage(),
                                              skin.tableDirs(),
                                              summ ? summ->scenarioPath() : lib::String{},
                                              wsnVersion,
                                              skin.wsnTableDirs(wsnVersion)).empty()) {
                                                  ::cwxeditor::cwx::perf::TraceScope trace_warning_d45(__FILE__, __LINE__);
                                result.push_back(utils::tryFormat(
                                    messages.searchErrorImageNotFound.value,
                                    utils::encodePath(imagePath)));
                            }
                            append(skin::Skin::warningImage(
                                prop, imagePath, isClassic, includeType,
                                targetVersion));
                        }
                        }
                        if (image->type == types::CardImageType::PCNumber &&
                            !prop.targetVersion("1.50", targetVersion)) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d52(__FILE__, __LINE__);
                            result.push_back(messages.warningPCNumberClassic.value);
                        }
                        if (!warnedPosition &&
                            image->positionType() !=
                                types::CardImagePosition::Default &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d55(__FILE__, __LINE__);
                            result.push_back(messages.warningCardImagePosition.value);
                            warnedPosition = true;
                        }
                    }
                };

                if (const auto* value =
                        dynamic_cast<const summary::Summary*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d63(__FILE__, __LINE__);
                    addCardImages(value->imagePaths(), true);
                    if (!value->area(value->startArea())) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d65(__FILE__, __LINE__);
                        result.push_back(messages.searchErrorStartAreaNotFound.value);
                    }
                    append(warning::sjisWarnings(prop, isClassic,
                                                 value->scenarioName(),
                                                 messages.title.value));
                    append(warning::sjisWarnings(prop, isClassic, value->author(),
                                                 messages.author.value));
                    if ((value->levelMin() != 0 || value->levelMax() != 0) &&
                        value->legacy()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d70(__FILE__, __LINE__);
                        if (value->levelMin() == 0) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d71(__FILE__, __LINE__);
                            result.push_back(messages.warningNoLevelMin.value);
                        } else if (value->levelMax() == 0) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d73(__FILE__, __LINE__);
                            result.push_back(messages.warningNoLevelMax.value);
                        }
                    }
                    if (value->levelMin() != 0 && value->levelMax() != 0 &&
                        value->levelMin() > value->levelMax()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d77(__FILE__, __LINE__);
                        result.push_back(messages.searchErrorReversalLevel.value);
                    }
                    append(warning::sjisWarnings(prop, isClassic, value->desc(),
                                                 messages.desc.value));
                    for (const auto& coupon : value->rCoupons()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d81(__FILE__, __LINE__);
                        append(warning::sjisWarnings(prop, isClassic, coupon,
                                                     messages.rCoupons.value));
                    }
                }

                const auto root = summ ? summ->flagDirRoot()
                                       : lib::Owner<flag::FlagDir>{};
                const auto hasFlag = [&](const lib::StringView name,
                                         const lib::Owner<usecounter::UseCounter>& counter) {
                    return name.empty() ||
                           static_cast<bool>(flag::findVar<flag::Flag>(root, counter, name));
                };
                const auto hasStep = [&](const lib::StringView name,
                                         const lib::Owner<usecounter::UseCounter>& counter) {
                    return name.empty() ||
                           static_cast<bool>(flag::findVar<flag::Step>(root, counter, name));
                };
                const auto hasVariant = [&](
                                            const lib::StringView name,
                                            const lib::Owner<usecounter::UseCounter>& counter) {
                    return name.empty() ||
                           static_cast<bool>(
                               flag::findVar<flag::Variant>(root, counter, name));
                };
                const auto checkTextRes =
                    [&](const lib::StringView text,
                        const usecounter::UseCounter* counter,
                        const std::span<const lib::String> flags,
                        const std::span<const lib::String> steps,
                        const std::span<const lib::String> variants,
                        const std::span<const lib::String> fonts,
                        const std::span<const char> colors,
                        lib::Dictionary<lib::String, bool>& wFlags,
                        lib::Dictionary<lib::String, bool>& wSteps,
                        lib::Dictionary<lib::String, bool>& wVariants,
                        lib::Dictionary<lib::String, bool>& wFonts,
                        lib::Dictionary<char, bool>& wColors) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d88(__FILE__, __LINE__);
                        return warning::textWarnings(
                            prop, skin, summ, counter, isClassic, wsnVersion,
                            targetVersion, text, flags, steps, variants, fonts,
                            colors, wFlags, wSteps, wVariants, wFonts, wColors);
                    };
                const auto checkTextRes2 =
                    [&](const lib::StringView text,
                        const usecounter::UseCounter* counter,
                        const std::span<const lib::String> flags,
                        const std::span<const lib::String> steps,
                        const std::span<const lib::String> variants,
                        const std::span<const lib::String> fonts,
                        const std::span<const char> colors) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d91(__FILE__, __LINE__);
                        lib::Dictionary<lib::String, bool> wFlags;
                        lib::Dictionary<lib::String, bool> wSteps;
                        lib::Dictionary<lib::String, bool> wVariants;
                        lib::Dictionary<lib::String, bool> wFonts;
                        lib::Dictionary<char, bool> wColors;
                        return checkTextRes(text, counter, flags, steps, variants,
                                            fonts, colors, wFlags, wSteps,
                                            wVariants, wFonts, wColors).all;
                    };
                if (const auto* value =
                        dynamic_cast<const flag::FlagDir*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d101(__FILE__, __LINE__);
                    if (value->parent().get() == root.get() &&
                        prop.sys().isSystemVar(value->name())) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d102(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorSystemVariable.value,
                            value->name()));
                    }
                    append(warning::sjisWarnings(prop, isClassic, value->name(), {}));
                }
                if (const auto* value = dynamic_cast<const flag::Flag*>(&pathValue)) {
                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d108(__FILE__, __LINE__);
                    if (value->parent().get() == root.get() &&
                        prop.sys().isSystemVar(value->name())) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d109(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorSystemVariable.value,
                            value->name()));
                    }
                    if (value->expandSPChars() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d112(__FILE__, __LINE__);
                        result.push_back(messages.warningExpandSPChars.value);
                    }
                    if (value->useCounter() &&
                        dynamic_cast<const summary::Summary*>(
                            value->useCounter()->owner().get()) &&
                        !types::isDefaultVariableInitialization(
                            value->initialization()) &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d115(__FILE__, __LINE__);
                        result.push_back(messages.warningVariableInitialization);
                    }
                    append(warning::sjisWarnings(prop, isClassic, value->name(),
                                                 messages.dlgLblFlagName.value));
                    append(warning::sjisWarnings(prop, isClassic, value->on(),
                                                 messages.flagOnValue.value));
                    append(warning::sjisWarnings(prop, isClassic, value->off(),
                                                 messages.flagOffValue.value));
                    if (value->expandSPChars()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d121(__FILE__, __LINE__);
                        {
                            const auto& text = value->on();
                            const auto counter = value->useCounter();
                            const auto input_flags = value->flagsInText(true);
                            const auto input_steps = value->stepsInText(true);
                            const auto input_variants = value->variantsInText(true);
                            append(checkTextRes2(text, counter.get(), input_flags, input_steps, input_variants, {}, {}));
                        }
                        {
                            const auto& text = value->off();
                            const auto counter = value->useCounter();
                            const auto input_flags = value->flagsInText(false);
                            const auto input_steps = value->stepsInText(false);
                            const auto input_variants = value->variantsInText(false);
                            append(checkTextRes2(text, counter.get(), input_flags, input_steps, input_variants, {}, {}));
                        }
                    }
                }
                if (const auto* value = dynamic_cast<const flag::Step*>(&pathValue)) {
                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d127(__FILE__, __LINE__);
                    if (value->parent().get() == root.get() &&
                        prop.sys().isSystemVar(value->name())) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d128(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorSystemVariable.value,
                            value->name()));
                    }
                    if (isClassic && !is160 &&
                        value->count() !=
                            static_cast<std::uint32_t>(prop.looks().stepMaxCount())) {
                        result.push_back(utils::tryFormat(
                            messages.warningStepCount.value,
                            prop.looks().stepMaxCount()));
                    }
                    if (value->expandSPChars() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "2") && !is160) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d134(__FILE__, __LINE__);
                        result.push_back(messages.warningExpandSPChars.value);
                    }
                    if (value->useCounter() &&
                        dynamic_cast<const summary::Summary*>(
                            value->useCounter()->owner().get()) &&
                        !types::isDefaultVariableInitialization(
                            value->initialization()) &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d137(__FILE__, __LINE__);
                        result.push_back(messages.warningVariableInitialization);
                    }
                    append(warning::sjisWarnings(prop, isClassic, value->name(),
                                                 messages.dlgLblStepName.value));
                    for (const auto& text : value->values()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d141(__FILE__, __LINE__);
                        append(warning::sjisWarnings(prop, isClassic, text,
                                                     messages.stepValueForWarning.value));
                    }
                    if (value->expandSPChars()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d144(__FILE__, __LINE__);
                        const auto count = value->count();
                        for (std::uint32_t i = 0; i < count; ++i) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d145(__FILE__, __LINE__);
                            // Dはこれらの引数を左から右へ評価する。呼び
                            // 出し中は値全体の結果とカウンターを生存させ
                            // 、既存の添字位置の保持オブジェクトを読む。
                            const auto currentValues = value->values();
                            const auto& text = currentValues[i];
                            const auto counter = value->useCounter();
                            const auto flags = value->flagsInText(i);
                            const auto steps = value->stepsInText(i);
                            const auto variants = value->variantsInText(i);
                            append(checkTextRes2(text, counter.get(), flags,
                                                 steps, variants,
                                                 std::span<const lib::String>{},
                                                 std::span<const char>{}));
                        }
                    }
                }
                if (dynamic_cast<const flag::Variant*>(&pathValue)) {
                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d150(__FILE__, __LINE__);
                    if (!prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d151(__FILE__, __LINE__);
                        result.push_back(messages.warningVariant.value);
                    }
                }

                if (const auto* value =
                        dynamic_cast<const event::EventTree*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d156(__FILE__, __LINE__);
                    if (value->fireEveryRound() &&
                        !prop.targetVersion("1.50", targetVersion)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d157(__FILE__, __LINE__);
                        result.push_back(messages.warningEveryRound.value);
                    }
                    if (value->fireRoundEnd() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d160(__FILE__, __LINE__);
                        result.push_back(messages.warningRoundEnd.value);
                    }
                    if (value->fireRound0() &&
                        !prop.targetVersion("1.50", targetVersion)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d163(__FILE__, __LINE__);
                        result.push_back(messages.warningRound0.value);
                    }
                    if (value->keyCodeMatchingType() == types::MatchingType::And &&
                        !prop.targetVersion("1.50", targetVersion)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d166(__FILE__, __LINE__);
                        result.push_back(messages.warningKeyCodeMatchingTypeAnd.value);
                    }
                    if (!value->keyCodes().empty() &&
                        prop.sys().convFireKeyCode(value->keyCodes().front()) ==
                            "MatchingType=All") {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d169(__FILE__, __LINE__);
                        result.push_back(messages.searchErrorKeyCodeMatchingAll.value);
                    }
                    const bool typedKeyCode =
                        dynamic_cast<const area::EnemyCard*>(value->owner()) ||
                        dynamic_cast<const area::PlayerCardEvents*>(value->owner());
                    for (const auto& keyCode : value->keyCodes()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d173(__FILE__, __LINE__);
                        append(warning::sjisWarnings(prop, isClassic,
                                                     keyCode.keyCode,
                                                     messages.keyCode.value));
                        // HasNot以外のキーに対するtargetVersionの診断呼
                        // び出しも含め、種別より先にバージョンを調べるDの順序を維持する。
                        if (typedKeyCode &&
                            !prop.targetVersion("1.50", targetVersion)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d175(__FILE__, __LINE__);
                            if (keyCode.kind == types::FKCKind::HasNot) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d176(__FILE__, __LINE__);
                                result.push_back(messages.warningHasNotKeyCode.value);
                            }
                        }
                    }
                    bool ownerExempt =
                        dynamic_cast<const area::Package*>(value->owner()) != nullptr ||
                        dynamic_cast<const card::EffectCard*>(value->owner()) != nullptr;
                    if (!ownerExempt) {
                        if (auto* adapter = dynamic_cast<event::AbstractEventTreeOwner*>(value->owner())) {
                            // DのEffectCard.CETO.conは外側のカードを返
                            // す。ネイティブのメンバーアダプターは、既存の空パスの基点か
                            // らその同一性を公開する。この警告の除外判定だけを解決する。
                            const auto connected = adapter->event::AbstractEventTreeOwner::findCWXPath({});
                            ownerExempt = dynamic_cast<const card::EffectCard*>(connected.get()) != nullptr;
                        }
                    }
                    if (!ownerExempt) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d181(__FILE__, __LINE__);
                        if (!value->fireEnter() &&
                            !value->fireEscape() && !value->fireLose() &&
                            !value->fireEveryRound() && !value->fireRoundEnd() &&
                            !value->fireRound0() && value->rounds().empty() &&
                            value->keyCodes().empty()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d182(__FILE__, __LINE__);
                            result.push_back(messages.warningNoIgnition.value);
                        }
                    }
                }
                if (const auto* value =
                        dynamic_cast<const area::PlayerCardEvents*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d188(__FILE__, __LINE__);
                    if (!value->trees().empty() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d189(__FILE__, __LINE__);
                        result.push_back(messages.warningPlayerCardEvents.value);
                    }
                }
                if (const auto* value =
                        dynamic_cast<const background::BgImage*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d387(__FILE__, __LINE__);
                    if (!hasFlag(value->flag(), value->useCounter())) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d388(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorFlagNotFound.value,
                            value->flag()));
                    }
                    if (!value->cellName().empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d391(__FILE__, __LINE__);
                        append(warning::sjisWarnings(
                            prop, isClassic, value->cellName(),
                            messages.bgImageCellName.value));
                        if (!prop.isTargetVersion(isClassic, wsnVersion, "1") &&
                            !is160) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d393(__FILE__, __LINE__);
                            result.push_back(messages.warningBgImageCellName.value);
                        }
                    }
                    if (value->layer() != background::BgImage::LAYER_BACK_CELL &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "1") &&
                        !(is160 && value->layer() == types::LAYER_FORE_CELL)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d397(__FILE__, __LINE__);
                        if (is160) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d398(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.warningLayerV7.value,
                                types::LAYER_FORE_CELL));
                        } else {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d400(__FILE__, __LINE__);
                            result.push_back(messages.warningLayer.value);
                        }
                    }
                }
                if (const auto* value =
                        dynamic_cast<const background::ImageCell*>(&pathValue)) {
                    if (value->path().empty()) {
                        result.push_back(messages.searchErrorNoImage.value);
                    } else if (utils::isBinImg(value->path())) {
                        if (!is160) {
                            result.push_back(messages.warningBgImageIncluded.value);
                        }
                    } else if (skin.findPath(
                                   value->path(), skin.extImage(), skin.tableDirs(),
                                   summ ? summ->scenarioPath() : lib::String{},
                                   wsnVersion,
                                   skin.wsnTableDirs(wsnVersion)).empty()) {
                        result.push_back(utils::tryFormat(
                            messages.searchErrorImageNotFound.value,
                            utils::encodePath(value->path())));
                    }
                    if (!value->path().empty()) {
                        append(skin::Skin::warningImage(
                            prop, value->path(), isClassic, false,
                            targetVersion));
                    }
                    if (value->smoothing() != types::Smoothing::Default &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                        result.push_back(messages.warningBgImageSmoothing.value);
                    }
                }
                if (const auto* value =
                        dynamic_cast<const background::TextCell*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d425(__FILE__, __LINE__);
                    {
                        const auto& text = value->text();
                        const auto input_flags = value->flagsInText();
                        const auto input_steps = value->stepsInText();
                        const auto input_variants = value->variantsInText();
                        append(checkTextRes2(text, nullptr, input_flags, input_steps, input_variants, {}, {}));
                    }
                    if (!prop.targetVersion("1.50", targetVersion)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d427(__FILE__, __LINE__);
                        result.push_back(messages.warningTextCell.value);
                    }
                    if (isClassic) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d430(__FILE__, __LINE__);
                        if (value->updateType() != types::UpdateType::Fixed) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d431(__FILE__, __LINE__);
                            result.push_back(messages.warningUpdateTypeNotFixed.value);
                        }
                    } else {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d434(__FILE__, __LINE__);
                        if (value->updateType() != types::UpdateType::Variables &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d435(__FILE__, __LINE__);
                            result.push_back(messages.warningUpdateType.value);
                        }
                    }
                    if (value->antialias() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d439(__FILE__, __LINE__);
                        result.push_back(messages.warningAntialiasedTextCell.value);
                    }
                }
                if (const auto* value =
                        dynamic_cast<const background::ColorCell*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d444(__FILE__, __LINE__);
                    if (!prop.targetVersion("1.50", targetVersion)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d445(__FILE__, __LINE__);
                        result.push_back(messages.warningColorCell.value);
                    }
                    if (isClassic &&
                        value->blendMode() != types::BlendMode::Normal &&
                        value->gradientDir() == types::GradientDir::None &&
                        value->color1().a != 255) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d448(__FILE__, __LINE__);
                        result.push_back(
                            messages.warningColorCellNoGradientAlphaBlend160.value);
                    }
                }
                if (const auto* value =
                        dynamic_cast<const background::PCCell*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d453(__FILE__, __LINE__);
                    if (!prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d454(__FILE__, __LINE__);
                        if (is160) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d455(__FILE__, __LINE__);
                            if (value->expand()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d456(__FILE__, __LINE__);
                                result.push_back(messages.warningExpandedPCCell.value);
                            }
                        } else {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d459(__FILE__, __LINE__);
                            result.push_back(messages.warningPCCell.value);
                        }
                    }
                    if (value->smoothing() != types::Smoothing::Default &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d463(__FILE__, __LINE__);
                        result.push_back(messages.warningBgImageSmoothing.value);
                    }
                }
                if (const auto* value =
                        dynamic_cast<const area::AbstractArea*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d468(__FILE__, __LINE__);
                    append(warning::sjisWarnings(prop, isClassic, value->name(),
                                                 messages.name.value));
                }
                if (const auto* value =
                        dynamic_cast<const area::Battle*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d472(__FILE__, __LINE__);
                    if (!value->possibleToRunAway() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "3")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d473(__FILE__, __LINE__);
                        result.push_back(messages.warningPossibleToRunAway.value);
                    }
                    if (!value->possibleToRunAway()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d476(__FILE__, __LINE__);
                        for (const auto& tree : value->trees()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d477(__FILE__, __LINE__);
                            if (tree && tree->fireEscape()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d478(__FILE__, __LINE__);
                                result.push_back(messages.warningNoIgniteRunAway.value);
                                break;
                            }
                        }
                    }
                    if (!value->music().empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d484(__FILE__, __LINE__);
                        append(skin::Skin::warningBGM(
                            prop, value->music(), isClassic, targetVersion));
                        if (skin.findPath(
                                value->music(), skin.extBgm(), skin.bgmDirs(),
                                summ ? summ->scenarioPath() : lib::String{},
                                wsnVersion,
                                skin.wsnMusicDirs(wsnVersion)).empty()) {
                                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d486(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.searchErrorBGMNotFound.value,
                                utils::encodePath(value->music())));
                        }
                        if (value->volume() != 100 &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d489(__FILE__, __LINE__);
                            result.push_back(messages.warningVolume.value);
                        }
                        if (value->loopCount() != 0 &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d492(__FILE__, __LINE__);
                            result.push_back(messages.warningLoopCount.value);
                        }
                        if (value->fadeIn() != 0 &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d495(__FILE__, __LINE__);
                            result.push_back(messages.warningFadeIn.value);
                        }
                    }
                    if (value->continueBGM() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "3")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d499(__FILE__, __LINE__);
                        result.push_back(messages.warningContinueBGM.value);
                    }
                }
                if (const auto* value =
                        dynamic_cast<const area::MenuCard*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d504(__FILE__, __LINE__);
                    append(warning::sjisWarnings(prop, isClassic, value->name(),
                                                 messages.name.value));
                    append(warning::sjisWarnings(prop, isClassic, value->desc(),
                                                 messages.desc.value));
                    if (value->expandSPChars() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4") &&
                        !is160) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d507(__FILE__, __LINE__);
                        result.push_back(
                            messages.warningExpandSPCharsInMenuCardName.value);
                    }
                    if (value->expandSPChars()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d510(__FILE__, __LINE__);
                        {
                            const auto& text = value->name();
                            const auto input_flags = value->flagsInText();
                            const auto input_steps = value->stepsInText();
                            const auto input_variants = value->variantsInText();
                            append(checkTextRes2(text, nullptr, input_flags, input_steps, input_variants, {}, {}));
                        }
                    }
                    if (!value->cardGroup().empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d513(__FILE__, __LINE__);
                        if (!prop.isTargetVersion(isClassic, wsnVersion, "3") &&
                            !is160) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d514(__FILE__, __LINE__);
                            result.push_back(messages.warningCardGroup.value);
                        }
                    }
                    if (value->animationSpeed() != -1) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d518(__FILE__, __LINE__);
                        if (!prop.isTargetVersion(isClassic, wsnVersion, "4") &&
                            !(is160 && value->animationSpeed() <= 1)) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d519(__FILE__, __LINE__);
                            result.push_back(messages.warningCardAnimationSpeed.value);
                        }
                    }
                    addCardImages(value->paths(), false);
                    if (!hasFlag(value->flag(), value->useCounter())) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d524(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorFlagNotFound.value,
                            value->flag()));
                    }
                }
                if (const auto* value =
                        dynamic_cast<const area::EnemyCard*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d529(__FILE__, __LINE__);
                    if (value->id() == 0U) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d530(__FILE__, __LINE__);
                        result.push_back(messages.searchErrorNoCast.value);
                    } else if (!summ || !summ->cwCast(value->id())) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d533(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorCastNotFound.value, value->id()));
                    }
                    if (!hasFlag(value->flag(), value->useCounter())) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d536(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorFlagNotFound.value, value->flag()));
                    }
                    if (value->isOverrideName() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4") &&
                        !is160) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d539(__FILE__, __LINE__);
                        result.push_back(messages.warningOverrideEnemyCardName.value);
                    }
                    {
                        const auto& text = value->overrideName();
                        const auto input_flags = value->flagsInText();
                        const auto input_steps = value->stepsInText();
                        const auto input_variants = value->variantsInText();
                        append(checkTextRes2(text, nullptr, input_flags, input_steps, input_variants, {}, {}));
                    }
                    const auto overrideImages = value->overrideImages();
                    const bool classic160PcImage = is160 && overrideImages.size() == 1U &&
                        overrideImages.front() &&
                        overrideImages.front()->type == types::CardImageType::PCNumber;
                    if (value->isOverrideImage() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4") &&
                        !classic160PcImage) {
                        result.push_back(messages.warningOverrideEnemyCardImage.value);
                    }
                    if (!value->cardGroup().empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d546(__FILE__, __LINE__);
                        if (!prop.isTargetVersion(isClassic, wsnVersion, "3")) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d547(__FILE__, __LINE__);
                            result.push_back(messages.warningCardGroup.value);
                        }
                    }
                    if (value->animationSpeed() != -1) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d551(__FILE__, __LINE__);
                        if (!prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d552(__FILE__, __LINE__);
                            result.push_back(messages.warningCardAnimationSpeed.value);
                        }
                    }
                    bool warnedNoAction = false;
                    if (isClassic) {
                        const auto cast = summ ? summ->cwCast(value->id())
                                               : lib::Owner<const card::CastCard>{};
                        const auto items = cast ? cast->items()
                                                : decltype(cast->items()){};
                        const auto exchangeName = skin.actionCardName(
                            prop.sys(), types::ActionCardType::Exchange);
                        const auto baseItem = summ && !items.empty()
                            ? summ->baseCard(items.front())
                            : lib::Owner<const card::ItemCard>{};
                        const bool hasExchange = baseItem &&
                                                 baseItem->name() == exchangeName;
                        if (value->action(types::ActionCardType::Exchange) &&
                            hasExchange) {
                            result.push_back(utils::tryFormat(
                                messages.warningExchangeIsItemInClassic.value,
                                exchangeName));
                        } else if (!value->action(types::ActionCardType::Exchange) &&
                                   !hasExchange) {
                            result.push_back(messages.warningNoActionCard.value);
                            warnedNoAction = true;
                        }
                    }
                    if (!warnedNoAction) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d567(__FILE__, __LINE__);
                        // C++のローカルラムダで、Dの入れ子のcheckActionCardType関数を表す。
                        const auto checkActionCardType = [&](const types::ActionCardType type) -> bool {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d568(__FILE__, __LINE__);
                            if (type == types::ActionCardType::Exchange && isClassic) return true;
                            if (type == types::ActionCardType::RunAway) return true;
                            if (!value->action(type) &&
                                !prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d571(__FILE__, __LINE__);
                                result.push_back(messages.warningNoActionCard.value);
                                return false;
                            }
                            return true;
                        };
                        constexpr std::array actionTypes{
                            types::ActionCardType::Exchange,
                            types::ActionCardType::Attack,
                            types::ActionCardType::PowerfulAttack,
                            types::ActionCardType::CriticalAttack,
                            types::ActionCardType::Feint,
                            types::ActionCardType::Defense,
                            types::ActionCardType::Distance,
                            types::ActionCardType::Confuse,
                            types::ActionCardType::RunAway,
                        };
                        for (const auto type : actionTypes) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d577(__FILE__, __LINE__);
                            if (!checkActionCardType(type)) break;
                        }
                    }
                    addCardImages(overrideImages, false);
                }
                if (const auto* value =
                        dynamic_cast<const area::AbstractSpCard*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d584(__FILE__, __LINE__);
                    append(warning::sjisWarnings(prop, isClassic,
                                                 value->cardGroup(),
                                                 messages.cardGroup.value));
                    if (value->layer() != types::LAYER_MENU_CARD &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d587(__FILE__, __LINE__);
                        result.push_back(messages.warningLayer.value);
                    }
                }
                const auto addMotions = [&](const auto& motions) {
                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d308(__FILE__, __LINE__);
                    lib::Dictionary<lib::String, bool> motionWarnings;
                    const auto addMotionWarning = [&](lib::String text) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d311(__FILE__, __LINE__);
                        if (!motionWarnings.contains(text)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d312(__FILE__, __LINE__);
                            result.push_back(text);
                            motionWarnings[text] = true;
                        }
                    };
                    for (const auto& effect : motions) {
                        if (!effect) continue;
                        if (effect->type() == types::MType::CancelAction &&
                            !prop.targetVersion("1.50", targetVersion)) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d317(__FILE__, __LINE__);
                            addMotionWarning(utils::tryFormat(
                                messages.warningUnknownMotion.value,
                                messages.motionName(effect->type()), "1.50"));
                        }
                        if (effect->type() == types::MType::NoEffect &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d320(__FILE__, __LINE__);
                            addMotionWarning(utils::tryFormat(
                                messages.warningUnknownMotion.value,
                                messages.motionName(effect->type()), "2"));
                        }
                        if (effect->type() == types::MType::SummonBeast) {
                            const auto beast = effect->beast();
                            if (!beast) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d323(__FILE__, __LINE__);
                                addMotionWarning(messages.searchErrorNoBeast.value);
                            } else if (beast->linkId() != 0U &&
                                       (!summ || !summ->beast(beast->linkId()))) {
                                           ::cwxeditor::cwx::perf::TraceScope trace_warning_d326(__FILE__, __LINE__);
                                addMotionWarning(utils::tryFormat(
                                    messages.searchErrorLinkIdBeastNotFound.value,
                                    beast->linkId()));
                            }
                        }
                        if ((effect->type() == types::MType::GetSkillPower ||
                             effect->type() == types::MType::LoseSkillPower) &&
                            effect->damageType() != types::DamageType::Max &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d330(__FILE__, __LINE__);
                            addMotionWarning(messages.warningSkillPowerWithFixedValue.value);
                        }
                    }
                };
                if (const auto* value =
                        dynamic_cast<const card::CastCard*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d194(__FILE__, __LINE__);
                    lib::Array<types::Status> statuses;
                    if (value->life() == 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d218(__FILE__, __LINE__);
                        statuses.push_back(types::Status::Unconscious);
                    } else if (value->life() <= value->lifeMax() / 5) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d220(__FILE__, __LINE__);
                        statuses.push_back(types::Status::HeavyInjured);
                    } else if (value->life() < value->lifeMax()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d222(__FILE__, __LINE__);
                        statuses.push_back(types::Status::Injured);
                    } else {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d224(__FILE__, __LINE__);
                        statuses.push_back(types::Status::Fine);
                    }
                    constexpr std::array enhancements{types::Enhance::Action, types::Enhance::Avoid,
                        types::Enhance::Resist, types::Enhance::Defense};
                    constexpr std::array down{types::Status::DownAction, types::Status::DownAvoid,
                        types::Status::DownResist, types::Status::DownDefense};
                    constexpr std::array up{types::Status::UpAction, types::Status::UpAvoid,
                        types::Status::UpResist, types::Status::UpDefense};
                    for (std::size_t i = 0; i < enhancements.size(); ++i) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d227(__FILE__, __LINE__);
                        if (value->enhanceRound(enhancements[i]) > 0 && value->enhance(enhancements[i]) != 0) {
                            if (value->enhance(enhancements[i]) < 0) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d229(__FILE__, __LINE__);
                                statuses.push_back(down[i]);
                            } else {
                                statuses.push_back(up[i]);
                            }
                        }
                    }
                    if (value->paralyze() > 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d261(__FILE__, __LINE__);
                        statuses.push_back(types::Status::Paralyze);
                    }
                    if (value->poison() > 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d264(__FILE__, __LINE__);
                        statuses.push_back(types::Status::Poison);
                    }
                    if (value->bindRound() > 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d267(__FILE__, __LINE__);
                        statuses.push_back(types::Status::Bind);
                    }
                    if (value->silenceRound() > 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d270(__FILE__, __LINE__);
                        statuses.push_back(types::Status::Silence);
                    }
                    if (value->faceUpRound() > 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d273(__FILE__, __LINE__);
                        statuses.push_back(types::Status::FaceUp);
                    }
                    if (value->antiMagicRound() > 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d276(__FILE__, __LINE__);
                        statuses.push_back(types::Status::AntiMagic);
                    }
                    if (value->mentalityRound() > 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d279(__FILE__, __LINE__);
                        switch (value->mentality()) {
                        case types::Mentality::Sleep: statuses.push_back(types::Status::Sleep); break;
                        case types::Mentality::Confuse: statuses.push_back(types::Status::Confuse); break;
                        case types::Mentality::Overheat: statuses.push_back(types::Status::Overheat); break;
                        case types::Mentality::Brave: statuses.push_back(types::Status::Brave); break;
                        case types::Mentality::Panic: statuses.push_back(types::Status::Panic); break;
                        case types::Mentality::Normal: break;
                        }
                    }
                    for (const auto& coupon : value->coupons()) {
                        if (!coupon) continue;
                        const auto name = coupon->coupon();
                        append(warning::sjisWarnings(prop, isClassic, name,
                                                     messages.history.value));
                        if (name.starts_with(prop.sys().couponSystem()) &&
                            name != prop.sys().levelLimit() && name != prop.sys().ep() &&
                            !prop.sys().isGene(name)) {
                            result.push_back(messages.warningSystemCouponForHistory.value);
                        }
                    }
                    for (const auto& item : value->skills()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d201(__FILE__, __LINE__);
                        if (item && item->linkId() != 0U &&
                            (!summ || !summ->skill(item->linkId()))) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d202(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.searchErrorLinkIdSkillNotFound.value,
                                item->linkId()));
                        }
                    }
                    for (const auto& item : value->items()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d206(__FILE__, __LINE__);
                        if (item && item->linkId() != 0U &&
                            (!summ || !summ->item(item->linkId()))) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d207(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.searchErrorLinkIdItemNotFound.value,
                                item->linkId()));
                        }
                    }
                    for (const auto& item : value->beasts()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d211(__FILE__, __LINE__);
                        if (item && item->linkId() != 0U &&
                            (!summ || !summ->beast(item->linkId()))) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d212(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.searchErrorLinkIdBeastNotFound.value,
                                item->linkId()));
                        }
                    }
                    // Dは状態の不整合より先にクーポン・リンク切れ
                    // の警告群を出し、一覧の順序と重複を維持する。
                    append(warning::warningInconsistency(prop, statuses));
                }
                if (const auto* value =
                        dynamic_cast<const card::Card*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d303(__FILE__, __LINE__);
                    append(warning::sjisWarnings(prop, isClassic, value->name(),
                                                 messages.name.value));
                    append(warning::sjisWarnings(prop, isClassic, value->desc(),
                                                 messages.desc.value));
                    addCardImages(value->paths(), true);
                }
                if (const auto* value =
                        dynamic_cast<const card::EffectCard*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d336(__FILE__, __LINE__);
                    append(warning::sjisWarnings(prop, isClassic,
                                                 value->scenario(),
                                                 messages.sourceScenario.value));
                    append(warning::sjisWarnings(prop, isClassic, value->author(),
                                                 messages.sourceAuthor.value));
                    addMotions(value->motions());
                    if (const auto* beast = dynamic_cast<const card::BeastCard*>(value)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d341(__FILE__, __LINE__);
                        if (beast->showStyle() != types::ShowStyle::Center &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d342(__FILE__, __LINE__);
                            result.push_back(messages.warningShowStyleForBeastCard.value);
                        }
                        const auto& conditions = beast->invocationCondition();
                        if (!(conditions.size() == 1U &&
                              conditions.front() == types::Status::Alive) &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "3")) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d345(__FILE__, __LINE__);
                            result.push_back(messages.warningInvocationCondition.value);
                        }
                        if (!beast->removeWithUnconscious() &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "3")) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d348(__FILE__, __LINE__);
                            result.push_back(messages.warningRemoveWithUnconscious.value);
                        }
                        if (beast->removeWithUnconscious() &&
                            std::find(conditions.begin(), conditions.end(),
                                      types::Status::Unconscious) != conditions.end()) {
                                          ::cwxeditor::cwx::perf::TraceScope trace_warning_d351(__FILE__, __LINE__);
                            result.push_back(messages.warningUnconsciousCondition.value);
                        }
                    }
                    // ネイティブのEffectCardのパス取得は、所有する文字列を返す。
                    // 確保を伴う取得を繰り返さず、Dのパス分岐ごとに一つの値を保持する。
                    if (const auto soundPath = value->soundPath1(); !soundPath.empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d355(__FILE__, __LINE__);
                        append(skin::Skin::warningSE(prop, soundPath, isClassic,
                                                     targetVersion));
                        if (skin.findPath(
                                soundPath, skin.extSound(), skin.seDirs(),
                                summ ? summ->scenarioPath() : lib::String{},
                                wsnVersion,
                                skin.wsnSoundDirs(wsnVersion)).empty()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d357(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.searchErrorSENotFound.value,
                                utils::encodePath(soundPath)));
                        }
                        if ((value->volume1() != 100 || value->volume2() != 100) &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d360(__FILE__, __LINE__);
                            result.push_back(messages.warningVolume.value);
                        }
                    }
                    if (const auto soundPath = value->soundPath2(); !soundPath.empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d364(__FILE__, __LINE__);
                        append(skin::Skin::warningSE(prop, soundPath, isClassic,
                                                     targetVersion));
                        if (skin.findPath(
                                soundPath, skin.extSound(), skin.seDirs(),
                                summ ? summ->scenarioPath() : lib::String{},
                                wsnVersion,
                                skin.wsnSoundDirs(wsnVersion)).empty()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d366(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.searchErrorSENotFound.value,
                                utils::encodePath(soundPath)));
                        }
                        if ((value->loopCount1() != 1 || value->loopCount2() != 1) &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d369(__FILE__, __LINE__);
                            result.push_back(messages.warningLoopCount.value);
                        }
                    }
                    if (prop.sys().isRunAway(value->keyCodes())) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d373(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.warningRunAwayCard.value,
                            prop.sys().runAway()));
                    }
                    if (isClassic && !is160 &&
                        value->keyCodes().size() >
                            static_cast<std::size_t>(
                                prop.looks().keyCodesMaxLegacy())) {
                                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d376(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.warningKeyCodeCount.value,
                            prop.looks().keyCodesMaxLegacy()));
                    }
                    for (const auto& keyCode : value->keyCodes()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d379(__FILE__, __LINE__);
                        append(warning::sjisWarnings(prop, isClassic, keyCode,
                                                     messages.keyCode.value));
                    }
                    const auto localRoot = value->flagDirRoot();
                    if (localRoot &&
                        (localRoot->hasFlag() || localRoot->hasStep() ||
                         localRoot->hasVariant()) &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                        result.push_back(
                            messages.warningLocalVariablesOfEffectCard.value);
                    }
                }
                if (const auto* value =
                        dynamic_cast<const event::Content*>(&pathValue)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d593(__FILE__, __LINE__);
                    addMotions(value->motionsRef());
                    const auto detail = value->detail();
                    const auto parent = value->parent();
                    const bool customTalkParent =
                        parent && parent->type() == types::CType::CustomXmlContent &&
                        (parent->xmlType() == "TalkMessage" ||
                         parent->xmlType() == "TalkDialog");
                    if (!parent || parent->detail().nextType == types::CNextType::Text ||
                        parent->detail().nextType == types::CNextType::Coupon ||
                        customTalkParent) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d596(__FILE__, __LINE__);
                        append(warning::sjisWarnings(
                            prop, isClassic, value->name(),
                            messages.contentNameForWarning.value));
                    }
                    if (parent) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d599(__FILE__, __LINE__);
                        if (parent->type() != types::CType::CustomXmlContent) {
                            const auto text = warning::warningNextCondition(
                                prop, parent->detail().nextType, value->name());
                            if (!text.empty()) result.push_back(text);
                        }
                    }
                    if (value->type() == types::CType::CheckFlag && parent &&
                        (parent->type() == types::CType::SetFlag ||
                         parent->type() == types::CType::ReverseFlag ||
                         parent->type() == types::CType::SubstituteFlag) &&
                        !value->flag().empty() &&
                        value->flag() == parent->flag()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d608(__FILE__, __LINE__);
                        result.push_back(messages.warningCheckFlagV7.value);
                    }
                    if (value->type() == types::CType::CheckStep && parent &&
                        (parent->type() == types::CType::SetStep ||
                         parent->type() == types::CType::SetStepUp ||
                         parent->type() == types::CType::SetStepDown ||
                         parent->type() == types::CType::SubstituteStep) &&
                        !value->step().empty() &&
                        value->step() == parent->step()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d611(__FILE__, __LINE__);
                        result.push_back(messages.warningCheckStepV7.value);
                    }
                    if (detail.owner && detail.nextType != types::CNextType::Text) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d614(__FILE__, __LINE__);
                        const bool customTalk =
                            value->type() == types::CType::CustomXmlContent &&
                            (value->xmlType() == "TalkMessage" ||
                             value->xmlType() == "TalkDialog");
                        if (!customTalk) {
                            lib::Dictionary<lib::String, bool> names;
                            for (const auto& child : value->next()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d621(__FILE__, __LINE__);
                                // ネイティブのnullの子を許容する動作は、Dの通常の
                                // 非null集合の範囲外の明示的な拡張として維持する。
                                if (!child) continue;
                                if (child->name().empty() &&
                                    value->type() != types::CType::BranchMultiCoupon) {
                                    continue;
                                }
                                if (child->type() == types::CType::CheckFlag ||
                                    child->type() == types::CType::CheckStep ||
                                    child->type() == types::CType::CheckVariant) {
                                    continue;
                                }
                                if (names.contains(child->name())) {
                                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d624(__FILE__, __LINE__);
                                    result.push_back(
                                        messages.searchErrorDupNextContent.value);
                                    break;
                                }
                                names[child->name()] = true;
                            }
                        }
                    }
                    if (parent &&
                        parent->detail().nextType == types::CNextType::Text) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d632(__FILE__, __LINE__);
                        const auto nameFlags = value->flagsInName();
                        const auto nameSteps = value->stepsInName();
                        const auto nameVariants = value->variantsInName();
                        const auto nameSpecials = value->namesInName();
                        if (!prop.targetVersion("1.50", targetVersion) &&
                            (!nameFlags.empty() || !nameSteps.empty() ||
                             !nameVariants.empty() || !nameSpecials.empty())) {
                                 ::cwxeditor::cwx::perf::TraceScope trace_warning_d637(__FILE__, __LINE__);
                            result.push_back(messages.warningSPCharsInSelections.value);
                        } else {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d639(__FILE__, __LINE__);
                            {
                                const auto& text = lib::StringView{};
                                const auto counter = value->useCounter();
                                const auto& input_flags = nameFlags;
                                const auto& input_steps = nameSteps;
                                const auto& input_variants = nameVariants;
                                append(checkTextRes2(text, counter.get(), input_flags, input_steps, input_variants, {}, {}));
                            }
                        }
                    }
                    if (parent &&
                        parent->detail().nextType == types::CNextType::Step &&
                        !parent->step().empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d643(__FILE__, __LINE__);
                        if (const auto step = flag::findVar<flag::Step>(
                                root, value->useCounter(), parent->step())) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d644(__FILE__, __LINE__);
                            try {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d645(__FILE__, __LINE__);
                                const auto branchValue = value->name() == prop.sys().evtChildDefault()
                                    ? std::int64_t{-1} : warningSignedBranchValue(value->name());
                                if (branchValue >= 0) {
                                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d647(__FILE__, __LINE__);
                                    if (step->count() <= static_cast<std::uint64_t>(branchValue)) {
                                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d648(__FILE__, __LINE__);
                                        result.push_back(utils::tryFormat(
                                            messages.searchErrorStepValueNotFound.value, branchValue));
                                    }
                                }
                            } catch (const std::bad_alloc&) {
                                // DのExceptionの捕捉は、メモリ確保のErrorを捕捉しない。
                                throw;
                            } catch (const std::exception&) {
                                result.push_back(utils::tryFormat(
                                    messages.searchErrorStepValueNotFound.value, value->name()));
                            }
                        }
                    }
                    if (parent && parent->detail().nextType == types::CNextType::IdArea) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d657(__FILE__, __LINE__);
                        try {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d658(__FILE__, __LINE__);
                            const auto branchValue = value->name() == prop.sys().evtChildDefault()
                                ? std::int64_t{-1} : warningSignedBranchValue(value->name());
                            if (branchValue >= 0) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d660(__FILE__, __LINE__);
                                if (!summ || !summ->area(static_cast<std::uint64_t>(branchValue))) {
                                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d661(__FILE__, __LINE__);
                                    result.push_back(utils::tryFormat(
                                        messages.searchErrorAreaNotFound.value, branchValue));
                                }
                            }
                        } catch (const std::bad_alloc&) {
                            // DのExceptionの捕捉は、メモリ確保のErrorを捕捉しない。
                            throw;
                        } catch (const std::exception&) {
                            // Dは不正なArea名にもBattle用のメッセージを使う。
                            result.push_back(utils::tryFormat(
                                messages.searchErrorBattleNotFound.value, value->name()));
                        }
                    }
                    if (parent && parent->detail().nextType == types::CNextType::IdBattle) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d669(__FILE__, __LINE__);
                        try {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d670(__FILE__, __LINE__);
                            const auto branchValue = value->name() == prop.sys().evtChildDefault()
                                ? std::int64_t{-1} : warningSignedBranchValue(value->name());
                            if (branchValue >= 0) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d672(__FILE__, __LINE__);
                                if (!summ || !summ->battle(static_cast<std::uint64_t>(branchValue))) {
                                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d673(__FILE__, __LINE__);
                                    result.push_back(utils::tryFormat(
                                        messages.searchErrorBattleNotFound.value, branchValue));
                                }
                            }
                        } catch (const std::bad_alloc&) {
                            // DのExceptionの捕捉は、メモリ確保のErrorを捕捉しない。
                            throw;
                        } catch (const std::exception&) {
                            // Dは不正なArea名にもBattle用のメッセージを使う。
                            result.push_back(utils::tryFormat(
                                messages.searchErrorBattleNotFound.value, value->name()));
                        }
                    }
                    const auto couponWarnings = [&](const lib::String& coupon,
                                                    bool getLose,
                                                    const lib::String& name) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d682(__FILE__, __LINE__);
                        append(warning::couponWarnings(
                            prop, isClassic, wsnVersion, targetVersion,
                            coupon, getLose, name));
                    };
                    if (value->type() == types::CType::TalkDialog) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d686(__FILE__, __LINE__);
                        if (!prop.targetVersion("1.50", targetVersion)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d687(__FILE__, __LINE__);
                            if (value->talkerNC() == types::Talker::Valued) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d688(__FILE__, __LINE__);
                                result.push_back(messages.warningValuedTalker.value);
                            }
                        }
                        const auto& dialogs = value->dialogs();
                        if (!dialogs.empty()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d692(__FILE__, __LINE__);
                            lib::Dictionary<lib::String, bool> flags;
                            lib::Dictionary<lib::String, bool> steps;
                            lib::Dictionary<lib::String, bool> variants;
                            lib::Dictionary<lib::String, bool> fonts;
                            lib::Dictionary<char, bool> colors;
                            for (std::size_t index = 0; index < dialogs.size(); ++index) {
                                const auto& dialog = dialogs[index];
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d698(__FILE__, __LINE__);
                                // ネイティブ側のnullの台詞項目を許容する動作を維持する。
                                if (!dialog) continue;
                                for (const auto& coupon : dialog->rCoupons()) {
                                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d699(__FILE__, __LINE__);
                                    couponWarnings(coupon, false, messages.toneCoupons.value);
                                }
                                {
                                    const auto& text = dialog->text();
                                    const auto counter = dialog->useCounter();
                                    const auto input_flags = dialog->flagsInText();
                                    const auto input_steps = dialog->stepsInText();
                                    const auto input_variants = dialog->variantsInText();
                                    const auto input_fonts = dialog->fontsInText();
                                    const auto input_colors = dialog->colorsInText();
                                    append(checkTextRes(text, counter.get(), input_flags, input_steps, input_variants, input_fonts, input_colors, flags, steps, variants, fonts, colors).noDup);
                                }
                                if (index + 1U < dialogs.size() &&
                                    dialog->rCoupons().empty()) {
                                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d705(__FILE__, __LINE__);
                                    result.push_back(
                                        messages.searchErrorNoRCouponsDialog.value);
                                }
                            }
                        }
                    }
                    {
                        const auto& text = value->text();
                        const auto counter = value->useCounter();
                        const auto input_flags = value->flagsInText();
                        const auto input_steps = value->stepsInText();
                        const auto input_variants = value->variantsInText();
                        const auto input_fonts = value->fontsInText();
                        const auto input_colors = value->colorsInText();
                        append(checkTextRes2(text, counter.get(), input_flags, input_steps, input_variants, input_fonts, input_colors));
                    }
                    const auto hasStart = [&]() {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d713(__FILE__, __LINE__);
                        // 切り離されたコンテントやnullの開始項目を許容するネイティブ動作
                        // を維持する。通常のDのコンテントは、ツリーと実際の開始項目を持つ。
                        if (const auto tree = value->tree()) {
                            for (const auto& start : tree->starts()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d714(__FILE__, __LINE__);
                                if (start && start->name() == value->start())
                                    return true;
                            }
                        }
                        return false;
                    };
                    {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d719(__FILE__, __LINE__);
                        warning::WarningList pendingWarnings;
                        lib::Dictionary<lib::String, bool> backgroundWarnings;
                        for (const auto& background : value->backgroundImages()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d722(__FILE__, __LINE__);
                            // Dの通常の対象範囲外では、ネイティブのnull項目を許容する動作を維持する。
                            if (!background) continue;
                            auto values = warning::warnings(
                                prop, skin, summ, *background, isClassic,
                                wsnVersion, targetVersion);
                            if (detail.use(types::CArg::IgnoreEffectBooster) &&
                                value->ignoreEffectBooster()) {
                                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d724(__FILE__, __LINE__);
                                if (const auto* image =
                                        dynamic_cast<const background::ImageCell*>(
                                            background.get())) {
                                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d725(__FILE__, __LINE__);
                                    const auto imagePath = image->path();
                                    // 字句上のパス処理だけを行う。資源を開かず、ドット
                                    // で始まるファイル名の拡張子の扱いをDに合わせる。
                                    const auto extensionUtf8 = std::filesystem::u8path(imagePath)
                                                                   .extension().u8string();
                                    const auto extension = enumutils::simpleLowerCase(lib::String(
                                        extensionUtf8.begin(), extensionUtf8.end()));
                                    if (extension == ".jpy1" || extension == ".jptx" ||
                                        extension == ".jpdc") {
                                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d727(__FILE__, __LINE__);
                                        values.push_back(messages
                                            .warningEffectBoosterFileWithReplaceBgImage
                                            .value);
                                    }
                                }
                            }
                            for (const auto& text : values) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d732(__FILE__, __LINE__);
                                if (!backgroundWarnings.contains(text)) {
                                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d733(__FILE__, __LINE__);
                                    pendingWarnings.push_back(text);
                                    backgroundWarnings[text] = true;
                                }
                            }
                        }
                        append(pendingWarnings);
                    }
                    // Systemの??Randomと??SelectedPlayerだけを
                    // 対象とする。後者にはDのicmpがU+017F（長いs）も受け付ける。
                    // 固定したPhobosの表では、他の非ASCIIのケースフォールド項目が
                    // どちらの記号にも一致しない。汎用のUnicodeのicmpではない。
                    const auto matchesWarningReservedToken = [](
                        const lib::StringView input, const lib::StringView token) {
                        // 途中で不一致になった後の不正UTF-8も含め、従来の
                        // simpleLowerCaseの検証契約を維持する。
                        enumutils::validateUtf8(input);
                        const auto asciiLower = [](const unsigned char byte) noexcept {
                            return byte >= 'A' && byte <= 'Z'
                                ? static_cast<unsigned char>(byte + ('a' - 'A')) : byte;
                        };
                        std::size_t inputOffset = 0;
                        std::size_t tokenOffset = 0;
                        while (inputOffset < input.size() && tokenOffset < token.size()) {
                            auto byte = static_cast<unsigned char>(input[inputOffset++]);
                            if (byte >= 0x80) {
                                if (byte != 0xc5 || inputOffset == input.size() ||
                                    static_cast<unsigned char>(input[inputOffset]) != 0xbf)
                                    return false;
                                ++inputOffset;
                                byte = 's';
                            }
                            if (asciiLower(byte) != asciiLower(
                                    static_cast<unsigned char>(token[tokenOffset++])))
                                return false;
                        }
                        return inputOffset == input.size() && tokenOffset == token.size();
                    };
                    if (!hasFlag(value->flag(), value->useCounter())) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d741(__FILE__, __LINE__);
                        if (!(value->type() == types::CType::SubstituteFlag &&
                              matchesWarningReservedToken(value->flag(),
                                               prop.sys().randomValue()))) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d743(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.searchErrorFlagNotFound.value,
                                value->flag()));
                        }
                    }
                    if (!hasStep(value->step(), value->useCounter())) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d747(__FILE__, __LINE__);
                        if (value->type() != types::CType::SubstituteStep ||
                            (!matchesWarningReservedToken(value->step(), prop.sys().randomValue()) &&
                             !matchesWarningReservedToken(value->step(), prop.sys().selectedPlayerCardNumber()))) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d749(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.searchErrorStepNotFound.value,
                                value->step()));
                        }
                        if (value->type() == types::CType::SubstituteStep &&
                            matchesWarningReservedToken(value->step(), prop.sys().selectedPlayerCardNumber()) &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "2") &&
                            !is160) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d752(__FILE__, __LINE__);
                            result.push_back(messages.warningSelectedPlayerValue.value);
                        }
                    }
                    if (!hasVariant(value->variant(), value->useCounter())) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d756(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorVariantNotFound.value,
                            value->variant()));
                    }
                    if (!hasFlag(value->flag2(), value->useCounter())) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d800(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorFlagNotFound.value,
                            value->flag2()));
                    }
                    if (!hasStep(value->step2(), value->useCounter())) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d803(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorStepNotFound.value,
                            value->step2()));
                    }
                    if (!value->flag().empty() &&
                        value->flag() == value->flag2()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d806(__FILE__, __LINE__);
                        result.push_back(messages.searchErrorSourceIsTarget.value);
                    }
                    if (!value->step().empty() &&
                        value->step() == value->step2()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d809(__FILE__, __LINE__);
                        result.push_back(messages.searchErrorSourceIsTarget.value);
                    }
                    if (detail.use(types::CArg::CouponNames)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d812(__FILE__, __LINE__);
                        if (value->couponNames().empty()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d813(__FILE__, __LINE__);
                            result.push_back(messages.searchErrorNoCoupon.value);
                        }
                        if (!prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d816(__FILE__, __LINE__);
                            if (value->couponNames().size() > 1U) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d817(__FILE__, __LINE__);
                                result.push_back(messages.warningBranchCouponMulti.value);
                            }
                        }
                        for (const auto& coupon : value->couponNames()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d821(__FILE__, __LINE__);
                            couponWarnings(coupon, false, messages.couponName.value);
                        }
                    }
                    if ((value->type() == types::CType::GetCoupon ||
                         value->type() == types::CType::LoseCoupon) &&
                        prop.sys().isCouponType(
                            value->coupon(), features::CouponType::System)) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d825(__FILE__, __LINE__);
                        couponWarnings(value->coupon(), true, messages.couponName.value);
                    }
                    if (detail.use(types::CArg::Coupons)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d828(__FILE__, __LINE__);
                        for (const auto& coupon : value->couponRecords()) {
                            if (!coupon) continue;
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d829(__FILE__, __LINE__);
                            couponWarnings(coupon->coupon(), false, messages.valued.value);
                        }
                    }
                    // Dのwarning.d::putCardImagesはFile画
                    // 像だけを検証する。ここでも型付きCardImageを維持する。Material/??Selected
                    // などの話者の疑似画像は拡張子を持たないため、warningImage()
                    // へ渡してはならない。
                    addCardImages(value->cardImages(), false);
                    // 型付きの音声ダイアログは、eventdialog.cppで解決さ
                    // れたMaterialSelectのパスを通じてこれらの診断を行う
                    // 。全体検証では引き続きContentに指定されたパスを検証する。
                    if (domain == warning::WarningDomain::ScenarioValidation &&
                        !value->bgmPath().empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d760(__FILE__, __LINE__);
                        append(skin::Skin::warningBGM(prop, value->bgmPath(), isClassic,
                                                      targetVersion));
                    }
                    if (domain == warning::WarningDomain::ScenarioValidation &&
                        !value->bgmPath().empty() && skin.findPath(
                            value->bgmPath(), skin.extBgm(), skin.bgmDirs(),
                            summ ? summ->scenarioPath() : lib::String{},
                            wsnVersion,
                            skin.wsnMusicDirs(wsnVersion)).empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d763(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorBGMNotFound.value,
                            utils::encodePath(value->bgmPath())));
                    }
                    if (domain == warning::WarningDomain::ScenarioValidation &&
                        !value->soundPath().empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d766(__FILE__, __LINE__);
                        append(skin::Skin::warningSE(prop, value->soundPath(), isClassic,
                                                      targetVersion));
                    }
                    if (domain == warning::WarningDomain::ScenarioValidation &&
                        !value->soundPath().empty() && skin.findPath(
                            value->soundPath(), skin.extSound(), skin.seDirs(),
                            summ ? summ->scenarioPath() : lib::String{},
                            wsnVersion,
                            skin.wsnSoundDirs(wsnVersion)).empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d769(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorSENotFound.value,
                            utils::encodePath(value->soundPath())));
                    }
                    if (value->area() != 0 && (!summ || !summ->area(value->area()))) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d772(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorAreaNotFound.value,
                            value->area()));
                    }
                    if (value->battle() != 0 &&
                        (!summ || !summ->battle(value->battle()))) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d775(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorBattleNotFound.value,
                            value->battle()));
                    }
                    if (value->packages() != 0 &&
                        (!summ || !summ->cwPackage(value->packages()))) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d778(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorPackageNotFound.value,
                            value->packages()));
                    }
                    if (value->casts() != 0 &&
                        (!summ || !summ->cwCast(value->casts()))) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d781(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorCastNotFound.value,
                            value->casts()));
                    }
                    if (value->item() != 0 &&
                        !(value->range() == types::Range::SelectedCard &&
                          value->type() == types::CType::LoseItem) &&
                        (!summ || !summ->item(value->item()))) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d784(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorItemNotFound.value,
                            value->item()));
                    }
                    if (value->skill() != 0 &&
                        !(value->range() == types::Range::SelectedCard &&
                          value->type() == types::CType::LoseSkill) &&
                        (!summ || !summ->skill(value->skill()))) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d787(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorSkillNotFound.value,
                            value->skill()));
                    }
                    if (value->beast() != 0 &&
                        !(value->range() == types::Range::SelectedCard &&
                          value->type() == types::CType::LoseBeast) &&
                        (!summ || !summ->beast(value->beast()))) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d790(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorBeastNotFound.value,
                            value->beast()));
                    }
                    if (value->info() != 0 &&
                        (!summ || !summ->info(value->info()))) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d793(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorInfoNotFound.value,
                            value->info()));
                    }
                    if (!value->start().empty() && !hasStart()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d796(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.searchErrorStartNotFound.value,
                            value->start()));
                    }
                    if (value->levelMin() > value->levelMax()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d833(__FILE__, __LINE__);
                        result.push_back(messages.searchErrorReversalLevel.value);
                    }
                    if (value->type() == types::CType::BranchRound) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d836(__FILE__, __LINE__);
                        path::CWXPath* current = const_cast<event::Content*>(value);
                        lib::Owner<path::CWXPath> currentOwner;
                        while (current) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d838(__FILE__, __LINE__);
                            if (dynamic_cast<area::Area*>(current)) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d839(__FILE__, __LINE__);
                                result.push_back(
                                    messages.searchErrorBranchRoundInArea.value);
                                break;
                            }
                            // 各弱参照の親の対象を、訪問中は所有して生存させる
                            // 。最初のContentは呼び出し側が保持済み。
                            currentOwner = current->cwxParent().lock();
                            current = currentOwner.get();
                        }
                    }
                    if (domain == warning::WarningDomain::ScenarioValidation &&
                        value->bgmChannel() != 0 &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d846(__FILE__, __LINE__);
                        result.push_back(messages.warningChannel.value);
                    }
                    if (domain == warning::WarningDomain::ScenarioValidation &&
                        value->bgmFadeIn() != 0 &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d849(__FILE__, __LINE__);
                        result.push_back(messages.warningFadeIn.value);
                    }
                    if (domain == warning::WarningDomain::ScenarioValidation &&
                        !value->bgmPath().empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d852(__FILE__, __LINE__);
                        if (value->bgmVolume() != 100 &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d853(__FILE__, __LINE__);
                            result.push_back(messages.warningVolume.value);
                        }
                        if (value->bgmLoopCount() != 0 &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d856(__FILE__, __LINE__);
                            result.push_back(messages.warningLoopCount.value);
                        }
                    }
                    if (domain == warning::WarningDomain::ScenarioValidation &&
                        !value->soundPath().empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d860(__FILE__, __LINE__);
                        if (value->soundFadeIn() != 0 &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d861(__FILE__, __LINE__);
                            result.push_back(messages.warningFadeIn.value);
                        }
                        if (value->soundVolume() != 100 &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d864(__FILE__, __LINE__);
                            result.push_back(messages.warningVolume.value);
                        }
                        if (value->soundLoopCount() != 1 &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d867(__FILE__, __LINE__);
                            result.push_back(messages.warningLoopCount.value);
                        }
                        if (value->soundChannel() != 0 &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d870(__FILE__, __LINE__);
                            result.push_back(messages.warningChannel.value);
                        }
                    }
                    // Dのwarning.d874..903と同じ短絡評価順と、字句上の所有元を使う。
                    if (detail.use(types::CArg::Area) && value->area() == 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d874(__FILE__, __LINE__);
                        result.push_back(messages.searchErrorNoArea.value);
                    }
                    if (detail.use(types::CArg::Battle) && value->battle() == 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d877(__FILE__, __LINE__);
                        result.push_back(messages.searchErrorNoBattle.value);
                    }
                    if (detail.use(types::CArg::Package) && value->packages() == 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d880(__FILE__, __LINE__);
                        result.push_back(messages.searchErrorNoPackage.value);
                    }
                    if (detail.use(types::CArg::Cast) && value->casts() == 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d883(__FILE__, __LINE__);
                        result.push_back(messages.searchErrorNoCast.value);
                    }
                    if (detail.use(types::CArg::Item) && value->item() == 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d886(__FILE__, __LINE__);
                        if (!(value->range() == types::Range::SelectedCard &&
                              value->type() == types::CType::LoseItem)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d887(__FILE__, __LINE__);
                            result.push_back(messages.searchErrorNoItem.value);
                        }
                    }
                    if (detail.use(types::CArg::Skill) && value->skill() == 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d891(__FILE__, __LINE__);
                        if (!(value->range() == types::Range::SelectedCard &&
                              value->type() == types::CType::LoseSkill)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d892(__FILE__, __LINE__);
                            result.push_back(messages.searchErrorNoSkill.value);
                        }
                    }
                    if (detail.use(types::CArg::Beast) && value->beast() == 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d896(__FILE__, __LINE__);
                        if (!(value->range() == types::Range::SelectedCard &&
                              value->type() == types::CType::LoseBeast)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d897(__FILE__, __LINE__);
                            result.push_back(messages.searchErrorNoBeast.value);
                        }
                    }
                    if (detail.use(types::CArg::Info) && value->info() == 0) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d901(__FILE__, __LINE__);
                        result.push_back(messages.searchErrorNoInfo.value);
                    }
                    const bool expressionTarget =
                        detail.use(types::CArg::Flag) &&
                        detail.use(types::CArg::Step) &&
                        detail.use(types::CArg::Variant);
                    if (expressionTarget) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d904(__FILE__, __LINE__);
                        if (value->flag().empty() && value->step().empty() &&
                            value->variant().empty()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d905(__FILE__, __LINE__);
                            result.push_back(
                                messages.searchErrorNoExpressionTarget.value);
                        }
                    } else {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d908(__FILE__, __LINE__);
                        if (detail.use(types::CArg::Flag) && value->flag().empty()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d909(__FILE__, __LINE__);
                            result.push_back(messages.searchErrorNoFlag.value);
                        }
                        if (detail.use(types::CArg::Step) && value->step().empty()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d912(__FILE__, __LINE__);
                            result.push_back(messages.searchErrorNoStep.value);
                        }
                        if (detail.use(types::CArg::Variant) &&
                            value->variant().empty()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d915(__FILE__, __LINE__);
                            result.push_back(messages.searchErrorNoVariant.value);
                        }
                    }
                    if (detail.use(types::CArg::Flag2) && value->flag2().empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d919(__FILE__, __LINE__);
                        result.push_back(messages.searchErrorNoFlag.value);
                    }
                    if (detail.use(types::CArg::Step2) && value->step2().empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d922(__FILE__, __LINE__);
                        result.push_back(messages.searchErrorNoStep.value);
                    }
                    if (detail.use(types::CArg::Coupon)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d925(__FILE__, __LINE__);
                        append(warning::sjisWarnings(
                            prop, isClassic, value->coupon(),
                            messages.couponName.value));
                        if (value->coupon().empty()) {
                            result.push_back(messages.searchErrorNoCoupon.value);
                        }
                    }
                    if (detail.use(types::CArg::Gossip)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d929(__FILE__, __LINE__);
                        append(warning::sjisWarnings(
                            prop, isClassic, value->gossip(),
                            messages.gossipName.value));
                        if (value->gossip().empty() &&
                            domain == warning::WarningDomain::ScenarioValidation) {
                            result.push_back(messages.searchErrorNoGossip.value);
                        }
                    }
                    if (detail.use(types::CArg::CompleteStamp)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d933(__FILE__, __LINE__);
                        append(warning::sjisWarnings(
                            prop, isClassic, value->completeStamp(),
                            messages.endName.value));
                        if (value->completeStamp().empty() &&
                            domain == warning::WarningDomain::ScenarioValidation) {
                            result.push_back(
                                messages.searchErrorNoCompleteStamp.value);
                        }
                    }
                    if (detail.use(types::CArg::KeyCode)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d937(__FILE__, __LINE__);
                        append(warning::sjisWarnings(
                            prop, isClassic, value->keyCode(),
                            messages.keyCode.value));
                        if (value->keyCode().empty()) {
                            result.push_back(messages.searchErrorNoKeyCode.value);
                        }
                    }
                    if (detail.use(types::CArg::CellName) &&
                        value->cellName().empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d941(__FILE__, __LINE__);
                        append(warning::sjisWarnings(
                            prop, isClassic, value->cellName(),
                            messages.cellName.value));
                        result.push_back(messages.searchErrorNoCellName.value);
                    }
                    if (detail.use(types::CArg::CardGroup) &&
                        value->cardGroup().empty()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d945(__FILE__, __LINE__);
                        append(warning::sjisWarnings(
                            prop, isClassic, value->cardGroup(),
                            messages.cardGroup.value));
                        result.push_back(messages.searchErrorNoCardGroup.value);
                    }
                    const auto maxNextLen = [&](const event::Content& content) -> std::uint32_t {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d949(__FILE__, __LINE__);
                        if (content.type() == types::CType::TalkMessage) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d950(__FILE__, __LINE__);
                            return content.text().empty()
                                ? prop.looks().selectionBarMax()
                                : prop.looks().selectionBarMaxWithMessage();
                        } else if (content.type() == types::CType::TalkDialog) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d952(__FILE__, __LINE__);
                            for (const auto& dialog : content.dialogs()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d953(__FILE__, __LINE__);
                                // ネイティブアダプターのnull項目を許容する動作を
                                // 維持する。Dの通常の台詞集合には実際の台詞が入る。
                                if (dialog && !dialog->text().empty())
                                    return prop.looks().selectionBarMaxWithMessage();
                            }
                            return prop.looks().selectionBarMax();
                        }
                        return std::numeric_limits<std::uint32_t>::max();
                    };
                    const auto rows = [](const event::Content& content) -> std::size_t {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d960(__FILE__, __LINE__);
                        std::size_t len = 0U;
                        for (const auto& child : content.next()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d962(__FILE__, __LINE__);
                            // モデルの境界で、ネイティブのnull項目を許容する動作を維持する。
                            if (child && !child->name().empty() &&
                                child->type() != types::CType::CheckFlag &&
                                child->type() != types::CType::CheckStep &&
                                child->type() != types::CType::CheckVariant) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d963(__FILE__, __LINE__);
                                ++len;
                            }
                        }
                        return (len + (content.selectionColumns() - 1U)) /
                               content.selectionColumns();
                    };
                    if (detail.nextType == types::CNextType::Text) {
                        // Dは被演算子と引数を左から右へ評価する。呼び出しを分
                        // け、その順序と二度目の補助関数の呼び出しを維持する。
                        const auto comparisonMax = maxNextLen(*value);
                        const auto comparisonRows = rows(*value);
                        if (comparisonMax < comparisonRows) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d969(__FILE__, __LINE__);
                            const auto warningRows = rows(*value);
                            const auto warningMax = maxNextLen(*value);
                            result.push_back(utils::tryFormat(
                                messages.warningSelectionBarIsMany.value,
                                warningRows, warningMax));
                        }
                    }
                    if (detail.use(types::CArg::SingleLine) && value->singleLine() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "5")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d972(__FILE__, __LINE__);
                        result.push_back(messages.warningSingleLineMessage.value);
                    }
                    if (detail.nextType == types::CNextType::Text &&
                        value->selectionColumns() != 1 &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "1") &&
                        !is160) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d975(__FILE__, __LINE__);
                        result.push_back(messages.warningSelectionColumns.value);
                    }
                    if (value->transition() != types::Transition::Default &&
                        !prop.isTargetVersion(isClassic, wsnVersion, {})) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d979(__FILE__, __LINE__);
                        result.push_back(messages.warningTransitionType.value);
                    }
                    if (value->status() != types::Status::None) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d982(__FILE__, __LINE__);
                        const auto status = static_cast<std::int32_t>(value->status());
                        if (status >= static_cast<std::int32_t>(types::Status::Silence) &&
                            !prop.targetVersion("1.50", targetVersion)) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d983(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.warningBranchStatusMental.value,
                                messages.statusName(value->status()), "1.50"));
                        } else if (status >=
                                       static_cast<std::int32_t>(types::Status::Confuse) &&
                                   !prop.targetVersion("1.30", targetVersion)) {
                                       ::cwxeditor::cwx::perf::TraceScope trace_warning_d985(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.warningBranchStatusMental.value,
                                messages.statusName(value->status()), "1.30"));
                        }
                    }
                    if (value->range() == types::Range::Field &&
                        detail.use(types::CArg::Coupon) &&
                        !prop.targetVersion("1.30", targetVersion)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d989(__FILE__, __LINE__);
                        result.push_back(messages.warningBranchCouponAtField.value);
                    }
                    if (value->range() == types::Range::Npc &&
                        (value->type() == types::CType::BranchCoupon ||
                         value->type() == types::CType::BranchMultiCoupon) &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "5")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d992(__FILE__, __LINE__);
                        result.push_back(messages.warningBranchCouponAtNpc.value);
                    }
                    if (value->selectionMethod() ==
                            types::SelectionMethod::Valued &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d995(__FILE__, __LINE__);
                        result.push_back(messages.warningValuedSelectionMethod.value);
                    }
                    const auto warnClassicContent =
                        [&](const types::CType type, const lib::StringView version) {
                            result.push_back(utils::tryFormat(
                                messages.warningUnknownContent.value,
                                messages.contentName(type), version));
                        };
                    const auto warnWsnContent =
                        [&](const types::CType type, const lib::StringView version) {
                            result.push_back(utils::tryFormat(
                                messages.warningUnknownContentWsn.value,
                                messages.contentName(type), version));
                        };
                    if (value->type() == types::CType::CheckStep &&
                        !prop.targetVersion("1.50", targetVersion)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d998(__FILE__, __LINE__);
                        warnClassicContent(value->type(), "1.50");
                    }
                    if (value->type() == types::CType::SubstituteStep &&
                        !prop.targetVersion("1.30", targetVersion)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1001(__FILE__, __LINE__);
                        warnClassicContent(types::CType::SubstituteStep, "1.30");
                    }
                    if (value->type() == types::CType::SubstituteFlag &&
                        !prop.targetVersion("1.30", targetVersion)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1004(__FILE__, __LINE__);
                        warnClassicContent(types::CType::SubstituteFlag, "1.30");
                    }
                    if (value->type() == types::CType::BranchStepCmp &&
                        !prop.targetVersion("1.30", targetVersion)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1007(__FILE__, __LINE__);
                        warnClassicContent(types::CType::BranchStepCmp, "1.30");
                    }
                    if (value->type() == types::CType::BranchFlagCmp &&
                        !prop.targetVersion("1.30", targetVersion)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1010(__FILE__, __LINE__);
                        warnClassicContent(types::CType::BranchFlagCmp, "1.30");
                    }
                    if (value->type() == types::CType::BranchRandomSelect &&
                        !prop.targetVersion("1.30", targetVersion)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1013(__FILE__, __LINE__);
                        warnClassicContent(types::CType::BranchRandomSelect, "1.30");
                    }
                    if (value->type() == types::CType::BranchKeyCode &&
                        !prop.targetVersion("1.50", targetVersion)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1016(__FILE__, __LINE__);
                        warnClassicContent(types::CType::BranchKeyCode, "1.50");
                    }
                    if (value->type() == types::CType::BranchRound &&
                        !prop.targetVersion("1.50", targetVersion)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1019(__FILE__, __LINE__);
                        warnClassicContent(types::CType::BranchRound, "1.50");
                    }
                    if (value->type() == types::CType::ReplaceBgImage &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1022(__FILE__, __LINE__);
                        if (is160) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1023(__FILE__, __LINE__);
                            if (value->doAnime() || !value->ignoreEffectBooster()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1024(__FILE__, __LINE__);
                                result.push_back(messages.warningReplaceBgImageV7.value);
                            }
                        } else {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1027(__FILE__, __LINE__);
                            warnWsnContent(value->type(), "1");
                        }
                    }
                    if (value->type() == types::CType::LoseBgImage &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1031(__FILE__, __LINE__);
                        if (is160) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1032(__FILE__, __LINE__);
                            if (!value->doAnime() || !value->ignoreEffectBooster()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1033(__FILE__, __LINE__);
                                result.push_back(messages.warningLoseBgImageV7.value);
                            }
                        } else {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1036(__FILE__, __LINE__);
                            warnWsnContent(value->type(), "1");
                        }
                    }
                    if (value->type() == types::CType::MoveBgImage &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "1")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1040(__FILE__, __LINE__);
                        if (is160) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1041(__FILE__, __LINE__);
                            if (!value->doAnime() || !value->ignoreEffectBooster()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1042(__FILE__, __LINE__);
                                result.push_back(messages.warningMoveBgImageV7.value);
                            }
                        } else {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1045(__FILE__, __LINE__);
                            warnWsnContent(value->type(), "1");
                        }
                    }
                    if (value->type() == types::CType::BranchMultiCoupon &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1049(__FILE__, __LINE__);
                        warnWsnContent(types::CType::BranchMultiCoupon, "2");
                    }
                    if (value->type() == types::CType::BranchMultiRandom &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1052(__FILE__, __LINE__);
                        warnWsnContent(types::CType::BranchMultiRandom, "2");
                    }
                    if (value->type() == types::CType::MoveCard &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "3")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1055(__FILE__, __LINE__);
                        if (is160) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1056(__FILE__, __LINE__);
                            if (!((value->overrideCardSpeed() &&
                                   value->cardSpeed() < 1) ||
                                  (!value->overrideCardSpeed() &&
                                   value->cardSpeed() == -1)) ||
                                value->scale() != -1 || value->layer() != -1) {
                                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1057(__FILE__, __LINE__);
                                result.push_back(messages.warningMoveCardV7.value);
                            }
                        } else {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1060(__FILE__, __LINE__);
                            warnWsnContent(value->type(), "3");
                        }
                    }
                    if (value->type() == types::CType::ChangeEnvironment &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1064(__FILE__, __LINE__);
                        if (is160) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1065(__FILE__, __LINE__);
                            if (value->backpackEnabled() ==
                                types::EnvironmentStatus::NotSet) {
                                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1066(__FILE__, __LINE__);
                                result.push_back(
                                    messages.warningChangeEnvironmentV7.value);
                            }
                        } else {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1069(__FILE__, __LINE__);
                            warnWsnContent(value->type(), "4");
                        }
                    }
                    if (value->type() == types::CType::ChangeEnvironment &&
                        value->gameOverEnabled() !=
                            types::EnvironmentStatus::NotSet &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "5")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1073(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.warningEnvironmentEnabled.value,
                            messages.gameOver.value, "5"));
                    }
                    if (value->type() == types::CType::ChangeEnvironment &&
                        value->runAwayEnabled() !=
                            types::EnvironmentStatus::NotSet &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "5")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1076(__FILE__, __LINE__);
                        result.push_back(utils::tryFormat(
                            messages.warningEnvironmentEnabled.value,
                            messages.runAway.value, "5"));
                    }
                    if (value->type() == types::CType::BranchVariant &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1079(__FILE__, __LINE__);
                        warnWsnContent(types::CType::BranchVariant, "4");
                    }
                    if (value->type() == types::CType::SetVariant &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1082(__FILE__, __LINE__);
                        warnWsnContent(types::CType::SetVariant, "4");
                    }
                    if (value->type() == types::CType::CheckVariant &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1085(__FILE__, __LINE__);
                        warnWsnContent(types::CType::CheckVariant, "4");
                    }
                    if (value->type() == types::CType::CustomXmlContent &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "6")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1088(__FILE__, __LINE__);
                        warnWsnContent(value->type(), "6");
                    }
                    if (detail.use(types::CArg::StepValue)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1091(__FILE__, __LINE__);
                        if (root && !value->step().empty()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1092(__FILE__, __LINE__);
                            const auto step = flag::findVar<flag::Step>(
                                root, value->useCounter(), value->step());
                            if (step && step->count() <
                                    static_cast<std::uint32_t>(
                                        value->stepValue())) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1094(__FILE__, __LINE__);
                                result.push_back(utils::tryFormat(
                                    messages.warningStepOverCount.value,
                                    step->name(), step->count(),
                                    value->stepValue()));
                            }
                        }
                        if (isClassic && !is160 &&
                            static_cast<std::uint32_t>(
                                prop.looks().stepMaxCount()) <
                                static_cast<std::uint32_t>(
                                    value->stepValue())) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1098(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.warningStepCount.value,
                                prop.looks().stepMaxCount()));
                        }
                    }
                    if (parent &&
                        parent->detail().nextType == types::CNextType::Step &&
                        value->name() != prop.sys().evtChildDefault() &&
                        warningBranchIsNumeric(value->name())) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1102(__FILE__, __LINE__);
                        try {
                            const auto branchValue = warningUnsignedBranchValue(value->name());
                            if (root && !parent->step().empty()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1105(__FILE__, __LINE__);
                                const auto step = flag::findVar<flag::Step>(
                                    root, parent->useCounter(), parent->step());
                                if (step && step->count() < branchValue) {
                                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1107(__FILE__, __LINE__);
                                    result.push_back(utils::tryFormat(
                                        messages.warningStepOverCount.value,
                                        step->name(), step->count(), branchValue));
                                }
                            }
                            if (isClassic && !is160 &&
                                static_cast<std::uint32_t>(
                                    prop.looks().stepMaxCount()) < branchValue) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1111(__FILE__, __LINE__);
                                result.push_back(utils::tryFormat(
                                    messages.warningStepCount.value,
                                    prop.looks().stepMaxCount()));
                            }
                        } catch (const WarningNumberConversionError&) {
                            // オーバーフローも含め、DのConvExceptionだけ
                            // を狭く捕捉する動作に合わせる。無関係な例外は伝播させる。
                            static_cast<void>(utils::printStackTrace());
                        }
                    }
                    if (isClassic &&
                        (value->type() == types::CType::ChangeArea ||
                         value->type() == types::CType::StartBattle ||
                         value->type() == types::CType::End)) {
                             ::cwxeditor::cwx::perf::TraceScope trace_warning_d1119(__FILE__, __LINE__);
                        if (const auto tree = value->tree();
                            tree && tree->fireRound0()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1121(__FILE__, __LINE__);
                            result.push_back(messages.warningEndOrChangeAreaInRound0.value);
                        }
                    }
                    if (detail.use(types::CArg::StartAction)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1125(__FILE__, __LINE__);
                        if (isClassic &&
                            value->startAction() != types::StartAction::NextRound) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1126(__FILE__, __LINE__);
                            result.push_back(messages.warningStartAction.value);
                        } else if (!isClassic &&
                                   !prop.isTargetVersion(isClassic, wsnVersion, "2") &&
                                   value->startAction() != types::StartAction::Now) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1129(__FILE__, __LINE__);
                            result.push_back(messages.warningStartAction.value);
                        }
                    }
                    if (detail.use(types::CArg::RefAbility)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1134(__FILE__, __LINE__);
                        if (value->refAbility() &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1135(__FILE__, __LINE__);
                            result.push_back(messages.warningRefAbility.value);
                        }
                    }
                    if (detail.use(types::CArg::Ignite) &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1140(__FILE__, __LINE__);
                        if (is160) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1141(__FILE__, __LINE__);
                            const bool hasDamage = std::any_of(
                                value->motionsRef().begin(),
                                value->motionsRef().end(), [](const auto& motion) {
                                    // ネイティブのnullの効果内容を許容する拡張を維持する
                                    // 。通常のDの効果内容配列には実際のオブジェクトが入る。
                                    if (!motion) return false;
                                    return motion->type() == types::MType::Damage ||
                                           motion->type() == types::MType::Absorb ||
                                           motion->type() == types::MType::Paralyze ||
                                           motion->type() ==
                                               types::MType::VanishTarget;
                                });
                            if (hasDamage && !value->ignite()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1143(__FILE__, __LINE__);
                                result.push_back(
                                    messages.warningIgniteDeadEvent.value);
                            } else if (!hasDamage && value->ignite()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1145(__FILE__, __LINE__);
                                result.push_back(
                                    messages.warningIgniteNoDeadEvent.value);
                            }
                        } else {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1148(__FILE__, __LINE__);
                            if (value->ignite()) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1149(__FILE__, __LINE__);
                                result.push_back(messages.warningIgnite.value);
                            }
                        }
                    }
                    if (detail.use(types::CArg::KeyCodes)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1154(__FILE__, __LINE__);
                        if (!value->ignite() && !value->keyCodes().empty()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1155(__FILE__, __LINE__);
                            result.push_back(messages.warningIgnoreKeyCode.value);
                        }
                        if (is160 && !value->keyCodes().empty()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1158(__FILE__, __LINE__);
                            result.push_back(messages.warningIgnoreKeyCodeV7.value);
                        }
                        if (value->ignite() &&
                            prop.sys().isRunAway(value->keyCodes())) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1161(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.warningRunAwayCard.value,
                                prop.sys().runAway()));
                        }
                    }
                    if (value->type() == types::CType::BranchKeyCode &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1165(__FILE__, __LINE__);
                        if (isClassic &&
                            !(value->targetIsSkill() && value->targetIsItem() && value->targetIsBeast() && value->targetIsHand()) &&
                            !(value->targetIsSkill() && !value->targetIsItem() && !value->targetIsBeast() && !value->targetIsHand()) &&
                            !(!value->targetIsSkill() && value->targetIsItem() && !value->targetIsBeast() && value->targetIsHand()) &&
                            !(!value->targetIsSkill() && !value->targetIsItem() && value->targetIsBeast() && !value->targetIsHand())) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1170(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.warningBranchKeyCodeAtClassic.value));
                        } else if (!isClassic &&
                            !(value->targetIsSkill() && value->targetIsItem() && value->targetIsBeast() && !value->targetIsHand()) &&
                            !(value->targetIsSkill() && !value->targetIsItem() && !value->targetIsBeast() && !value->targetIsHand()) &&
                            !(!value->targetIsSkill() && value->targetIsItem() && !value->targetIsBeast() && !value->targetIsHand()) &&
                            !(!value->targetIsSkill() && !value->targetIsItem() && value->targetIsBeast() && !value->targetIsHand())) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1176(__FILE__, __LINE__);
                            result.push_back(utils::tryFormat(
                                messages.warningBranchKeyCodeAtWsn1.value));
                        }
                    }
                    // WithItemの警告は、Dの型付きBranchKeyCodeの項目一覧に属す
                    // る。共通のwarning.dのシナリオ検証は、キーコードの欠落だけを報告する。
                    if (domain == warning::WarningDomain::TypedEventDialog &&
                        value->type() == types::CType::BranchKeyCode && isClassic &&
                        value->targetIsItem()) {
                        result.push_back(messages.warningBranchKeyCodeWithItem.value);
                    }
                    if (value->range() == types::Range::CouponHolder) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1180(__FILE__, __LINE__);
                        switch (value->type()) {
                        case types::CType::Effect:
                            if (!prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1183(__FILE__, __LINE__);
                                result.push_back(utils::tryFormat(
                                    messages.warningCouponHolder.value,
                                    messages.contentName(value->type()), "2"));
                            }
                            break;
                        case types::CType::GetCoupon:
                        case types::CType::LoseCoupon:
                            if (!prop.isTargetVersion(isClassic, wsnVersion, "3")) {
                                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1189(__FILE__, __LINE__);
                                result.push_back(utils::tryFormat(
                                    messages.warningCouponHolder.value,
                                    messages.contentName(value->type()), "3"));
                            }
                            break;
                        default:
                            warning::detail::failDAssertion(__FILE__, __LINE__, "CouponHolder content type");
                        }
                        if (value->holdingCoupon().empty()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1196(__FILE__, __LINE__);
                            result.push_back(messages.warningNoHoldingCoupon.value);
                        }
                    }
                    if (value->range() == types::Range::CardTarget &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "2") &&
                        !is160) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1200(__FILE__, __LINE__);
                        result.push_back(messages.warningCardTarget.value);
                    }
                    if (value->boundaryCheck() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1203(__FILE__, __LINE__);
                        result.push_back(messages.warningBoundaryCheck.value);
                    }
                    if (value->centeringX() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1206(__FILE__, __LINE__);
                        result.push_back(messages.warningCenteringX.value);
                    }
                    if (value->centeringY() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "2")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1209(__FILE__, __LINE__);
                        result.push_back(messages.warningCenteringY.value);
                    }
                    if ((value->type() == types::CType::BranchKeyCode ||
                         value->type() == types::CType::BranchSkill ||
                         value->type() == types::CType::BranchItem ||
                         value->type() == types::CType::BranchBeast) &&
                        value->selectCard() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "3")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1213(__FILE__, __LINE__);
                        result.push_back(messages.warningSelectCard.value);
                    }
                    // DのCardEventDialogテンプレートは、カード
                    // の分岐と取得・喪失の両方を扱う。Wsn3未満の対象範囲
                    // の警告は、すべての具体化で共通モデルの警告経路に保つ。
                    const bool card_event_type =
                        value->type() == types::CType::BranchSkill ||
                        value->type() == types::CType::BranchItem ||
                        value->type() == types::CType::BranchBeast ||
                        value->type() == types::CType::GetSkill ||
                        value->type() == types::CType::GetItem ||
                        value->type() == types::CType::GetBeast ||
                        value->type() == types::CType::LoseSkill ||
                        value->type() == types::CType::LoseItem ||
                        value->type() == types::CType::LoseBeast;
                    const bool key_code_type = value->type() == types::CType::BranchKeyCode;
                    const bool selected_card_range = key_code_type
                        ? value->keyCodeRange() == types::Range::SelectedCard
                        : value->range() == types::Range::SelectedCard;
                    if ((card_event_type || key_code_type) && selected_card_range &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "3")) {
                        result.push_back(messages.warningRangeSelectedCard.value);
                    }
                    if (detail.use(types::CArg::SelectTalker) &&
                        value->selectTalker() &&
                        warning::hasCharacterTalker(*value) &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "3") &&
                        !(is160 && value->type() == types::CType::TalkDialog)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1216(__FILE__, __LINE__);
                        result.push_back(messages.warningSelectTalker.value);
                    }
                    if (detail.use(types::CArg::InvertResult) &&
                        value->invertResult() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4") &&
                        !(is160 && value->type() == types::CType::BranchCoupon)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1219(__FILE__, __LINE__);
                        if (value->type() == types::CType::BranchKeyCode) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1220(__FILE__, __LINE__);
                            result.push_back(messages.warningInvertResultBranchKeyCode.value);
                        } else {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1222(__FILE__, __LINE__);
                            result.push_back(messages.warningInvertResult.value);
                        }
                    }
                    if (detail.use(types::CArg::MatchingCondition) &&
                        static_cast<std::int32_t>(value->matchingCondition()) != 0 &&
                        value->matchingCondition() != types::MatchingCondition::Has &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "5")) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1226(__FILE__, __LINE__);
                        // 利用者向け警告の前に、Dの二つの検査の順序を維持する。
                        CWX_WARNING_D_ASSERT(value->type() == types::CType::BranchKeyCode);
                        CWX_WARNING_D_ASSERT(value->matchingCondition() == types::MatchingCondition::HasNot);
                        result.push_back(messages.warningHasNotBranchKeyCode.value);
                    }
                    if (detail.use(types::CArg::ConsumeCard) &&
                        !value->consumeCard() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "3")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1231(__FILE__, __LINE__);
                        result.push_back(messages.warningConsumeCard.value);
                    }
                    if (detail.use(types::CArg::CardSpeed) &&
                        value->cardSpeed() != -1 &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4") &&
                        !(is160 && value->type() == types::CType::MoveCard)) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1234(__FILE__, __LINE__);
                        result.push_back(messages.warningCardAnimationSpeed.value);
                    }
                    if (detail.use(types::CArg::Expression)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1237(__FILE__, __LINE__);
                        if (value->expression().empty()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1238(__FILE__, __LINE__);
                            result.push_back(messages.searchErrorNoExpression.value);
                        } else {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1240(__FILE__, __LINE__);
                            const auto typeName = [&](const expression::VariantVal& val) {
                                switch (val.type) {
                                case types::VariantType::Number:
                                    return messages.numberValue.value;
                                case types::VariantType::String:
                                    return messages.stringValue.value;
                                case types::VariantType::Boolean:
                                    return messages.booleanValue.value;
                                case types::VariantType::List:
                                    return messages.listValue.value;
                                case types::VariantType::Structure:
                                    return utils::tryFormat(
                                        messages.structValue.value,
                                        val.structName);
                                }
                                return lib::String{};
                            };
                            if (value->type() == types::CType::SetVariant) {
                                if (!value->flag().empty()) {
                                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1245(__FILE__, __LINE__);
                                    const auto returnValue = value->getExpressionReturnValue(&prop);
                                    if (returnValue.valid) {
                                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1247(__FILE__, __LINE__);
                                        if (returnValue.type != types::VariantType::Boolean) {
                                            result.push_back(utils::tryFormat(
                                                messages.warningExpressionToFlag.value,
                                                typeName(returnValue)));
                                        }
                                    }
                                } else if (!value->step().empty()) {
                                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1265(__FILE__, __LINE__);
                                    const auto returnValue = value->getExpressionReturnValue(&prop);
                                    if (returnValue.valid) {
                                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1267(__FILE__, __LINE__);
                                        if (returnValue.type != types::VariantType::Number) {
                                            result.push_back(utils::tryFormat(
                                                messages.warningExpressionToStep.value,
                                                typeName(returnValue)));
                                        }
                                    }
                                }
                            } else if (value->type() == types::CType::BranchVariant ||
                                       value->type() == types::CType::CheckVariant) {
                                const auto returnValue = value->getExpressionReturnValue(&prop);
                                if (returnValue.type != types::VariantType::Boolean) {
                                    result.push_back(utils::tryFormat(
                                        messages.warningExpressionNeedBooleanReturnType.value,
                                        typeName(returnValue),
                                        messages.contentName(value->type())));
                                }
                            }

                            const msgutils::NameMap names;
                            const expression::VarGetter invalidValue =
                                [](const lib::String&) {
                                    return msgutils::VarValue{};
                                };
                            const expression::VariableInfo variableInfo(
                                &prop, summ, value->useCounter(),
                                lib::String(targetVersion), names,
                                invalidValue, invalidValue, invalidValue,
                                invalidValue,
                                [](std::uint32_t) { return lib::String{}; },
                                [](std::int32_t, std::uint32_t) {
                                    return lib::String{};
                                },
                                [] { return lib::String{}; },
                                [] { return lib::String{}; },
                                [summ] {
                                    return summ ? summ->scenarioName()
                                                : lib::String{};
                                },
                                [] { return 0.0; }, [] { return 0.0; },
                                [](std::int32_t, std::uint32_t,
                                   const lib::String&) {
                                    return expression::VariantVal::invalidValue();
                                },
                                [](std::int32_t, std::uint32_t,
                                   const lib::String&) { return false; },
                                [](std::int32_t, std::uint32_t,
                                   const lib::String&) {
                                    return std::uint32_t{0};
                                });
                            append(warning::exprErrorToWarnings(
                                prop, value->expression(),
                                value->getExpressionErrors(&prop, variableInfo)));
                            {
                                const auto& text = value->expression();
                                const auto counter = value->useCounter();
                                const auto input_flags = value->flagsInExpression();
                                const auto input_steps = value->stepsInExpression();
                                const auto input_variants = value->variantsInExpression();
                                append(checkTextRes2(text, counter.get(), input_flags, input_steps, input_variants, {}, {}));
                            }
                        }
                    }
                    if ((detail.use(types::CArg::Coupon) ||
                         detail.use(types::CArg::CouponNames)) &&
                        detail.use(types::CArg::ExpandSPChars) &&
                        value->expandSPChars()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1314(__FILE__, __LINE__);
                        if (!prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                            result.push_back(
                                messages.warningExpandSPCharsInCoupon.value);
                        }
                        const auto flagsInCoupons = value->flagsInCoupons();
                        const auto stepsInCoupons = value->stepsInCoupons();
                        const auto variantsInCoupons = value->variantsInCoupons();
                        {
                            const auto& text = value->coupon();
                            const auto counter = value->useCounter();
                            const auto& input_flags = flagsInCoupons;
                            const auto& input_steps = stepsInCoupons;
                            const auto& input_variants = variantsInCoupons;
                            append(checkTextRes2(text, counter.get(), input_flags, input_steps, input_variants, {}, {}));
                        }
                    }
                    if (detail.use(types::CArg::Gossip) &&
                        detail.use(types::CArg::ExpandSPChars) &&
                        value->expandSPChars()) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1318(__FILE__, __LINE__);
                        if (!prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                            result.push_back(
                                messages.warningExpandSPCharsInGossip.value);
                        }
                        {
                            const auto& text = value->gossip();
                            const auto counter = value->useCounter();
                            const auto input_flags = value->flagsInGossip();
                            const auto input_steps = value->stepsInGossip();
                            const auto input_variants = value->variantsInGossip();
                            append(checkTextRes2(text, counter.get(), input_flags, input_steps, input_variants, {}, {}));
                        }
                    }
                    if (detail.nextType == types::CNextType::Coupon &&
                        value->expandSPChars()) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1322(__FILE__, __LINE__);
                        if (!prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                            result.push_back(
                                messages.warningExpandSPCharsInCoupon.value);
                        }
                    }
                    if (parent && parent->expandSPChars() &&
                        parent->detail().nextType == types::CNextType::Coupon) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1325(__FILE__, __LINE__);
                        {
                            const auto& text = value->name();
                            const auto counter = value->useCounter();
                            const auto input_flags = value->flagsInName();
                            const auto input_steps = value->stepsInName();
                            const auto input_variants = value->variantsInName();
                            append(checkTextRes2(text, counter.get(), input_flags, input_steps, input_variants, {}, {}));
                        }
                    }
                    if (detail.use(types::CArg::InitialEffect) &&
                        value->initialEffect() &&
                        !prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1328(__FILE__, __LINE__);
                        result.push_back(messages.warningInitialEffect.value);
                    }
                    if (detail.use(types::CArg::AbsorbTo)) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1331(__FILE__, __LINE__);
                        if (value->absorbTo() == types::AbsorbTo::Selected &&
                            !prop.isTargetVersion(isClassic, wsnVersion, "4")) {
                            ::cwxeditor::cwx::perf::TraceScope trace_warning_d1332(__FILE__, __LINE__);
                            result.push_back(messages.warningAbsorbToSelected.value);
                        }
                    }
                }
                return result;
            });
        warning::detail::setSummaryInfoProvider([](const summary::Summary* value) {
            warning::detail::SummaryInfo info;
            if (value) {
                info.scenarioPath = value->scenarioPath();
                info.legacy = value->legacy();
            }
            return info;
        });
        warning::detail::setVariableFinder(
            [](const summary::Summary* summ, const usecounter::UseCounter* counter,
               const VariableKind kind, const lib::StringView name) {
                // Dのwarning.findVarは、カウンターの実際のロ
                // ーカル変数所有元で定義を探し、次にシナリオを調べる。使用登
                // 録は定義ではない。hasIDでは切り離されたプレビューを
                // 見落とし、削除済み変数への参照を認めてしまう場合もある。
                const auto exists = [&](const flag::FlagDirPtr& root) {
                    if (!root) return false;
                    if (kind == VariableKind::Flag) return bool(root->findFlag(lib::String(name)));
                    if (kind == VariableKind::Step) return bool(root->findStep(lib::String(name)));
                    return bool(root->findVariant(lib::String(name)));
                };
                if (counter) {
                    const auto owner = std::dynamic_pointer_cast<flag::LocalVariableOwner>(counter->owner());
                    if (owner && exists(owner->flagDirRoot())) return true;
                }
                return summ && exists(summ->flagDirRoot());
            });
        warning::detail::setCharacterTalkerInspector([](const event::Content& value) {
            if (value.type() == types::CType::TalkMessage) {
                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1360(__FILE__, __LINE__);
                for (const auto& image : value.cardImages()) {
                    ::cwxeditor::cwx::perf::TraceScope trace_warning_d1361(__FILE__, __LINE__);
                    if (!image) throw std::invalid_argument("Null talker card image");
                    if (image->type == types::CardImageType::Talker &&
                        image->talker() != types::Talker::Card) {
                        ::cwxeditor::cwx::perf::TraceScope trace_warning_d1362(__FILE__, __LINE__);
                        return true;
                    }
                }
                return false;
            } else if (value.type() == types::CType::TalkDialog) {
                ::cwxeditor::cwx::perf::TraceScope trace_warning_d1367(__FILE__, __LINE__);
                return true;
            }
            throw std::logic_error("Character talker inspection requires TalkMessage or TalkDialog");
        });
        warning::detail::setSkinPathFinder(
            [](const skin::Skin& value, const lib::StringView path,
               const lib::StringView scenarioPath, const lib::StringView wsnVersion,
               const lib::StringView kind) {
                if (path.empty()) return true;
                if (kind == "bgm") {
                    return !value.findPath(path, value.extBgm(), value.bgmDirs(), scenarioPath,
                                           wsnVersion, value.wsnMusicDirs(wsnVersion)).empty();
                }
                if (kind == "sound") {
                    return !value.findPath(path, value.extSound(), value.seDirs(), scenarioPath,
                                           wsnVersion, value.wsnSoundDirs(wsnVersion)).empty();
                }
                return !value.findPath(path, value.extImage(), value.tableDirs(), scenarioPath,
                                       wsnVersion, value.wsnTableDirs(wsnVersion)).empty();
            });
        warning::detail::setSkinSpecialCharacterFinder(
            [](const skin::Skin& value, const char32_t codepoint) {
                return value.spChars().contains(codepoint);
            });
        warning::detail::setSkinImageWarningProvider(
            [](const props::CProps& prop, const skin::Skin&, const lib::StringView path,
               const bool isClassic, const bool includeType, const lib::StringView targetVersion) {
                return skin::Skin::warningImage(prop, path, isClassic, includeType, targetVersion);
            });
    }
};

const WarningAdapterInstaller warning_adapter_installer{};

}  // namespace
}  // namespace cwxeditor::cwx
