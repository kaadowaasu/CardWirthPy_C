#include "cwxeditor_src/core/cwx/system.hpp"

#include "cwxeditor_src/core/cwx/coupon.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cwxeditor::cwx::system {
namespace {

using lib::String;
using lib::StringView;

constexpr StringView kFkcSuccess = "○";
constexpr StringView kFkcFailure = "×";
constexpr StringView kFkcHasNot = "！";

[[nodiscard]] bool startsWith(StringView value, StringView prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] bool endsWith(StringView value, StringView suffix) noexcept {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
}

[[noreturn]] void throwInvalidFeature(StringView /*feature*/, std::size_t value) {
    // DのTypedef2.text()は、その値自身の十進表記を返す。
    throw std::runtime_error(std::to_string(value));
}

[[noreturn]] void throwInvalidAction(types::ActionCardType type) {
    throw std::runtime_error("Invalid action type: " +
                             std::to_string(static_cast<std::int32_t>(type)));
}

template <typename E, std::size_t N>
[[nodiscard]] PhysicalModifierTable<E> makePhysical(
    const std::array<std::array<std::int32_t, 6>, N>& values) {
    constexpr std::array<Physical, 6> keys{
        Physical::Dex, Physical::Agl, Physical::Int,
        Physical::Str, Physical::Vit, Physical::Min};
    PhysicalModifierTable<E> result;
    result.reserve(N);
    for (std::size_t index = 0; index < N; ++index) {
        lib::Dictionary<Physical, std::int32_t> row;
        row.reserve(keys.size());
        for (std::size_t keyIndex = 0; keyIndex < keys.size(); ++keyIndex) {
            row.emplace(keys[keyIndex], values[index][keyIndex]);
        }
        result.emplace(E{index}, std::move(row));
    }
    return result;
}

template <typename E, std::size_t N>
[[nodiscard]] MentalModifierTable<E> makeMental(
    const std::array<std::array<double, 5>, N>& values) {
    constexpr std::array<std::pair<Mental, Mental>, 5> keys{
        std::pair{Mental::Aggressive, Mental::Unaggressive},
        std::pair{Mental::Cautious, Mental::Uncautious},
        std::pair{Mental::Brave, Mental::Unbrave},
        std::pair{Mental::Cheerful, Mental::Uncheerful},
        std::pair{Mental::Trickish, Mental::Untrickish}};
    MentalModifierTable<E> result;
    result.reserve(N);
    for (std::size_t index = 0; index < N; ++index) {
        lib::Dictionary<Mental, double> row;
        row.reserve(keys.size() * 2);
        for (std::size_t keyIndex = 0; keyIndex < keys.size(); ++keyIndex) {
            const double value = values[index][keyIndex];
            row.emplace(keys[keyIndex].first, value);
            row.emplace(keys[keyIndex].second, -value);
        }
        result.emplace(E{index}, std::move(row));
    }
    return result;
}

}  // namespace

XMLInfo::XMLInfo(const System& system, String version)
    : system_owner_(std::make_shared<const System>(system)),
      sys(*system_owner_), ver(std::move(version)) {}

XMLInfo::XMLInfo(const XMLInfo& other)
    : system_owner_(other.system_owner_), sys(*system_owner_), ver(other.ver) {}

String System::sexName(Sex value, StringView /*legacyName*/) const {
    if (value.value == 0) return "♂";
    if (value.value == 1) return "♀";
    throwInvalidFeature("Invalid sex: ", value.value);
}

String System::periodName(Period value, StringView /*legacyName*/) const {
    switch (value.value) {
    case 0: return "子供";
    case 1: return "若者";
    case 2: return "大人";
    case 3: return "老人";
    default: throwInvalidFeature("Invalid period: ", value.value);
    }
}

String System::natureName(Nature value, StringView legacyName) const {
    // Dは認識済みのNature分岐の中だけでtoLowerへ進む。
    if (value.value >= 12) throwInvalidFeature("Invalid nature: ", value.value);
    const String legacy = enumutils::lowerCase(legacyName);
    switch (value.value) {
    case 0:
        return legacy == "darkwirth" ? String("他種族") : String("標準型");
    case 1:
        if (legacy == "oedowirth") return "隠密型";
        if (legacy == "darkwirth") return "人獣族";
        return "万能型";
    case 2:
        if (legacy == "s_c_wirth") return "根性型";
        if (legacy == "oedowirth") return "剣客型";
        if (legacy == "darkwirth") return "悪鬼族";
        return "勇将型";
    case 3:
        if (legacy == "s_c_wirth") return "熱血型";
        if (legacy == "darkwirth") return "蜥蜴族";
        return "豪傑型";
    case 4:
        if (legacy == "s_c_wirth") return "理性型";
        if (legacy == "oedowirth") return "参謀型";
        if (legacy == "darkwirth") return "妖木族";
        return "知将型";
    case 5:
        if (legacy == "s_c_wirth") return "秀才型";
        if (legacy == "darkwirth") return "小悪魔";
        return "策士型";
    case 6:
        if (legacy == "s_c_wirth") return "努力型";
        if (legacy == "oedowirth") return "晩成型";
        if (legacy == "darkwirth") return "妖虫族";
        return "凡庸型";
    case 7:
        if (legacy == "oedowirth") return "秀英型";
        if (legacy == "darkwirth") return "人狼族";
        return "英明型";
    case 8:
        if (legacy == "oedowirth") return "剣豪型";
        if (legacy == "darkwirth") return "鬼人族";
        return "無双型";
    case 9:
        if (legacy == "oedowirth") return "賢才型";
        if (legacy == "darkwirth") return "大悪魔";
        return "天才型";
    case 10:
        if (legacy == "s_c_wirth") return "超人型";
        if (legacy == "oedowirth") return "覇道型";
        if (legacy == "darkwirth") return "闇の者";
        return "英雄型";
    case 11:
        return legacy == "darkwirth" ? String("神竜族") : String("神仙型");
    default:
        throwInvalidFeature("Invalid nature: ", value.value);
    }
}

