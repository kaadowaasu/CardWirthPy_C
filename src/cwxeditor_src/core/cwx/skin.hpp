#pragma once

// cwx/skin.dのC++20移植。ここではパスとXMLを
// UTF-8のまま扱い、 ファイルシステムとの変換はskin.cpp
// と共通のutils/imagesizeへ限定する。

#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/coupon.hpp"
#include "cwxeditor_src/core/cwx/features.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/race.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/system.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"
#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/shared_dictionary.hpp"
#include "cwxeditor_src/lib/array_slice.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace cwxeditor::cwx::card { class ActionCard; }

namespace cwxeditor::cwx::skin {

using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;
using XNode = ::cwxeditor::cwx::xml::XNode;
using CProps = ::cwxeditor::cwx::props::CProps;
using System = ::cwxeditor::cwx::system::System;
using XMLInfo = ::cwxeditor::cwx::system::XMLInfo;
using Coupon = ::cwxeditor::cwx::coupon::Coupon;
using Race = ::cwxeditor::cwx::race::Race;
using Sex = ::cwxeditor::cwx::features::Sex;
using Period = ::cwxeditor::cwx::features::Period;
using Nature = ::cwxeditor::cwx::features::Nature;
using Makings = ::cwxeditor::cwx::features::Makings;
using CouponType = ::cwxeditor::cwx::features::CouponType;
using ActionCardType = ::cwxeditor::cwx::types::ActionCardType;
using Physical = ::cwxeditor::cwx::types::Physical;
using Mental = ::cwxeditor::cwx::types::Mental;
using Enhance = ::cwxeditor::cwx::types::Enhance;
using Mentality = ::cwxeditor::cwx::types::Mentality;
using Aptitude = ::cwxeditor::cwx::system::Aptitude;
using BgImage = ::cwxeditor::cwx::background::BgImage;
using BgImageArray = ::cwxeditor::cwx::background::BgImageArray;
using BgImageS = ::cwxeditor::cwx::structs::BgImageS;
using ClassicEngine = ::cwxeditor::cwx::structs::ClassicEngine;
using SkinTypeByClassicEngine = ::cwxeditor::cwx::structs::SkinTypeByClassicEngine;
using SoundFontWithVolume = ::cwxeditor::cwx::structs::SoundFontWithVolume;

// カード側の実装への接続を分離し、生成済みのカードヘッダーや Card
// とSkinの循環依存なしにSkinを使えるようにする。
namespace detail {
template <typename>
inline constexpr bool dependent_false = false;
[[nodiscard]] ::cwxeditor::lib::Owner<::cwxeditor::cwx::card::ActionCard>
actionCardFromNode(XNode& node, const XMLInfo& version);
[[nodiscard]] ActionCardType actionCardType(
    const ::cwxeditor::cwx::card::ActionCard& card);
[[nodiscard]] String actionCardName(
    const ::cwxeditor::cwx::card::ActionCard& card, const System& system);
[[nodiscard]] Aptitude actionCardAptitude(
    const ::cwxeditor::cwx::card::ActionCard& card);
[[nodiscard]] String warningMessage(const props::Msgs& messages,
                                    StringView key);
}  // namespace detail

[[nodiscard]] ::cwxeditor::lib::Array<BgImageS>
createBgImageSs(const BgImageArray& bgs);
[[nodiscard]] BgImageArray createBgImages(
    const class Skin& skin, const ::cwxeditor::lib::Array<BgImageS>& bgs);
[[nodiscard]] String skinTypeByClassicEngine(
    const ::cwxeditor::lib::Array<SkinTypeByClassicEngine>& info,
    StringView enginePath, StringView defaultType);

enum class MaskType : std::int32_t {
    NoMask = 0, NormalMask = 1, RightMask = 2, Mask1_1 = 3,
};

class ModData {
public:
    ModData() = default;
    [[nodiscard]] const String& name() const noexcept { return name_; }
    [[nodiscard]] bool special() const noexcept { return special_; }
    [[nodiscard]] const ::cwxeditor::lib::Dictionary<Physical, std::int32_t>&
    physical() const noexcept { return physical_; }
    [[nodiscard]] const ::cwxeditor::lib::Dictionary<Mental, double>&
    mental() const noexcept { return mental_; }
    [[nodiscard]] const ::cwxeditor::lib::Array<::cwxeditor::lib::Owner<Coupon>>&
    coupons() const noexcept { return coupons_; }
    [[nodiscard]] static ::cwxeditor::lib::Owner<ModData> fromNode(
        XNode& node, const XMLInfo& version);

private:
    String name_;
    bool special_ = false;
    ::cwxeditor::lib::Dictionary<Physical, std::int32_t> physical_;
    ::cwxeditor::lib::Dictionary<Mental, double> mental_;
    ::cwxeditor::lib::Array<::cwxeditor::lib::Owner<Coupon>> coupons_;
};

class Skin : public std::enable_shared_from_this<Skin> {
public:
    using Ptr = ::cwxeditor::lib::Owner<Skin>;
    using Table = ::cwxeditor::lib::SharedDictionary<String, Ptr>;
    using FeatureData = ::cwxeditor::lib::Dictionary<Sex, ::cwxeditor::lib::Owner<ModData>>;
    using PeriodData = ::cwxeditor::lib::Dictionary<Period, ::cwxeditor::lib::Owner<ModData>>;
    using NatureData = ::cwxeditor::lib::Dictionary<Nature, ::cwxeditor::lib::Owner<ModData>>;
    using MakingsData = ::cwxeditor::lib::Dictionary<Makings, ::cwxeditor::lib::Owner<ModData>>;
    using ActionCards = ::cwxeditor::lib::Dictionary<ActionCardType,
        ::cwxeditor::lib::Owner<::cwxeditor::cwx::card::ActionCard>>;

