#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/core/cwx/menu.hpp"

#include "cwxeditor_src/core/cwx/msgs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace cwxeditor::cwx::menu {

namespace {

std::size_t checkedMenuIndex(MenuID id) {
    const auto raw = static_cast<std::int64_t>(id);
    if (raw < 0 || raw >= static_cast<std::int64_t>(enumutils::enum_traits<MenuID>::values.size())) {
        // Dのfinal switchと列挙値を添字にした保存領域は、未知のIDで失敗する。
        throw std::out_of_range("MenuID is outside MenuProps storage");
    }
    return static_cast<std::size_t>(raw);
}


std::size_t findIgnoreCase(StringView text, StringView needle) {
    // PhobosのindexOf(CaseSensitive.no)は、スカラー値
    // のtoLower判定を使ったfindで検索する。比較する各組を検索対象・検索
    // 文字列の順に必要時に復号し、不正入力は未一致に変えず、その時点で失敗させる。
    for (std::size_t start = 0; start < text.size();) {
        auto source = start;
        std::size_t target = 0;
        bool matched = true;
        while (target < needle.size()) {
            if (source == text.size()) { matched = false; break; }
            const auto actual = enumutils::decodeUtf8Scalar(text, source);
            const auto expected = enumutils::decodeUtf8Scalar(needle, target);
            if (enumutils::lowerScalar(actual.value) != enumutils::lowerScalar(expected.value)) {
                matched = false;
                break;
            }
            source = actual.next;
            target = expected.next;
        }
        if (matched) return start;
        start = enumutils::decodeUtf8Scalar(text, start).next;
    }
    return String::npos;
}

void replaceAll(String& text, StringView from, StringView to) {
    if (from.empty()) return;
    std::size_t offset = 0U;
    while ((offset = text.find(from, offset)) != String::npos) {
        text.replace(offset, from.size(), to);
        offset += to.size();
    }
}

}  // namespace

std::size_t MenuProps::index(MenuID id) {
    return checkedMenuIndex(id);
}