String System::makingsName(Makings value, StringView /*legacyName*/) const {
    static constexpr std::array<StringView, 48> names{
        "秀麗", "醜悪", "高貴の出", "下賎の出", "都会育ち", "田舎育ち",
        "裕福", "貧乏", "厚き信仰", "不心得者", "誠実", "不実",
        "冷静沈着", "猪突猛進", "貪欲", "無欲", "献身的", "利己的",
        "秩序派", "混沌派", "進取派", "保守派", "神経質", "鈍感",
        "好奇心旺盛", "無頓着", "過激", "穏健", "楽観的", "悲観的",
        "勤勉", "遊び人", "陽気", "内気", "派手", "地味", "高慢",
        "謙虚", "上品", "粗野", "武骨", "繊細", "硬派", "軟派",
        "お人好し", "ひねくれ者", "名誉こそ命", "愛に生きる"};
    if (value.value < names.size()) return String(names[value.value]);
    throwInvalidFeature("Invalid makings: ", value.value);
}

String System::sexCoupon(Sex value, StringView legacyName) const {
    return "＿" + sexName(value, legacyName);
}

String System::periodCoupon(Period value, StringView legacyName) const {
    return "＿" + periodName(value, legacyName);
}

String System::natureCoupon(Nature value, StringView legacyName) const {
    return "＿" + natureName(value, legacyName);
}

String System::makingsCoupon(Makings value, StringView legacyName) const {
    return "＿" + makingsName(value, legacyName);
}

lib::Array<lib::Owner<coupon::Coupon>> System::periodInitialCoupons(
    Period value, StringView /*legacyName*/) const {
    lib::Array<lib::Owner<coupon::Coupon>> result;
    if (value.value == 2) {
        result.emplace_back(std::make_shared<coupon::Coupon>("熟練", 2));
    } else if (value.value == 3) {
        result.emplace_back(std::make_shared<coupon::Coupon>("老獪", 4));
    }
    return result;
}

String System::number1Coupon(StringView legacyName) const {
    return enumutils::lowerCase(legacyName) == "darkwirth" ? String("＿妖") : String("＿１");
}

String System::actionCardName(ActionCardType type, StringView legacyName) const {
    switch (type) {
    case ActionCardType::Exchange: return "カード交換";
    case ActionCardType::Attack: return "攻撃";
    case ActionCardType::PowerfulAttack: return "渾身の一撃";
    case ActionCardType::CriticalAttack: return "会心の一撃";
    case ActionCardType::Feint:
        return enumutils::lowerCase(legacyName) == "oedowirth" ? String("牽制") : String("フェイント");
    case ActionCardType::Defense: return "防御";
    case ActionCardType::Distance: return "見切り";
    case ActionCardType::RunAway: return "逃走";
    case ActionCardType::Confuse: return "混乱";
    default: throwInvalidAction(type);
    }
}

Aptitude System::actionCardAptitude(ActionCardType type) const {
    switch (type) {
    case ActionCardType::Exchange: return {Physical::Int, Mental::Cautious};
    case ActionCardType::Attack: return {Physical::Str, Mental::Brave};
    case ActionCardType::PowerfulAttack: return {Physical::Str, Mental::Aggressive};
    case ActionCardType::CriticalAttack: return {Physical::Dex, Mental::Brave};
    case ActionCardType::Feint: return {Physical::Dex, Mental::Trickish};
    case ActionCardType::Defense: return {Physical::Min, Mental::Cautious};
    case ActionCardType::Distance: return {Physical::Agl, Mental::Cautious};
    case ActionCardType::RunAway: return {Physical::Agl, Mental::Unbrave};
    case ActionCardType::Confuse: return {Physical::Int, Mental::Untrickish};
    default: throwInvalidAction(type);
    }
}

Aptitude System::actionOrderAptitude() const {
    return {Physical::Agl, Mental::Uncautious};
}

Aptitude System::runAwaySpeedAptitude() const {
    return {Physical::Agl, Mental::Trickish};
}

Aptitude System::resilienceAptitude() const {
    return {Physical::Vit, Mental::Aggressive};
}

Aptitude System::resistanceAptitude() const {
    return {Physical::Min, Mental::Brave};
}

Aptitude System::avoidanceAptitude() const {
    return {Physical::Agl, Mental::Cautious};
}

bool System::isPenalty(std::span<const String> keyCodes) const {
    const String value = penalty();
    for (const String& keyCode : keyCodes) {
        if (keyCode == value) return true;
    }
    return false;
}

String System::penalty() const { return "ペナルティ"; }

bool System::isRecycle(std::span<const String> keyCodes) const {
    const String value = recycle();
    for (const String& keyCode : keyCodes) {
        if (keyCode == value) return true;
    }
    return false;
}

String System::recycle() const { return "リサイクル"; }

