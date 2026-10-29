#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::types {

MotionException::MotionException(::cwxeditor::lib::String msg)
    : std::runtime_error(std::move(msg)) {}

Target Target::opCall(M m, bool sleep) {
    Target result;
    result.m = m;
    result.sleep = sleep;
    return result;
}

bool Target::opEquals(const Target& t) const noexcept {
    return t.m == m && t.sleep == sleep;
}

bool Target::operator==(const Target& t) const noexcept {
    return opEquals(t);
}

Mentality toMentality(::cwxeditor::lib::StringView name) {
    if (name == "Normal") return Mentality::Normal;
    if (name == "Panic") return Mentality::Panic;
    if (name == "Brave") return Mentality::Brave;
    if (name == "Overheat") return Mentality::Overheat;
    if (name == "Confuse") return Mentality::Confuse;
    if (name == "Sleep") return Mentality::Sleep;
    throw std::runtime_error(std::string("Unknown Mentality: ") + std::string(name));
}

::cwxeditor::lib::String fromMentality(Mentality value) {
    switch (value) {
    case Mentality::Normal:
        return "Normal";
    case Mentality::Panic:
        return "Panic";
    case Mentality::Brave:
        return "Brave";
    case Mentality::Overheat:
        return "Overheat";
    case Mentality::Confuse:
        return "Confuse";
    case Mentality::Sleep:
        return "Sleep";
    default:
        throw std::logic_error("Invalid Mentality value");
    }
}

EffectType toEffectType(::cwxeditor::lib::StringView name) {
    if (name == "Physic") return EffectType::Physic;
    if (name == "Magic") return EffectType::Magic;
    if (name == "MagicalPhysic") return EffectType::MagicalPhysic;
    if (name == "PhysicalMagic") return EffectType::PhysicalMagic;
    if (name == "None") return EffectType::None;
    if (name == "Normal") return EffectType::None;
    throw MotionException(std::string("Unknown effecttype: ") + std::string(name));
}

::cwxeditor::lib::String fromEffectType(EffectType value) {
    switch (value) {
    case EffectType::Physic:
        return "Physic";
    case EffectType::Magic:
        return "Magic";
    case EffectType::MagicalPhysic:
        return "MagicalPhysic";
    case EffectType::PhysicalMagic:
        return "PhysicalMagic";
    case EffectType::None:
        return "None";
    default:
        throw std::logic_error("Invalid EffectType value");
    }
}

Resist toResist(::cwxeditor::lib::StringView name) {
    if (name == "Avoid") return Resist::Avoid;
    if (name == "Resist") return Resist::Resist;
    if (name == "Unfail") return Resist::Unfail;
    throw MotionException(std::string("Unknown resist: ") + std::string(name));
}

::cwxeditor::lib::String fromResist(Resist value) {
    switch (value) {
    case Resist::Avoid:
        return "Avoid";
    case Resist::Resist:
        return "Resist";
    case Resist::Unfail:
        return "Unfail";
    default:
        throw std::logic_error("Invalid Resist value");
    }
}

CardVisual toCardVisual(::cwxeditor::lib::StringView name) {
    if (name == "None") return CardVisual::None;
    if (name == "Reverse") return CardVisual::Reverse;
    if (name == "Horizontal") return CardVisual::Horizontal;
    if (name == "Vertical") return CardVisual::Vertical;
    throw MotionException(std::string("Unknown cardvisual: ") + std::string(name));
}

::cwxeditor::lib::String fromCardVisual(CardVisual value) {
    switch (value) {
    case CardVisual::None:
        return "None";
    case CardVisual::Reverse:
        return "Reverse";
    case CardVisual::Horizontal:
        return "Horizontal";
    case CardVisual::Vertical:
        return "Vertical";
    default:
        throw std::logic_error("Invalid CardVisual value");
    }
}

Element toElement(::cwxeditor::lib::StringView name) {
    if (name == "All") return Element::All;
    if (name == "Health") return Element::Health;
    if (name == "Mind") return Element::Mind;
    if (name == "Miracle") return Element::Miracle;
    if (name == "Magic") return Element::Magic;
    if (name == "Fire") return Element::Fire;
    if (name == "Ice") return Element::Ice;
    throw MotionException(std::string("Unknown element: ") + std::string(name));
}

