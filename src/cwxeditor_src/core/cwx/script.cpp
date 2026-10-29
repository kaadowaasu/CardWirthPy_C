#include "cwxeditor_src/core/cwx/script.hpp"
#include "cwxeditor_src/lib/scalar_conversion.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/lib/d_regex.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <span>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <unordered_set>

namespace cwxeditor::cwx::script {
namespace {

[[nodiscard]] String scriptMessage(const Props* prop,
    const msgs::Message msgs::Msgs::* field, StringView fallback) {
    return prop ? (prop->requiredMsgs().*field).value : String(fallback);
}

[[nodiscard]] String lowerAscii(StringView value) {
    // Dのstd.string.toLowerは共通のUnicode文字列変換。変数
    // キーには、バイトごとのCロケール変換ではなく、全プラットフォームでこれを使う。
    return enumutils::lowerCase(value);
}

[[nodiscard]] const lib::DRegex& dScriptTokenRegex() {
    // script.dの単一のTOKENS選択式に対応する。通
    // 常の走査で不正入力が見つかった後だけ参照し、D形式の先頭
    // 側の未一致部分と、最後のpostの未一致部分を区別する。
    static const lib::DRegex regex{enumutils::toUtf32(
        R"cwxscript((?i)([a-z_][a-z_0-9]*)|(\$[^\x08\x09\x0a\x0b\x0c\x0d !"#$%&'\(\)*+,\-./:;<=>?@\[\\\]^`{}|~]+)|(=)|([0-9]+(\.[0-9]+)?)|(\[)|(\])|(,)|("(""|[^"])*?")|('(''|[^'])*?')|(@[ \t]*([0-9]+|c|center)?[ \t]*\n(([^@]|@@|\n)*\n)?[ \t]*@)|([ \t\r\n]+)|(\+)|(-)|(\*\/?)|(\/(\/.*(\n|$)|\*)?)|(%)|(~)|(\()|(\)))cwxscript")};
    return regex;
}

[[nodiscard]] String scriptMentalKeyword(StringView value) {
    // ContentInitialValueEditorはMentalのモデル
    // 上の列挙名（aggressiveなど）を保存するが、Dのスクリプト文法
    // は専用キーワードaggを必要とする。この変換はスクリプトの境界で行う。
    const auto lowered = lowerAscii(value);
    if (lowered == "aggressive" || lowered == "agg") return "agg";
    if (lowered == "unaggressive" || lowered == "unagg") return "unagg";
    if (lowered == "cheerful" || lowered == "cheerf") return "cheerf";
    if (lowered == "uncheerful" || lowered == "uncheerf") return "uncheerf";
    if (lowered == "brave") return "brave";
    if (lowered == "unbrave") return "unbrave";
    if (lowered == "cautious" || lowered == "caut") return "caut";
    if (lowered == "uncautious" || lowered == "uncaut") return "uncaut";
    if (lowered == "trickish" || lowered == "trick") return "trick";
    if (lowered == "untrickish" || lowered == "untrick") return "untrick";
    return String(value);
}

struct ScriptArgumentLayout {
    std::vector<CArg> kinds;
    std::vector<String> values;
    std::vector<std::size_t> sources;
};

[[nodiscard]] bool scriptBool(StringView value) {
    return value == "true" || value == "yes" || value == "on" ||
           value == "all" || value == "average" || value == "complete";
}

[[nodiscard]] bool scriptArrayArgument(CArg kind) {
    switch (kind) {
    case CArg::TalkerC: case CArg::Dialogs: case CArg::BgImages:
    case CArg::Motions: case CArg::Coupons: case CArg::KeyCodes:
    case CArg::CouponNames: case CArg::TargetIsSkill:
        return true;
    default: return false;
    }
}

template <typename Integer>
[[nodiscard]] std::optional<Integer> scriptInteger(StringView value) {
    return utils::detail::parseDInteger<Integer>(value);
}

[[nodiscard]] std::optional<long double> scriptReal(StringView value) {
    // Phobos v2.070のstd.conv.parse!realでは、アンダースコアは仮数の数字の
    // 後に続き、十六進数にはp指数が必要。特殊値の表記はinfinityではなくinf。 https://github.com/dlang/phobos/blob/v2.070.0/std/conv.d
    //
    if (value.empty()) return std::nullopt;
    std::size_t index = value.front() == '+' || value.front() == '-' ? 1U : 0U;
    if (index == value.size()) return std::nullopt;
    const auto special = lowerAscii(value.substr(index));
    if (special == "inf" && value.front() != '+')
        return value.front() == '-' ? -std::numeric_limits<long double>::infinity()
                                    : std::numeric_limits<long double>::infinity();
    if (special == "nan") return std::numeric_limits<long double>::quiet_NaN();
    String normalized(value.substr(0, index));
    bool digits = false, nonzero = false, dot = false;
    std::int64_t beforeDot = 0, leadingZeros = 0;
    std::uint64_t most = 0, least = 0, scale = 1;
    std::int64_t decimalShift = 0;
    const bool zeroPrefix = value[index] == '0';
    if (zeroPrefix) { normalized += '0'; ++index; digits = true; }
    const bool hex = zeroPrefix && index < value.size() &&
                     (value[index] == 'x' || value[index] == 'X');
    if (hex) { normalized += value[index++]; digits = false; }
    while (index < value.size()) {
        const char c = value[index];
        const bool digit = (c >= '0' && c <= '9') ||
            (hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
        if (digit) {
            digits = true;
            if (!dot) ++beforeDot;
            if (!nonzero && c == '0') ++leadingZeros;
            nonzero = nonzero || c != '0';
            if (!hex) {
                if (most < (0x7FFFFFFFFFFFULL - 10) / 10)
                    most = most * 10 + static_cast<unsigned>(c - '0');
                else if (scale < (0xFFFFFFFFULL - 10) / 10) {
                    least = least * 10 + static_cast<unsigned>(c - '0');
                    scale *= 10;
                } else ++decimalShift;
                if (dot) --decimalShift;
            }
            normalized += c;
            ++index;
            if (index < value.size() && value[index] == '_') ++index;
        } else if (c == '.' && !dot) {
            dot = true;
            normalized += c;
            ++index;
        } else break;
    }
    if (!digits) return std::nullopt;
    const bool exponent = index < value.size() &&
        (hex ? value[index] == 'p' || value[index] == 'P'
             : value[index] == 'e' || value[index] == 'E');
    if (hex && !exponent) return std::nullopt;
    std::int64_t exponentValue = 0;
    bool negativeExponent = false;
    if (exponent) {
        normalized += value[index++];
        if (index < value.size() && (value[index] == '+' || value[index] == '-')) {
            negativeExponent = value[index] == '-';
            normalized += value[index++];
        }
        const auto begin = index;
        while (index < value.size() && value[index] >= '0' && value[index] <= '9') {
            // ネイティブの浮動小数点の範囲を超えると、意味があるのは指数の
            // 符号だけ。Dのintオーバーフローを再現せず、飽和させる。
            exponentValue = std::min<std::int64_t>(1000000000, exponentValue * 10 + value[index] - '0');
            normalized += value[index++];
        }
        if (begin == index) return std::nullopt;
    }
    if (index != value.size()) return std::nullopt;
    if (!nonzero) return value.front() == '-' ? -0.0L : 0.0L;
    if (!hex) {
        // ホストの十進変換で仮数全体を解析して丸
        // めを変えず、Phobosの分割した十進
        // 累積値と、降順の10の累乗を維持する。
        static const auto powers = [] {
            std::array<std::pair<long double, long double>, 13> result{};
            for (std::size_t bit = 0; bit < result.size(); ++bit) {
                const auto exponent = std::to_string(1U << bit);
                for (const bool negative : {false, true}) {
                    const String text = String(negative ? "1e-" : "1e") + exponent;
                    long double factor{};
                    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), factor);
                    if (parsed.ec == std::errc::result_out_of_range)
                        factor = negative ? 0.0L : std::numeric_limits<long double>::infinity();
                    (negative ? result[bit].second : result[bit].first) = factor;
                }
            }
            return result;
        }();
        auto result = static_cast<long double>(most);
        if (scale != 1) result = result * scale + least;
        auto shift = decimalShift + (negativeExponent ? -exponentValue : exponentValue);
        for (std::size_t bit = powers.size(); bit-- > 0;) {
            const auto power = std::int64_t{1} << bit;
            while (shift >= power) {
                result *= powers[bit].first;
                shift -= power;
                if (!std::isfinite(result)) return std::nullopt;
            }
            while (shift <= -power) {
                result *= powers[bit].second;
                shift += power;
                if (result == 0) return std::nullopt;
            }
        }
        return value.front() == '-' ? -result : result;
    }
    if (hex) normalized.erase(normalized.front() == '+' || normalized.front() == '-' ? 1 : 0, 2);
    if (normalized.front() == '+') normalized.erase(0, 1);
    long double result{};
    const auto parsed = std::from_chars(normalized.data(), normalized.data() + normalized.size(), result,
                                        hex ? std::chars_format::hex : std::chars_format::general);
    if (parsed.ec == std::errc::result_out_of_range && hex &&
        (!nonzero || 4 * (beforeDot - leadingZeros - 1) +
            (negativeExponent ? -exponentValue : exponentValue) < 0))
        return value.front() == '-' ? -0.0L : 0.0L;
    if (parsed.ec != std::errc{} || parsed.ptr != normalized.data() + normalized.size() ||
        !std::isfinite(result) ||
        (!hex && nonzero && result == 0)) return std::nullopt;
    return result;
}

struct ScriptArgumentRead {
    String value;
    std::size_t consumed = 1;
};

template <typename GetValue, typename Reader, typename InvalidVariable>
[[nodiscard]] ScriptArgumentLayout argumentLayout(
    const CType type, const std::size_t sourceCount,
    const std::vector<CWXScript::Kind>& tokenKinds,
    const GetValue& getValue, const Reader& read, const InvalidVariable& invalidVariable) {
    const auto detail = event::contentDetail(type);
    const auto use = [&](CArg value) { return detail.use(value); };
    ScriptArgumentLayout result;
    std::size_t source = 0;
    const auto present = [&] { return source < sourceCount; };
    const auto value = [&]() { return present() ? getValue(source) : String{}; };
    const auto append = [&](CArg kind, String value, std::size_t origin) {
        result.kinds.push_back(kind);
        result.values.push_back(std::move(value));
        result.sources.push_back(origin);
    };
    const auto addSlot = [&](const CArg kind) {
        if (!present()) return false;
        auto parsed = read(kind, source);
        append(kind, std::move(parsed.value), source);
        source += parsed.consumed;
        return true;
    };
    const auto add = [&](const CArg kind) {
        return use(kind) && addSlot(kind);
    };

    // TalkMessageのsingleはTalkerCの特殊形式。Dはそこで
    // 消費してSingleLineを設定する。SingleLineはXMLやコ
    // ンテント詳細の項目だが、スクリプト末尾の引数ではない。Dのsingle/VariableType
    // 補助処理はparseAttrの通常の既定値検査より前に省略引
    // 数の先を参照し得るため、特殊形式の先読みには終端検査を行う。
    if (use(CArg::TalkerC) && present()) {
        if (source < tokenKinds.size() &&
            tokenKinds[source] == CWXScript::Kind::SYMBOL &&
            value() == "single") {
            addSlot(CArg::SingleLine);
        } else {
            addSlot(CArg::TalkerC);
        }
    }
    add(CArg::Text);

    String talker;
    if (use(CArg::TalkerNC) && present()) talker = value();
    add(CArg::TalkerNC); add(CArg::Dialogs); add(CArg::CellName);
    add(CArg::CardGroup); add(CArg::PositionType); add(CArg::X); add(CArg::Y);
    add(CArg::SizeType); add(CArg::Width); add(CArg::Height); add(CArg::Scale);
    add(CArg::Layer); add(CArg::BgImages);

    // モデルアダプターは結合されたTarget表記を受け付ける。成功時は元
    // の二引数を維持し、Dと同様に不正なTargetでは何も消費しない。
    if (use(CArg::TargetS) && present()) {
        auto parsed = read(CArg::TargetS, source);
        append(CArg::TargetS, parsed.value, source);
        if (parsed.consumed == 2 && source + 1 < sourceCount)
            append(CArg::TargetS, parsed.value, source + 1);
        source += parsed.consumed;
    }
    String range;
    if (use(CArg::Range) && present()) range = value();
    add(CArg::Range);

    // CastRange[]だけが、括弧を使わない可変長の引数群。
    if (use(CArg::CastRange)) {
        const auto begin = source;
        while (present()) {
            const auto castRange = value();
            if (castRange != "field" && castRange != "party" &&
                castRange != "t" && castRange != "team" &&
                castRange != "enemy" && castRange != "npc")
                break;
            addSlot(CArg::CastRange);
        }
        // 範囲以外のトークンがあれば、Dはそれを消費せず空配列を代
        // 入する。トークン自体がなければモデルの既定値を維持する。
        if (source == begin && present()) (void)read(CArg::CastRange, source);
    }
    add(CArg::KeyCodeRange);
    if (use(CArg::TargetIsSkill) || use(CArg::TargetIsItem) ||
        use(CArg::TargetIsBeast) || use(CArg::TargetIsHand))
        addSlot(CArg::TargetIsSkill);
    if (use(CArg::HoldingCoupon) && range == "coupon")
        addSlot(CArg::HoldingCoupon);
    add(CArg::Area); add(CArg::Battle);
    add(CArg::Package); add(CArg::Cast); add(CArg::Item); add(CArg::Skill);
    add(CArg::Info); add(CArg::Beast); add(CArg::Start); add(CArg::Complete);
    add(CArg::Money); add(CArg::Coupon); add(CArg::CouponValue);
    add(CArg::CompleteStamp); add(CArg::Gossip); add(CArg::KeyCode);
    if (use(CArg::Flag) && use(CArg::Step) && use(CArg::Variant) && present()) {
        if (tokenKinds[source] == CWXScript::Kind::STRING) {
            addSlot(CArg::Variant);
        } else if (tokenKinds[source] == CWXScript::Kind::SYMBOL) {
            const auto marker = value();
            const bool valid = marker == "flag" || marker == "step" ||
                               marker == "variant" || marker == "var";
            if (!valid) invalidVariable(source);
            const CArg kind = marker == "flag" ? CArg::Flag
                              : marker == "step" ? CArg::Step : CArg::Variant;
            const auto begin = source;
            // 不正なVariableTypeでは記号を消費せず既定値
            // を返す。Dは続いて同じ記号を文字列のパスとして読む。
            const auto pathSource = valid ? source + 1 : source;
            auto path = pathSource < sourceCount ? read(kind, pathSource) : ScriptArgumentRead{"", 0};
            append(kind, path.value, begin);
            if (valid && pathSource < sourceCount) append(kind, path.value, pathSource);
            source = pathSource + path.consumed;
        } else {
            append(CArg::Variant, "", source++);
        }
    } else { add(CArg::Flag); add(CArg::Step); add(CArg::Variant); }
    add(CArg::Flag2); add(CArg::Step2); add(CArg::Comparison4);
    add(CArg::Comparison3); add(CArg::FlagValue); add(CArg::StepValue);
    add(CArg::CardNumber); add(CArg::SelectCard); add(CArg::Motions);
    add(CArg::CardVisual); add(CArg::UnsignedLevel); add(CArg::SignedLevel);
    add(CArg::LevelMin); add(CArg::LevelMax); add(CArg::Wait); add(CArg::Percent);
    add(CArg::TargetAll);
    String selectionMethod;
    if (use(CArg::SelectionMethod) && present())
        selectionMethod = value();
    add(CArg::SelectionMethod); add(CArg::Average);
    add(CArg::PartyNumber); add(CArg::SuccessRate); add(CArg::EffectType);
    add(CArg::Resist); add(CArg::Status); add(CArg::BgmPath); add(CArg::BgmChannel);
    add(CArg::BgmFadeIn); add(CArg::BgmVolume); add(CArg::BgmLoopCount);
    add(CArg::SoundPath); add(CArg::SoundChannel); add(CArg::SoundFadeIn);
    add(CArg::SoundVolume); add(CArg::SoundLoopCount); add(CArg::RefAbility);
    add(CArg::Physical); add(CArg::Mental); add(CArg::Round);
    add(CArg::TransitionSpeed); add(CArg::Transition); add(CArg::DoAnime);
    add(CArg::IgnoreEffectBooster);
    if (talker == "v" || talker == "valued" || selectionMethod == "valued") {
        add(CArg::InitValue); add(CArg::Coupons);
    }
    add(CArg::SelectionColumns); add(CArg::BoundaryCheck); add(CArg::CenteringX);
    add(CArg::CenteringY); add(CArg::SelectTalker); add(CArg::InvertResult);
    add(CArg::MatchingCondition); add(CArg::StartAction); add(CArg::Ignite);
    add(CArg::KeyCodes); add(CArg::CouponNames); add(CArg::MatchingType);
    add(CArg::ExpandSPChars); add(CArg::ConsumeCard); add(CArg::CardSpeed);
    add(CArg::OverrideCardSpeed); add(CArg::InitialEffect);
    add(CArg::InitialSoundPath); add(CArg::InitialSoundChannel);
    add(CArg::InitialSoundFadeIn); add(CArg::InitialSoundVolume);
    add(CArg::InitialSoundLoopCount); add(CArg::AbsorbTo);
    add(CArg::BackpackEnabled); add(CArg::GameOverEnabled);
    add(CArg::RunAwayEnabled); add(CArg::Expression); add(CArg::XmlType);
    add(CArg::XmlStr); add(CArg::XmlAssignment); add(CArg::XmlVariable);
    return result;
}

template <typename Enum>
[[nodiscard]] String enumName(const Enum value) {
    String name;
    if constexpr (std::is_same_v<Enum, types::Range>) {
        name = types::fromRange(value);
    } else if constexpr (std::is_same_v<Enum, types::Talker>) {
        name = types::fromTalker(value);
    } else if constexpr (std::is_same_v<Enum, types::Transition>) {
        name = types::fromTransition(value);
    } else if constexpr (std::is_same_v<Enum, types::EffectType>) {
        name = types::fromEffectType(value);
    } else if constexpr (std::is_same_v<Enum, types::Resist>) {
        name = types::fromResist(value);
    } else if constexpr (std::is_same_v<Enum, types::CardVisual>) {
        name = types::fromCardVisual(value);
    } else if constexpr (std::is_same_v<Enum, types::Mental>) {
        name = types::fromMental(value);
    } else if constexpr (std::is_same_v<Enum, types::Physical>) {
        name = types::fromPhysical(value);
    } else if constexpr (std::is_same_v<Enum, types::SelectionMethod>) {
        name = types::fromSelectionMethod(value);
    } else if constexpr (std::is_same_v<Enum, types::Comparison4>) {
        name = types::fromComparison4(value);
    } else if constexpr (std::is_same_v<Enum, types::Comparison3>) {
        name = types::fromComparison3(value);
    } else if constexpr (std::is_same_v<Enum, types::CoordinateType>) {
        name = types::fromCoordinateType(value);
    } else if constexpr (std::is_same_v<Enum, types::StartAction>) {
        name = types::fromStartAction(value);
    } else if constexpr (std::is_same_v<Enum, types::MatchingType>) {
        name = types::fromMatchingType(value);
    } else if constexpr (std::is_same_v<Enum, types::MatchingCondition>) {
        name = types::fromMatchingCondition(value);
    } else if constexpr (std::is_same_v<Enum, types::EnvironmentStatus>) {
        name = types::fromEnvironmentStatus(value);
    } else if constexpr (std::is_same_v<Enum, types::Status>) {
        name = types::fromStatus(value);
    } else if constexpr (std::is_same_v<Enum, types::AbsorbTo>) {
        switch (value) {
        case types::AbsorbTo::None: name = "None"; break;
        case types::AbsorbTo::Selected: name = "Selected"; break;
        default: throw std::invalid_argument("value is not an AbsorbTo member");
        }
    } else {
        name = enumutils::enumToString(value);
    }
    if (!name.empty() && name.front() >= 'A' && name.front() <= 'Z') {
        name.front() = static_cast<char>(name.front() - 'A' + 'a');
    }
    return name;
}

[[nodiscard]] String normalizeScriptArgument(
    const CArg kind, String value, const CWXScript::Kind tokenKind,
    const Props* prop) {
    const auto lowered = value;
    const auto booleanKind = [&] {
        switch (kind) {
        case CArg::TargetAll: case CArg::Average: case CArg::Complete:
        case CArg::FlagValue: case CArg::SelectCard: case CArg::DoAnime:
        case CArg::IgnoreEffectBooster: case CArg::BoundaryCheck:
        case CArg::CenteringX: case CArg::CenteringY: case CArg::SelectTalker:
        case CArg::InvertResult: case CArg::Ignite: case CArg::ExpandSPChars:
        case CArg::ConsumeCard: case CArg::OverrideCardSpeed:
        case CArg::InitialEffect: case CArg::RefAbility: case CArg::SingleLine:
            return true;
        default: return false;
        }
    };
    if (booleanKind()) {
        if (scriptBool(lowered)) return "true";
        if (lowered == "false" || lowered == "no" || lowered == "off" ||
            lowered == "active" || lowered == "max" ||
            lowered == "nocomplete")
            return "false";
        return value;
    }

    const auto integerKind = [&] {
        switch (kind) {
        case CArg::BgmChannel: case CArg::BgmVolume: case CArg::BgmLoopCount:
        case CArg::SoundChannel: case CArg::SoundVolume:
        case CArg::SoundLoopCount: case CArg::UnsignedLevel:
        case CArg::SignedLevel: case CArg::SuccessRate:
        case CArg::TransitionSpeed: case CArg::Percent: case CArg::StepValue:
        case CArg::CouponValue: case CArg::PartyNumber: case CArg::CardNumber:
        case CArg::Money: case CArg::Wait: case CArg::LevelMin:
        case CArg::LevelMax: case CArg::InitValue: case CArg::Round:
        case CArg::X: case CArg::Y: case CArg::Width: case CArg::Height:
        case CArg::Scale: case CArg::Layer: case CArg::SelectionColumns:
        case CArg::CardSpeed: case CArg::XmlAssignment:
        case CArg::InitialSoundChannel: case CArg::InitialSoundVolume:
        case CArg::InitialSoundLoopCount:
            return true;
        default: return false;
        }
    };
    if (integerKind() && tokenKind == CWXScript::Kind::SYMBOL) {
        if (lowered == "all") return "0";
        if (lowered == "none" || lowered == "default") return "-1";
        return value;
    }

    switch (kind) {
    case CArg::Range:
    case CArg::KeyCodeRange:
        if (lowered == "m" || lowered == "selected")
            return enumName(types::Range::Selected);
        if (lowered == "r" || lowered == "random" || lowered == "one")
            return enumName(types::Range::Random);
        if (lowered == "t" || lowered == "team")
            return enumName(types::Range::Party);
        if (lowered == "coupon") return enumName(types::Range::CouponHolder);
        if (lowered == "card") return enumName(types::Range::CardTarget);
        if (lowered == "backpack") return enumName(types::Range::Backpack);
        if (lowered == "party") return enumName(types::Range::PartyAndBackpack);
        if (lowered == "field") return enumName(types::Range::Field);
        if (lowered == "npc") return enumName(types::Range::Npc);
        if (lowered == "selcard") return enumName(types::Range::SelectedCard);
        return {};
    case CArg::TalkerNC:
        if (lowered == "m" || lowered == "selected")
            return enumName(types::Talker::Selected);
        if (lowered == "u" || lowered == "unselected")
            return enumName(types::Talker::Unselected);
        if (lowered == "r" || lowered == "random")
            return enumName(types::Talker::Random);
        if (lowered == "c" || lowered == "card")
            return enumName(types::Talker::Card);
        if (lowered == "v" || lowered == "valued")
            return enumName(types::Talker::Valued);
        return {};
    case CArg::Transition:
        if (lowered == "default") return enumName(types::Transition::Default);
        if (lowered == "none") return enumName(types::Transition::None);
        if (lowered == "blinds" || lowered == "thread")
            return enumName(types::Transition::Blinds);
        if (lowered == "dissolve" || lowered == "shave")
            return enumName(types::Transition::PixelDissolve);
        if (lowered == "fade") return enumName(types::Transition::Fade);
        return {};
    case CArg::EffectType:
        if (lowered == "physic") return enumName(types::EffectType::Physic);
        if (lowered == "magic") return enumName(types::EffectType::Magic);
        if (lowered == "mphysic")
            return enumName(types::EffectType::MagicalPhysic);
        if (lowered == "pmagic")
            return enumName(types::EffectType::PhysicalMagic);
        if (lowered == "none") return enumName(types::EffectType::None);
        return {};
    case CArg::Resist:
        if (lowered == "avoid") return enumName(types::Resist::Avoid);
        if (lowered == "resist") return enumName(types::Resist::Resist);
        if (lowered == "unfail") return enumName(types::Resist::Unfail);
        return {};
    case CArg::CardVisual:
        if (lowered == "none") return enumName(types::CardVisual::None);
        if (lowered == "reverse") return enumName(types::CardVisual::Reverse);
        if (lowered == "hswing") return enumName(types::CardVisual::Horizontal);
        if (lowered == "vswing") return enumName(types::CardVisual::Vertical);
        return {};
    case CArg::Mental:
        if (lowered == "agg") return enumName(types::Mental::Aggressive);
        if (lowered == "unagg") return enumName(types::Mental::Unaggressive);
        if (lowered == "cheerf") return enumName(types::Mental::Cheerful);
        if (lowered == "uncheerf") return enumName(types::Mental::Uncheerful);
        if (lowered == "brave") return enumName(types::Mental::Brave);
        if (lowered == "unbrave") return enumName(types::Mental::Unbrave);
        if (lowered == "caut") return enumName(types::Mental::Cautious);
        if (lowered == "uncaut") return enumName(types::Mental::Uncautious);
        if (lowered == "trick") return enumName(types::Mental::Trickish);
        if (lowered == "untrick") return enumName(types::Mental::Untrickish);
        return {};
    case CArg::Physical:
        if (lowered == "dex") return enumName(types::Physical::Dex);
        if (lowered == "agl") return enumName(types::Physical::Agl);
        if (lowered == "int") return enumName(types::Physical::Int);
        if (lowered == "str") return enumName(types::Physical::Str);
        if (lowered == "vit") return enumName(types::Physical::Vit);
        if (lowered == "min") return enumName(types::Physical::Min);
        return {};
    case CArg::SelectionMethod:
        if (lowered == "manual") return enumName(types::SelectionMethod::Manual);
        if (lowered == "random") return enumName(types::SelectionMethod::Random);
        if (lowered == "valued") return enumName(types::SelectionMethod::Valued);
        return {};
    case CArg::Comparison4:
        if (lowered == "=") return enumName(types::Comparison4::Eq);
        if (lowered == "<>" || lowered == "!=")
            return enumName(types::Comparison4::Ne);
        if (lowered == ">") return enumName(types::Comparison4::Lt);
        if (lowered == "<") return enumName(types::Comparison4::Gt);
        return {};
    case CArg::Comparison3:
        if (lowered == "=") return enumName(types::Comparison3::Eq);
        if (lowered == ">") return enumName(types::Comparison3::Lt);
        if (lowered == "<") return enumName(types::Comparison3::Gt);
        return {};
    case CArg::PositionType:
    case CArg::SizeType:
        if (lowered == "none") return enumName(types::CoordinateType::None);
        if (lowered == "abs" || lowered == "absolute")
            return enumName(types::CoordinateType::Absolute);
        if (lowered == "rel" || lowered == "relative")
            return enumName(types::CoordinateType::Relative);
        if (lowered == "per" || lowered == "percent" ||
            lowered == "percentage")
            return enumName(types::CoordinateType::Percentage);
        return {};
    case CArg::StartAction:
        if (lowered == "now") return enumName(types::StartAction::Now);
        if (lowered == "current")
            return enumName(types::StartAction::CurrentRound);
        if (lowered == "next") return enumName(types::StartAction::NextRound);
        return {};
    case CArg::MatchingType:
        if (lowered == "and") return enumName(types::MatchingType::And);
        if (lowered == "or") return enumName(types::MatchingType::Or);
        return {};
    case CArg::MatchingCondition:
        if (lowered == "has") return enumName(types::MatchingCondition::Has);
        if (lowered == "hasnot")
            return enumName(types::MatchingCondition::HasNot);
        return {};
    case CArg::AbsorbTo:
        if (lowered == "none") return enumName(types::AbsorbTo::None);
        if (lowered == "m" || lowered == "selected")
            return enumName(types::AbsorbTo::Selected);
        return {};
    case CArg::BackpackEnabled:
    case CArg::GameOverEnabled:
    case CArg::RunAwayEnabled:
        if (lowered == "none")
            return enumName(types::EnvironmentStatus::NotSet);
        if (lowered == "on") return enumName(types::EnvironmentStatus::Enable);
        if (lowered == "off") return enumName(types::EnvironmentStatus::Disable);
        return {};
    case CArg::Status: {
        static const std::unordered_map<String, types::Status> statuses{
            {"active", types::Status::Active}, {"inactive", types::Status::Inactive},
            {"alive", types::Status::Alive}, {"dead", types::Status::Dead},
            {"fine", types::Status::Fine}, {"injured", types::Status::Injured},
            {"heavyinjured", types::Status::HeavyInjured},
            {"unconscious", types::Status::Unconscious},
            {"poison", types::Status::Poison}, {"sleep", types::Status::Sleep},
            {"bind", types::Status::Bind}, {"paralyze", types::Status::Paralyze},
            {"confuse", types::Status::Confuse}, {"overheat", types::Status::Overheat},
            {"brave", types::Status::Brave}, {"panic", types::Status::Panic},
            {"silence", types::Status::Silence}, {"faceup", types::Status::FaceUp},
            {"antimagic", types::Status::AntiMagic},
            {"upaction", types::Status::UpAction}, {"upavoid", types::Status::UpAvoid},
            {"upresist", types::Status::UpResist}, {"updefense", types::Status::UpDefense},
            {"downaction", types::Status::DownAction}, {"downavoid", types::Status::DownAvoid},
            {"downresist", types::Status::DownResist},
            {"downdefense", types::Status::DownDefense}, {"none", types::Status::None}};
        const auto found = statuses.find(lowered);
        if (found != statuses.end()) return enumName(found->second);
        return {};
    }
    default: break;
    }

    const auto stringKind = [&] {
        switch (kind) {
        case CArg::BgmPath: case CArg::SoundPath:
        case CArg::InitialSoundPath: case CArg::Text: case CArg::Start:
        case CArg::Coupon: case CArg::Gossip: case CArg::CompleteStamp:
        case CArg::KeyCode: case CArg::Flag: case CArg::Step:
        case CArg::Variant: case CArg::Flag2: case CArg::Step2:
        case CArg::CellName: case CArg::HoldingCoupon: case CArg::CardGroup:
        case CArg::Expression: case CArg::XmlType: case CArg::XmlStr:
        case CArg::XmlVariable:
            return true;
        default: return false;
        }
    };
    if (stringKind() && tokenKind == CWXScript::Kind::SYMBOL) {
        if (lowered == "stop") value.clear();
        else if (lowered == "random" && prop)
            value = prop->sys().randomValue();
        else if ((lowered == "m" || lowered == "selected") && prop)
            value = prop->sys().selectedPlayerCardNumber();
    }
    if (kind == CArg::BgmPath || kind == CArg::SoundPath ||
        kind == CArg::InitialSoundPath)
        value = utils::encodePath(std::move(value));
    return value;
}

[[nodiscard]] types::MType scriptMotionType(StringView value) {
    static const std::unordered_map<String, types::MType> values{
        {"heal", types::MType::Heal}, {"damage", types::MType::Damage},
        {"absorb", types::MType::Absorb}, {"paralyze", types::MType::Paralyze},
        {"disparalyze", types::MType::DisParalyze}, {"poison", types::MType::Poison},
        {"dispoison", types::MType::DisPoison}, {"getspilit", types::MType::GetSkillPower},
        {"losespilit", types::MType::LoseSkillPower}, {"sleep", types::MType::Sleep},
        {"confuse", types::MType::Confuse}, {"overheat", types::MType::Overheat},
        {"brave", types::MType::Brave}, {"panic", types::MType::Panic},
        {"resetmind", types::MType::Normal}, {"bind", types::MType::Bind},
        {"disbind", types::MType::DisBind}, {"silence", types::MType::Silence},
        {"dissilence", types::MType::DisSilence}, {"faceup", types::MType::FaceUp},
        {"facedown", types::MType::FaceDown}, {"antimagic", types::MType::AntiMagic},
        {"disantimagic", types::MType::DisAntiMagic}, {"enhaction", types::MType::EnhanceAction},
        {"enhavoid", types::MType::EnhanceAvoid}, {"enhresist", types::MType::EnhanceResist},
        {"enhdefense", types::MType::EnhanceDefense}, {"vantarget", types::MType::VanishTarget},
        {"vancard", types::MType::VanishCard}, {"vanbeast", types::MType::VanishBeast},
        {"dealattack", types::MType::DealAttackCard},
        {"dealpowerful", types::MType::DealPowerfulAttackCard},
        {"dealcritical", types::MType::DealCriticalAttackCard},
        {"dealfeint", types::MType::DealFeintCard},
        {"dealdefense", types::MType::DealDefenseCard},
        {"dealdistance", types::MType::DealDistanceCard},
        {"dealconfuse", types::MType::DealConfuseCard},
        {"dealskill", types::MType::DealSkillCard}, {"summon", types::MType::SummonBeast},
        {"cancelaction", types::MType::CancelAction}, {"noeffect", types::MType::NoEffect},
        {"none", types::MType::NoEffect},
    };
    const auto found = values.find(String(value));
    if (found == values.end()) throw std::invalid_argument("unknown script motion: " + String(value));
    return found->second;
}

[[nodiscard]] types::Element scriptElement(StringView value) {
    const auto text = value;
    if (text == "phy") return types::Element::Health;
    if (text == "mind") return types::Element::Mind;
    if (text == "holy") return types::Element::Miracle;
    if (text == "magic") return types::Element::Magic;
    if (text == "fire") return types::Element::Fire;
    if (text == "ice") return types::Element::Ice;
    if (text == "all") return types::Element::All;
    throw std::invalid_argument("unknown script element");
}

[[nodiscard]] types::DamageType scriptDamageType(StringView value,
                                                  types::DamageType fallback) {
    const auto text = value;
    if (text == "level") return types::DamageType::LevelRatio;
    if (text == "value") return types::DamageType::Normal;
    if (text == "max") return types::DamageType::Max;
    if (text == "fixed") return types::DamageType::Fixed;
    if (text.empty()) return fallback;
    throw std::invalid_argument("unknown script damage type");
}

[[nodiscard]] String kindName(CWXScript::Kind kind) {
    switch (kind) {
    case CWXScript::Kind::START: return "START";
    case CWXScript::Kind::IF: return "IF";
    case CWXScript::Kind::FI: return "FI";
    case CWXScript::Kind::ELIF: return "ELIF";
    case CWXScript::Kind::SIF: return "SIF";
    case CWXScript::Kind::O_BRA: return "O_BRA";
    case CWXScript::Kind::C_BRA: return "C_BRA";
    case CWXScript::Kind::SYMBOL: return "SYMBOL";
    case CWXScript::Kind::NUMBER: return "NUMBER";
    case CWXScript::Kind::VAR_NAME: return "VAR_NAME";
    case CWXScript::Kind::EQ: return "EQ";
    case CWXScript::Kind::COMMA: return "COMMA";
    case CWXScript::Kind::STRING: return "STRING";
    case CWXScript::Kind::PLU: return "PLU";
    case CWXScript::Kind::MIN: return "MIN";
    case CWXScript::Kind::MUL: return "MUL";
    case CWXScript::Kind::DIV: return "DIV";
    case CWXScript::Kind::RES: return "RES";
    case CWXScript::Kind::CAT: return "CAT";
    case CWXScript::Kind::O_PAR: return "O_PAR";
    case CWXScript::Kind::C_PAR: return "C_PAR";
    case CWXScript::Kind::COMMENT: return "COMMENT";
    }
    return "UNKNOWN";
}

struct KeywordDef {
    const char* keyword;
    CType type;
};

// script.dが使う順序と表記の表。一か所に集
// 約し、従来とWsnのコマンドでfirstContentType
// 、構文検証、直列化を一致させる。
constexpr KeywordDef KEYWORD_DEFS[] = {
    {"start", CType::Start},
    {"gobattle", CType::StartBattle},
    {"endsc", CType::End},
    {"gameover", CType::EndBadEnd},
    {"goarea", CType::ChangeArea},
    {"chback", CType::ChangeBgImage},
    {"effect", CType::Effect},
    {"break", CType::EffectBreak},
    {"gostart", CType::LinkStart},
    {"gopack", CType::LinkPackage},
    {"msg", CType::TalkMessage},
    {"dialog", CType::TalkDialog},
    {"bgm", CType::PlayBgm},
    {"se", CType::PlaySound},
    {"wait", CType::Wait},
    {"elapse", CType::ElapseTime},
    {"callstart", CType::CallStart},
    {"callpack", CType::CallPackage},
    {"brflag", CType::BranchFlag},
    {"brstepm", CType::BranchMultiStep},
    {"brstept", CType::BranchStep},
    {"selmember", CType::BranchSelect},
    {"brability", CType::BranchAbility},
    {"brrandom", CType::BranchRandom},
    {"brlevel", CType::BranchLevel},
    {"brstatus", CType::BranchStatus},
    {"brcount", CType::BranchPartyNumber},
    {"brarea", CType::BranchArea},
    {"brbattle", CType::BranchBattle},
    {"bronbattle", CType::BranchIsBattle},
    {"brcast", CType::BranchCast},
    {"britem", CType::BranchItem},
    {"brskill", CType::BranchSkill},
    {"brinfo", CType::BranchInfo},
    {"brbeast", CType::BranchBeast},
    {"brmoney", CType::BranchMoney},
    {"brcoupon", CType::BranchCoupon},
    {"brstamp", CType::BranchCompleteStamp},
    {"brgossip", CType::BranchGossip},
    {"setflag", CType::SetFlag},
    {"setstep", CType::SetStep},
    {"stepup", CType::SetStepUp},
    {"stepdown", CType::SetStepDown},
    {"revflag", CType::ReverseFlag},
    {"chkflag", CType::CheckFlag},
    {"getcast", CType::GetCast},
    {"getitem", CType::GetItem},
    {"getskill", CType::GetSkill},
    {"getinfo", CType::GetInfo},
    {"getbeast", CType::GetBeast},
    {"getmoney", CType::GetMoney},
    {"getcoupon", CType::GetCoupon},
    {"getstamp", CType::GetCompleteStamp},
    {"getgossip", CType::GetGossip},
    {"losecast", CType::LoseCast},
    {"loseitem", CType::LoseItem},
    {"loseskill", CType::LoseSkill},
    {"loseinfo", CType::LoseInfo},
    {"losebeast", CType::LoseBeast},
    {"losemoney", CType::LoseMoney},
    {"losecoupon", CType::LoseCoupon},
    {"losestamp", CType::LoseCompleteStamp},
    {"losegossip", CType::LoseGossip},
    {"showparty", CType::ShowParty},
    {"hideparty", CType::HideParty},
    {"redraw", CType::Redisplay},
    {"cpstep", CType::SubstituteStep},
    {"cpflag", CType::SubstituteFlag},
    {"cmpstep", CType::BranchStepCmp},
    {"cmpflag", CType::BranchFlagCmp},
    {"selrandom", CType::BranchRandomSelect},
    {"brkeycode", CType::BranchKeyCode},
    {"chkstep", CType::CheckStep},
    {"brround", CType::BranchRound},
    {"mvback", CType::MoveBgImage},
    {"rplback", CType::ReplaceBgImage},
    {"loseback", CType::LoseBgImage},
    {"brcouponm", CType::BranchMultiCoupon},
    {"brrandomm", CType::BranchMultiRandom},
    {"mvcard", CType::MoveCard},
    {"chenv", CType::ChangeEnvironment},
    {"brvar", CType::BranchVariant},
    {"setvar", CType::SetVariant},
    {"chkvar", CType::CheckVariant},
};

[[nodiscard]] const KeywordDef* findKeyword(StringView value) noexcept {
    const String lowered = lowerAscii(value);
    for (const auto& item : KEYWORD_DEFS) {
        if (lowered == item.keyword) return &item;
    }
    return nullptr;
}

[[nodiscard]] String keywordFor(CType type) {
    for (const auto& item : KEYWORD_DEFS) {
        if (item.type == type) return item.keyword;
    }
    return {};
}

[[nodiscard]] bool isKeyword(StringView value) noexcept {
    return findKeyword(value) != nullptr;
}

[[nodiscard]] bool isAsciiAlpha(unsigned char c) noexcept {
    return std::isalpha(c) != 0 || c == '_';
}

[[nodiscard]] bool isAsciiAlphaNumeric(unsigned char c) noexcept {
    return std::isalnum(c) != 0 || c == '_';
}

[[nodiscard]] String trimAscii(StringView value) {
    std::size_t first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first])) != 0)
        ++first;
    std::size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1])) != 0)
        --last;
    return String(value.substr(first, last - first));
}

