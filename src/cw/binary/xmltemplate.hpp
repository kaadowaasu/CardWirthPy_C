#pragma once

// CardWirthPy のバイナリー XML テンプレートと置換 API を移植した型付き C++ API。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::binary::xmltemplate {
extern ::cw::lib::String g_Adventurer;
extern ::cw::lib::String g_Album;
extern ::cw::lib::String g_Area;
extern ::cw::lib::String g_Battle;
extern ::cw::lib::String g_BeastCard;
extern ::cw::lib::String g_BgImage;
extern ::cw::lib::String g_CardRecord;
extern ::cw::lib::String g_CastCard;
extern ::cw::lib::String g_Content;
extern ::cw::lib::String g_Coupon;
extern ::cw::lib::String g_Dialog;
extern ::cw::lib::String g_EffectMotion;
extern ::cw::lib::String g_EnemyCard;
extern ::cw::lib::String g_Environment;
extern ::cw::lib::String g_Event;
extern ::cw::lib::String g_Flag;
extern ::cw::lib::String g_ImagePath;
extern ::cw::lib::String g_InfoCard;
extern ::cw::lib::String g_ItemCard;
extern ::cw::lib::String g_MenuCard;
extern ::cw::lib::String g_Package;
extern ::cw::lib::String g_Party;
extern ::cw::lib::String g_PartyRecord;
extern ::cw::lib::String g_SimpleEvent;
extern ::cw::lib::String g_SkillCard;
extern ::cw::lib::String g_SkinVariables;
extern ::cw::lib::String g_Step;
extern ::cw::lib::String g_Summary;

::cw::lib::String get_xmltemplate(::cw::lib::String arg_name);
::cw::lib::String get_xmltext(::cw::lib::String arg_name, std::map<::cw::lib::String, ::cw::lib::String> arg_d);
void main();
}