::cwxeditor::lib::String fromElement(Element value) {
    switch (value) {
    case Element::All:
        return "All";
    case Element::Health:
        return "Health";
    case Element::Mind:
        return "Mind";
    case Element::Miracle:
        return "Miracle";
    case Element::Magic:
        return "Magic";
    case Element::Fire:
        return "Fire";
    case Element::Ice:
        return "Ice";
    default:
        throw std::logic_error("Invalid Element value");
    }
}

DamageType toDamageType(::cwxeditor::lib::StringView name) {
    if (name == "LevelRatio") return DamageType::LevelRatio;
    if (name == "Normal") return DamageType::Normal;
    if (name == "Max") return DamageType::Max;
    if (name == "Fixed") return DamageType::Fixed;
    throw MotionException(std::string("Unknown damagetype: ") + std::string(name));
}

::cwxeditor::lib::String fromDamageType(DamageType value) {
    switch (value) {
    case DamageType::LevelRatio:
        return "LevelRatio";
    case DamageType::Normal:
        return "Normal";
    case DamageType::Max:
        return "Max";
    case DamageType::Fixed:
        return "Fixed";
    default:
        throw std::logic_error("Invalid DamageType value");
    }
}

Mental toMental(::cwxeditor::lib::StringView name) {
    if (name == "Aggressive") return Mental::Aggressive;
    if (name == "Unaggressive") return Mental::Unaggressive;
    if (name == "Cheerful") return Mental::Cheerful;
    if (name == "Uncheerful") return Mental::Uncheerful;
    if (name == "Brave") return Mental::Brave;
    if (name == "Unbrave") return Mental::Unbrave;
    if (name == "Cautious") return Mental::Cautious;
    if (name == "Uncautious") return Mental::Uncautious;
    if (name == "Trickish") return Mental::Trickish;
    if (name == "Untrickish") return Mental::Untrickish;
    throw MotionException(std::string("Unknown mental: ") + std::string(name));
}

::cwxeditor::lib::String fromMental(Mental value) {
    switch (value) {
    case Mental::Aggressive:
        return "Aggressive";
    case Mental::Unaggressive:
        return "Unaggressive";
    case Mental::Cheerful:
        return "Cheerful";
    case Mental::Uncheerful:
        return "Uncheerful";
    case Mental::Brave:
        return "Brave";
    case Mental::Unbrave:
        return "Unbrave";
    case Mental::Cautious:
        return "Cautious";
    case Mental::Uncautious:
        return "Uncautious";
    case Mental::Trickish:
        return "Trickish";
    case Mental::Untrickish:
        return "Untrickish";
    default:
        throw std::logic_error("Invalid Mental value");
    }
}

Physical toPhysical(::cwxeditor::lib::StringView name) {
    if (name == "Dex") return Physical::Dex;
    if (name == "Agl") return Physical::Agl;
    if (name == "Int") return Physical::Int;
    if (name == "Str") return Physical::Str;
    if (name == "Vit") return Physical::Vit;
    if (name == "Min") return Physical::Min;
    throw MotionException(std::string("Unknown physical: ") + std::string(name));
}

::cwxeditor::lib::String fromPhysical(Physical value) {
    switch (value) {
    case Physical::Dex:
        return "Dex";
    case Physical::Agl:
        return "Agl";
    case Physical::Int:
        return "Int";
    case Physical::Str:
        return "Str";
    case Physical::Vit:
        return "Vit";
    case Physical::Min:
        return "Min";
    default:
        throw std::logic_error("Invalid Physical value");
    }
}

Status toStatus(::cwxeditor::lib::StringView name) {
    if (name == "Active") return Status::Active;
    if (name == "Inactive") return Status::Inactive;
    if (name == "Alive") return Status::Alive;
    if (name == "Dead") return Status::Dead;
    if (name == "Fine") return Status::Fine;
    if (name == "Injured") return Status::Injured;
    if (name == "HeavyInjured") return Status::HeavyInjured;
    if (name == "Unconscious") return Status::Unconscious;
    if (name == "Poison") return Status::Poison;
    if (name == "Sleep") return Status::Sleep;
    if (name == "Bind") return Status::Bind;
    if (name == "Paralyze") return Status::Paralyze;
    if (name == "Confuse") return Status::Confuse;
    if (name == "Overheat") return Status::Overheat;
    if (name == "Brave") return Status::Brave;
    if (name == "Panic") return Status::Panic;
    if (name == "Silence") return Status::Silence;
    if (name == "FaceUp") return Status::FaceUp;
    if (name == "AntiMagic") return Status::AntiMagic;
    if (name == "UpAction") return Status::UpAction;
    if (name == "UpAvoid") return Status::UpAvoid;
    if (name == "UpResist") return Status::UpResist;
    if (name == "UpDefense") return Status::UpDefense;
    if (name == "DownAction") return Status::DownAction;
    if (name == "DownAvoid") return Status::DownAvoid;
    if (name == "DownResist") return Status::DownResist;
    if (name == "DownDefense") return Status::DownDefense;
    if (name == "None") return Status::None;
    throw MotionException(std::string("Unknown status: ") + std::string(name));
}

