#include "cwxeditor_src/gui/cwx/editor/gui/dwt/contentinitializer.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"

#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/eventdialog.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/customtable.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/scripterrordialog.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"

#include <algorithm>
#include <optional>
#include <charconv>
#include <cctype>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::contentinitializer {

using CArg = types::CArg;
using MArg = types::MArg;

namespace {

[[nodiscard]] std::int64_t integerValue(StringView text,
                                         std::int64_t fallback = 0) noexcept {
    if (text.empty()) return fallback;
    std::int64_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size()
               ? value
               : fallback;
}

[[nodiscard]] std::uint64_t unsignedValue(StringView text,
                                           std::uint64_t fallback = 0) noexcept {
    if (text.empty()) return fallback;
    std::uint64_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size()
               ? value
               : fallback;
}

[[nodiscard]] String lowerAscii(String value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return value;
}

[[nodiscard]] bool isSame(const ContentPtr& left,
                          const ContentPtr& right) noexcept {
    if (left.get() == right.get()) return true;
    if (!left || !right) return false;
    return left->opEquals(*right);
}

[[nodiscard]] String typeName(CType type) {
    const auto detail = event::contentDetail(type);
    if (!detail.names.empty() && !detail.names.front().empty())
        return detail.names.front();
    return "Content" + std::to_string(static_cast<std::int32_t>(type));
}

[[nodiscard]] String editedText(const ContentInitializerContext& context) {
    return context.editedInitializer.empty() ? String{"edited"}
                                               : context.editedInitializer;
}

[[nodiscard]] std::vector<CType> editableTypes() {
    std::vector<CType> result;
    for (const auto group : event::CTYPE_GROUPS_LIST()) {
        const auto found = event::CTYPE_GROUP().find(group);
        if (found == event::CTYPE_GROUP().end()) continue;
        for (const auto type : found->second) {
            if (hasDialog(type, false)) result.push_back(type);
        }
    }
    return result;
}

void notify(const std::vector<lib::Action>& callbacks) {
    const auto copy = callbacks;
    for (const auto& callback : copy) {
        if (callback) callback();
    }
}

[[nodiscard]] DialogKind dialogKind(CType type) noexcept {
    switch (type) {
    case CType::StartBattle:
    case CType::ChangeArea:
    case CType::LinkPackage:
    case CType::CallPackage:
    case CType::BranchCast:
    case CType::BranchInfo:
    case CType::GetCast:
    case CType::GetInfo:
    case CType::LoseCast:
    case CType::LoseInfo:
        return DialogKind::AreaSelect;
    case CType::LinkStart:
    case CType::CallStart:
        return DialogKind::StartSelect;
    case CType::BranchFlag:
    case CType::SetFlag:
    case CType::ReverseFlag:
    case CType::CheckFlag:
    case CType::SubstituteFlag:
    case CType::BranchFlagCmp:
        return DialogKind::Flag;
    case CType::BranchMultiStep:
    case CType::BranchStep:
    case CType::SetStep:
    case CType::SetStepUp:
    case CType::SetStepDown:
    case CType::SubstituteStep:
    case CType::BranchStepCmp:
    case CType::CheckStep:
        return DialogKind::Step;
    case CType::BranchSelect:
    case CType::BranchAbility:
    case CType::BranchRandom:
    case CType::BranchLevel:
    case CType::BranchStatus:
    case CType::BranchPartyNumber:
    case CType::BranchItem:
    case CType::BranchSkill:
    case CType::BranchBeast:
    case CType::BranchMoney:
    case CType::BranchCoupon:
    case CType::BranchCompleteStamp:
    case CType::BranchGossip:
    case CType::BranchRandomSelect:
    case CType::BranchKeyCode:
    case CType::BranchMultiCoupon:
    case CType::BranchRound:
        return DialogKind::Card;
    case CType::TalkMessage:
    case CType::TalkDialog:
        return DialogKind::Message;
    case CType::Effect:
    case CType::EffectBreak:
        return DialogKind::Effect;
    case CType::PlayBgm:
    case CType::PlaySound:
    case CType::Wait:
        return DialogKind::Audio;
    case CType::ChangeBgImage:
    case CType::MoveBgImage:
    case CType::ReplaceBgImage:
    case CType::LoseBgImage:
        return DialogKind::Background;
    case CType::ChangeEnvironment:
    case CType::ShowParty:
    case CType::HideParty:
    case CType::Redisplay:
    case CType::End:
        return DialogKind::Environment;
    case CType::BranchVariant:
    case CType::SetVariant:
    case CType::CheckVariant:
        return DialogKind::Expression;
    case CType::CustomXmlContent:
        return DialogKind::Xml;
    default:
        return DialogKind::Generic;
    }
}

[[nodiscard]] String firstAttribute(const script::Content& source,
                                    StringView key) {
    for (const auto& [name, value] : source.attributes) {
        if (name == key) return value;
    }
    return {};
}

[[nodiscard]] String argumentAt(const script::Content& source,
                                 std::size_t index) {
    const auto key = "arg" + std::to_string(index);
    // 準備された空文字列は値であり、属性の欠落ではない。
    for (const auto& [name, value] : source.attributes)
        if (name == key) return value;
    return index < source.arguments.size() ? source.arguments[index] : String{};
}

void applyScriptArgument(Content& target, const CArg kind, const String& value) {
    const auto integral=[&]<typename T>() {
        T parsed{};
        StringView text=value;
        if (!text.empty() && text.front()=='+') text.remove_prefix(1);
        const auto result=std::from_chars(text.data(),text.data()+text.size(),parsed);
        if (result.ec!=std::errc{} || result.ptr!=text.data()+text.size())
            throw std::invalid_argument("Invalid prepared script integer: "+value);
        return parsed;
    };
    const auto i = [&] { return integral.template operator()<int>(); };
    const auto u = [&] { return integral.template operator()<std::uint64_t>(); };
    const auto b = [&] {
        const auto text = lowerAscii(value);
        return text == "true" || text == "yes" || text == "on" || text == "1";
    };
#define CWX_ENUM_ARG(ARG, TYPE, SETTER) case CArg::ARG: target.SETTER(enumutils::stringToEnum<TYPE>(value)); break
    switch (kind) {
    case CArg::Area: target.area(u()); break;
    case CArg::Battle: target.battle(u()); break;
    case CArg::Package: target.packages(u()); break;
    case CArg::Cast: target.casts(u()); break;
    case CArg::Item: target.item(u()); break;
    case CArg::Skill: target.skill(u()); break;
    case CArg::Beast: target.beast(u()); break;
    case CArg::Info: target.info(u()); break;
    case CArg::Flag: target.flag(value); break;
    case CArg::Step: target.step(value); break;
    case CArg::Variant: target.variant(value); break;
    case CArg::Flag2: target.flag2(value); break;
    case CArg::Step2: target.step2(value); break;
    case CArg::Text: target.text(value); break;
    case CArg::Start: target.start(value); break;
    case CArg::Coupon: target.coupon(value); break;
    case CArg::Gossip: target.gossip(value); break;
    case CArg::CompleteStamp: target.completeStamp(value); break;
    case CArg::KeyCode: target.keyCode(value); break;
    case CArg::CellName: target.cellName(value); break;
    case CArg::CardGroup: target.cardGroup(value); break;
    case CArg::HoldingCoupon: target.holdingCoupon(value); break;
    case CArg::BgmPath: target.bgmPath(value); break; // 単独のstopはコンパイラーで正規化済み。
    case CArg::SoundPath: target.soundPath(value); break;
    case CArg::InitialSoundPath: target.initialSoundPath(value); break;
    case CArg::Expression: target.expression(value); break;
    case CArg::XmlType: target.xmlType(value); break;
    case CArg::XmlStr: target.xmlStr(value); break;
    case CArg::XmlVariable: target.xmlVariable(value); break;
    case CArg::BgmChannel: target.bgmChannel(static_cast<std::uint32_t>(i())); break;
    case CArg::BgmVolume: target.bgmVolume(static_cast<std::uint32_t>(i())); break;
    case CArg::BgmLoopCount: target.bgmLoopCount(static_cast<std::uint32_t>(i())); break;
    case CArg::BgmFadeIn: target.bgmFadeIn(static_cast<std::uint32_t>(i())); break;
    case CArg::SoundChannel: target.soundChannel(static_cast<std::uint32_t>(i())); break;
    case CArg::SoundVolume: target.soundVolume(static_cast<std::uint32_t>(i())); break;
    case CArg::SoundLoopCount: target.soundLoopCount(static_cast<std::uint32_t>(i())); break;
    case CArg::SoundFadeIn: target.soundFadeIn(static_cast<std::uint32_t>(i())); break;
    case CArg::InitialSoundChannel: target.initialSoundChannel(static_cast<std::uint32_t>(i())); break;
    case CArg::InitialSoundVolume: target.initialSoundVolume(static_cast<std::uint32_t>(i())); break;
    case CArg::InitialSoundLoopCount: target.initialSoundLoopCount(static_cast<std::uint32_t>(i())); break;
    case CArg::InitialSoundFadeIn: target.initialSoundFadeIn(static_cast<std::uint32_t>(i())); break;
    case CArg::UnsignedLevel: target.unsignedLevel(i()); break;
    case CArg::SignedLevel: target.signedLevel(i()); break;
    case CArg::SuccessRate: target.successRate(i()); break;
    case CArg::TransitionSpeed: target.transitionSpeed(i()); break;
    case CArg::Percent: target.percent(i()); break;
    case CArg::StepValue: target.stepValue(i()); break;
    case CArg::CouponValue: target.couponValue(i()); break;
    case CArg::PartyNumber: target.partyNumber(i()); break;
    case CArg::CardNumber: target.cardNumber(i()); break;
    case CArg::Money: target.money(i()); break;
    case CArg::Wait: target.wait(i()); break;
    case CArg::LevelMin: target.levelMin(i()); break;
    case CArg::LevelMax: target.levelMax(i()); break;
    case CArg::InitValue: target.initValue(i()); break;
    case CArg::Round: target.round(static_cast<std::uint32_t>(i())); break;
    case CArg::X: target.x(i()); break;
    case CArg::Y: target.y(i()); break;
    case CArg::Width: target.width(i()); break;
    case CArg::Height: target.height(i()); break;
    case CArg::Scale: target.scale(value == "none" ? -1 : i()); break;
    case CArg::Layer: target.layer(value == "none" ? -1 : i()); break;
    case CArg::SelectionColumns: target.selectionColumns(static_cast<std::uint32_t>(i())); break;
    case CArg::CardSpeed: target.cardSpeed(i()); break;
    case CArg::XmlAssignment: target.xmlAssignment(i()); break;
    case CArg::Complete: target.complete(b()); break;
    case CArg::TargetAll: target.targetAll(b()); break;
    case CArg::Average: target.average(b()); break;
    case CArg::FlagValue: target.flagValue(b()); break;
    case CArg::SelectCard: target.selectCard(b()); break;
    case CArg::DoAnime: target.doAnime(b()); break;
    case CArg::IgnoreEffectBooster: target.ignoreEffectBooster(b()); break;
    case CArg::BoundaryCheck: target.boundaryCheck(b()); break;
    case CArg::CenteringX: target.centeringX(b()); break;
    case CArg::CenteringY: target.centeringY(b()); break;
    case CArg::SelectTalker: target.selectTalker(b()); break;
    case CArg::InvertResult: target.invertResult(b()); break;
    case CArg::Ignite: target.ignite(b()); break;
    case CArg::ExpandSPChars: target.expandSPChars(b()); break;
    case CArg::ConsumeCard: target.consumeCard(b()); break;
    case CArg::OverrideCardSpeed: target.overrideCardSpeed(b()); break;
    case CArg::InitialEffect: target.initialEffect(b()); break;
    case CArg::RefAbility: target.refAbility(b()); break;
    case CArg::SingleLine: target.singleLine(b()); break;
    CWX_ENUM_ARG(Mental, types::Mental, mental);
    CWX_ENUM_ARG(Physical, types::Physical, physical);
    CWX_ENUM_ARG(Status, types::Status, status);
    CWX_ENUM_ARG(Range, types::Range, range);
    CWX_ENUM_ARG(KeyCodeRange, types::Range, keyCodeRange);
    CWX_ENUM_ARG(CardVisual, types::CardVisual, cardVisual);
    case CArg::TargetS: {
        auto text = lowerAscii(value);
        const bool sleep = text.ends_with("sleep");
        if (sleep) text.erase(text.size() - 5);
        types::Target::M member = types::Target::M::Selected;
        if (text == "unselected") member = types::Target::M::Unselected;
        else if (text == "random") member = types::Target::M::Random;
        else if (text == "party") member = types::Target::M::Party;
        target.targetS(types::Target::opCall(member, sleep));
        break;
    }
    CWX_ENUM_ARG(TalkerNC, types::Talker, talkerNC);
    CWX_ENUM_ARG(EffectType, types::EffectType, effectType);
    CWX_ENUM_ARG(Resist, types::Resist, resist);
    CWX_ENUM_ARG(Transition, types::Transition, transition);
    CWX_ENUM_ARG(SelectionMethod, types::SelectionMethod, selectionMethod);
    CWX_ENUM_ARG(Comparison4, types::Comparison4, comparison4);
    CWX_ENUM_ARG(Comparison3, types::Comparison3, comparison3);
    CWX_ENUM_ARG(PositionType, types::CoordinateType, positionType);
    CWX_ENUM_ARG(SizeType, types::CoordinateType, sizeType);
    CWX_ENUM_ARG(StartAction, types::StartAction, startAction);
    CWX_ENUM_ARG(MatchingType, types::MatchingType, matchingType);
    CWX_ENUM_ARG(MatchingCondition, types::MatchingCondition, matchingCondition);
    CWX_ENUM_ARG(AbsorbTo, types::AbsorbTo, absorbTo);
    CWX_ENUM_ARG(BackpackEnabled, types::EnvironmentStatus, backpackEnabled);
    CWX_ENUM_ARG(GameOverEnabled, types::EnvironmentStatus, gameOverEnabled);
    CWX_ENUM_ARG(RunAwayEnabled, types::EnvironmentStatus, runAwayEnabled);
    default: break; // 構造化配列、効果内容、台詞は別途処理する。
    }
#undef CWX_ENUM_ARG
}

}  // namespace

