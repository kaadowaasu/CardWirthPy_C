#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/core/cwx/race.hpp"

#include "cwxeditor_src/core/cwx/utils.hpp"

#include <cassert>
#include <utility>

namespace cwxeditor::cwx::race {

namespace {

template <typename T>
void setAttr(XNode& node, const StringView name, const T& value) {
    const String text = ::cwxeditor::lib::detail::xml_to_string(value);
    // StringViewを明示的に渡す。共通XmlNodeのオーバー
    // ロードはD向けの名前を保持するが、MSVCでテンプレートの選択
    // が再帰しないよう、ここでは汎用newAttrラッパーを避ける。
    node.new_attr(name, StringView(text));
}

}  // namespace

RaceException::RaceException(String message) : std::runtime_error(std::move(message)) {}

template <bool Set>
bool RaceParam<Set>::resist(const Element element) const {
    const auto found = resist_.find(element);
    return found != resist_.end() && found->second;
}

template <bool Set>
bool RaceParam<Set>::weakness(const Element element) const {
    const auto found = weakness_.find(element);
    return found != weakness_.end() && found->second;
}

template <bool Set>
std::uint32_t RaceParam<Set>::physical(const Physical value) const {
    // Dは必須の連想配列キーを添字で参照する。キーがなければ、読み取り自体で失敗する。
    return physical_.at(value);
}

template <bool Set>
double RaceParam<Set>::mental(const Mental value) const {
    ::cwxeditor::cwx::perf::TraceScope trace_race_260(__FILE__, __LINE__);
    switch (value) {
    case Mental::Unaggressive:
    case Mental::Uncheerful:
    case Mental::Unbrave:
    case Mental::Uncautious:
    case Mental::Untrickish:
        return mental_.at(positiveMental(value)) * -1.0;
    case Mental::Aggressive:
    case Mental::Cheerful:
    case Mental::Brave:
    case Mental::Cautious:
    case Mental::Trickish:
        return mental_.at(value);
    }
    // Dのfinal switchの失敗は、ネイティブの不変条件例外に変換する。
    throw std::logic_error("Invalid mental parameter");
}

template <bool Set>
std::int32_t RaceParam<Set>::defaultEnhance(const Enhance value) const {
    const auto found = defaultEnhance_.find(value);
    return found == defaultEnhance_.end() ? 0 : found->second;
}

template <bool Set>
void RaceParam<Set>::constructRace() {
    ::cwxeditor::cwx::perf::TraceScope trace_race_302(__FILE__, __LINE__);
    resist_[Element::Fire] = false;
    resist_[Element::Ice] = false;
    weakness_[Element::Fire] = false;
    weakness_[Element::Ice] = false;
    physical_[Physical::Dex] = 0;
    physical_[Physical::Agl] = 0;
    physical_[Physical::Int] = 0;
    physical_[Physical::Str] = 0;
    physical_[Physical::Vit] = 0;
    physical_[Physical::Min] = 0;
    mental_[Mental::Aggressive] = 0.0;
    mental_[Mental::Cheerful] = 0.0;
    mental_[Mental::Brave] = 0.0;
    mental_[Mental::Cautious] = 0.0;
    mental_[Mental::Trickish] = 0.0;
    defaultEnhance_[Enhance::Avoid] = 0;
    defaultEnhance_[Enhance::Resist] = 0;
    defaultEnhance_[Enhance::Defense] = 0;
}

template <bool Set>
void RaceParam<Set>::setFeature(XNode& parent) const {
    ::cwxeditor::cwx::perf::TraceScope trace_race_323(__FILE__, __LINE__);
    auto feature = parent.newElement("Feature");
    auto type = feature.newElement("Type");
    setAttr(type, "undead", utils::fromBool(undead_));
    setAttr(type, "automaton", utils::fromBool(automaton_));
    setAttr(type, "unholy", utils::fromBool(unholy_));
    setAttr(type, "constructure", utils::fromBool(constructure_));

    auto noEffect = feature.newElement("NoEffect");
    setAttr(noEffect, "weapon", utils::fromBool(weaponRes_));
    setAttr(noEffect, "magic", utils::fromBool(magicRes_));

    auto resistNode = feature.newElement("Resist");
    setAttr(resistNode, "fire", utils::fromBool(resist(Element::Fire)));
    setAttr(resistNode, "ice", utils::fromBool(resist(Element::Ice)));

    auto weaknessNode = feature.newElement("Weakness");
    setAttr(weaknessNode, "fire", utils::fromBool(weakness(Element::Fire)));
    setAttr(weaknessNode, "ice", utils::fromBool(weakness(Element::Ice)));
}

template <bool Set>
void RaceParam<Set>::setAbility(XNode& parent) const {
    ::cwxeditor::cwx::perf::TraceScope trace_race_341(__FILE__, __LINE__);
    auto ability = parent.newElement("Ability");
    auto physicalNode = ability.newElement("Physical");
    setAttr(physicalNode, "dex", physical(Physical::Dex));
    setAttr(physicalNode, "agl", physical(Physical::Agl));
    setAttr(physicalNode, "int", physical(Physical::Int));
    setAttr(physicalNode, "str", physical(Physical::Str));
    setAttr(physicalNode, "vit", physical(Physical::Vit));
    setAttr(physicalNode, "min", physical(Physical::Min));

    auto mentalNode = ability.newElement("Mental");
    setAttr(mentalNode, "aggressive", mental(Mental::Aggressive));
    setAttr(mentalNode, "cheerful", mental(Mental::Cheerful));
    setAttr(mentalNode, "brave", mental(Mental::Brave));
    setAttr(mentalNode, "cautious", mental(Mental::Cautious));
    setAttr(mentalNode, "trickish", mental(Mental::Trickish));

    auto enhanceNode = ability.newElement("Enhance");
    setAttr(enhanceNode, "avoid", defaultEnhance(Enhance::Avoid));
    setAttr(enhanceNode, "resist", defaultEnhance(Enhance::Resist));
    setAttr(enhanceNode, "defense", defaultEnhance(Enhance::Defense));
}

template <bool Set>
void RaceParam<Set>::loadFeature(XNode& featureNode, const XMLInfo& ver) {
    ::cwxeditor::cwx::perf::TraceScope trace_race_361(__FILE__, __LINE__);
    (void)ver;
    assert(featureNode.name() == "Feature");
    featureNode.onTag["Type"] = [this](XNode& typeNode) { // Dの読み取り順に従い、最初の不正属性のエラーを選ぶ。
        ::cwxeditor::cwx::perf::TraceScope trace_race_363(__FILE__, __LINE__);
        undead_ = utils::parseBool(typeNode.attr("undead", false, "False"));
        automaton_ = utils::parseBool(typeNode.attr("automaton", false, "False"));
        unholy_ = utils::parseBool(typeNode.attr("unholy", false, "False"));
        constructure_ = utils::parseBool(typeNode.attr("constructure", false, "False"));
    };
    featureNode.onTag["NoEffect"] = [this](XNode& noEffectNode) {
        ::cwxeditor::cwx::perf::TraceScope trace_race_369(__FILE__, __LINE__);
        weaponRes_ = utils::parseBool(noEffectNode.attr("weapon", false, "False"));
        magicRes_ = utils::parseBool(noEffectNode.attr("magic", false, "False"));
    };
    featureNode.onTag["Resist"] = [this](XNode& resistNode) {
        ::cwxeditor::cwx::perf::TraceScope trace_race_373(__FILE__, __LINE__);
        resist_[Element::Fire] =
            utils::parseBool(resistNode.attr("fire", false, "False"));
        resist_[Element::Ice] =
            utils::parseBool(resistNode.attr("ice", false, "False"));
    };
    featureNode.onTag["Weakness"] = [this](XNode& weaknessNode) {
        ::cwxeditor::cwx::perf::TraceScope trace_race_377(__FILE__, __LINE__);
        weakness_[Element::Fire] =
            utils::parseBool(weaknessNode.attr("fire", false, "False"));
        weakness_[Element::Ice] =
            utils::parseBool(weaknessNode.attr("ice", false, "False"));
    };
    featureNode.parse();
}

template <bool Set>
void RaceParam<Set>::loadAbility(const CProps& prop, XNode& abilityNode,
                                 const XMLInfo& ver) {
    loadAbility(prop.looks().physicalNormal(), abilityNode, ver);
}

template <bool Set>
void RaceParam<Set>::loadAbility(const std::uint32_t physicalNormal,
                                 XNode& abilityNode, const XMLInfo& ver) {
    ::cwxeditor::cwx::perf::TraceScope trace_race_383(__FILE__, __LINE__);
    (void)ver;
    assert(abilityNode.name() == "Ability");
    abilityNode.onTag["Physical"] = [this, physicalNormal](XNode& physicalNode) {
        ::cwxeditor::cwx::perf::TraceScope trace_race_385(__FILE__, __LINE__);
        const auto readPhysical = [&physicalNode, physicalNormal](StringView name) {
            return static_cast<std::uint32_t>(
                physicalNode.attr<std::int32_t>(name, false, physicalNormal));
        };
        physical_[Physical::Dex] = readPhysical("dex");
        physical_[Physical::Agl] = readPhysical("agl");
        physical_[Physical::Int] = readPhysical("int");
        physical_[Physical::Str] = readPhysical("str");
        physical_[Physical::Vit] = readPhysical("vit");
        physical_[Physical::Min] = readPhysical("min");
    };
    abilityNode.onTag["Mental"] = [this](XNode& mentalNode) {
        ::cwxeditor::cwx::perf::TraceScope trace_race_393(__FILE__, __LINE__);
        mental_[Mental::Aggressive] =
            mentalNode.attr<double>("aggressive", false, 0.0);
        mental_[Mental::Cheerful] =
            mentalNode.attr<double>("cheerful", false, 0.0);
        mental_[Mental::Brave] = mentalNode.attr<double>("brave", false, 0.0);
        mental_[Mental::Cautious] =
            mentalNode.attr<double>("cautious", false, 0.0);
        mental_[Mental::Trickish] =
            mentalNode.attr<double>("trickish", false, 0.0);
    };
    abilityNode.onTag["Enhance"] = [this](XNode& enhanceNode) {
        ::cwxeditor::cwx::perf::TraceScope trace_race_400(__FILE__, __LINE__);
        defaultEnhance_[Enhance::Avoid] =
            enhanceNode.attr<std::int32_t>("avoid", false, 0);
        defaultEnhance_[Enhance::Resist] =
            enhanceNode.attr<std::int32_t>("resist", false, 0);
        defaultEnhance_[Enhance::Defense] =
            enhanceNode.attr<std::int32_t>("defense", false, 0);
    };
    abilityNode.parse();
}

Race::Race() = default;

lib::Owner<Race> Race::fromNode(const CProps& prop, XNode& node,
                                const XMLInfo& ver) {
    ::cwxeditor::cwx::perf::TraceScope trace_race_34(__FILE__, __LINE__);
    auto result = lib::Owner<Race>(new Race());
    // 解析が例外で終わるとXNodeはこれらのハンドラーを保持する。スタック参照をキ
    // ャプチャせず、DのGCクロージャーと同様にローカルの結果と存在状態を所有する。
    auto nameFound = std::make_shared<bool>(false);
    const auto versionSnapshot = ver;
    const auto physicalNormal = prop.looks().physicalNormal();

    node.onTag["Name"] = [result, nameFound](XNode& nameNode) {
        result->name_ = nameNode.value();
        *nameFound = true;
    };
    node.onTag["Description"] = [result](XNode& descriptionNode) {
        result->desc_ = utils::decodeLf2(descriptionNode.value());
    };
    // 保持された再試行ハンドラーに必要な入力を、値として生存させる。CPropsはコピー
    // 不可なので、RaceのAbilityが使う一つのLooks値だけをキャプチャする。
    node.onTag["Feature"] = [result, versionSnapshot](XNode& featureNode) {
        result->loadFeature(featureNode, versionSnapshot);
    };
    node.onTag["Ability"] = [result, versionSnapshot, physicalNormal](XNode& abilityNode) {
        result->loadAbility(physicalNormal, abilityNode, versionSnapshot);
    };
    node.onTag[String(Coupon::XML_NAME_M)] = [result, versionSnapshot](XNode& couponsNode) {
        ::cwxeditor::cwx::perf::TraceScope trace_race_41(__FILE__, __LINE__);
        couponsNode.onTag[String(Coupon::XML_NAME)] = [result, versionSnapshot](XNode& couponNode) {
            ::cwxeditor::cwx::perf::TraceScope trace_race_42(__FILE__, __LINE__);
            auto value = Coupon::fromNode(couponNode, versionSnapshot);
            if (!value) throw RaceException("Could not create race coupon.");
            value->owner(std::static_pointer_cast<CouponsOwner>(result));
            lib::appendToSlice(result->coupons_, std::move(value));
        };
        couponsNode.parse();
    };
    node.onTag["Coefficient"] = [result](XNode& coefficientNode) {
        ::cwxeditor::cwx::perf::TraceScope trace_race_49(__FILE__, __LINE__);
        result->levelCoefficient_ =
            coefficientNode.attr<double>("level", false, 1.0);
        result->epPerLevel_ =
            coefficientNode.attr<std::uint32_t>("ep", false,
                                                 DEFAULT_EP_PER_LEVEL);
    };

    node.parse();
    // Dは_nameをnullで初期化し、スライスの同一性を調べる。XNodeは<Name/>
    // に非nullの空値を返すため、長さではなく存在を判定する必要がある。ネ
    // イティブ文字列にはnull状態がないので、この違いを明示的に保持する。
    if (!*nameFound)
        throw RaceException("Race name not found.");
    return result;
}

String Race::name() const { return name_; }
String Race::desc() const { return desc_; }
double Race::levelCoefficient() const noexcept { return levelCoefficient_; }
std::uint32_t Race::epPerLevel() const noexcept { return epPerLevel_; }

lib::SharedSlice<lib::Owner<Coupon>> Race::coupons() { return coupons_; }
lib::ConstOwnerSlice<Coupon> Race::coupons() const {
    return lib::ConstOwnerSlice<Coupon>(coupons_);
}

// このテンプレートはここではRaceが使い、具体的なCastCardの利用箇所にも使える。
template class RaceParam<false>;
template class RaceParam<true>;

}  // namespace cwxeditor::cwx::race
