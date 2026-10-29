#pragma once

// cwx/cwl.dのC++20移植。Classic形式
// は明示的なバイナリー幅とShift-JIS文字列を使う
// 。 クラス参照はC++との境界で共有所有として表す。

#include "cwxeditor_src/core/cwx/binary.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <cstdint>
#include <functional>
#include <string_view>

namespace cwxeditor::cwx {
namespace area { class Area; class Battle; class Package; }
namespace background { class BgImage; }
namespace card {
class CastCard; class EffectCard; class InfoCard; class ItemCard;
class SkillCard; class BeastCard; class Card; class CardImage;
}
namespace event { class Content; class EventTree; }
namespace filesync { class FileSync; }
namespace motion { class Motion; }
namespace path { class CWXPath; }
namespace props { class CProps; }
namespace skin { class Skin; }
namespace summary { class Summary; struct LoadOption; struct SaveOption; }

namespace cwl {

using Byte = lib::Byte;
using String = lib::String;
using StringView = lib::StringView;
using ByteIO = binary::ByteIO;
using CProps = props::CProps;
using Skin = skin::Skin;
using Summary = summary::Summary;
using LoadOption = summary::LoadOption;
using SaveOption = summary::SaveOption;
using FileSync = filesync::FileSync;
using CWXPath = path::CWXPath;
using Area = area::Area;
using Battle = area::Battle;
using Package = area::Package;
using CastCard = card::CastCard;
using EffectCard = card::EffectCard;
using SkillCard = card::SkillCard;
using ItemCard = card::ItemCard;
using BeastCard = card::BeastCard;
using InfoCard = card::InfoCard;
using Motion = motion::Motion;
using Content = event::Content;
using EventTree = event::EventTree;
using BgImage = background::BgImage;
using CardImage = card::CardImage;

using OwnerSummary = lib::Owner<Summary>;
using OwnerArea = lib::Owner<Area>;
using OwnerBattle = lib::Owner<Battle>;
using OwnerPackage = lib::Owner<Package>;
using OwnerCast = lib::Owner<CastCard>;
using OwnerSkill = lib::Owner<SkillCard>;
using OwnerItem = lib::Owner<ItemCard>;
using OwnerBeast = lib::Owner<BeastCard>;
using OwnerInfo = lib::Owner<InfoCard>;
using OwnerMotion = lib::Owner<Motion>;
using OwnerContent = lib::Owner<Content>;
using OwnerEventTree = lib::Owner<EventTree>;
using OwnerBgImage = lib::Owner<BgImage>;
using ConstOwnerBgImage = lib::Owner<const BgImage>;

enum class ResourceType : std::uint8_t {
    Unknown, Area, Battle, Package, Cast, Skill, Item, Beast, Info,
};

[[nodiscard]] bool sWith(StringView file, StringView prefix,
                          std::uint64_t& id);
[[nodiscard]] String encodePathLegacy(String path);
[[nodiscard]] String encodePathRes(const struct SData& d, String path,
                                   StringView resourceExtension,
                                   const lib::Array<String>& resourceDirs,
                                   const lib::Array<String>& resourceExtensions);
[[nodiscard]] String encodePathTable(const struct SData& d, String path);
[[nodiscard]] String encodePathMidi(const struct SData& d, String path);
[[nodiscard]] String encodePathWave(const struct SData& d, String path);
[[nodiscard]] String decodePathLegacy(String path);

struct RData {
    // ネイティブのCPropsはloadLScenarioの呼び出し元から借り、両ワーカーのjoin
    // まで呼び出し元が保持する。 コピーもCPropsを複製せず、同じコンテキストを共有する。
    const CProps* prop{};
    bool cardOnly{};
    String sPath;
    String skinType;
    String skinName;
    std::uint32_t numberStepToVariantThreshold{};
    std::int32_t dataVersion{};
    lib::Dictionary<String, std::uint32_t> numberSteps;
    lib::Dictionary<String, bool> stepNames;