[[nodiscard]] String numberString(std::int64_t value) {
    return std::to_string(value);
}

[[nodiscard]] String realString(const lib::Real80& value) {
    // DのCalcResultは表示と連結にstd.conv.to!string
    // を使い、有効数字6桁の丸めも適用する。
    return value.toString();
}

[[nodiscard]] String nodeCode(const CWXScript::Node& node,
                              StringView indent);

[[nodiscard]] String tokenExpression(const std::vector<CWXScript::Token>& tokens) {
    String result;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const auto kind = tokens[i].kind;
        const bool noBefore = kind == CWXScript::Kind::C_PAR;
        const bool noAfter = i > 0 &&
                             tokens[i - 1].kind == CWXScript::Kind::O_PAR;
        if (!result.empty() && !noBefore && !noAfter) result.push_back(' ');
        result += tokens[i].value;
    }
    return result;
}

[[nodiscard]] String calculationCode(std::span<const CWXScript::Node> nodes) {
    if (nodes.size() != 1) throw std::invalid_argument("Invalid calculation node count");
    return tokenExpression(nodes.front().calc);
}

[[nodiscard]] String nodeCode(const CWXScript::Node& node,
                              StringView indent) {
    (void)indent;
    if (node.type == CWXScript::NodeType::VAR_SET) {
        if (node.texts.empty()) throw std::invalid_argument("Invalid node");
        const auto& value = node.texts.front();
        return node.token.value + " = " + (value.token.kind == CWXScript::Kind::STRING
            ? value.token.value : tokenExpression(value.calc));
    }
    if (node.type == CWXScript::NodeType::VALUE) {
        if (!node.calc.empty() && node.calc.size() > 1)
            return tokenExpression(node.calc);
        return node.token.value;
    }
    if (node.type == CWXScript::NodeType::VALUES) {
        String result = "[";
        for (std::size_t i = 0; i < node.childs.size(); ++i) {
            if (i) result += ", ";
            result += nodeCode(node.childs[i], indent);
        }
        return result + "]";
    }
    if (node.type == CWXScript::NodeType::ARRAY) {
        String result;
        for (std::size_t i = 0; i < node.childs.size(); ++i) {
            if (i) result.push_back(' ');
            result += nodeCode(node.childs[i], indent);
        }
        return result;
    }
    String result = node.token.value;
    for (const auto& attr : node.attr) {
        result += result == node.token.value ? " " : ", ";
        result += nodeCode(attr, indent);
    }
    return result;
}

[[nodiscard]] String stripLineEnding(String value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
        value.pop_back();
    return value;
}

// DのNode.codeは連続するコマンドを分岐ブロックにまとめる。コ
// マンドの属性だけを整形すると、条件、子、変数への書き込みが失われる。
[[nodiscard]] String scriptNodesCode(std::span<const CWXScript::Node> nodes,
                                    const String& baseIndent,
                                    const String& indent,
                                    StringView conditionKeyword) {
    using Type = CWXScript::NodeType;
    using Kind = CWXScript::Kind;
    String result;
    for (const auto& node : nodes) {
        if (!result.empty()) result += '\n';
        if (node.type == Type::COMMAND && !node.texts.empty()) {
            result += conditionKeyword == "sif" ? indent : baseIndent;
            result += conditionKeyword;
            result += ' ';
            result += calculationCode(node.texts);
            result += '\n';
            conditionKeyword = "sif";
        }
        if (node.type != Type::COMMAND && node.type != Type::START) {
            if (node.type == Type::VAR_SET) {
                if (node.texts.empty()) throw std::invalid_argument("Invalid node");
                // Dは数値代入で、変数名の後に字下げを置
                // く。この特殊なソース配置も維持する。
                if (node.texts.front().token.kind == Kind::STRING)
                    result += indent + nodeCode(node, "");
                else
                    result += node.token.value + indent + " = " + calculationCode(node.texts);
            } else result += nodeCode(node, "");
            continue;
        }
        for (const auto& var : node.beforeVars) {
            result += indent + nodeCode(var, "") + '\n';
        }
        result += indent + node.token.value;
        if (node.token.kind == Kind::START && !node.texts.empty()) {
            result += ' ';
            result += calculationCode(node.texts);
        } else {
            for (std::size_t i = 0; i < node.attr.size(); ++i) {
                const auto& attr = node.attr[i];
                if (i > 0 && attr.type == Type::VALUES)
                    result += '\n' + indent + String(node.token.value.size() + 1, ' ');
                else
                    result += i == 0 ? " " : ", ";
                result += nodeCode(attr, "");
            }
        }
        if (node.nextIsChild || node.childs.empty()) continue;
        std::vector<std::span<const CWXScript::Node>> blocks;
        bool startBlock = true;
        bool nextIsStart = true;
        std::size_t begin = 0;
        for (std::size_t i = 0; i < node.childs.size(); ++i) {
            if (startBlock && nextIsStart && i > begin) {
                blocks.emplace_back(node.childs.data() + begin, i - begin);
                begin = i;
            }
            startBlock = false;
            if (node.childs[i].type != Type::VAR_SET) {
                nextIsStart = !node.childs[i].nextIsChild;
                startBlock = true;
            }
        }
        blocks.emplace_back(node.childs.data() + begin, node.childs.size() - begin);
        if (blocks.size() > 1) {
            for (std::size_t i = 0; i < blocks.size(); ++i)
                result += '\n' + scriptNodesCode(blocks[i], indent, indent + "    ",
                                                i == 0 ? "if" : "elif");
            result += '\n' + indent + "fi";
        } else {
            result += '\n' + scriptNodesCode(blocks.front(), indent,
                node.token.kind == Kind::START ? indent + "    " : indent, "sif");
        }
    }
    return result;
}