::cwxeditor::lib::String fromStatus(Status value) {
    switch (value) {
    case Status::Active:
        return "Active";
    case Status::Inactive:
        return "Inactive";
    case Status::Alive:
        return "Alive";
    case Status::Dead:
        return "Dead";
    case Status::Fine:
        return "Fine";
    case Status::Injured:
        return "Injured";
    case Status::HeavyInjured:
        return "HeavyInjured";
    case Status::Unconscious:
        return "Unconscious";
    case Status::Poison:
        return "Poison";
    case Status::Sleep:
        return "Sleep";
    case Status::Bind:
        return "Bind";
    case Status::Paralyze:
        return "Paralyze";
    case Status::Confuse:
        return "Confuse";
    case Status::Overheat:
        return "Overheat";
    case Status::Brave:
        return "Brave";
    case Status::Panic:
        return "Panic";
    case Status::Silence:
        return "Silence";
    case Status::FaceUp:
        return "FaceUp";
    case Status::AntiMagic:
        return "AntiMagic";
    case Status::UpAction:
        return "UpAction";
    case Status::UpAvoid:
        return "UpAvoid";
    case Status::UpResist:
        return "UpResist";
    case Status::UpDefense:
        return "UpDefense";
    case Status::DownAction:
        return "DownAction";
    case Status::DownAvoid:
        return "DownAvoid";
    case Status::DownResist:
        return "DownResist";
    case Status::DownDefense:
        return "DownDefense";
    case Status::None:
        return "None";
    default:
        throw std::logic_error("Invalid Status value");
    }
}

Range toRange(::cwxeditor::lib::StringView name) {
    if (name == "Selected") return Range::Selected;
    if (name == "Random") return Range::Random;
    if (name == "Party") return Range::Party;
    if (name == "Backpack") return Range::Backpack;
    if (name == "PartyAndBackpack") return Range::PartyAndBackpack;
    if (name == "Field") return Range::Field;
    if (name == "CouponHolder") return Range::CouponHolder;
    if (name == "CardTarget") return Range::CardTarget;
    if (name == "SelectedCard") return Range::SelectedCard;
    if (name == "Npc") return Range::Npc;
    throw MotionException(std::string("Unknown targets: ") + std::string(name));
}

::cwxeditor::lib::String fromRange(Range value) {
    switch (value) {
    case Range::Selected:
        return "Selected";
    case Range::Random:
        return "Random";
    case Range::Party:
        return "Party";
    case Range::Backpack:
        return "Backpack";
    case Range::PartyAndBackpack:
        return "PartyAndBackpack";
    case Range::Field:
        return "Field";
    case Range::CouponHolder:
        return "CouponHolder";
    case Range::CardTarget:
        return "CardTarget";
    case Range::SelectedCard:
        return "SelectedCard";
    case Range::Npc:
        return "Npc";
    default:
        throw std::logic_error("Invalid Range value");
    }
}

CastRange toCastRange(::cwxeditor::lib::StringView name) {
    if (name == "Party") return CastRange::Party;
    if (name == "Enemy") return CastRange::Enemy;
    if (name == "Npc") return CastRange::Npc;
    throw MotionException(std::string("Unknown targets: ") + std::string(name));
}

::cwxeditor::lib::String fromCastRange(CastRange value) {
    switch (value) {
    case CastRange::Party:
        return "Party";
    case CastRange::Enemy:
        return "Enemy";
    case CastRange::Npc:
        return "Npc";
    default:
        throw std::logic_error("Invalid CastRange value");
    }
}