MenuProps::MenuProps()
    : mnemonic_(MENU_COUNT),
      mnemonic_init_(MENU_COUNT),
      hotkey_(MENU_COUNT),
      hotkey_init_(MENU_COUNT) {
    mnemonic_[static_cast<std::size_t>(MenuID::None)] = "";
    mnemonic_[static_cast<std::size_t>(MenuID::File)] = "F";
    mnemonic_[static_cast<std::size_t>(MenuID::Edit)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::View)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::Tool)] = "T";
    mnemonic_[static_cast<std::size_t>(MenuID::Table)] = "B";
    mnemonic_[static_cast<std::size_t>(MenuID::Variable)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::Help)] = "H";
    mnemonic_[static_cast<std::size_t>(MenuID::Card)] = "C";
    mnemonic_[static_cast<std::size_t>(MenuID::CardsAndBacks)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::DelNotUsedFile)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::CreateSubWindow)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::LeftPane)] = "L";
    mnemonic_[static_cast<std::size_t>(MenuID::RightPane)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::ClosePane)] = "C";
    mnemonic_[static_cast<std::size_t>(MenuID::ClosePaneExcept)] = "W";
    mnemonic_[static_cast<std::size_t>(MenuID::ClosePaneLeft)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::ClosePaneRight)] = "I";
    mnemonic_[static_cast<std::size_t>(MenuID::ClosePaneAll)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::New)] = "N";
    mnemonic_[static_cast<std::size_t>(MenuID::Open)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::NewAtNewWindow)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::OpenAtNewWindow)] = "P";
    mnemonic_[static_cast<std::size_t>(MenuID::Close)] = "X";
    mnemonic_[static_cast<std::size_t>(MenuID::CloseWin)] = "X";
    mnemonic_[static_cast<std::size_t>(MenuID::Save)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::SaveAs)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::Reload)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::EditScenarioHistory)] = "H";
    mnemonic_[static_cast<std::size_t>(MenuID::EditImportHistory)] = "H";
    mnemonic_[static_cast<std::size_t>(MenuID::EditExecutedPartyHistory)] = "H";
    mnemonic_[static_cast<std::size_t>(MenuID::PutParty)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::OpenDir)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::OpenBackupDir)] = "B";
    mnemonic_[static_cast<std::size_t>(MenuID::OpenPlace)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::SaveImage)] = "I";
    mnemonic_[static_cast<std::size_t>(MenuID::IncludeImage)] = "N";
    mnemonic_[static_cast<std::size_t>(MenuID::LookImages)] = "L";
    mnemonic_[static_cast<std::size_t>(MenuID::EditLayers)] = "Y";
    mnemonic_[static_cast<std::size_t>(MenuID::AddLayer)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::RemoveLayer)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::ChangeVH)] = "H";
    mnemonic_[static_cast<std::size_t>(MenuID::SelectConnectedResource)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::Find)] = "F";
    mnemonic_[static_cast<std::size_t>(MenuID::FindID)] = "I";
    mnemonic_[static_cast<std::size_t>(MenuID::IncSearch)] = "N";
    mnemonic_[static_cast<std::size_t>(MenuID::CloseIncSearch)] = "X";
    mnemonic_[static_cast<std::size_t>(MenuID::EditProp)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::ShowProp)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::Refresh)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::Undo)] = "U";
    mnemonic_[static_cast<std::size_t>(MenuID::Redo)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::Cut)] = "T";
    mnemonic_[static_cast<std::size_t>(MenuID::Copy)] = "C";
    mnemonic_[static_cast<std::size_t>(MenuID::Paste)] = "P";
    mnemonic_[static_cast<std::size_t>(MenuID::Delete)] = "D";
    mnemonic_[static_cast<std::size_t>(MenuID::Cut1Content)] = "U";
    mnemonic_[static_cast<std::size_t>(MenuID::Copy1Content)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::Delete1Content)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::PasteInsert)] = "I";
    mnemonic_[static_cast<std::size_t>(MenuID::Clone)] = "L";
    mnemonic_[static_cast<std::size_t>(MenuID::SelectAll)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyAll)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::ToXMLText)] = "X";
    mnemonic_[static_cast<std::size_t>(MenuID::AddItem)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::DelItem)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::TableView)] = "D";
    mnemonic_[static_cast<std::size_t>(MenuID::VarView)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::CardView)] = "W";
    mnemonic_[static_cast<std::size_t>(MenuID::CastView)] = "C";
    mnemonic_[static_cast<std::size_t>(MenuID::SkillView)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::ItemView)] = "I";
    mnemonic_[static_cast<std::size_t>(MenuID::BeastView)] = "B";
    mnemonic_[static_cast<std::size_t>(MenuID::InfoView)] = "N";
    mnemonic_[static_cast<std::size_t>(MenuID::FileView)] = "F";
    mnemonic_[static_cast<std::size_t>(MenuID::CouponView)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::GossipView)] = "G";
    mnemonic_[static_cast<std::size_t>(MenuID::CompleteStampView)] = "M";
    mnemonic_[static_cast<std::size_t>(MenuID::KeyCodeView)] = "K";
    mnemonic_[static_cast<std::size_t>(MenuID::CellNameView)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::CardGroupView)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::ExecEngine)] = "G";
    mnemonic_[static_cast<std::size_t>(MenuID::ExecEngineAuto)] = "G";
    mnemonic_[static_cast<std::size_t>(MenuID::ExecEngineMain)] = "P";
    mnemonic_[static_cast<std::size_t>(MenuID::ExecEngineWithParty)] = "P";
    mnemonic_[static_cast<std::size_t>(MenuID::ExecEngineWithLastParty)] = "L";
    mnemonic_[static_cast<std::size_t>(MenuID::DeleteNotExistsParties)] = "D";
    mnemonic_[static_cast<std::size_t>(MenuID::OuterTools)] = "T";
    mnemonic_[static_cast<std::size_t>(MenuID::Settings)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::VersionInfo)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::LockToolBar)] = "L";
    mnemonic_[static_cast<std::size_t>(MenuID::ResetToolBar)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyAsText)] = "C";
    mnemonic_[static_cast<std::size_t>(MenuID::OpenAtView)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::EventToPackage)] = "G";
    mnemonic_[static_cast<std::size_t>(MenuID::StartToPackage)] = "G";
    mnemonic_[static_cast<std::size_t>(MenuID::WrapTree)] = "W";
    mnemonic_[static_cast<std::size_t>(MenuID::CreateContent)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::ConvertContent)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::CGroupTerminal)] = "T";
    mnemonic_[static_cast<std::size_t>(MenuID::CGroupStandard)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::CGroupData)] = "D";
    mnemonic_[static_cast<std::size_t>(MenuID::CGroupUtility)] = "U";
    mnemonic_[static_cast<std::size_t>(MenuID::CGroupBranch)] = "B";
    mnemonic_[static_cast<std::size_t>(MenuID::CGroupGet)] = "G";
    mnemonic_[static_cast<std::size_t>(MenuID::CGroupLost)] = "L";
    mnemonic_[static_cast<std::size_t>(MenuID::CGroupVisual)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::CGroupVariant)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::EditSummary)] = "M";
    mnemonic_[static_cast<std::size_t>(MenuID::NewAreaDir)] = "D";
    mnemonic_[static_cast<std::size_t>(MenuID::NewArea)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::NewBattle)] = "B";
    mnemonic_[static_cast<std::size_t>(MenuID::NewPackage)] = "K";
    mnemonic_[static_cast<std::size_t>(MenuID::ReNumberingAll)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::ReNumbering)] = "B";
    mnemonic_[static_cast<std::size_t>(MenuID::EditScene)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::EditSceneDup)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::EditEvent)] = "N";
    mnemonic_[static_cast<std::size_t>(MenuID::EditEventDup)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::SetStartArea)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::NewFlagDir)] = "N";
    mnemonic_[static_cast<std::size_t>(MenuID::NewFlag)] = "F";
    mnemonic_[static_cast<std::size_t>(MenuID::NewStep)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::NewVariant)] = "C";
    mnemonic_[static_cast<std::size_t>(MenuID::CreateStepValues)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::PutSPChar)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::PutFlagValue)] = "F";
    mnemonic_[static_cast<std::size_t>(MenuID::PutStepValue)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::PutVariantValue)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::PutColor)] = "C";
    mnemonic_[static_cast<std::size_t>(MenuID::PutSkinSPChar)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::PutImageFont)] = "I";
    mnemonic_[static_cast<std::size_t>(MenuID::CreateVariableEventTree)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::InitVariablesTree)] = "I";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyVariablePath)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::Up)] = "K";
    mnemonic_[static_cast<std::size_t>(MenuID::Down)] = "J";
    mnemonic_[static_cast<std::size_t>(MenuID::ConvertCouponType)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::ConvertCouponTypeNormal)] = "N";
    mnemonic_[static_cast<std::size_t>(MenuID::ConvertCouponTypeHide)] = "H";
    mnemonic_[static_cast<std::size_t>(MenuID::ConvertCouponTypeSystem)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::ConvertCouponTypeDur)] = "D";
    mnemonic_[static_cast<std::size_t>(MenuID::ConvertCouponTypeDurBattle)] = "B";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyTypeConvertedCoupon)] = "N";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyTypeConvertedCouponNormal)] = "N";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyTypeConvertedCouponHide)] = "H";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyTypeConvertedCouponSystem)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyTypeConvertedCouponDur)] = "D";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyTypeConvertedCouponDurBattle)] = "B";
    mnemonic_[static_cast<std::size_t>(MenuID::AddInitialCoupons)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::ReverseSignOfValues)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::Reverse)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::SwapToParent)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::SwapToChild)] = "W";
    mnemonic_[static_cast<std::size_t>(MenuID::OverDialog)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::UnderDialog)] = "U";
    mnemonic_[static_cast<std::size_t>(MenuID::CreateDialog)] = "N";
    mnemonic_[static_cast<std::size_t>(MenuID::DeleteDialog)] = "D";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyToAllDialogs)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyToUpperDialogs)] = "U";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyToLowerDialogs)] = "L";
    mnemonic_[static_cast<std::size_t>(MenuID::ShowParty)] = "P";
    mnemonic_[static_cast<std::size_t>(MenuID::ShowMsg)] = "M";
    mnemonic_[static_cast<std::size_t>(MenuID::ShowRefCards)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::FixedCards)] = "F";
    mnemonic_[static_cast<std::size_t>(MenuID::FixedCells)] = "I";
    mnemonic_[static_cast<std::size_t>(MenuID::FixedBackground)] = "B";
    mnemonic_[static_cast<std::size_t>(MenuID::ShowGrid)] = "G";
    mnemonic_[static_cast<std::size_t>(MenuID::ShowEnemyCardProp)] = "L";
    mnemonic_[static_cast<std::size_t>(MenuID::ShowCard)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::ShowBack)] = "I";
    mnemonic_[static_cast<std::size_t>(MenuID::NewMenuCard)] = "C";
    mnemonic_[static_cast<std::size_t>(MenuID::NewEnemyCard)] = "C";
    mnemonic_[static_cast<std::size_t>(MenuID::NewBack)] = "B";
    mnemonic_[static_cast<std::size_t>(MenuID::NewTextCell)] = "T";
    mnemonic_[static_cast<std::size_t>(MenuID::NewColorCell)] = "L";
    mnemonic_[static_cast<std::size_t>(MenuID::NewPCCell)] = "P";
    mnemonic_[static_cast<std::size_t>(MenuID::AutoArrange)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::ManualArrange)] = "U";
    mnemonic_[static_cast<std::size_t>(MenuID::PossibleToRunAway)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::SortWithPosition)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::Mask)] = "M";
    mnemonic_[static_cast<std::size_t>(MenuID::Escape)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::ChangePos)] = "P";
    mnemonic_[static_cast<std::size_t>(MenuID::PosTop)] = "K";
    mnemonic_[static_cast<std::size_t>(MenuID::PosBottom)] = "J";
    mnemonic_[static_cast<std::size_t>(MenuID::PosLeft)] = "H";
    mnemonic_[static_cast<std::size_t>(MenuID::PosRight)] = "L";
    mnemonic_[static_cast<std::size_t>(MenuID::PosEven)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::NearTop)] = "W";
    mnemonic_[static_cast<std::size_t>(MenuID::NearBottom)] = "Z";
    mnemonic_[static_cast<std::size_t>(MenuID::NearLeft)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::NearRight)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::NearCenterH)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::NearCenterV)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::NearCenter)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::ScaleMin)] = "N";
    mnemonic_[static_cast<std::size_t>(MenuID::ScaleMiddle)] = "I";
    mnemonic_[static_cast<std::size_t>(MenuID::ScaleMax)] = "G";
    mnemonic_[static_cast<std::size_t>(MenuID::ScaleBig)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::ScaleSmall)] = "M";
    mnemonic_[static_cast<std::size_t>(MenuID::ExpandBack)] = "X";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyColor1ToColor2)] = "1";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyColor2ToColor1)] = "2";
    mnemonic_[static_cast<std::size_t>(MenuID::ExchangeColors)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::StopBGM)] = "P";
    mnemonic_[static_cast<std::size_t>(MenuID::PlayBGM)] = "P";
    mnemonic_[static_cast<std::size_t>(MenuID::NewEvent)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::NewEventWithDialog)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::KeyCodeTiming)] = "K";
    mnemonic_[static_cast<std::size_t>(MenuID::KeyCodeTimingUse)] = "U";
    mnemonic_[static_cast<std::size_t>(MenuID::KeyCodeTimingSuccess)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::KeyCodeTimingFailure)] = "F";
    mnemonic_[static_cast<std::size_t>(MenuID::KeyCodeTimingHasNot)] = "H";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyTimingConvertedKeyCode)] = "N";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyTimingConvertedKeyCodeUse)] = "U";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyTimingConvertedKeyCodeSuccess)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyTimingConvertedKeyCodeFailure)] = "F";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyTimingConvertedKeyCodeHasNot)] = "H";
    mnemonic_[static_cast<std::size_t>(MenuID::AddKeyCodesByFeatures)] = "F";
    mnemonic_[static_cast<std::size_t>(MenuID::KeyCodeCond)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::KeyCodeCondOr)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::KeyCodeCondAnd)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::AddRangeOfRound)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::OpenAtTableView)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::OpenAtVarView)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::OpenAtCardView)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::OpenAtFileView)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::OpenAtEventView)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::Comment)] = "M";
    mnemonic_[static_cast<std::size_t>(MenuID::ShowCardProp)] = "L";
    mnemonic_[static_cast<std::size_t>(MenuID::ShowCardImage)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::ShowCardDetail)] = "D";
    mnemonic_[static_cast<std::size_t>(MenuID::OpenImportSource)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::SelectImportSource)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::NewCast)] = "C";
    mnemonic_[static_cast<std::size_t>(MenuID::NewSkill)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::NewItem)] = "I";
    mnemonic_[static_cast<std::size_t>(MenuID::NewBeast)] = "B";
    mnemonic_[static_cast<std::size_t>(MenuID::NewInfo)] = "F";
    mnemonic_[static_cast<std::size_t>(MenuID::Import)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::OpenHand)] = "H";
    mnemonic_[static_cast<std::size_t>(MenuID::AddHand)] = "A";
    mnemonic_[static_cast<std::size_t>(MenuID::RemoveRef)] = "I";
    mnemonic_[static_cast<std::size_t>(MenuID::EditEventAtTimeOfUsing)] = "N";
    mnemonic_[static_cast<std::size_t>(MenuID::Hold)] = "H";
    mnemonic_[static_cast<std::size_t>(MenuID::PlaySE)] = "P";
    mnemonic_[static_cast<std::size_t>(MenuID::StopSE)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::NewDir)] = "I";
    mnemonic_[static_cast<std::size_t>(MenuID::CopyFilePath)] = "M";
    mnemonic_[static_cast<std::size_t>(MenuID::CreateArchive)] = "V";
    mnemonic_[static_cast<std::size_t>(MenuID::PutQuick)] = "Q";
    mnemonic_[static_cast<std::size_t>(MenuID::PutSelect)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::PutContinue)] = "C";
    mnemonic_[static_cast<std::size_t>(MenuID::ToScript)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::ToScriptAll)] = "Y";
    mnemonic_[static_cast<std::size_t>(MenuID::ToScript1Content)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::EvTemplates)] = "";
    mnemonic_[static_cast<std::size_t>(MenuID::EvTemplatesOfScenario)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::EditSelection)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::Expand)] = "X";
    mnemonic_[static_cast<std::size_t>(MenuID::Collapse)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::SelectCurrentEvent)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::ResetValues)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::ResetValuesAll)] = "E";
    mnemonic_[static_cast<std::size_t>(MenuID::CustomizeToolBar)] = "Z";
    mnemonic_[static_cast<std::size_t>(MenuID::AddTool)] = "O";
    mnemonic_[static_cast<std::size_t>(MenuID::AddToolBar)] = "B";
    mnemonic_[static_cast<std::size_t>(MenuID::AddToolGroup)] = "G";
    mnemonic_[static_cast<std::size_t>(MenuID::ResetToolBarSettings)] = "R";
    mnemonic_[static_cast<std::size_t>(MenuID::DeleteNotExistsHistory)] = "N";
    mnemonic_[static_cast<std::size_t>(MenuID::SelectIcon)] = "S";
    mnemonic_[static_cast<std::size_t>(MenuID::SelectPresetIcon)] = "P";
    mnemonic_[static_cast<std::size_t>(MenuID::DeleteIcon)] = "D";
    hotkey_[static_cast<std::size_t>(MenuID::None)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::File)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Edit)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::View)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Tool)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Table)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Variable)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Help)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Card)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CardsAndBacks)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::DelNotUsedFile)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CreateSubWindow)] = "Ctrl+Shift+N";
    hotkey_[static_cast<std::size_t>(MenuID::LeftPane)] = "Ctrl+Page_Up";
    hotkey_[static_cast<std::size_t>(MenuID::RightPane)] = "Ctrl+Page_Down";
    hotkey_[static_cast<std::size_t>(MenuID::ClosePane)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ClosePaneExcept)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ClosePaneLeft)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ClosePaneRight)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ClosePaneAll)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::New)] = "Ctrl+N";
    hotkey_[static_cast<std::size_t>(MenuID::Open)] = "Ctrl+O";
    hotkey_[static_cast<std::size_t>(MenuID::NewAtNewWindow)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::OpenAtNewWindow)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Close)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CloseWin)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Save)] = "Ctrl+S";
    hotkey_[static_cast<std::size_t>(MenuID::SaveAs)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Reload)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::EditScenarioHistory)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::EditImportHistory)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::EditExecutedPartyHistory)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PutParty)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::OpenDir)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::OpenBackupDir)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::OpenPlace)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::SaveImage)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::IncludeImage)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::LookImages)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::EditLayers)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::AddLayer)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::RemoveLayer)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ChangeVH)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::SelectConnectedResource)] = "Ctrl+R";
    hotkey_[static_cast<std::size_t>(MenuID::Find)] = "Ctrl+F";
    hotkey_[static_cast<std::size_t>(MenuID::FindID)] = "Ctrl+Shift+F";
    hotkey_[static_cast<std::size_t>(MenuID::IncSearch)] = "Ctrl+I";
    hotkey_[static_cast<std::size_t>(MenuID::CloseIncSearch)] = "Escape";
    hotkey_[static_cast<std::size_t>(MenuID::EditProp)] = "Enter";
    hotkey_[static_cast<std::size_t>(MenuID::ShowProp)] = "Enter";
    hotkey_[static_cast<std::size_t>(MenuID::Refresh)] = "F5";
    hotkey_[static_cast<std::size_t>(MenuID::Undo)] = "Ctrl+Z";
    hotkey_[static_cast<std::size_t>(MenuID::Redo)] = "Ctrl+Y";
    hotkey_[static_cast<std::size_t>(MenuID::Cut)] = "Ctrl+X";
    hotkey_[static_cast<std::size_t>(MenuID::Copy)] = "Ctrl+C";
    hotkey_[static_cast<std::size_t>(MenuID::Paste)] = "Ctrl+V";
    hotkey_[static_cast<std::size_t>(MenuID::Delete)] = "Delete";
    hotkey_[static_cast<std::size_t>(MenuID::Cut1Content)] = "Ctrl+Shift+X";
    hotkey_[static_cast<std::size_t>(MenuID::Copy1Content)] = "Ctrl+Shift+C";
    hotkey_[static_cast<std::size_t>(MenuID::Delete1Content)] = "Shift+Delete";
    hotkey_[static_cast<std::size_t>(MenuID::PasteInsert)] = "Ctrl+Shift+V";
    hotkey_[static_cast<std::size_t>(MenuID::Clone)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::SelectAll)] = "Ctrl+A";
    hotkey_[static_cast<std::size_t>(MenuID::CopyAll)] = "Ctrl+Shift+C";
    hotkey_[static_cast<std::size_t>(MenuID::ToXMLText)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::AddItem)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::DelItem)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::TableView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::VarView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CardView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CastView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::SkillView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ItemView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::BeastView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::InfoView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::FileView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CouponView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::GossipView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CompleteStampView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::KeyCodeView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CellNameView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CardGroupView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ExecEngine)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ExecEngineAuto)] = "F9";
    hotkey_[static_cast<std::size_t>(MenuID::ExecEngineMain)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ExecEngineWithParty)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ExecEngineWithLastParty)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::DeleteNotExistsParties)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::OuterTools)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Settings)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::VersionInfo)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::LockToolBar)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ResetToolBar)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyAsText)] = "Ctrl+C";
    hotkey_[static_cast<std::size_t>(MenuID::OpenAtView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::EventToPackage)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::StartToPackage)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::WrapTree)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CreateContent)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ConvertContent)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CGroupTerminal)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CGroupStandard)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CGroupData)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CGroupUtility)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CGroupBranch)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CGroupGet)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CGroupLost)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CGroupVisual)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CGroupVariant)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::EditSummary)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewAreaDir)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewArea)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewBattle)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewPackage)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ReNumberingAll)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ReNumbering)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::EditScene)] = "F3";
    hotkey_[static_cast<std::size_t>(MenuID::EditSceneDup)] = "Ctrl+F3";
    hotkey_[static_cast<std::size_t>(MenuID::EditEvent)] = "F4";
    hotkey_[static_cast<std::size_t>(MenuID::EditEventDup)] = "Ctrl+F4";
    hotkey_[static_cast<std::size_t>(MenuID::SetStartArea)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewFlagDir)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewFlag)] = "Ctrl+L";
    hotkey_[static_cast<std::size_t>(MenuID::NewStep)] = "Ctrl+P";
    hotkey_[static_cast<std::size_t>(MenuID::NewVariant)] = "Ctrl+Shift+O";
    hotkey_[static_cast<std::size_t>(MenuID::CreateStepValues)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PutSPChar)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PutFlagValue)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PutStepValue)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PutVariantValue)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PutColor)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PutSkinSPChar)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PutImageFont)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CreateVariableEventTree)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::InitVariablesTree)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyVariablePath)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Up)] = "Ctrl+Arrow_Up";
    hotkey_[static_cast<std::size_t>(MenuID::Down)] = "Ctrl+Arrow_Down";
    hotkey_[static_cast<std::size_t>(MenuID::ConvertCouponType)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ConvertCouponTypeNormal)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ConvertCouponTypeHide)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ConvertCouponTypeSystem)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ConvertCouponTypeDur)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ConvertCouponTypeDurBattle)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyTypeConvertedCoupon)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyTypeConvertedCouponNormal)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyTypeConvertedCouponHide)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyTypeConvertedCouponSystem)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyTypeConvertedCouponDur)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyTypeConvertedCouponDurBattle)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::AddInitialCoupons)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ReverseSignOfValues)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Reverse)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::SwapToParent)] = "Ctrl+Shift+Arrow_Up";
    hotkey_[static_cast<std::size_t>(MenuID::SwapToChild)] = "Ctrl+Shift+Arrow_Down";
    hotkey_[static_cast<std::size_t>(MenuID::OverDialog)] = "Ctrl+Shift+Arrow_Up";
    hotkey_[static_cast<std::size_t>(MenuID::UnderDialog)] = "Ctrl+Shift+Arrow_Down";
    hotkey_[static_cast<std::size_t>(MenuID::CreateDialog)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::DeleteDialog)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyToAllDialogs)] = "Ctrl+Shift+A";
    hotkey_[static_cast<std::size_t>(MenuID::CopyToUpperDialogs)] = "Ctrl+Shift+U";
    hotkey_[static_cast<std::size_t>(MenuID::CopyToLowerDialogs)] = "Ctrl+Shift+L";
    hotkey_[static_cast<std::size_t>(MenuID::ShowParty)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ShowMsg)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ShowRefCards)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::FixedCards)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::FixedCells)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::FixedBackground)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ShowGrid)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ShowEnemyCardProp)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ShowCard)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ShowBack)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewMenuCard)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewEnemyCard)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewBack)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewTextCell)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewColorCell)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewPCCell)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::AutoArrange)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ManualArrange)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PossibleToRunAway)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::SortWithPosition)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Mask)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Escape)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ChangePos)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PosTop)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PosBottom)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PosLeft)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PosRight)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PosEven)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NearTop)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NearBottom)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NearLeft)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NearRight)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NearCenterH)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NearCenterV)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NearCenter)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ScaleMin)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ScaleMiddle)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ScaleMax)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ScaleBig)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ScaleSmall)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ExpandBack)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyColor1ToColor2)] = "Ctrl+Shift+1";
    hotkey_[static_cast<std::size_t>(MenuID::CopyColor2ToColor1)] = "Ctrl+Shift+2";
    hotkey_[static_cast<std::size_t>(MenuID::ExchangeColors)] = "Ctrl+Shift+E";
    hotkey_[static_cast<std::size_t>(MenuID::StopBGM)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PlayBGM)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewEvent)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewEventWithDialog)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::KeyCodeTiming)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::KeyCodeTimingUse)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::KeyCodeTimingSuccess)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::KeyCodeTimingFailure)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::KeyCodeTimingHasNot)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyTimingConvertedKeyCode)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyTimingConvertedKeyCodeUse)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyTimingConvertedKeyCodeSuccess)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyTimingConvertedKeyCodeFailure)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyTimingConvertedKeyCodeHasNot)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::AddKeyCodesByFeatures)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::KeyCodeCond)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::KeyCodeCondOr)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::KeyCodeCondAnd)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::AddRangeOfRound)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::OpenAtTableView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::OpenAtVarView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::OpenAtCardView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::OpenAtFileView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::OpenAtEventView)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Comment)] = "Ctrl+M";
    hotkey_[static_cast<std::size_t>(MenuID::ShowCardProp)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ShowCardImage)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ShowCardDetail)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::OpenImportSource)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::SelectImportSource)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewCast)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewSkill)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewItem)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewBeast)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewInfo)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Import)] = "Ctrl+Enter";
    hotkey_[static_cast<std::size_t>(MenuID::OpenHand)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::AddHand)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::RemoveRef)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::EditEventAtTimeOfUsing)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::Hold)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PlaySE)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::StopSE)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::NewDir)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CopyFilePath)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CreateArchive)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PutQuick)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PutSelect)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::PutContinue)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ToScript)] = "Ctrl+G";
    hotkey_[static_cast<std::size_t>(MenuID::ToScriptAll)] = "Ctrl+B";
    hotkey_[static_cast<std::size_t>(MenuID::ToScript1Content)] = "Ctrl+Shift+G";
    hotkey_[static_cast<std::size_t>(MenuID::EvTemplates)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::EvTemplatesOfScenario)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::EditSelection)] = "F2";
    hotkey_[static_cast<std::size_t>(MenuID::Expand)] = "Ctrl+Arrow_Right";
    hotkey_[static_cast<std::size_t>(MenuID::Collapse)] = "Ctrl+Arrow_Left";
    hotkey_[static_cast<std::size_t>(MenuID::SelectCurrentEvent)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ResetValues)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ResetValuesAll)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::CustomizeToolBar)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::AddTool)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::AddToolBar)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::AddToolGroup)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::ResetToolBarSettings)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::DeleteNotExistsHistory)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::SelectIcon)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::SelectPresetIcon)] = "";
    hotkey_[static_cast<std::size_t>(MenuID::DeleteIcon)] = "";
    mnemonic_init_ = mnemonic_;
    hotkey_init_ = hotkey_;
}