bool System::isRunAway(std::span<const String> keyCodes) const {
    const String value = runAway();
    for (const String& keyCode : keyCodes) {
        if (keyCode == value) return true;
    }
    return false;
}

String System::runAway() const { return "逃走"; }

FKCKind System::fireKeyCodeKind(StringView keyCode) const {
    if (endsWith(keyCode, kFkcSuccess)) return FKCKind::Success;
    if (endsWith(keyCode, kFkcFailure)) return FKCKind::Failure;
    if (startsWith(keyCode, kFkcHasNot)) return FKCKind::HasNot;
    return FKCKind::Use;
}

FKCKind System::fireKeyCodeKindRef(String& keyCode) const {
    if (endsWith(keyCode, kFkcSuccess)) {
        keyCode.erase(keyCode.size() - kFkcSuccess.size());
        return FKCKind::Success;
    }
    if (endsWith(keyCode, kFkcFailure)) {
        keyCode.erase(keyCode.size() - kFkcFailure.size());
        return FKCKind::Failure;
    }
    if (startsWith(keyCode, kFkcHasNot)) {
        keyCode.erase(0, kFkcHasNot.size());
        return FKCKind::HasNot;
    }
    return FKCKind::Use;
}

String System::convFireKeyCode(const FKeyCode& keyCode) const {
    switch (keyCode.kind) {
    case FKCKind::Use: return keyCode.keyCode;
    case FKCKind::Success: return keyCode.keyCode + String(kFkcSuccess);
    case FKCKind::Failure: return keyCode.keyCode + String(kFkcFailure);
    case FKCKind::HasNot: return String(kFkcHasNot) + keyCode.keyCode;
    default: throw std::runtime_error("Invalid fire key-code kind");
    }
}

String System::convFireKeyCode(StringView keyCode, FKCKind kind) const {
    String base;
    if (endsWith(keyCode, kFkcSuccess)) {
        base = String(keyCode.substr(0, keyCode.size() - kFkcSuccess.size()));
        switch (kind) {
        case FKCKind::Use: return base;
        case FKCKind::Success: return String(keyCode);
        case FKCKind::Failure: return base + String(kFkcFailure);
        case FKCKind::HasNot: return String(kFkcHasNot) + base;
        default: throw std::runtime_error("Invalid fire key-code kind");
        }
    }
    if (endsWith(keyCode, kFkcFailure)) {
        base = String(keyCode.substr(0, keyCode.size() - kFkcFailure.size()));
        switch (kind) {
        case FKCKind::Use: return base;
        case FKCKind::Success: return base + String(kFkcSuccess);
        case FKCKind::Failure: return String(keyCode);
        case FKCKind::HasNot: return String(kFkcHasNot) + base;
        default: throw std::runtime_error("Invalid fire key-code kind");
        }
    }
    if (startsWith(keyCode, kFkcHasNot)) {
        base = String(keyCode.substr(kFkcHasNot.size()));
        switch (kind) {
        case FKCKind::Use: return base;
        case FKCKind::Success: return base + String(kFkcSuccess);
        case FKCKind::Failure: return base + String(kFkcFailure);
        case FKCKind::HasNot: return String(keyCode);
        default: throw std::runtime_error("Invalid fire key-code kind");
        }
    }
    switch (kind) {
    case FKCKind::Use: return String(keyCode);
    case FKCKind::Success: return String(keyCode) + String(kFkcSuccess);
    case FKCKind::Failure: return String(keyCode) + String(kFkcFailure);
    case FKCKind::HasNot: return String(kFkcHasNot) + String(keyCode);
    default: throw std::runtime_error("Invalid fire key-code kind");
    }
}

FKeyCode System::toFKeyCode(String keyCode) const {
    const FKCKind kind = fireKeyCodeKindRef(keyCode);
    return {std::move(keyCode), kind};
}

String System::raceCoupon(StringView raceName) const {
    return String("＠Ｒ") + String(raceName);
}

bool System::isRaceCoupon(StringView coupon) const {
    return startsWith(coupon, "＠Ｒ");
}

String System::levelLimit() const { return "＠レベル上限"; }

String System::ep() const { return "＠ＥＰ"; }

bool System::isGene(StringView coupon) const {
    constexpr StringView prefix = "＠Ｇ";
    if (coupon.size() != prefix.size() + 10 || !startsWith(coupon, prefix)) {
        return false;
    }
    for (const char value : coupon.substr(prefix.size())) {
        if (value != '0' && value != '1') return false;
    }
    return true;
}

String System::userCoupon() const { return "＠使用者"; }

String System::effectTargetCoupon() const { return "＠効果対象"; }

String System::eventTargetCoupon() const { return "＠イベント対象"; }

String System::effectOutOfTargetCoupon() const { return "＠効果対象外"; }

String System::evtChildTrue() const { return "○"; }

String System::evtChildFalse() const { return "×"; }

String System::evtChildDefault() const { return "Default"; }

String System::evtChildOK(StringView legacyName) const {
    return enumutils::lowerCase(legacyName) == "oedowirth" ? String(" 是 ") : String("ＯＫ");
}

String System::evtChildGreater() const { return ">"; }

String System::evtChildLesser() const { return "<"; }

String System::evtChildEq() const { return "="; }

CouponType System::couponType(StringView coupon) const {
    constexpr std::array<CouponType, 4> types{
        CouponType::Hide, CouponType::System,
        CouponType::Dur, CouponType::DurBattle};
    for (const CouponType type : types) {
        if (isCouponType(coupon, type)) return type;
    }
    return CouponType::Normal;
}