Enhance toEnhance(::cwxeditor::lib::StringView name) {
    if (name == "Action") return Enhance::Action;
    if (name == "Avoid") return Enhance::Avoid;
    if (name == "Resist") return Enhance::Resist;
    if (name == "Defense") return Enhance::Defense;
    throw std::runtime_error(std::string("Unknown enhance: ") + std::string(name));
}

::cwxeditor::lib::String fromEnhance(Enhance value) {
    switch (value) {
    case Enhance::Action:
        return "Action";
    case Enhance::Avoid:
        return "Avoid";
    case Enhance::Resist:
        return "Resist";
    case Enhance::Defense:
        return "Defense";
    default:
        throw std::logic_error("Invalid Enhance value");
    }
}

Premium toPremium(::cwxeditor::lib::StringView name) {
    if (name == "Normal") return Premium::Normal;
    if (name == "Rare") return Premium::Rare;
    if (name == "Premium") return Premium::Premium;
    throw std::runtime_error(std::string("Unknown premium: ") + std::string(name));
}

::cwxeditor::lib::String fromPremium(Premium value) {
    switch (value) {
    case Premium::Normal:
        return "Normal";
    case Premium::Rare:
        return "Rare";
    case Premium::Premium:
        return "Premium";
    default:
        throw std::logic_error("Invalid Premium value");
    }
}

CardTarget toCardTarget(::cwxeditor::lib::StringView name) {
    if (name == "None") return CardTarget::None;
    if (name == "User") return CardTarget::User;
    if (name == "Party") return CardTarget::Party;
    if (name == "Enemy") return CardTarget::Enemy;
    if (name == "Both") return CardTarget::Both;
    throw std::runtime_error(std::string("Unknown card target: ") + std::string(name));
}

::cwxeditor::lib::String fromCardTarget(CardTarget value) {
    switch (value) {
    case CardTarget::None:
        return "None";
    case CardTarget::User:
        return "User";
    case CardTarget::Party:
        return "Party";
    case CardTarget::Enemy:
        return "Enemy";
    case CardTarget::Both:
        return "Both";
    default:
        throw std::logic_error("Invalid CardTarget value");
    }
}

::cwxeditor::lib::String fromTalker(Talker value) {
    switch (value) {
    case Talker::Selected:
        return "Selected";
    case Talker::Unselected:
        return "Unselected";
    case Talker::Random:
        return "Random";
    case Talker::Card:
        return "Card";
    case Talker::Valued:
        return "Valued";
    default:
        throw std::logic_error("Invalid Talker value");
    }
}

Transition toTransition(::cwxeditor::lib::StringView name) {
    if (name == "Default") return Transition::Default;
    if (name == "None") return Transition::None;
    if (name == "Blinds") return Transition::Blinds;
    if (name == "PixelDissolve") return Transition::PixelDissolve;
    if (name == "Fade") return Transition::Fade;
    throw std::runtime_error(std::string("Unknown transition: ") + std::string(name));
}

::cwxeditor::lib::String fromTransition(Transition value) {
    switch (value) {
    case Transition::Default:
        return "Default";
    case Transition::None:
        return "None";
    case Transition::Blinds:
        return "Blinds";
    case Transition::PixelDissolve:
        return "PixelDissolve";
    case Transition::Fade:
        return "Fade";
    default:
        throw std::logic_error("Invalid Transition value");
    }
}

EffectCardType toEffectCardType(::cwxeditor::lib::StringView name) {
    if (name == "All") return EffectCardType::All;
    if (name == "Skill") return EffectCardType::Skill;
    if (name == "Item") return EffectCardType::Item;
    if (name == "Beast") return EffectCardType::Beast;
    if (name == "Hand") return EffectCardType::Hand;
    throw std::runtime_error(std::string("Unknown card type: ") + std::string(name));
}

::cwxeditor::lib::String fromEffectCardType(EffectCardType value) {
    switch (value) {
    case EffectCardType::All:
        return "All";
    case EffectCardType::Skill:
        return "Skill";
    case EffectCardType::Item:
        return "Item";
    case EffectCardType::Beast:
        return "Beast";
    case EffectCardType::Hand:
        return "Hand";
    default:
        throw std::logic_error("Invalid EffectCardType value");
    }
}

Comparison4 toComparison4(::cwxeditor::lib::StringView name) {
    if (name == "=") return Comparison4::Eq;
    if (name == "<>") return Comparison4::Ne;
    if (name == "<") return Comparison4::Lt;
    if (name == ">") return Comparison4::Gt;
    throw std::runtime_error(std::string("Unknown 4 way comparison: ") + std::string(name));
}