ContentInitializerContext::ContentInitializerContext(
    props::CProps& parent, variables::FlexEtcProps& properties,
    lib::Services value)
    : prop(&parent), vars(&properties), services(std::move(value)),
      clipboard(services.clipboard) {}

bool ContentInitializerContext::isTargetVersion(
    const bool legacy, const StringView dataVersion,
    const StringView wsnVersion) const {
    if (prop) return prop->isTargetVersion(legacy, dataVersion, wsnVersion);
    return summary::Summary::isTargetVersionWith(legacy, dataVersion, wsnVersion);
}

SkinPtr ContentInitializerContext::findSkin(const Summary* summ,
                                            StringView type,
                                            StringView name) const {
    if (skinResolver) return skinResolver(summ, type, name);
    if (!prop) return {};

    const String enginePath = vars ? String(vars->enginePath.value) : String{};
    if (type.empty()) {
        if (summ && !summ->type().empty()) type = summ->type();
        else if (vars && !vars->defaultSkin.value.empty()) type = vars->defaultSkin.value;
        else type = defaultSkin;
    }
    if (name.empty()) {
        if (summ && !summ->skinName().empty()) name = summ->skinName();
        else if (vars && !vars->defaultSkinName.value.empty())
            name = vars->defaultSkinName.value;
        else
            name = defaultSkinName;
    }

    // Skin::findはモデル側の検索。表が利用可能なら、dskin.findSkin2
    // の種別・名前による選択に合わせ、名前付きの項目を優先する。
    const auto table = Skin::table(*prop, enginePath);
    SkinPtr first;
    for (const auto& [path, candidate] : table) {
        (void)path;
        if (!candidate || candidate->type() != type) continue;
        if (!first) first = candidate;
        if (candidate->name() == name) return candidate;
    }
    if (first) return first;

    const String scenarioPath =
        summ ? summ->scenarioPath()
             : (vars ? String(vars->scenarioPath.value) : String{});
    const String classicEngineRegex =
        vars ? String(vars->classicEngineRegex.value) : String{};
    const String classicDataDirRegex =
        vars ? String(vars->classicDataDirRegex.value) : String{};
    const String classicMatchKey =
        vars ? String(vars->classicMatchKey.value) : String{};
    const auto engines = vars ? vars->classicEngines.value
                              : lib::Array<structs::ClassicEngine>{};
    const String defaultType = vars ? String(vars->defaultSkin.value) : defaultSkin;
    return Skin::find(*prop, enginePath, type, scenarioPath,
                      summ && summ->legacy(),
                      classicEngineRegex, classicDataDirRegex, classicMatchKey,
                      engines, defaultType);
}

bool hasDialog(const CType type, const bool existsSummary) {
    ::cwxeditor::cwx::perf::TraceScope trace_hasDialog(__FILE__, __LINE__);
    switch (type) {
    case CType::Start:
    case CType::EndBadEnd:
    case CType::ElapseTime:
    case CType::BranchArea:
    case CType::BranchBattle:
    case CType::BranchIsBattle:
    case CType::BranchMultiRandom:
        return false;
    case CType::StartBattle:
    case CType::LinkStart:
    case CType::CallStart:
    case CType::LinkPackage:
    case CType::CallPackage:
    case CType::BranchFlag:
    case CType::SetFlag:
    case CType::ReverseFlag:
    case CType::BranchMultiStep:
    case CType::BranchStep:
    case CType::SetStep:
    case CType::SetStepUp:
    case CType::SetStepDown:
    case CType::SubstituteStep:
    case CType::SubstituteFlag:
    case CType::BranchStepCmp:
    case CType::BranchFlagCmp:
    case CType::CheckFlag:
    case CType::BranchCast:
    case CType::BranchInfo:
    case CType::GetInfo:
    case CType::LoseCast:
    case CType::LoseInfo:
        return existsSummary;
    default:
        return true;
    }
}

EventDialogPtr createEventDialog(const ContentInitializerContext& context,
                                 SummaryPtr summ, SkinPtr forceSkin,
                                 UseCounterPtr uc, Parent parent,
                                 ContentPtr evt, ContentPtr parentContent,
                                 const bool create) {
    return eventdialog::createEventDialog(
        context, std::move(summ), std::move(forceSkin), std::move(uc),
        std::move(parent), std::move(evt), std::move(parentContent), create);
}

ContentPtr initial(const ContentInitializerContext& context, const Skin* skin,
                   const bool legacy, const StringView dataVersion,
                   const std::span<const BgImageS> bgImagesDefault,
                   ContentPtr content) {
    if (!content) return {};
    switch (content->type()) {
    case CType::ChangeBgImage:
        if (skin) {
            content->backgroundImages(
                skin::createBgImages(*skin,
                                     lib::Array<BgImageS>(bgImagesDefault.begin(),
                                                          bgImagesDefault.end())));
        }
        break;
    case CType::TalkDialog:
        content->dialogs({std::make_shared<event::SDialog>()});
        if (context.isTargetVersion(legacy, dataVersion, "2"))
            content->boundaryCheck(true);
        break;
    case CType::BranchSkill:
    case CType::BranchItem:
    case CType::BranchBeast:
    case CType::LoseSkill:
    case CType::LoseItem:
    case CType::LoseBeast:
        content->range(types::Range::Field);
        break;
    case CType::TalkMessage:
        if (context.isTargetVersion(legacy, dataVersion, "2"))
            content->boundaryCheck(true);
        break;
    case CType::BranchKeyCode:
        if (legacy) {
            content->targetIsSkill(true);
            content->targetIsItem(true);
            content->targetIsBeast(true);
            content->targetIsHand(true);
        }
        break;
    default:
        break;
    }
    return content;
}

ContentInitialValueEditor::ContentInitialValueEditor(
    ContentInitializerContext context, Parent parent)
    : context_(std::move(context)), parent_(std::move(parent)),
      list_(std::make_shared<customtable::TableModel>(
          customtable::STYLE_MULTI_SELECT | customtable::STYLE_VIRTUAL,
          context_.services)), data_version_(versioninfo::LATEST_VERSION) {
    std::static_pointer_cast<customtable::TableModel>(list_)->installRangeSelection();
    list_->multi_select = true;
    list_->full_row_selection = true;
    // SWTのTableヘッダーは明示的に有効化しなければ非表示で、
    // Dもここでは有効にしない。一覧の上に空のヘッダー行を出さない。
    list_->header_visible = false;
    list_->columns = {
        // SWTのTableヘッダーは、明示的に有効化しなければ非表示。
        lib::TableColumn{"", 132.0F},
        lib::TableColumn{"", 42.0F},
    };

    for (const auto type : editableTypes()) {
        const auto row = list_->rows.size();
        type_rows_[type] = row;
        lib::TableRow item;
        item.cells.resize(2);
        item.cells[0].text = context_.contentName ? context_.contentName(type)
                                                  : typeName(type);
        item.cells[0].value = std::to_string(static_cast<std::int32_t>(type));
        item.cells[0].icon = context_.contentImage ? context_.contentImage(type) : 0;
        item.cells[1].text = editedText(context_);
        item.cells[1].value = item.cells[0].value;
        list_->rows.push_back(std::move(item));
    }
    const std::weak_ptr<void> callbacksLife=lifetime_;
    list_->on_selection_changed = [this,callbacksLife](const lib::Array<std::size_t>&) {
        if (callbacksLife.expired() || !refreshToolBar()) return;
        updateToolTip();
    };
    list_->on_cell_activate = [this,callbacksLife](const std::size_t row, const std::size_t) {
        if (callbacksLife.expired()) return;
        const auto generation=session_generation_;
        setSelection({row});
        if (callbacksLife.expired() || generation!=session_generation_) return;
        editContent();
    };
    list_->on_key_down = [this,callbacksLife](lib::KeyEvent& event) {
        if (callbacksLife.expired()) return;
        if(context_.editor_props && !event.handled) {
            const auto matches=[&](types::MenuID id) {
                const auto text=context_.editor_props->buildMenu(id);
                const auto tab=text.find('\t');
                return tab!=String::npos && lib::menu_shortcut_matches(StringView(text).substr(tab+1),event.key,event.modifiers);
            };
            if(matches(types::MenuID::Undo)) {undo();event.handled=true;return;}
            if(matches(types::MenuID::Redo)) {redo();event.handled=true;return;}
        }
        // 一覧の切り取り・コピー・貼り付けはポップアップメニュー内だけにあり、共通メニュー検索
        // はそのショートカットを見ない。DのTCPDキーに合わせ、Ctrl+X/C/Vを扱う。
        if (dmenu::handleClipboardShortcut(event, {{},
                [this] { return canDoC(); }, [this] { return canDoP(); },
                {}, [this] { copy(); }, [this] { paste(); }})) return;
        if (event.handled || event.key != lib::Key::Enter || !canEditContent()) return;
        // DはSWT.KeyDownのEnterを、ダブルクリッ
        // クと同じプロパティコマンドへ接続する。意味上のキーイベ
        // ントとして扱い、全バックエンドで共通入力経路を通す。
        editContent();
        event.handled = true;
    };
    context_menu_ = std::make_shared<lib::Menu>(true);
    context_menu_->on_open = [this,callbacksLife] { if (!callbacksLife.expired()) rebuildContextMenu(); };
    context_menu_->on_command = [this,callbacksLife](const StringView command) {
        if (callbacksLife.expired()) return;
        if (command == "content-initializer.edit") editContent();
        else if (command == "content-initializer.undo") undo();
        else if (command == "content-initializer.redo") redo();
        else if (command == "content-initializer.reset") resetValues();
        else if (command == "content-initializer.reset-all") resetValuesAll();
        else if (command == "content-initializer.copy") copy();
        else if (command == "content-initializer.paste") paste();
        else if (command == "content-initializer.select-all") selectAll();
    };
    if(context_.editor_commons) {
        // DのGCはデリゲートの通知元を生存させる。ネイティブの初期値
        // 編集部はMainWindowより長く残り得るため、コピーされ
        // た通知と破棄時の接続解除の両方で、弱い寿命情報を保持する。
        publisher_lifetime_=context_.editor_commons->lifetimeToken();
        const auto publisherLife=publisher_lifetime_;
        menu_connection_=context_.editor_commons->refMenu.add([this,callbacksLife,publisherLife](types::MenuID) {
            if(!callbacksLife.expired() && !publisherLife.expired())rebuildContextMenu();
        });
        undo_max_connection_=context_.editor_commons->refUndoMax.add([this,callbacksLife,publisherLife] {
            if(!callbacksLife.expired() && !publisherLife.expired() && undo_ && context_.editor_props)
                undo_->max(static_cast<std::size_t>(std::max(0,context_.editor_props->var().undoMaxEtc.value)));
        });
    }
    rebuildContextMenu();
    const auto rangePointer=list_->on_pointer;
    const std::weak_ptr<void> pointerLife=lifetime_;
    list_->on_pointer = [this,pointerLife,rangePointer](lib::PointerEvent& event) {
        if (pointerLife.expired()) return;
        const auto generation=session_generation_;
        if (rangePointer) rangePointer(event);
        if (pointerLife.expired() || generation!=session_generation_) return;
        if (event.action==lib::PointerAction::Move || event.action==lib::PointerAction::Enter ||
            event.action==lib::PointerAction::Leave) {
            hovered_row_ = event.action==lib::PointerAction::Leave ? std::nullopt :
                event.item_hit_test_available ? (event.item_index>=0 ?
                    std::optional<std::size_t>(static_cast<std::size_t>(event.item_index)) : std::nullopt) :
                    std::static_pointer_cast<customtable::TableModel>(list_)->rowAt(event.position);
            updateToolTip();
            if (pointerLife.expired() || generation!=session_generation_) return;
        }
        if (event.button != lib::PointerButton::Secondary ||
            event.action != lib::PointerAction::Up || !context_.services.gui)
            return;
        // SWTはポップアップを開く前に未選択行を選ぶ。クリ
        // ックが既存の複数選択内なら、その選択を維持する。
        if (event.item_hit_test_available && event.item_index >= 0) {
            const auto row = static_cast<std::size_t>(event.item_index);
            if (row < list_->rows.size() &&
                std::find(list_->selection.begin(), list_->selection.end(), row) ==
                    list_->selection.end())
                setSelection({row});
        }
        if (pointerLife.expired() || generation!=session_generation_) return;
        rebuildContextMenu();
        const auto gui=context_.services.gui;
        const auto menu=context_menu_;
        const auto table=list_;
        gui->show_context_menu(menu, table, event.position);
        event.handled = true;
    };
    if (parent_) parent_->add(list_);
}

