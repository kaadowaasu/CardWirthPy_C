#pragma once

#include <cstdint>
#include <stdexcept>

#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/lib/portable.hpp"

namespace cwxeditor::cwx::types {

inline constexpr std::int32_t LAYER_BACK_CELL = 0;
inline constexpr std::int32_t LAYER_MENU_CARD = 100;
inline constexpr std::int32_t LAYER_PLAYER_CARD = 200;
inline constexpr std::int32_t LAYER_FORE_CELL = 400;
inline constexpr std::int32_t LAYER_MESSAGE = 1000;

enum class ActionCardType : std::int32_t {
    Exchange = 0,
    Attack = 1,
    PowerfulAttack = 2,
    CriticalAttack = 3,
    Feint = 4,
    Defense = 5,
    Distance = 6,
    Confuse = -1,
    RunAway = 7,
};

enum class Mentality : std::int32_t {
    Normal,
    Sleep,
    Confuse,
    Overheat,
    Brave,
    Panic,
};

enum class EffectType : std::int32_t {
    Physic,
    Magic,
    MagicalPhysic,
    PhysicalMagic,
    None,
};

enum class Resist : std::int32_t {
    Avoid,
    Resist,
    Unfail,
};

enum class CardVisual : std::int32_t {
    None,
    Reverse,
    Horizontal,
    Vertical,
};

enum class Element : std::int32_t {
    All,
    Health,
    Mind,
    Miracle,
    Magic,
    Fire,
    Ice,
};

enum class DamageType : std::int32_t {
    LevelRatio,
    Normal,
    Max,
    Fixed,
};

enum class Mental : std::int32_t {
    Aggressive,
    Unaggressive,
    Cheerful,
    Uncheerful,
    Brave,
    Unbrave,
    Cautious,
    Uncautious,
    Trickish,
    Untrickish,
};

enum class Physical : std::int32_t {
    Dex,
    Agl,
    Int,
    Str,
    Vit,
    Min,
};

enum class Status : std::int32_t {
    Active,
    Inactive,
    Alive,
    Dead,
    Fine,
    Injured,
    HeavyInjured,
    Unconscious,
    Poison,
    Sleep,
    Bind,
    Paralyze,
    Confuse,
    Overheat,
    Brave,
    Panic,
    Silence,
    FaceUp,
    AntiMagic,
    UpAction,
    UpAvoid,
    UpResist,
    UpDefense,
    DownAction,
    DownAvoid,
    DownResist,
    DownDefense,
    None,
};

enum class Range : std::int32_t {
    Selected,
    Random,
    Party,
    Backpack,
    PartyAndBackpack,
    Field,
    CouponHolder,
    CardTarget,
    SelectedCard,
    Npc,
};

enum class CastRange : std::int32_t {
    Party = 0b0001,
    Enemy = 0b0010,
    Npc = 0b0100,
};

enum class Enhance : std::int32_t {
    Action,
    Avoid,
    Resist,
    Defense,
};

enum class Premium : std::int32_t {
    Normal,
    Rare,
    Premium,
};

enum class CardTarget : std::int32_t {
    None,
    User,
    Party,
    Enemy,
    Both,
};

enum class Talker : std::int32_t {
    Selected,
    Unselected,
    Random,
    Card,
    Valued,
};

enum class Transition : std::int32_t {
    Default,
    None,
    Blinds,
    PixelDissolve,
    Fade,
};

enum class EffectCardType : std::int32_t {
    All,
    Skill,
    Item,
    Beast,
    Hand,
};

enum class Comparison4 : std::int32_t {
    Eq,
    Ne,
    Lt,
    Gt,
};

enum class Comparison3 : std::int32_t {
    Eq,
    Lt,
    Gt,
};

enum class BlendMode : std::int32_t {
    Normal,
    Mask,
    Add,
    Subtract,
    Multiply,
};

enum class GradientDir : std::int32_t {
    None,
    LeftToRight,
    TopToBottom,
};

enum class BorderingType : std::int32_t {
    None,
    Outline,
    Inline,
};

enum class CoordinateType : std::int32_t {
    None,
    Absolute,
    Relative,
    Percentage,
};

enum class SelectionMethod : std::int32_t {
    Manual,
    Random,
    Valued,
};

enum class StartAction : std::int32_t {
    Now,
    CurrentRound,
    NextRound,
};

enum class Smoothing : std::int32_t {
    Default,
    True,
    False,
};

enum class ShowStyle : std::int32_t {
    Invisible,
    Center,
    FrontOfUser,
};

enum class UpdateType : std::int32_t {
    Fixed,
    Variables,
    All,
};

enum class EnvironmentStatus : std::int32_t {
    NotSet,
    Enable,
    Disable,
};

enum class VariableType : std::int32_t {
    Flag,
    Step,
    Variant,
};

enum class VariantType : std::int32_t {
    Number,
    String,
    Boolean,
    List,
    Structure,
};

enum class VariableInitialization : std::int32_t {
    None = 0,
    Leave = 1,
    EventExit = 2,
    BattleStart = 4,
    BattleEnd = 8,
};

enum class AbsorbTo : std::int32_t {
    None,
    Selected,
};

enum class FunctionCategory : std::int32_t {
    StringOperation,
    NumberOperation,
    ListOperation,
    Conversion,
    VariableOperation,
    CastInformation,
    CardInformation,
    CouponInformation,
    PlayingInformation,
    Etc,
};

enum class FKCKind : std::int32_t {
    Use,
    Success,
    Failure,
    HasNot,
};

enum class CType : std::int32_t {
    Start,
    StartBattle,
    End,
    EndBadEnd,
    ChangeArea,
    ChangeBgImage,
    Effect,
    EffectBreak,
    LinkStart,
    LinkPackage,
    TalkMessage,
    TalkDialog,
    PlayBgm,
    PlaySound,
    Wait,
    ElapseTime,
    CallStart,
    CallPackage,
    BranchFlag,
    BranchMultiStep,
    BranchStep,
    BranchSelect,
    BranchAbility,
    BranchRandom,
    BranchLevel,
    BranchStatus,
    BranchPartyNumber,
    BranchArea,
    BranchBattle,
    BranchIsBattle,
    BranchCast,
    BranchItem,
    BranchSkill,
    BranchInfo,
    BranchBeast,
    BranchMoney,
    BranchCoupon,
    BranchCompleteStamp,
    BranchGossip,
    SetFlag,
    SetStep,
    SetStepUp,
    SetStepDown,
    ReverseFlag,
    CheckFlag,
    GetCast,
    GetItem,
    GetSkill,
    GetInfo,
    GetBeast,
    GetMoney,
    GetCoupon,
    GetCompleteStamp,
    GetGossip,
    LoseCast,
    LoseItem,
    LoseSkill,
    LoseInfo,
    LoseBeast,
    LoseMoney,
    LoseCoupon,
    LoseCompleteStamp,
    LoseGossip,
    ShowParty,
    HideParty,
    Redisplay,
    SubstituteStep,
    SubstituteFlag,
    BranchStepCmp,
    BranchFlagCmp,
    BranchRandomSelect,
    BranchKeyCode,
    CheckStep,
    BranchRound,
    MoveBgImage,
    ReplaceBgImage,
    LoseBgImage,
    BranchMultiCoupon,
    BranchMultiRandom,
    MoveCard,
    ChangeEnvironment,
    BranchVariant,
    SetVariant,
    CheckVariant,
    CustomXmlContent,
};

enum class CTypeGroup : std::int32_t {
    Terminal = 0,
    Standard = 1,
    Data = 2,
    Utility = 3,
    Branch = 4,
    Get = 5,
    Lost = 6,
    Visual = 7,
    Variant = 8,
};

enum class CArg : std::int32_t {
    Area,
    Battle,
    Package,
    Flag,
    Step,
    BgmPath,
    BgmChannel,
    BgmVolume,
    BgmLoopCount,
    BgmFadeIn,
    SoundPath,
    SoundChannel,
    SoundVolume,
    SoundLoopCount,
    SoundFadeIn,
    Cast,
    Item,
    Skill,
    Beast,
    Info,
    Motions,
    Text,
    Dialogs,
    Start,
    Coupon,
    Gossip,
    CompleteStamp,
    Mental,
    Physical,
    Status,
    Range,
    CardVisual,
    TargetS,
    TalkerC,
    TalkerNC,
    EffectType,
    Resist,
    Transition,
    TargetAll,
    SelectionMethod,
    Average,
    Complete,
    UnsignedLevel,
    SignedLevel,
    SuccessRate,
    TransitionSpeed,
    Percent,
    FlagValue,
    StepValue,
    CouponValue,
    PartyNumber,
    CardNumber,
    Money,
    Wait,
    BgImages,
    Step2,
    Flag2,
    CastRange,
    LevelMin,
    LevelMax,
    KeyCodeRange,
    KeyCode,
    Coupons,
    InitValue,
    Comparison4,
    Comparison3,
    Round,
    CellName,
    PositionType,
    X,
    Y,
    SizeType,
    Width,
    Height,
    DoAnime,
    IgnoreEffectBooster,
    SelectionColumns,
    StartAction,
    Ignite,
    KeyCodes,
    TargetIsSkill,
    TargetIsItem,
    TargetIsBeast,
    TargetIsHand,
    HoldingCoupon,
    RefAbility,
    CenteringX,
    CenteringY,
    BoundaryCheck,
    CouponNames,
    MatchingType,
    SelectCard,
    SelectTalker,
    CardGroup,
    Scale,
    Layer,
    ConsumeCard,
    InvertResult,
    CardSpeed,
    OverrideCardSpeed,
    BackpackEnabled,
    Variant,
    Expression,
    ExpandSPChars,
    InitialEffect,
    InitialSoundPath,
    InitialSoundChannel,
    InitialSoundVolume,
    InitialSoundLoopCount,
    InitialSoundFadeIn,
    AbsorbTo,
    GameOverEnabled,
    RunAwayEnabled,
    SingleLine,
    MatchingCondition,
    XmlType,
    XmlStr,
    XmlAssignment,
    XmlVariable,
};

enum class CNextType : std::int32_t {
    None,
    Text,
    Bool,
    Step,
    IdArea,
    IdBattle,
    Trio,
    Coupon,
};

enum class MType : std::int32_t {
    Heal,
    Damage,
    Absorb,
    Paralyze,
    DisParalyze,
    Poison,
    DisPoison,
    GetSkillPower,
    LoseSkillPower,
    Sleep,
    Confuse,
    Overheat,
    Brave,
    Panic,
    Normal,
    Bind,
    DisBind,
    Silence,
    DisSilence,
    FaceUp,
    FaceDown,
    AntiMagic,
    DisAntiMagic,
    EnhanceAction,
    EnhanceAvoid,
    EnhanceResist,
    EnhanceDefense,
    VanishTarget,
    VanishCard,
    VanishBeast,
    DealAttackCard,
    DealPowerfulAttackCard,
    DealCriticalAttackCard,
    DealFeintCard,
    DealDefenseCard,
    DealDistanceCard,
    DealConfuseCard,
    DealSkillCard,
    SummonBeast,
    CancelAction,
    NoEffect,
};

enum class MArg : std::int32_t {
    ValueType,
    UValue,
    AValue,
    Round,
    Beast,
};

enum class MenuID : std::int32_t {
    None = 0,
    File,
    Edit,
    View,
    Tool,
    Table,
    Variable,
    Help,
    Card,
    CardsAndBacks,
    DelNotUsedFile,
    CreateSubWindow,
    LeftPane,
    RightPane,
    ClosePane,
    ClosePaneExcept,
    ClosePaneLeft,
    ClosePaneRight,
    ClosePaneAll,
    New,
    Open,
    NewAtNewWindow,
    OpenAtNewWindow,
    Close,
    CloseWin,
    Save,
    SaveAs,
    Reload,
    EditScenarioHistory,
    EditImportHistory,
    EditExecutedPartyHistory,
    PutParty,
    OpenDir,
    OpenBackupDir,
    OpenPlace,
    SaveImage,
    IncludeImage,
    LookImages,
    EditLayers,
    AddLayer,
    RemoveLayer,
    ChangeVH,
    SelectConnectedResource,
    Find,
    FindID,
    IncSearch,
    CloseIncSearch,
    EditProp,
    ShowProp,
    Refresh,
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    Delete,
    Cut1Content,
    Copy1Content,
    Delete1Content,
    PasteInsert,
    Clone,
    SelectAll,
    CopyAll,
    ToXMLText,
    AddItem,
    DelItem,
    TableView,
    VarView,
    CardView,
    CastView,
    SkillView,
    ItemView,
    BeastView,
    InfoView,
    FileView,
    CouponView,
    GossipView,
    CompleteStampView,
    KeyCodeView,
    CellNameView,
    CardGroupView,
    ExecEngine,
    ExecEngineAuto,
    ExecEngineMain,
    ExecEngineWithParty,
    ExecEngineWithLastParty,
    DeleteNotExistsParties,
    OuterTools,
    Settings,
    VersionInfo,
    LockToolBar,
    ResetToolBar,
    CopyAsText,
    OpenAtView,
    EventToPackage,
    StartToPackage,
    WrapTree,
    CreateContent,
    ConvertContent,
    CGroupTerminal,
    CGroupStandard,
    CGroupData,
    CGroupUtility,
    CGroupBranch,
    CGroupGet,
    CGroupLost,
    CGroupVisual,
    CGroupVariant,
    EditSummary,
    NewAreaDir,
    NewArea,
    NewBattle,
    NewPackage,
    ReNumberingAll,
    ReNumbering,
    EditScene,
    EditSceneDup,
    EditEvent,
    EditEventDup,
    SetStartArea,
    NewFlagDir,
    NewFlag,
    NewStep,
    NewVariant,
    CreateStepValues,
    PutSPChar,
    PutFlagValue,
    PutStepValue,
    PutVariantValue,
    PutColor,
    PutSkinSPChar,
    PutImageFont,
    CreateVariableEventTree,
    InitVariablesTree,
    CopyVariablePath,
    Up,
    Down,
    ConvertCouponType,
    ConvertCouponTypeNormal,
    ConvertCouponTypeHide,
    ConvertCouponTypeSystem,
    ConvertCouponTypeDur,
    ConvertCouponTypeDurBattle,
    CopyTypeConvertedCoupon,
    CopyTypeConvertedCouponNormal,
    CopyTypeConvertedCouponHide,
    CopyTypeConvertedCouponSystem,
    CopyTypeConvertedCouponDur,
    CopyTypeConvertedCouponDurBattle,
    AddInitialCoupons,
    ReverseSignOfValues,
    Reverse,
    SwapToParent,
    SwapToChild,
    OverDialog,
    UnderDialog,
    CreateDialog,
    DeleteDialog,
    CopyToAllDialogs,
    CopyToUpperDialogs,
    CopyToLowerDialogs,
    ShowParty,
    ShowMsg,
    ShowRefCards,
    FixedCards,
    FixedCells,
    FixedBackground,
    ShowGrid,
    ShowEnemyCardProp,
    ShowCard,
    ShowBack,
    NewMenuCard,
    NewEnemyCard,
    NewBack,
    NewTextCell,
    NewColorCell,
    NewPCCell,
    AutoArrange,
    ManualArrange,
    PossibleToRunAway,
    SortWithPosition,
    Mask,
    Escape,
    ChangePos,
    PosTop,
    PosBottom,
    PosLeft,
    PosRight,
    PosEven,
    NearTop,
    NearBottom,
    NearLeft,
    NearRight,
    NearCenterH,
    NearCenterV,
    NearCenter,
    ScaleMin,
    ScaleMiddle,
    ScaleMax,
    ScaleBig,
    ScaleSmall,
    ExpandBack,
    CopyColor1ToColor2,
    CopyColor2ToColor1,
    ExchangeColors,
    StopBGM,
    PlayBGM,
    NewEvent,
    NewEventWithDialog,
    KeyCodeTiming,
    KeyCodeTimingUse,
    KeyCodeTimingSuccess,
    KeyCodeTimingFailure,
    KeyCodeTimingHasNot,
    CopyTimingConvertedKeyCode,
    CopyTimingConvertedKeyCodeUse,
    CopyTimingConvertedKeyCodeSuccess,
    CopyTimingConvertedKeyCodeFailure,
    CopyTimingConvertedKeyCodeHasNot,
    AddKeyCodesByFeatures,
    KeyCodeCond,
    KeyCodeCondOr,
    KeyCodeCondAnd,
    AddRangeOfRound,
    OpenAtTableView,
    OpenAtVarView,
    OpenAtCardView,
    OpenAtFileView,
    OpenAtEventView,
    Comment,
    ShowCardProp,
    ShowCardImage,
    ShowCardDetail,
    OpenImportSource,
    SelectImportSource,
    NewCast,
    NewSkill,
    NewItem,
    NewBeast,
    NewInfo,
    Import,
    OpenHand,
    AddHand,
    RemoveRef,
    EditEventAtTimeOfUsing,
    Hold,
    PlaySE,
    StopSE,
    NewDir,
    CopyFilePath,
    CreateArchive,
    PutQuick,
    PutSelect,
    PutContinue,
    ToScript,
    ToScriptAll,
    ToScript1Content,
    EvTemplates,
    EvTemplatesOfScenario,
    EditSelection,
    Expand,
    Collapse,
    SelectCurrentEvent,
    ResetValues,
    ResetValuesAll,
    CustomizeToolBar,
    AddTool,
    AddToolBar,
    AddToolGroup,
    ResetToolBarSettings,
    DeleteNotExistsHistory,
    SelectIcon,
    SelectPresetIcon,
    DeleteIcon,
};

enum class ImportTypeIncluded : std::int32_t {
    Exclude,
    Include,
    AsIs,
};

enum class ImportTypeReference1 : std::int32_t {
    Rename,
    NoOverwrite,
    Overwrite,
    NoImport,
};

enum class ImportTypeReference2 : std::int32_t {
    Rename,
    NoImport,
};

enum class CardImageType : std::int32_t {
    PCNumber,
    File,
    Talker,
};

enum class CardImagePosition : std::int32_t {
    Default,
    Center,
    TopLeft,
};

enum class MatchingType : std::int32_t {
    And,
    Or,
};

enum class MatchingCondition : std::int32_t {
    Has,
    HasNot,
};

class MotionException : public std::runtime_error {
public:
    explicit MotionException(::cwxeditor::lib::String msg);
};

struct Target {
    enum class M : std::int32_t {
        Selected,
        Unselected,
        Random,
        Party,
    };

