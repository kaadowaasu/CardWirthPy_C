#pragma once

// cwx/menu.dのC++20移植。

#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/settings.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

namespace cwxeditor::cwx::enumutils {

template <>
struct enum_traits<::cwxeditor::cwx::types::MenuID> {
    inline static constexpr auto values = [] {
        std::array<::cwxeditor::cwx::types::MenuID,
                   static_cast<std::size_t>(::cwxeditor::cwx::types::MenuID::DeleteIcon) + 1U>
            result{};
        for (std::size_t i = 0; i < result.size(); ++i) {
            result[i] = static_cast<::cwxeditor::cwx::types::MenuID>(i);
        }
        return result;
    }();

    inline static constexpr std::array<std::string_view,
                                       static_cast<std::size_t>(::cwxeditor::cwx::types::MenuID::DeleteIcon) + 1U>
        names{
        "None",
        "File",
        "Edit",
        "View",
        "Tool",
        "Table",
        "Variable",
        "Help",
        "Card",
        "CardsAndBacks",
        "DelNotUsedFile",
        "CreateSubWindow",
        "LeftPane",
        "RightPane",
        "ClosePane",
        "ClosePaneExcept",
        "ClosePaneLeft",
        "ClosePaneRight",
        "ClosePaneAll",
        "New",
        "Open",
        "NewAtNewWindow",
        "OpenAtNewWindow",
        "Close",
        "CloseWin",
        "Save",
        "SaveAs",
        "Reload",
        "EditScenarioHistory",
        "EditImportHistory",
        "EditExecutedPartyHistory",
        "PutParty",
        "OpenDir",
        "OpenBackupDir",
        "OpenPlace",
        "SaveImage",
        "IncludeImage",
        "LookImages",
        "EditLayers",
        "AddLayer",
        "RemoveLayer",
        "ChangeVH",
        "SelectConnectedResource",
        "Find",
        "FindID",
        "IncSearch",
        "CloseIncSearch",
        "EditProp",
        "ShowProp",
        "Refresh",
        "Undo",
        "Redo",
        "Cut",
        "Copy",
        "Paste",
        "Delete",
        "Cut1Content",
        "Copy1Content",
        "Delete1Content",
        "PasteInsert",
        "Clone",
        "SelectAll",
        "CopyAll",
        "ToXMLText",
        "AddItem",
        "DelItem",
        "TableView",
        "VarView",
        "CardView",
        "CastView",
        "SkillView",
        "ItemView",
        "BeastView",
        "InfoView",
        "FileView",
        "CouponView",
        "GossipView",
        "CompleteStampView",
        "KeyCodeView",
        "CellNameView",
        "CardGroupView",
        "ExecEngine",
        "ExecEngineAuto",
        "ExecEngineMain",
        "ExecEngineWithParty",
        "ExecEngineWithLastParty",
        "DeleteNotExistsParties",
        "OuterTools",
        "Settings",
        "VersionInfo",
        "LockToolBar",
        "ResetToolBar",
        "CopyAsText",
        "OpenAtView",
        "EventToPackage",
        "StartToPackage",
        "WrapTree",
        "CreateContent",
        "ConvertContent",
        "CGroupTerminal",
        "CGroupStandard",
        "CGroupData",
        "CGroupUtility",
        "CGroupBranch",
        "CGroupGet",
        "CGroupLost",
        "CGroupVisual",
        "CGroupVariant",
        "EditSummary",
        "NewAreaDir",
        "NewArea",
        "NewBattle",
        "NewPackage",
        "ReNumberingAll",
        "ReNumbering",
        "EditScene",
        "EditSceneDup",
        "EditEvent",
        "EditEventDup",
        "SetStartArea",
        "NewFlagDir",
        "NewFlag",
        "NewStep",
        "NewVariant",
        "CreateStepValues",
        "PutSPChar",
        "PutFlagValue",
        "PutStepValue",
        "PutVariantValue",
        "PutColor",
        "PutSkinSPChar",
        "PutImageFont",
        "CreateVariableEventTree",
        "InitVariablesTree",
        "CopyVariablePath",
        "Up",
        "Down",
        "ConvertCouponType",
        "ConvertCouponTypeNormal",
        "ConvertCouponTypeHide",
        "ConvertCouponTypeSystem",
        "ConvertCouponTypeDur",
        "ConvertCouponTypeDurBattle",
        "CopyTypeConvertedCoupon",
        "CopyTypeConvertedCouponNormal",
        "CopyTypeConvertedCouponHide",
        "CopyTypeConvertedCouponSystem",
        "CopyTypeConvertedCouponDur",
        "CopyTypeConvertedCouponDurBattle",
        "AddInitialCoupons",
        "ReverseSignOfValues",
        "Reverse",
        "SwapToParent",
        "SwapToChild",
        "OverDialog",
        "UnderDialog",
        "CreateDialog",
        "DeleteDialog",
        "CopyToAllDialogs",
        "CopyToUpperDialogs",
        "CopyToLowerDialogs",
        "ShowParty",
        "ShowMsg",
        "ShowRefCards",
        "FixedCards",
        "FixedCells",
        "FixedBackground",
        "ShowGrid",
        "ShowEnemyCardProp",
        "ShowCard",
        "ShowBack",
        "NewMenuCard",
        "NewEnemyCard",
        "NewBack",
        "NewTextCell",
        "NewColorCell",
        "NewPCCell",
        "AutoArrange",
        "ManualArrange",
        "PossibleToRunAway",
        "SortWithPosition",
        "Mask",
        "Escape",
        "ChangePos",
        "PosTop",
        "PosBottom",
        "PosLeft",
        "PosRight",
        "PosEven",
        "NearTop",
        "NearBottom",
        "NearLeft",
        "NearRight",
        "NearCenterH",
        "NearCenterV",
        "NearCenter",
        "ScaleMin",
        "ScaleMiddle",
        "ScaleMax",
        "ScaleBig",
        "ScaleSmall",
        "ExpandBack",
        "CopyColor1ToColor2",
        "CopyColor2ToColor1",
        "ExchangeColors",
        "StopBGM",
        "PlayBGM",
        "NewEvent",
        "NewEventWithDialog",
        "KeyCodeTiming",
        "KeyCodeTimingUse",
        "KeyCodeTimingSuccess",
        "KeyCodeTimingFailure",
        "KeyCodeTimingHasNot",
        "CopyTimingConvertedKeyCode",
        "CopyTimingConvertedKeyCodeUse",
        "CopyTimingConvertedKeyCodeSuccess",
        "CopyTimingConvertedKeyCodeFailure",
        "CopyTimingConvertedKeyCodeHasNot",
        "AddKeyCodesByFeatures",
        "KeyCodeCond",
        "KeyCodeCondOr",
        "KeyCodeCondAnd",
        "AddRangeOfRound",
        "OpenAtTableView",
        "OpenAtVarView",
        "OpenAtCardView",
        "OpenAtFileView",
        "OpenAtEventView",
        "Comment",
        "ShowCardProp",
        "ShowCardImage",
        "ShowCardDetail",
        "OpenImportSource",
        "SelectImportSource",
        "NewCast",
        "NewSkill",
        "NewItem",
        "NewBeast",
        "NewInfo",
        "Import",
        "OpenHand",
        "AddHand",
        "RemoveRef",
        "EditEventAtTimeOfUsing",
        "Hold",
        "PlaySE",
        "StopSE",
        "NewDir",
        "CopyFilePath",
        "CreateArchive",
        "PutQuick",
        "PutSelect",
        "PutContinue",
        "ToScript",
        "ToScriptAll",
        "ToScript1Content",
        "EvTemplates",
        "EvTemplatesOfScenario",
        "EditSelection",
        "Expand",
        "Collapse",
        "SelectCurrentEvent",
        "ResetValues",
        "ResetValuesAll",
        "CustomizeToolBar",
        "AddTool",
        "AddToolBar",
        "AddToolGroup",
        "ResetToolBarSettings",
        "DeleteNotExistsHistory",
        "SelectIcon",
        "SelectPresetIcon",
        "DeleteIcon"
    };
};

}  // namespace cwxeditor::cwx::enumutils