    static Ptr find(const CProps& prop, StringView enginePath, StringView type,
                    StringView scenarioPath, bool legacy,
                    StringView classicEngineRegex, StringView classicDataDirRegex,
                    StringView classicMatchKey,
                    const ::cwxeditor::lib::Array<ClassicEngine>& engines,
                    StringView defaultSkin);
    static Table table(const CProps& prop, StringView enginePath);
    static Ptr emptySkin(const CProps& prop, StringView enginePath, StringView type);
    static Ptr createLegacySkin(const CProps& prop, StringView enginePath,
        StringView legacyEnginePath, StringView dataDirName, StringView execute,
        const ::cwxeditor::lib::Array<ClassicEngine>& engines, StringView defaultSkin);
    static Ptr findLegacySkin(const CProps& prop, StringView enginePath,
        StringView scenarioPath, StringView classicEngineRegex,
        StringView classicDataDirRegex, StringView classicMatchKey,
        const ::cwxeditor::lib::Array<ClassicEngine>& engines, StringView defaultSkin);
    [[nodiscard]] static String findResDir(StringView path,
        StringView classicDataDirRegex, StringView classicMatchKey);
    static bool hasClassicEngine(StringView path, String& resourceDir,
        String& enginePath, StringView classicEngineRegex,
        StringView classicDataDirRegex, StringView classicMatchKey,
        const ::cwxeditor::lib::Array<ClassicEngine>& engines);
    static bool findLegacy(StringView scenarioPath, String& resourceDir,
        String& enginePath, StringView classicEngineRegex,
        StringView classicDataDirRegex, StringView classicMatchKey,
        const ::cwxeditor::lib::Array<ClassicEngine>& engines);
    static String findCardWirthPy(StringView scenarioPath, StringView exeName,
                                  StringView dataName);

    explicit Skin(const CProps& prop, StringView enginePath);

    [[nodiscard]] const String& name() const noexcept { return name_; }
    [[nodiscard]] String type() const;
    [[nodiscard]] const String& skinFile() const noexcept { return skin_file_; }
    [[nodiscard]] const String& path() const noexcept { return path_; }
    [[nodiscard]] bool sourceOfMaterialsIsClassicEngine() const noexcept {
        return source_of_materials_is_classic_engine_;
    }
    [[nodiscard]] bool isEmpty() const noexcept { return path_.empty() && legacy_path_.empty(); }