ContentInitialValueEditor::~ContentInitialValueEditor() {
    lifetime_.reset();
    if(context_.editor_commons && !publisher_lifetime_.expired()) {
        context_.editor_commons->refMenu.remove(menu_connection_);
        context_.editor_commons->refUndoMax.remove(undo_max_connection_);
    }
    if (list_) {
        list_->on_selection_changed = {};
        list_->on_cell_activate = {};
        list_->on_key_down = {};
        list_->on_pointer = {};
    }
    if (context_menu_) {
        context_menu_->on_open = {};
        context_menu_->on_command = {};
    }
    // ダイアログの終了はcloseEventを同期通知する。再入した
    // 削除が巡回を無効にしないよう、通知前に所有するマップを移す。
    auto retired=std::move(edit_dialogs_);
    edit_dialogs_.clear();
    for (auto& [type, dialog] : retired) {
        (void)type;
        if (dialog) dialog->forceCancel();
    }
}

void ContentInitialValueEditor::bgImagesDefault(BgImageDefaults value) {
    ::cwxeditor::cwx::perf::TraceScope trace_bgImagesDefault(__FILE__, __LINE__);
    const std::weak_ptr<void> life=lifetime_;
    const auto generation=session_generation_;
    bg_images_ = std::move(value);
    const auto found = values_.find(CType::ChangeBgImage);
    if (found == values_.end() || !found->second || !summary_) return;

    const auto context=context_;
    const auto summary=summary_;
    const auto skin = context.findSkin(summary.get());
    if (life.expired() || generation!=session_generation_) return;
    const auto init = initial(skin.get(), CType::ChangeBgImage);
    initial_values_[CType::ChangeBgImage] = init;
    if (!edited_[CType::ChangeBgImage]) {
        if (undo_ && !store({found->second})) return;
        values_[CType::ChangeBgImage] = init->dup();
    }
    edited_[CType::ChangeBgImage] = !sameContent(values_[CType::ChangeBgImage], init);
    if (!updateRow(CType::ChangeBgImage)) return;
    updateToolTip();
}

void ContentInitialValueEditor::setInitializer(
    const Initializer& initializer, const bool legacy, String dataVersion,
    undo::UndoManager* undo) {
    ::cwxeditor::cwx::perf::TraceScope trace_setInitializer(__FILE__, __LINE__);
    const std::weak_ptr<void> life=lifetime_;
    const auto generation=++session_generation_;
    ++tooltip_revision_;
    auto retired=std::move(edit_dialogs_);
    edit_dialogs_.clear();
    undo_ = undo;
    legacy_ = legacy;
    data_version_ = std::move(dataVersion);

    const String skinType =
        context_.vars ? String(context_.vars->defaultSkin.value) : context_.defaultSkin;
    const String skinName =
        context_.vars ? String(context_.vars->defaultSkinName.value)
                      : context_.defaultSkinName;
    summary_ = std::make_shared<Summary>("", skinType, skinName, "", false, legacy_);
    summary_->dataVersion(data_version_);

    const auto context=context_;
    const auto summary=summary_;
    const auto skin = context.findSkin(summary.get());
    if (life.expired() || generation!=session_generation_) return;
    initial_values_.clear();
    values_.clear();
    edited_.clear();
    for (const auto type : editableTypes()) {
        const auto init = initial(skin.get(), type);
        initial_values_[type] = init;
        const auto found = initializer.find(type);
        if (found != initializer.end() && found->second &&
            !sameContent(found->second, init)) {
            values_[type] = found->second->dup();
            edited_[type] = true;
        } else {
            values_[type] = init->dup();
            edited_[type] = false;
        }
        if (!updateRow(type)) return;
    }
    for (auto& [type, dialog] : retired) {
        (void)type;
        if (dialog) dialog->forceCancel();
        if (life.expired()) return;
        if (generation!=session_generation_) return;
    }
    (void)lib::select_table_rows(*list_, {}, false, false);
    if (!refreshToolBar()) return;
    updateToolTip();
}

ContentInitialValueEditor::Initializer
ContentInitialValueEditor::getInitializer() const {
    ::cwxeditor::cwx::perf::TraceScope trace_getInitializer(__FILE__, __LINE__);
    Initializer result;
    for (const auto& [type, edited] : edited_) {
        if (!edited) continue;
        const auto found = values_.find(type);
        if (found != values_.end() && found->second) result.emplace(type, found->second->dup());
    }
    return result;
}

void ContentInitialValueEditor::setSelection(lib::Array<std::size_t> indices) {
    indices.erase(std::remove_if(indices.begin(), indices.end(),
                                 [this](std::size_t value) {
                                     return value >= list_->rows.size();
                                 }),
                  indices.end());
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    (void)lib::select_table_rows(*list_, std::move(indices), false, false);
    if (!refreshToolBar()) return;
    updateToolTip();
}

ContentPtr ContentInitialValueEditor::initial(const Skin* skin,
                                              const CType type) const {
    ::cwxeditor::cwx::perf::TraceScope trace_initial(__FILE__, __LINE__);
    return contentinitializer::initial(context_, skin, legacy_, data_version_,
                                       std::span<const BgImageS>(bg_images_),
                                       std::make_shared<Content>(type, ""));
}

bool ContentInitialValueEditor::store(const lib::Array<ContentPtr>& contents) {
    if (!undo_ || contents.empty()) return true;
    const std::weak_ptr<void> life=lifetime_;
    const auto generation=session_generation_;
    *undo_ += std::make_shared<CUndo>(*this, contents);
    return !life.expired() && generation==session_generation_;
}

bool ContentInitialValueEditor::updateRow(const CType type) {
    const auto row = type_rows_.find(type);
    if (row == type_rows_.end() || row->second >= list_->rows.size()) return true;
    auto& cells = list_->rows[row->second].cells;
    if (cells.size() < 2) cells.resize(2);
    cells[1].value = cells[0].value;
    const auto table=list_;
    const std::weak_ptr<void> life=lifetime_;
    const auto generation=session_generation_;
    (void)lib::publish_table_cell_text(
        *table, row->second, 1U, edited_[type] ? editedText(context_) : String{});
    return !life.expired() && generation==session_generation_;
}

bool ContentInitialValueEditor::notifyChanged() {
    const std::weak_ptr<void> life=lifetime_;
    const auto generation=session_generation_;
    const auto copy = modEvent;
    for (const auto& callback : copy) {
        if (callback) callback();
        if (life.expired() || generation!=session_generation_) return false;
    }
    return true;
}

bool ContentInitialValueEditor::refreshToolBar() {
    // Dの所有元は同期SWT通知をまたいで生存する。共通コールバックはこ
    // の編集部を終了・置換し得るため、その場合は古い通知の連鎖を止める。
    const std::weak_ptr<void> life=lifetime_;
    const auto generation=session_generation_;
    const auto callback=context_.refreshToolBar;
    if (callback) callback();
    return !life.expired() && generation==session_generation_;
}

void ContentInitialValueEditor::updateToolTip() {
    String text;
    if (hovered_row_) {
        const auto row = *hovered_row_;
        if (row < list_->rows.size()) {
            const auto typeValue = integerValue(list_->rows[row].cells.front().value);
            const auto type = static_cast<CType>(typeValue);
            const auto found = values_.find(type);
            if (found != values_.end() && found->second) {
                const auto content=found->second;
                const auto summary=summary_;
                const auto callback=context_.contentText;
                const std::weak_ptr<void> life=lifetime_;
                const auto generation=session_generation_;
                text = callback ? callback(*content, summary.get()) : content->name();
                if (life.expired() || generation!=session_generation_) return;
            }
        }
    }
    const auto table=list_;
    const auto revision=++tooltip_revision_;
    const auto generation=session_generation_;
    const std::weak_ptr<void> life=lifetime_;
    auto publish=[this,table,life,revision,generation,text=std::move(text)]() mutable {
        if (life.expired() || generation!=session_generation_ || revision!=tooltip_revision_) return;
        table->set_tooltip(std::move(text));
    };
    // Dは表の遅い同期ツールチップ更新を避けるためasyncExecを使う。共通ウ
    // ィジェットへ公開する前に、置換済みのホバー・セッションの処理を無効にする。
    if (context_.services.dispatcher) context_.services.dispatcher->post(std::move(publish));
    else publish();
}

bool ContentInitialValueEditor::canEditContent() const noexcept {
    ::cwxeditor::cwx::perf::TraceScope trace_canEditContent(__FILE__, __LINE__);
    return !list_->selection.empty();
}