namespace cwxeditor::cwx::menu {

using ::cwxeditor::cwx::props::CProps;
using ::cwxeditor::cwx::types::CTypeGroup;
using ::cwxeditor::cwx::types::MenuID;
using ::cwxeditor::cwx::xml::XNode;
using ::cwxeditor::lib::String;
using ::cwxeditor::lib::StringView;

class MenuProps : public ::cwxeditor::cwx::settings::Properties {
public:
    inline static constexpr StringView XML_NAME = "menu";

    MenuProps();

    [[nodiscard]] String mnemonic(MenuID id) const;
    void mnemonic(MenuID id, String key);
    [[nodiscard]] String hotkey(MenuID id) const;
    void hotkey(MenuID id, String key);

    [[nodiscard]] String buildTool(const CProps& prop, MenuID id) const;
    static String buildTool(const CProps& prop, MenuID id,
                            String mnemonic, String hotkey);
    static String buildTool(String text, String mnemonic, String hotkey,
                            bool hasSubmenu);

    [[nodiscard]] String buildMenu(const CProps& prop, MenuID id) const;
    static String buildMenu(const CProps& prop, MenuID id,
                            String mnemonic, String hotkey);
    static String buildMenu(String text, String mnemonic, String hotkey,
                            bool hasSubmenu);

    [[nodiscard]] String buildMenuSample(const CProps& prop, MenuID id) const;
    static String buildMenuSample(const CProps& prop, MenuID id,
                                  String mnemonic, String hotkey);
    static String buildMenuSample(String text, String mnemonic, String hotkey,
                                  bool hasSubmenu);

    [[nodiscard]] XNode toNode() const;
    void toNode(XNode& node) const;

    static MenuProps fromNode(XNode& node);

private:
    static constexpr std::size_t MENU_COUNT =
        static_cast<std::size_t>(MenuID::DeleteIcon) + 1U;

    std::vector<String> mnemonic_;
    std::vector<String> mnemonic_init_;
    std::vector<String> hotkey_;
    std::vector<String> hotkey_init_;

    [[nodiscard]] static std::size_t index(MenuID id);
    void toNodeImpl(XNode& element) const;
};

class MenuData {
public:
    MenuID id{MenuID::None};
    std::function<String(String)> format{};
    std::function<bool()> enabled{};
};

[[nodiscard]] bool isPMenu(MenuID id);
[[nodiscard]] bool isNoKeyBindMenu(MenuID id);
[[nodiscard]] MenuID cTypeGroupToMenuID(CTypeGroup group);
[[nodiscard]] bool isMainToolBarMenu(MenuID id);
[[nodiscard]] bool isGlobalMenu(MenuID id);
[[nodiscard]] bool isLocalMenu(MenuID id);

}  // namespace cwxeditor::cwx::menu