::cwxeditor::lib::String fromComparison4(Comparison4 value) {
    switch (value) {
    case Comparison4::Eq:
        return "=";
    case Comparison4::Ne:
        return "<>";
    case Comparison4::Lt:
        return "<";
    case Comparison4::Gt:
        return ">";
    default:
        throw std::logic_error("Invalid Comparison4 value");
    }
}

Comparison3 toComparison3(::cwxeditor::lib::StringView name) {
    if (name == "=") return Comparison3::Eq;
    if (name == "<") return Comparison3::Lt;
    if (name == ">") return Comparison3::Gt;
    throw std::runtime_error(std::string("Unknown 3 way comparison: ") + std::string(name));
}

::cwxeditor::lib::String fromComparison3(Comparison3 value) {
    switch (value) {
    case Comparison3::Eq:
        return "=";
    case Comparison3::Lt:
        return "<";
    case Comparison3::Gt:
        return ">";
    default:
        throw std::logic_error("Invalid Comparison3 value");
    }
}

BlendMode toBlendMode(::cwxeditor::lib::StringView name) {
    if (name == "Normal") return BlendMode::Normal;
    if (name == "Mask") return BlendMode::Mask;
    if (name == "Add") return BlendMode::Add;
    if (name == "Subtract") return BlendMode::Subtract;
    if (name == "Multiply") return BlendMode::Multiply;
    throw std::runtime_error(std::string("Unknown blend mode: ") + std::string(name));
}

::cwxeditor::lib::String fromBlendMode(BlendMode value) {
    switch (value) {
    case BlendMode::Normal:
        return "Normal";
    case BlendMode::Mask:
        return "Mask";
    case BlendMode::Add:
        return "Add";
    case BlendMode::Subtract:
        return "Subtract";
    case BlendMode::Multiply:
        return "Multiply";
    default:
        throw std::logic_error("Invalid BlendMode value");
    }
}

GradientDir toGradientDir(::cwxeditor::lib::StringView name) {
    if (name == "None") return GradientDir::None;
    if (name == "LeftToRight") return GradientDir::LeftToRight;
    if (name == "TopToBottom") return GradientDir::TopToBottom;
    throw std::runtime_error(std::string("Unknown gradient dir: ") + std::string(name));
}

::cwxeditor::lib::String fromGradientDir(GradientDir value) {
    switch (value) {
    case GradientDir::None:
        return "None";
    case GradientDir::LeftToRight:
        return "LeftToRight";
    case GradientDir::TopToBottom:
        return "TopToBottom";
    default:
        throw std::logic_error("Invalid GradientDir value");
    }
}

BorderingType toBorderingType(::cwxeditor::lib::StringView name) {
    if (name == "None") return BorderingType::None;
    if (name == "Outline") return BorderingType::Outline;
    if (name == "Inline") return BorderingType::Inline;
    throw std::runtime_error(std::string("Unknown bordering type: ") + std::string(name));
}

::cwxeditor::lib::String fromBorderingType(BorderingType value) {
    switch (value) {
    case BorderingType::None:
        return "None";
    case BorderingType::Outline:
        return "Outline";
    case BorderingType::Inline:
        return "Inline";
    default:
        throw std::logic_error("Invalid BorderingType value");
    }
}

CoordinateType toCoordinateType(::cwxeditor::lib::StringView name) {
    if (name == "None") return CoordinateType::None;
    if (name == "Absolute") return CoordinateType::Absolute;
    if (name == "Relative") return CoordinateType::Relative;
    if (name == "Percentage") return CoordinateType::Percentage;
    throw std::runtime_error(std::string("Unknown coordinate type: ") + std::string(name));
}

::cwxeditor::lib::String fromCoordinateType(CoordinateType value) {
    switch (value) {
    case CoordinateType::None:
        return "None";
    case CoordinateType::Absolute:
        return "Absolute";
    case CoordinateType::Relative:
        return "Relative";
    case CoordinateType::Percentage:
        return "Percentage";
    default:
        throw std::logic_error("Invalid CoordinateType value");
    }
}

SelectionMethod toSelectionMethod(::cwxeditor::lib::StringView name) {
    if (name == "Manual") return SelectionMethod::Manual;
    if (name == "Random") return SelectionMethod::Random;
    if (name == "Valued") return SelectionMethod::Valued;
    throw std::runtime_error(std::string("Unknown selection method: ") + std::string(name));
}