[[nodiscard]] String scriptComment(StringView value) {
    std::vector<String> lines;
    std::size_t offset = 0;
    while (offset <= value.size()) {
        const auto end = value.find('\n', offset);
        String line(value.substr(offset, end == StringView::npos
                                             ? value.size() - offset
                                             : end - offset));
        while (!line.empty() &&
               std::isspace(static_cast<unsigned char>(line.back())) != 0)
            line.pop_back();
        if (!lines.empty() || !line.empty()) lines.push_back(std::move(line));
        if (end == StringView::npos) break;
        offset = end + 1;
    }
    while (!lines.empty() && !lines.front().empty() &&
           std::isspace(static_cast<unsigned char>(lines.front().front())) != 0) {
        const char common = lines.front().front();
        if (std::any_of(lines.begin(), lines.end(), [common](const String& line) {
                return !line.empty() && line.front() != common;
            }))
            break;
        for (auto& line : lines) {
            if (!line.empty()) line.erase(0, 1);
        }
    }
    String result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) result.push_back('\n');
        result += lines[i];
    }
    return utils::lastRet(std::move(result));
}

}  // namespace

String motionToScript(const motion::Motion& value, StringView beastId) {
    // これらの正規のスクリプト表記は、XMLの列挙名と意図的に異なる。
    const auto name = [&]() -> String {
        switch (value.type()) {
        case types::MType::Heal: return String{"heal"};
        case types::MType::Damage: return String{"damage"};
        case types::MType::Absorb: return String{"absorb"};
        case types::MType::Paralyze: return String{"paralyze"};
        case types::MType::DisParalyze: return String{"disparalyze"};
        case types::MType::Poison: return String{"poison"};
        case types::MType::DisPoison: return String{"dispoison"};
        case types::MType::GetSkillPower: return String{"getspilit"};
        case types::MType::LoseSkillPower: return String{"losespilit"};
        case types::MType::Sleep: return String{"sleep"};
        case types::MType::Confuse: return String{"confuse"};
        case types::MType::Overheat: return String{"overheat"};
        case types::MType::Brave: return String{"brave"};
        case types::MType::Panic: return String{"panic"};
        case types::MType::Normal: return String{"resetmind"};
        case types::MType::Bind: return String{"bind"};
        case types::MType::DisBind: return String{"disbind"};
        case types::MType::Silence: return String{"silence"};
        case types::MType::DisSilence: return String{"dissilence"};
        case types::MType::FaceUp: return String{"faceup"};
        case types::MType::FaceDown: return String{"facedown"};
        case types::MType::AntiMagic: return String{"antimagic"};
        case types::MType::DisAntiMagic: return String{"disantimagic"};
        case types::MType::EnhanceAction: return String{"enhaction"};
        case types::MType::EnhanceAvoid: return String{"enhavoid"};
        case types::MType::EnhanceResist: return String{"enhresist"};
        case types::MType::EnhanceDefense: return String{"enhdefense"};
        case types::MType::VanishTarget: return String{"vantarget"};
        case types::MType::VanishCard: return String{"vancard"};
        case types::MType::VanishBeast: return String{"vanbeast"};
        case types::MType::DealAttackCard: return String{"dealattack"};
        case types::MType::DealPowerfulAttackCard: return String{"dealpowerful"};
        case types::MType::DealCriticalAttackCard: return String{"dealcritical"};
        case types::MType::DealFeintCard: return String{"dealfeint"};
        case types::MType::DealDefenseCard: return String{"dealdefense"};
        case types::MType::DealDistanceCard: return String{"dealdistance"};
        case types::MType::DealConfuseCard: return String{"dealconfuse"};
        case types::MType::DealSkillCard: return String{"dealskill"};
        case types::MType::SummonBeast: return String{"summon"};
        case types::MType::CancelAction: return String{"cancelaction"};
        case types::MType::NoEffect: return String{"noeffect"};
        }
        return {};
    };
    const auto damage = [&]() -> String {
        switch (value.damageType()) {
        case types::DamageType::LevelRatio: return String{"level"};
        case types::DamageType::Normal: return String{"value"};
        case types::DamageType::Max: return String{"max"};
        case types::DamageType::Fixed: return String{"fixed"};
        }
        return {};
    };
    const auto element = [&]() -> String {
        switch (value.element()) {
        case types::Element::All: return String{"all"};
        case types::Element::Health: return String{"phy"};
        case types::Element::Mind: return String{"mind"};
        case types::Element::Miracle: return String{"holy"};
        case types::Element::Magic: return String{"magic"};
        case types::Element::Fire: return String{"fire"};
        case types::Element::Ice: return String{"ice"};
        }
        return {};
    };
    const auto detail = value.detail();
    String result = "[" + name();
    if (detail.use(types::MArg::ValueType)) result += ", " + damage();
    if (detail.use(types::MArg::UValue)) result += ", " + std::to_string(value.uValue());
    if (detail.use(types::MArg::AValue)) result += ", " + std::to_string(value.aValue());
    if (detail.use(types::MArg::Round)) result += ", " + std::to_string(value.round());
    if (detail.use(types::MArg::Beast)) result += ", " + String(beastId);
    return result + ", " + element() + "]";
}

CWXScriptException::CWXScriptException(String file, std::size_t line,
                                       String text,
                                       std::vector<CWXSError> errors,
                                       bool over100)
    : std::runtime_error("cwx script error"), text_(std::move(text)),
      errors_(std::move(errors)), over100_(over100) {
    file_ = std::move(file);
    line_ = line;
}

CWXScriptException::CWXScriptException(String text,
                                       std::vector<CWXSError> errors,
                                       bool over100)
    : std::runtime_error("cwx script error"), text_(std::move(text)),
      errors_(std::move(errors)), over100_(over100) {}

String CWXScript::Token::toString() const {
    std::ostringstream stream;
    stream << "Token {line " << line << " : " << pos << ", " << index
           << ", " << kindName(kind) << ", " << value << ", " << comment
           << "}";
    return stream.str();
}

bool CWXScript::Token::opEquals(const Token& other) const noexcept {
    return line == other.line && pos == other.pos && index == other.index &&
           kind == other.kind && value == other.value &&
           comment == other.comment;
}

bool CWXScript::CalcResult::opEquals(const CalcResult& value) const noexcept {
    if (kind != value.kind) return false;
    switch (kind) {
    case CRKind::STR: return str == value.str;
    case CRKind::INT: return numInt == value.numInt;
    case CRKind::REAL: return numReal == value.numReal;
    }
    return false;
}

bool CWXScript::CalcResult::opEquals(std::int64_t value) const noexcept {
    if (kind == CRKind::INT) return numInt == value;
    return kind == CRKind::REAL && numReal == lib::Real80(value);
}

bool CWXScript::CalcResult::opEquals(double value) const noexcept {
    return opEquals(lib::Real80(value));
}

bool CWXScript::CalcResult::opEquals(lib::Real80 value) const noexcept {
    if (kind == CRKind::INT) return lib::Real80(numInt) == value;
    return kind == CRKind::REAL && numReal == value;
}

bool CWXScript::CalcResult::opEquals(StringView value) const noexcept {
    return kind == CRKind::STR && str == value;
}

String CWXScript::CalcResult::toString() const {
    switch (kind) {
    case CRKind::STR: return str;
    case CRKind::INT: return numberString(numInt);
    case CRKind::REAL: return realString(numReal);
    }
    return {};
}

namespace {

void calcNumeric(CWXScript::CalcResult& left, const CWXScript::Token& token,
                 const CWXScript::CalcResult& right, char operation,
                 bool checkZero, const std::function<void(StringView)>& report = {}) {
    const auto error = [&](StringView text) {
        if (report) report(text);
        // Dの入れ子の結果は、所属するパーサーの診断先へアクセスする。独立し
        // たC++の値の結果には外側のパーサーがないため、例外で報告する。
        else throw std::runtime_error(String(text));
    };
    if (left.kind == CWXScript::CRKind::STR ||
        right.kind == CWXScript::CRKind::STR) {
        error("invalid number");
        // Dは文字列の共用体の数値メンバーを読んで継続する。C++
        // の値表現では数値メンバーをゼロ初期化しているため、共用体
        // のビットを読むのではなく、定義済みの代替演算を維持する。
    }
    const lib::Real80 rightReal = right.kind == CWXScript::CRKind::REAL
                                 ? right.numReal
                                 : lib::Real80(right.numInt);
    if (checkZero && rightReal == 0.0) {
        error("zero division");
        // Dのエラーハンドラーが戻ると、整数のゼロ除算へ進む場合があ
        // る。診断の蓄積は維持しつつ、C++の未定義演算を避ける。
        if (left.kind != CWXScript::CRKind::REAL && right.kind != CWXScript::CRKind::REAL) return;
    }
    if (left.kind == CWXScript::CRKind::REAL ||
        right.kind == CWXScript::CRKind::REAL) {
        const lib::Real80 leftReal = left.kind == CWXScript::CRKind::REAL
                                    ? left.numReal
                                    : lib::Real80(left.numInt);
        switch (operation) {
        case '+': left.numReal = leftReal + rightReal; break;
        case '-': left.numReal = leftReal - rightReal; break;
        case '*': left.numReal = leftReal * rightReal; break;
        case '/': left.numReal = leftReal / rightReal; break;
        case '%': left.numReal = leftReal.fmod(rightReal); break;
        default: throw std::runtime_error("invalid operator");
        }
        left.kind = CWXScript::CRKind::REAL;
    } else if (left.kind == CWXScript::CRKind::INT && right.kind == CWXScript::CRKind::INT) {
        // Dの整数の加減乗算は折り返す。C++の符号付き未定義動作を避けるため、符号なしで計算する。
        const auto lbits = std::bit_cast<std::uint64_t>(left.numInt);
        const auto rbits = std::bit_cast<std::uint64_t>(right.numInt);
        if ((operation == '/' || operation == '%') &&
            left.numInt == std::numeric_limits<std::int64_t>::min() && right.numInt == -1) {
            // Dはこの除算で異常を検出する。C++の未定義動作にせず診断を残す。
            error("invalid number");
            return;
        }
        switch (operation) {
        case '+': left.numInt = std::bit_cast<std::int64_t>(lbits + rbits); break;
        case '-': left.numInt = std::bit_cast<std::int64_t>(lbits - rbits); break;
        case '*': left.numInt = std::bit_cast<std::int64_t>(lbits * rbits); break;
        case '/': left.numInt /= right.numInt; break;
        case '%': left.numInt %= right.numInt; break;
        default: throw std::runtime_error("invalid operator");
        }
    } else {
        error("invalid number");
    }
    (void)token;
}

}  // namespace

void CWXScript::CalcResult::cat(const Props* prop, const Token& token,
                                const CalcResult& rhs) {
    (void)prop;
    (void)token;
    if (kind != CRKind::STR) str = toString();
    str += rhs.toString();
    kind = CRKind::STR;
}

void CWXScript::CalcResult::add(const Props* prop, const Token& token,
                                const CalcResult& rhs) {
    (void)prop;
    calcNumeric(*this, token, rhs, '+', false);
}

void CWXScript::CalcResult::min(const Props* prop, const Token& token,
                                const CalcResult& rhs) {
    (void)prop;
    calcNumeric(*this, token, rhs, '-', false);
}

void CWXScript::CalcResult::mul(const Props* prop, const Token& token,
                                const CalcResult& rhs) {
    (void)prop;
    calcNumeric(*this, token, rhs, '*', false);
}

void CWXScript::CalcResult::div(const Props* prop, const Token& token,
                                const CalcResult& rhs) {
    (void)prop;
    calcNumeric(*this, token, rhs, '/', true);
}

void CWXScript::CalcResult::res(const Props* prop, const Token& token,
                                const CalcResult& rhs) {
    (void)prop;
    calcNumeric(*this, token, rhs, '%', true);
}

bool CWXScript::Node::opEquals(const Node& other) const noexcept {
    return type == other.type && token == other.token && texts == other.texts &&
           attr == other.attr && childs == other.childs &&
           calc == other.calc &&
           beforeVars == other.beforeVars;
}

CWXScript::Node CWXScript::Node::dup() const { return *this; }

String CWXScript::Node::toString() const {
    std::ostringstream stream;
    static constexpr const char* names[]{"VAR_SET", "START", "COMMAND", "ARRAY", "VALUES", "VALUE"};
    stream << "Node {" << names[static_cast<std::size_t>(type)] << ", " << token.toString();
    const auto append = [&](const std::vector<Node>& nodes) {
        stream << ", [";
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (i) stream << ", ";
            stream << nodes[i].toString();
        }
        stream << ']';
    };
    append(texts); append(attr); append(childs);
    stream << '}';
    return stream.str();
}

String CWXScript::Node::code(StringView indent,
                             const std::vector<Node>& nodes) {
    // Dの公開入口は、indentに関係なく常に空白4文字を使う。
    (void)indent;
    return scriptNodesCode(nodes, "", "", "sif");
}

CWXScript::Symbol CWXScript::VarTable::id(String value, std::uint64_t idValue) {
    if (!useVar) return Symbol{std::to_string(idValue)};
    auto found = ids_.find(value);
    if (found == ids_.end()) {
        found = ids_.emplace(std::move(value), idValue).first;
    }
    Symbol result;
    result.symbol = found->first;
    return result;
}

CWXScript::Symbol CWXScript::VarTable::id(StringView category, StringView name,
                                        std::uint64_t idValue) {
    if (!useVar) return Symbol{std::to_string(idValue)};
    const auto key=String(category)+":"+std::to_string(idValue);
    if (const auto existing=identities_.find(key);existing!=identities_.end())
        return Symbol{existing->second};
    String safe(name);
    constexpr char prohibited[]{"\0\b\t\n\v\f\r !\"#$%&'()*+,-./:;<=>?@[\\]^`{}|~"};
    for (auto& c : safe) if (StringView(prohibited,sizeof(prohibited)-1).find(c)!=StringView::npos) c='_';
    const String base="$"+String(category)+"_"+safe;
    String symbol=base;
    for (std::size_t index=2;ids_.contains(symbol);++index) symbol=base+"_"+std::to_string(index);
    ids_[symbol]=idValue;identities_[key]=symbol;categories_[symbol]=category;
    return Symbol{std::move(symbol)};
}

