#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include "cwxeditor_src/core/cwx/features.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/lib/portable.hpp"

namespace cwxeditor::cwx::coupon {
class Coupon;
}

namespace cwxeditor::cwx::system {

using Sex = features::Sex;
using Period = features::Period;
using Nature = features::Nature;
using Makings = features::Makings;
using CouponType = features::CouponType;
using ActionCardType = types::ActionCardType;
using FKCKind = types::FKCKind;
using Mental = types::Mental;
using Physical = types::Physical;

/// XMLからデータを生成する際に必要な情報。
class System;

class XMLInfo {
private:
    lib::Owner<const System> system_owner_;

public:
    XMLInfo(const System& system, lib::String version);
    XMLInfo(const XMLInfo& other);

    // DのXMLInfoはSystemを強く保持する。ネイティブでは状態のない
    // ラッパーの所有コピーへ接続するため、 コンストラクター引数とはアドレスが
    // 異なる場合がある。CProps::sys()は引き続き借用参照とする。
    const System& sys;
    /// XMLのデータバージョン。
    lib::String ver;
};

/// キーコード発火条件とキーコード本体の組み合わせ。
struct FKeyCode {
    lib::String keyCode;
    FKCKind kind = FKCKind::Use;
};

/// 適性。
struct Aptitude {
    Physical physical;
    Mental mental;
};

template <typename E>
using PhysicalModifierTable =
    lib::Dictionary<E, lib::Dictionary<Physical, std::int32_t>>;

template <typename E>
using MentalModifierTable =
    lib::Dictionary<E, lib::Dictionary<Mental, double>>;

class System {
private:
    static PhysicalModifierTable<Sex> physicalModSex(lib::StringView legacyName);
    static PhysicalModifierTable<Period> physicalModPeriod(lib::StringView legacyName);
    static PhysicalModifierTable<Nature> physicalModNature(lib::StringView legacyName);
    static PhysicalModifierTable<Makings> physicalModMakings(lib::StringView legacyName);

    static MentalModifierTable<Sex> mentalModSex(lib::StringView legacyName);
    static MentalModifierTable<Period> mentalModPeriod(lib::StringView legacyName);
    static MentalModifierTable<Nature> mentalModNature(lib::StringView legacyName);
    static MentalModifierTable<Makings> mentalModMakings(lib::StringView legacyName);

    template <typename>
    inline static constexpr bool dependentFalse = false;

public:
    System() = default;

    /// 各特性を名前に変換する。名前は「＿」を除いてクーポンと一致する。
    [[nodiscard]] lib::String sexName(Sex value, lib::StringView legacyName) const;
    [[nodiscard]] lib::String periodName(Period value, lib::StringView legacyName) const;
    [[nodiscard]] lib::String natureName(Nature value, lib::StringView legacyName) const;
    [[nodiscard]] lib::String makingsName(Makings value, lib::StringView legacyName) const;

    [[nodiscard]] lib::String sexCoupon(Sex value, lib::StringView legacyName) const;
    [[nodiscard]] lib::String periodCoupon(Period value, lib::StringView legacyName) const;
    [[nodiscard]] lib::String natureCoupon(Nature value, lib::StringView legacyName) const;
    [[nodiscard]] lib::String makingsCoupon(Makings value, lib::StringView legacyName) const;

    /// 年代ごとの初期クーポンを返す。
    [[nodiscard]] lib::Array<lib::Owner<coupon::Coupon>>
    periodInitialCoupons(Period value, lib::StringView legacyName) const;

    /// パーティ先頭のメンバを指すシステムクーポンを返す。
    [[nodiscard]] lib::String number1Coupon(lib::StringView legacyName) const;

    [[nodiscard]] lib::String actionCardName(ActionCardType type,
                                              lib::StringView legacyName) const;
    [[nodiscard]] Aptitude actionCardAptitude(ActionCardType type) const;

    [[nodiscard]] Aptitude actionOrderAptitude() const;
    [[nodiscard]] Aptitude runAwaySpeedAptitude() const;
    [[nodiscard]] Aptitude resilienceAptitude() const;
    [[nodiscard]] Aptitude resistanceAptitude() const;
    [[nodiscard]] Aptitude avoidanceAptitude() const;

    [[nodiscard]] bool isPenalty(std::span<const lib::String> keyCodes) const;
    [[nodiscard]] lib::String penalty() const;
    [[nodiscard]] bool isRecycle(std::span<const lib::String> keyCodes) const;
    [[nodiscard]] lib::String recycle() const;
    [[nodiscard]] bool isRunAway(std::span<const lib::String> keyCodes) const;
    [[nodiscard]] lib::String runAway() const;