void ContentInitialValueEditor::rebuildContextMenu() {
    if (!context_menu_) return;
    const auto item = [this](const types::MenuID id, String text,
                             String command, const bool enabled) {
        lib::MenuItem result;
        result.text = std::move(text);
        result.command = std::move(command);
        // DのcreateMenuItemは、ローカライズされたラベルとともにMenuIDの画像を渡す。
        result.icon = context_.editor_props
                          ? context_.editor_props->images().menu(id)
                          : ResourceId{};
        result.enabled = enabled;
        return result;
    };
    const auto separator = [] {
        lib::MenuItem result;
        result.separator = true;
        return result;
    };
    const auto label=[&](types::MenuID id,String fallback) {
        return context_.editor_props ? context_.editor_props->buildMenu(id) : fallback;
    };
    const bool can_undo = undo_ && undo_->canUndo();
    const bool can_redo = undo_ && undo_->canRedo();
    context_menu_->items = {
        item(types::MenuID::EditProp, label(types::MenuID::EditProp,"編集"),
             "content-initializer.edit", canEditContent()), separator(),
        item(types::MenuID::Undo, label(types::MenuID::Undo,"元に戻す"),
             "content-initializer.undo", can_undo),
        item(types::MenuID::Redo, label(types::MenuID::Redo,"やり直し"),
             "content-initializer.redo", can_redo), separator(),
        item(types::MenuID::ResetValues,
             label(types::MenuID::ResetValues,"選択項目を初期値に戻す"),
             "content-initializer.reset",
             canEditContent() && !isInitialValues()),
        item(types::MenuID::ResetValuesAll,
             label(types::MenuID::ResetValuesAll,"すべて初期値に戻す"),
             "content-initializer.reset-all",
             !isInitialValuesAll()), separator(),
        item(types::MenuID::Copy, label(types::MenuID::Copy,"コピー"),
             "content-initializer.copy", canDoC()),
        item(types::MenuID::Paste, label(types::MenuID::Paste,"貼り付け"),
             "content-initializer.paste", canDoP()), separator(),
        item(types::MenuID::SelectAll, label(types::MenuID::SelectAll,"すべて選択"),
             "content-initializer.select-all",
             list_->selection.size() != list_->rows.size()),
    };
    ++context_menu_->item_update_revision;
}

void ContentInitialValueEditor::undo() {
    if (undo_ && undo_->canUndo()) (void)undo_->undo();
}

void ContentInitialValueEditor::redo() {
    if (undo_ && undo_->canRedo()) (void)undo_->redo();
}

void ContentInitialValueEditor::selectAll() {
    lib::Array<std::size_t> rows(list_->rows.size());
    std::iota(rows.begin(), rows.end(), std::size_t{});
    setSelection(std::move(rows));
}

void ContentInitialValueEditor::editContent() {
    ::cwxeditor::cwx::perf::TraceScope trace_editContent(__FILE__, __LINE__);
    const std::weak_ptr<void> life=lifetime_;
    const auto generation=session_generation_;
    const auto stop=context_.stopOpen;
    const auto selection = list_->selection;
    for (const auto row : selection) {
        const bool stopped=stop && stop();
        if (life.expired() || generation!=session_generation_ || stopped) return;
        editContentImpl(row);
        if (life.expired() || generation!=session_generation_) return;
    }
}

void ContentInitialValueEditor::editContentImpl(const std::size_t row) {
    const std::weak_ptr<void> life=lifetime_;
    const auto generation=session_generation_;
    if (row >= list_->rows.size()) return;
    const auto typeValue = integerValue(list_->rows[row].cells.front().value);
    const auto type = static_cast<CType>(typeValue);
    const auto found = values_.find(type);
    if (found == values_.end() || !found->second) return;
    if (const auto active = edit_dialogs_.find(type);
        active != edit_dialogs_.end() && active->second) {
        active->second->active();
        return;
    }

    // コピーした配送先の状態間で、再接続可能な基準値を共有する。
    const auto before = std::make_shared<ContentPtr>(found->second->dup());
    const auto context=context_;
    const auto summary=summary_;
    const auto content=found->second;
    const auto data_version = data_version_;
    const auto legacy = legacy_;
    const auto skin=context.findSkin(summary.get());
    if (life.expired() || generation!=session_generation_) return;
    auto dialog = createEventDialog(context, summary, skin,
                                    summary ? summary->useCounter() : UseCounterPtr{},
                                    parent_, content, {}, false);
    if (life.expired() || generation!=session_generation_) return;
    if (!dialog) return;
    dialog->appliedEvent.push_back(
        [this, life, generation, type, row, data_version, legacy,
         before, content]() mutable {
        const auto currentSession = [&] {
            return !life.expired() && generation == session_generation_ &&
                   data_version_ == data_version && legacy_ == legacy;
        };
        const auto rowIsCurrent = [&] {
            if (!currentSession()) return false;
            const auto mapped = type_rows_.find(type);
            return mapped != type_rows_.end() && mapped->second == row &&
                   row < list_->rows.size() &&
                   !list_->rows[row].cells.empty() &&
                   integerValue(list_->rows[row].cells.front().value) ==
                       static_cast<std::int32_t>(type);
        };
        if (!rowIsCurrent()) return;
        // Dは取り消しで表の行を再接続しても、モードレスの初
        // 期値ダイアログを旧Contentへ接続したままにす
        // る。その受理時に変更の印だけ更新し、古い行値を公開
        // し得るため、意図的に修正する。受理した編集内容を現
        // 在行へ結び付け、履歴は現在見えている行を保存する。
        if (!content || sameContent(content, *before)) return;
        const auto current = values_.find(type);
        if (current == values_.end() || !current->second) return;
        const auto row_before = current->second;
        const bool dialogStillOwnsRow = row_before == content;
        if (!dialogStillOwnsRow && sameContent(row_before, content)) {
            // 受理した値は、取り消しで読み戻した行とすでに一致する。
            // 空のコマンドを作らず、ダイアログの基準値だけを進める。
            *before = content->dup();
            return;
        }
        const auto undo_snapshot = dialogStillOwnsRow ? *before : row_before;
        if (!store({undo_snapshot})) return;
        if (!rowIsCurrent()) return;
        const auto rebound = values_.find(type);
        if (rebound == values_.end() || rebound->second != row_before) return;
        if (!dialogStillOwnsRow) rebound->second = content->dup();
        *before = content->dup();
        edited_[type] = !sameContent(rebound->second, initial_values_[type]);
        if (!updateRow(type)) return;
        notifyChanged();
        if (life.expired() || generation!=session_generation_) return;
        refreshToolBar();
        if (life.expired() || generation!=session_generation_) return;
        updateToolTip();
    });
    dialog->closeEvent.push_back([this, life, generation, type,
                                  data_version, legacy] {
        if (life.expired() || generation != session_generation_ ||
            data_version_ != data_version || legacy_ != legacy) return;
        edit_dialogs_.erase(type);
    });
    edit_dialogs_[type] = std::move(dialog);
    edit_dialogs_[type]->open();
}

void ContentInitialValueEditor::resetValues() {
    ::cwxeditor::cwx::perf::TraceScope trace_resetValues(__FILE__, __LINE__);
    if (!isInitialValues()) resetValuesImpl(list_->selection);
}

void ContentInitialValueEditor::resetValuesAll() {
    ::cwxeditor::cwx::perf::TraceScope trace_resetValuesAll(__FILE__, __LINE__);
    if (!isInitialValuesAll()) {
        lib::Array<std::size_t> rows;
        rows.resize(list_->rows.size());
        for (std::size_t i = 0; i < rows.size(); ++i) rows[i] = i;
        resetValuesImpl(rows);
    }
}

void ContentInitialValueEditor::resetValuesImpl(
    const lib::Array<std::size_t>& rows) {
    lib::Array<ContentPtr> old;
    old.reserve(rows.size());
    for (const auto row : rows) {
        if (row >= list_->rows.size()) continue;
        const auto typeValue = integerValue(list_->rows[row].cells.front().value);
        const auto type = static_cast<CType>(typeValue);
        const auto found = values_.find(type);
        if (found != values_.end() && found->second) old.push_back(found->second);
    }
    if (old.empty()) return;
    if (!store(old)) return;
    for (const auto row : rows) {
        if (row >= list_->rows.size()) continue;
        const auto type = static_cast<CType>(
            integerValue(list_->rows[row].cells.front().value));
        const auto init = initial_values_.find(type);
        if (init == initial_values_.end() || !init->second) continue;
        values_[type] = init->second->dup();
        edited_[type] = false;
        if (!updateRow(type)) return;
    }
    if (!notifyChanged()) return;
    if (!refreshToolBar()) return;
    updateToolTip();
}

bool ContentInitialValueEditor::isInitialValues() const {
    ::cwxeditor::cwx::perf::TraceScope trace_isInitialValues(__FILE__, __LINE__);
    return isInitialValuesImpl(list_->selection);
}

bool ContentInitialValueEditor::isInitialValuesAll() const {
    ::cwxeditor::cwx::perf::TraceScope trace_isInitialValuesAll(__FILE__, __LINE__);
    lib::Array<std::size_t> rows;
    rows.resize(list_->rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) rows[i] = i;
    return isInitialValuesImpl(rows);
}

bool ContentInitialValueEditor::isInitialValuesImpl(
    const lib::Array<std::size_t>& rows) const {
    for (const auto row : rows) {
        if (row >= list_->rows.size()) continue;
        const auto type = static_cast<CType>(
            integerValue(list_->rows[row].cells.front().value));
        const auto current = values_.find(type);
        const auto init = initial_values_.find(type);
        if (current == values_.end() || init == initial_values_.end() ||
            !sameContent(current->second, init->second))
            return false;
    }
    return true;
}

void ContentInitialValueEditor::cut() {}
void ContentInitialValueEditor::del() {}
void ContentInitialValueEditor::clone() {}

void ContentInitialValueEditor::copy() {
    const std::weak_ptr<void> life=lifetime_;
    const auto generation=session_generation_;
    if (list_->selection.empty()) return;
    auto root = event::XNode::create("Contents");
    for (const auto row : list_->selection) {
        if (row >= list_->rows.size()) continue;
        const auto type = static_cast<CType>(
            integerValue(list_->rows[row].cells.front().value));
        const auto found = values_.find(type);
        if (found != values_.end() && found->second)
            found->second->toNode(root, nullptr);
    }
    const bool xmlCopy = context_.vars && static_cast<bool>(context_.vars->xmlCopy);
    xmlbytestransfer::XMLtoCB(xmlCopy, context_.clipboard, root.text());
    if (life.expired() || generation!=session_generation_) return;
    refreshToolBar();
}

