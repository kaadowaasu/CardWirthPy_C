#pragma once
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"

namespace cwxeditor::cwx::editor::gui::dwt {
// DのEventTreeView.checkOpenDialogと同じく、開けるかどうかはダイアログの種類に依存しない。
inline bool contentDialogResourcesAvailable(types::CType type,
        const std::shared_ptr<summary::Summary>& summary,
        const event::EventTreePtr& tree) {
    if (!summary) return true; // 初期値の編集画面は、シナリオ資源の範囲を持たない。
    const auto variable = [&](int kind) {
        if (!tree) return false;
        const auto has = [kind](const auto& root) {
            return root && (kind == 0 ? !root->allFlags().empty() :
                            kind == 1 ? !root->allSteps().empty() : !root->allVariants().empty());
        };
        if (has(summary->flagDirRoot())) return true;
        const auto counter = tree->useCounter();
        const auto owner = counter ? std::dynamic_pointer_cast<flag::LocalVariableOwner>(counter->owner()) : nullptr;
        return owner && has(owner->flagDirRoot());
    };
    using types::CType;
    switch (type) {
    case CType::StartBattle: return !summary->battles().empty();
    case CType::ChangeArea: return !summary->areas().empty();
    case CType::LinkPackage: case CType::CallPackage: return !summary->packages().empty();
    case CType::BranchFlag: case CType::SetFlag: case CType::ReverseFlag:
    case CType::CheckFlag: case CType::SubstituteFlag: case CType::BranchFlagCmp: return variable(0);
    case CType::BranchMultiStep: case CType::BranchStep: case CType::SetStep:
    case CType::SetStepUp: case CType::SetStepDown: case CType::SubstituteStep:
    case CType::BranchStepCmp: case CType::CheckStep: return variable(1);
    case CType::BranchCast: case CType::GetCast: case CType::LoseCast: return !summary->casts().empty();
    case CType::BranchItem: case CType::GetItem: return !summary->items().empty();
    case CType::LoseItem: return !summary->items().empty() || !summary->legacy();
    case CType::BranchSkill: case CType::GetSkill: return !summary->skills().empty();
    case CType::LoseSkill: return !summary->skills().empty() || !summary->legacy();
    case CType::BranchInfo: case CType::GetInfo: case CType::LoseInfo: return !summary->infos().empty();
    case CType::BranchBeast: case CType::GetBeast: return !summary->beasts().empty();
    case CType::LoseBeast: return !summary->beasts().empty() || !summary->legacy();
    case CType::ShowParty: case CType::HideParty: case CType::Redisplay:
    case CType::EffectBreak: return !summary->legacy();
    case CType::SetVariant: return variable(0) || variable(1) || variable(2);
    default: return true;
    }
}
}