std::vector<String> CWXScript::VarTable::vars() const {
    std::vector<String> result;
    result.reserve(ids_.size());
    std::vector<String> names;
    for (const auto& [name,value] : ids_) names.push_back(name);
    constexpr std::array<StringView,8> order{"area","battle","pack","cast","skill","item","beast","info"};
    const auto category=[&](const String& name) {const auto value=categories_.find(name);return value==categories_.end()?order.size():static_cast<std::size_t>(std::find(order.begin(),order.end(),value->second)-order.begin());};
    std::sort(names.begin(),names.end(),[&](const String& a,const String& b) {const auto ca=category(a),cb=category(b);return ca!=cb?ca<cb:ids_.at(a)!=ids_.at(b)?ids_.at(a)<ids_.at(b):a<b;});
    for (const auto& name : names) {
        auto value=std::to_string(ids_.at(name));
        // Dのulong項目は、符号付き計算の文法の範囲を超えるIDを
        // 許可する。単なる数値の宣言ではそのIDを再コンパイルできない
        // ため、十進IDを引用符で囲み、既存の全範囲の項目契約を保つ。
        if (ids_.at(name)>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            value=CWXScript::createString(value);
        result.push_back(name+" = "+value);
    }
    return result;
}

void CWXScript::VarTable::clear() { ids_.clear();identities_.clear();categories_.clear(); }

CWXScript::CWXScript(const Props* prop, const Summary* summ, String text,
                     std::size_t maxError, std::size_t autoWrap)
    : prop_(prop), summ_(summ), text_(std::move(text)), maxError_(maxError),
      autoWrap_(autoWrap) {}

String CWXScript::createString(StringView value, bool useSingleQuote) {
    const char quote = useSingleQuote ? '\'' : '"';
    String result(1, quote);
    for (const char c : value) {
        result.push_back(c);
        if (c == quote) result.push_back(quote);
    }
    result.push_back(quote);
    return result;
}

void CWXScript::throwError(String message, const Token& token,
                          std::source_location origin) {
    throwError(std::move(message), token.line, token.pos, token.value, origin);
}

void CWXScript::throwError(String message, std::size_t line, std::size_t pos,
                           StringView value, std::source_location origin) {
    (void)value;
    if (maxError_ == 0) return;
    if (!prop_) message.clear();
    if (!errors_.empty() && errors_.back().errLine == line &&
        errors_.back().errPos == pos)
        return;
    if (errors_.size() >= maxError_)
        throw CWXScriptException(__FILE__, __LINE__, text_, errors_, true);
    errors_.push_back(CWXSError{std::move(message), line, pos, origin.file_name(),
                                 static_cast<std::size_t>(origin.line())});
}

std::vector<String> CWXScript::wrap(StringView line, std::size_t width) {
    if (width == 0) return {String(line)};
    std::vector<String> result;
    String rest(line);
    // Dの表示幅に対応する共通補助処理を使う。幅1の複数バイト文字が最後の断
    // 片の場合は残りが空になり、Dは元の行の間でもその空の残りを保持する。
    // lengthJも不正UTF-8をバイト列の文字として扱わず拒否する。
    while (utils::lengthJ(rest) > width) {
        auto piece = utils::sliceJ(rest, 0, width);
        if (piece.empty()) piece = utils::sliceJ(rest, 0, width + 1);
        rest.erase(0, piece.size());
        result.push_back(std::move(piece));
    }
    result.push_back(std::move(rest));
    return result;
}

std::size_t CWXScript::stringCenter(const std::vector<String>& lines,
                                    std::size_t width) const {
    std::vector<String> wrapped;
    if (width > 0) {
        for (const auto& line : lines) {
            const auto pieces = wrap(line, width);
            wrapped.insert(wrapped.end(), pieces.begin(), pieces.end());
        }
    } else {
        wrapped = lines;
    }
    // ネイティブ側の意図的な拡張として、CWXScriptは省略可
    // 能なPropsを受け付ける。なければ、Dの必須コンテキストを
    // 使った中央配置の検索ではなく、明示的な行指定で直列化する。
    if (!prop_) return 0;
    // Dは空でない範囲を数え、その内部の空行だけを保持する。
    const auto lineCount = static_cast<std::int32_t>(utils::lineCount(wrapped));
    const auto messageLines = prop_->looks().messageLine();
    if (lineCount <= 0 || lineCount >= messageLines) return 0;
    const auto result = (messageLines - (lineCount - 1)) / 2 + 1;
    return result > 0 ? static_cast<std::size_t>(result) : 0U;
}

String CWXScript::stringValue(const Token& token, std::size_t width) {
    if (token.kind != Kind::STRING || token.value.size() < 2) {
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidString, "invalid string"), token);
        // Dは報告後も継続する。不正な外部トークンが短すぎて
        // 本体を含めない場合だけ、範囲外のスライスを避ける。
        if (token.value.size() < 2) return {};
    }
    const auto decode = [](StringView body, char escape) {
        String result;
        bool escaped = false;
        for (const char c : body) {
            if (!escaped && c == escape) escaped = true;
            else { result.push_back(c); escaped = false; }
        }
        if (escaped) result.push_back(escape);
        return result;
    };
    const StringView value = token.value;
    const char first = value.front();
    if (first != '@') return decode(value.substr(1, value.size() - 2), first);

    const auto sourceLines = utils::splitLines(value.substr(0, value.size() - 1));
    const auto& firstLine = sourceLines.front();
    std::vector<String> lines;
    std::vector<String> wrapped;
    for (std::size_t i = 1; i < sourceLines.size(); ++i) {
        auto line = decode(utils::astripl(sourceLines[i]), '@');
        if (!line.empty() && line.front() == '\\') line.erase(0, 1);
        lines.push_back(line);
        const auto pieces = wrap(line, width);
        wrapped.insert(wrapped.end(), pieces.begin(), pieces.end());
    }
    std::size_t leadingLines = 0;
    if (firstLine.size() > 1) {
        const auto directive = lowerAscii(utils::astrip(firstLine.substr(1)));
        const bool center = directive == "c" || directive == "center";
        // Dはptrdiff_tへの変換前にisNumericを検査する。
        // そのため小数の数値指定も整数変換へ進み、例外になる場合がある。
        if (utils::isNumeric(directive)) {
            std::size_t consumed = 0;
            const auto position = std::stoll(directive, &consumed);
            if (consumed != directive.size()) throw std::invalid_argument("invalid string line number");
            if (position > 1) leadingLines = static_cast<std::size_t>(position - 1);
        } else {
            if (!center)
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidStr, "invalid string directive"), token);
            const auto position = stringCenter(wrapped, 0);
            if (position > 0) leadingLines = position - 1;
        }
    }
    String result(leadingLines, '\n');
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) result.push_back('\n');
        result += lines[i];
    }
    return result;
}
std::vector<CWXScript::Token> CWXScript::tokenizeImpl(
    String& text, bool eatEmptyVarMode, const CompileOption& opt) {
    // Dのref stringに合わせ、トークン位置とeatEmptyVarsの呼び出
    // し側バッファは同じ正規化済みテキストを使う。変数を一つも取り除かない場合も同様。
    String normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            normalized.push_back('\n');
        } else {
            normalized.push_back(text[i]);
        }
    }
    text = std::move(normalized);
    std::vector<Token> result;
    std::size_t i = 0;
    std::size_t line = 0;
    std::size_t pos = 0;
    String docComment;
    bool spaceAfter = true;
    auto logicalLine = [&]() -> std::size_t {
        // Dは符号付きの論理座標をTokenのsize_t項目へ格納する。生成
        // された接頭部の負の行番号はsize_tの剰余で保持し、エラー表示側が
        // 位置を復元できるようにする。符号なし加算でオーバーフローを避ける。
        return line + static_cast<std::size_t>(opt.startLine);
    };
    auto logicalPos = [&]() -> std::size_t {
        const bool onAddedLine = static_cast<std::ptrdiff_t>(line) == opt.addLines;
        const auto shifted = static_cast<std::ptrdiff_t>(pos) +
                             (onAddedLine ? opt.startPos : 0);
        return shifted < 0 ? 0U : static_cast<std::size_t>(shifted);
    };
    auto advance = [&](StringView value) {
        for (const char c : value) {
            if (c == '\n') {
                ++line;
                pos = 0;
            } else {
                ++pos;
            }
        }
    };
    std::size_t lastRegexMatchStart = 0;
    std::size_t lastRegexMatchLine = 0;
    bool hasRegexMatch = false;
    auto rememberRegexMatch = [&](std::size_t start) {
        lastRegexMatchStart = start;
        lastRegexMatchLine = line;
        hasRegexMatch = true;
    };
    auto throwInvalidToken = [&](std::size_t start) {
        std::size_t afterInvalid = std::min(start + 1U, text.size());
        try {
            afterInvalid = enumutils::decodeUtf8Scalar(text, start).next;
        } catch (const std::invalid_argument&) {
            // 不正なUTF-8に対する、既存のバイト単位の回復処理を維持する。
        }
        const auto laterMatches = dScriptTokenRegex().matchAll(
            enumutils::toUtf32Replacing(StringView(text).substr(afterInvalid)));
        if (!laterMatches.empty()) {
            // Dの古い接頭部の座標を使う不具合を意図的に維持する。未
            // 一致部分のエラーはそのバイト位置ではなく、前回の正規表
            // 現の一致のpreを使う。その開始位置をここで追跡する。
            const auto prefix = StringView(text).substr(0,
                hasRegexMatch ? lastRegexMatchStart : 0U);
            const auto newline = prefix.find_last_of('\n');
            const std::size_t prefixPos = newline == StringView::npos
                ? prefix.size() : prefix.size() - newline;
            const auto errorLine = lastRegexMatchLine +
                static_cast<std::size_t>(opt.startLine);
            const auto shifted = static_cast<std::ptrdiff_t>(prefixPos) +
                (static_cast<std::ptrdiff_t>(lastRegexMatchLine) == opt.addLines
                    ? opt.startPos : 0);
            const auto errorPos = shifted < 0 ? 0U
                : static_cast<std::size_t>(shifted);
            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidToken,
                                     "invalid token"), errorLine, errorPos, "");
        } else {
            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidToken,
                                     "invalid token"), logicalLine(), logicalPos(), "");
        }
    };
    auto add = [&](Kind kind, std::size_t begin, String value,
                   String comment = String{}) {
        result.push_back(Token{logicalLine(), logicalPos(), begin, kind,
                                std::move(value), std::move(comment)});
    };
    while (i < text.size()) {
        const std::size_t begin = i;
        const char c = text[i];
        if (i + 1 < text.size() && c == '*' && text[i + 1] == '/') {
            // Dは対応する開き記号のない閉じ記号を読み進めてから報告する。
            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorUnOpenComment, "comment is not open"), logicalLine(), logicalPos() + 2U, "*/");
            // Dはこのエラー後にcommentLevelを0未満へ減らす。後の有効
            // なコメントを誤ってトークン化しないよう、コメント外の状態へ回復する。
            i += 2; pos += 2;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n') {
            // script.dのTOKENSの空白分岐は、VT/FFを意図的に除外する。
            std::size_t end = i + 1U;
            while (end < text.size() &&
                   (text[end] == ' ' || text[end] == '\t' || text[end] == '\n'))
                ++end;
            advance(StringView(text).substr(i, end - i));
            i = end;
            rememberRegexMatch(begin);
            spaceAfter = true;
            continue;
        }
        if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/') {
            const std::size_t startLine = logicalLine();
            const std::size_t startPos = logicalPos();
            const std::size_t start = i;
            while (i < text.size() && text[i] != '\n') ++i;
            if (i < text.size()) ++i;
            const String comment = text.substr(start, i - start);
            add(Kind::COMMENT, begin, comment);
            docComment += comment.size() > 2 ? comment.substr(2) : String{};
            if (comment.find('\n') != String::npos) {
                ++line;
                pos = 0;
            } else {
                pos += comment.size();
            }
            result.back().line = startLine;
            result.back().pos = startPos;
            rememberRegexMatch(start);
            spaceAfter = true;
            continue;
        }
        if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '*') {
            const std::size_t startLine = logicalLine();
            const std::size_t startPos = logicalPos();
            const std::size_t start = i;
            std::size_t closingStart = start;
            int depth = 0;
            String inner;
            while (i < text.size()) {
                if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '*') {
                    ++depth;
                    if (depth > 1) inner += "/*";
                    i += 2;
                    pos += 2;
                    continue;
                }
                if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '/') {
                    const std::size_t delimiterStart = i;
                    --depth;
                    i += 2;
                    pos += 2;
                    if (depth == 0) {
                        closingStart = delimiterStart;
                        break;
                    }
                    inner += "*/";
                    continue;
                }
                if (depth > 0) inner.push_back(text[i]);
                if (text[i] == '\n') {
                    ++line;
                    pos = 0;
                } else {
                    ++pos;
                }
                ++i;
            }
            if (depth != 0) {
                Token token{startLine, startPos, begin, Kind::COMMENT,
                            text.substr(start, i - start), docComment};
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorUnCloseComment, "unclosed comment"), token);
                break;
            }
            add(Kind::COMMENT, begin, text.substr(start, i - start));
            result.back().line = startLine;
            result.back().pos = startPos;
            docComment += inner;
            rememberRegexMatch(closingStart);
            spaceAfter = true;
            continue;
        }
        if (c == '\'' || c == '"' || c == '@') {
            if (eatEmptyVarMode) break;
            const std::size_t startLine = logicalLine();
            const std::size_t startPos = logicalPos();
            const std::size_t rawStartLine = line;
            const std::size_t rawStartPos = pos;
            const char quote = c;
            std::size_t end = i + 1;
            bool closed = false;
            if (quote == '@') {
                while (end < text.size()) {
                    if (text[end] == '@') {
                        if (end + 1 < text.size() && text[end + 1] == '@') {
                            end += 2;
                            continue;
                        }
                        ++end;
                        closed = true;
                        break;
                    }
                    ++end;
                }
            } else {
                while (end < text.size()) {
                    if (text[end] == quote) {
                        if (end + 1 < text.size() && text[end + 1] == quote) {
                            end += 2;
                            continue;
                        }
                        ++end;
                        closed = true;
                        break;
                    }
                    ++end;
                }
            }
            if (!closed) {
                Token token{startLine, startPos, begin, Kind::STRING,
                            text.substr(begin), docComment};
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorUnCloseString, "unclosed string"), token);
                break;
            }
            const String value = text.substr(begin, end - begin);
            add(Kind::STRING, begin, value, docComment);
            result.back().line = startLine;
            result.back().pos = startPos;
            docComment.clear();
            // 埋め込み改行も含め、文字列全体の座標を更新する。走
            // 査位置はバイト単位で、token.indexもscript.d
            // の契約どおり元のバイト位置を保持する。
            line = rawStartLine;
            pos = rawStartPos;
            for (std::size_t p = begin; p < end; ++p) {
                if (text[p] == '\n') {
                    ++line;
                    pos = 0;
                } else {
                    ++pos;
                }
            }
            i = end;
            rememberRegexMatch(begin);
            spaceAfter = false;
            continue;
        }
        if (isAsciiAlpha(static_cast<unsigned char>(c))) {
            if (eatEmptyVarMode) break;
            std::size_t end = i + 1;
            while (end < text.size() &&
                   isAsciiAlphaNumeric(static_cast<unsigned char>(text[end])))
                ++end;
            const String value = text.substr(i, end - i);
            const String lowered = lowerAscii(value);
            Kind kind = Kind::SYMBOL;
            if (lowered == "start") kind = Kind::START;
            else if (lowered == "if") kind = Kind::IF;
            else if (lowered == "fi") kind = Kind::FI;
            else if (lowered == "elif") kind = Kind::ELIF;
            else if (lowered == "sif") kind = Kind::SIF;
            add(kind, begin, value, docComment);
            result.back().line = logicalLine();
            result.back().pos = logicalPos();
            docComment.clear();
            pos += end - i;
            i = end;
            rememberRegexMatch(begin);
            spaceAfter = false;
            continue;
        }
        if (c == '$') {
            std::size_t end = i + 1;
            // Dの変数トークンはASCIIの句読点・制御文字の区切りを除外する
            // が、非公開のID正規化と異なり、数字とUnicodeを許可する。
            constexpr StringView delimiters = "\b\t\n\v\f\r !\"#$%&'()*+,-./:;<=>?@[\\]^`{}|~";
            while (end < text.size() && delimiters.find(text[end]) == StringView::npos)
                ++end;
            if (end == i + 1) {
                Token token{logicalLine(), logicalPos(), begin, Kind::VAR_NAME,
                            "$", docComment};
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidVar, "invalid variable"), token);
                ++i;
                ++pos;
                continue;
            }
            add(Kind::VAR_NAME, begin, text.substr(i, end - i), docComment);
            docComment.clear();
            pos += end - i;
            i = end;
            rememberRegexMatch(begin);
            spaceAfter = false;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            if (eatEmptyVarMode) break;
            std::size_t end = i + 1;
            while (end < text.size() &&
                   std::isdigit(static_cast<unsigned char>(text[end])) != 0)
                ++end;
            if (end < text.size() && text[end] == '.') {
                if (end + 1 < text.size() &&
                    std::isdigit(static_cast<unsigned char>(text[end + 1])) != 0) {
                    ++end;
                    while (end < text.size() &&
                           std::isdigit(static_cast<unsigned char>(text[end])) != 0)
                        ++end;
                }
            }
            add(Kind::NUMBER, begin, text.substr(i, end - i), docComment);
            docComment.clear();
            pos += end - i;
            i = end;
            rememberRegexMatch(begin);
            spaceAfter = false;
            continue;
        }
        Kind kind = Kind::SYMBOL;
        switch (c) {
        case '[': kind = Kind::O_BRA; break;
        case ']': kind = Kind::C_BRA; break;
        case '=': kind = Kind::EQ; break;
        case ',': kind = Kind::COMMA; break;
        case '+': kind = Kind::PLU; break;
        case '-': kind = Kind::MIN; break;
        case '*': kind = Kind::MUL; break;
        case '/': kind = Kind::DIV; break;
        case '%': kind = Kind::RES; break;
        case '~': kind = Kind::CAT; break;
        case '(': kind = Kind::O_PAR; break;
        case ')': kind = Kind::C_PAR; break;
        default: {
            throwInvalidToken(begin);
            return result;
        }
        }
        // script.dの空変数走査は、コメント、空白、変
        // 数名、「=」だけを認める。ほかの記号は消費せず先
        // 頭部分の走査を終える。「=」を残すことで、eatEmptyVars
        // が代入と空の宣言を区別できる。
        if (eatEmptyVarMode && kind != Kind::EQ) break;
        add(kind, begin, String(1, c), docComment);
        docComment.clear();
        ++i;
        ++pos;
        rememberRegexMatch(begin);
        spaceAfter = false;
    }
    (void)spaceAfter;
    return result;
}

std::vector<CWXScript::Token> CWXScript::tokenize(
    StringView source, const CompileOption& opt) {
    String text(source);
    return tokenizeImpl(text, false, opt);
}

std::vector<String> CWXScript::eatEmptyVars(String& text, CompileOption& opt) {
    const auto tokens = tokenizeImpl(text, true, opt);
    std::vector<Token> filtered;
    filtered.reserve(tokens.size());
    for (const auto& token : tokens) {
        if (token.kind != Kind::COMMENT) filtered.push_back(token);
    }
    std::vector<String> result;
    std::size_t cut = 0;
    for (std::size_t i = 0; i < filtered.size(); ++i) {
        if (filtered[i].kind == Kind::EQ) {
            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidSyntax, "invalid variable syntax"), filtered[i]);
            break;
        }
        if (filtered[i].kind != Kind::VAR_NAME) break;
        if (i + 1 < filtered.size() && filtered[i + 1].kind == Kind::EQ) break;
        result.push_back(filtered[i].value);
        cut = filtered[i].index + filtered[i].value.size();
        opt.startLine = static_cast<std::ptrdiff_t>(filtered[i].line);
        opt.startPos = static_cast<std::ptrdiff_t>(filtered[i].pos +
                                                    filtered[i].value.size());
    }
    if (cut > text.size()) cut = text.size();
    text.erase(0, cut);
    opt.startLine = result.empty() ? 0 : opt.startLine;
    opt.startPos = result.empty() ? 0 : opt.startPos;
    return result;
}

String CWXScript::pushVars(String text, const std::vector<VarSet>& variables,
                           CompileOption& opt) {
    std::vector<String> lines;
    opt.addLines = 0;
    for (const auto& variable : variables) {
        String value = variable.value.empty() ? "\"\"" : variable.value;
        lines.push_back(variable.var + " = " + value);
        opt.addLines += static_cast<std::ptrdiff_t>(utils::splitLines(value).size());
    }
    lines.push_back(std::move(text));
    String result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) result.push_back('\n');
        result += lines[i];
    }
    opt.startLine -= opt.addLines;
    return result;
}

CWXScript::CalcResult CWXScript::calcNum(
    const std::vector<Token>& tokens, std::size_t& index, const NodeTable& vars,
    std::size_t strWidth) {
    if (index >= tokens.size()) {
        // Dはトークンが存在することを必要とする。途中で切れた入力に対するC++の防御を維持する。
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidNumber, "invalid number"), 0, 0, {});
        return CalcResult{};
    }
    const auto integer = [](const String& text) {
        return lib::scalar_conversion::integral_from_string<std::int64_t>(text);
    };
    const auto real = [](const String& text) {
        return lib::Real80::parse(text);
    };
    const Token original = tokens[index];
    if (original.kind == Kind::VAR_NAME) {
        ++index;
        try {
            const auto resolved = var(original, vars);
            if (resolved.kind == Kind::STRING) return CalcResult(stringValue(resolved, strWidth));
            if (resolved.value.find('.') != String::npos) return CalcResult(real(resolved.value));
            return CalcResult(integer(resolved.value));
        } catch (const std::exception&) {
            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorReqNumber, "number required"), original);
            return CalcResult{};
        }
    }
    const bool negative = original.kind == Kind::MIN;
    if (original.kind == Kind::PLU || negative) {
        ++index;
        if (index >= tokens.size() || tokens[index].kind != Kind::NUMBER)
            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidNumber, "invalid number"), original);
    }
    if (index >= tokens.size()) return CalcResult{}; // Dでは入力末尾を越えて読み取ってしまう。
    const auto token = tokens[index];
    if (token.kind != Kind::NUMBER && token.kind != Kind::STRING)
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidNumber, "invalid number"), original);
    CalcResult result;
    try {
        if (token.kind == Kind::NUMBER) {
            if (token.value.find('.') != String::npos) {
                result = CalcResult(0.0);
                const auto value = real(token.value);
                result.numReal = negative ? -value : value;
            } else {
                const auto value = integer(token.value);
                result = CalcResult(negative ? std::bit_cast<std::int64_t>(
                    std::uint64_t{} - std::bit_cast<std::uint64_t>(value)) : value);
            }
        } else if (token.kind == Kind::STRING) {
            result = CalcResult(String{});
            result.str = stringValue(token, strWidth);
        } else {
            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorReqNumber, "number required"), token);
        }
        ++index;
    } catch (const std::exception&) {
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorReqNumber, "number required"), token);
    }
    return result;
}
CWXScript::CalcResult CWXScript::calcPar(
    const std::vector<Token>& tokens, std::size_t& index, const NodeTable& vars,
    std::size_t strWidth) {
    if (index >= tokens.size()) {
        // 元のDはこの境界でアサートする。途中で切れた入力を安全に報告する。
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidCalc, "invalid calculation"), 0, 0, {});
        return CalcResult{};
    }
    if (tokens[index].kind != Kind::O_PAR)
        return calcNum(tokens, index, vars, strWidth);
    const Token opening = tokens[index++];
    CalcResult result = calcImpl(0, tokens, index, vars, strWidth);
    if (index >= tokens.size()) {
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorCloseParenNotFound, "closing parenthesis not found"), opening);
        // 元のDはここで入力末尾を越えて読む。この安全確認は、予期し
        // ないトークンが存在する場合の定義された回復処理とは分ける。
        return result;
    }
    if (tokens[index].kind != Kind::C_PAR)
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorCloseParenNotFound, "closing parenthesis not found"), opening);
    // 元のDは診断後、予期しない閉じ記号以外のトークンも含め、存在するトー
    // クンを一つ消費する。終端に対する防御で、その位置の進め方を変えない。
    ++index;
    return result;
}

CWXScript::CalcResult CWXScript::calcImpl(
    std::size_t level, const std::vector<Token>& tokens, std::size_t& index,
    const NodeTable& vars, std::size_t strWidth) {
    CalcResult result = level >= 2 ? calcPar(tokens, index, vars, strWidth)
                                   : calcImpl(level + 1, tokens, index, vars,
                                              strWidth);
    while (index < tokens.size()) {
        const Token token = tokens[index];
        const bool accepted = (level == 0 && token.kind == Kind::CAT) ||
                              (level == 1 && (token.kind == Kind::PLU ||
                                              token.kind == Kind::MIN)) ||
                              (level == 2 && (token.kind == Kind::MUL ||
                                              token.kind == Kind::DIV ||
                                              token.kind == Kind::RES));
        if (!accepted) break;
        ++index;
        if (index >= tokens.size()) {
            // DのCAT・加算・減算の検査は、位置が入力末尾と等しい場合を見落とす
            // 。アサートする入れ子のパーサーへ入らず、被演算子の欠落を診断する。
            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidCalc, "invalid calculation"), token);
            break;
        }
        // Dは次の「~」を読む前に、完了した各算術被演算子を連
        // 結する。加算・減算だけが、それぞれの段階で再帰する。
        CalcResult right = level == 2
                               ? calcPar(tokens, index, vars, strWidth)
                               : calcImpl(level == 0 ? 1U : level, tokens, index, vars, strWidth);
        if (level == 0) result.cat(prop_, token, right);
        else {
            const char operation = token.kind == Kind::PLU ? '+' : token.kind == Kind::MIN ? '-'
                : token.kind == Kind::MUL ? '*' : token.kind == Kind::DIV ? '/' : '%';
            calcNumeric(result, token, right, operation, operation == '/' || operation == '%',
                [&](StringView error) {
                    const auto field = error == "zero division"
                        ? &msgs::Msgs::scriptErrorZeroDivision : &msgs::Msgs::scriptErrorInvalidNumber;
                    throwError(scriptMessage(prop_, field, error), token);
                });
        }
    }
    return result;
}

CWXScript::CalcResult CWXScript::calc(const std::vector<Token>& tokens,
                                      std::size_t& index,
                                      const NodeTable& vars,
                                      std::size_t strWidth) {
    if (index >= tokens.size()) {
        // Dは呼び出し側が渡した空範囲でアサートする。安全な診断を返す。
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidCalc, "invalid calculation"), 0, 0, {});
        return CalcResult{};
    }
    return calcImpl(0, tokens, index, vars, strWidth);
}

std::vector<CWXScript::Node> CWXScript::eatVarSet(
    const std::vector<Token>& tokens, std::size_t& index) {
    std::vector<Node> result;
    while (index < tokens.size() && tokens[index].kind == Kind::VAR_NAME) {
        result.push_back(analyzeSyntaxVar(tokens, index));
    }
    return result;
}

CWXScript::Node CWXScript::analyzeSyntaxVar(
    const std::vector<Token>& tokens, std::size_t& index) {
    Node result;
    if (index >= tokens.size() || tokens[index].kind != Kind::VAR_NAME) {
        if (index < tokens.size()) throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidVar, "invalid variable"), tokens[index]);
        // 外部で切り詰められたトークン一覧には、調べる変数がない。
        if (index >= tokens.size()) return result;
    }
    result.type = NodeType::VAR_SET;
    result.token = tokens[index++];
    if (index >= tokens.size() || tokens[index].kind != Kind::EQ) {
        // 元のDはここで入力末尾でもtokens[i]を参照する。そ
        // の範囲外エラーを再現せず、変数トークンに対して診断する。
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorNoVarSet, "variable assignment expected"),
            index < tokens.size() ? tokens[index] : result.token);
    }
    if (index < tokens.size()) ++index;
    if (index >= tokens.size())
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorNoVarVal, "variable value expected"), result.token);
    result.texts = analyzeSyntaxAttr(tokens, index);
    return result;
}

std::vector<CWXScript::Token> CWXScript::analyzeSyntaxValue(
    const std::vector<Token>& tokens, std::size_t& index) {
    std::vector<Token> result;
    bool needOperand = true;
    bool calculated = false;
    while (index < tokens.size()) {
        const Token token = tokens[index];
        if (token.kind == Kind::VAR_NAME || token.kind == Kind::NUMBER ||
            token.kind == Kind::STRING) {
            if (!needOperand) break;
            result.push_back(token);
            ++index;
            needOperand = false;
            calculated = true;
            continue;
        }
        if (token.kind == Kind::PLU || token.kind == Kind::MIN) {
            if (needOperand) {
                result.push_back(token);
                ++index;
                if (index >= tokens.size()) {
                    // 元のDは次のトークンを無条件に読む。末尾の
                    // 符号は、入力末尾を越えて読まずに診断する。
                    throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidCalc, "invalid calculation"), token);
                    break;
                }
                result.push_back(tokens[index++]);
                needOperand = false;
                calculated = true;
                continue;
            }
            result.push_back(token);
            ++index;
            needOperand = true;
            calculated = true;
            continue;
        }
        if (token.kind == Kind::O_PAR) {
            if (!needOperand && calculated) break;
            result.push_back(token);
            ++index;
            needOperand = true;
            calculated = true;
            continue;
        }
        if (token.kind == Kind::C_PAR) {
            if (needOperand) break;
            result.push_back(token);
            ++index;
            needOperand = false;
            continue;
        }
        if (token.kind == Kind::MUL || token.kind == Kind::DIV ||
            token.kind == Kind::RES || token.kind == Kind::CAT) {
            if (needOperand) break;
            result.push_back(token);
            ++index;
            needOperand = true;
            calculated = true;
            continue;
        }
        if (token.kind == Kind::SYMBOL) {
            if (!calculated) {
                result.push_back(token);
                ++index;
            }
            break;
        }
        if (token.kind == Kind::COMMA) {
            if (result.empty())
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidValue, "invalid value"), token);
            break;
        }
        if (token.kind == Kind::O_BRA || token.kind == Kind::C_BRA ||
            token.kind == Kind::START || token.kind == Kind::IF ||
            token.kind == Kind::FI || token.kind == Kind::ELIF ||
            token.kind == Kind::SIF || token.kind == Kind::EQ) break;
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidCalc, "invalid calculation"), token);
        ++index;
    }
    return result;
}