String MenuProps::mnemonic(MenuID id) const {
    ::cwxeditor::cwx::perf::TraceScope trace_menu_587(__FILE__, __LINE__);
    return mnemonic_[index(id)];
}

void MenuProps::mnemonic(MenuID id, String key) {
    ::cwxeditor::cwx::perf::TraceScope trace_menu_591(__FILE__, __LINE__);
    mnemonic_[index(id)] = std::move(key);
}

String MenuProps::hotkey(MenuID id) const {
    ::cwxeditor::cwx::perf::TraceScope trace_menu_596(__FILE__, __LINE__);
    return hotkey_[index(id)];
}

void MenuProps::hotkey(MenuID id, String key) {
    ::cwxeditor::cwx::perf::TraceScope trace_menu_600(__FILE__, __LINE__);
    hotkey_[index(id)] = std::move(key);
}

String MenuProps::buildTool(const CProps& prop, MenuID id) const {
    return buildTool(prop, id, mnemonic(id), hotkey(id));
}

String MenuProps::buildTool(const CProps& prop, MenuID id,
                            String mnemonic, String hotkey) {
    return buildTool(prop.requiredMsgs().menuText(id), std::move(mnemonic),
                     std::move(hotkey), isPMenu(id));
}

String MenuProps::buildTool(String text, String /*mnemonic*/, String hotkey,
                            bool hasSubmenu) {
    if (hasSubmenu) text += "...";
    if (!hotkey.empty()) {
        text += "\n";
        text += hotkey;
    }
    return text;
}