bool System::isCouponType(StringView coupon, CouponType type) const {
    switch (type) {
    case CouponType::Normal:
        return !isCouponType(coupon, CouponType::Hide) &&
               !isCouponType(coupon, CouponType::System) &&
               !isCouponType(coupon, CouponType::Dur) &&
               !isCouponType(coupon, CouponType::DurBattle);
    case CouponType::Hide: return startsWith(coupon, couponHide());
    case CouponType::System: return startsWith(coupon, couponSystem());
    case CouponType::Dur: return startsWith(coupon, couponDur());
    case CouponType::DurBattle: return startsWith(coupon, couponDurBattle());
    default: throw std::runtime_error("Invalid coupon type");
    }
}

String System::convCoupon(StringView coupon, CouponType type,
                          bool ignoreSystemCoupon) const {
    const String hide = couponHide();
    const String system = couponSystem();
    const String dur = couponDur();
    const String durBattle = couponDurBattle();
    switch (type) {
    case CouponType::Normal:
        if (isCouponType(coupon, CouponType::Hide)) {
            return String(coupon.substr(hide.size()));
        }
        if (!ignoreSystemCoupon && isCouponType(coupon, CouponType::System)) {
            return String(coupon.substr(system.size()));
        }
        if (isCouponType(coupon, CouponType::Dur)) {
            return String(coupon.substr(dur.size()));
        }
        if (isCouponType(coupon, CouponType::DurBattle)) {
            return String(coupon.substr(durBattle.size()));
        }
        return String(coupon);
    case CouponType::Hide:
        if (isCouponType(coupon, CouponType::Hide)) return String(coupon);
        return hide + convCoupon(coupon, CouponType::Normal, ignoreSystemCoupon);
    case CouponType::System:
        if (ignoreSystemCoupon) {
            return convCoupon(coupon, CouponType::Normal, true);
        }
        if (isCouponType(coupon, CouponType::System)) return String(coupon);
        return system + convCoupon(coupon, CouponType::Normal, false);
    case CouponType::Dur:
        if (isCouponType(coupon, CouponType::Dur)) return String(coupon);
        return dur + convCoupon(coupon, CouponType::Normal, ignoreSystemCoupon);
    case CouponType::DurBattle:
        if (isCouponType(coupon, CouponType::DurBattle)) return String(coupon);
        return durBattle + convCoupon(coupon, CouponType::Normal, ignoreSystemCoupon);
    default: throw std::runtime_error("Invalid coupon type");
    }
}

String System::couponHide() const { return "＿"; }

String System::couponSystem() const { return "＠"; }

String System::couponDur() const { return "："; }

String System::couponDurBattle() const { return "；"; }

String System::prefixSystemVarName() const { return "??"; }

bool System::isSystemVar(StringView varName) const {
    return startsWith(varName, prefixSystemVarName());
}

String System::localVariablePrefix() const { return "Local"; }

bool System::isLocalVariable(StringView path) const {
    return startsWith(path, "\\Local\\");
}

String System::randomValue() const { return "??Random"; }

String System::selectedPlayerCardNumber() const { return "??SelectedPlayer"; }

String System::playerCardName(std::uint32_t number) const {
    return "??Player" + std::to_string(number);
}

PhysicalModifierTable<Sex> System::physicalModSex(StringView /*legacyName*/) {
    return makePhysical<Sex>(std::array<std::array<std::int32_t, 6>, 2>{
        std::array<std::int32_t, 6>{0, 0, 0, 1, 0, 0},
        std::array<std::int32_t, 6>{1, 0, 0, 0, 0, 0}});
}

PhysicalModifierTable<Period> System::physicalModPeriod(StringView legacyName) {
    if (startsWith(enumutils::lowerCase(legacyName), "cw´")) {
        return makePhysical<Period>(std::array<std::array<std::int32_t, 6>, 4>{
            std::array<std::int32_t, 6>{1, 1, 1, 0, 0, 1},
            std::array<std::int32_t, 6>{1, 1, 1, 1, 1, 1},
            std::array<std::int32_t, 6>{1, 1, 1, 1, 0, 2},
            std::array<std::int32_t, 6>{1, 0, 2, 0, 0, 2}});
    }
    return makePhysical<Period>(std::array<std::array<std::int32_t, 6>, 4>{
        std::array<std::int32_t, 6>{1, 1, 0, -1, -1, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, -1, 0},
        std::array<std::int32_t, 6>{-1, -1, 1, -1, -1, 1}});
}