    [[nodiscard]] String resSummary(MaskType& mask) const;
    [[nodiscard]] String resMenuCard(MaskType& mask) const;
    [[nodiscard]] String resCastCard(MaskType& mask) const;
    [[nodiscard]] String resCastCardInjury(MaskType& mask) const;
    [[nodiscard]] String resCastCardDanger(MaskType& mask) const;
    [[nodiscard]] String resCastCardFaint(MaskType& mask) const;
    [[nodiscard]] String resCastCardBind(MaskType& mask) const;
    [[nodiscard]] String resCastCardParaly(MaskType& mask) const;
    [[nodiscard]] String resCastCardPetrif(MaskType& mask) const;
    [[nodiscard]] String resCastCardSleep(MaskType& mask) const;
    [[nodiscard]] String resLifeBar(MaskType& mask) const;
    [[nodiscard]] String resLifeGuage(MaskType& mask) const;
    [[nodiscard]] String resLifeGuage2(MaskType& mask) const;
    [[nodiscard]] String resLifeGuage2Mask(MaskType& mask) const;
    [[nodiscard]] String resEnhanceUp(MaskType& mask, Enhance value) const;
    [[nodiscard]] String resEnhanceDown(MaskType& mask, Enhance value) const;
    [[nodiscard]] String resMentality(MaskType& mask, Mentality value) const;
    [[nodiscard]] String resBind(MaskType& mask) const;
    [[nodiscard]] String resSilence(MaskType& mask) const;
    [[nodiscard]] String resFaceUp(MaskType& mask) const;
    [[nodiscard]] String resAntiMagic(MaskType& mask) const;
    [[nodiscard]] String resParalyze(MaskType& mask) const;
    [[nodiscard]] String resPoison(MaskType& mask) const;
    [[nodiscard]] String resSummon(MaskType& mask) const;
    [[nodiscard]] String resItemCard(MaskType& mask) const;
    [[nodiscard]] String resSkillCard(MaskType& mask) const;
    [[nodiscard]] String resBeastCard(MaskType& mask) const;
    [[nodiscard]] String resOptionCard(MaskType& mask) const;
    [[nodiscard]] String resInfoCard(MaskType& mask) const;
    [[nodiscard]] String resCardHold(MaskType& mask) const;
    [[nodiscard]] String resCardPenalty(MaskType& mask) const;
    [[nodiscard]] String resRare(MaskType& mask) const;
    [[nodiscard]] String resPremier(MaskType& mask) const;
    [[nodiscard]] String resAptVeryHigh(MaskType& mask) const;
    [[nodiscard]] String resAptHigh(MaskType& mask) const;
    [[nodiscard]] String resAptNormal(MaskType& mask) const;
    [[nodiscard]] String resAptLow(MaskType& mask) const;
    [[nodiscard]] String resUse0(MaskType& mask) const;
    [[nodiscard]] String resUse1(MaskType& mask) const;
    [[nodiscard]] String resUse2(MaskType& mask) const;
    [[nodiscard]] String resUse3(MaskType& mask) const;
    [[nodiscard]] String resUse4(MaskType& mask) const;

    [[nodiscard]] String engine() const;
    [[nodiscard]] const String& legacyEngine() const noexcept { return legacy_engine_; }
    [[nodiscard]] String executeEngine() const;
    [[nodiscard]] String legacyName() const;
    [[nodiscard]] const String& legacyDataPath() const noexcept { return legacy_path_; }
    [[nodiscard]] bool legacy() const noexcept { return legacy_; }
    [[nodiscard]] ::cwxeditor::lib::Dictionary<String, String> loadEngineSettings() const;
    [[nodiscard]] ::cwxeditor::lib::Array<SoundFontWithVolume>
    loadSoundFonts(StringView defaultSoundFont) const;
    [[nodiscard]] bool useLegacyRes() const noexcept {
#ifdef _WIN32
        return !legacy_engine_.empty();
#else
        return false;
#endif
    }
    [[nodiscard]] String materialPath() const { return legacy_ ? String{} : String("Material"); }
    [[nodiscard]] bool isMaterial(StringView path, bool check = false) const;
    [[nodiscard]] bool isSE(StringView path, bool check = false) const;
    static ::cwxeditor::lib::Array<String> warningSE(const CProps& prop,
        StringView path, bool legacy, StringView targetVersion);
    [[nodiscard]] bool isBGM(StringView path, bool check = false) const;
    static ::cwxeditor::lib::Array<String> warningBGM(const CProps& prop,
        StringView path, bool legacy, StringView targetVersion);
    [[nodiscard]] bool isCardImage(StringView path, bool ignoreSize, bool included) const;
    [[nodiscard]] bool isBgImage(StringView path, bool excludeCardSize = false,
                                 bool check = false) const;
    static ::cwxeditor::lib::Array<String> warningImage(const CProps& prop,
        StringView path, bool legacy, bool includeType, StringView targetVersion);
    [[nodiscard]] bool canInclude(StringView path) const;
    [[nodiscard]] const ::cwxeditor::lib::Dictionary<char32_t, String>&
    spChars() const noexcept { return sp_chars_; }

