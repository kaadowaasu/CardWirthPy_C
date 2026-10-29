#include "cwxeditor_src/core/cwx/variables.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace cwxeditor::cwx::variables {

namespace {
template <typename T>
struct is_equality_comparable
    : std::bool_constant<requires(const T& left, const T& right) {
          left == right;
      }> {};

template <typename T, typename Allocator>
struct is_equality_comparable<std::vector<T, Allocator>>
    : is_equality_comparable<T> {};

template <typename Property>
[[nodiscard]] bool changed(const Property& property, bool writeAll) {
    if (writeAll) return true;
    using Value = std::remove_cvref_t<decltype(property.value)>;
    if constexpr (is_equality_comparable<Value>::value) {
        return !(property.value == property.INIT);
    } else {
        // 移植した集成体にC++のoperator==がなければ、正規のXML表現
        // を比較する。Dの値とINITの比較による書き込みの絞り込みを維持する。
        auto current = xml::XNode::create("compare");
        auto initial = xml::XNode::create("compare");
        settings::toNode(current, property.KEY, property.value);
        settings::toNode(initial, property.KEY, property.INIT);
        return current.serialize() != initial.serialize();
    }
}
}  // namespace

xml::XNode ContentInitializer::toNode() const {
    auto result = xml::XNode::create(XML_NAME, lib::String{});
    result.newAttr("dataVersion", dataVersion);
    for (const auto& [type, value] : initializer) {
        static_cast<void>(type);
        if (value) value->toNode(result, nullptr);
    }
    return result;
}

void ContentInitializer::toNode(xml::XNode& node) const {
    auto output = node.newElement(XML_NAME, lib::String{});
    output.newAttr("dataVersion", dataVersion);
    for (const auto& [type, value] : initializer) {
        static_cast<void>(type);
        if (value) value->toNode(output, nullptr);
    }
}

void ContentInitializer::fromNode(xml::XNode& node) {
    if (!node.valid() || node.name() != XML_NAME) {
        throw std::runtime_error("Node is not content initializer");
    }
    dataVersion = node.attr<lib::String>("dataVersion", true);
    initializer.clear();
    for (const auto& child : node.element_children()) {
        if (auto value = event::Content::createFromNode(child, nullptr, true))
            initializer[value->type()] = std::move(value);
    }
}

