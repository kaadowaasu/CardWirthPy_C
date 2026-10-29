#include "cw/binary/xmltemplate.hpp"
#include <stdexcept>

namespace cw::binary::xmltemplate {
::cw::lib::String g_Adventurer = R"CWXML($indent<Adventurer scaledimage="$scaledimage">
$indent <Property>
$indent  <Id>$id</Id>
$indent  <Name>$name</Name>
$indent  <ImagePaths>$imgpaths
$indent  </ImagePaths>
$indent  <Description>$description</Description>
$indent  <Level>$level</Level>
$indent  <Life max="$maxlife">$life</Life>
$indent  <Feature>
$indent   <Type undead="$undead" automaton="$automaton" unholy="$unholy" constructure="$constructure" />
$indent   <NoEffect weapon="$noeffect_weapon" magic="$noeffect_magic" />
$indent   <Resist fire="$resist_fire" ice="$resist_ice" />
$indent   <Weakness fire="$weakness_fire" ice="$weakness_ice" />
$indent  </Feature>
$indent  <Ability>
$indent   <Physical dex="$dex" agl="$agl" int="$int" str="$str" vit="$vit" min="$min" />
$indent   <Mental aggressive="$aggressive" cheerful="$cheerful" brave="$brave" cautious="$cautious"
$indent           trickish="$trickish" />
$indent   <Enhance avoid="$avoid" resist="$resist" defense="$defense" />
$indent  </Ability>
$indent  <Status>
$indent   <Mentality duration="$duration_mentality">$mentality</Mentality>
$indent   <Paralyze>$paralyze</Paralyze>
$indent   <Poison>$poison</Poison>
$indent   <Bind duration="$bind" />
$indent   <Silence duration="$silence" />
$indent   <FaceUp duration="$faceup" />
$indent   <AntiMagic duration="$antimagic" />
$indent  </Status>
$indent  <Enhance>
$indent   <Action duration="$duration_enhance_action">$enhance_action</Action>
$indent   <Avoid duration="$duration_enhance_avoid">$enhance_avoid</Avoid>
$indent   <Resist duration="$duration_enhance_resist">$enhance_resist</Resist>
$indent   <Defense duration="$duration_enhance_defense">$enhance_defense</Defense>
$indent  </Enhance>
$indent  <Coupons>$coupons
$indent  </Coupons>
$indent  <Coefficient level="$coeff_level" ep="$coeff_ep" />
$indent </Property>
$indent <ItemCards>$items
$indent </ItemCards>
$indent <SkillCards>$skills
$indent </SkillCards>
$indent <BeastCards>$beasts
$indent </BeastCards>
$indent</Adventurer>)CWXML";
::cw::lib::String g_Album = R"CWXML($indent<Album>
$indent <Property>
$indent  <Name>$name</Name>
$indent  <ImagePath>$imgpath</ImagePath>
$indent  <Description>$description</Description>
$indent  <Level>$level</Level>
$indent  <Ability>
$indent   <Physical dex="$dex" agl="$agl" int="$int" str="$str" vit="$vit" min="$min" />
$indent   <Mental aggressive="$aggressive" cheerful="$cheerful" brave="$brave" cautious="$cautious"
$indent           trickish="$trickish" />
$indent   <Enhance avoid="$avoid" resist="$resist" defense="$defense" />
$indent  </Ability>
$indent  <Coupons>$coupons
$indent  </Coupons>
$indent </Property>
$indent</Album>)CWXML";
::cw::lib::String g_Area = R"CWXML($indent<Area>
$indent <Property>
$indent  <Id>$id</Id>
$indent  <Name>$name</Name>
$indent </Property>
$indent <BgImages>$bgimgs
$indent </BgImages>
$indent <MenuCards spreadtype="$spreadtype">$menucards
$indent </MenuCards>
$indent <Events>$events
$indent </Events>
$indent</Area>)CWXML";
::cw::lib::String g_Battle = R"CWXML($indent<Battle>
$indent <Property>
$indent  <Id>$id</Id>
$indent  <Name>$name</Name>
$indent  <MusicPath>$bgm</MusicPath>
$indent </Property>
$indent <EnemyCards spreadtype="$spreadtype">$enemycards
$indent </EnemyCards>
$indent <Events>$events
$indent </Events>
$indent</Battle>)CWXML";
::cw::lib::String g_BeastCard = R"CWXML($indent<BeastCard>
$indent <Property>
$indent  <Id>$id</Id>
$indent  <Name>$name</Name>
$indent  <ImagePath>$imgpath</ImagePath>
$indent  <Description>$description</Description>
$indent  <Scenario>$scenario</Scenario>
$indent  <Author>$author</Author>
$indent  <Ability physical="$p_ability" mental="$m_ability" />
$indent  <Target allrange="$target_all">$target</Target>
$indent  <EffectType spell="$silence">$effecttype</EffectType>
$indent  <ResistType>$resisttype</ResistType>
$indent  <SuccessRate>$successrate</SuccessRate>
$indent  <VisualEffect>$visual</VisualEffect>
$indent  <Enhance avoid="$enhance_avoid" resist="$enhance_resist" defense="$enhance_defense"/>
$indent  <SoundPath>$sound</SoundPath>
$indent  <SoundPath2>$sound2</SoundPath2>
$indent  <KeyCodes>$keycodes</KeyCodes>
$indent  <Premium>$premium</Premium>
$indent  <UseLimit>$uselimit</UseLimit>$attachment
$indent </Property>
$indent <Motions>$motions
$indent </Motions>
$indent <Events>$events
$indent </Events>
$indent</BeastCard>)CWXML";
::cw::lib::String g_BgImage = R"CWXML($indent<BgImage mask="$mask">
$indent <ImagePath>$imgpath</ImagePath>
$indent <Flag>$flag</Flag>
$indent <Location left="$left" top="$top" />
$indent <Size width="$width" height="$height" />
$indent</BgImage>)CWXML";
::cw::lib::String g_CardRecord = R"CWXML($indent<CardRecord name="$name" desc="$desc" author="$author" scenario="$scenario"
$indent            uselimit="$uselimit" />)CWXML";
::cw::lib::String g_CastCard = R"CWXML($indent<CastCard>
$indent <Property>
$indent  <Id>$id</Id>
$indent  <Name>$name</Name>
$indent  <ImagePath>$imgpath</ImagePath>
$indent  <Description>$description</Description>
$indent  <Level>$level</Level>
$indent  <Money>$money</Money>
$indent  <Life max="$maxlife">$life</Life>
$indent  <Feature>
$indent   <Type undead="$undead" automaton="$automaton" unholy="$unholy" constructure="$constructure" />
$indent   <NoEffect weapon="$noeffect_weapon" magic="$noeffect_magic" />
$indent   <Resist fire="$resist_fire" ice="$resist_ice" />
$indent   <Weakness fire="$weakness_fire" ice="$weakness_ice" />
$indent  </Feature>
$indent  <Ability>
$indent   <Physical dex="$dex" agl="$agl" int="$int" str="$str" vit="$vit" min="$min" />
$indent   <Mental aggressive="$aggressive" cheerful="$cheerful" brave="$brave" cautious="$cautious"
$indent           trickish="$trickish" />
$indent   <Enhance avoid="$avoid" resist="$resist" defense="$defense" />
$indent  </Ability>
$indent  <Status>
$indent   <Mentality duration="$duration_mentality">$mentality</Mentality>
$indent   <Paralyze>$paralyze</Paralyze>
$indent   <Poison>$poison</Poison>
$indent   <Bind duration="$bind" />
$indent   <Silence duration="$silence" />
$indent   <FaceUp duration="$faceup" />
$indent   <AntiMagic duration="$antimagic" />
$indent  </Status>
$indent  <Enhance>
$indent   <Action duration="$duration_enhance_action">$enhance_action</Action>
$indent   <Avoid duration="$duration_enhance_avoid">$enhance_avoid</Avoid>
$indent   <Resist duration="$duration_enhance_resist">$enhance_resist</Resist>
$indent   <Defense duration="$duration_enhance_defense">$enhance_defense</Defense>
$indent  </Enhance>
$indent  <Coupons>$coupons
$indent  </Coupons>
$indent </Property>
$indent <ItemCards>$items
$indent </ItemCards>
$indent <SkillCards>$skills
$indent </SkillCards>
$indent <BeastCards>$beasts
$indent </BeastCards>
$indent</CastCard>)CWXML";
::cw::lib::String g_Content = R"CWXML($indent<$tag$type$name$properties>$children
$indent <Contents>$contents
$indent </Contents>
$indent</$tag>)CWXML";
::cw::lib::String g_Coupon = R"CWXML($indent<Coupon value="$value">$name</Coupon>)CWXML";
::cw::lib::String g_Dialog = R"CWXML($indent<Dialog>
$indent <RequiredCoupons>$coupons</RequiredCoupons>
$indent <Text>$text</Text>
$indent</Dialog>)CWXML";
::cw::lib::String g_EffectMotion = R"CWXML($indent<Motion type="$type" element="$element"$properties$children)CWXML";
::cw::lib::String g_EnemyCard = R"CWXML($indent<EnemyCard escape="$escape">
$indent <Property>
$indent  <Id>$castid</Id>
$indent  <Flag>$flag</Flag>
$indent  <Location left="$left" top="$top" />
$indent  <Size scale="$scale%" />
$indent </Property>
$indent <Events>$events
$indent </Events>
$indent</EnemyCard>)CWXML";
::cw::lib::String g_Environment = R"CWXML($indent<Environment>
$indent <Property>
$indent  <Name>$name</Name>
$indent  <Skin>$skinname</Skin>
$indent  <Type>$skintype</Type>
$indent  <ImagePaths>$imgpaths
$indent  </ImagePaths>
$indent  <Cashbox>$cashbox</Cashbox>
$indent  <NowSelectingParty autoload="$is_autoloadparty">$selectingparty</NowSelectingParty>
$indent </Property>
$indent <CompleteStamps>$completestamps
$indent </CompleteStamps>
$indent <Gossips>$gossips
$indent </Gossips>
$indent</Environment>)CWXML";
::cw::lib::String g_Event = R"CWXML($indent<Event>
$indent <Ignitions>
$indent  <Number>$ignitions</Number>
$indent  <KeyCodes>$keycodes</KeyCodes>
$indent </Ignitions>
$indent <Contents>$contents
$indent </Contents>
$indent</Event>)CWXML";
::cw::lib::String g_Flag = R"CWXML($indent<Flag default="$default">
$indent <Name>$name</Name>
$indent <True>$valname0</True>
$indent <False>$valname1</False>
$indent</Flag>)CWXML";
::cw::lib::String g_ImagePath = R"CWXML($indent<ImagePath positiontype="$postype">$path</ImagePath>)CWXML";
::cw::lib::String g_InfoCard = R"CWXML($indent<InfoCard>
$indent <Property>
$indent  <Id>$id</Id>
$indent  <Name>$name</Name>
$indent  <ImagePath>$imgpath</ImagePath>
$indent  <Description>$description</Description>
$indent </Property>
$indent</InfoCard>)CWXML";
::cw::lib::String g_ItemCard = R"CWXML($indent<ItemCard>
$indent <Property>
$indent  <Id>$id</Id>
$indent  <Name>$name</Name>
$indent  <ImagePath>$imgpath</ImagePath>
$indent  <Description>$description</Description>
$indent  <Scenario>$scenario</Scenario>
$indent  <Author>$author</Author>
$indent  <Ability physical="$p_ability" mental="$m_ability" />
$indent  <Target allrange="$target_all">$target</Target>
$indent  <EffectType spell="$silence">$effecttype</EffectType>
$indent  <ResistType>$resisttype</ResistType>
$indent  <SuccessRate>$successrate</SuccessRate>
$indent  <VisualEffect>$visual</VisualEffect>
$indent  <Enhance avoid="$enhance_avoid" resist="$enhance_resist" defense="$enhance_defense" />
$indent  <SoundPath>$sound</SoundPath>
$indent  <SoundPath2>$sound2</SoundPath2>
$indent  <KeyCodes>$keycodes</KeyCodes>
$indent  <Premium>$premium</Premium>
$indent  <UseLimit max="$uselimitmax">$uselimit</UseLimit>
$indent  <Price>$price</Price>
$indent  <EnhanceOwner avoid="$enhance_avoid2" resist="$enhance_resist2" defense="$enhance_defense2"/>
$indent  <Hold>$hold</Hold>
$indent </Property>
$indent <Motions>$motions
$indent </Motions>
$indent <Events>$events
$indent </Events>
$indent</ItemCard>)CWXML";
::cw::lib::String g_MenuCard = R"CWXML($indent<MenuCard>
$indent <Property>
$indent  <Name>$name</Name>
$indent  <ImagePath>$imgpath</ImagePath>
$indent  <Description>$description</Description>
$indent  <Flag>$flag</Flag>
$indent  <Location left="$left" top="$top" />
$indent  <Size scale="$scale%" />
$indent </Property>
$indent <Events>$events
$indent </Events>
$indent</MenuCard>)CWXML";
::cw::lib::String g_Package = R"CWXML($indent<Package>
$indent <Property>
$indent  <Id>$id</Id>
$indent  <Name>$name</Name>
$indent </Property>
$indent <Events>$events
$indent </Events>
$indent</Package>)CWXML";
::cw::lib::String g_Party = R"CWXML($indent<Party>
$indent <Property>
$indent  <Name>$name</Name>
$indent  <Money>$money</Money>
$indent  <SuspendLevelUp>$suspend_levelup</SuspendLevelUp>
$indent  <Members>$members
$indent  </Members>
$indent </Property>
$indent <Backpack>$backpack
$indent </Backpack>
$indent</Party>)CWXML";
::cw::lib::String g_PartyRecord = R"CWXML($indent<PartyRecord>
$indent <Property>
$indent  <Name>$name</Name>
$indent  <Money>$money</Money>
$indent  <SuspendLevelUp>$suspend_levelup</SuspendLevelUp>
$indent  <Members>$members
$indent  </Members>
$indent </Property>
$indent <BackpackRecord>$backpack
$indent </BackpackRecord>
$indent</PartyRecord>)CWXML";
::cw::lib::String g_SimpleEvent = R"CWXML($indent<Event>
$indent <Contents>$contents
$indent </Contents>
$indent</Event>)CWXML";
::cw::lib::String g_SkillCard = R"CWXML($indent<SkillCard>
$indent <Property>
$indent  <Id>$id</Id>
$indent  <Name>$name</Name>
$indent  <ImagePath>$imgpath</ImagePath>
$indent  <Description>$description</Description>
$indent  <Scenario>$scenario</Scenario>
$indent  <Author>$author</Author>
$indent  <Level>$level</Level>
$indent  <Ability physical="$p_ability" mental="$m_ability" />
$indent  <Target allrange="$target_all">$target</Target>
$indent  <EffectType spell="$silence">$effecttype</EffectType>
$indent  <ResistType>$resisttype</ResistType>
$indent  <SuccessRate>$successrate</SuccessRate>
$indent  <VisualEffect>$visual</VisualEffect>
$indent  <Enhance avoid="$enhance_avoid" resist="$enhance_resist" defense="$enhance_defense"/>
$indent  <SoundPath>$sound</SoundPath>
$indent  <SoundPath2>$sound2</SoundPath2>
$indent  <KeyCodes>$keycodes</KeyCodes>
$indent  <Premium>$premium</Premium>
$indent  <UseLimit>$uselimit</UseLimit>
$indent  <Hold>$hold</Hold>
$indent </Property>
$indent <Motions>$motions
$indent </Motions>
$indent <Events>$events
$indent </Events>
$indent</SkillCard>)CWXML";
::cw::lib::String g_SkinVariables = R"CWXML($indent<SkinVariables>
$indent</SkinVariables>)CWXML";
::cw::lib::String g_Step = R"CWXML($indent<Step default="$default">
$indent <Name>$name</Name>
$indent <Value0>$valname0</Value0>
$indent <Value1>$valname1</Value1>
$indent <Value2>$valname2</Value2>
$indent <Value3>$valname3</Value3>
$indent <Value4>$valname4</Value4>
$indent <Value5>$valname5</Value5>
$indent <Value6>$valname6</Value6>
$indent <Value7>$valname7</Value7>
$indent <Value8>$valname8</Value8>
$indent <Value9>$valname9</Value9>
$indent</Step>)CWXML";
::cw::lib::String g_Summary = R"CWXML($indent<Summary>
$indent <Property>
$indent  <Name>$name</Name>
$indent  <ImagePath>$imgpath</ImagePath>
$indent  <Author>$author</Author>
$indent  <Description>$description</Description>
$indent  <Level min="$levelmin" max="$levelmax" />
$indent  <RequiredCoupons number="$required_coupons_num">$required_coupons</RequiredCoupons>
$indent  <StartAreaId>$startarea_id</StartAreaId>
$indent  <Tags>$tags</Tags>
$indent  <Type>$skintype</Type>
$indent </Property>
$indent <Flags>$flags
$indent </Flags>
$indent <Steps>$steps
$indent </Steps>
$indent <Labels>$labels
$indent </Labels>
$indent</Summary>)CWXML";