    [[nodiscard]] ::cwxeditor::lib::Array<String> wsnTableDirs(StringView version) const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> wsnMusicDirs(StringView version) const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> wsnSoundDirs(StringView version) const;
    [[nodiscard]] bool hasCardImage(StringView dir, bool forceRefresh,
                                     bool ignoreSize, bool included) const;
    [[nodiscard]] bool hasBgImage(StringView dir, bool forceRefresh,
                                   bool excludeCardSize) const;
    [[nodiscard]] bool hasBGM(StringView dir, bool forceRefresh) const;
    [[nodiscard]] bool hasSE(StringView dir, bool forceRefresh) const;
    [[nodiscard]] bool hasWsnCardImage(StringView version, bool forceRefresh,
                                       bool ignoreSize) const;
    [[nodiscard]] bool hasWsnBgImage(StringView version, bool forceRefresh,
                                     bool excludeCardSize) const;
    [[nodiscard]] bool hasWsnBGM(StringView version, bool forceRefresh) const;
    [[nodiscard]] bool hasWsnSE(StringView version, bool forceRefresh) const;

    [[nodiscard]] ::cwxeditor::lib::Array<String> cards(StringView dir,
        bool logicalSort, bool forceRefresh, bool ignoreSize, bool included) const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> tables(bool logicalSort,
        bool forceRefresh = false, bool excludeCardSize = false) const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> tables(StringView dir,
        bool logicalSort, bool forceRefresh, bool excludeCardSize) const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> musics(bool logicalSort,
        bool forceRefresh = false) const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> musics(StringView dir,
        bool logicalSort, bool forceRefresh) const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> sounds(bool logicalSort,
        bool forceRefresh = false) const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> sounds(StringView dir,
        bool logicalSort, bool forceRefresh) const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> wsnCards(StringView version,
        bool logicalSort, bool forceRefresh, bool ignoreSize) const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> wsnTables(StringView version,
        bool logicalSort, bool forceRefresh, bool excludeCardSize) const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> wsnMusics(StringView version,
        bool logicalSort, bool forceRefresh) const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> wsnSounds(StringView version,
        bool logicalSort, bool forceRefresh) const;