CWXScript::Node CWXScript::analyzeSyntaxBrackets(
    const std::vector<Token>& tokens, std::size_t& index) {
    Node result;
    result.type = NodeType::VALUES;
    if (index >= tokens.size() || tokens[index].kind != Kind::O_BRA) {
        if (index < tokens.size()) throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidValuesOpen, "opening bracket expected"), tokens[index]);
        // Dはトークンが存在することを必要とする。外部入力に対するC++の終端検査を維持する。
        if (index >= tokens.size()) return result;
    }
    result.token = tokens[index++];
    result.childs = analyzeSyntaxAttr(tokens, index);
    if (index >= tokens.size() || tokens[index].kind != Kind::C_BRA)
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorCloseBracketNotFound, "closing bracket not found"), result.token);
    else
        ++index;
    return result;
}

std::vector<CWXScript::Node> CWXScript::analyzeSyntaxAttr(
    const std::vector<Token>& tokens, std::size_t& index) {
    std::vector<Node> result;
    Kind lastKind = Kind::COMMA;
    while (index < tokens.size()) {
        Token token = tokens[index];
        if (!result.empty() && token.kind == Kind::COMMA) {
            lastKind = Kind::COMMA;
            ++index;
            if (index >= tokens.size()) break;
            token = tokens[index];
        }
        auto put = [&](Node node) {
            if (lastKind == Kind::COMMA || result.empty()) {
                result.push_back(std::move(node));
            } else if (result.back().type == NodeType::ARRAY) {
                result.back().childs.push_back(std::move(node));
            } else {
                Node second = std::move(node);
                Node array = second;
                array.type = NodeType::ARRAY;
                array.token = result.back().token;
                array.childs.clear();
                array.childs.push_back(std::move(result.back()));
                array.childs.push_back(std::move(second));
                result.back() = std::move(array);
            }
        };
        if (token.kind == Kind::O_BRA) {
            put(analyzeSyntaxBrackets(tokens, index));
        } else if (token.kind == Kind::VAR_NAME && index + 1 < tokens.size() &&
                   tokens[index + 1].kind == Kind::EQ) {
            // 後続の代入は、現在のコマンドの引数ではなく、次のコマンドのbeforeVars
            // に属する（DのanalyzeSyntaxAttr）。
            break;
        } else if (token.kind == Kind::START || token.kind == Kind::IF ||
                   token.kind == Kind::ELIF || token.kind == Kind::FI ||
                   token.kind == Kind::SIF || token.kind == Kind::C_BRA) {
            break;
        } else if (token.kind == Kind::COMMA) {
            Node empty;
            empty.type = NodeType::ARRAY;
            empty.token = token;
            ++index;
            put(std::move(empty));
        } else if (token.kind == Kind::SYMBOL && isKeyword(token.value)) {
            break;
        } else if (token.kind == Kind::SYMBOL || token.kind == Kind::NUMBER ||
                   token.kind == Kind::STRING || token.kind == Kind::VAR_NAME ||
                   token.kind == Kind::PLU || token.kind == Kind::MIN ||
                   token.kind == Kind::O_PAR) {
            if (isKeyword(token.value)) break;
            Node value;
            value.type = NodeType::VALUE;
            value.token = token;
            value.calc = analyzeSyntaxValue(tokens, index);
            put(std::move(value));
        } else {
            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidAttr, "invalid attribute"), token);
            ++index;
        }
        lastKind = token.kind;
    }
    return result;
}

std::vector<CWXScript::Node> CWXScript::analyzeSyntaxStatement(
    const std::vector<Token>& tokens, std::size_t& index,
    std::vector<Node>& vars) {
    std::vector<Node> result;
    while (index < tokens.size()) {
        auto assigned = eatVarSet(tokens, index);
        vars.insert(vars.end(), assigned.begin(), assigned.end());
        if (index >= tokens.size()) break;
        std::vector<Node> condition;
        const bool hadSif = tokens[index].kind == Kind::SIF;
        if (hadSif) {
            const Token sif = tokens[index];
            ++index;
            condition = analyzeSyntaxAttr(tokens, index);
            if (index >= tokens.size()) {
                // Dはここで入力末尾を添字で参照する。C++パーサーの範囲
                // 外読み取りを避けつつ、開始側のSIFの診断を維持する。
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidSif, "invalid sif"), sif);
                break;
            }
        }
        Token token = tokens[index];
        if (!hadSif && token.kind != Kind::SYMBOL) {
            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidStatement, "invalid statement"), token);
        }
        if (!findKeyword(token.value)) {
            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidKeyword, "invalid keyword"), token);
        }
        Node node;
        node.type = NodeType::COMMAND;
        node.token = token;
        node.nextIsChild = false;
        // 通常のIFパーサーと同様に、型付き値、変
        // 数参照、計算、元の条件位置を保持する。
        node.texts = std::move(condition);
        node.beforeVars = vars;
        vars.clear();
        ++index;
        node.attr = analyzeSyntaxAttr(tokens, index);
        auto after = eatVarSet(tokens, index);
        vars.insert(vars.end(), after.begin(), after.end());
        if (index >= tokens.size()) {
            result.push_back(std::move(node));
            break;
        }
        if (tokens[index].kind == Kind::IF) {
            node.childs = analyzeSyntaxBranch(tokens, index, vars);
            result.push_back(std::move(node));
            break;
        }
        if (tokens[index].kind == Kind::SIF || tokens[index].kind == Kind::SYMBOL) {
            node.nextIsChild = true;
            result.push_back(std::move(node));
            continue;
        }
        if (tokens[index].kind == Kind::START || tokens[index].kind == Kind::ELIF ||
            tokens[index].kind == Kind::FI) {
            result.push_back(std::move(node));
            break;
        }
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidStatement, "invalid statement"), token);
        result.push_back(std::move(node));
        break;
    }
    return result;
}

std::vector<CWXScript::Node> CWXScript::analyzeSyntaxBranch(
    const std::vector<Token>& tokens, std::size_t& index, std::vector<Node>& vars) {
    std::vector<Node> result;
    if (index >= tokens.size()) return result;
    if (tokens[index].kind == Kind::START) return result;
    if (tokens[index].kind == Kind::IF || tokens[index].kind == Kind::VAR_NAME) {
        const Token branchToken = tokens[index];
        while (index < tokens.size()) {
            std::vector<Node> condition;
            if (tokens[index].kind == Kind::IF || tokens[index].kind == Kind::ELIF) {
                ++index;
                condition = analyzeSyntaxAttr(tokens, index);
                if (index >= tokens.size()) {
                    throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorNoIfContents, "no if contents"), branchToken);
                    // 元のD基準実装との意図的な範囲安全性の違い。analyzeSyntaxStatement
                    // は最初のトークンの存在を前提とするた
                    // め、入力末尾では呼ばない。後のDにも
                    // 同じ本体欠落の検査が追加されている。
                    break;
                }
            }
            auto nodes = analyzeSyntaxStatement(tokens, index, vars);
            if (!nodes.empty()) {
                nodes.front().texts = std::move(condition);
                result.insert(result.end(), nodes.begin(), nodes.end());
            }
            if (index >= tokens.size()) break;
            if (tokens[index].kind == Kind::ELIF) continue;
            if (tokens[index].kind == Kind::FI) {
                ++index;
                break;
            }
            if (tokens[index].kind == Kind::START) break;
            if (tokens[index].kind == Kind::VAR_NAME) continue;
            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidStatement, "invalid statement"), tokens[index]);
            ++index;
        }
        return result;
    }
    if (tokens[index].kind == Kind::SYMBOL || tokens[index].kind == Kind::SIF)
        return analyzeSyntaxStatement(tokens, index, vars);
    throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidBranch, "invalid branch"), tokens[index]);
    ++index;
    return result;
}

std::vector<CWXScript::Node> CWXScript::analyzeSyntax(
    const std::vector<Token>& tokens) {
    std::vector<Token> filtered;
    filtered.reserve(tokens.size());
    for (const auto& token : tokens) {
        if (token.kind != Kind::COMMENT) filtered.push_back(token);
    }
    std::vector<Node> result;
    std::vector<Node> vars;
    std::size_t index = 0;
    while (index < filtered.size()) {
        auto assigned = eatVarSet(filtered, index);
        vars.insert(vars.end(), assigned.begin(), assigned.end());
        if (index >= filtered.size()) break;
        if (filtered[index].kind == Kind::START) {
            Node start;
            start.type = NodeType::START;
            start.token = filtered[index++];
            start.texts = analyzeSyntaxAttr(filtered, index);
            if (start.texts.empty()) throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorNoStartText, "start text expected"), start.token);
            start.beforeVars = vars;
            vars.clear();
            while (index < filtered.size()) {
                auto innerVars = eatVarSet(filtered, index);
                vars.insert(vars.end(), innerVars.begin(), innerVars.end());
                if (index >= filtered.size() || filtered[index].kind == Kind::START ||
                    filtered[index].kind == Kind::FI)
                    break;
                if (filtered[index].kind == Kind::IF ||
                    filtered[index].kind == Kind::ELIF ||
                    filtered[index].kind == Kind::SYMBOL ||
                    filtered[index].kind == Kind::SIF) {
                    auto children = analyzeSyntaxBranch(filtered, index, vars);
                    start.childs.insert(start.childs.end(), children.begin(), children.end());
                    continue;
                }
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidStatement, "invalid statement"), filtered[index]);
                ++index;
            }
            result.push_back(std::move(start));
            continue;
        }
        auto branch = analyzeSyntaxBranch(filtered, index, vars);
        result.insert(result.end(), branch.begin(), branch.end());
    }
    return result;
}

CWXScript::Token CWXScript::var(const Token& token, const NodeTable& vars) {
    if (token.kind != Kind::VAR_NAME) return token;
    const auto found = vars.find(lowerAscii(token.value));
    if (found == vars.end() || found->second.empty()) {
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorUndefinedVar, "undefined variable"), token);
        return token;
    }
    return var(found->second.front().token, vars);
}

std::vector<CWXScript::Node> CWXScript::var(const Node& node,
                                            const NodeTable& vars) {
    if (node.token.kind != Kind::VAR_NAME) return {node};
    const auto found = vars.find(lowerAscii(node.token.value));
    if (found == vars.end()) {
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorUndefinedVar, "undefined variable"), node.token);
        return {};
    }
    std::vector<Node> result;
    for (const auto& value : found->second) {
        auto expanded = var(value, vars);
        result.insert(result.end(), expanded.begin(), expanded.end());
    }
    return result;
}

std::vector<CWXScript::Node> CWXScript::var(const std::vector<Node>& nodes,
                                            const NodeTable& vars) {
    std::vector<Node> result;
    for (const auto& node : nodes) {
        auto expanded = var(node, vars);
        result.insert(result.end(), expanded.begin(), expanded.end());
    }
    return result;
}

std::vector<CWXScript::Node> CWXScript::varValue(
    const Node& node, const std::vector<Node>& values, const NodeTable& vars,
    std::size_t strWidth) {
    if (values.empty()) {
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidVar, "invalid variable"), node.token);
        return {};
    }
    std::vector<Node> result;
    for (const auto& value : values) {
        const auto expanded = value.token.kind == Kind::VAR_NAME
                                  ? var(value, vars)
                                  : std::vector<Node>{value};
        for (auto current : expanded) {
            if (current.token.kind == Kind::STRING ||
                current.token.kind == Kind::NUMBER ||
                current.token.kind == Kind::PLU || current.token.kind == Kind::MIN ||
                current.token.kind == Kind::O_PAR) {
                if (current.calc.empty()) {
                    throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidAttr, "invalid attribute"), current.token);
                    continue;
                }
                std::size_t index = 0;
                const auto calcValue = calc(current.calc, index, vars, strWidth);
                current.token.kind = calcValue.kind == CRKind::STR ? Kind::STRING
                                                                    : Kind::NUMBER;
                current.token.value = calcValue.kind == CRKind::STR
                                          ? createString(calcValue.str)
                                          : calcValue.toString();
            }
            result.push_back(std::move(current));
        }
    }
    return result;
}

String CWXScript::attrValue(const Node& node, const NodeTable& vars,
                            std::size_t strWidth) {
    const auto record = [&](const Node& origin, String value, bool quoted) {
        if (attribute_read_) attribute_read_(origin, value, quoted, strWidth);
        return value;
    };
    switch (node.token.kind) {
    case Kind::SYMBOL: return record(node, lowerAscii(node.token.value), false);
    case Kind::VAR_NAME: {
        if (node.calc.size() > 1) {
            std::size_t index = 0;
            const auto result = calc(node.calc, index, vars, strWidth);
            return record(node, result.toString(), result.kind == CRKind::STR);
        }
        const auto values = var(node, vars);
        if (values.empty()) return {};
        if (values.front().token.kind == Kind::STRING)
            return record(values.front(), stringValue(values.front().token, strWidth), true);
        if (values.front().token.kind == Kind::SYMBOL)
            return record(values.front(), lowerAscii(values.front().token.value), false);
        return attrValue(values.front(), vars, strWidth);
    }
    case Kind::STRING:
    case Kind::NUMBER:
    case Kind::PLU:
    case Kind::MIN:
    case Kind::O_PAR: {
        if (node.calc.empty()) {
            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidAttr, "invalid attribute"), node.token);
            return {};
        }
        std::size_t index = 0;
        const auto result = calc(node.calc, index, vars, strWidth);
        return record(node, result.toString(), result.kind == CRKind::STR);
    }
    case Kind::O_BRA:
    case Kind::COMMA: return {};
    default:
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidAttr, "invalid attribute"), node.token);
        return {};
    }
    return {};
}

String CWXScript::nodeValue(const Node& node, const NodeTable& vars,
                            std::size_t width) {
    if (node.type == NodeType::VALUES) {
        String result = "[";
        for (std::size_t i = 0; i < node.childs.size(); ++i) {
            if (i) result += ", ";
            result += nodeValue(node.childs[i], vars, width);
        }
        return result + "]";
    }
    if (node.type == NodeType::ARRAY) {
        String result;
        for (std::size_t i = 0; i < node.childs.size(); ++i) {
            if (i) result.push_back(' ');
            result += nodeValue(node.childs[i], vars, width);
        }
        return result;
    }
    return attrValue(node, vars, width);
}