String MenuProps::buildMenu(const CProps& prop, MenuID id) const {
    return buildMenu(prop, id, mnemonic(id), hotkey(id));
}

String MenuProps::buildMenu(const CProps& prop, MenuID id,
                            String mnemonic, String hotkey) {
    return buildMenu(prop.requiredMsgs().menuText(id), std::move(mnemonic),
                     std::move(hotkey), isPMenu(id));
}

String MenuProps::buildMenu(String text, String mnemonic, String hotkey,
                            bool hasSubmenu) {
    replaceAll(text, "&", "&&");
    replaceAll(text, "\n", "");
    if (!mnemonic.empty()) {
        const auto position = findIgnoreCase(text, mnemonic);
        if (position == String::npos) {
            text += "(&";
            text += mnemonic;
            text += ")";
        } else {
            text.insert(position, 1U, '&');
        }
    }
    if (hasSubmenu) text += "...";
    if (!hotkey.empty()) {
        text += "\t";
        text += hotkey;
    }
    return text;
}

String MenuProps::buildMenuSample(const CProps& prop, MenuID id) const {
    return buildMenuSample(prop, id, mnemonic(id), hotkey(id));
}

String MenuProps::buildMenuSample(const CProps& prop, MenuID id,
                                  String mnemonic, String hotkey) {
    String text = prop.requiredMsgs().menuText(id);
    if (id == MenuID::StopBGM) {
        text = ::cwxeditor::cwx::utils::tryFormat(
            text, prop.requiredMsgs().bgm.value);
    }
    return buildMenuSample(std::move(text), std::move(mnemonic),
                           std::move(hotkey), isPMenu(id));
}