void ContentInitialValueEditor::paste() {
    const std::weak_ptr<void> life=lifetime_;
    const auto generation=session_generation_;
    const auto errorReporter=context_.errorReporter;
    const auto scriptErrorReporter=context_.scriptErrorReporter;
    const auto variableResolver=context_.variableResolver;
    const auto scriptVariableDialog=context_.scriptVariableDialog;
    lib::Array<ContentPtr> contents;
    String xml = xmlbytestransfer::CBtoXML(context_.clipboard);
    if (!xml.empty()) {
        try {
            auto node = event::XNode::parse(xml, true);
            if (node.name() == "Contents") {
                event::Content::createContentsFromNode(
                    node, nullptr, [&contents](ContentPtr value) {
                        if (value) contents.push_back(std::move(value));
                    });
            } else if (auto value = event::Content::createFromNode(node, nullptr, false)) {
                contents.push_back(std::move(value));
            }
        } catch (const std::exception& error) {
            if (errorReporter) errorReporter(error.what());
        }
    } else {
        auto data = context_.clipboard.getContents();
        String scriptText = std::move(data.text);
        if (context_.clipboard.memoryMode()) scriptText = context_.clipboard.memory();
        if (!scriptText.empty() && summary_) {
            const String base = scriptText;
            script::CompileOption option;
            try {
                script::CWXScript compiler(context_.prop, summary_.get(), scriptText);
                const auto variables = compiler.eatEmptyVars(scriptText, option);
                if (!variables.empty() && scriptVariableDialog) {
                    const auto summary=summary_;
                    const auto records=scriptVariableDialog(summary,variables,scriptText,base,option);
                    if (life.expired() || generation!=session_generation_ || !records) return;
                    for (const auto& record : *records)
                        if (auto value=fromScript(record, context_.prop)) contents.push_back(std::move(value));
                    pasteContents(std::move(contents));
                    return;
                }
                if (!variables.empty() && variableResolver) {
                    const auto values = variableResolver(variables, scriptText);
                    if (life.expired() || generation!=session_generation_) return;
                    scriptText = script::CWXScript::pushVars(
                        std::move(scriptText), values, option);
                }
                const auto records = script::compile(context_.prop, summary_.get(),
                                                     scriptText, option);
                for (const auto& record : records)
                    if (auto value = fromScript(record, context_.prop)) contents.push_back(std::move(value));
            } catch (const script::CWXScriptException& error) {
                if (scriptErrorReporter) scriptErrorReporter(error,base,option);
                else if (errorReporter) errorReporter(
                    scripterrordialog::ScriptErrorDialog::formatError(
                        error, context_.prop, base, option));
            } catch (const std::exception& error) {
                if (errorReporter) errorReporter(error.what());
            }
        }
    }
    if (life.expired() || generation!=session_generation_) return;
    pasteContents(std::move(contents));
}

void ContentInitialValueEditor::pasteContents(lib::Array<ContentPtr> contents) {
    lib::Array<ContentPtr> old;
    for (const auto& content : contents) {
        if (!content) continue;
        const auto found = type_rows_.find(content->type());
        if (found == type_rows_.end()) continue;
        const auto current = values_.find(content->type());
        if (current != values_.end() && current->second &&
            !sameContent(current->second, content))
            old.push_back(current->second);
    }
    if (!old.empty() && !store(old)) return;
    lib::Array<std::size_t> selection;
    for (const auto& content : contents) {
        if (!content) continue;
        const auto found = type_rows_.find(content->type());
        if (found == type_rows_.end()) continue;
        const auto current = values_.find(content->type());
        if (current == values_.end() || !current->second ||
            !sameContent(current->second, content)) {
            values_[content->type()] = content->dup();
            edited_[content->type()] = !sameContent(
                values_[content->type()], initial_values_[content->type()]);
            if (!updateRow(content->type())) return;
        }
        selection.push_back(found->second);
    }
    (void)lib::select_table_rows(*list_, std::move(selection), false, false);
    if (!old.empty() && !notifyChanged()) return;
    if (!refreshToolBar()) return;
    updateToolTip();
}

bool ContentInitialValueEditor::canDoP() const {
    if (xmlbytestransfer::CBisXML(context_.clipboard)) return true;
    // DのCBisTextは、空データも含めネイティブのTextTransfer
    // 形式を調べる。メモリXMLモードを通常テキストと混同しない。
    if (const auto* backend = context_.clipboard.backend())
        return backend->contains("text/plain") || backend->contains("text/plain;charset=utf-8");
    const auto data = context_.clipboard.getContents();
    return !data.text.empty() || data.formats.contains("text/plain") ||
           data.formats.contains("text/plain;charset=utf-8");
}

ContentInitialValueEditor::CUndo::CUndo(
    ContentInitialValueEditor& owner, const lib::Array<ContentPtr>& contents)
    : owner_(&owner), lifetime_(owner.lifetime_), undo_manager_(owner.undo_),
      legacy_(owner.legacy_), data_version_(owner.data_version_) {
    for (const auto& content : contents) {
        if (content) contents_.push_back(content->dup());
    }
}

void ContentInitialValueEditor::CUndo::apply() {
    // Dは初期値の対象領域ごとに独立した履歴を持つ。その領域の再
    // 読み込みで記録を無効にしない。ただし適用中に行・通知コール
    // バックがセッションを変えた場合は、現在の操作を終了する。
    if (!owner_ || lifetime_.expired() || owner_->undo_ != undo_manager_ ||
        owner_->legacy_ != legacy_ || owner_->data_version_ != data_version_) return;
    const auto generation = owner_->session_generation_;
    for (auto& saved : contents_) {
        if (!saved) continue;
        const auto type = saved->type();
        auto current = owner_->values_.find(type);
        if (current == owner_->values_.end() || !current->second) continue;
        // Dは保存状態を戻す前に現在行を複製する。モードレスEventDialog
        // がcurrent->secondを所有している場合もあるため、
        // 履歴はその有効な編集対象をやり直し用の保存状態として使わない。
        auto restored = saved;
        saved = current->second->dup();
        current->second = std::move(restored);
        owner_->edited_[type] =
            !ContentInitialValueEditor::sameContent(
                current->second, owner_->initial_values_[type]);
        if (!owner_->updateRow(type) || lifetime_.expired() ||
            generation != owner_->session_generation_) return;
    }
    if (!owner_->notifyChanged()) return;
    if (!owner_->refreshToolBar()) return;
    owner_->updateToolTip();
}

void ContentInitialValueEditor::CUndo::undo() { apply(); }
void ContentInitialValueEditor::CUndo::redo() { apply(); }

bool ContentInitialValueEditor::sameContent(const ContentPtr& left,
                                            const ContentPtr& right) noexcept {
    return isSame(left, right);
}

ContentPtr ContentInitialValueEditor::fromScript(const script::Content& source,
                                                 const props::CProps* prop) {
    // Dと同様に、new Content(type, parseNextValue(...))
    // でラベルが空なら空名を維持する。waitなどのキーワードを名前に入れると
    // 、直後の子にそのラベルが付き、書き戻し時に不要なsifを出力してしまう。
    auto result = std::make_shared<Content>(source.type, source.name);
    result->comment(source.comment);
    for (std::size_t index = 0; index < source.argumentKinds.size(); ++index) {
        const auto key="arg"+std::to_string(index);
        const bool attributeExists=std::any_of(source.attributes.begin(),source.attributes.end(),
            [&](const auto& attribute){return attribute.first==key;});
        if(index>=source.arguments.size() && !attributeExists)
            throw std::invalid_argument("script record has a missing typed argument");
        // 呼び出し側はscript::compileを
        // 経ずにContentを直接構築できる。黙って
        // 既定値へ変えず変換失敗を維持し、外側のスクリ
        // プトUIのエラー表示で報告できるようにする。
        applyScriptArgument(*result, source.argumentKinds[index],
                            argumentAt(source, index));
    }
    const auto arg0 = argumentAt(source, 0);
    if (source.type == CType::TalkMessage && source.argumentKinds.empty()) {
        const auto talker = lowerAscii(arg0);
        const bool hasTalker = talker == "none" || talker == "single" ||
                               talker == "m" || talker == "u" || talker == "r" ||
                               talker == "c" || talker == "v";
        result->text(hasTalker && !argumentAt(source, 1).empty()
                         ? argumentAt(source, 1)
                         : arg0);
    } else if (source.type == CType::TalkDialog && !source.dialogs) {
        const auto talker = lowerAscii(arg0);
        const bool hasTalker = talker == "selected" || talker == "unselected" ||
                               talker == "random" || talker == "card" ||
                               talker == "valued";
        const auto text = hasTalker && !argumentAt(source, 1).empty()
                              ? argumentAt(source, 1)
                              : arg0;
        result->dialogs({std::make_shared<event::SDialog>(text)});
    }
    if (!source.cardImages.empty()) result->cardPaths(source.cardImages);
    if (source.typedCardImages) {
        lib::Array<lib::Owner<card::CardImage>> images;
        for (const auto& image : *source.typedCardImages)
            if (image) images.push_back(std::make_shared<card::CardImage>(nullptr,*image));
        result->cardImages(std::move(images));
    }
    if (source.dialogs) {
        lib::Array<lib::Owner<event::SDialog>> dialogs;
        for (const auto& dialog : *source.dialogs) if (dialog) dialogs.push_back(dialog->dup());
        result->dialogs(std::move(dialogs));
    }
    if (source.coupons) {
        lib::Array<lib::Owner<coupon::Coupon>> coupons;
        for (const auto& coupon : *source.coupons) if (coupon) coupons.push_back(coupon->dup());
        result->coupons(std::move(coupons));
    }
    if (source.keyCodes) result->keyCodes(*source.keyCodes);
    if (source.couponNames)
        result->couponNames(lib::SharedSlice<String>(*source.couponNames));
    if (source.castRanges) result->castRange(*source.castRanges);
    if (source.type == CType::BranchKeyCode && source.effectCardTypes) {
        result->targetIsSkill(false);
        result->targetIsItem(false);
        result->targetIsBeast(false);
        result->targetIsHand(false);
        for (const auto type : *source.effectCardTypes) {
            using E = types::EffectCardType;
            switch (type) {
            case E::All:
                result->targetIsSkill(true);
                result->targetIsItem(true);
                result->targetIsBeast(true);
                break; // AllはHandを含まず、以前のHandもリセットしない。
            case E::Skill: result->targetIsSkill(true); break;
            case E::Item: result->targetIsItem(true); break;
            case E::Beast: result->targetIsBeast(true); break;
            case E::Hand: result->targetIsHand(true); break;
            default: throw std::invalid_argument("Invalid prepared effect card type");
            }
        }
    }
    if (!source.areaValues.empty())
        result->area(unsignedValue(source.areaValues.front()));
    if (!source.backgrounds.empty()) {
        lib::Array<background::BgImagePtr> copies;
        for (const auto& image : source.backgrounds)
            if (image) copies.push_back(image->dup());
        result->backgroundImages(std::move(copies));
    }
    if (!source.motions.empty()) {
        lib::Array<lib::Owner<motion::Motion>> copies;
        copies.reserve(source.motions.size());
        for (const auto& value : source.motions)
            if (value) copies.push_back(value->dup());
        result->motions(std::move(copies));
    }
    // テキスト専用の互換レコードはargumentKinds以前の形式。
    // 準備済みのコンパイラーレコードは上記の厳密な型付き経路を通過済みな
    // ので、旧形式の強制変換を再適用して+1や構造化値を上書きしない。
    if (source.argumentKinds.empty()) switch (source.type) {
    case CType::ChangeArea:
        if (!arg0.empty()) result->area(unsignedValue(arg0));
        break;
    case CType::StartBattle:
        if (!arg0.empty()) result->battle(unsignedValue(arg0));
        break;
    case CType::LinkPackage:
    case CType::CallPackage:
        if (!arg0.empty()) result->packages(unsignedValue(arg0));
        break;
    case CType::ChangeBgImage:
    case CType::ReplaceBgImage:
    case CType::MoveBgImage:
    case CType::LoseBgImage:
        break;
    case CType::Wait:
        if (!arg0.empty()) result->wait(static_cast<std::int32_t>(integerValue(arg0)));
        break;
    case CType::GetMoney:
    case CType::LoseMoney:
    case CType::BranchMoney:
        if (!arg0.empty()) result->money(static_cast<std::int32_t>(integerValue(arg0)));
        break;
    case CType::SetFlag:
    case CType::ReverseFlag:
    case CType::CheckFlag:
    case CType::BranchFlag:
    case CType::BranchFlagCmp:
        if (!arg0.empty()) result->flag(arg0);
        break;
    case CType::SetStep:
    case CType::SetStepUp:
    case CType::SetStepDown:
    case CType::CheckStep:
    case CType::BranchStep:
    case CType::BranchStepCmp:
        if (!arg0.empty()) result->step(arg0);
        break;
    case CType::BranchVariant:
    case CType::SetVariant:
    case CType::CheckVariant:
        if (!arg0.empty()) result->variant(arg0);
        break;
    case CType::CustomXmlContent:
        result->xmlType(source.keyword);
        result->xmlStr(source.condition);
        break;
    default:
        break;
    }
    for (const auto& child : source.children) {
        // 元実装(D)に合わせ、子の追加は prop 付き add で名前を正規化する。
        if (auto converted = fromScript(child, prop)) result->add(prop, converted);
    }
    return result;
}