Content CWXScript::makeContent(const Node& node, const NodeTable& vars,
                               const CompileOption& opt, std::size_t depth) {
    if (depth > 512) {
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorSystem, "script nesting is too deep"), node.token);
        return {};
    }
    Content result;
    using AttributeKey = std::tuple<std::size_t, std::size_t, std::size_t>;
    std::map<AttributeKey, String> readValues;
    struct RestoreAttributeRead {
        decltype(attribute_read_)& target;
        decltype(attribute_read_) previous;
        ~RestoreAttributeRead() { target = std::move(previous); }
    } restoreAttributeRead{attribute_read_, std::move(attribute_read_)};
    attribute_read_ = [&](const Node& field, const String& value, bool quoted, std::size_t width) {
        readValues[{field.token.line, field.token.pos, width}] = quoted ? createString(value) : value;
    };
    const KeywordDef* keyword = findKeyword(node.token.value);
    result.type = keyword == nullptr ? CType::CustomXmlContent : keyword->type;
    result.keyword = lowerAscii(node.token.value);
    result.name.clear();
    result.comment = scriptComment(node.token.comment);
    result.sourceLine = node.token.line;
    result.sourcePos = node.token.pos;
    result.linkCardIds = opt.linkId;
    if (!node.texts.empty()) {
        if (node.type != NodeType::START) result.condition = nodeCode(node.texts.front(), "");
        const auto values = varValue(node, node.texts, vars, 0);
        if (!values.empty()) {
            const auto& token = values.front().token;
            if (token.kind == Kind::SYMBOL) {
                const auto symbol = lowerAscii(token.value);
                if (findKeyword(symbol)) {
                    result.name.clear();
                } else if (symbol == "default") {
                    result.name = prop_ ? prop_->sys().evtChildDefault() : "Default";
                } else if (symbol == "select" || symbol == "over" ||
                           symbol == "true" || symbol == "success" ||
                           symbol == "yes" || symbol == "has" || symbol == "on") {
                    result.name = prop_ ? prop_->sys().evtChildTrue() : "○";
                } else if (symbol == "cancel" || symbol == "under" ||
                           symbol == "false" || symbol == "failure" ||
                           symbol == "no" || symbol == "hasnot" || symbol == "off") {
                    result.name = prop_ ? prop_->sys().evtChildFalse() : "×";
                } else if (symbol != "none") {
                    throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorUndefinedSymbol, "undefined branch symbol"), node.token);
                }
            } else if (token.kind == Kind::STRING) {
                result.name = stringValue(token);
            } else if (token.kind == Kind::NUMBER) {
                result.name = token.value;
            } else {
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidValue, "invalid branch value"), token);
            }
        }
    }
    if (node.type == NodeType::START && !node.texts.empty())
        result.attributes.emplace_back("text", result.name);
    // Dは一覧が省略された場合も、BranchKeyCodeの四つの対象をすべて解除する。
    if (result.type == CType::BranchKeyCode) result.effectCardTypes.emplace();
    std::size_t argumentIndex = 0;
    const auto stringArgument = [&](const Node& field, std::size_t width) {
        return normalizeScriptArgument(CArg::Text, attrValue(field, vars, width),
                                       field.token.kind, prop_);
    };
    // DのparseAttrは単一値の読み取り成功後だけ位置を進める
    // 。型付き読み込みが項目を消費するまで、トークン構造を維持する。
    const auto readInteger = [&](const std::vector<Node>& fields, std::size_t& index,
                                 std::int32_t fallback) {
        if (index >= fields.size()) return fallback;
        const auto& field = fields[index];
        const auto value = normalizeScriptArgument(
            CArg::Round, nodeValue(field, vars, 0), field.token.kind, prop_);
        if (const auto parsed = scriptInteger<std::int32_t>(value)) {
            ++index;
            return *parsed;
        }
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorReqNumber,
                                 "invalid number"), field.token);
        return std::int32_t{};
    };
    const auto readBoolean = [&](const std::vector<Node>& fields, std::size_t& index,
                                 bool fallback) {
        if (index >= fields.size()) return fallback;
        const auto value = nodeValue(fields[index], vars, 0);
        if (scriptBool(value)) { ++index; return true; }
        if (value == "false" || value == "no" || value == "off" ||
            value == "active" || value == "max" || value == "nocomplete") {
            ++index;
            return false;
        }
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidBoolVal,
                                 "invalid boolean"), fields[index].token);
        return false;
    };
    const auto readString = [&](const std::vector<Node>& fields, std::size_t& index,
                                String fallback = {}) {
        if (index >= fields.size()) return fallback;
        return stringArgument(fields[index++], 0);
    };
    const auto readEnum = [&](const std::vector<Node>& fields, std::size_t& index,
                              auto fallback, const auto& convert, auto errorField) {
        if (index >= fields.size()) return fallback;
        try {
            const auto parsed = convert(nodeValue(fields[index], vars, 0));
            ++index;
            return parsed;
        } catch (const std::invalid_argument&) {
            throwError(scriptMessage(prop_, errorField, "invalid enum"), fields[index].token);
        }
        return decltype(fallback){};
    };
    const auto readColor = [&](const std::vector<Node>& fields, std::size_t& index,
                               structs::CRGB fallback) {
        if (index >= fields.size()) return fallback;
        const auto& field = fields[index];
        if (field.type == NodeType::VALUES) {
            const auto components = var(field.childs, vars);
            std::size_t componentIndex{};
            const auto r = readInteger(components, componentIndex, static_cast<std::int32_t>(fallback.r));
            const auto g = readInteger(components, componentIndex, static_cast<std::int32_t>(fallback.g));
            const auto b = readInteger(components, componentIndex, static_cast<std::int32_t>(fallback.b));
            const auto a = readInteger(components, componentIndex, static_cast<std::int32_t>(fallback.a));
            ++index;
            return structs::CRGB{static_cast<std::uint32_t>(r), static_cast<std::uint32_t>(g),
                                 static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(a)};
        }
        const auto value = lowerAscii(readString(fields, index));
        if ((value.size() == 7 || value.size() == 9) && value.front() == '#' &&
            std::all_of(value.begin() + 1, value.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            })) {
            const auto byte = [&](std::size_t offset) {
                return static_cast<std::uint32_t>(std::stoul(value.substr(offset, 2), nullptr, 16));
            };
            return structs::CRGB{byte(1), byte(3), byte(5), value.size() == 9 ? byte(7) : 255U};
        }
        // Dは文字列の消費後にattr[i]で診断するため、配列外の
        // 場合がある。問題のトークンを保持して範囲外エラーを避ける。
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidColor,
                                 "invalid color"), field.token);
        return fallback;
    };
    // ContentInitialValueEditorを単一値の最
    // 初のパーサーにしない。これは正規化済みのモデル値を受け付けるた
    // め、ここで検証して不正なソースを元のトークンに結び付ける。ダ
    // イアログ構築中に黙って0/falseへ変わらないようにする。
    const auto validateNumber = [&](const String& value, bool unsigned_value,
                                    StringView name, const Token& token) {
        const bool valid = unsigned_value ? scriptInteger<std::uint64_t>(value).has_value()
                                          : scriptInteger<std::int32_t>(value).has_value();
        if (!valid) {
            const auto field = unsigned_value ? &msgs::Msgs::scriptErrorReqID
                                              : &msgs::Msgs::scriptErrorReqNumber;
            throwError(scriptMessage(prop_, field, "invalid " + String(name)), token);
            return false;
        }
        return true;
    };
    const auto scalarBoolean = [&](CArg kind) {
        switch (kind) {
        case CArg::TargetAll: case CArg::Average: case CArg::Complete:
        case CArg::FlagValue: case CArg::SelectCard: case CArg::DoAnime:
        case CArg::IgnoreEffectBooster: case CArg::BoundaryCheck:
        case CArg::CenteringX: case CArg::CenteringY: case CArg::SelectTalker:
        case CArg::InvertResult: case CArg::Ignite: case CArg::ExpandSPChars:
        case CArg::ConsumeCard: case CArg::OverrideCardSpeed:
        case CArg::InitialEffect: case CArg::RefAbility: case CArg::SingleLine:
            return true;
        default: return false;
        }
    };
    const auto scalarInteger = [&](CArg kind) {
        switch (kind) {
        case CArg::BgmChannel: case CArg::BgmVolume: case CArg::BgmLoopCount:
        case CArg::SoundChannel: case CArg::SoundVolume: case CArg::SoundLoopCount:
        case CArg::UnsignedLevel: case CArg::SignedLevel: case CArg::SuccessRate:
        case CArg::TransitionSpeed: case CArg::Percent: case CArg::StepValue:
        case CArg::CouponValue: case CArg::PartyNumber: case CArg::CardNumber:
        case CArg::Money: case CArg::Wait: case CArg::LevelMin: case CArg::LevelMax:
        case CArg::InitValue: case CArg::Round: case CArg::X: case CArg::Y:
        case CArg::Width: case CArg::Height: case CArg::Scale: case CArg::Layer:
        case CArg::SelectionColumns: case CArg::CardSpeed: case CArg::XmlAssignment:
        case CArg::InitialSoundChannel: case CArg::InitialSoundVolume:
        case CArg::InitialSoundLoopCount: case CArg::BgmFadeIn:
        case CArg::SoundFadeIn: case CArg::InitialSoundFadeIn: return true;
        default: return false;
        }
    };
    const auto scalarId = [](CArg kind) {
        switch (kind) {
        case CArg::Area: case CArg::Battle: case CArg::Package: case CArg::Cast:
        case CArg::Item: case CArg::Skill: case CArg::Beast: case CArg::Info:
            return true;
        default: return false;
        }
    };
    const auto validateEnum = [&](CArg kind, const String& value,
                                  const Token& token, String& fallback) {
        auto errorField = &msgs::Msgs::scriptErrorInvalidValue;
        try {
#define CWX_VALIDATE_ENUM(ARG, TYPE, FIELD) \
            case CArg::ARG: errorField = &msgs::Msgs::FIELD; \
                fallback = enumName(TYPE{}); \
                (void)enumutils::stringToEnum<TYPE>(value); break;
            switch (kind) {
            CWX_VALIDATE_ENUM(Mental, types::Mental, scriptErrorInvalidMental)
            CWX_VALIDATE_ENUM(Physical, types::Physical, scriptErrorInvalidPhysical)
            CWX_VALIDATE_ENUM(Status, types::Status, scriptErrorInvalidStatus)
            CWX_VALIDATE_ENUM(Range, types::Range, scriptErrorInvalidRange)
            CWX_VALIDATE_ENUM(KeyCodeRange, types::Range, scriptErrorInvalidRange)
            CWX_VALIDATE_ENUM(CardVisual, types::CardVisual, scriptErrorInvalidCardVisual)
            case CArg::TalkerNC:
                errorField = &msgs::Msgs::scriptErrorInvalidTalker;
                fallback = enumName(types::Talker::Selected);
                if (enumutils::stringToEnum<types::Talker>(value) == types::Talker::Card) throw std::invalid_argument("card talker is forbidden here");
                break;
            CWX_VALIDATE_ENUM(EffectType, types::EffectType, scriptErrorInvalidEffectType)
            CWX_VALIDATE_ENUM(Resist, types::Resist, scriptErrorInvalidResist)
            CWX_VALIDATE_ENUM(Transition, types::Transition, scriptErrorInvalidTransition)
            CWX_VALIDATE_ENUM(SelectionMethod, types::SelectionMethod, scriptErrorInvalidSelectionMethod)
            CWX_VALIDATE_ENUM(Comparison4, types::Comparison4, scriptErrorInvalidComparison4)
            CWX_VALIDATE_ENUM(Comparison3, types::Comparison3, scriptErrorInvalidComparison3)
            CWX_VALIDATE_ENUM(PositionType, types::CoordinateType, scriptErrorInvalidCoordinateType)
            CWX_VALIDATE_ENUM(SizeType, types::CoordinateType, scriptErrorInvalidCoordinateType)
            CWX_VALIDATE_ENUM(StartAction, types::StartAction, scriptErrorInvalidStartAction)
            CWX_VALIDATE_ENUM(MatchingType, types::MatchingType, scriptErrorInvalidMatchingType)
            CWX_VALIDATE_ENUM(MatchingCondition, types::MatchingCondition, scriptErrorInvalidMatchingCondition)
            CWX_VALIDATE_ENUM(AbsorbTo, types::AbsorbTo, scriptErrorInvalidAbsorbTo)
            CWX_VALIDATE_ENUM(BackpackEnabled, types::EnvironmentStatus, scriptErrorInvalidEnvironmentStatus)
            CWX_VALIDATE_ENUM(GameOverEnabled, types::EnvironmentStatus, scriptErrorInvalidEnvironmentStatus)
            CWX_VALIDATE_ENUM(RunAwayEnabled, types::EnvironmentStatus, scriptErrorInvalidEnvironmentStatus)
            default: return true;
            }
#undef CWX_VALIDATE_ENUM
        } catch (const std::exception&) {
            throwError(scriptMessage(prop_, errorField, "invalid enum"), token);
            return false;
        }
        return true;
    };
    const auto prepareArgument = [&](CArg argumentKind, const Node& attr,
                                     const String& value, std::size_t argumentWidth) {
        // Dの配列パーサーは、VALUES（一つの構造化要素）とARRAY
        // （空白区切りの要素）を区別する。通常のイベントモデル
        // 引数を準備する前に、どちらも文字列へ平坦化しない。
        const auto elements = [&] {
            auto values = var(attr,vars);
            if (!values.empty() && values.front().type == NodeType::ARRAY)
                values = std::vector<Node>(values.front().childs);
            if (!values.empty() && values.front().token.kind == Kind::COMMA)
                values.clear();
            return values;
        };
        if (result.type == CType::BranchKeyCode && argumentKind == CArg::TargetIsSkill) {
            for (const auto& item : elements()) {
                const auto spelling = nodeValue(item, vars, 0);
                using E = types::EffectCardType;
                if (item.type != NodeType::VALUE ||
                    (spelling != "all" && spelling != "skill" && spelling != "item" &&
                     spelling != "beast" && spelling != "hand")) {
                    throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidEffectCardType,
                                             "invalid effect card type"), item.token);
                    result.effectCardTypes->push_back(E::All);
                    // Dは不正な要素を消費せずT.initを追加する。そのル
                    // ープを避けるため、先行要素があってもここで停止する。
                    break;
                }
                if (spelling == "all") result.effectCardTypes->push_back(E::All);
                else if (spelling == "skill") result.effectCardTypes->push_back(E::Skill);
                else if (spelling == "item") result.effectCardTypes->push_back(E::Item);
                else if (spelling == "beast") result.effectCardTypes->push_back(E::Beast);
                else if (spelling == "hand") result.effectCardTypes->push_back(E::Hand);
                else throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidEffectCardType,
                                              "invalid effect card type"), item.token);
            }
        } else if (argumentKind == CArg::TalkerC) {
            result.typedCardImages.emplace();
            for (const auto& imageNode : elements()) {
                lib::Owner<card::CardImage> image;
                if (imageNode.type == NodeType::VALUES) {
                    const auto fields = var(imageNode.childs,vars);
                    const String file = fields.empty() ? String{} : utils::decodePath(stringArgument(fields[0],0));
                    auto position = types::CardImagePosition::Default;
                    if (fields.size()>1 && fields[1].token.kind == Kind::SYMBOL) {
                        const auto spelling = attrValue(fields[1],vars,0);
                        if (spelling=="center") position=types::CardImagePosition::Center;
                        else if (spelling=="topleft") position=types::CardImagePosition::TopLeft;
                        else if (spelling!="default") throwError(scriptMessage(prop_,&msgs::Msgs::scriptErrorInvalidCardImagePosition,"invalid card image position"),fields[1].token);
                    }
                    image=std::make_shared<card::CardImage>(file,position);
                } else if (imageNode.token.kind==Kind::STRING) {
                    image=std::make_shared<card::CardImage>(utils::decodePath(nodeValue(imageNode,vars,0)));
                } else if (imageNode.token.kind==Kind::SYMBOL) {
                    const auto spelling=lowerAscii(nodeValue(imageNode,vars,0));
                    if (spelling=="n" || spelling=="none") continue;
                    try { image=std::make_shared<card::CardImage>(enumutils::stringToEnum<types::Talker>(normalizeScriptArgument(CArg::TalkerNC,spelling,Kind::SYMBOL,prop_))); }
                    catch (const std::invalid_argument&) {
                        throwError(scriptMessage(prop_,&msgs::Msgs::scriptErrorInvalidTalker,"invalid talker"),imageNode.token);
                        image=std::make_shared<card::CardImage>(types::Talker::Selected);
                    }
                }
                // Dは空のファイル画像と非正のPC番号画像をここで除
                // く。話者の仮画像はファイルパスがなくても保持する。
                if (image && (image->type!=types::CardImageType::File || !image->path().empty()))
                    result.typedCardImages->push_back(std::move(image));
            }
        } else if (argumentKind == CArg::Dialogs) {
            result.dialogs.emplace();
            for (const auto& dialogNode : elements()) {
                if (dialogNode.type!=NodeType::VALUES) {
                    throwError(scriptMessage(prop_,&msgs::Msgs::scriptErrorInvalidDialog,"invalid dialog"),dialogNode.token);
                    result.dialogs->push_back(std::make_shared<event::SDialog>());
                    continue;
                }
                const auto fields=var(dialogNode.childs,vars);
                lib::Array<String> required;
                if (!fields.empty()) {
                    if (fields[0].type==NodeType::ARRAY) {
                        const auto& conditions = fields[0].childs;
                        if (conditions.empty() || conditions.front().token.kind != Kind::COMMA)
                            for (const auto& field : conditions) required.push_back(stringArgument(field,argumentWidth));
                    } else {
                        const auto names=stringArgument(fields[0],argumentWidth);
                        std::size_t start{};
                        do {const auto end=names.find(';',start);required.push_back(names.substr(start,end-start));if(end==String::npos)break;start=end+1;} while(start<=names.size());
                    }
                }
                const auto text=fields.size()>1 ? stringArgument(fields[1],argumentWidth) : String{};
                result.dialogs->push_back(std::make_shared<event::SDialog>(text,std::move(required)));
            }
        } else if (argumentKind == CArg::Coupons) {
            result.coupons.emplace();
            std::unordered_set<String> names;
            for (const auto& couponNode : elements()) {
                auto name=attrValue(couponNode,vars,0);int amount=1;
                if (couponNode.type==NodeType::VALUES) {
                    const auto fields=var(couponNode.childs,vars);
                    name=fields.empty()?String{}:stringArgument(fields[0],0);
                    std::size_t fieldIndex = 1;
                    amount=readInteger(fields,fieldIndex,0);
                }
                if (names.insert(name).second) result.coupons->push_back(std::make_shared<coupon::Coupon>(std::move(name),amount));
            }
        } else if (argumentKind == CArg::KeyCodes || argumentKind == CArg::CouponNames) {
            lib::Array<String> values;
            std::unordered_set<String> seen;
            for (const auto& item : elements()) {
                auto text=stringArgument(item,0);
                // Dの名前を一意にする規則はCouponNamesだけに適用する。KeyCodes
                // は重複と空項目を維持する（Dのscript.d3210-3230）。
                if (argumentKind==CArg::KeyCodes || seen.insert(text).second) values.push_back(std::move(text));
            }
            if (argumentKind==CArg::KeyCodes) result.keyCodes=std::move(values);
            else result.couponNames=std::move(values);
        } else if (argumentKind == CArg::CastRange) {
            if (!result.castRanges) result.castRanges.emplace();
            auto& ranges=*result.castRanges;
            const auto spelling=lowerAscii(value);
            if (spelling=="field") ranges.insert(ranges.end(),{types::CastRange::Party,types::CastRange::Enemy,types::CastRange::Npc});
            else ranges.push_back(spelling=="enemy" ? types::CastRange::Enemy : spelling=="npc" ? types::CastRange::Npc : types::CastRange::Party);
            std::sort(ranges.begin(),ranges.end());
            ranges.erase(std::unique(ranges.begin(),ranges.end()),ranges.end());
        }
        if (argumentKind == CArg::BgImages || argumentKind == CArg::Motions)
        for (const auto& typed : elements()) {
        const auto& attr = typed;
        if (attr.type != NodeType::VALUES && attr.type != NodeType::ARRAY) {
            if (argumentKind == CArg::Motions) {
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidMotion,
                                         "invalid motion"), attr.token);
                result.motions.push_back(std::make_shared<motion::Motion>(types::MType::Heal, types::Element::All));
            } else if (argumentKind == CArg::BgImages) {
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidBgImage,
                                         "invalid background"), attr.token);
                result.backgrounds.push_back(nullptr);
            }
        }
        if (attr.type == NodeType::VALUES || attr.type == NodeType::ARRAY) {
            if (argumentKind == CArg::BgImages) {
                result.backgroundValues.push_back(value);
                const std::vector<Node> groups{attr};
                for (const auto& group : groups) {
                    if (group.type != NodeType::VALUES) {
                        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidBgImage,
                                                 "invalid background"), group.token);
                        result.backgrounds.push_back(nullptr);
                        // Dは項目を消費せずnullを返す。位置が進まない配列
                        // ループを避けるため、コピーした範囲の処理を停止する。
                        break;
                    }
                    const auto fields = var(group.childs, vars);
                    std::size_t index{};
                    String cellType = "image";
                    if (!fields.empty() && fields.front().token.kind == Kind::SYMBOL)
                        cellType = nodeValue(fields[index++], vars, 0);
                    background::BgImagePtr cell;
                    if (cellType == "image") {
                        const auto path = utils::decodePath(readString(fields, index));
                        cell = std::make_shared<background::ImageCell>(path, "", 0, 0, 0, 0, false);
                    } else if (cellType == "pc") {
                        const auto number = readInteger(fields, index, 1);
                        const auto expand = readBoolean(fields, index, false);
                        cell = std::make_shared<background::PCCell>(
                            static_cast<std::uint32_t>(number), expand, "", 0, 0, 0, 0, false);
                    } else if (cellType == "color") {
                        const auto blend = readEnum(fields, index, types::BlendMode::Normal,
                            [](StringView value) {
                                if (value == "normal") return types::BlendMode::Normal;
                                if (value == "add") return types::BlendMode::Add;
                                if (value == "sub") return types::BlendMode::Subtract;
                                if (value == "mul") return types::BlendMode::Multiply;
                                throw std::invalid_argument("invalid blend mode");
                            }, &msgs::Msgs::scriptErrorInvalidBlendMode);
                        const auto first = readColor(fields, index, {255, 255, 255, 255});
                        auto gradient = types::GradientDir::None;
                        auto second = first;
                        if (index < fields.size() && fields[index].token.kind == Kind::SYMBOL) {
                            gradient = readEnum(fields, index, gradient, [](StringView value) {
                                if (value == "none") return types::GradientDir::None;
                                if (value == "h" || value == "horizontal") return types::GradientDir::LeftToRight;
                                if (value == "v" || value == "vertical") return types::GradientDir::TopToBottom;
                                throw std::invalid_argument("invalid gradient direction");
                            }, &msgs::Msgs::scriptErrorInvalidGradientDir);
                            second = readColor(fields, index, second);
                        }
                        cell = std::make_shared<background::ColorCell>(
                            blend, gradient, first, second, "", 0, 0, 0, 0, false);
                    } else if (cellType == "text") {
                        const auto text = readString(fields, index);
                        const auto font = readString(fields, index);
                        const auto size = readInteger(fields, index, 18);
                        const auto color = readColor(fields, index, {0, 0, 0, 255});
                        bool bold{}, italic{}, underline{}, strike{}, vertical{}, antialias{};
                        auto border = types::BorderingType::None;
                        bool invalidStyle = false;
                        const auto style = [&](const Node& item) {
                            const auto value = nodeValue(item, vars, 0);
                            if (value == "bold") bold = true;
                            else if (value == "italic") italic = true;
                            else if (value == "underline" || value == "uline") underline = true;
                            else if (value == "strike") strike = true;
                            else if (value == "vertical") vertical = true;
                            else if (value == "antialias") antialias = true;
                            else if (value == "border1") border = types::BorderingType::Outline;
                            else if (value == "border2") border = types::BorderingType::Inline;
                            else {
                                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidKeyword2,
                                                         "invalid text style"), item.token);
                                invalidStyle = true;
                            }
                        };
                        if (index < fields.size()) {
                            const auto& item = fields[index];
                            if (!item.childs.empty() || item.type == NodeType::ARRAY) {
                                for (const auto& part : var(item.childs, vars)) {
                                    style(part);
                                    if (invalidStyle) break;
                                }
                                ++index;
                            } else if (item.token.kind == Kind::SYMBOL) {
                                style(item);
                                ++index;
                            } else {
                                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidArray,
                                                         "invalid text style array"), item.token);
                                invalidStyle = true;
                            }
                        }
                        if (invalidStyle) {
                            // Dはここで空のImageCellを返す。要素範
                            // 囲のコピーを巡回することで、外側の配列パーサ
                            // ーが位置の進まないループに陥ることも避ける。
                            result.backgrounds.push_back(std::make_shared<background::ImageCell>(
                                "", "", 0, 0, 0, 0, false));
                            continue;
                        }
                        auto borderColor = structs::CRGB{255, 255, 255, 255};
                        std::int32_t borderWidth = 1;
                        if (border != types::BorderingType::None) {
                            borderColor = readColor(fields, index, borderColor);
                            if (border == types::BorderingType::Inline)
                                borderWidth = readInteger(fields, index, borderWidth);
                        }
                        cell = std::make_shared<background::TextCell>(
                            text, font, static_cast<std::uint32_t>(size), color, bold, italic,
                            underline, strike, vertical, antialias, border, borderColor,
                            static_cast<std::uint32_t>(borderWidth), types::UpdateType::Fixed,
                            "", 0, 0, 0, 0, false);
                    } else {
                        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidBgImage,
                                                 "invalid background type"), group.token);
                        result.backgrounds.push_back(nullptr);
                        break;
                    }
                    cell->flag(readString(fields, index));
                    cell->x(readInteger(fields, index, 0));
                    cell->y(readInteger(fields, index, 0));
                    const auto size = prop_ ? prop_->looks().viewSize() : structs::CSize{};
                    cell->width(readInteger(fields, index, static_cast<std::int32_t>(size.width)));
                    cell->height(readInteger(fields, index, static_cast<std::int32_t>(size.height)));
                    if (std::dynamic_pointer_cast<background::ImageCell>(cell))
                        cell->mask(readBoolean(fields, index, cell->mask()));
                    cell->cellName(readString(fields, index, cell->cellName()));
                    cell->layer(readInteger(fields, index, cell->layer()));
                    const auto smoothing = [&](types::Smoothing fallback) {
                        if (index >= fields.size()) return fallback;
                        if (fields[index].token.kind != Kind::SYMBOL) return types::Smoothing{};
                        if (nodeValue(fields[index], vars, 0) == "default") {
                            ++index;
                            return types::Smoothing::Default;
                        }
                        return readBoolean(fields, index, true) ? types::Smoothing::True : types::Smoothing::False;
                    };
                    if (auto image = std::dynamic_pointer_cast<background::ImageCell>(cell))
                        image->smoothing(smoothing(image->smoothing()));
                    else if (auto pc = std::dynamic_pointer_cast<background::PCCell>(cell))
                        pc->smoothing(smoothing(pc->smoothing()));
                    if (auto text = std::dynamic_pointer_cast<background::TextCell>(cell);
                        text && index < fields.size() && fields[index].token.kind == Kind::SYMBOL) {
                        const auto update = nodeValue(fields[index], vars, 0);
                        if (update == "fixed") { text->updateType(types::UpdateType::Fixed); ++index; }
                        else if (update == "variables") { text->updateType(types::UpdateType::Variables); ++index; }
                        else if (update == "all") { text->updateType(types::UpdateType::All); ++index; }
                        else throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidUpdateType,
                                                      "invalid update type"), fields[index].token);
                    }
                    result.backgrounds.push_back(std::move(cell));
                }
            }
            if (result.type == CType::Effect || result.type == CType::MoveCard)
                result.motionValues.push_back(value);
            if (argumentKind == CArg::Motions) {
                // Dは配列と各VALUESタプルを別々に展開する。タ
                // プル内の変数は複数の項目を供給できるため、一つの文
                // 字列へ平坦化すると境界とトークン種別が失われる。
                const std::vector<Node> groups{attr};
                for (const auto& group : groups) {
                    if (group.type != NodeType::VALUES) {
                        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidMotion,
                                                 "invalid motion"), group.token);
                        result.motions.push_back(std::make_shared<motion::Motion>(types::MType::Heal, types::Element::All));
                        continue;
                    }
                    const auto fields = var(group.childs, vars);
                    std::size_t index = 0;
                    const auto token = [&]() -> const Token& {
                        return index < fields.size() ? fields[index].token : group.token;
                    };
                    const auto motionType = readEnum(fields, index, types::MType::Heal, scriptMotionType,
                        &msgs::Msgs::scriptErrorInvalidMotionType);
                    auto parsed = std::make_shared<motion::Motion>(motionType,
                                                                   types::Element::All);
                    const auto detail = parsed->detail();
                    if (detail.use(types::MArg::ValueType)) {
                        const auto fallback =
                            motionType == types::MType::GetSkillPower ||
                                    motionType == types::MType::LoseSkillPower
                                ? types::DamageType::Max
                                : types::DamageType::LevelRatio;
                        const auto damageToken = token();
                        auto damage = readEnum(fields, index, fallback, [](StringView value) {
                            if (value.empty()) throw std::invalid_argument("empty damage type");
                            return scriptDamageType(value, types::DamageType::LevelRatio);
                        }, &msgs::Msgs::scriptErrorInvalidDamageType);
                        const bool skillPower = motionType == types::MType::GetSkillPower || motionType == types::MType::LoseSkillPower;
                        if (skillPower && damage != types::DamageType::Max && damage != types::DamageType::Fixed) {
                            damage = types::DamageType::Max;
                            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidUpDownSkillPowerType, "invalid skill power type"),damageToken);
                        } else if (!skillPower && damage == types::DamageType::Fixed) {
                            damage = types::DamageType::LevelRatio;
                            throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidDamageType, "invalid damage type"),damageToken);
                        }
                        parsed->damageType(damage);
                    }
                    if (detail.use(types::MArg::UValue))
                        parsed->uValue(readInteger(fields, index, static_cast<std::int32_t>(parsed->uValue())));
                    if (detail.use(types::MArg::AValue))
                        parsed->aValue(readInteger(fields, index, parsed->aValue()));
                    if (detail.use(types::MArg::Round))
                        parsed->round(readInteger(fields, index, parsed->round()));
                    if (detail.use(types::MArg::Beast)) {
                        std::uint64_t id{};
                        if (index < fields.size()) {
                            const auto spelling = nodeValue(fields[index], vars, 0);
                            if (const auto parsedId = scriptInteger<std::uint64_t>(spelling)) {
                                id = *parsedId;
                                ++index;
                            } else {
                                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorReqID,
                                                         "invalid beast ID"), token());
                            }
                        }
                        if (id != 0U && summ_) {
                            if (opt.linkId) {
                                auto card = std::make_shared<card::BeastCard>(
                                    prop_ ? &prop_->sys() : nullptr, 1U, String{},
                                    lib::Array<lib::Owner<card::CardImage>>{}, String{});
                                card->linkId(id);
                                parsed->newBeast(card);
                            } else {
                                // Dはここでコピーするbeastプロパティを使う。シナリ
                                // オの正規のカードはSummaryの所有のままにする。
                                parsed->beast(summ_->beast(id));
                            }
                        }
                    }
                    parsed->element(readEnum(fields, index, types::Element::All, scriptElement,
                                             &msgs::Msgs::scriptErrorInvalidElement));
                    result.motions.push_back(std::move(parsed));
                }
            }
        }
        }
    };
    bool singleLine = false;
    std::vector<Kind> argumentTokenKinds;
    argumentTokenKinds.reserve(node.attr.size());
    for (const auto& attr : node.attr) argumentTokenKinds.push_back(attr.token.kind);
    const auto argumentValue = [&](std::size_t index) {
        return attrValue(node.attr[index], vars, 0);
    };
    const auto readArgument = [&](CArg kind, std::size_t source) -> ScriptArgumentRead {
        const auto& attr = node.attr[source];
        if (kind == CArg::SingleLine) { singleLine = true; return {"true", 1}; }
        if (kind == CArg::Text) {
            const auto width = prop_ ? static_cast<std::size_t>(std::max(0,
                result.typedCardImages && !result.typedCardImages->empty() ?
                    prop_->looks().messageImageLen() : prop_->looks().messageLen())) : autoWrap_;
            auto text = stringArgument(attr, width);
            if (singleLine && !text.empty()) text = utils::splitLines(text).front();
            return {std::move(text), 1};
        }
        if (scriptArrayArgument(kind)) {
            const auto width = kind == CArg::Dialogs ?
                (prop_ ? static_cast<std::size_t>(std::max(0, prop_->looks().messageImageLen())) : autoWrap_) : 0U;
            auto value = nodeCode(attr, "");
            prepareArgument(kind, attr, value, width);
            return {std::move(value), 1};
        }
        if (kind == CArg::TargetS) {
            auto value = argumentValue(source);
            std::size_t sleepIndex = source + 1;
            const bool sleep = readBoolean(node.attr, sleepIndex, false);
            if (value == "m" || value == "selected") value = "selected";
            else if (value == "u" || value == "unselected") value = "unselected";
            else if (value == "r" || value == "random" || value == "one") value = "random";
            else if (value == "t" || value == "team") value = "party";
            else {
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidTarget,
                                         "invalid target"), attr.token);
                return {"selected", 0};
            }
            if (sleep) value += "sleep";
            return {std::move(value), 2};
        }
        if ((kind == CArg::BackpackEnabled || kind == CArg::GameOverEnabled ||
             kind == CArg::RunAwayEnabled) && attr.token.kind != Kind::SYMBOL)
            return {enumName(types::EnvironmentStatus{}), 0};
        if (kind == CArg::BgmFadeIn || kind == CArg::SoundFadeIn ||
            kind == CArg::InitialSoundFadeIn) {
            const auto value = argumentValue(source);
            const auto seconds = scriptReal(value);
            if (!seconds) {
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorReqNumber,
                                         "invalid fade-in number"), attr.token);
                // Dはこの診断後にreal.init（NaN）を返し、呼び出し側
                // のroundTo!uintが例外を送出する。0は代入しない。
                throw std::out_of_range("fade-in roundTo!uint of invalid real");
            }
            // Phobos v2.070のstd.conv.roundToは、符号付きの0.5
            // を足し、切り捨ててから範囲検査付きで変換する。小さい負値は0へ丸められる。 https://github.com/dlang/phobos/blob/v2.070.0/std/conv.d
            // C++はネイティブのlong double
            // を使う。コンパイラーによってDのrealと精
            // 度が異なり得るが、OS別の規則は導入しない。
            const auto scaled = *seconds * 100.0L;
            const auto rounded = std::trunc(scaled + (scaled < 0 ? -0.5L : 0.5L));
            if (!std::isfinite(rounded) || rounded < 0 ||
                rounded > static_cast<long double>(std::numeric_limits<std::uint32_t>::max()))
                throw std::out_of_range("fade-in roundTo!uint overflow");
            return {std::to_string(static_cast<std::uint32_t>(rounded)), 1};
        }
        if (kind == CArg::CastRange) {
            const auto value = argumentValue(source);
            if (value != "field" && value != "party" && value != "t" &&
                value != "team" && value != "enemy" && value != "npc") {
                result.castRanges.emplace();
                return {"", 0};
            }
        }
        auto value = normalizeScriptArgument(kind, argumentValue(source), attr.token.kind, prop_);
        if (scalarBoolean(kind)) {
            if (value != "true" && value != "false") {
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidBoolVal,
                                         "invalid boolean"), attr.token);
                return {"false", 0};
            }
        } else if (scalarInteger(kind)) {
            if (!validateNumber(value, false, "integer", attr.token)) return {"0", 0};
        } else if (scalarId(kind)) {
            if (!validateNumber(value, true, "id", attr.token)) return {"0", 0};
        } else {
            String fallback;
            if (!validateEnum(kind, value, attr.token, fallback))
                return {std::move(fallback), kind == CArg::TalkerNC ? 1U : 0U};
        }
        if (kind == CArg::CastRange) prepareArgument(kind, attr, value, 0);
        return {std::move(value), 1};
    };
    const auto invalidVariable = [&](std::size_t source) {
        throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidVariableType,
                                 "invalid variable type"), node.attr[source].token);
    };
    auto layout = argumentLayout(result.type, node.attr.size(), argumentTokenKinds,
                                 argumentValue, readArgument, invalidVariable);
    // Dのモデル出力と異なり、この追加APIは構文レコードを保持する。
    // 実際の型付き読み取りで得た値だけを使う。ここでソースを再評価する
    // と、Dが意図的に読まない未使用のタプル項目まで診断してしまう。
    std::size_t argumentWidth=autoWrap_;
    bool expandVariableArray = false;
    std::unordered_set<String> exportingVariables;
    std::function<String(const Node&)> resolvedCode = [&](const Node& value) -> String {
        if (value.type == NodeType::VALUES || value.type == NodeType::ARRAY) {
            String code = value.type == NodeType::VALUES ? "[" : "";
            for (std::size_t i=0;i<value.childs.size();++i) {
                if (i) code += value.type == NodeType::VALUES ? ", " : " ";
                code += resolvedCode(value.childs[i]);
            }
            if (value.type == NodeType::VALUES) code += "]";
            return code;
        }
        if (value.token.kind == Kind::VAR_NAME && value.calc.size() <= 1) {
            const auto name = lowerAscii(value.token.value);
            const auto found = vars.find(name);
            if (found == vars.end() || !exportingVariables.insert(name).second)
                return nodeCode(value, "");
            String code;
            const auto count = expandVariableArray ? found->second.size()
                                                   : std::min<std::size_t>(1, found->second.size());
            for (std::size_t index = 0; index < count; ++index) {
                if (!code.empty()) code += " ";
                code += resolvedCode(found->second[index]);
            }
            exportingVariables.erase(name);
            return code;
        }
        const auto read = readValues.find({value.token.line, value.token.pos, argumentWidth});
        return read != readValues.end() ? read->second : nodeCode(value, "");
    };
    for (std::size_t slot = 0; slot < layout.kinds.size(); ++slot) {
        const auto sourceIndex = layout.sources[slot];
        const auto& attr = node.attr[sourceIndex];
        String value = layout.values[slot];
        const std::optional<CArg> argumentKind = layout.kinds[slot];
        result.argumentKinds.push_back(*argumentKind);
        expandVariableArray = scriptArrayArgument(*argumentKind);
        argumentWidth=0;
        if (argumentKind==CArg::Text)
            argumentWidth=prop_ ? static_cast<std::size_t>(std::max(0,
                result.typedCardImages && !result.typedCardImages->empty() ?
                    prop_->looks().messageImageLen() : prop_->looks().messageLen())) : autoWrap_;
        else if (argumentKind==CArg::Dialogs)
            argumentWidth=prop_ ? static_cast<std::size_t>(std::max(0,prop_->looks().messageImageLen())) : autoWrap_;
        result.arguments.push_back(argumentKind==CArg::Text ? createString(value) : resolvedCode(attr));
        result.attributes.emplace_back("arg" + std::to_string(argumentIndex++), value);
    }
    if (event::contentDetail(result.type).use(CArg::Dialogs) &&
        (!result.dialogs || result.dialogs->empty()))
        result.dialogs=lib::Array<lib::Owner<event::SDialog>>{std::make_shared<event::SDialog>()};
    return result;
}