    [[nodiscard]] String resDir() const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> tableDirs() const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> bgmDirs() const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> seDirs() const;
    [[nodiscard]] String resourceDir() const;
    [[nodiscard]] const ::cwxeditor::lib::Array<String>& extImage() const noexcept;
    [[nodiscard]] const ::cwxeditor::lib::Array<String>& extBgm() const noexcept;
    [[nodiscard]] const ::cwxeditor::lib::Array<String>& extSound() const noexcept;
    // Dはinoutのスライス記述子を返す。コピーした参照は要素の格納先
    // を共有するが、 構造の変更ではコピー側の記述子だけを付け替える。
    [[nodiscard]] ::cwxeditor::lib::SharedArray<::cwxeditor::lib::Owner<Race>>
    races() noexcept {
        return races_;
    }
    [[nodiscard]] ::cwxeditor::lib::ConstOwnerSlice<Race>
    races() const noexcept { return ::cwxeditor::lib::ConstOwnerSlice<Race>(races_); }
    [[nodiscard]] String defBattle(StringView scenarioPath, StringView wsnVersion) const;
    [[nodiscard]] String findImagePathF(StringView path, StringView scenarioPath,
        StringView wsnVersion, bool& isSkinMaterial, bool& isEngineMaterial) const;
    [[nodiscard]] String findImagePath(StringView path, StringView scenarioPath,
                                       StringView wsnVersion) const;
    [[nodiscard]] String findImagePath(StringView path, StringView scenarioPath,
                                       StringView wsnVersion, std::size_t& defaultIndex) const;
    [[nodiscard]] String findPathF(StringView path,
        const ::cwxeditor::lib::Array<String>& extensions,
        const ::cwxeditor::lib::Array<String>& defaultDirs, StringView scenarioPath,
        StringView wsnVersion, const ::cwxeditor::lib::Array<String>& wsnDirs,
        bool& isSkinMaterial, bool& isEngineMaterial) const;
    [[nodiscard]] String findPathF(StringView path,
        const ::cwxeditor::lib::Array<String>& extensions,
        const ::cwxeditor::lib::Array<String>& defaultDirs, StringView scenarioPath,
        StringView wsnVersion, const ::cwxeditor::lib::Array<String>& wsnDirs,
        bool& isSkinMaterial, bool& isEngineMaterial, std::size_t& defaultIndex) const;
    [[nodiscard]] String findPath(StringView path,
        const ::cwxeditor::lib::Array<String>& extensions,
        const ::cwxeditor::lib::Array<String>& defaultDirs, StringView scenarioPath,
        StringView wsnVersion, const ::cwxeditor::lib::Array<String>& wsnDirs) const;
    [[nodiscard]] bool isSkinResource(StringView path,
        const ::cwxeditor::lib::Array<String>& extensions,
        const ::cwxeditor::lib::Array<String>& defaultDirs, StringView scenarioPath) const;

    [[nodiscard]] String evtChildOK() const;
    [[nodiscard]] ::cwxeditor::lib::Array<Sex> allSexes() const;
    [[nodiscard]] ::cwxeditor::lib::Array<Period> allPeriods() const;
    [[nodiscard]] ::cwxeditor::lib::Array<Nature> allNatures() const;
    [[nodiscard]] ::cwxeditor::lib::Array<Nature> normalNatures() const;
    [[nodiscard]] ::cwxeditor::lib::Array<Nature> extraNatures() const;
    [[nodiscard]] ::cwxeditor::lib::Array<Makings> allMakings() const;
    [[nodiscard]] ::cwxeditor::lib::Array<Makings> leftMakings() const;
    [[nodiscard]] ::cwxeditor::lib::Array<Makings> rightMakings() const;
    [[nodiscard]] Makings reverseMakings(Makings value) const;
    [[nodiscard]] String sexName(Sex value) const;
    [[nodiscard]] String periodName(Period value) const;
    [[nodiscard]] String natureName(Nature value) const;
    [[nodiscard]] String makingsName(Makings value) const;
    template <typename Feature>
    [[nodiscard]] String featureName(Feature value) const {
        if constexpr (std::is_same_v<Feature, Sex>) return sexName(value);
        else if constexpr (std::is_same_v<Feature, Period>) return periodName(value);
        else if constexpr (std::is_same_v<Feature, Nature>) return natureName(value);
        else if constexpr (std::is_same_v<Feature, Makings>) return makingsName(value);
        else static_assert(detail::dependent_false<Feature>, "invalid feature type");
    }
    [[nodiscard]] String sexCoupon(Sex value) const;
    [[nodiscard]] String periodCoupon(Period value) const;
    [[nodiscard]] String natureCoupon(Nature value) const;
    [[nodiscard]] String makingsCoupon(Makings value) const;