bool FlexEtcProps::searchContents(types::CType id) const {
    ::cwxeditor::cwx::perf::TraceScope trace_variables_d618(__FILE__, __LINE__);
    switch (id) {
        case types::CType::Start: return searchContentsStart.value;
        case types::CType::StartBattle: return searchContentsStartBattle.value;
        case types::CType::End: return searchContentsEnd.value;
        case types::CType::EndBadEnd: return searchContentsEndBadEnd.value;
        case types::CType::ChangeArea: return searchContentsChangeArea.value;
        case types::CType::ChangeBgImage: return searchContentsChangeBgImage.value;
        case types::CType::Effect: return searchContentsEffect.value;
        case types::CType::EffectBreak: return searchContentsEffectBreak.value;
        case types::CType::LinkStart: return searchContentsLinkStart.value;
        case types::CType::LinkPackage: return searchContentsLinkPackage.value;
        case types::CType::TalkMessage: return searchContentsTalkMessage.value;
        case types::CType::TalkDialog: return searchContentsTalkDialog.value;
        case types::CType::PlayBgm: return searchContentsPlayBgm.value;
        case types::CType::PlaySound: return searchContentsPlaySound.value;
        case types::CType::Wait: return searchContentsWait.value;
        case types::CType::ElapseTime: return searchContentsElapseTime.value;
        case types::CType::CallStart: return searchContentsCallStart.value;
        case types::CType::CallPackage: return searchContentsCallPackage.value;
        case types::CType::BranchFlag: return searchContentsBranchFlag.value;
        case types::CType::BranchMultiStep: return searchContentsBranchMultiStep.value;
        case types::CType::BranchStep: return searchContentsBranchStep.value;
        case types::CType::BranchSelect: return searchContentsBranchSelect.value;
        case types::CType::BranchAbility: return searchContentsBranchAbility.value;
        case types::CType::BranchRandom: return searchContentsBranchRandom.value;
        case types::CType::BranchLevel: return searchContentsBranchLevel.value;
        case types::CType::BranchStatus: return searchContentsBranchStatus.value;
        case types::CType::BranchPartyNumber: return searchContentsBranchPartyNumber.value;
        case types::CType::BranchArea: return searchContentsBranchArea.value;
        case types::CType::BranchBattle: return searchContentsBranchBattle.value;
        case types::CType::BranchIsBattle: return searchContentsBranchIsBattle.value;
        case types::CType::BranchCast: return searchContentsBranchCast.value;
        case types::CType::BranchItem: return searchContentsBranchItem.value;
        case types::CType::BranchSkill: return searchContentsBranchSkill.value;
        case types::CType::BranchInfo: return searchContentsBranchInfo.value;
        case types::CType::BranchBeast: return searchContentsBranchBeast.value;
        case types::CType::BranchMoney: return searchContentsBranchMoney.value;
        case types::CType::BranchCoupon: return searchContentsBranchCoupon.value;
        case types::CType::BranchCompleteStamp: return searchContentsBranchCompleteStamp.value;
        case types::CType::BranchGossip: return searchContentsBranchGossip.value;
        case types::CType::SetFlag: return searchContentsSetFlag.value;
        case types::CType::SetStep: return searchContentsSetStep.value;
        case types::CType::SetStepUp: return searchContentsSetStepUp.value;
        case types::CType::SetStepDown: return searchContentsSetStepDown.value;
        case types::CType::ReverseFlag: return searchContentsReverseFlag.value;
        case types::CType::CheckFlag: return searchContentsCheckFlag.value;
        case types::CType::GetCast: return searchContentsGetCast.value;
        case types::CType::GetItem: return searchContentsGetItem.value;
        case types::CType::GetSkill: return searchContentsGetSkill.value;
        case types::CType::GetInfo: return searchContentsGetInfo.value;
        case types::CType::GetBeast: return searchContentsGetBeast.value;
        case types::CType::GetMoney: return searchContentsGetMoney.value;
        case types::CType::GetCoupon: return searchContentsGetCoupon.value;
        case types::CType::GetCompleteStamp: return searchContentsGetCompleteStamp.value;
        case types::CType::GetGossip: return searchContentsGetGossip.value;
        case types::CType::LoseCast: return searchContentsLoseCast.value;
        case types::CType::LoseItem: return searchContentsLoseItem.value;
        case types::CType::LoseSkill: return searchContentsLoseSkill.value;
        case types::CType::LoseInfo: return searchContentsLoseInfo.value;
        case types::CType::LoseBeast: return searchContentsLoseBeast.value;
        case types::CType::LoseMoney: return searchContentsLoseMoney.value;
        case types::CType::LoseCoupon: return searchContentsLoseCoupon.value;
        case types::CType::LoseCompleteStamp: return searchContentsLoseCompleteStamp.value;
        case types::CType::LoseGossip: return searchContentsLoseGossip.value;
        case types::CType::ShowParty: return searchContentsShowParty.value;
        case types::CType::HideParty: return searchContentsHideParty.value;
        case types::CType::Redisplay: return searchContentsRedisplay.value;
        case types::CType::SubstituteStep: return searchContentsSubstituteStep.value;
        case types::CType::SubstituteFlag: return searchContentsSubstituteFlag.value;
        case types::CType::BranchStepCmp: return searchContentsBranchStepCmp.value;
        case types::CType::BranchFlagCmp: return searchContentsBranchFlagCmp.value;
        case types::CType::BranchRandomSelect: return searchContentsBranchRandomSelect.value;
        case types::CType::BranchKeyCode: return searchContentsBranchKeyCode.value;
        case types::CType::CheckStep: return searchContentsCheckStep.value;
        case types::CType::BranchRound: return searchContentsBranchRound.value;
        case types::CType::MoveBgImage: return searchContentsMoveBgImage.value;
        case types::CType::ReplaceBgImage: return searchContentsReplaceBgImage.value;
        case types::CType::LoseBgImage: return searchContentsLoseBgImage.value;
        case types::CType::BranchMultiCoupon: return searchContentsBranchMultiCoupon.value;
        case types::CType::BranchMultiRandom: return searchContentsBranchMultiRandom.value;
        case types::CType::MoveCard: return searchContentsMoveCard.value;
        case types::CType::ChangeEnvironment: return searchContentsChangeEnvironment.value;
        case types::CType::BranchVariant: return searchContentsBranchVariant.value;
        case types::CType::SetVariant: return searchContentsSetVariant.value;
        case types::CType::CheckVariant: return searchContentsCheckVariant.value;
        case types::CType::CustomXmlContent: return searchContentsCustomXmlContent.value;
    }
    throw std::out_of_range("unknown cwx CType");
}