String MenuProps::buildMenuSample(String text, String mnemonic, String hotkey,
                                  bool hasSubmenu) {
    if (!mnemonic.empty()) {
        const auto position = findIgnoreCase(text, mnemonic);
        if (position == String::npos) {
            text += "(&";
            text += mnemonic;
            text += ")";
        } else {
            text.insert(position, 1U, '&');
        }
    }
    if (hasSubmenu) text += "...";
    if (!hotkey.empty()) {
        text += " ";
        text += hotkey;
    }
    return text;
}

XNode MenuProps::toNode() const {
    ::cwxeditor::cwx::perf::TraceScope trace_menu_688(__FILE__, __LINE__);
    auto element = XNode::create(XML_NAME);
    toNodeImpl(element);
    return element;
}

void MenuProps::toNode(XNode& node) const {
    ::cwxeditor::cwx::perf::TraceScope trace_menu_695(__FILE__, __LINE__);
    auto element = node.newElement(XML_NAME);
    toNodeImpl(element);
}

void MenuProps::toNodeImpl(XNode& element) const {
    ::cwxeditor::cwx::perf::TraceScope trace_menu_701(__FILE__, __LINE__);
    for (std::size_t raw = 0U; raw < MENU_COUNT; ++raw) {
        ::cwxeditor::cwx::perf::TraceScope trace_menu_702(__FILE__, __LINE__);
        const auto id = static_cast<MenuID>(raw);
        if (isNoKeyBindMenu(id)) continue;
        const auto& mnemonic = mnemonic_[raw];
        const auto& hotkey = hotkey_[raw];
        if (mnemonic == mnemonic_init_[raw] && hotkey == hotkey_init_[raw]) {
            continue;
        }

        { // Dの変更済みメニューの分岐に合わせて寿命を保持する。既存のcontinueは変更しない。
        ::cwxeditor::cwx::perf::TraceScope trace_menu_708(__FILE__, __LINE__);
        auto menuItem = element.newElement("menuItem");
        const String name = ::cwxeditor::cwx::enumutils::enumToString(id);
        menuItem.new_attr("name", std::string_view(name));
        if (mnemonic != mnemonic_init_[raw]) {
            ::cwxeditor::cwx::perf::TraceScope trace_menu_711(__FILE__, __LINE__);
            menuItem.new_attr("mnemonic", std::string_view(mnemonic));
        }
        if (hotkey != hotkey_init_[raw]) {
            ::cwxeditor::cwx::perf::TraceScope trace_menu_714(__FILE__, __LINE__);
            menuItem.new_attr("hotkey", std::string_view(hotkey));
        }
        }
    }
}