    template <typename Feature>
    [[nodiscard]] std::int32_t physicalMod(Feature value, Physical physical) const;
    template <typename Feature>
    [[nodiscard]] double mentalMod(Feature value, Mental mental) const;
    [[nodiscard]] ::cwxeditor::lib::Array<::cwxeditor::lib::Owner<Coupon>>
    periodInitialCoupons(const System& system, Period value) const;
    [[nodiscard]] String number1Coupon(const System& system) const;
    [[nodiscard]] String replaceSystemCouponName(const System& system,
                                                  StringView coupon) const;
    [[nodiscard]] ::cwxeditor::lib::Owner<::cwxeditor::cwx::card::ActionCard>
    actionCard(ActionCardType type) const;
    [[nodiscard]] String actionCardName(const System& system,
                                        ActionCardType type) const;
    [[nodiscard]] Aptitude actionCardAptitude(const System& system,
                                              ActionCardType type) const;
    [[nodiscard]] ::cwxeditor::lib::Array<ActionCardType> actionCardTypes() const;
    void initialize();
    void loadFromXML(const CProps& prop, StringView filename, const XMLInfo& version);
    void loadFromXML(const CProps& prop, StringView filename,
                     ::cwxeditor::lib::Owner<const XMLInfo> version);

private:
    Skin(const CProps& prop, StringView skinFile, StringView enginePath);
    static bool canAccess(StringView file);
    void setupLegacy(StringView legacyEnginePath, StringView resourceDir,
                     const ClassicEngine& engine);
    [[nodiscard]] static String findResource(StringView dir, StringView name,
        const ::cwxeditor::lib::Array<String>& extensions);
    [[nodiscard]] ::cwxeditor::lib::Array<String> wsnResDirs(
        const ::cwxeditor::lib::Array<String>& names, StringView version) const;
    [[nodiscard]] ::cwxeditor::lib::Array<String> list(
        const ::cwxeditor::lib::Array<String>& dirs, bool logicalSort,
        bool forceRefresh, int kind, bool flag, bool flag2, bool sort = true) const;
    [[nodiscard]] ::cwxeditor::lib::SharedSlice<String> listImpl(
        StringView dir, bool forceRefresh, int kind, bool flag, bool flag2) const;
    [[nodiscard]] bool has(StringView dir, bool forceRefresh, int kind,
                            bool flag, bool flag2) const;
    template <typename Feature>
    [[nodiscard]] const ModData* featureData(Feature value) const;
    template <typename Feature>
    [[nodiscard]] const auto* classicPhysicalData() const;
    template <typename Feature>
    [[nodiscard]] const auto* classicMentalData() const;
    template <typename Feature>
    [[nodiscard]] std::int32_t systemPhysicalMod(Feature value, Physical physical) const;
    template <typename Feature>
    [[nodiscard]] double systemMentalMod(Feature value, Mental mental) const;
    void resetLoadedData();