PhysicalModifierTable<Nature> System::physicalModNature(StringView legacyName) {
    if (enumutils::lowerCase(legacyName) == "cw´standard") {
        return makePhysical<Nature>(std::array<std::array<std::int32_t, 6>, 12>{
            std::array<std::int32_t, 6>{-1, -1, -1, -1, -1, 1},
            std::array<std::int32_t, 6>{0, 1, -1, -1, -1, -2},
            std::array<std::int32_t, 6>{-1, -1, -2, 0, 0, 0},
            std::array<std::int32_t, 6>{-2, -1, -2, 1, 1, -1},
            std::array<std::int32_t, 6>{-1, -1, 0, -1, -1, 0},
            std::array<std::int32_t, 6>{0, 0, 1, -2, -2, -1},
            std::array<std::int32_t, 6>{-3, -3, -3, -3, -3, -3},
            std::array<std::int32_t, 6>{0, 1, 0, 0, 0, 1},
            std::array<std::int32_t, 6>{-1, 0, -1, 2, 2, 0},
            std::array<std::int32_t, 6>{0, 0, 2, -1, -1, 2},
            std::array<std::int32_t, 6>{0, 0, 1, 1, 1, 2},
            std::array<std::int32_t, 6>{1, 1, 1, 1, 1, 2}});
    }
    if (enumutils::lowerCase(legacyName) == "cw´heroic") {
        return makePhysical<Nature>(std::array<std::array<std::int32_t, 6>, 12>{
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 2},
            std::array<std::int32_t, 6>{1, 1, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 0, -1, 1, 1, 1},
            std::array<std::int32_t, 6>{-1, 0, -1, 2, 2, 0},
            std::array<std::int32_t, 6>{0, 0, 2, -1, 0, 1},
            std::array<std::int32_t, 6>{1, 0, 3, -1, -1, 0},
            std::array<std::int32_t, 6>{-2, -2, -2, -2, -2, -2},
            std::array<std::int32_t, 6>{1, 1, 1, 1, 1, 2},
            std::array<std::int32_t, 6>{0, 1, 0, 3, 2, 1},
            std::array<std::int32_t, 6>{1, 1, 3, 0, 0, 2},
            std::array<std::int32_t, 6>{2, 2, 2, 2, 2, 2},
            std::array<std::int32_t, 6>{2, 2, 3, 2, 2, 3}});
    }
    if (enumutils::lowerCase(legacyName) == "cw´commoner") {
        return makePhysical<Nature>(std::array<std::array<std::int32_t, 6>, 12>{
            std::array<std::int32_t, 6>{-2, -2, -2, -2, -2, -1},
            std::array<std::int32_t, 6>{-1, -1, -2, -2, -2, -3},
            std::array<std::int32_t, 6>{-2, -2, -2, -1, -2, -2},
            std::array<std::int32_t, 6>{-3, -2, -3, 0, -1, -2},
            std::array<std::int32_t, 6>{-2, -2, -1, -2, -2, -2},
            std::array<std::int32_t, 6>{-1, -2, 0, -3, -3, -2},
            std::array<std::int32_t, 6>{-3, -3, -3, -3, -3, -3},
            std::array<std::int32_t, 6>{0, 1, 0, 0, 0, 1},
            std::array<std::int32_t, 6>{-1, 0, -1, 2, 2, 0},
            std::array<std::int32_t, 6>{0, 0, 2, -1, -1, 2},
            std::array<std::int32_t, 6>{0, 0, 1, 1, 1, 2},
            std::array<std::int32_t, 6>{1, 1, 1, 1, 1, 2}});
    }
    if (enumutils::lowerCase(legacyName) == "darkwirth") {
        return makePhysical<Nature>(std::array<std::array<std::int32_t, 6>, 12>{
            std::array<std::int32_t, 6>{1, 0, 0, 0, 1, 1},
            std::array<std::int32_t, 6>{1, 1, 0, 0, 0, -1},
            std::array<std::int32_t, 6>{-1, 0, -2, 2, 2, -1},
            std::array<std::int32_t, 6>{-2, -1, -2, 3, 2, -2},
            std::array<std::int32_t, 6>{-2, -2, 2, -1, 2, 1},
            std::array<std::int32_t, 6>{-2, 2, 3, -2, -2, -1},
            std::array<std::int32_t, 6>{-1, 0, -3, -3, -3, -2},
            std::array<std::int32_t, 6>{1, 1, 1, 1, 1, 1},
            std::array<std::int32_t, 6>{0, 1, 0, 3, 2, 0},
            std::array<std::int32_t, 6>{0, 2, 3, 0, 0, 1},
            std::array<std::int32_t, 6>{1, 2, 1, 2, 2, 1},
            std::array<std::int32_t, 6>{2, 2, 2, 2, 2, 2}});
    }
    return makePhysical<Nature>(std::array<std::array<std::int32_t, 6>, 12>{
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 1},
        std::array<std::int32_t, 6>{1, 1, 0, 0, 0, -1},
        std::array<std::int32_t, 6>{-1, 0, -1, 2, 0, 0},
        std::array<std::int32_t, 6>{-2, -1, -2, 3, 1, -1},
        std::array<std::int32_t, 6>{0, 0, 2, -1, -1, 0},
        std::array<std::int32_t, 6>{0, -1, 3, -2, -2, 0},
        std::array<std::int32_t, 6>{-2, -2, -2, -2, -2, -2},
        std::array<std::int32_t, 6>{1, 1, 1, 1, 1, 1},
        std::array<std::int32_t, 6>{0, 1, 0, 3, 2, 0},
        std::array<std::int32_t, 6>{1, 0, 3, 0, 0, 2},
        std::array<std::int32_t, 6>{1, 1, 2, 2, 1, 2},
        std::array<std::int32_t, 6>{2, 2, 2, 2, 2, 2}});
}