    [[nodiscard]] FKCKind fireKeyCodeKind(lib::StringView keyCode) const;
    [[nodiscard]] FKCKind fireKeyCodeKindRef(lib::String& keyCode) const;
    [[nodiscard]] lib::String convFireKeyCode(const FKeyCode& keyCode) const;
    [[nodiscard]] lib::String convFireKeyCode(lib::StringView keyCode,
                                              FKCKind kind) const;
    [[nodiscard]] FKeyCode toFKeyCode(lib::String keyCode) const;

    [[nodiscard]] lib::String raceCoupon(lib::StringView raceName) const;
    [[nodiscard]] bool isRaceCoupon(lib::StringView coupon) const;

    [[nodiscard]] lib::String levelLimit() const;
    [[nodiscard]] lib::String ep() const;
    [[nodiscard]] bool isGene(lib::StringView coupon) const;

    [[nodiscard]] lib::String userCoupon() const;
    [[nodiscard]] lib::String effectTargetCoupon() const;
    [[nodiscard]] lib::String eventTargetCoupon() const;
    [[nodiscard]] lib::String effectOutOfTargetCoupon() const;
    [[nodiscard]] lib::String evtChildTrue() const;
    [[nodiscard]] lib::String evtChildFalse() const;
    [[nodiscard]] lib::String evtChildDefault() const;
    [[nodiscard]] lib::String evtChildOK(lib::StringView legacyName) const;
    [[nodiscard]] lib::String evtChildGreater() const;
    [[nodiscard]] lib::String evtChildLesser() const;
    [[nodiscard]] lib::String evtChildEq() const;

    [[nodiscard]] CouponType couponType(lib::StringView coupon) const;
    [[nodiscard]] bool isCouponType(lib::StringView coupon, CouponType type) const;
    [[nodiscard]] lib::String convCoupon(lib::StringView coupon,
                                         CouponType type,
                                         bool ignoreSystemCoupon) const;
    [[nodiscard]] lib::String couponHide() const;
    [[nodiscard]] lib::String couponSystem() const;
    [[nodiscard]] lib::String couponDur() const;
    [[nodiscard]] lib::String couponDurBattle() const;

    [[nodiscard]] lib::String prefixSystemVarName() const;
    [[nodiscard]] bool isSystemVar(lib::StringView varName) const;
    [[nodiscard]] lib::String localVariablePrefix() const;
    [[nodiscard]] bool isLocalVariable(lib::StringView path) const;
    [[nodiscard]] lib::String randomValue() const;
    [[nodiscard]] lib::String selectedPlayerCardNumber() const;
    [[nodiscard]] lib::String playerCardName(std::uint32_t number) const;

    /// 特徴による肉体能力の修正値の表を返す。 Dは呼び出しごとにconst
    /// の連想配列を作る。ネイティブの戻り値も独立して所有し、
    /// 局所コピーを編集してもSystemの状態は変わらない。
    template <typename E>
    [[nodiscard]] PhysicalModifierTable<E> physicalMod(lib::StringView legacyName) const {
        using Feature = std::remove_cv_t<E>;
        if constexpr (std::is_same_v<Feature, Sex>) {
            return physicalModSex(legacyName);
        } else if constexpr (std::is_same_v<Feature, Period>) {
            return physicalModPeriod(legacyName);
        } else if constexpr (std::is_same_v<Feature, Nature>) {
            return physicalModNature(legacyName);
        } else if constexpr (std::is_same_v<Feature, Makings>) {
            return physicalModMakings(legacyName);
        } else {
            static_assert(dependentFalse<Feature>,
                          "physicalMod is defined only for CWX feature types");
        }
    }

    /// 特徴による精神能力の修正値を返す。
    /// ネイティブの結果は新しく作った表を所
    /// 有し、内部の可変の表を公開しない。
    template <typename E>
    [[nodiscard]] MentalModifierTable<E> mentalMod(lib::StringView legacyName) const {
        using Feature = std::remove_cv_t<E>;
        if constexpr (std::is_same_v<Feature, Sex>) {
            return mentalModSex(legacyName);
        } else if constexpr (std::is_same_v<Feature, Period>) {
            return mentalModPeriod(legacyName);
        } else if constexpr (std::is_same_v<Feature, Nature>) {
            return mentalModNature(legacyName);
        } else if constexpr (std::is_same_v<Feature, Makings>) {
            return mentalModMakings(legacyName);
        } else {
            static_assert(dependentFalse<Feature>,
                          "mentalMod is defined only for CWX feature types");
        }
    }

    /// エンジンのファイル名からYadoフォルダの名前を返す。
    [[nodiscard]] lib::String yadoName(lib::StringView legacyName) const;
};

}  // namespace cwxeditor::cwx::system