    RData() = default;
    RData(const CProps& props, bool cardsOnly, String scenarioPath,
          String type, String name, std::uint32_t threshold);
    RData(const RData& source);
    RData(RData&&) = default;
    RData& operator=(const RData&) = default;
    RData& operator=(RData&&) = default;
};

[[nodiscard]] OwnerSummary loadLScenario(
    StringView path, StringView skinType, StringView skinName,
    const CProps& prop, const LoadOption& opt,
    lib::Array<String>& errorFiles, std::int32_t& dataVersion,
    StringView newName = {});
void loadComment(const OwnerSummary& summary);
void loadImageRef(const OwnerSummary& summary);
void loadCardRef(const OwnerSummary& summary);
void loadTemplate(const OwnerSummary& summary);

[[nodiscard]] ResourceType getType(StringView file, std::uint64_t& id);

[[nodiscard]] types::Target toTargetT(Byte b);
[[nodiscard]] types::Target toTargetA(Byte b);
[[nodiscard]] types::EffectType toEffectType(Byte b);
[[nodiscard]] types::Resist toResist(Byte b);
[[nodiscard]] types::CardVisual toCardVisual(Byte b);
[[nodiscard]] types::Range toRange(const RData& d, Byte b,
                                   Content& content,
                                   std::int32_t dataVersion);
[[nodiscard]] types::Range toRangeE(Byte b);
[[nodiscard]] types::Range toRangeEffectContent(Byte b);
[[nodiscard]] types::Range toKeyCodeRange(const RData& d, Byte b,
                                           Content& content,
                                           std::int32_t dataVersion);
[[nodiscard]] types::Range toCouponRange(Byte b);
[[nodiscard]] lib::Array<types::CastRange> toCastRanges(Byte b);
[[nodiscard]] types::EffectCardType toEffectCardType(Byte b);
[[nodiscard]] types::Comparison4 toComparison4(Byte b);
[[nodiscard]] types::Comparison3 toComparison3(Byte b);
[[nodiscard]] types::BorderingType toBorderingType(Byte b);
[[nodiscard]] types::BlendMode toBlendMode(Byte b, bool& mask);
[[nodiscard]] types::GradientDir toGradientDir(Byte b);
[[nodiscard]] types::CoordinateType toCoordinateType(Byte b);
[[nodiscard]] types::Status toStatus(Byte b);
[[nodiscard]] types::Element toElement(Byte b);
[[nodiscard]] types::DamageType toDamageType(Byte b);
[[nodiscard]] types::Physical toPhysical(std::uint32_t b);
[[nodiscard]] types::Mental toMental(std::int32_t b);
[[nodiscard]] types::Mentality toMentality(Byte b);
[[nodiscard]] types::CardTarget toCardTarget(Byte b);
[[nodiscard]] types::Premium toPremium(Byte b);

[[nodiscard]] bool readBool(ByteIO& f);
[[nodiscard]] String readExImage(const RData& d, ByteIO& f);
[[nodiscard]] String readImage(const RData& d, ByteIO& f);
[[nodiscard]] String readImageImpl(const RData& d, ByteIO& f,
                                   const std::function<std::uint32_t()>& readSize);
[[nodiscard]] String readExString(ByteIO& f, bool lns = false,
                                  bool cutText = false);
[[nodiscard]] String readString(ByteIO& f, bool lns = false,
                                bool cutText = false);
[[nodiscard]] String readStringImpl(ByteIO& f, bool lns, bool cutText,
                                    const std::function<std::uint32_t()>& readSize);
[[nodiscard]] String readString(ByteIO& f,
                                lib::Dictionary<String, String>& addInfo,
                                bool lns = false, bool cutText = false);
[[nodiscard]] lib::Array<String> readExStrings(ByteIO& f);
[[nodiscard]] lib::Array<String> readStrings(ByteIO& f);
[[nodiscard]] String readSound(const RData& d, ByteIO& f);

[[nodiscard]] OwnerSummary loadSummary(RData& d, ByteIO& f,
                                       std::uint64_t& startAreaId);
[[nodiscard]] OwnerMotion readMotion(RData& d, std::int32_t dataVersion,
                                     ByteIO& f, std::size_t index);
[[nodiscard]] OwnerContent readContent(RData& d, ByteIO& f,
                                       std::size_t index);
[[nodiscard]] OwnerEventTree readCEventTree(RData& d, ByteIO& f,
                                             std::size_t index);
[[nodiscard]] OwnerEventTree readEventTree(RData& d, ByteIO& f,
                                            bool enemyCard, std::size_t index);
[[nodiscard]] OwnerBgImage readBgImage(const RData& d, ByteIO& f, bool area,
                                       std::size_t index);
[[nodiscard]] lib::Array<OwnerBgImage> readBgImages(
    const RData& d, ByteIO& f, std::int32_t dataVersion, bool area,
    bool replaceBackground = false);
[[nodiscard]] lib::Array<OwnerBgImage> convInheritCellsR(
    const lib::Array<OwnerBgImage>& cells);
void readAreaHeader(RData& d, ByteIO& f, std::uint64_t& id, String& name);
[[nodiscard]] OwnerArea loadArea(RData& d, ByteIO& f, std::uint64_t fileId);
[[nodiscard]] OwnerBattle loadBattle(RData& d, ByteIO& f,
                                     std::uint64_t fileId);
[[nodiscard]] OwnerPackage loadPackage(RData& d, ByteIO& f,
                                       std::uint64_t fileId);
[[nodiscard]] OwnerCast loadCast(RData& d, ByteIO& f, std::uint64_t fileId);

template <typename CardT>
[[nodiscard]] lib::Owner<CardT> readEffCard(RData&, ByteIO&);

[[nodiscard]] OwnerSkill loadSkill(RData& d, ByteIO& f, std::uint64_t fileId);
[[nodiscard]] OwnerItem loadItem(RData& d, ByteIO& f, std::uint64_t fileId);
[[nodiscard]] OwnerBeast loadBeast(RData& d, ByteIO& f, std::uint64_t fileId);
[[nodiscard]] OwnerInfo loadInfo(RData& d, ByteIO& f, std::uint64_t fileId);
[[nodiscard]] String readPartyName(const CProps& prop, StringView wpl);
[[nodiscard]] bool isDebugYado(const CProps& prop, StringView yadoDir);

struct CommentKey {
    String path;
    bool event{};
    friend bool operator==(const CommentKey& a, const CommentKey& b) noexcept {
        return a.event == b.event && a.path == b.path;
    }
    friend bool operator<(const CommentKey& a, const CommentKey& b) noexcept {
        return a.path < b.path || (a.path == b.path && a.event < b.event);
    }
};

struct SData {
    const CProps* prop{};
    String sPath;
    const Skin* skin{};
    bool saveInnerImagePath{};
    bool logicalSort{};
    std::function<OwnerSkill(std::uint64_t)> skill;
    std::function<OwnerItem(std::uint64_t)> item;
    std::function<OwnerBeast(std::uint64_t)> beast;
    const SaveOption* opt{};
    lib::OrderedDictionary<CommentKey, String> comment;
    lib::Dictionary<String, String> imageRef;
    lib::Dictionary<String, std::uint64_t> cardRef;
    lib::Dictionary<String, std::uint32_t> maxNest;
    lib::Dictionary<std::uint64_t, std::uint32_t> nestCount;
    void merge(const SData& other);
};

void saveLScenario(const OwnerSummary& summary, StringView scenarioPath,
                   const Skin& skin, const CProps& prop, const SaveOption& opt,
                   FileSync* sync = nullptr);
[[nodiscard]] String saveComment(const SData& d);
[[nodiscard]] String saveImageRef(const SData& d);
[[nodiscard]] String saveCardRef(const SData& d);
[[nodiscard]] String saveTemplate(const Summary& summary);
void putExData(SData& d, const lib::Owner<CWXPath>& path);

[[nodiscard]] Byte fromTargetT(types::Target value);
[[nodiscard]] Byte fromTargetA(types::Target value);
[[nodiscard]] Byte fromEffectType(types::EffectType value);
[[nodiscard]] Byte fromResist(types::Resist value);
[[nodiscard]] Byte fromCardVisual(types::CardVisual value);
[[nodiscard]] Byte fromRange(types::Range value);
[[nodiscard]] Byte fromRangeEffectContent(types::Range value);
[[nodiscard]] Byte fromKeyCodeRange(types::Range value);
[[nodiscard]] Byte fromCouponRange(types::Range value);
[[nodiscard]] Byte fromCastRanges(const lib::Array<types::CastRange>& value);
[[nodiscard]] Byte fromEffectCardType(types::EffectCardType value);
[[nodiscard]] Byte fromComparison4(types::Comparison4 value);
[[nodiscard]] Byte fromComparison3(types::Comparison3 value);
[[nodiscard]] Byte fromBorderingType(types::BorderingType value);
[[nodiscard]] Byte fromBlendMode(types::BlendMode value, bool mask);
[[nodiscard]] Byte fromGradientDir(types::GradientDir value);
[[nodiscard]] Byte fromCoordinateType(types::CoordinateType value);
[[nodiscard]] Byte fromStatus(types::Status value);
[[nodiscard]] Byte fromElement(types::Element value);
[[nodiscard]] Byte fromDamageType(types::DamageType value);
[[nodiscard]] std::uint32_t fromPhysical(types::Physical value);
[[nodiscard]] std::int32_t fromMental(types::Mental value);
[[nodiscard]] Byte fromMentality(types::Mentality value);
[[nodiscard]] Byte fromCardTarget(types::CardTarget value);
[[nodiscard]] Byte fromPremium(types::Premium value);

inline constexpr StringView B_IMG_REF = ":INNER_BINARY_IMAGE";

void writeBool(ByteIO& f, bool value);
void writeExImage(SData& d, ByteIO& f, const lib::Owner<CWXPath>& path,
                  StringView imagePath);
void writeImage(SData& d, ByteIO& f, const lib::Owner<CWXPath>& path,
                StringView imagePath);
void writeImageImpl(SData& d, ByteIO& f, const lib::Owner<CWXPath>& path,
                    StringView imagePath,
                    const std::function<void(std::uint32_t)>& writeSize);
void writeExString(ByteIO& f, StringView value, bool lns = false,
                   bool cutText = false);
void writeString(ByteIO& f, StringView value, bool lns = false,
                 bool cutText = false);
void writeStringImpl(ByteIO& f, StringView value, bool lns, bool cutText,
                     const std::function<void(std::uint32_t)>& writeSize);
void writeExStrings(ByteIO& f, const lib::Array<String>& values);
void writeStrings(ByteIO& f, const lib::Array<String>& values);
void writeComments(SData& d, const lib::Owner<CWXPath>& path);
void writeSummary(SData& d, ByteIO& f, const Summary& summary);
void writeMotion(SData& d, ByteIO& f, const Motion& motion);
void writeContent(SData& d, ByteIO& f, const Content& content);
void writeCEventTree(SData& d, ByteIO& f, const EventTree& tree);
void writeEventTree(SData& d, ByteIO& f, const EventTree& tree);
void writeBgImage(SData& d, ByteIO& f, const BgImage& image);
void writeBgImages(SData& d, ByteIO& f,
                   const lib::Array<ConstOwnerBgImage>& images,
                   bool replaceBackground = false);
void writeBgImages(SData& d, ByteIO& f, const lib::Array<OwnerBgImage>& images,
                   bool replaceBackground = false);
[[nodiscard]] lib::Array<ConstOwnerBgImage> convInheritCellsW(
    const lib::Array<ConstOwnerBgImage>& cells);
[[nodiscard]] lib::Array<OwnerBgImage> convInheritCellsW(
    const lib::Array<OwnerBgImage>& cells);
void writeArea(SData& d, ByteIO& f, const Area& area);
void writeBattle(SData& d, ByteIO& f, const Battle& battle);
void writePackage(SData& d, ByteIO& f, const Package& package);
void writeCast(SData& d, ByteIO& f, const CastCard& card);
void writeEffCard(SData& d, ByteIO& f, const EffectCard& card, Byte type,
                  std::uint64_t id);
void writeSkill(SData& d, ByteIO& f, const SkillCard& card);
void writeItem(SData& d, ByteIO& f, const ItemCard& card);
void writeBeast(SData& d, ByteIO& f, const BeastCard& card);
void writeInfo(SData& d, ByteIO& f, const InfoCard& card);

}  // namespace cwl
}  // namespace cwxeditor::cwx