MenuProps MenuProps::fromNode(XNode& node) {
    ::cwxeditor::cwx::perf::TraceScope trace_menu_721(__FILE__, __LINE__);
    MenuProps result;
    const auto readMenu = [&result](const XNode& menu) {
        ::cwxeditor::cwx::perf::TraceScope trace_menu_723(__FILE__, __LINE__);
        for (const auto& item : menu.element_children()) {
            if (item.name() != "menuItem") continue;
            ::cwxeditor::cwx::perf::TraceScope trace_menu_724(__FILE__, __LINE__);
            const auto name = item.attribute("name");
            if (!name.has_value()) {
                throw ::cwxeditor::lib::XmlValueError(
                    "XML attribute is not found: name");
            }
            const auto id =
                ::cwxeditor::cwx::enumutils::stringToEnum<MenuID>(*name);
            if (isNoKeyBindMenu(id)) continue;
            if (const auto value = item.attribute("mnemonic"); value.has_value() && !value->empty()) {
                result.mnemonic_[index(id)] = *value;
            }
            if (const auto value = item.attribute("hotkey"); value.has_value() && !value->empty()) {
                result.hotkey_[index(id)] = *value;
            }
        }
    };

    for (const auto& child : node.element_children()) {
        if (child.name() == XML_NAME) readMenu(child);
    }
    return result;
}