::cwxeditor::lib::String fromSelectionMethod(SelectionMethod value) {
    switch (value) {
    case SelectionMethod::Manual:
        return "Manual";
    case SelectionMethod::Random:
        return "Random";
    case SelectionMethod::Valued:
        return "Valued";
    default:
        throw std::logic_error("Invalid SelectionMethod value");
    }
}

StartAction toStartAction(::cwxeditor::lib::StringView name) {
    if (name == "Now") return StartAction::Now;
    if (name == "CurrentRound") return StartAction::CurrentRound;
    if (name == "NextRound") return StartAction::NextRound;
    throw std::runtime_error(std::string("Unknown start action: ") + std::string(name));
}

::cwxeditor::lib::String fromStartAction(StartAction value) {
    switch (value) {
    case StartAction::Now:
        return "Now";
    case StartAction::CurrentRound:
        return "CurrentRound";
    case StartAction::NextRound:
        return "NextRound";
    default:
        throw std::logic_error("Invalid StartAction value");
    }
}

Smoothing toSmoothing(::cwxeditor::lib::StringView name) {
    if (name == "Default") return Smoothing::Default;
    if (name == "True") return Smoothing::True;
    if (name == "False") return Smoothing::False;
    throw std::runtime_error(std::string("Unknown smoothing: ") + std::string(name));
}

::cwxeditor::lib::String fromSmoothing(Smoothing value) {
    switch (value) {
    case Smoothing::Default:
        return "Default";
    case Smoothing::True:
        return "True";
    case Smoothing::False:
        return "False";
    default:
        throw std::logic_error("Invalid Smoothing value");
    }
}

ShowStyle toShowStyle(::cwxeditor::lib::StringView name) {
    if (name == "Invisible") return ShowStyle::Invisible;
    if (name == "Center") return ShowStyle::Center;
    if (name == "FrontOfUser") return ShowStyle::FrontOfUser;
    throw std::runtime_error(std::string("Unknown show style: ") + std::string(name));
}

::cwxeditor::lib::String fromShowStyle(ShowStyle value) {
    switch (value) {
    case ShowStyle::Invisible:
        return "Invisible";
    case ShowStyle::Center:
        return "Center";
    case ShowStyle::FrontOfUser:
        return "FrontOfUser";
    default:
        throw std::logic_error("Invalid ShowStyle value");
    }
}

UpdateType toUpdateType(::cwxeditor::lib::StringView name) {
    if (name == "Fixed") return UpdateType::Fixed;
    if (name == "Variables") return UpdateType::Variables;
    if (name == "All") return UpdateType::All;
    throw std::runtime_error(std::string("Unknown update type: ") + std::string(name));
}

::cwxeditor::lib::String fromUpdateType(UpdateType value) {
    switch (value) {
    case UpdateType::Fixed:
        return "Fixed";
    case UpdateType::Variables:
        return "Variables";
    case UpdateType::All:
        return "All";
    default:
        throw std::logic_error("Invalid UpdateType value");
    }
}

EnvironmentStatus toEnvironmentStatus(::cwxeditor::lib::StringView name) {
    if (name == "NotSet") return EnvironmentStatus::NotSet;
    if (name == "Enable") return EnvironmentStatus::Enable;
    if (name == "Disable") return EnvironmentStatus::Disable;
    throw std::runtime_error(std::string("Unknown environment status: ") + std::string(name));
}

::cwxeditor::lib::String fromEnvironmentStatus(EnvironmentStatus value) {
    switch (value) {
    case EnvironmentStatus::NotSet:
        return "NotSet";
    case EnvironmentStatus::Enable:
        return "Enable";
    case EnvironmentStatus::Disable:
        return "Disable";
    default:
        throw std::logic_error("Invalid EnvironmentStatus value");
    }
}

AbsorbTo toAbsorbTo(::cwxeditor::lib::StringView name) {
    if (name == "None") return AbsorbTo::None;
    if (name == "Selected") return AbsorbTo::Selected;
    throw std::runtime_error(std::string("Unknown absorb destination: ") +
                             std::string(name));
}

::cwxeditor::lib::String fromAbsorbTo(AbsorbTo value) {
    switch (value) {
    case AbsorbTo::None:
        return "None";
    case AbsorbTo::Selected:
        return "Selected";
    default:
        throw std::logic_error("Invalid AbsorbTo value");
    }
}