PhysicalModifierTable<Makings> System::physicalModMakings(StringView legacyName) {
    if (startsWith(enumutils::lowerCase(legacyName), "cw´")) {
        return makePhysical<Makings>(std::array<std::array<std::int32_t, 6>, 48>{
            std::array<std::int32_t, 6>{0, 0, 0, 0, -1, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 1, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 1, 0, -1},
            std::array<std::int32_t, 6>{0, 0, 1, 0, -1, 0},
            std::array<std::int32_t, 6>{-1, 0, 0, 0, 1, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, -1},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 1},
            std::array<std::int32_t, 6>{0, 0, -1, 0, 0, 1},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{0, -1, 0, 0, 0, 1},
            std::array<std::int32_t, 6>{0, 1, 0, 0, 0, -1},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 1, -1},
            std::array<std::int32_t, 6>{1, 0, 0, 0, -1, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 0, -1, 1},
            std::array<std::int32_t, 6>{0, 0, 1, 0, 0, -1},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{-1, 1, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 1, 0, 0, -1, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 1, 0, -1, 0, 0},
            std::array<std::int32_t, 6>{0, 0, -1, 0, 1, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{0, -1, 0, 0, 1, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 1, -1, 0},
            std::array<std::int32_t, 6>{0, 0, 0, -1, 0, 1},
            std::array<std::int32_t, 6>{1, -1, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{-1, 0, 1, 0, 0, 0},
            std::array<std::int32_t, 6>{1, 0, -1, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 1, -1, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 0, 0, -1, 1, 0},
            std::array<std::int32_t, 6>{-1, 0, 0, 0, 0, 1},
            std::array<std::int32_t, 6>{0, -1, 1, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 0, 1, -1, 0, 0},
            std::array<std::int32_t, 6>{0, 0, -1, 1, 0, 0},
            std::array<std::int32_t, 6>{-1, 0, 0, 1, 0, 0},
            std::array<std::int32_t, 6>{1, 0, 0, -1, 0, 0},
            std::array<std::int32_t, 6>{0, -1, 0, 1, 0, 0},
            std::array<std::int32_t, 6>{1, 0, 0, 0, 0, -1},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
            std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0}});
    }
    return makePhysical<Makings>(std::array<std::array<std::int32_t, 6>, 48>{
        std::array<std::int32_t, 6>{0, 0, 0, 0, -1, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 1, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 1, 0, -1, 0},
        std::array<std::int32_t, 6>{0, -1, 0, 0, 1, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, -1},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 1},
        std::array<std::int32_t, 6>{0, 0, -1, 0, 0, 1},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, -1, 1, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 1, 0, 0, 0, -1},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 1, -1},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, -1, 1},
        std::array<std::int32_t, 6>{-1, 1, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 1, 0, -1},
        std::array<std::int32_t, 6>{0, 1, 0, 0, -1, 0},
        std::array<std::int32_t, 6>{0, 0, 0, -1, 0, 1},
        std::array<std::int32_t, 6>{0, 1, 0, -1, 0, 0},
        std::array<std::int32_t, 6>{0, 0, -1, 0, 1, 0},
        std::array<std::int32_t, 6>{1, 0, 0, 0, -1, 0},
        std::array<std::int32_t, 6>{0, -1, 0, 0, 0, 1},
        std::array<std::int32_t, 6>{0, 0, 0, 1, -1, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{1, -1, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 1, 0, 0, -1},
        std::array<std::int32_t, 6>{-1, 0, 0, 0, 1, 0},
        std::array<std::int32_t, 6>{1, 0, -1, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 1, -1, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 0, -1, 1, 0},
        std::array<std::int32_t, 6>{-1, 0, 0, 0, 0, 1},
        std::array<std::int32_t, 6>{-1, 0, 1, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 1, -1, 0, 0},
        std::array<std::int32_t, 6>{0, 0, -1, 1, 0, 0},
        std::array<std::int32_t, 6>{-1, 0, 0, 1, 0, 0},
        std::array<std::int32_t, 6>{1, 0, 0, -1, 0, 0},
        std::array<std::int32_t, 6>{0, -1, 0, 1, 0, 0},
        std::array<std::int32_t, 6>{1, 0, 0, 0, 0, -1},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0},
        std::array<std::int32_t, 6>{0, 0, 0, 0, 0, 0}});
}

MentalModifierTable<Sex> System::mentalModSex(StringView /*legacyName*/) {
    return makeMental<Sex>(std::array<std::array<double, 5>, 2>{
        std::array<double, 5>{0.5, 0, 0, 0, 0},
        std::array<double, 5>{0, 0.5, 0, 0, 0}});
}

MentalModifierTable<Period> System::mentalModPeriod(StringView /*legacyName*/) {
    return makeMental<Period>(std::array<std::array<double, 5>, 4>{
        std::array<double, 5>{0, -0.5, 0, 0.5, 0},
        std::array<double, 5>{0, 0, 0, 0, 0},
        std::array<double, 5>{-0.5, 0.5, 0, 0, 0},
        std::array<double, 5>{-0.5, 0.5, -0.5, 0, 0.5}});
}