void CWXScript::collectChildren(const Node& node, Content& output,
                                const NodeTable& parentVars,
                                const CompileOption& opt, std::size_t depth) {
    NodeTable vars = parentVars;
    for (const auto& variable : node.beforeVars) {
        const auto values = varValue(variable, variable.texts, vars, autoWrap_);
        vars[lowerAscii(variable.token.value)] = values;
    }
    for (const auto& child : node.childs) {
        for (const auto& variable : child.beforeVars) {
            const auto values = varValue(variable, variable.texts, vars, autoWrap_);
            vars[lowerAscii(variable.token.value)] = values;
        }
        if (child.type == NodeType::VAR_SET) {
            const auto values = varValue(child, child.texts, vars, autoWrap_);
            vars[lowerAscii(child.token.value)] = values;
            continue;
        }
        Content nested = makeContent(child, vars, opt, depth + 1);
        collectChildren(child, nested, vars, opt, depth + 1);
        output.children.push_back(std::move(nested));
    }
}

Contents CWXScript::analyzeSemantics(const std::vector<Node>& nodes,
                                     const CompileOption& opt) {
    NodeTable vars;
    std::vector<String> startNames;
    auto contents = analyzeSemanticsImpl(nodes, vars, opt, 0, startNames);
    return contents;
}

Contents CWXScript::analyzeSemanticsImpl(const std::vector<Node>& nodes,
                                         NodeTable& vars,
                                         const CompileOption& opt,
                                         std::size_t depth,
                                         std::vector<String>& startNames) {
    // Dは生成したルートを追加する間、参照が安定したContentオブジェ
    // クトを保持する。C++の公開スクリプトContentは値レコードなの
    // で、解析中は一つの参照関係を構築し、接続完了後にレコードを生成する。
    struct Entry {
        Content value;
        std::vector<std::shared_ptr<Entry>> children;
    };
    using EntryPtr = std::shared_ptr<Entry>;
    using Group = std::vector<EntryPtr>;
    std::size_t autoWrapCount{};
    const auto uniqueName = [&](String name) {
        name = utils::createNewName(std::move(name), [&](const String& candidate) {
            return std::find(startNames.begin(), startNames.end(), candidate) == startNames.end();
        });
        startNames.push_back(name);
        return name;
    };
    std::function<Group(const std::vector<Node>&, std::size_t, std::size_t, Group&, bool)> parse;
    parse = [&](const std::vector<Node>& source, std::size_t stack,
                std::size_t nesting, Group& topGroup, bool isTop) -> Group {
        Group result;
        EntryPtr lastParent;
        bool nextIsChild = false;
        const auto bindVariable = [&](const Node& variable) {
            if (variable.type != NodeType::VAR_SET)
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidVar, "invalid variable"), variable.token);
            if (variable.texts.empty()) {
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorNoVarVal, "variable value expected"), variable.token);
            } else {
                vars[lowerAscii(variable.token.value)] = var(variable.texts, vars);
            }
        };
        const auto cannotHaveContent = [&](const EntryPtr& parent, const Token& token) {
            throwError(utils::tryFormat(scriptMessage(prop_,
                &msgs::Msgs::scriptErrorCanNotHaveContent, "content cannot have children"),
                prop_ ? prop_->requiredMsgs().contentName(parent->value.type) : String{}), token);
        };
        const auto autoWrap = [&](const EntryPtr& parent) -> EntryPtr {
            if (autoWrap_ > stack) return parent;
            stack = 0;
            auto continuation = std::make_shared<Entry>();
            continuation->value.type = CType::Start;
            continuation->value.keyword = "start";
            continuation->value.name = uniqueName("Auto wrap (" + std::to_string(++autoWrapCount) + ")");
            continuation->value.linkCardIds = parent->value.linkCardIds;
            continuation->value.sourceLine = parent->value.sourceLine;
            continuation->value.sourcePos = parent->value.sourcePos;
            auto link = std::make_shared<Entry>();
            link->value.type = CType::LinkStart;
            link->value.keyword = "gostart";
            link->value.name = parent->value.name;
            link->value.arguments.push_back(createString(continuation->value.name));
            link->value.argumentKinds.push_back(CArg::Start);
            link->value.attributes.emplace_back("arg0", continuation->value.name);
            link->value.linkCardIds = parent->value.linkCardIds;
            link->value.sourceLine = parent->value.sourceLine;
            link->value.sourcePos = parent->value.sourcePos;
            parent->children.push_back(std::move(link));
            (isTop ? result : topGroup).push_back(continuation);
            return continuation;
        };
        for (const auto& node : source) {
            for (const auto& variable : node.beforeVars) bindVariable(variable);
            if (node.type == NodeType::VAR_SET) { bindVariable(node); continue; }
            if (node.type != NodeType::COMMAND && node.type != NodeType::START)
                throwError(scriptMessage(prop_, &msgs::Msgs::scriptErrorInvalidCommand, "invalid command"), node.token);
            if (node.type == NodeType::START) stack = 0;
            auto current = std::make_shared<Entry>();
            current->value = makeContent(node, vars, opt, nesting);
            if (node.type == NodeType::START)
                current->value.name = uniqueName(std::move(current->value.name));
            if (nextIsChild && lastParent) {
                if (!event::contentDetail(lastParent->value.type).owner) {
                    cannotHaveContent(lastParent, node.token);
                } else {
                    // Dの参照動作への意図的な修正。末端のLinkStartは葉であ
                    // り、直列化した分割を再解析したときに親全体を再び自動で包んで
                    // はならない。そうすると、空の自己リンクした区間が出力される。
                    const bool terminalLink = current->value.type == CType::LinkStart &&
                        node.childs.empty() && !node.nextIsChild;
                    auto parent = terminalLink ? lastParent : autoWrap(lastParent);
                    parent->children.push_back(current);
                    ++stack;
                }
            } else result.push_back(current);
            if (!node.childs.empty()) {
                if (!event::contentDetail(current->value.type).owner) {
                    cannotHaveContent(current, node.token);
                } else {
                    auto parent = autoWrap(current);
                    auto children = parse(node.childs, stack + 1, nesting + 1,
                                          isTop ? result : topGroup, false);
                    // これらの公開C++レコードは解析済みの分岐ラベルを
                    // 保持する。モデルへの設定にはContent::add
                    // を使い、Dが解析時に行う名前の正規化を共有する。
                    parent->children.insert(parent->children.end(), children.begin(), children.end());
                }
            }
            nextIsChild = node.nextIsChild;
            if (nextIsChild) lastParent = current;
        }
        return result;
    };
    Group unused;
    auto roots = parse(nodes, 0, depth, unused, true);
    const auto materialize = [&](const auto& self, const EntryPtr& entry) -> Content {
        Content value = std::move(entry->value);
        for (const auto& child : entry->children) value.children.push_back(self(self, child));
        return value;
    };
    Contents result;
    result.reserve(roots.size());
    for (const auto& root : roots) result.push_back(materialize(materialize, root));
    return result;
}
std::vector<String> CWXScript::toScript(const Contents& contents,
                                        StringView evtChildOK, bool legacy,
                                        StringView indent) const {
    std::vector<String> lines;
    VarTable variables;
    const auto textAttribute=[&](const String& spelling,const Content& content,const String& padding, bool expression=false) {
        if (spelling.empty() || (spelling.front()!='"' && spelling.front()!='\'')) return spelling;
        CWXScript reader(prop_,summ_);
        Token token;token.kind=Kind::STRING;token.value=spelling;
        const auto text=reader.stringValue(token,0);
        auto rows=utils::splitLines(text);
        // DのtoAttrは、空値を常に二重引用符二つで書く。通常は一重引用符を使うExpression
        // 引数（createString(..., true)）でも同様。
        if (rows.empty()) return String("\"\"");
        if (rows.size()<2) return createString(rows.front(),expression);
        std::size_t leading{};
        for(std::size_t i=0;i<rows.size();++i) if(!rows[i].empty()){leading=i;break;}
        const auto width=expression ? 0U : prop_ ? static_cast<std::size_t>(std::max(0,
            content.typedCardImages && !content.typedCardImages->empty() ?
                prop_->looks().messageImageLen() : prop_->looks().messageLen())) : autoWrap_;
        String result="@";
        if(leading && !expression) {
            result += variables.useCenter && leading+1==stringCenter(rows,width) ?
                " center" : " "+std::to_string(leading+1);
            rows.erase(rows.begin(),rows.begin()+static_cast<std::ptrdiff_t>(leading));
        }
        result+='\n';
        for(auto row:rows) {
            String escaped;
            for(char c:row){escaped+=c;if(c=='@')escaped+='@';}
            if(!escaped.empty()&&(escaped.front()==' '||escaped.front()=='\t'||escaped.front()=='\\')) escaped.insert(escaped.begin(),'\\');
            result+=padding+escaped+'\n';
        }
        result += rows.back().empty() ? "@" : padding+"@";
        return result;
    };
    // 単独の二重引用符付き引数。step, "x"の複合形式ではない。引用符内の文字列を使い、二重引用符二つのエスケープを解除する。
    const auto plainQuoted=[](const String& spelling) {
        return spelling.size()>=2 && spelling.front()=='"' && spelling.back()=='"';
    };
    const auto unquoted=[](const String& spelling) {
        String result;
        for (std::size_t i=1;i+1<spelling.size();++i) {
            result.push_back(spelling[i]);
            if (spelling[i]=='"' && i+2<spelling.size() && spelling[i+1]=='"') ++i;
        }
        return result;
    };
    const auto typedId=[&](CArg kind,String value) {
        if (!summ_) return value;
        switch (kind) {
        case CArg::Area:case CArg::Battle:case CArg::Package:case CArg::Cast:
        case CArg::Skill:case CArg::Item:case CArg::Beast:case CArg::Info:break;
        default:return value;
        }
        std::uint64_t id{};
        String numeric=value;
        if (!numeric.empty() && (numeric.front()=='"' || numeric.front()=='\'')) {
            CWXScript reader(prop_,summ_);Token token;token.kind=Kind::STRING;token.value=numeric;
            numeric=reader.stringValue(token,0);
        }
        StringView input=numeric;
        if (!input.empty() && input.front()=='+') input.remove_prefix(1);
        const auto conversion=std::from_chars(input.data(),input.data()+input.size(),id);
        if(conversion.ec!=std::errc{} || conversion.ptr!=input.data()+input.size()) return value;
        const auto record=[&](StringView category,const auto& owner) {
            return owner ? variables.id(category,owner->name(),owner->id()).symbol : value;
        };
        switch(kind) {
        case CArg::Area:return record("area",summ_->area(id));
        case CArg::Battle:return record("battle",summ_->battle(id));
        case CArg::Package:return record("pack",summ_->cwPackage(id));
        case CArg::Cast:return record("cast",summ_->cwCast(id));
        case CArg::Skill:return record("skill",summ_->skill(id));
        case CArg::Item:return record("item",summ_->item(id));
        case CArg::Beast:return record("beast",summ_->beast(id));
        case CArg::Info:return record("info",summ_->info(id));
        default:return value;
        }
    };
    const auto typedMotions = [&](const Content& content, const String& fallback) {
        if (content.motions.empty()) return fallback;
        String result;
        for (const auto& motion : content.motions) {
            if (!result.empty()) result += " ";
            if (!motion) { result += "[]"; continue; }
            String beastId = "0";
            if (motion->detail().use(types::MArg::Beast) && summ_) {
                if (const auto beast = motion->beast()) {
                    // リンクされた効果内容はid=1の一時カードを所有する。
                    // DはID変数を選ぶ前に、このSummaryでlinkId
                    // または一致する非リンクの複製を解決する。vars.id(findSameBeast(beast),
                    // beast.linkId)は、変数
                    // が無効なら一致カードのIDではなく、
                    // 効果内容自身のlinkIdを使う。
                    if (const auto same = summ_->findSameBeast(*beast); same && variables.useVar)
                        beastId = variables.id("beast", same->name(), same->id()).symbol;
                    else
                        beastId = std::to_string(beast->linkId());
                }
            }
            result += motionToScript(*motion, beastId);
        }
        return result;
    };
    const auto legacyOnly = [](const CArg arg) {
        return arg == CArg::TransitionSpeed || arg == CArg::Transition ||
               arg == CArg::DoAnime || arg == CArg::IgnoreEffectBooster;
    };
    std::function<void(const Content&, std::size_t)> append =
        [&](const Content& content, std::size_t level) {
            String padding;
            for (std::size_t i = 0; i < level; ++i) padding += indent;
            // Dは利用者のコメントを、その所属コマンドの直前に
            // 保持する。クリップボードへの直列化で型付きContent
            // を変えないよう、表示用の境界で扱う。
            if (!content.comment.empty()) {
                std::size_t begin = 0U;
                while (begin < content.comment.size()) {
                    const auto end = content.comment.find('\n', begin);
                    lines.push_back(padding + "// " +
                                    content.comment.substr(begin, end - begin));
                    if (end == String::npos) break;
                    begin = end + 1U;
                }
            }
            String line = padding + (content.keyword.empty()
                                         ? keywordFor(content.type)
                                         : content.keyword);
            // Startの引用符付きの名前をここで作る。コマンド行の組み立て前に無
            // 効参照になるStringViewではなく、所有する文字列を保存する。
            std::vector<String> outputArguments;
            if (content.type == CType::Start)
                outputArguments.push_back(createString(content.name));
            for (std::size_t i = 0; i < content.arguments.size(); ++i) {
                if (legacy && i < content.argumentKinds.size() &&
                    legacyOnly(content.argumentKinds[i])) continue;
                if (i<content.argumentKinds.size() && content.argumentKinds[i]==CArg::Text)
                    outputArguments.push_back(textAttribute(content.arguments[i],content,padding));
                else if (i<content.argumentKinds.size() && content.argumentKinds[i]==CArg::Expression)
                    outputArguments.push_back(textAttribute(content.arguments[i],content,padding,true));
                else if (i<content.argumentKinds.size() && content.argumentKinds[i]==CArg::Motions)
                    outputArguments.push_back(typedMotions(content,content.arguments[i]));
                else if (i<content.argumentKinds.size() && content.argumentKinds[i]==CArg::Mental)
                    outputArguments.push_back(scriptMentalKeyword(content.arguments[i]));
                else if (i<content.argumentKinds.size() && prop_ &&
                         (content.argumentKinds[i]==CArg::Flag || content.argumentKinds[i]==CArg::Step) &&
                         plainQuoted(content.arguments[i]) &&
                         utils::icmp(unquoted(content.arguments[i]), prop_->sys().randomValue()) == 0)
                    // DのtoScriptImplでは、システムのランダム値と等しいフラグ・ステップをrandomとする。
                    outputArguments.push_back("random");
                else if (i<content.argumentKinds.size() && prop_ && content.argumentKinds[i]==CArg::Step &&
                         plainQuoted(content.arguments[i]) &&
                         utils::icmp(unquoted(content.arguments[i]), prop_->sys().selectedPlayerCardNumber()) == 0)
                    // DのtoScriptImpl（Wsn.2）に対応する、選択中プレイヤーのステップ表記。
                    outputArguments.push_back("selected");
                else outputArguments.push_back(i<content.argumentKinds.size()
                    ? typedId(content.argumentKinds[i],content.arguments[i]) : content.arguments[i]);
            }
            if (!outputArguments.empty()) {
                line.push_back(' ');
                for (std::size_t i = 0; i < outputArguments.size(); ++i) {
                    if (i) line += ", ";
                    line += outputArguments[i];
                }
            }
            // 解析済みの分岐は、意味上の分岐ラベルname
            // と元の表記conditionを保持する。分
            // 岐条件は親が出力するため、子を再び包むと
            // if文が二つ連続する。分岐ラベルのない条
            // 件は独立した条件として、ここで出力する。
            if (!content.condition.empty() && content.name.empty()) {
                lines.push_back(padding + "if " + content.condition);
                lines.push_back(std::move(line));
                for (const auto& child : content.children) append(child, level + 1);
                lines.push_back(padding + "fi");
            } else {
                lines.push_back(std::move(line));
                const auto detail = event::contentDetail(content.type);
                const auto isImplicitOK = [&](const Content& child) {
                    return detail.nextType == types::CNextType::Text &&
                           !evtChildOK.empty() && utils::icmp(child.name, evtChildOK) == 0;
                };
                bool branches = content.children.size() > 1;
                if (!branches && content.children.size() == 1)
                    branches = !content.children.front().name.empty() &&
                               !isImplicitOK(content.children.front());
                // Dは名前付きの単一の継続にsifを使う。if/fi
                // ブロックではなく線形の条件であるため、イベント
                // ツリーの出力時もその表記と字下げを維持する。
                const bool useSif = branches && content.children.size() == 1 &&
                                    !content.children.front().name.empty();
                for (std::size_t i = 0; i < content.children.size(); ++i) {
                    const auto& child = content.children[i];
                    if (branches) {
                        String condition;
                        switch (detail.nextType) {
                        case types::CNextType::Text:
                        case types::CNextType::Trio:
                            condition = createString(child.name); break;
                        case types::CNextType::Bool:
                            condition = prop_ && utils::icmp(child.name, prop_->sys().evtChildTrue()) == 0
                                            ? "true" : "false";
                            break;
                        case types::CNextType::Coupon:
                            condition = child.name.empty() ? "none"
                                                          : createString(child.name);
                            break;
                        case types::CNextType::Step:
                        case types::CNextType::IdArea:
                        case types::CNextType::IdBattle:
                            condition = (prop_ && utils::icmp(child.name, prop_->sys().evtChildDefault()) == 0)
                                ? "default" : child.name;
                            break;
                        default: break;
                        }
                        lines.push_back(padding + (useSif ? "sif " :
                                                   i == 0 ? "if " : "elif ") +
                                        condition);
                    }
                    append(child, level + (branches && !useSif ? 1U :
                                           content.type == CType::Start ? 1U : 0U));
                }
                if (branches && !useSif) lines.push_back(padding + "fi");
            }
        };
    for (std::size_t i = 0U; i < contents.size(); ++i) {
        // Dのルート間の空行を維持する。一つのルートが出
        // 力するコメント・分岐・子の行の間には入れない。
        if (i != 0U) lines.emplace_back();
        append(contents[i], 0);
    }
    auto declarations=variables.vars();
    if (!declarations.empty()) {
        declarations.push_back({});
        lines.insert(lines.begin(),declarations.begin(),declarations.end());
    }
    return lines;
}