bool isPMenu(MenuID id) {
    ::cwxeditor::cwx::perf::TraceScope trace_menu_741(__FILE__, __LINE__);
    switch (id) {
    case MenuID::New:
    case MenuID::Open:
    case MenuID::NewAtNewWindow:
    case MenuID::OpenAtNewWindow:
    case MenuID::SaveAs:
    case MenuID::EditScenarioHistory:
    case MenuID::EditImportHistory:
    case MenuID::EditExecutedPartyHistory:
    case MenuID::SaveImage:
    case MenuID::Find:
    case MenuID::FindID:
    case MenuID::Settings:
    case MenuID::VersionInfo:
    case MenuID::EditSummary:
    case MenuID::ReNumberingAll:
    case MenuID::ReNumbering:
    case MenuID::EditProp:
    case MenuID::ShowProp:
    case MenuID::NewFlagDir:
    case MenuID::NewFlag:
    case MenuID::NewStep:
    case MenuID::NewVariant:
    case MenuID::NewMenuCard:
    case MenuID::NewEnemyCard:
    case MenuID::NewBack:
    case MenuID::NewTextCell:
    case MenuID::NewColorCell:
    case MenuID::NewPCCell:
    case MenuID::OpenImportSource:
    case MenuID::SelectImportSource:
    case MenuID::NewEventWithDialog:
    case MenuID::NewCast:
    case MenuID::NewSkill:
    case MenuID::NewItem:
    case MenuID::NewBeast:
    case MenuID::NewInfo:
    case MenuID::CreateArchive:
    case MenuID::EvTemplatesOfScenario:
    case MenuID::CustomizeToolBar:
    case MenuID::AddRangeOfRound:
    case MenuID::AddKeyCodesByFeatures:
    case MenuID::SelectIcon:
        return true;
    default:
        return false;
    }
}

bool isNoKeyBindMenu(MenuID id) {
    ::cwxeditor::cwx::perf::TraceScope trace_menu_792(__FILE__, __LINE__);
    return id == MenuID::None;
}

MenuID cTypeGroupToMenuID(CTypeGroup group) {
    ::cwxeditor::cwx::perf::TraceScope trace_menu_797(__FILE__, __LINE__);
    switch (group) {
    case CTypeGroup::Terminal: return MenuID::CGroupTerminal;
    case CTypeGroup::Standard: return MenuID::CGroupStandard;
    case CTypeGroup::Data: return MenuID::CGroupData;
    case CTypeGroup::Utility: return MenuID::CGroupUtility;
    case CTypeGroup::Branch: return MenuID::CGroupBranch;
    case CTypeGroup::Get: return MenuID::CGroupGet;
    case CTypeGroup::Lost: return MenuID::CGroupLost;
    case CTypeGroup::Visual: return MenuID::CGroupVisual;
    case CTypeGroup::Variant: return MenuID::CGroupVariant;
    default:
        throw std::out_of_range("CTypeGroup is outside the known range");
    }
}

bool isMainToolBarMenu(MenuID id) {
    ::cwxeditor::cwx::perf::TraceScope trace_menu_819(__FILE__, __LINE__);
    switch (id) {
    case MenuID::DelNotUsedFile:
    case MenuID::New:
    case MenuID::Open:
    case MenuID::NewAtNewWindow:
    case MenuID::OpenAtNewWindow:
    case MenuID::Close:
    case MenuID::Save:
    case MenuID::SaveAs:
    case MenuID::Reload:
    case MenuID::OpenDir:
    case MenuID::OpenBackupDir:
    case MenuID::ChangeVH:
    case MenuID::SelectConnectedResource:
    case MenuID::Find:
    case MenuID::FindID:
    case MenuID::IncSearch:
    case MenuID::EditProp:
    case MenuID::ShowProp:
    case MenuID::Refresh:
    case MenuID::Undo:
    case MenuID::Redo:
    case MenuID::Cut:
    case MenuID::Copy:
    case MenuID::Paste:
    case MenuID::Delete:
    case MenuID::Cut1Content:
    case MenuID::Copy1Content:
    case MenuID::Delete1Content:
    case MenuID::PasteInsert:
    case MenuID::Clone:
    case MenuID::SelectAll:
    case MenuID::CopyAll:
    case MenuID::ToXMLText:
    case MenuID::AddItem:
    case MenuID::DelItem:
    case MenuID::TableView:
    case MenuID::VarView:
    case MenuID::CastView:
    case MenuID::SkillView:
    case MenuID::ItemView:
    case MenuID::BeastView:
    case MenuID::InfoView:
    case MenuID::FileView:
    case MenuID::CouponView:
    case MenuID::GossipView:
    case MenuID::CompleteStampView:
    case MenuID::KeyCodeView:
    case MenuID::CellNameView:
    case MenuID::CardGroupView:
    case MenuID::ExecEngine:
    case MenuID::ExecEngineWithParty:
    case MenuID::OuterTools:
    case MenuID::Settings:
    case MenuID::VersionInfo:
    case MenuID::EventToPackage:
    case MenuID::StartToPackage:
    case MenuID::WrapTree:
    case MenuID::EditSummary:
    case MenuID::NewAreaDir:
    case MenuID::NewArea:
    case MenuID::NewBattle:
    case MenuID::NewPackage:
    case MenuID::ReNumberingAll:
    case MenuID::ReNumbering:
    case MenuID::EditScene:
    case MenuID::EditSceneDup:
    case MenuID::EditEvent:
    case MenuID::EditEventDup:
    case MenuID::SetStartArea:
    case MenuID::NewFlagDir:
    case MenuID::NewFlag:
    case MenuID::NewStep:
    case MenuID::NewVariant:
    case MenuID::CopyVariablePath:
    case MenuID::Up:
    case MenuID::Down:
    case MenuID::ConvertCouponType:
    case MenuID::ConvertCouponTypeNormal:
    case MenuID::ConvertCouponTypeHide:
    case MenuID::ConvertCouponTypeSystem:
    case MenuID::ConvertCouponTypeDur:
    case MenuID::ConvertCouponTypeDurBattle:
    case MenuID::CopyTypeConvertedCoupon:
    case MenuID::CopyTypeConvertedCouponNormal:
    case MenuID::CopyTypeConvertedCouponHide:
    case MenuID::CopyTypeConvertedCouponDur:
    case MenuID::CopyTypeConvertedCouponDurBattle:
    case MenuID::CopyTypeConvertedCouponSystem:
    case MenuID::AddInitialCoupons:
    case MenuID::ReverseSignOfValues:
    case MenuID::Reverse:
    case MenuID::SwapToParent:
    case MenuID::SwapToChild:
    case MenuID::Comment:
    case MenuID::ShowCardProp:
    case MenuID::ShowCardImage:
    case MenuID::ShowCardDetail:
    case MenuID::OpenImportSource:
    case MenuID::SelectImportSource:
    case MenuID::NewCast:
    case MenuID::NewSkill:
    case MenuID::NewItem:
    case MenuID::NewBeast:
    case MenuID::NewInfo:
    case MenuID::Import:
    case MenuID::OpenHand:
    case MenuID::RemoveRef:
    case MenuID::EditEventAtTimeOfUsing:
    case MenuID::NewDir:
    case MenuID::CopyFilePath:
    case MenuID::CreateArchive:
    case MenuID::ToScript:
    case MenuID::ToScriptAll:
    case MenuID::ToScript1Content:
    case MenuID::EditScenarioHistory:
    case MenuID::EditExecutedPartyHistory:
    case MenuID::EditImportHistory:
    case MenuID::DeleteNotExistsHistory:
    case MenuID::DeleteNotExistsParties:
        return true;
    default:
        static_cast<void>(checkedMenuIndex(id));
        return false;
    }
}