script::Content ContentInitialValueEditor::toScript(const Content& source,
                                                     const bool shallow) {
    // 整形とコマンド表記はscript::CWXScriptが管
    // 理する。ここはfromScriptの単一値CArg処理の逆
    // 変換で、構造化値は下記で文法の括弧・空白形式へ出力する。
    const auto quote = [](const StringView value) {
        return script::CWXScript::createString(value);
    };
    const auto quote_path = [&quote](const StringView value) {
        String canonical(value);
        std::replace(canonical.begin(), canonical.end(), '\\', '/');
        return quote(canonical);
    };
    const auto number = [](const auto value) { return std::to_string(value); };
    const auto boolean = [](const bool value) { return value ? String{"true"} : String{"false"}; };
    // 元実装(D)の toAttr が出力する CWXScript 上の綴り(例: CardVisual::Horizontal は
    // "hswing"、StartAction::NextRound は "next"、比較記号は引用符付き)。C++ は列挙名を
    // 小文字化した綴りを出していたため、Dや本パーサで読み戻せない綴り(hswing 等)があった。
    // 表は src/cwxeditor_src_d/core/cwx/script.d の toAttr から機械的に生成している。
    const auto d_spelling = [](const auto value) -> std::optional<String> {
        using Value = std::remove_cvref_t<decltype(value)>;
        {
            if constexpr (std::is_same_v<Value, types::BlendMode>) {
                switch (value) {
                case types::BlendMode::Normal: return String{"normal"};
                case types::BlendMode::Mask: return String{"normal"};
                case types::BlendMode::Add: return String{"add"};
                case types::BlendMode::Subtract: return String{"sub"};
                case types::BlendMode::Multiply: return String{"mul"};
                default: break;
                }
            }
            if constexpr (std::is_same_v<Value, types::CardVisual>) {
                switch (value) {
                case types::CardVisual::None: return String{"none"};
                case types::CardVisual::Reverse: return String{"reverse"};
                case types::CardVisual::Horizontal: return String{"hswing"};
                case types::CardVisual::Vertical: return String{"vswing"};
                default: break;
                }
            }
            if constexpr (std::is_same_v<Value, types::CoordinateType>) {
                switch (value) {
                case types::CoordinateType::None: return String{"none"};
                case types::CoordinateType::Absolute: return String{"abs"};
                case types::CoordinateType::Relative: return String{"rel"};
                case types::CoordinateType::Percentage: return String{"per"};
                default: break;
                }
            }
            if constexpr (std::is_same_v<Value, types::EffectType>) {
                switch (value) {
                case types::EffectType::Physic: return String{"physic"};
                case types::EffectType::Magic: return String{"magic"};
                case types::EffectType::MagicalPhysic: return String{"mphysic"};
                case types::EffectType::PhysicalMagic: return String{"pmagic"};
                case types::EffectType::None: return String{"none"};
                default: break;
                }
            }
            if constexpr (std::is_same_v<Value, types::EnvironmentStatus>) {
                switch (value) {
                case types::EnvironmentStatus::NotSet: return String{"none"};
                case types::EnvironmentStatus::Enable: return String{"on"};
                case types::EnvironmentStatus::Disable: return String{"off"};
                default: break;
                }
            }
            if constexpr (std::is_same_v<Value, types::GradientDir>) {
                switch (value) {
                case types::GradientDir::None: return String{"none"};
                case types::GradientDir::LeftToRight: return String{"horizontal"};
                case types::GradientDir::TopToBottom: return String{"vertical"};
                default: break;
                }
            }
            if constexpr (std::is_same_v<Value, types::MatchingCondition>) {
                switch (value) {
                case types::MatchingCondition::Has: return String{"has"};
                case types::MatchingCondition::HasNot: return String{"hasnot"};
                default: break;
                }
            }
            if constexpr (std::is_same_v<Value, types::Mental>) {
                switch (value) {
                case types::Mental::Aggressive: return String{"agg"};
                case types::Mental::Unaggressive: return String{"unagg"};
                case types::Mental::Cheerful: return String{"cheerf"};
                case types::Mental::Uncheerful: return String{"uncheerf"};
                case types::Mental::Brave: return String{"brave"};
                case types::Mental::Unbrave: return String{"unbrave"};
                case types::Mental::Cautious: return String{"caut"};
                case types::Mental::Uncautious: return String{"uncaut"};
                case types::Mental::Trickish: return String{"trick"};
                case types::Mental::Untrickish: return String{"untrick"};
                default: break;
                }
            }
            if constexpr (std::is_same_v<Value, types::StartAction>) {
                switch (value) {
                case types::StartAction::Now: return String{"now"};
                case types::StartAction::CurrentRound: return String{"current"};
                case types::StartAction::NextRound: return String{"next"};
                default: break;
                }
            }
            if constexpr (std::is_same_v<Value, types::Status>) {
                switch (value) {
                case types::Status::Active: return String{"active"};
                case types::Status::Inactive: return String{"inactive"};
                case types::Status::Alive: return String{"alive"};
                case types::Status::Dead: return String{"dead"};
                case types::Status::Fine: return String{"fine"};
                case types::Status::Injured: return String{"injured"};
                case types::Status::HeavyInjured: return String{"heavyinjured"};
                case types::Status::Unconscious: return String{"unconscious"};
                case types::Status::Poison: return String{"poison"};
                case types::Status::Sleep: return String{"sleep"};
                case types::Status::Bind: return String{"bind"};
                case types::Status::Paralyze: return String{"paralyze"};
                case types::Status::Confuse: return String{"confuse"};
                case types::Status::Overheat: return String{"overheat"};
                case types::Status::Brave: return String{"brave"};
                case types::Status::Panic: return String{"panic"};
                case types::Status::Silence: return String{"silence"};
                case types::Status::FaceUp: return String{"faceup"};
                case types::Status::AntiMagic: return String{"antimagic"};
                case types::Status::UpAction: return String{"upaction"};
                case types::Status::UpAvoid: return String{"upavoid"};
                case types::Status::UpResist: return String{"upresist"};
                case types::Status::UpDefense: return String{"updefense"};
                case types::Status::DownAction: return String{"downaction"};
                case types::Status::DownAvoid: return String{"downavoid"};
                case types::Status::DownResist: return String{"downresist"};
                case types::Status::DownDefense: return String{"downdefense"};
                case types::Status::None: return String{"none"};
                default: break;
                }
            }
            if constexpr (std::is_same_v<Value, types::Transition>) {
                switch (value) {
                case types::Transition::Default: return String{"default"};
                case types::Transition::None: return String{"none"};
                case types::Transition::Blinds: return String{"thread"};
                case types::Transition::PixelDissolve: return String{"shave"};
                case types::Transition::Fade: return String{"fade"};
                default: break;
                }
            }
            if constexpr (std::is_same_v<Value, types::AbsorbTo>) {
                switch (value) {
                case types::AbsorbTo::None: return String{"none"};
                case types::AbsorbTo::Selected: return String{"M"};
                default: break;
                }
            }
            if constexpr (std::is_same_v<Value, types::Comparison4>) {
                switch (value) {
                case types::Comparison4::Eq: return String{"\"=\""};
                case types::Comparison4::Ne: return String{"\"<>\""};
                case types::Comparison4::Lt: return String{"\">\""};
                case types::Comparison4::Gt: return String{"\"<\""};
                default: break;
                }
            }
            if constexpr (std::is_same_v<Value, types::Comparison3>) {
                switch (value) {
                case types::Comparison3::Eq: return String{"\"=\""};
                case types::Comparison3::Lt: return String{"\">\""};
                case types::Comparison3::Gt: return String{"\"<\""};
                default: break;
                }
            }
        }
        return std::nullopt;
    };
    const auto enum_value = [&d_spelling](const auto value) {
        using Value = std::remove_cvref_t<decltype(value)>;
        if (const auto spelled = d_spelling(value)) return *spelled;
        auto result = [&] {
            if constexpr (std::is_same_v<Value, types::Talker>)
                return types::fromTalker(value);
            else if constexpr (std::is_same_v<Value, types::Range>)
                return types::fromRange(value);
            else if constexpr (std::is_same_v<Value, types::Comparison4>)
                return types::fromComparison4(value);
            else if constexpr (std::is_same_v<Value, types::Comparison3>)
                return types::fromComparison3(value);
            else if constexpr (std::is_same_v<Value, types::CoordinateType>)
                return types::fromCoordinateType(value);
            else if constexpr (std::is_same_v<Value, types::BlendMode>)
                return types::fromBlendMode(value);
            else if constexpr (std::is_same_v<Value, types::GradientDir>)
                return types::fromGradientDir(value);
            else if constexpr (std::is_same_v<Value, types::Smoothing>)
                return types::fromSmoothing(value);
            else if constexpr (std::is_same_v<Value, types::UpdateType>)
                return types::fromUpdateType(value);
            else if constexpr (std::is_same_v<Value, types::CardVisual>)
                return types::fromCardVisual(value);
            else if constexpr (std::is_same_v<Value, types::SelectionMethod>)
                return types::fromSelectionMethod(value);
            else if constexpr (std::is_same_v<Value, types::EffectType>)
                return types::fromEffectType(value);
            else if constexpr (std::is_same_v<Value, types::Resist>)
                return types::fromResist(value);
            else if constexpr (std::is_same_v<Value, types::Status>)
                return types::fromStatus(value);
            else if constexpr (std::is_same_v<Value, types::Physical>)
                return types::fromPhysical(value);
            else if constexpr (std::is_same_v<Value, types::Mental>)
                return types::fromMental(value);
            else if constexpr (std::is_same_v<Value, types::Transition>)
                return types::fromTransition(value);
            else if constexpr (std::is_same_v<Value, types::MatchingCondition>)
                return types::fromMatchingCondition(value);
            else if constexpr (std::is_same_v<Value, types::StartAction>)
                return types::fromStartAction(value);
            else if constexpr (std::is_same_v<Value, types::MatchingType>)
                return types::fromMatchingType(value);
            else if constexpr (std::is_same_v<Value, types::EnvironmentStatus>)
                return types::fromEnvironmentStatus(value);
            else if constexpr (std::is_same_v<Value, types::AbsorbTo>) {
                switch (value) {
                case types::AbsorbTo::None: return String{"None"};
                case types::AbsorbTo::Selected: return String{"Selected"};
                }
                throw std::invalid_argument("value is not an AbsorbTo member");
            }
            else
                return enumutils::enumToString(value);
        }();
        if (!result.empty() && result.front() >= 'A' && result.front() <= 'Z')
            result.front() = static_cast<char>(result.front() - 'A' + 'a');
        return result;
    };
    const auto joined = [](const auto& values, const auto& render) {
        String result;
        for (const auto& value : values) {
            if (!result.empty()) result.push_back(' ');
            result += render(value);
        }
        return result;
    };

    script::Content result;
    result.type = source.type();
    result.typedCardImages = source.cardImages();
    // CWXScriptがSummaryの状態を得るまで、型付きの効果内容・Beastの同一性を維持する。
    result.motions.reserve(source.motionsRef().size());
    for (const auto& motion : source.motionsRef()) result.motions.emplace_back(motion);
    result.name = source.name();
    result.comment = source.comment();
    const auto add = [&](const CArg kind, String value) {
        result.argumentKinds.push_back(kind);
        result.arguments.push_back(std::move(value));
    };
    const auto detail = source.detail();
    // script.cppのargumentLayoutと同じ順序。EventTreeView
    // がコンパイラーと同じCArg順を使うよう明示する。
    if (detail.use(CArg::TalkerC)) {
        const auto& paths = source.cardPaths();
        const auto talker_image = [&](const auto& image) {
            if (!image) return String{};
            switch (image->type) {
            case types::CardImageType::File:
                switch (image->positionType()) {
                case types::CardImagePosition::Center:
                    return "[" + quote_path(image->path()) + ", center]";
                case types::CardImagePosition::TopLeft:
                    return "[" + quote_path(image->path()) + ", topleft]";
                case types::CardImagePosition::Default:
                    return quote_path(image->path());
                }
                break;
            case types::CardImageType::PCNumber:
                // DはCWXScriptからPC番号画像を意図的に省略する。
                return String{};
            case types::CardImageType::Talker:
                switch (image->talker()) {
                case types::Talker::Selected: return String{"M"};
                case types::Talker::Unselected: return String{"U"};
                case types::Talker::Random: return String{"R"};
                case types::Talker::Card: return String{"C"};
                case types::Talker::Valued: return String{"V"};
                }
                break;
            }
            return String{};
        };
        String images;
        for (const auto& image : source.cardImages()) {
            const auto value = talker_image(image);
            if (value.empty()) continue;
            if (!images.empty()) images.push_back(' ');
            images += value;
        }
        add(CArg::TalkerC, source.singleLine() ? "single" :
            (!images.empty() ? images : paths.empty() ? "none" : joined(paths, quote_path)));
    }
    if (detail.use(CArg::SingleLine) && !detail.use(CArg::TalkerC))
        add(CArg::SingleLine, boolean(source.singleLine()));
    if (detail.use(CArg::Text)) add(CArg::Text, quote(source.text()));
    if (detail.use(CArg::TalkerNC)) {
        // DのtoAttr(c.talkerNC)からtoAttrTalker([CardImage(talker)])
        // への変換は、話者をM/U/R/C/Vで表す。他の列挙引数の小文字の列挙名とは異なる。
        String spelled;
        switch (source.talkerNC()) {
        case types::Talker::Selected: spelled = "M"; break;
        case types::Talker::Unselected: spelled = "U"; break;
        case types::Talker::Random: spelled = "R"; break;
        case types::Talker::Card: spelled = "C"; break;
        case types::Talker::Valued: spelled = "V"; break;
        }
        add(CArg::TalkerNC, spelled);
    }
    if (detail.use(CArg::Dialogs)) {
        String dialogs;
        for (const auto& dialog : source.dialogs()) {
            if (!dialog) continue;
            if (!dialogs.empty()) dialogs.push_back(' ');
            // Dはクーポン名にセミコロンがなければ、セミコロン区
            // 切りの一つの引用文字列で書く。含む場合は別々の値
            // で書き、名前を損失なく往復変換できるようにする。
            bool separate = false;
            for (const auto& coupon : dialog->rCoupons())
                separate = separate || coupon.find(';') != String::npos;
            String required;
            if (separate) {
                required = joined(dialog->rCoupons(), quote);
            } else {
                String joined_coupons;
                for (const auto& coupon : dialog->rCoupons()) {
                    if (!joined_coupons.empty()) joined_coupons.push_back(';');
                    joined_coupons += coupon;
                }
                required = quote(joined_coupons);
            }
            dialogs += "[" + required + ", " + quote(dialog->text()) + "]";
        }
        add(CArg::Dialogs, std::move(dialogs));
    }
    if (detail.use(CArg::CellName)) add(CArg::CellName, quote(source.cellName()));
    if (detail.use(CArg::CardGroup)) add(CArg::CardGroup, quote(source.cardGroup()));
    if (detail.use(CArg::PositionType)) add(CArg::PositionType, enum_value(source.positionType()));
    if (detail.use(CArg::X)) add(CArg::X, number(source.x()));
    if (detail.use(CArg::Y)) add(CArg::Y, number(source.y()));
    if (detail.use(CArg::SizeType)) add(CArg::SizeType, enum_value(source.sizeType()));
    if (detail.use(CArg::Width)) add(CArg::Width, number(source.width()));
    if (detail.use(CArg::Height)) add(CArg::Height, number(source.height()));
    if (detail.use(CArg::Scale)) add(CArg::Scale, source.scale() == -1 ? "none" : number(source.scale()));
    if (detail.use(CArg::Layer)) add(CArg::Layer, source.layer() == -1 ? "none" : number(source.layer()));
    if (detail.use(CArg::BgImages)) {
        // DのtoAttr!(BgImage)は具体的なセルと共通の配置情報を保
        // 持する。backs()は従来のパスキャッシュだけなので、型付き集合を
        // 使い、スクリプトとしてコピーしてもテキスト・色・PCセルを維持する。
        const auto color = [&quote](const structs::CRGB& value) {
            static constexpr char hex[] = "0123456789ABCDEF";
            const auto byte = [&](const std::uint32_t component, String& out) {
                out.push_back(hex[(component >> 4U) & 0x0FU]);
                out.push_back(hex[component & 0x0FU]);
            };
            String result{"#"};
            byte(value.r, result); byte(value.g, result); byte(value.b, result);
            if (value.a != 255U) byte(value.a, result);
            return quote(result);
        };
        const auto background_text = [&](const auto& owner) {
            if (!owner) return String{"[]"};
            const auto& cell = *owner;
            String values;
            const auto append = [&](String value) {
                if (!values.empty()) values += ", ";
                values += std::move(value);
            };
            if (const auto* image = dynamic_cast<const background::ImageCell*>(&cell)) {
                append(quote_path(image->path()));
            } else if (const auto* text = dynamic_cast<const background::TextCell*>(&cell)) {
                append("text"); append(quote(text->text()));
                append(quote(text->fontName())); append(number(text->size()));
                append(color(text->color()));
                String style;
                if (text->bold()) style += "bold";
                if (text->italic()) style += (style.empty() ? "italic" : " italic");
                if (text->underline()) style += (style.empty() ? "underline" : " underline");
                if (text->strike()) style += (style.empty() ? "strike" : " strike");
                if (text->vertical()) style += (style.empty() ? "vertical" : " vertical");
                if (text->antialias()) style += (style.empty() ? "antialias" : " antialias");
                if (text->borderingType() == types::BorderingType::Outline)
                    style += (style.empty() ? "border1" : " border1");
                if (text->borderingType() == types::BorderingType::Inline)
                    style += (style.empty() ? "border2" : " border2");
                append(style); // 空のスタイルも、DSL配列の位置を持つ項目として残す。
                if (text->borderingType() != types::BorderingType::None) {
                    append(color(text->borderingColor()));
                    if (text->borderingType() == types::BorderingType::Inline)
                        append(number(text->borderingWidth()));
                }
            } else if (const auto* solid = dynamic_cast<const background::ColorCell*>(&cell)) {
                append("color"); append(enum_value(solid->blendMode()));
                append(color(solid->color1()));
                if (solid->gradientDir() != types::GradientDir::None) {
                    append(enum_value(solid->gradientDir()));
                    append(color(solid->color2()));
                }
            } else if (const auto* pc = dynamic_cast<const background::PCCell*>(&cell)) {
                append("pc"); append(number(pc->pcNumber())); append(boolean(pc->expand()));
            }
            append(quote(cell.flag())); append(number(cell.x())); append(number(cell.y()));
            append(number(cell.width())); append(number(cell.height()));
            if (const auto* image = dynamic_cast<const background::ImageCell*>(&cell))
                append(boolean(image->mask()));
            append(quote(cell.cellName())); append(number(cell.layer()));
            if (const auto* image = dynamic_cast<const background::ImageCell*>(&cell))
                append(enum_value(image->smoothing()));
            if (const auto* pc = dynamic_cast<const background::PCCell*>(&cell))
                append(enum_value(pc->smoothing()));
            if (const auto* text = dynamic_cast<const background::TextCell*>(&cell))
                append(enum_value(text->updateType()));
            return "[" + values + "]";
        };
        const auto& backgrounds = source.backgroundImages();
        add(CArg::BgImages, backgrounds.empty()
                                ? joined(source.backs(), quote)
                                : joined(backgrounds, background_text));
    }
    if (detail.use(CArg::TargetS)) {
        // 型付きモデルは一つのTargetとして保存するが、argumentLayout
        // は対象と睡眠の項目を分けて維持する。
        const auto target_name = [&] {
            switch (source.targetS().m) {
            case types::Target::M::Selected: return String{"M"};
            case types::Target::M::Unselected: return String{"U"};
            case types::Target::M::Random: return String{"R"};
            case types::Target::M::Party: return String{"T"};
            }
            return String{"selected"};
        };
        add(CArg::TargetS, target_name());
        add(CArg::TargetS, boolean(source.targetS().sleep));
    }
    if (detail.use(CArg::Range)) {
        // Dのスクリプト出力は短い範囲記号を使う。特にRange::Party
        // はTとする。XMLの列挙表記partyでは、生成スクリプトの再
        // 解析時にRange::PartyAndBackpackになる。
        String spelling;
        switch (source.range()) {
        case types::Range::Selected: spelling = "M"; break;
        case types::Range::Random: spelling = "R"; break;
        case types::Range::Party: spelling = "T"; break;
        case types::Range::Backpack: spelling = "backpack"; break;
        case types::Range::PartyAndBackpack: spelling = "party"; break;
        case types::Range::Field: spelling = "field"; break;
        case types::Range::CouponHolder: spelling = "coupon"; break;
        case types::Range::CardTarget: spelling = "card"; break;
        case types::Range::SelectedCard: spelling = "selcard"; break;
        case types::Range::Npc: spelling = "npc"; break;
        default: throw std::invalid_argument("Invalid Range value");
        }
        add(CArg::Range, std::move(spelling));
    }
    if (detail.use(CArg::CastRange))
        for (const auto range : source.castRange()) add(CArg::CastRange, enum_value(range));
    if (detail.use(CArg::KeyCodeRange)) {
        auto spelling = enum_value(source.keyCodeRange());
        if (source.type() == CType::BranchKeyCode) {
            // Dのscript.d:3404-3412はXMLの列挙名ではなくスクリプトの
            // キーワードを使う。この補正はBranchKeyCode引数だけに適用する。
            switch (source.keyCodeRange()) {
            case types::Range::Selected: spelling = "M"; break;
            case types::Range::Random: spelling = "R"; break;
            case types::Range::Backpack: spelling = "backpack"; break;
            case types::Range::PartyAndBackpack: spelling = "party"; break;
            case types::Range::SelectedCard: spelling = "selcard"; break;
            default: break;
            }
        }
        add(CArg::KeyCodeRange, std::move(spelling));
    }
    if (detail.use(CArg::TargetIsSkill) || detail.use(CArg::TargetIsItem) ||
        detail.use(CArg::TargetIsBeast) || detail.use(CArg::TargetIsHand)) {
        String types;
        if (source.targetIsSkill()) types += "skill";
        if (source.targetIsItem()) types += (types.empty() ? "item" : " item");
        if (source.targetIsBeast()) types += (types.empty() ? "beast" : " beast");
        if (source.targetIsHand()) types += (types.empty() ? "hand" : " hand");
        add(CArg::TargetIsSkill, std::move(types));
    }
    if (detail.use(CArg::HoldingCoupon) && source.range() == types::Range::CouponHolder)
        add(CArg::HoldingCoupon, quote(source.holdingCoupon()));
    if (detail.use(CArg::Area)) add(CArg::Area, number(source.area()));
    if (detail.use(CArg::Battle)) add(CArg::Battle, number(source.battle()));
    if (detail.use(CArg::Package)) add(CArg::Package, number(source.packages()));
    if (detail.use(CArg::Cast)) add(CArg::Cast, number(source.casts()));
    if (detail.use(CArg::Item)) add(CArg::Item, number(source.item()));
    if (detail.use(CArg::Skill)) add(CArg::Skill, number(source.skill()));
    if (detail.use(CArg::Info)) add(CArg::Info, number(source.info()));
    if (detail.use(CArg::Beast)) add(CArg::Beast, number(source.beast()));
    if (detail.use(CArg::Start)) add(CArg::Start, quote(source.start()));
    if (detail.use(CArg::Complete)) add(CArg::Complete, boolean(source.complete()));
    if (detail.use(CArg::Money)) add(CArg::Money, number(source.money()));
    if (detail.use(CArg::Coupon)) add(CArg::Coupon, quote(source.coupon()));
    if (detail.use(CArg::CouponValue)) add(CArg::CouponValue, number(source.couponValue()));
    if (detail.use(CArg::CompleteStamp)) add(CArg::CompleteStamp, quote(source.completeStamp()));
    if (detail.use(CArg::KeyCode)) add(CArg::KeyCode, quote(source.keyCode()));
    if (detail.use(CArg::Gossip)) add(CArg::Gossip, quote(source.gossip()));
    // Flag・Step・Variantを持つCTypeは、Dの短
    // い変数種別形式を使う。コモンの文字列だけ、またはstep/flag
    // 記号とそのパスの組であり、三つの位置引数を作らない。
    if (detail.use(CArg::Flag) && detail.use(CArg::Step) && detail.use(CArg::Variant)) {
        if (!source.variant().empty())
            add(CArg::Variant, quote(source.variant()));
        else if (!source.step().empty())
            add(CArg::Step, "step, " + quote(source.step()));
        else if (!source.flag().empty())
            add(CArg::Flag, "flag, " + quote(source.flag()));
        else
            // Dの空変数の分岐は、引用符付き空文字列を一つ出力し、flag
            // 記号は付けない。flagと空文字列を出すと文法が変わる。
            add(CArg::Variant, quote(""));
    } else {
        if (detail.use(CArg::Flag)) add(CArg::Flag, quote(source.flag()));
        if (detail.use(CArg::Step)) add(CArg::Step, quote(source.step()));
        if (detail.use(CArg::Variant)) add(CArg::Variant, quote(source.variant()));
    }
    if (detail.use(CArg::Flag2)) add(CArg::Flag2, quote(source.flag2()));
    if (detail.use(CArg::Step2)) add(CArg::Step2, quote(source.step2()));
    if (detail.use(CArg::Comparison4)) add(CArg::Comparison4, enum_value(source.comparison4()));
    if (detail.use(CArg::Comparison3)) add(CArg::Comparison3, enum_value(source.comparison3()));
    if (detail.use(CArg::FlagValue)) add(CArg::FlagValue, boolean(source.flagValue()));
    if (detail.use(CArg::StepValue)) add(CArg::StepValue, number(source.stepValue()));
    if (detail.use(CArg::CardNumber)) add(CArg::CardNumber, number(source.cardNumber()));
    if (detail.use(CArg::SelectCard)) add(CArg::SelectCard, boolean(source.selectCard()));
    if (detail.use(CArg::Motions)) {
        const auto motion_text = [](const auto& motion) {
            return motion ? script::motionToScript(*motion) : String{"[]"};
        };
        add(CArg::Motions, joined(source.motionsRef(), motion_text));
    }
    if (detail.use(CArg::CardVisual))
        add(CArg::CardVisual, enum_value(source.cardVisual()));
    if (detail.use(CArg::UnsignedLevel)) add(CArg::UnsignedLevel, number(source.unsignedLevel()));
    if (detail.use(CArg::SignedLevel)) add(CArg::SignedLevel, number(source.signedLevel()));
    if (detail.use(CArg::LevelMin)) add(CArg::LevelMin, number(source.levelMin()));
    if (detail.use(CArg::LevelMax)) add(CArg::LevelMax, number(source.levelMax()));
    if (detail.use(CArg::Wait)) add(CArg::Wait, number(source.wait()));
    if (detail.use(CArg::Percent)) add(CArg::Percent, number(source.percent()));
    if (detail.use(CArg::TargetAll)) add(CArg::TargetAll, boolean(source.targetAll()));
    if (detail.use(CArg::SelectionMethod)) add(CArg::SelectionMethod, enum_value(source.selectionMethod()));
    if (detail.use(CArg::Average)) add(CArg::Average, boolean(source.average()));
    if (detail.use(CArg::PartyNumber)) add(CArg::PartyNumber, number(source.partyNumber()));
    if (detail.use(CArg::SuccessRate)) add(CArg::SuccessRate, number(source.successRate()));
    if (detail.use(CArg::EffectType)) add(CArg::EffectType, enum_value(source.effectType()));
    if (detail.use(CArg::Resist)) add(CArg::Resist, enum_value(source.resist()));
    if (detail.use(CArg::Status)) add(CArg::Status, enum_value(source.status()));
    if (detail.use(CArg::BgmPath)) add(CArg::BgmPath,
        source.bgmPath().empty() ? "stop" : quote(source.bgmPath()));
    if (detail.use(CArg::BgmChannel)) add(CArg::BgmChannel, number(source.bgmChannel()));
    if (detail.use(CArg::BgmFadeIn)) add(CArg::BgmFadeIn,
        utils::formatReal(static_cast<double>(source.bgmFadeIn()) / 100.0, 3));
    if (detail.use(CArg::BgmVolume)) add(CArg::BgmVolume, number(source.bgmVolume()));
    if (detail.use(CArg::BgmLoopCount)) add(CArg::BgmLoopCount, number(source.bgmLoopCount()));
    if (detail.use(CArg::SoundPath)) add(CArg::SoundPath, quote(source.soundPath()));
    if (detail.use(CArg::SoundChannel)) add(CArg::SoundChannel, number(source.soundChannel()));
    if (detail.use(CArg::SoundFadeIn)) add(CArg::SoundFadeIn,
        utils::formatReal(static_cast<double>(source.soundFadeIn()) / 100.0, 3));
    if (detail.use(CArg::SoundVolume)) add(CArg::SoundVolume, number(source.soundVolume()));
    if (detail.use(CArg::SoundLoopCount)) add(CArg::SoundLoopCount, number(source.soundLoopCount()));
    if (detail.use(CArg::RefAbility)) add(CArg::RefAbility, boolean(source.refAbility()));
    if (detail.use(CArg::Physical)) add(CArg::Physical, enum_value(source.physical()));
    if (detail.use(CArg::Mental)) add(CArg::Mental, enum_value(source.mental()));
    if (detail.use(CArg::Round)) add(CArg::Round, number(source.round()));
    if (detail.use(CArg::TransitionSpeed)) add(CArg::TransitionSpeed, number(source.transitionSpeed()));
    if (detail.use(CArg::Transition)) add(CArg::Transition, enum_value(source.transition()));
    if (detail.use(CArg::DoAnime)) add(CArg::DoAnime, boolean(source.doAnime()));
    if (detail.use(CArg::IgnoreEffectBooster)) add(CArg::IgnoreEffectBooster,
        boolean(source.ignoreEffectBooster()));
    const bool valued_choice = source.talkerNC() == types::Talker::Valued ||
                               source.selectionMethod() == types::SelectionMethod::Valued;
    if (valued_choice && detail.use(CArg::InitValue))
        add(CArg::InitValue, number(source.initValue()));
    if (valued_choice && detail.use(CArg::Coupons)) {
        const auto coupon_text = [&](const auto& coupon) {
            return coupon ? "[" + quote(coupon->name()) + ", " +
                                number(coupon->value()) + "]"
                          : String{"[]"};
        };
        add(CArg::Coupons, joined(source.couponRecords(), coupon_text));
    }
    if (detail.use(CArg::SelectionColumns)) add(CArg::SelectionColumns,
        number(source.selectionColumns()));
    if (detail.use(CArg::BoundaryCheck)) add(CArg::BoundaryCheck, boolean(source.boundaryCheck()));
    if (detail.use(CArg::CenteringX)) add(CArg::CenteringX, boolean(source.centeringX()));
    if (detail.use(CArg::CenteringY)) add(CArg::CenteringY, boolean(source.centeringY()));
    if (detail.use(CArg::SelectTalker)) add(CArg::SelectTalker, boolean(source.selectTalker()));
    if (detail.use(CArg::InvertResult)) add(CArg::InvertResult, boolean(source.invertResult()));
    if (detail.use(CArg::MatchingCondition)) add(CArg::MatchingCondition,
        enum_value(source.matchingCondition()));
    if (detail.use(CArg::StartAction)) add(CArg::StartAction, enum_value(source.startAction()));
    if (detail.use(CArg::Ignite)) add(CArg::Ignite, boolean(source.ignite()));
    if (detail.use(CArg::KeyCodes)) add(CArg::KeyCodes, joined(source.keyCodes(), quote));
    if (detail.use(CArg::CouponNames)) add(CArg::CouponNames, joined(source.couponNames(), quote));
    if (detail.use(CArg::MatchingType)) add(CArg::MatchingType, enum_value(source.matchingType()));
    if (detail.use(CArg::ExpandSPChars)) add(CArg::ExpandSPChars, boolean(source.expandSPChars()));
    if (detail.use(CArg::ConsumeCard)) add(CArg::ConsumeCard, boolean(source.consumeCard()));
    if (detail.use(CArg::CardSpeed)) add(CArg::CardSpeed,
        source.cardSpeed() == -1 ? "default" : number(source.cardSpeed()));
    if (detail.use(CArg::OverrideCardSpeed)) add(CArg::OverrideCardSpeed,
        boolean(source.overrideCardSpeed()));
    if (detail.use(CArg::InitialEffect)) add(CArg::InitialEffect, boolean(source.initialEffect()));
    if (detail.use(CArg::InitialSoundPath)) add(CArg::InitialSoundPath, quote(source.initialSoundPath()));
    if (detail.use(CArg::InitialSoundChannel)) add(CArg::InitialSoundChannel,
        number(source.initialSoundChannel()));
    if (detail.use(CArg::InitialSoundFadeIn)) add(CArg::InitialSoundFadeIn,
        utils::formatReal(static_cast<double>(source.initialSoundFadeIn()) / 100.0, 3));
    if (detail.use(CArg::InitialSoundVolume)) add(CArg::InitialSoundVolume,
        number(source.initialSoundVolume()));
    if (detail.use(CArg::InitialSoundLoopCount)) add(CArg::InitialSoundLoopCount,
        number(source.initialSoundLoopCount()));
    if (detail.use(CArg::AbsorbTo)) add(CArg::AbsorbTo, enum_value(source.absorbTo()));
    if (detail.use(CArg::BackpackEnabled)) add(CArg::BackpackEnabled,
        enum_value(source.backpackEnabled()));
    if (detail.use(CArg::GameOverEnabled)) add(CArg::GameOverEnabled,
        enum_value(source.gameOverEnabled()));
    if (detail.use(CArg::RunAwayEnabled)) add(CArg::RunAwayEnabled,
        enum_value(source.runAwayEnabled()));
    // Dは式を一重引用符で直列化し、式内の通常の二重引
    // 用符付き文字列を読みやすく、損失なく保持する。
    if (detail.use(CArg::Expression))
        add(CArg::Expression, script::CWXScript::createString(source.expression(), true));
    if (detail.use(CArg::XmlType)) add(CArg::XmlType, quote(source.xmlType()));
    if (detail.use(CArg::XmlStr)) add(CArg::XmlStr, quote(source.xmlStr()));
    if (detail.use(CArg::XmlAssignment)) add(CArg::XmlAssignment, number(source.xmlAssignment()));
    if (detail.use(CArg::XmlVariable)) add(CArg::XmlVariable, quote(source.xmlVariable()));
    if (!shallow) {
        for (const auto& child : source.next())
            if (child) result.children.push_back(toScript(*child));
    }
    return result;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::contentinitializer