String CWXScript::toScriptText(const Contents& contents, StringView evtChildOK,
                               bool legacy, StringView indent) const {
    const auto lines = toScript(contents, evtChildOK, legacy, indent);
    String result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) result.push_back('\n');
        result += lines[i];
    }
    return result;
}

Contents compile(const Props* prop, const Summary* summ, StringView source,
                 const CompileOption& opt) {
    CWXScript compiler(prop, summ, String(source));
    const auto tokens = compiler.tokenize(source, opt);
    if (!compiler.errors().empty())
        throw CWXScriptException(__FILE__, __LINE__, String(source),
                                 compiler.errors());
    const auto nodes = compiler.analyzeSyntax(tokens);
    if (!compiler.errors().empty())
        throw CWXScriptException(__FILE__, __LINE__, String(source),
                                 compiler.errors());
    const auto contents = compiler.analyzeSemantics(nodes, opt);
    if (!compiler.errors().empty())
        throw CWXScriptException(__FILE__, __LINE__, String(source),
                                 compiler.errors());
    return contents;
}

CType firstContentType(const Props* prop, const Summary* summ, StringView source) {
    CWXScript compiler(prop, summ, String(source));
    const auto tokens = compiler.tokenize(source);
    if (!compiler.errors().empty()) return static_cast<CType>(-1);
    for (const auto& token : tokens) {
        if (token.kind == CWXScript::Kind::START) return CType::Start;
        if (token.kind == CWXScript::Kind::SYMBOL) {
            if (const auto* key = findKeyword(token.value)) return key->type;
        }
    }
    return static_cast<CType>(-1);
}

bool validVarName(StringView name) {
    if (name.size() < 2 || name.front() != '$') return false;
    if (!isAsciiAlpha(static_cast<unsigned char>(name[1]))) return false;
    for (std::size_t i = 2; i < name.size(); ++i) {
        if (!isAsciiAlphaNumeric(static_cast<unsigned char>(name[i]))) return false;
    }
    return true;
}

std::vector<CWXScriptKeyword> keywordInfos(const Props* prop,
    const std::unordered_map<types::Element, structs::ElementOverride>& overrides) {
    static const msgs::Msgs defaults;
    const auto& messages = prop ? prop->requiredMsgs() : defaults;
    const auto elementName = [&](types::Element element) {
        const auto found = overrides.find(element);
        return found != overrides.end() && !found->second.name.empty()
            ? found->second.name : messages.elementName(element);
    };
    // Dのscript.d4474-4687に合わせ、キーワード、分類、ローカライズ
    // された名前を維持する。異なる型付き引数の分類で同じ語が繰り返される場合も含む。
    std::vector<CWXScriptKeyword> values{
        {"true", CType::Start, messages.flagOn.value, ""},
        {"false", CType::Start, messages.flagOff.value, ""},
        {"default", CType::Start, messages.transitionName(types::Transition::Default), messages.transition.value},
        {"none", CType::Start, messages.transitionName(types::Transition::None), messages.transition.value},
        {"thread", CType::Start, messages.transitionName(types::Transition::Blinds), messages.transition.value},
        {"shave", CType::Start, messages.transitionName(types::Transition::PixelDissolve), messages.transition.value},
        {"fade", CType::Start, messages.transitionName(types::Transition::Fade), messages.transition.value},
        {"bold", CType::Start, messages.bold.value, messages.fontStyle.value},
        {"italic", CType::Start, messages.italic.value, messages.fontStyle.value},
        {"underline", CType::Start, messages.underline.value, messages.fontStyle.value},
        {"strike", CType::Start, messages.strike.value, messages.fontStyle.value},
        {"vertical", CType::Start, messages.vertical.value, messages.fontStyle.value},
        {"antialias", CType::Start, messages.antialias.value, messages.fontStyle.value},
        {"border1", CType::Start, messages.borderingTypeName(types::BorderingType::Outline), messages.bordering.value},
        {"border2", CType::Start, messages.borderingTypeName(types::BorderingType::Inline), messages.bordering.value},
        {"normal", CType::Start, messages.blendModeName(types::BlendMode::Normal), messages.blendMode.value},
        {"add", CType::Start, messages.blendModeName(types::BlendMode::Add), messages.blendMode.value},
        {"sub", CType::Start, messages.blendModeName(types::BlendMode::Subtract), messages.blendMode.value},
        {"mul", CType::Start, messages.blendModeName(types::BlendMode::Multiply), messages.blendMode.value},
        {"none", CType::Start, messages.gradientDirName(types::GradientDir::None), messages.gradient.value},
        {"h", CType::Start, messages.gradientDirName(types::GradientDir::LeftToRight), messages.gradient.value},
        {"v", CType::Start, messages.gradientDirName(types::GradientDir::TopToBottom), messages.gradient.value},
        {"heal", CType::Start, messages.motionName(types::MType::Heal), messages.motion.value},
        {"damage", CType::Start, messages.motionName(types::MType::Damage), messages.motion.value},
        {"absorb", CType::Start, messages.motionName(types::MType::Absorb), messages.motion.value},
        {"paralyze", CType::Start, messages.motionName(types::MType::Paralyze), messages.motion.value},
        {"disparalyze", CType::Start, messages.motionName(types::MType::DisParalyze), messages.motion.value},
        {"poison", CType::Start, messages.motionName(types::MType::Poison), messages.motion.value},
        {"dispoison", CType::Start, messages.motionName(types::MType::DisPoison), messages.motion.value},
        {"getspilit", CType::Start, messages.motionName(types::MType::GetSkillPower), messages.motion.value},
        {"losespilit", CType::Start, messages.motionName(types::MType::LoseSkillPower), messages.motion.value},
        {"sleep", CType::Start, messages.motionName(types::MType::Sleep), messages.motion.value},
        {"confuse", CType::Start, messages.motionName(types::MType::Confuse), messages.motion.value},
        {"overheat", CType::Start, messages.motionName(types::MType::Overheat), messages.motion.value},
        {"brave", CType::Start, messages.motionName(types::MType::Brave), messages.motion.value},
        {"panic", CType::Start, messages.motionName(types::MType::Panic), messages.motion.value},
        {"resetmind", CType::Start, messages.motionName(types::MType::Normal), messages.motion.value},
        {"bind", CType::Start, messages.motionName(types::MType::Bind), messages.motion.value},
        {"disbind", CType::Start, messages.motionName(types::MType::DisBind), messages.motion.value},
        {"silence", CType::Start, messages.motionName(types::MType::Silence), messages.motion.value},
        {"dissilence", CType::Start, messages.motionName(types::MType::DisSilence), messages.motion.value},
        {"faceup", CType::Start, messages.motionName(types::MType::FaceUp), messages.motion.value},
        {"facedown", CType::Start, messages.motionName(types::MType::FaceDown), messages.motion.value},
        {"antimagic", CType::Start, messages.motionName(types::MType::AntiMagic), messages.motion.value},
        {"disantimagic", CType::Start, messages.motionName(types::MType::DisAntiMagic), messages.motion.value},
        {"enhaction", CType::Start, messages.motionName(types::MType::EnhanceAction), messages.motion.value},
        {"enhavoid", CType::Start, messages.motionName(types::MType::EnhanceAvoid), messages.motion.value},
        {"enhresist", CType::Start, messages.motionName(types::MType::EnhanceDefense), messages.motion.value},
        {"enhdefense", CType::Start, messages.motionName(types::MType::EnhanceResist), messages.motion.value},
        {"vantarget", CType::Start, messages.motionName(types::MType::VanishTarget), messages.motion.value},
        {"vancard", CType::Start, messages.motionName(types::MType::VanishCard), messages.motion.value},
        {"vanbeast", CType::Start, messages.motionName(types::MType::VanishBeast), messages.motion.value},
        {"dealattack", CType::Start, messages.motionName(types::MType::DealAttackCard), messages.motion.value},
        {"dealpowerful", CType::Start, messages.motionName(types::MType::DealPowerfulAttackCard), messages.motion.value},
        {"dealcritical", CType::Start, messages.motionName(types::MType::DealCriticalAttackCard), messages.motion.value},
        {"dealfeint", CType::Start, messages.motionName(types::MType::DealFeintCard), messages.motion.value},
        {"dealdefense", CType::Start, messages.motionName(types::MType::DealDefenseCard), messages.motion.value},
        {"dealdistance", CType::Start, messages.motionName(types::MType::DealDistanceCard), messages.motion.value},
        {"dealconfuse", CType::Start, messages.motionName(types::MType::DealConfuseCard), messages.motion.value},
        {"dealskill", CType::Start, messages.motionName(types::MType::DealSkillCard), messages.motion.value},
        {"cancelaction", CType::Start, messages.motionName(types::MType::CancelAction), messages.motion.value},
        {"summon", CType::Start, messages.motionName(types::MType::SummonBeast), messages.motion.value},
        {"noeffect", CType::Start, messages.motionName(types::MType::NoEffect), messages.motion.value},
        {"level", CType::Start, messages.damageTypeName(types::DamageType::LevelRatio), messages.calcType.value},
        {"value", CType::Start, messages.damageTypeName(types::DamageType::Normal), messages.calcType.value},
        {"max", CType::Start, messages.damageTypeName(types::DamageType::Max), messages.calcType.value},
        {"fixed", CType::Start, messages.damageTypeName(types::DamageType::Fixed), messages.calcType.value},
        {"all", CType::Start, elementName(types::Element::All), messages.motionElement.value},
        {"phy", CType::Start, elementName(types::Element::Health), messages.motionElement.value},
        {"mind", CType::Start, elementName(types::Element::Mind), messages.motionElement.value},
        {"holy", CType::Start, elementName(types::Element::Miracle), messages.motionElement.value},
        {"magic", CType::Start, elementName(types::Element::Magic), messages.motionElement.value},
        {"fire", CType::Start, elementName(types::Element::Fire), messages.motionElement.value},
        {"ice", CType::Start, elementName(types::Element::Ice), messages.motionElement.value},
        {"none", CType::Start, messages.cardVisualName(types::CardVisual::None), messages.effectVisual.value},
        {"reverse", CType::Start, messages.cardVisualName(types::CardVisual::Reverse), messages.effectVisual.value},
        {"hswing", CType::Start, messages.cardVisualName(types::CardVisual::Horizontal), messages.effectVisual.value},
        {"vswing", CType::Start, messages.cardVisualName(types::CardVisual::Vertical), messages.effectVisual.value},
        {"physic", CType::Start, messages.effectTypeName(types::EffectType::Physic), messages.elementProps.value},
        {"magic", CType::Start, messages.effectTypeName(types::EffectType::Magic), messages.elementProps.value},
        {"mphysic", CType::Start, messages.effectTypeName(types::EffectType::MagicalPhysic), messages.elementProps.value},
        {"pmagic", CType::Start, messages.effectTypeName(types::EffectType::PhysicalMagic), messages.elementProps.value},
        {"none", CType::Start, messages.effectTypeName(types::EffectType::None), messages.elementProps.value},
        {"avoid", CType::Start, messages.resistName(types::Resist::Avoid), messages.resistProps.value},
        {"resist", CType::Start, messages.resistName(types::Resist::Resist), messages.resistProps.value},
        {"unfail", CType::Start, messages.resistName(types::Resist::Unfail), messages.resistProps.value},
        {"none", CType::Start, messages.scTalkerNameNarration.value, messages.talker.value},
        {"M", CType::Start, messages.talkerName(types::Talker::Selected), messages.talker.value},
        {"U", CType::Start, messages.talkerName(types::Talker::Unselected), messages.talker.value},
        {"R", CType::Start, messages.talkerName(types::Talker::Random), messages.talker.value},
        {"C", CType::Start, messages.talkerName(types::Talker::Card), messages.talker.value},
        {"V", CType::Start, messages.scTalkerNameValued.value, messages.talker.value},
        {"M", CType::Start, messages.rangeName(types::Range::Selected), messages.range.value},
        {"R", CType::Start, messages.rangeName(types::Range::Random), messages.range.value},
        {"T", CType::Start, messages.rangeName(types::Range::Party), messages.range.value},
        {"backpack", CType::Start, messages.rangeName(types::Range::Backpack), messages.range.value},
        {"party", CType::Start, messages.rangeName(types::Range::PartyAndBackpack), messages.range.value},
        {"field", CType::Start, messages.rangeName(types::Range::Field), messages.range.value},
        {"selcard", CType::Start, messages.rangeName(types::Range::SelectedCard), messages.range.value},
        {"npc", CType::Start, messages.rangeName(types::Range::Npc), messages.range.value},
        {"dex", CType::Start, messages.physicalName(types::Physical::Dex), messages.aptPhysical.value},
        {"agl", CType::Start, messages.physicalName(types::Physical::Agl), messages.aptPhysical.value},
        {"int", CType::Start, messages.physicalName(types::Physical::Int), messages.aptPhysical.value},
        {"str", CType::Start, messages.physicalName(types::Physical::Str), messages.aptPhysical.value},
        {"vit", CType::Start, messages.physicalName(types::Physical::Vit), messages.aptPhysical.value},
        {"min", CType::Start, messages.physicalName(types::Physical::Min), messages.aptPhysical.value},
        {"agg", CType::Start, messages.mentalName(types::Mental::Aggressive), messages.aptMental.value},
        {"unagg", CType::Start, messages.mentalName(types::Mental::Unaggressive), messages.aptMental.value},
        {"cheerf", CType::Start, messages.mentalName(types::Mental::Cheerful), messages.aptMental.value},
        {"uncheerf", CType::Start, messages.mentalName(types::Mental::Uncheerful), messages.aptMental.value},
        {"brave", CType::Start, messages.mentalName(types::Mental::Brave), messages.aptMental.value},
        {"unbrave", CType::Start, messages.mentalName(types::Mental::Unbrave), messages.aptMental.value},
        {"caut", CType::Start, messages.mentalName(types::Mental::Cautious), messages.aptMental.value},
        {"uncaut", CType::Start, messages.mentalName(types::Mental::Uncautious), messages.aptMental.value},
        {"trick", CType::Start, messages.mentalName(types::Mental::Trickish), messages.aptMental.value},
        {"untrick", CType::Start, messages.mentalName(types::Mental::Untrickish), messages.aptMental.value},
        {"none", CType::Start, messages.statusName(types::Status::None), messages.status.value},
        {"active", CType::Start, messages.statusName(types::Status::Active), messages.status.value},
        {"inactive", CType::Start, messages.statusName(types::Status::Inactive), messages.status.value},
        {"alive", CType::Start, messages.statusName(types::Status::Alive), messages.status.value},
        {"dead", CType::Start, messages.statusName(types::Status::Dead), messages.status.value},
        {"fine", CType::Start, messages.statusName(types::Status::Fine), messages.status.value},
        {"injured", CType::Start, messages.statusName(types::Status::Injured), messages.status.value},
        {"heavyinjured", CType::Start, messages.statusName(types::Status::HeavyInjured), messages.status.value},
        {"unconscious", CType::Start, messages.statusName(types::Status::Unconscious), messages.status.value},
        {"poison", CType::Start, messages.statusName(types::Status::Poison), messages.status.value},
        {"sleep", CType::Start, messages.statusName(types::Status::Sleep), messages.status.value},
        {"bind", CType::Start, messages.statusName(types::Status::Bind), messages.status.value},
        {"paralyze", CType::Start, messages.statusName(types::Status::Paralyze), messages.status.value},
        {"confuse", CType::Start, messages.statusName(types::Status::Confuse), messages.status.value},
        {"overheat", CType::Start, messages.statusName(types::Status::Overheat), messages.status.value},
        {"brave", CType::Start, messages.statusName(types::Status::Brave), messages.status.value},
        {"panic", CType::Start, messages.statusName(types::Status::Panic), messages.status.value},
        {"silence", CType::Start, messages.statusName(types::Status::Silence), messages.status.value},
        {"faceup", CType::Start, messages.statusName(types::Status::FaceUp), messages.status.value},
        {"antimagic", CType::Start, messages.statusName(types::Status::AntiMagic), messages.status.value},
        {"upaction", CType::Start, messages.statusName(types::Status::UpAction), messages.status.value},
        {"upavoid", CType::Start, messages.statusName(types::Status::UpAvoid), messages.status.value},
        {"upresit", CType::Start, messages.statusName(types::Status::UpResist), messages.status.value},
        {"updefense", CType::Start, messages.statusName(types::Status::UpDefense), messages.status.value},
        {"downaction", CType::Start, messages.statusName(types::Status::DownAction), messages.status.value},
        {"downavoid", CType::Start, messages.statusName(types::Status::DownAvoid), messages.status.value},
        {"downresit", CType::Start, messages.statusName(types::Status::DownResist), messages.status.value},
        {"downdefense", CType::Start, messages.statusName(types::Status::DownDefense), messages.status.value},
        {"party", CType::Start, utils::tryFormat(messages.castRange1.value, messages.castRangeName(types::CastRange::Party)), messages.selectMember.value},
        {"enemy", CType::Start, utils::tryFormat(messages.castRange1.value, messages.castRangeName(types::CastRange::Enemy)), messages.selectMember.value},
        {"npc", CType::Start, utils::tryFormat(messages.castRange1.value, messages.castRangeName(types::CastRange::Npc)), messages.selectMember.value},
        {"all", CType::Start, messages.effectCardTypeName(types::EffectCardType::All), messages.cardType.value},
        {"skill", CType::Start, messages.effectCardTypeName(types::EffectCardType::Skill), messages.cardType.value},
        {"item", CType::Start, messages.effectCardTypeName(types::EffectCardType::Item), messages.cardType.value},
        {"beast", CType::Start, messages.effectCardTypeName(types::EffectCardType::Beast), messages.cardType.value},
        {"hand", CType::Start, messages.effectCardTypeName(types::EffectCardType::Hand), messages.cardType.value},
        {"and", CType::Start, messages.matchingTypeName(types::MatchingType::And), messages.matchingType.value},
        {"or", CType::Start, messages.matchingTypeName(types::MatchingType::Or), messages.matchingType.value},
        {"has", CType::Start, messages.matchingConditionName(types::MatchingCondition::Has), messages.matchingCondition.value},
        {"hasnot", CType::Start, messages.matchingConditionName(types::MatchingCondition::HasNot), messages.matchingCondition.value},
        {"fixed", CType::Start, messages.updateTypeName(types::UpdateType::Fixed), messages.updateType.value},
        {"variables", CType::Start, messages.updateTypeName(types::UpdateType::Variables), messages.updateType.value},
        {"all", CType::Start, messages.updateTypeName(types::UpdateType::All), messages.updateType.value},
        {"none", CType::Start, messages.environmentStatusName(types::EnvironmentStatus::NotSet), messages.environmentStatus.value},
        {"on", CType::Start, messages.environmentStatusName(types::EnvironmentStatus::Enable), messages.environmentStatus.value},
        {"off", CType::Start, messages.environmentStatusName(types::EnvironmentStatus::Disable), messages.environmentStatus.value},
        {"flag", CType::Start, messages.variableTypeName(types::VariableType::Flag), messages.variableType.value},
        {"step", CType::Start, messages.variableTypeName(types::VariableType::Step), messages.variableType.value},
        {"variant", CType::Start, messages.variableTypeName(types::VariableType::Variant), messages.variableType.value},
    };
    // コマンドレコードは、移植可能なAPI向けに追加した補助情報。同
    // じメタデータ実装へ追加し、Dの引数キーワードを置き換えない。
    for (const auto& item : KEYWORD_DEFS)
        values.push_back({item.keyword,item.type,item.keyword,{}});
    return values;
}

const std::vector<CWXScriptKeyword>& keywordInfos() {
    static const auto values = keywordInfos(nullptr,{});
    return values;
}

}  // namespace cwxeditor::cwx::script