MentalModifierTable<Nature> System::mentalModNature(StringView legacyName) {
    if (startsWith(enumutils::lowerCase(legacyName), "cw´")) {
        return makeMental<Nature>(std::array<std::array<double, 5>, 12>{
            std::array<double, 5>{-0.5, 0.5, 0, 0, 0},
            std::array<double, 5>{0, 0, 0, 0.5, 0},
            std::array<double, 5>{0, 0, 1.5, 0, 0},
            std::array<double, 5>{1, -0.5, 0.5, 0, 0},
            std::array<double, 5>{0, 0.5, 0, 0, 0},
            std::array<double, 5>{0, 0.5, 0, 0, 1},
            std::array<double, 5>{0, 0.5, -0.5, 0, 0},
            std::array<double, 5>{0, 0.5, 0, 0, 0},
            std::array<double, 5>{1, 0, 1, 0, 0},
            std::array<double, 5>{0, 0.5, 0, 0, 0.5},
            std::array<double, 5>{0, 0, 1, 1, -0.5},
            std::array<double, 5>{0, 0, 0, 0, 0}});
    }
    if (enumutils::lowerCase(legacyName) == "darkwirth") {
        return makeMental<Nature>(std::array<std::array<double, 5>, 12>{
            std::array<double, 5>{-0.5, 0.5, 0, 0, 0},
            std::array<double, 5>{0, 0, 0, 0.5, 0},
            std::array<double, 5>{1, 0, 0, 0, 0},
            std::array<double, 5>{0.5, -0.5, 0.5, 0, 0},
            std::array<double, 5>{0, 1, 0, 0, 0},
            std::array<double, 5>{0, -0.5, 0, 0, 0.5},
            std::array<double, 5>{0, 0.5, -0.5, 0, 0},
            std::array<double, 5>{0, 0.5, 0, 0.5, 0},
            std::array<double, 5>{0.5, 0, 0.5, 0, 0},
            std::array<double, 5>{0, 0.5, 0, 0, 1},
            std::array<double, 5>{0, 0, 0, 0, 1},
            std::array<double, 5>{0, 0, 0, 0, 0}});
    }
    return makeMental<Nature>(std::array<std::array<double, 5>, 12>{
        std::array<double, 5>{-0.5, 0.5, 0, 0, 0},
        std::array<double, 5>{0, 0, 0, 0.5, 0},
        std::array<double, 5>{0, 0, 1, 0, 0},
        std::array<double, 5>{0.5, -0.5, 0.5, 0, 0},
        std::array<double, 5>{0, 0.5, 0, 0, 0},
        std::array<double, 5>{0, 0.5, 0, 0, 0.5},
        std::array<double, 5>{0, 0.5, -0.5, 0, 0},
        std::array<double, 5>{0, 0.5, 0, 0.5, 0},
        std::array<double, 5>{0.5, 0, 0.5, 0, 0},
        std::array<double, 5>{0, 0.5, 0, 0, 0.5},
        std::array<double, 5>{0, 0, 0.5, 0.5, -0.5},
        std::array<double, 5>{0, 0, 0, 0, 0}});
}