void FlexEtcProps::searchContents(types::CType id, bool value) {
    ::cwxeditor::cwx::perf::TraceScope trace_variables_d621(__FILE__, __LINE__);
    switch (id) {
        case types::CType::Start: searchContentsStart.value = value; return;
        case types::CType::StartBattle: searchContentsStartBattle.value = value; return;
        case types::CType::End: searchContentsEnd.value = value; return;
        case types::CType::EndBadEnd: searchContentsEndBadEnd.value = value; return;
        case types::CType::ChangeArea: searchContentsChangeArea.value = value; return;
        case types::CType::ChangeBgImage: searchContentsChangeBgImage.value = value; return;
        case types::CType::Effect: searchContentsEffect.value = value; return;
        case types::CType::EffectBreak: searchContentsEffectBreak.value = value; return;
        case types::CType::LinkStart: searchContentsLinkStart.value = value; return;
        case types::CType::LinkPackage: searchContentsLinkPackage.value = value; return;
        case types::CType::TalkMessage: searchContentsTalkMessage.value = value; return;
        case types::CType::TalkDialog: searchContentsTalkDialog.value = value; return;
        case types::CType::PlayBgm: searchContentsPlayBgm.value = value; return;
        case types::CType::PlaySound: searchContentsPlaySound.value = value; return;
        case types::CType::Wait: searchContentsWait.value = value; return;
        case types::CType::ElapseTime: searchContentsElapseTime.value = value; return;
        case types::CType::CallStart: searchContentsCallStart.value = value; return;
        case types::CType::CallPackage: searchContentsCallPackage.value = value; return;
        case types::CType::BranchFlag: searchContentsBranchFlag.value = value; return;
        case types::CType::BranchMultiStep: searchContentsBranchMultiStep.value = value; return;
        case types::CType::BranchStep: searchContentsBranchStep.value = value; return;
        case types::CType::BranchSelect: searchContentsBranchSelect.value = value; return;
        case types::CType::BranchAbility: searchContentsBranchAbility.value = value; return;
        case types::CType::BranchRandom: searchContentsBranchRandom.value = value; return;
        case types::CType::BranchLevel: searchContentsBranchLevel.value = value; return;
        case types::CType::BranchStatus: searchContentsBranchStatus.value = value; return;
        case types::CType::BranchPartyNumber: searchContentsBranchPartyNumber.value = value; return;
        case types::CType::BranchArea: searchContentsBranchArea.value = value; return;
        case types::CType::BranchBattle: searchContentsBranchBattle.value = value; return;
        case types::CType::BranchIsBattle: searchContentsBranchIsBattle.value = value; return;
        case types::CType::BranchCast: searchContentsBranchCast.value = value; return;
        case types::CType::BranchItem: searchContentsBranchItem.value = value; return;
        case types::CType::BranchSkill: searchContentsBranchSkill.value = value; return;
        case types::CType::BranchInfo: searchContentsBranchInfo.value = value; return;
        case types::CType::BranchBeast: searchContentsBranchBeast.value = value; return;
        case types::CType::BranchMoney: searchContentsBranchMoney.value = value; return;
        case types::CType::BranchCoupon: searchContentsBranchCoupon.value = value; return;
        case types::CType::BranchCompleteStamp: searchContentsBranchCompleteStamp.value = value; return;
        case types::CType::BranchGossip: searchContentsBranchGossip.value = value; return;
        case types::CType::SetFlag: searchContentsSetFlag.value = value; return;
        case types::CType::SetStep: searchContentsSetStep.value = value; return;
        case types::CType::SetStepUp: searchContentsSetStepUp.value = value; return;
        case types::CType::SetStepDown: searchContentsSetStepDown.value = value; return;
        case types::CType::ReverseFlag: searchContentsReverseFlag.value = value; return;
        case types::CType::CheckFlag: searchContentsCheckFlag.value = value; return;
        case types::CType::GetCast: searchContentsGetCast.value = value; return;
        case types::CType::GetItem: searchContentsGetItem.value = value; return;
        case types::CType::GetSkill: searchContentsGetSkill.value = value; return;
        case types::CType::GetInfo: searchContentsGetInfo.value = value; return;
        case types::CType::GetBeast: searchContentsGetBeast.value = value; return;
        case types::CType::GetMoney: searchContentsGetMoney.value = value; return;
        case types::CType::GetCoupon: searchContentsGetCoupon.value = value; return;
        case types::CType::GetCompleteStamp: searchContentsGetCompleteStamp.value = value; return;
        case types::CType::GetGossip: searchContentsGetGossip.value = value; return;
        case types::CType::LoseCast: searchContentsLoseCast.value = value; return;
        case types::CType::LoseItem: searchContentsLoseItem.value = value; return;
        case types::CType::LoseSkill: searchContentsLoseSkill.value = value; return;
        case types::CType::LoseInfo: searchContentsLoseInfo.value = value; return;
        case types::CType::LoseBeast: searchContentsLoseBeast.value = value; return;
        case types::CType::LoseMoney: searchContentsLoseMoney.value = value; return;
        case types::CType::LoseCoupon: searchContentsLoseCoupon.value = value; return;
        case types::CType::LoseCompleteStamp: searchContentsLoseCompleteStamp.value = value; return;
        case types::CType::LoseGossip: searchContentsLoseGossip.value = value; return;
        case types::CType::ShowParty: searchContentsShowParty.value = value; return;
        case types::CType::HideParty: searchContentsHideParty.value = value; return;
        case types::CType::Redisplay: searchContentsRedisplay.value = value; return;
        case types::CType::SubstituteStep: searchContentsSubstituteStep.value = value; return;
        case types::CType::SubstituteFlag: searchContentsSubstituteFlag.value = value; return;
        case types::CType::BranchStepCmp: searchContentsBranchStepCmp.value = value; return;
        case types::CType::BranchFlagCmp: searchContentsBranchFlagCmp.value = value; return;
        case types::CType::BranchRandomSelect: searchContentsBranchRandomSelect.value = value; return;
        case types::CType::BranchKeyCode: searchContentsBranchKeyCode.value = value; return;
        case types::CType::CheckStep: searchContentsCheckStep.value = value; return;
        case types::CType::BranchRound: searchContentsBranchRound.value = value; return;
        case types::CType::MoveBgImage: searchContentsMoveBgImage.value = value; return;
        case types::CType::ReplaceBgImage: searchContentsReplaceBgImage.value = value; return;
        case types::CType::LoseBgImage: searchContentsLoseBgImage.value = value; return;
        case types::CType::BranchMultiCoupon: searchContentsBranchMultiCoupon.value = value; return;
        case types::CType::BranchMultiRandom: searchContentsBranchMultiRandom.value = value; return;
        case types::CType::MoveCard: searchContentsMoveCard.value = value; return;
        case types::CType::ChangeEnvironment: searchContentsChangeEnvironment.value = value; return;
        case types::CType::BranchVariant: searchContentsBranchVariant.value = value; return;
        case types::CType::SetVariant: searchContentsSetVariant.value = value; return;
        case types::CType::CheckVariant: searchContentsCheckVariant.value = value; return;
        case types::CType::CustomXmlContent: searchContentsCustomXmlContent.value = value; return;
    }
    throw std::out_of_range("unknown cwx CType");
}

void FlexEtcProps::toNode(xml::XNode& node, bool writeAll) const {
    forEachProperty([&](const auto& property) {
        if (changed(property, writeAll)) property.toNode(node);
    });
}

FlexEtcProps FlexEtcProps::fromNode(xml::XNode& node, std::uint64_t dataVersion) {
    FlexEtcProps result;
    if (!node.valid()) return result;
    result.forEachProperty([&](auto& property) {
        auto child = node.child(property.KEY, false);
        if (!child.valid()) return;
        try {
            property.fromNode(child, dataVersion);
        } catch (const std::exception& error) {
            static_cast<void>(utils::printStackTrace());
            // DはネイティブのException文字列を記録し、C++で
            // はwhat()が例外の内容となる。どちらも既存の共通ロガ
            // ーを使い、不正値の後も有効な兄弟プロパティを読み続ける。
            utils::debugln(error.what());
        }
    });
    return result;
}

}  // namespace cwxeditor::cwx::variables