VariantType toVariantType(::cwxeditor::lib::StringView name) {
    if (name == "Number") return VariantType::Number;
    if (name == "String") return VariantType::String;
    if (name == "Boolean") return VariantType::Boolean;
    if (name == "List") return VariantType::List;
    if (name == "Structure") return VariantType::Structure;
    throw std::runtime_error(std::string("Unknown variant type: ") + std::string(name));
}

::cwxeditor::lib::String fromVariantType(VariantType value) {
    switch (value) {
    case VariantType::Number:
        return "Number";
    case VariantType::String:
        return "String";
    case VariantType::Boolean:
        return "Boolean";
    case VariantType::List:
        return "List";
    case VariantType::Structure:
        return "Structure";
    default:
        throw std::logic_error("Invalid VariantType value");
    }
}

CNextType toCNextType(::cwxeditor::lib::StringView name) {
    if (name == "None") return CNextType::None;
    if (name == "Text") return CNextType::Text;
    if (name == "Bool") return CNextType::Bool;
    if (name == "Step") return CNextType::Step;
    if (name == "IdArea") return CNextType::IdArea;
    if (name == "IdBattle") return CNextType::IdBattle;
    if (name == "Trio") return CNextType::Trio;
    if (name == "Coupon") return CNextType::Coupon;
    throw std::runtime_error(std::string("Unknown content next type: ") + std::string(name));
}

::cwxeditor::lib::String fromCNextType(CNextType value) {
    switch (value) {
    case CNextType::None:
        return "None";
    case CNextType::Text:
        return "Text";
    case CNextType::Bool:
        return "Bool";
    case CNextType::Step:
        return "Step";
    case CNextType::IdArea:
        return "IdArea";
    case CNextType::IdBattle:
        return "IdBattle";
    case CNextType::Trio:
        return "Trio";
    case CNextType::Coupon:
        return "Coupon";
    default:
        throw std::logic_error("Invalid CNextType value");
    }
}

MatchingType toMatchingType(::cwxeditor::lib::StringView name) {
    if (name == "And") return MatchingType::And;
    if (name == "Or") return MatchingType::Or;
    throw MotionException(std::string("Unknown matching type: ") + std::string(name));
}

::cwxeditor::lib::String fromMatchingType(MatchingType value) {
    switch (value) {
    case MatchingType::And:
        return "And";
    case MatchingType::Or:
        return "Or";
    default:
        throw std::logic_error("Invalid MatchingType value");
    }
}

MatchingCondition toMatchingCondition(::cwxeditor::lib::StringView name) {
    if (name == "Has") return MatchingCondition::Has;
    if (name == "HasNot") return MatchingCondition::HasNot;
    throw MotionException(std::string("Unknown matching condition: ") + std::string(name));
}

::cwxeditor::lib::String fromMatchingCondition(MatchingCondition value) {
    switch (value) {
    case MatchingCondition::Has:
        return "Has";
    case MatchingCondition::HasNot:
        return "HasNot";
    default:
        throw std::logic_error("Invalid MatchingCondition value");
    }
}

Target toTarget(::cwxeditor::lib::StringView name) {
    if (name == "Selected") return Target::opCall(Target::M::Selected, false);
    if (name == "Unselected") return Target::opCall(Target::M::Unselected, false);
    if (name == "Random") return Target::opCall(Target::M::Random, false);
    if (name == "Party") return Target::opCall(Target::M::Party, false);
    if (name == "SelectedSleep") return Target::opCall(Target::M::Selected, true);
    if (name == "UnselectedSleep") return Target::opCall(Target::M::Unselected, true);
    if (name == "RandomSleep") return Target::opCall(Target::M::Random, true);
    if (name == "PartySleep") return Target::opCall(Target::M::Party, true);
    throw MotionException(std::string("Unknown targetm: ") + std::string(name));
}

::cwxeditor::lib::String fromTarget(Target targ) {
    ::cwxeditor::lib::String text;
    switch (targ.m) {
    case Target::M::Selected: text = "Selected"; break;
    case Target::M::Unselected: text = "Unselected"; break;
    case Target::M::Random: text = "Random"; break;
    case Target::M::Party: text = "Party"; break;
    default: throw std::logic_error("Invalid Target::M value");
    }
    if (targ.sleep) text += "Sleep";
    return text;
}