MentalModifierTable<Makings> System::mentalModMakings(StringView legacyName) {
    if (startsWith(enumutils::lowerCase(legacyName), "cw´")) {
        return makeMental<Makings>(std::array<std::array<double, 5>, 48>{
            std::array<double, 5>{0, 0, 0, 1, 0},
            std::array<double, 5>{0, 0, 0, -1, 0},
            std::array<double, 5>{-0.5, 0, 0.5, 0.5, 0},
            std::array<double, 5>{0, -0.5, 0, 0, 0.5},
            std::array<double, 5>{0, 0, 0, 0.5, 0.5},
            std::array<double, 5>{0, 0, 0, 0, -0.5},
            std::array<double, 5>{-0.5, 0, 0, 0, -0.5},
            std::array<double, 5>{0, 0, -0.5, 0, 0},
            std::array<double, 5>{0, 0, 0.5, 0, -0.5},
            std::array<double, 5>{0, 0.5, 0, 0, 0.5},
            std::array<double, 5>{0, 0, 0.5, 0, -0.5},
            std::array<double, 5>{0, 0, -0.5, 0, 1},
            std::array<double, 5>{0, 1, 0, 0, 0.5},
            std::array<double, 5>{0, -1, 0, 0, -0.5},
            std::array<double, 5>{0.5, 0, -0.5, 0, 0},
            std::array<double, 5>{-0.5, 0, 0, 0, 0},
            std::array<double, 5>{-0.5, 0, 0, 0, 0},
            std::array<double, 5>{0, 0, 0, -0.5, 1},
            std::array<double, 5>{0.5, 0.5, 0, 0, 0},
            std::array<double, 5>{0.5, 0, 0, -0.5, 0.5},
            std::array<double, 5>{0, -0.5, 0.5, 0, 0},
            std::array<double, 5>{-0.5, 0.5, -0.5, 0, 0},
            std::array<double, 5>{0, 0.5, -0.5, 0, 0},
            std::array<double, 5>{0, 0, 0.5, 0, 0},
            std::array<double, 5>{0, -0.5, 0.5, 0, 0},
            std::array<double, 5>{0, 0, 0, -0.5, 0},
            std::array<double, 5>{1, -0.5, 0, 0, 0},
            std::array<double, 5>{-1, 0.5, 0, 0, 0},
            std::array<double, 5>{0, -0.5, 0.5, 0, 0},
            std::array<double, 5>{0, 0.5, -0.5, 0, 0},
            std::array<double, 5>{0, 0, 0, 0, 0},
            std::array<double, 5>{0, 0, 0, 0.5, 0.5},
            std::array<double, 5>{0, 0, 0, 1, 0},
            std::array<double, 5>{0, 0, -0.5, -1, 0},
            std::array<double, 5>{0, -1, 0, 0.5, 0},
            std::array<double, 5>{0, 0, -0.5, -0.5, 0},
            std::array<double, 5>{0.5, 0, 0, -0.5, 0},
            std::array<double, 5>{-0.5, 0.5, 0, 0, -0.5},
            std::array<double, 5>{-0.5, 0, 0, 0.5, 0},
            std::array<double, 5>{0.5, -0.5, 0, -0.5, 0},
            std::array<double, 5>{0, 0, 0.5, -0.5, 0},
            std::array<double, 5>{0, 0.5, -0.5, 0, 0},
            std::array<double, 5>{0.5, 0, 0.5, 0, -0.5},
            std::array<double, 5>{0, 0, -0.5, 0.5, 0},
            std::array<double, 5>{0, 0, 0, 0.5, -0.5},
            std::array<double, 5>{0.5, 0, 0, -0.5, 0},
            std::array<double, 5>{0.5, 0, 0.5, 0, -0.5},
            std::array<double, 5>{-0.5, 0, 0, 0.5, -0.5}});
    }
    return makeMental<Makings>(std::array<std::array<double, 5>, 48>{
        std::array<double, 5>{0, 0, 0, 0.5, 0},
        std::array<double, 5>{0, 0, 0, -0.5, 0},
        std::array<double, 5>{-0.5, 0, 0.5, 0, 0},
        std::array<double, 5>{0, -0.5, 0, 0, 0.5},
        std::array<double, 5>{0, 0, 0, 0.5, 0.5},
        std::array<double, 5>{0, 0, 0, 0, -0.5},
        std::array<double, 5>{-0.5, 0, 0, 0, -0.5},
        std::array<double, 5>{0.5, 0, -0.5, 0, 0},
        std::array<double, 5>{0, 0, 0.5, 0, -0.5},
        std::array<double, 5>{0, 0.5, 0, 0, 0.5},
        std::array<double, 5>{0, 0, 0.5, 0, -0.5},
        std::array<double, 5>{0, 0, -0.5, 0, 0.5},
        std::array<double, 5>{0, 0.5, 0, 0, 0.5},
        std::array<double, 5>{0, -0.5, 0, 0, 0},
        std::array<double, 5>{0.5, -0.5, -0.5, 0, 0},
        std::array<double, 5>{-0.5, 0, 0, 0, 0},
        std::array<double, 5>{-0.5, 0, 0, 0, 0},
        std::array<double, 5>{0.5, 0, 0, -0.5, 0.5},
        std::array<double, 5>{0.5, 0, 0, 0, -0.5},
        std::array<double, 5>{0.5, 0, 0, 0, 0.5},
        std::array<double, 5>{0, -0.5, 0.5, 0, 0},
        std::array<double, 5>{-0.5, 0.5, 0, 0, 0},
        std::array<double, 5>{0, 0.5, 0, -0.5, 0},
        std::array<double, 5>{0, 0, 0, 0, 0},
        std::array<double, 5>{0, 0, 0, 0, 0},
        std::array<double, 5>{0, 0, 0, -0.5, 0},
        std::array<double, 5>{0.5, -0.5, 0, 0, 0},
        std::array<double, 5>{-0.5, 0.5, 0, 0, 0},
        std::array<double, 5>{0, -0.5, 0.5, 0, 0},
        std::array<double, 5>{0, 0.5, -0.5, 0, 0},
        std::array<double, 5>{0, 0, 0, 0, 0},
        std::array<double, 5>{0, 0, 0, 0.5, 0.5},
        std::array<double, 5>{0, 0, 0, 0.5, 0},
        std::array<double, 5>{0, 0, -0.5, 0, 0},
        std::array<double, 5>{0, -0.5, 0, 0.5, 0},
        std::array<double, 5>{0, 0, -0.5, 0, 0},
        std::array<double, 5>{0.5, 0, 0, -0.5, 0},
        std::array<double, 5>{0, 0.5, 0, 0, 0},
        std::array<double, 5>{-0.5, 0, 0, 0.5, 0},
        std::array<double, 5>{0.5, 0, 0, -0.5, 0},
        std::array<double, 5>{0, 0, 0.5, -0.5, 0},
        std::array<double, 5>{0, 0.5, -0.5, 0, 0},
        std::array<double, 5>{0, 0, 0.5, 0, -0.5},
        std::array<double, 5>{0, 0, -0.5, 0.5, 0},
        std::array<double, 5>{0, 0, 0, 0.5, -0.5},
        std::array<double, 5>{0, 0, 0, -0.5, 0},
        std::array<double, 5>{0, -0.5, 0.5, 0, -0.5},
        std::array<double, 5>{-0.5, 0, 0, 0, 0}});
}

String System::yadoName(StringView legacyName) const {
    const String base = enumutils::lowerCase(utils::pathBaseName(legacyName));
    if (base == "s_c_wirth") return "heya";
    if (base == "oedowirth") return "Naga";
    if (base == "dirkwirth") return "mori";
    return "Yado";
}

// Dのstatic-ifは、この四つの特徴ラッパーだけ
// を受け付ける。宣言ヘッダーしか見えない翻訳単位から
// も使えるよう、共通の公開形式を明示的に具体化する。
template PhysicalModifierTable<Sex> System::physicalMod<Sex>(StringView) const;
template PhysicalModifierTable<Period> System::physicalMod<Period>(StringView) const;
template PhysicalModifierTable<Nature> System::physicalMod<Nature>(StringView) const;
template PhysicalModifierTable<Makings> System::physicalMod<Makings>(StringView) const;
template MentalModifierTable<Sex> System::mentalMod<Sex>(StringView) const;
template MentalModifierTable<Period> System::mentalMod<Period>(StringView) const;
template MentalModifierTable<Nature> System::mentalMod<Nature>(StringView) const;
template MentalModifierTable<Makings> System::mentalMod<Makings>(StringView) const;

}  // namespace cwxeditor::cwx::system
