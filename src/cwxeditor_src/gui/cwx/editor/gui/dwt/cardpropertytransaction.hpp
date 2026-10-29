#pragma once

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/coupon.hpp"
#include "cwxeditor_src/core/cwx/motion.hpp"
#include <array>
#include <type_traits>

namespace cwxeditor::cwx::editor::gui::dwt::cardpropertytransaction {

inline constexpr std::array propertyEnhancements{
    types::Enhance::Avoid, types::Enhance::Resist, types::Enhance::Defense};

// 適用とホスト側のUndo/Redoで共用するプロパティ変更範囲
// 。 汎用Cardコピーと異なり、ID・コメント・リンク・Hold
// ・イベント・ローカル変数の所有・ 所持カード・参照数の関連付
// けには触れない。中止可能な検証の後で、直前の状態を取得する。
template <typename C>
requires (std::is_same_v<C, card::SkillCard> ||
          std::is_same_v<C, card::ItemCard> ||
          std::is_same_v<C, card::BeastCard>)
void copyProperties(C& target, const C& source) {
    // プロパティの適用は、Dの項目ごとの処理と同じく、この画面が公開する項目だけ
    // を変更する。 作業コピーが一覧のコメント・Hold変更より古い場合もある
    // ため、 識別子・リンク・カウンターや無関係な実行中の状態はコピーしない。
    target.paths(source.paths());
    target.desc(source.desc());
    target.name(source.name());
    target.spell(source.spell());
    target.effectType(source.effectType());
    target.resist(source.resist());
    target.physical(source.physical());
    target.mental(source.mental());
    if constexpr (std::is_same_v<C, card::SkillCard>) {
        target.level(source.level());
    } else if constexpr (std::is_same_v<C, card::ItemCard>) {
        target.useLimitMax(source.useLimitMax());
        target.useLimit(source.useLimit());
        for (const auto value : propertyEnhancements)
            target.enhanceOwner(value, source.enhanceOwner(value));
    } else {
        target.showStyle(source.showStyle());
        target.useLimit(source.useLimit());
        target.invocationCondition(source.invocationCondition());
        target.removeWithUnconscious(source.removeWithUnconscious());
    }
    target.price(source.price());
    // 適用後も作業コピーを独立させ、後からキャンセルした効果編集で
    // 、 既に現在のカードが所有する効果まで変わらないようにする。
    lib::Array<lib::Owner<motion::Motion>> copiedMotions;
    for (const auto& motion : source.motions())
        if (motion) copiedMotions.push_back(motion->dup());
    target.motions(copiedMotions);
    for (const auto value : propertyEnhancements)
        target.enhance(value, source.enhance(value));
    target.target(source.target());
    target.allRange(source.allRange());
    target.visual(source.visual());
    target.premium(source.premium());
    target.successRate(source.successRate());
    target.soundPath1(source.soundPath1());
    target.volume1(source.volume1());
    target.loopCount1(source.loopCount1());
    target.soundPath2(source.soundPath2());
    target.volume2(source.volume2());
    target.loopCount2(source.loopCount2());
    target.scenario(source.scenario());
    target.author(source.author());
    target.keyCodes(source.keyCodes());
    target.settingsComment(source.settingsComment());
    target.externalVariantWriteScope(source.externalVariantWriteScope());
    target.externalVariantWriteScenarioVar(source.externalVariantWriteScenarioVar());
    target.externalVariantWriteYado(source.externalVariantWriteYado());
}

inline void copyProperties(card::InfoCard& target, const card::InfoCard& source) {
    target.name(source.name());
    target.paths(source.paths());
    target.desc(source.desc());
}

inline void copyProperties(card::CastCard& target, const card::CastCard& source) {
    target.paths(source.paths());
    target.desc(source.desc());
    target.name(source.name());
    target.copyRaceParam(source);
    target.level(source.level());
    target.lifeMax(source.lifeMax());
    target.life(source.life());
    target.mentality(source.mentality());
    target.mentalityRound(source.mentalityRound());
    target.paralyze(source.paralyze());
    target.poison(source.poison());
    target.bindRound(source.bindRound());
    target.silenceRound(source.silenceRound());
    target.faceUpRound(source.faceUpRound());
    target.antiMagicRound(source.antiMagicRound());
    for (const auto value : {types::Enhance::Action, types::Enhance::Avoid,
                             types::Enhance::Resist, types::Enhance::Defense}) {
        target.enhance(value, source.enhance(value));
        target.enhanceRound(value, source.enhanceRound(value));
    }
    target.levelCoefficient(source.levelCoefficient());
    target.epPerLevel(source.epPerLevel());
    lib::Array<lib::Owner<coupon::Coupon>> coupons;
    for (const auto& value : source.coupons())
        if (value) coupons.push_back(value->dup());
    target.coupons(coupons);
}

inline bool restoreProperties(const lib::Owner<card::Card>& target,
                              const lib::Owner<card::Card>& source) {
    if (!target || !source) return false;
    const auto restore = [&]<typename C>() {
        const auto to = std::dynamic_pointer_cast<C>(target);
        const auto from = std::dynamic_pointer_cast<C>(source);
        if (!to || !from) return false;
        copyProperties(*to, *from);
        return true;
    };
    return restore.template operator()<card::SkillCard>() ||
           restore.template operator()<card::ItemCard>() ||
           restore.template operator()<card::BeastCard>() ||
           restore.template operator()<card::CastCard>() ||
           restore.template operator()<card::InfoCard>();
}

} // namespace cwxeditor::cwx::editor::gui::dwt::cardpropertytransaction