Mental reverseMental(Mental m) {
    switch (m) {
    case Mental::Aggressive:
        return Mental::Unaggressive;
    case Mental::Unaggressive:
        return Mental::Aggressive;
    case Mental::Cheerful:
        return Mental::Uncheerful;
    case Mental::Uncheerful:
        return Mental::Cheerful;
    case Mental::Brave:
        return Mental::Unbrave;
    case Mental::Unbrave:
        return Mental::Brave;
    case Mental::Cautious:
        return Mental::Uncautious;
    case Mental::Uncautious:
        return Mental::Cautious;
    case Mental::Trickish:
        return Mental::Untrickish;
    case Mental::Untrickish:
        return Mental::Trickish;
    default:
        throw std::logic_error("Invalid Mental value");
    }
}

VariableInitialization toVariableInitialization(::cwxeditor::lib::StringView name) {
    if (name.empty()) return VariableInitialization::Leave;
    std::int32_t value = 0;
    // Dのstd.array.splitは文字列全体を先に復号し、Unicode
    // のWhite_Spaceで分割する。ロケールのストリームでは区切りと失敗
    // 順の両方が変わる。Noneトークンの後に不正UTF-8がある場合も含む。
    std::vector<std::string_view> tokens;
    std::size_t begin = 0;
    bool in_word = false;
    for (std::size_t offset = 0; offset < name.size();) {
        const auto scalar = enumutils::decodeUtf8Scalar(name, offset);
        if (enumutils::isWhiteSpace(scalar.value)) {
            if (in_word) tokens.push_back(name.substr(begin, offset - begin));
            in_word = false;
        } else if (!in_word) {
            begin = offset;
            in_word = true;
        }
        offset = scalar.next;
    }
    if (in_word) tokens.push_back(name.substr(begin));
    for (const auto token : tokens) {
        if (token == "Leave") value |= static_cast<std::int32_t>(VariableInitialization::Leave);
        else if (token == "EventExit") value |= static_cast<std::int32_t>(VariableInitialization::EventExit);
        else if (token == "BattleStart") value |= static_cast<std::int32_t>(VariableInitialization::BattleStart);
        else if (token == "BattleEnd") value |= static_cast<std::int32_t>(VariableInitialization::BattleEnd);
        else if (token == "None") return VariableInitialization::None;
        else throw std::runtime_error(std::string("Unknown variable initialization: ") + std::string(name));
    }
    return value != 0 ? static_cast<VariableInitialization>(value) : VariableInitialization::Leave;
}

::cwxeditor::lib::String fromVariableInitialization(VariableInitialization value) {
    if (value == VariableInitialization::None) return "None";
    const auto bits = static_cast<std::int32_t>(value);
    ::cwxeditor::lib::String result;
    const auto append = [&result](::cwxeditor::lib::StringView text) { if (!result.empty()) result += " "; result += text; };
    if ((bits & static_cast<std::int32_t>(VariableInitialization::BattleStart)) != 0) append("BattleStart");
    if ((bits & static_cast<std::int32_t>(VariableInitialization::BattleEnd)) != 0) append("BattleEnd");
    if ((bits & static_cast<std::int32_t>(VariableInitialization::EventExit)) != 0) append("EventExit");
    if ((bits & static_cast<std::int32_t>(VariableInitialization::Leave)) != 0) append("Leave");
    return result.empty() ? ::cwxeditor::lib::String("Leave") : result;
}

bool hasVariableInitialization(VariableInitialization value, VariableInitialization flag) {
    return (static_cast<std::int32_t>(value) & static_cast<std::int32_t>(flag)) != 0;
}

bool isDefaultVariableInitialization(VariableInitialization value) {
    return static_cast<std::int32_t>(value) == static_cast<std::int32_t>(VariableInitialization::Leave);
}

bool isWsnContent(CType cType) {
    switch (cType) {
    case CType::MoveBgImage:
    case CType::ReplaceBgImage:
    case CType::LoseBgImage:
    case CType::BranchMultiCoupon:
    case CType::BranchMultiRandom:
    case CType::MoveCard:
    case CType::ChangeEnvironment:
    case CType::BranchVariant:
    case CType::SetVariant:
    case CType::CheckVariant:
    case CType::CustomXmlContent:
        return true;
    default: return false;
    }
}

}  // namespace cwxeditor::cwx::types