    M m{};
    bool sleep{};

    static Target opCall(M m, bool sleep);
    [[nodiscard]] bool opEquals(const Target& t) const noexcept;
    [[nodiscard]] bool operator==(const Target& t) const noexcept;
};

inline ::cwxeditor::lib::Array<Range> RANGE_MEMBER{
    Range::Selected,
    Range::Random,
    Range::Party,
};
inline ::cwxeditor::lib::Array<Transition> ALL_TRANSITION{
    Transition::Default,
    Transition::None,
    Transition::Blinds,
    Transition::PixelDissolve,
    Transition::Fade,
};

[[nodiscard]] Mentality toMentality(::cwxeditor::lib::StringView s);
[[nodiscard]] ::cwxeditor::lib::String fromMentality(Mentality m);
[[nodiscard]] EffectType toEffectType(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromEffectType(EffectType etyp);
[[nodiscard]] Resist toResist(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromResist(Resist resist);
[[nodiscard]] CardVisual toCardVisual(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromCardVisual(CardVisual vis);
[[nodiscard]] Element toElement(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromElement(Element el);
[[nodiscard]] DamageType toDamageType(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromDamageType(DamageType dtyp);
[[nodiscard]] Target toTarget(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromTarget(Target targ);
[[nodiscard]] Mental toMental(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromMental(Mental m);
[[nodiscard]] Mental reverseMental(Mental m);
[[nodiscard]] Physical toPhysical(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromPhysical(Physical p);
[[nodiscard]] Status toStatus(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromStatus(Status stat);
[[nodiscard]] Range toRange(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromRange(Range r);
[[nodiscard]] CastRange toCastRange(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromCastRange(CastRange r);
[[nodiscard]] Enhance toEnhance(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromEnhance(Enhance r);
[[nodiscard]] Premium toPremium(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromPremium(Premium r);
[[nodiscard]] CardTarget toCardTarget(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromCardTarget(CardTarget r);
[[nodiscard]] ::cwxeditor::lib::String fromTalker(Talker talker);
[[nodiscard]] Transition toTransition(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromTransition(Transition t);
[[nodiscard]] EffectCardType toEffectCardType(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromEffectCardType(EffectCardType t);
[[nodiscard]] Comparison4 toComparison4(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromComparison4(Comparison4 t);
[[nodiscard]] Comparison3 toComparison3(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromComparison3(Comparison3 t);
[[nodiscard]] BlendMode toBlendMode(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromBlendMode(BlendMode t);
[[nodiscard]] GradientDir toGradientDir(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromGradientDir(GradientDir t);
[[nodiscard]] BorderingType toBorderingType(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromBorderingType(BorderingType t);
[[nodiscard]] CoordinateType toCoordinateType(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromCoordinateType(CoordinateType t);
[[nodiscard]] SelectionMethod toSelectionMethod(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromSelectionMethod(SelectionMethod t);
[[nodiscard]] StartAction toStartAction(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromStartAction(StartAction t);
[[nodiscard]] Smoothing toSmoothing(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromSmoothing(Smoothing t);
[[nodiscard]] ShowStyle toShowStyle(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromShowStyle(ShowStyle t);
[[nodiscard]] UpdateType toUpdateType(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromUpdateType(UpdateType t);
[[nodiscard]] EnvironmentStatus toEnvironmentStatus(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromEnvironmentStatus(EnvironmentStatus t);
[[nodiscard]] AbsorbTo toAbsorbTo(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromAbsorbTo(AbsorbTo t);
[[nodiscard]] VariantType toVariantType(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromVariantType(VariantType t);
[[nodiscard]] VariableInitialization toVariableInitialization(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromVariableInitialization(VariableInitialization t);
[[nodiscard]] bool hasVariableInitialization(VariableInitialization value, VariableInitialization flag);
[[nodiscard]] bool isDefaultVariableInitialization(VariableInitialization value);
[[nodiscard]] bool isWsnContent(CType cType);
[[nodiscard]] CNextType toCNextType(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromCNextType(CNextType t);
[[nodiscard]] MatchingType toMatchingType(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromMatchingType(MatchingType r);
[[nodiscard]] MatchingCondition toMatchingCondition(::cwxeditor::lib::StringView name);
[[nodiscard]] ::cwxeditor::lib::String fromMatchingCondition(MatchingCondition r);

}  // namespace cwxeditor::cwx::types

// MenuIDはenumutilsの狭い既定範囲を超えるため、列挙型
// の隣で範囲を指定する。 menu.hppを読み込まないツールバー
// 保存処理を含め、すべての翻訳単位から全値を参照できるようにする。
namespace cwxeditor::cwx::enumutils {

template <>
struct enum_range<::cwxeditor::cwx::types::MenuID> {
    inline static constexpr std::intmax_t min = 0;
    inline static constexpr std::intmax_t max = static_cast<std::intmax_t>(
        ::cwxeditor::cwx::types::MenuID::DeleteIcon);
};

}  // namespace cwxeditor::cwx::enumutils