namespace {

// Python の string.Template は (?a:[_a-z][_a-z0-9]*) を名前として扱う。
// std::isalnum はロケール依存なので、ASCII の判定を明示して同じ範囲に
// 固定する（XML値のエスケープは Python 側でも行わない）。
bool is_template_name_start(char ch) noexcept {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

bool is_template_name_char(char ch) noexcept {
    return is_template_name_start(ch) || (ch >= '0' && ch <= '9');
}

} // namespace

// C++にはPythonのモジュール全体の名前空間がないため、移植した
// テンプレート名を共通の登録先で解決する。 既存の登録済み文字列はポ
// インターで参照し、任意の新しいPythonグローバルは公開しない。
::cw::lib::String get_xmltemplate(::cw::lib::String name) {
    static const std::map<::cw::lib::String, const ::cw::lib::String*> table = {
        {"Adventurer", &g_Adventurer},
        {"Album", &g_Album},
        {"Area", &g_Area},
        {"Battle", &g_Battle},
        {"BeastCard", &g_BeastCard},
        {"BgImage", &g_BgImage},
        {"CardRecord", &g_CardRecord},
        {"CastCard", &g_CastCard},
        {"Content", &g_Content},
        {"Coupon", &g_Coupon},
        {"Dialog", &g_Dialog},
        {"EffectMotion", &g_EffectMotion},
        {"EnemyCard", &g_EnemyCard},
        {"Environment", &g_Environment},
        {"Event", &g_Event},
        {"Flag", &g_Flag},
        {"ImagePath", &g_ImagePath},
        {"InfoCard", &g_InfoCard},
        {"ItemCard", &g_ItemCard},
        {"MenuCard", &g_MenuCard},
        {"Package", &g_Package},
        {"Party", &g_Party},
        {"PartyRecord", &g_PartyRecord},
        {"SimpleEvent", &g_SimpleEvent},
        {"SkillCard", &g_SkillCard},
        {"SkinVariables", &g_SkinVariables},
        {"Step", &g_Step},
        {"Summary", &g_Summary},
    };
    const auto it = table.find(name);
    if (it == table.end()) throw std::out_of_range("unknown XML template: " + name);
    return *it->second;
}

::cw::lib::String get_xmltext(::cw::lib::String name, std::map<::cw::lib::String, ::cw::lib::String> values) {
    const auto source = get_xmltemplate(std::move(name));
    ::cw::lib::String out;
    out.reserve(source.size());
    for (std::size_t i = 0; i < source.size();) {
        if (source[i] != '$') { out.push_back(source[i++]); continue; }
        if (i + 1 < source.size() && source[i + 1] == '$') { out.push_back('$'); i += 2; continue; }

        // Template の invalid 分岐は区切り文字 '$' だけをそのまま出力し、
        // 後続文字を通常の文字として再走査する。不正な名前を値で置換しない。
        const std::size_t name_start = i + 1;
        if (name_start >= source.size()) { out.push_back('$'); ++i; continue; }

        if (source[name_start] == '{') {
            const std::size_t begin = name_start + 1;
            const std::size_t close = source.find('}', begin);
            if (close == ::cw::lib::String::npos || close == begin ||
                !is_template_name_start(source[begin])) {
                out.push_back('$');
                ++i;
                continue;
            }
            std::size_t end = begin + 1;
            while (end < close && is_template_name_char(source[end])) ++end;
            if (end != close) {
                out.push_back('$');
                ++i;
                continue;
            }

            const auto key = source.substr(begin, close - begin);
            const auto found = values.find(key);
            if (found != values.end()) out += found->second;
            else out += source.substr(i, close + 1 - i);
            i = close + 1;
            continue;
        }

        if (!is_template_name_start(source[name_start])) {
            out.push_back('$');
            ++i;
            continue;
        }
        std::size_t end = name_start + 1;
        while (end < source.size() && is_template_name_char(source[end])) ++end;
        const auto key = source.substr(name_start, end - name_start);
        const auto found = values.find(key);
        if (found != values.end()) out += found->second;
        else out += source.substr(i, end - i);
        i = end;
    }
    return out;
}

void main() {}
}