    // 共有セッションのキャッシュは、バックグラウンドの準備中にも参照される。
    // Dは表示スレッドで初期化するが、C++では初回の読み込みを直列化する。
    std::mutex initialization_mutex_;
    // Dのhas/listImplは同じ再入可能なオブジェクトモニターを使う。
    mutable std::recursive_mutex resource_cache_mutex_;
    bool legacy_ = false;
    String legacy_path_, legacy_engine_, execute_;
    ::cwxeditor::lib::Owner<::cwxeditor::cwx::props::detail::SkinContext>
        context_;
    String engine_path_;
    ClassicEngine classic_engine_;
    FeatureData sexes_;
    PeriodData periods_;
    NatureData natures_;
    MakingsData makings_;
    String number1_coupon_;
    ActionCards action_cards_;
    inline static const ::cwxeditor::lib::Array<String> ext_img_{
        ".bmp", ".jpg", ".jpeg", ".png", ".apng", ".gif", ".webp",
        ".jpy1", ".jpdc", ".jptx"};
    inline static const ::cwxeditor::lib::Array<String> ext_bgm_{
        ".aiff", ".mid", ".midi", ".mod", ".s3m", ".xm", ".it", ".mt2",
        ".669", ".med", ".mp3", ".ogg", ".ogv", ".oga", ".ogx", ".voc", ".wav"};
    inline static const ::cwxeditor::lib::Array<String> ext_sound_ = ext_bgm_;
    String path_, skin_file_, name_, type_, author_, desc_;
    bool source_of_materials_is_classic_engine_ = false;
    ::cwxeditor::lib::Dictionary<char32_t, String> sp_chars_;
    ::cwxeditor::lib::SharedArray<::cwxeditor::lib::Owner<Race>> races_;
    // 遅延読み込み用にDのXMLInfo参照を保持する。ネイティブの値
    // オーバーロードでは 一時値も渡せるため所有コピーを作り、Owner
    // 版では共有コンテキストへの変更が他の参照にも見えるようにする。
    ::cwxeditor::lib::Owner<const XMLInfo> version_;
};

template <typename Feature>
inline const ModData* Skin::featureData(Feature value) const {
    if constexpr (std::is_same_v<Feature, Sex>) {
        const auto found = sexes_.find(value);
        return found == sexes_.end() || !found->second ? nullptr : found->second.get();
    } else if constexpr (std::is_same_v<Feature, Period>) {
        const auto found = periods_.find(value);
        return found == periods_.end() || !found->second ? nullptr : found->second.get();
    } else if constexpr (std::is_same_v<Feature, Nature>) {
        const auto found = natures_.find(value);
        return found == natures_.end() || !found->second ? nullptr : found->second.get();
    } else if constexpr (std::is_same_v<Feature, Makings>) {
        const auto found = makings_.find(value);
        return found == makings_.end() || !found->second ? nullptr : found->second.get();
    } else {
        static_assert(detail::dependent_false<Feature>, "invalid feature type");
    }
}

template <typename Feature>
inline const auto* Skin::classicPhysicalData() const {
    if constexpr (std::is_same_v<Feature, Sex>) return &classic_engine_.physicalModSex;
    else if constexpr (std::is_same_v<Feature, Period>) return &classic_engine_.physicalModPeriod;
    else if constexpr (std::is_same_v<Feature, Nature>) return &classic_engine_.physicalModNature;
    else if constexpr (std::is_same_v<Feature, Makings>) return &classic_engine_.physicalModMakings;
    else { static_assert(detail::dependent_false<Feature>, "invalid feature type"); }
}

template <typename Feature>
inline const auto* Skin::classicMentalData() const {
    if constexpr (std::is_same_v<Feature, Sex>) return &classic_engine_.mentalModSex;
    else if constexpr (std::is_same_v<Feature, Period>) return &classic_engine_.mentalModPeriod;
    else if constexpr (std::is_same_v<Feature, Nature>) return &classic_engine_.mentalModNature;
    else if constexpr (std::is_same_v<Feature, Makings>) return &classic_engine_.mentalModMakings;
    else { static_assert(detail::dependent_false<Feature>, "invalid feature type"); }
}

template <typename Feature>
inline std::int32_t Skin::systemPhysicalMod(Feature value, Physical physical) const {
    const auto table = context_->system->template physicalMod<Feature>(legacyName());
    const auto outer = table.find(value);
    if (outer == table.end()) return 0;
    const auto inner = outer->second.find(physical);
    return inner == outer->second.end() ? 0 : inner->second;
}

template <typename Feature>
inline double Skin::systemMentalMod(Feature value, Mental mental) const {
    const auto table = context_->system->template mentalMod<Feature>(legacyName());
    const auto outer = table.find(value);
    if (outer == table.end()) return 0.0;
    const auto inner = outer->second.find(mental);
    return inner == outer->second.end() ? 0.0 : inner->second;
}

template <typename Feature>
inline std::int32_t Skin::physicalMod(Feature value, Physical physical) const {
    if (const auto* data = featureData(value)) {
        const auto found = data->physical().find(physical);
        if (found != data->physical().end()) return found->second;
    }
    if (const auto* table = classicPhysicalData<Feature>()) {
        const auto outer = table->find(value);
        if (outer != table->end()) {
            const auto inner = outer->second.find(physical);
            if (inner != outer->second.end()) return inner->second;
        }
    }
    return systemPhysicalMod(value, physical);
}

template <typename Feature>
inline double Skin::mentalMod(Feature value, Mental mental) const {
    if (mental == Mental::Unaggressive || mental == Mental::Uncheerful ||
        mental == Mental::Unbrave || mental == Mental::Uncautious ||
        mental == Mental::Untrickish) {
        return -mentalMod(value, ::cwxeditor::cwx::types::reverseMental(mental));
    }
    if (const auto* data = featureData(value)) {
        const auto found = data->mental().find(mental);
        if (found != data->mental().end()) return found->second;
    }
    if (const auto* table = classicMentalData<Feature>()) {
        const auto outer = table->find(value);
        if (outer != table->end()) {
            const auto inner = outer->second.find(mental);
            if (inner != outer->second.end()) return inner->second;
        }
    }
    return systemMentalMod(value, mental);
}

}  // namespace cwxeditor::cwx::skin