bool isGlobalMenu(MenuID id) {
    switch (id) {
    case MenuID::DelNotUsedFile:
    case MenuID::CreateSubWindow:
    case MenuID::New:
    case MenuID::Open:
    case MenuID::NewAtNewWindow:
    case MenuID::OpenAtNewWindow:
    case MenuID::Close:
    case MenuID::Save:
    case MenuID::SaveAs:
    case MenuID::Reload:
    case MenuID::OpenDir:
    case MenuID::OpenBackupDir:
    case MenuID::Find:
    case MenuID::Refresh:
    case MenuID::ToXMLText:
    case MenuID::TableView:
    case MenuID::VarView:
    case MenuID::CardView:
    case MenuID::CastView:
    case MenuID::SkillView:
    case MenuID::ItemView:
    case MenuID::BeastView:
    case MenuID::InfoView:
    case MenuID::FileView:
    case MenuID::CouponView:
    case MenuID::GossipView:
    case MenuID::CompleteStampView:
    case MenuID::KeyCodeView:
    case MenuID::CellNameView:
    case MenuID::CardGroupView:
    case MenuID::ExecEngine:
    case MenuID::ExecEngineAuto:
    case MenuID::ExecEngineMain:
    case MenuID::ExecEngineWithParty:
    case MenuID::ExecEngineWithLastParty:
    case MenuID::DeleteNotExistsParties:
    case MenuID::OuterTools:
    case MenuID::Settings:
    case MenuID::VersionInfo:
    case MenuID::EditSummary:
    case MenuID::NewAreaDir:
    case MenuID::NewArea:
    case MenuID::NewBattle:
    case MenuID::NewPackage:
    case MenuID::ReNumberingAll:
    case MenuID::EditScene:
    case MenuID::EditEvent:
    case MenuID::EditEventDup:
    case MenuID::NewFlagDir:
    case MenuID::NewFlag:
    case MenuID::NewStep:
    case MenuID::NewVariant:
    case MenuID::ShowCardProp:
    case MenuID::ShowCardImage:
    case MenuID::ShowCardDetail:
    case MenuID::OpenImportSource:
    case MenuID::SelectImportSource:
    case MenuID::NewCast:
    case MenuID::NewSkill:
    case MenuID::NewItem:
    case MenuID::NewBeast:
    case MenuID::NewInfo:
    case MenuID::NewDir:
    case MenuID::CreateArchive:
    case MenuID::EvTemplatesOfScenario:
    case MenuID::EditScenarioHistory:
    case MenuID::EditImportHistory:
    case MenuID::EditExecutedPartyHistory:
    case MenuID::DeleteNotExistsHistory:
        return true;
    default:
        static_cast<void>(checkedMenuIndex(id));
        return false;
    }
}

bool isLocalMenu(MenuID id) {
    switch (id) {
    case MenuID::EditProp:
    case MenuID::ShowProp:
    case MenuID::Undo:
    case MenuID::Redo:
    case MenuID::Cut:
    case MenuID::Copy:
    case MenuID::Paste:
    case MenuID::Delete:
    case MenuID::Cut1Content:
    case MenuID::Copy1Content:
    case MenuID::Delete1Content:
    case MenuID::PasteInsert:
    case MenuID::Clone:
    case MenuID::SelectAll:
    case MenuID::CopyAll:
    case MenuID::AddItem:
    case MenuID::DelItem:
    case MenuID::CopyAsText:
    case MenuID::CreateContent:
    case MenuID::ConvertContent:
    case MenuID::Up:
    case MenuID::Down:
    case MenuID::AddInitialCoupons:
    case MenuID::ReverseSignOfValues:
    case MenuID::Reverse:
    case MenuID::SwapToParent:
    case MenuID::SwapToChild:
    case MenuID::OverDialog:
    case MenuID::UnderDialog:
    case MenuID::Comment:
    case MenuID::CopyFilePath:
    case MenuID::ToScript:
    case MenuID::ToScript1Content:
    case MenuID::EditSelection:
    case MenuID::Expand:
    case MenuID::Collapse:
    case MenuID::ResetValues:
    case MenuID::ResetValuesAll:
    case MenuID::PutParty:
    case MenuID::SelectIcon:
    case MenuID::SelectPresetIcon:
    case MenuID::DeleteIcon:
        return true;
    default:
        return false;
    }
}

}  // namespace cwxeditor::cwx::menu
